#include "Engine/SceneCharacterMovement.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

namespace Engine {
    namespace {
        constexpr float Epsilon = 0.0001f;
        constexpr float SweepLift = 0.02f;
        constexpr float MaxFallSpeed = 80.0f;

        [[nodiscard]] bool finite(float value)
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool finite(const glm::vec3& value)
        {
            return finite(value.x) && finite(value.y) && finite(value.z);
        }

        [[nodiscard]] float clamp01(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        [[nodiscard]] glm::vec3 xz(const glm::vec3& value)
        {
            return {value.x, 0.0f, value.z};
        }

        [[nodiscard]] glm::vec3 normalizedOrZero(const glm::vec3& value)
        {
            const float lengthSquared = glm::length2(value);
            if (lengthSquared <= Epsilon * Epsilon || !finite(value)) {
                return {};
            }
            return value / std::sqrt(lengthSquared);
        }

        [[nodiscard]] glm::quat yawRotationForDirection(const glm::vec3& direction)
        {
            const glm::vec3 flat = normalizedOrZero(xz(direction));
            if (glm::length2(flat) <= Epsilon * Epsilon) {
                return {1.0f, 0.0f, 0.0f, 0.0f};
            }
            const float yaw = std::atan2(flat.x, flat.z);
            return glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 moveToward(
            const glm::vec3& current,
            const glm::vec3& target,
            float maxDelta)
        {
            const glm::vec3 delta = target - current;
            const float distance = glm::length(delta);
            if (distance <= maxDelta || distance <= Epsilon) {
                return target;
            }
            return current + delta / distance * maxDelta;
        }
    }

    SceneCharacterMovementSystem::SceneCharacterMovementSystem(Scene& scene, ScenePhysicsWorld& physics)
        : scene_(scene)
        , physics_(physics)
    {
    }

    SceneCharacterMovementSystem::~SceneCharacterMovementSystem()
    {
        for (uint32_t index = 0; index < characters_.size(); ++index) {
            if (characters_[index].occupied) {
                freeRecord(index);
            }
        }
    }

    void SceneCharacterMovementSystem::setNavigationService(SceneNavigationService* navigation)
    {
        navigation_ = navigation;
    }

    SceneCharacterHandle SceneCharacterMovementSystem::createCharacter(SceneCharacterDescriptor descriptor)
    {
        if (!scene_.contains(descriptor.actor) || scene_.parent(descriptor.actor).has_value()) {
            diagnostics_.warnings.push_back("Rejected character with invalid or parented actor.");
            return {};
        }
        if (!validDescriptor(descriptor)) {
            diagnostics_.warnings.push_back("Rejected character with invalid descriptor.");
            return {};
        }
        if (physics_.bodyForActor(descriptor.actor).has_value()) {
            diagnostics_.warnings.push_back("Rejected character because actor already has a physics body.");
            return {};
        }
        if (characterForActor(descriptor.actor).has_value()) {
            diagnostics_.warnings.push_back("Rejected duplicate character for actor.");
            return {};
        }

        ScenePhysicsBodyDescriptor bodyDescriptor;
        bodyDescriptor.actor = descriptor.actor;
        bodyDescriptor.motionType = ScenePhysicsMotionType::Kinematic;
        bodyDescriptor.enabled = descriptor.enabled;
        bodyDescriptor.layer = descriptor.physicsLayer;
        const ScenePhysicsBodyHandle body = physics_.createBody(bodyDescriptor);
        if (!isValid(body)) {
            diagnostics_.warnings.push_back("Failed to create character physics body.");
            return {};
        }

        const SceneColliderHandle collider = physics_.attachCollider(body, capsuleShape(descriptor));
        if (!isValid(collider)) {
            physics_.destroyBody(body);
            diagnostics_.warnings.push_back("Failed to create character capsule collider.");
            return {};
        }

        uint32_t slot = 0;
        for (; slot < characters_.size(); ++slot) {
            if (!characters_[slot].occupied) {
                break;
            }
        }
        if (slot == characters_.size()) {
            characters_.push_back({});
        }

        CharacterRecord& record = characters_[slot];
        record.generation = nextGeneration(record.generation);
        record.occupied = true;
        record.descriptor = std::move(descriptor);
        record.body = body;
        record.collider = collider;
        record.state.mode = record.descriptor.enabled ? SceneCharacterMovementMode::Falling : SceneCharacterMovementMode::Disabled;
        record.state.lastStatus = SceneCharacterMovementStatus::Success;
        record.state.lastMessage = "Character created.";
        refreshDiagnostics();
        return {slot, record.generation};
    }

    bool SceneCharacterMovementSystem::destroyCharacter(SceneCharacterHandle character)
    {
        CharacterRecord* target = record(character);
        if (!target) {
            return false;
        }
        freeRecord(character.index);
        refreshDiagnostics();
        return true;
    }

    bool SceneCharacterMovementSystem::contains(SceneCharacterHandle character) const
    {
        return record(character) != nullptr;
    }

    std::optional<SceneCharacterHandle> SceneCharacterMovementSystem::characterForActor(SceneActorHandle actor) const
    {
        for (uint32_t index = 0; index < characters_.size(); ++index) {
            const CharacterRecord& record = characters_[index];
            if (record.occupied && record.descriptor.actor == actor) {
                return SceneCharacterHandle{index, record.generation};
            }
        }
        return std::nullopt;
    }

    std::optional<SceneCharacterDescriptor> SceneCharacterMovementSystem::descriptor(SceneCharacterHandle character) const
    {
        const CharacterRecord* target = record(character);
        if (!target) {
            return std::nullopt;
        }
        return target->descriptor;
    }

    std::optional<SceneCharacterState> SceneCharacterMovementSystem::state(SceneCharacterHandle character) const
    {
        const CharacterRecord* target = record(character);
        if (!target) {
            return std::nullopt;
        }
        return target->state;
    }

    bool SceneCharacterMovementSystem::setEnabled(SceneCharacterHandle character, bool enabled)
    {
        CharacterRecord* target = record(character);
        if (!target) {
            return false;
        }
        target->descriptor.enabled = enabled;
        target->state.mode = enabled ? SceneCharacterMovementMode::Falling : SceneCharacterMovementMode::Disabled;
        physics_.setBodyEnabled(target->body, enabled);
        setRecordStatus(*target, SceneCharacterMovementStatus::Success, enabled ? "Character enabled." : "Character disabled.");
        refreshDiagnostics();
        return true;
    }

    bool SceneCharacterMovementSystem::setMoveInput(SceneCharacterHandle character, const SceneCharacterMoveInput& input)
    {
        CharacterRecord* target = record(character);
        if (!target || !finite(input.direction) ||
            (input.faceDirection.has_value() && !finite(*input.faceDirection)) || !finite(input.speedScale)) {
            return false;
        }
        target->input = input;
        target->input.speedScale = clamp01(target->input.speedScale);
        return true;
    }

    bool SceneCharacterMovementSystem::requestPathTo(SceneCharacterHandle character, const SceneCharacterPathRequest& request)
    {
        CharacterRecord* target = record(character);
        if (!target || !finite(request.goal) || !navigation_) {
            if (target) {
                setRecordStatus(*target, SceneCharacterMovementStatus::NavigationUnavailable, "Navigation service is unavailable.");
            }
            return false;
        }
        std::optional<glm::mat4> world = scene_.worldMatrix(target->descriptor.actor);
        if (!world.has_value()) {
            setRecordStatus(*target, SceneCharacterMovementStatus::InvalidActor, "Character actor transform is unavailable.");
            return false;
        }

        SceneCharacterPathRequest effective = request;
        effective.filter.allowPartialPath = request.allowPartialPath;
        ++diagnostics_.pathQueryCount;
        const glm::vec3 start = glm::vec3((*world)[3]);
        NavigationPathResult path =
            navigation_->findPathAcrossLoadedTiles(start, request.goal, request.agent, effective.filter);
        if (path.status != NavigationRuntimeStatus::Success || !path.path.complete || path.path.points.empty()) {
            target->state.activePath = {};
            target->state.activeWaypointIndex = 0;
            target->hasPathRequest = false;
            setRecordStatus(*target, SceneCharacterMovementStatus::NoPath, "Navigation path request failed: " + path.message);
            return false;
        }

        target->pathRequest = effective;
        target->hasPathRequest = true;
        target->state.activePath = path.path;
        target->state.activeWaypointIndex = path.path.points.size() > 1 ? 1u : 0u;
        setRecordStatus(*target, SceneCharacterMovementStatus::Success, "Navigation path accepted.");
        return true;
    }

    bool SceneCharacterMovementSystem::clearPath(SceneCharacterHandle character)
    {
        CharacterRecord* target = record(character);
        if (!target) {
            return false;
        }
        target->hasPathRequest = false;
        target->state.activePath = {};
        target->state.activeWaypointIndex = 0;
        setRecordStatus(*target, SceneCharacterMovementStatus::Success, "Navigation path cleared.");
        return true;
    }

    void SceneCharacterMovementSystem::updateFixed(float deltaSeconds)
    {
        if (!finite(deltaSeconds) || deltaSeconds <= 0.0f) {
            diagnostics_.warnings.push_back("Skipped character movement update with invalid fixed delta.");
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        cleanupInvalidOwners();
        for (uint32_t index = 0; index < characters_.size(); ++index) {
            CharacterRecord& character = characters_[index];
            if (!character.occupied) {
                continue;
            }
            const SceneCharacterHandle handle{index, character.generation};
            if (!character.descriptor.enabled) {
                character.state.mode = SceneCharacterMovementMode::Disabled;
                character.state.grounded = false;
                character.state.velocity = {};
                continue;
            }

            std::optional<SceneTransform> transform = scene_.localTransform(character.descriptor.actor);
            if (!transform.has_value()) {
                setRecordStatus(character, SceneCharacterMovementStatus::InvalidActor, "Character actor transform is unavailable.");
                continue;
            }

            glm::vec3 position = transform->translation;
            GroundProbe ground = probeGround(handle, character, position);
            if (ground.walkable && character.state.velocity.y <= 0.0f) {
                position.y = ground.position.y;
                character.state.grounded = true;
                character.state.mode = SceneCharacterMovementMode::Walking;
                character.state.floorNormal = ground.normal;
                character.state.floorDistance = ground.distance;
                character.state.velocity.y = 0.0f;
            } else {
                character.state.grounded = false;
                character.state.mode = SceneCharacterMovementMode::Falling;
                character.state.floorDistance = ground.hit ? ground.distance : character.descriptor.snapDistance;
                character.state.velocity.y = std::max(
                    character.state.velocity.y - character.descriptor.gravity * deltaSeconds,
                    -MaxFallSpeed);
            }

        const bool manualInput = glm::length2(xz(character.input.direction)) > Epsilon;
        const bool pathInput = !manualInput && character.hasPathRequest;
        const glm::vec3 desiredDirection = desiredInputDirection(character, position);
        const float speedScale = pathInput ? 1.0f : clamp01(character.input.speedScale);
        const glm::vec3 targetHorizontalVelocity =
                desiredDirection * character.descriptor.maxSpeed * speedScale;
            const float response = glm::length2(targetHorizontalVelocity) > Epsilon
                ? character.descriptor.acceleration
                : character.descriptor.braking;
            const glm::vec3 currentHorizontal = xz(character.state.velocity);
            const glm::vec3 newHorizontal = moveToward(
                currentHorizontal,
                targetHorizontalVelocity,
                response * deltaSeconds);
            character.state.velocity.x = newHorizontal.x;
            character.state.velocity.z = newHorizontal.z;

            const glm::vec3 desiredPosition = position + character.state.velocity * deltaSeconds;
            glm::vec3 finalPosition = moveWithCollision(handle, character, position, desiredPosition, deltaSeconds);

            GroundProbe finalGround = probeGround(handle, character, finalPosition);
            if (finalGround.walkable && character.state.velocity.y <= 0.0f) {
                finalPosition.y = finalGround.position.y;
                character.state.grounded = true;
                character.state.mode = SceneCharacterMovementMode::Walking;
                character.state.floorNormal = finalGround.normal;
                character.state.floorDistance = finalGround.distance;
                character.state.velocity.y = 0.0f;
            }

            if (character.input.faceDirection.has_value() && glm::length2(xz(*character.input.faceDirection)) > Epsilon) {
                transform->rotation = yawRotationForDirection(*character.input.faceDirection);
            } else if (glm::length2(xz(character.state.velocity)) > Epsilon) {
                transform->rotation = yawRotationForDirection(character.state.velocity);
            }
            transform->translation = finalPosition;
            scene_.setLocalTransform(character.descriptor.actor, *transform);
            physics_.setKinematicTarget(character.body, finalPosition, transform->rotation);
            setRecordStatus(character, SceneCharacterMovementStatus::Success, "Character movement updated.");
        }
        diagnostics_.lastUpdateMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        refreshDiagnostics();
    }

    SceneSystemHandle SceneCharacterMovementSystem::registerMovementSystem()
    {
        if (systemHandle_.has_value() && scene_.contains(*systemHandle_)) {
            return *systemHandle_;
        }
        SceneSystemDescriptor descriptor;
        descriptor.name = "SceneCharacterMovementSystem";
        descriptor.phases = {SceneTickPhase::FixedPrePhysics};
        descriptor.onTick = [this](Scene&, const SceneTickContext& context) {
            updateFixed(context.deltaSeconds);
        };
        const SceneSystemHandle handle = scene_.registerSystem(std::move(descriptor));
        if (isValid(handle)) {
            systemHandle_ = handle;
        }
        return handle;
    }

    bool SceneCharacterMovementSystem::unregisterMovementSystem()
    {
        if (!systemHandle_.has_value()) {
            return false;
        }
        const bool removed = scene_.unregisterSystem(*systemHandle_);
        if (removed) {
            systemHandle_.reset();
        }
        return removed;
    }

    SceneCharacterMovementDiagnostics SceneCharacterMovementSystem::diagnostics() const
    {
        return diagnostics_;
    }

    std::vector<SceneCharacterDebugRequest> SceneCharacterMovementSystem::debugRequests() const
    {
        return debugRequests_;
    }

    void SceneCharacterMovementSystem::clearDebugRequests()
    {
        debugRequests_.clear();
    }

    SceneCharacterMovementSystem::CharacterRecord* SceneCharacterMovementSystem::record(SceneCharacterHandle character)
    {
        if (!isValid(character) || character.index >= characters_.size()) {
            return nullptr;
        }
        CharacterRecord& result = characters_[character.index];
        if (!result.occupied || result.generation != character.generation) {
            return nullptr;
        }
        return &result;
    }

    const SceneCharacterMovementSystem::CharacterRecord* SceneCharacterMovementSystem::record(SceneCharacterHandle character) const
    {
        if (!isValid(character) || character.index >= characters_.size()) {
            return nullptr;
        }
        const CharacterRecord& result = characters_[character.index];
        if (!result.occupied || result.generation != character.generation) {
            return nullptr;
        }
        return &result;
    }

    uint32_t SceneCharacterMovementSystem::nextGeneration(uint32_t generation) const
    {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    SceneCharacterHandle SceneCharacterMovementSystem::handleForIndex(uint32_t index) const
    {
        if (index >= characters_.size() || !characters_[index].occupied) {
            return {};
        }
        return {index, characters_[index].generation};
    }

    bool SceneCharacterMovementSystem::validDescriptor(const SceneCharacterDescriptor& descriptor) const
    {
        return finite(descriptor.radius) && finite(descriptor.height) && finite(descriptor.maxSpeed) &&
            finite(descriptor.acceleration) && finite(descriptor.braking) && finite(descriptor.gravity) &&
            finite(descriptor.slopeLimitDegrees) && finite(descriptor.stepHeight) && finite(descriptor.snapDistance) &&
            descriptor.radius > 0.0f && descriptor.height > descriptor.radius * 2.0f &&
            descriptor.maxSpeed >= 0.0f && descriptor.acceleration >= 0.0f && descriptor.braking >= 0.0f &&
            descriptor.gravity >= 0.0f && descriptor.slopeLimitDegrees >= 0.0f &&
            descriptor.slopeLimitDegrees <= 89.0f && descriptor.stepHeight >= 0.0f && descriptor.snapDistance >= 0.0f;
    }

    ScenePhysicsShapeDescriptor SceneCharacterMovementSystem::capsuleShape(const SceneCharacterDescriptor& descriptor) const
    {
        ScenePhysicsShapeDescriptor shape;
        shape.type = ScenePhysicsShapeType::Capsule;
        shape.capsule = capsuleQuery(descriptor);
        return shape;
    }

    ScenePhysicsCapsuleShape SceneCharacterMovementSystem::capsuleQuery(const SceneCharacterDescriptor& descriptor) const
    {
        ScenePhysicsCapsuleShape capsule;
        capsule.radius = descriptor.radius;
        capsule.halfHeight = std::max(0.001f, descriptor.height * 0.5f - descriptor.radius);
        return capsule;
    }

    SceneCharacterMovementSystem::GroundProbe SceneCharacterMovementSystem::probeGround(
        SceneCharacterHandle handle,
        const CharacterRecord& record,
        const glm::vec3& position)
    {
        GroundProbe result;
        const glm::vec3 end = position - glm::vec3{0.0f, record.descriptor.snapDistance + SweepLift, 0.0f};
        const ScenePhysicsSweepResult sweep = physics_.sweepCapsule(
            capsuleQuery(record.descriptor),
            position + glm::vec3{0.0f, SweepLift, 0.0f},
            end,
            record.descriptor.physicsFilter);
        appendDebug({SceneCharacterDebugRequestType::GroundProbe, SceneCharacterMovementStatus::Success, handle, position, end});
        if (sweep.status != ScenePhysicsQueryStatus::Success || !sweep.hit.has_value()) {
            ScenePhysicsShapeDescriptor snapVolume = capsuleShape(record.descriptor);
            snapVolume.capsule.halfHeight += record.descriptor.snapDistance * 0.5f;
            const glm::vec3 overlapCenter = (position + end) * 0.5f;
            (void)physics_.overlap(snapVolume, overlapCenter, record.descriptor.physicsFilter);
            return result;
        }
        result.hit = true;
        result.position = sweep.hit->position;
        result.normal = sweep.hit->normal;
        result.distance = std::max(0.0f, position.y - result.position.y);
        result.walkable = walkableNormal(record.descriptor, result.normal);
        return result;
    }

    bool SceneCharacterMovementSystem::walkableNormal(const SceneCharacterDescriptor& descriptor, const glm::vec3& normal) const
    {
        if (!finite(normal) || glm::length2(normal) <= Epsilon) {
            return false;
        }
        const glm::vec3 n = glm::normalize(normal);
        const float y = std::clamp(n.y, -1.0f, 1.0f);
        const float slope = glm::degrees(std::acos(y));
        return slope <= descriptor.slopeLimitDegrees;
    }

    glm::vec3 SceneCharacterMovementSystem::pathDirection(CharacterRecord& record, const glm::vec3& position)
    {
        if (!record.hasPathRequest || record.state.activePath.points.empty()) {
            return {};
        }
        while (record.state.activeWaypointIndex < record.state.activePath.points.size()) {
            const glm::vec3 waypoint = record.state.activePath.points[record.state.activeWaypointIndex];
            const glm::vec3 toWaypoint = xz(waypoint - position);
            if (glm::length(toWaypoint) > record.pathRequest.waypointAcceptanceRadius) {
                return normalizedOrZero(toWaypoint);
            }
            ++record.state.activeWaypointIndex;
        }
        record.hasPathRequest = false;
        return {};
    }

    glm::vec3 SceneCharacterMovementSystem::desiredInputDirection(CharacterRecord& record, const glm::vec3& position)
    {
        const glm::vec3 manual = normalizedOrZero(xz(record.input.direction));
        if (glm::length2(manual) > Epsilon) {
            return manual;
        }
        return pathDirection(record, position);
    }

    glm::vec3 SceneCharacterMovementSystem::moveWithCollision(
        SceneCharacterHandle handle,
        CharacterRecord& record,
        const glm::vec3& position,
        const glm::vec3& desiredPosition,
        float)
    {
        const glm::vec3 delta = desiredPosition - position;
        if (glm::length2(delta) <= Epsilon * Epsilon) {
            return position;
        }

        const glm::vec3 horizontalDelta = xz(delta);
        if (record.state.grounded && glm::length2(horizontalDelta) > Epsilon * Epsilon) {
            if (std::optional<glm::vec3> step = tryStep(handle, record, position, horizontalDelta)) {
                return *step;
            }
        }

        const ScenePhysicsSweepResult sweep = physics_.sweepCapsule(
            capsuleQuery(record.descriptor),
            position + glm::vec3{0.0f, SweepLift, 0.0f},
            desiredPosition + glm::vec3{0.0f, SweepLift, 0.0f},
            record.descriptor.physicsFilter);
        appendDebug({SceneCharacterDebugRequestType::MovementSweep, SceneCharacterMovementStatus::Success, handle, position, desiredPosition});
        if (sweep.status != ScenePhysicsQueryStatus::Success || !sweep.hit.has_value()) {
            return desiredPosition;
        }
        if (sweep.hit->distance <= Epsilon) {
            if (walkableNormal(record.descriptor, sweep.hit->normal)) {
                return desiredPosition;
            }
            ++diagnostics_.failedSweepCount;
            setRecordStatus(record, SceneCharacterMovementStatus::Blocked, "Character movement started against blocking geometry.");
            return position;
        }

        ++diagnostics_.failedSweepCount;
        const glm::vec3 hitPosition = sweep.hit->position - glm::vec3{0.0f, SweepLift, 0.0f};
        const glm::vec3 remaining = desiredPosition - hitPosition;
        const glm::vec3 normal = glm::normalize(sweep.hit->normal);
        const glm::vec3 slide = remaining - normal * glm::dot(remaining, normal);
        setRecordStatus(record, SceneCharacterMovementStatus::Blocked, "Character movement hit blocking geometry.");
        return hitPosition + slide;
    }

    std::optional<glm::vec3> SceneCharacterMovementSystem::tryStep(
        SceneCharacterHandle handle,
        CharacterRecord& record,
        const glm::vec3& position,
        const glm::vec3& horizontalDelta)
    {
        const glm::vec3 lifted = position + glm::vec3{0.0f, record.descriptor.stepHeight + SweepLift, 0.0f};
        const glm::vec3 forward = lifted + horizontalDelta;
        const ScenePhysicsSweepResult forwardSweep = physics_.sweepCapsule(
            capsuleQuery(record.descriptor),
            lifted,
            forward,
            record.descriptor.physicsFilter);
        appendDebug({SceneCharacterDebugRequestType::StepProbe, SceneCharacterMovementStatus::Success, handle, lifted, forward});
        if (forwardSweep.status == ScenePhysicsQueryStatus::Success) {
            return std::nullopt;
        }

        GroundProbe ground = probeGround(handle, record, forward);
        if (!ground.walkable || ground.distance <= SweepLift ||
            ground.distance > record.descriptor.stepHeight + record.descriptor.snapDistance + SweepLift) {
            return std::nullopt;
        }
        return ground.position;
    }

    void SceneCharacterMovementSystem::setRecordStatus(
        CharacterRecord& record,
        SceneCharacterMovementStatus status,
        std::string message)
    {
        record.state.lastStatus = status;
        record.state.lastMessage = std::move(message);
    }

    void SceneCharacterMovementSystem::cleanupInvalidOwners()
    {
        for (uint32_t index = 0; index < characters_.size(); ++index) {
            CharacterRecord& character = characters_[index];
            if (!character.occupied || scene_.contains(character.descriptor.actor)) {
                continue;
            }
            freeRecord(index);
            ++diagnostics_.invalidOwnerCleanupCount;
        }
    }

    void SceneCharacterMovementSystem::refreshDiagnostics()
    {
        diagnostics_.characterCount = 0;
        diagnostics_.enabledCount = 0;
        diagnostics_.disabledCount = 0;
        diagnostics_.groundedCount = 0;
        diagnostics_.fallingCount = 0;
        for (const CharacterRecord& character : characters_) {
            if (!character.occupied) {
                continue;
            }
            ++diagnostics_.characterCount;
            if (character.descriptor.enabled) {
                ++diagnostics_.enabledCount;
            } else {
                ++diagnostics_.disabledCount;
            }
            if (character.state.grounded) {
                ++diagnostics_.groundedCount;
            }
            if (character.state.mode == SceneCharacterMovementMode::Falling) {
                ++diagnostics_.fallingCount;
            }
        }
    }

    void SceneCharacterMovementSystem::freeRecord(uint32_t index)
    {
        if (index >= characters_.size() || !characters_[index].occupied) {
            return;
        }
        CharacterRecord& character = characters_[index];
        if (isValid(character.collider) && physics_.contains(character.collider)) {
            physics_.detachCollider(character.collider);
        }
        if (isValid(character.body) && physics_.contains(character.body)) {
            physics_.destroyBody(character.body);
        }
        const uint32_t generation = character.generation;
        character = {};
        character.generation = generation;
    }

    void SceneCharacterMovementSystem::appendDebug(SceneCharacterDebugRequest request)
    {
        debugRequests_.push_back(std::move(request));
    }
}
