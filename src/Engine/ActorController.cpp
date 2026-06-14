#include "Engine/ActorController.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtc/constants.hpp>

#include "Engine/BlockingCollision.hpp"
#include "Engine/SpatialRegistry.hpp"

namespace Engine {
    namespace {
        constexpr const char* PlayerMoveAction = "player.move";

        float wrapRadians(float value)
        {
            while (value > glm::pi<float>()) {
                value -= glm::two_pi<float>();
            }
            while (value < -glm::pi<float>()) {
                value += glm::two_pi<float>();
            }
            return value;
        }

        float moveTowardAngle(float current, float target, float maxStep)
        {
            const float delta = wrapRadians(target - current);
            if (std::abs(delta) <= maxStep) {
                return target;
            }
            return wrapRadians(current + (delta > 0.0f ? maxStep : -maxStep));
        }

        glm::vec2 playerMoveAxis(const EventQueue& events)
        {
            glm::vec2 axis{};
            for (const InputActionEvent& event : events.inputActions()) {
                if (event.action == PlayerMoveAction &&
                    event.payloadType == InputActionPayloadType::Axis2 &&
                    event.phase == InputActionPhase::Held) {
                    axis += event.axis2Value;
                }
            }

            if (glm::dot(axis, axis) > 1.0f) {
                axis = glm::normalize(axis);
            }
            return axis;
        }

        bool hasManualMoveInput(const glm::vec2& axis)
        {
            return glm::dot(axis, axis) > 0.0f;
        }

        void setPathFailure(ActorPathState& path, NavQueryStatus status, std::string message)
        {
            path.status = ActorPathStatus::Failed;
            path.path = {};
            path.currentCorner = 0;
            path.lastQueryStatus = status;
            path.lastQueryMessage = std::move(message);
        }

        float xzDistanceSquared(const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec2 delta{a.x - b.x, a.z - b.z};
            return glm::dot(delta, delta);
        }

        bool activePathStatus(ActorPathStatus status)
        {
            return status == ActorPathStatus::Pathing ||
                status == ActorPathStatus::Moving ||
                status == ActorPathStatus::Blocked ||
                status == ActorPathStatus::Repathing;
        }
    }

    ActorHandle ActorController::createActor(WorldObjectHandle object, ActorControllerSettings settings)
    {
        if (object.id == UINT32_MAX) {
            return {};
        }

        settings.movementSpeed = std::max(settings.movementSpeed, 0.0f);
        settings.groundOffset = std::max(settings.groundOffset, 0.0f);
        settings.facingTurnSpeed = std::max(settings.facingTurnSpeed, 0.0f);
        settings.collisionRadius = std::max(settings.collisionRadius, 0.0f);
        settings.collisionHeight = std::max(settings.collisionHeight, 0.0f);
        settings.path.arrivalRadius = std::max(settings.path.arrivalRadius, 0.0f);
        settings.path.cornerAdvanceRadius = std::max(settings.path.cornerAdvanceRadius, 0.0f);
        settings.path.repathDistanceThreshold = std::max(settings.path.repathDistanceThreshold, 0.0f);

        ActorRecord actor;
        actor.alive = true;
        actor.state.object = object;
        actor.state.collisionEnabled = settings.collisionEnabled;
        actor.state.collisionRadius = settings.collisionRadius;
        actor.state.collisionHeight = settings.collisionHeight;
        actor.state.path.settings = settings.path;
        actor.settings = settings;

        for (uint32_t index = 0; index < actors_.size(); ++index) {
            if (!actors_[index].alive) {
                actors_[index] = actor;
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(actors_.size());
        actors_.push_back(actor);
        return {id};
    }

    void ActorController::destroyActor(ActorHandle actor)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return;
        }
        *actorRecord = {};
    }

    void ActorController::setPosition(ActorHandle actor, const glm::vec3& position, const TerrainSystem* terrain, World* world)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return;
        }

        glm::vec3 groundedPosition = position;
        if (terrain) {
            if (const std::optional<float> height = terrain->sampleHeight(position.x, position.z)) {
                groundedPosition.y = *height + actorRecord->settings.groundOffset;
            }
        }

        if (world && world->isValid(actorRecord->state.object)) {
            world->setPosition(actorRecord->state.object, groundedPosition);
        }
    }

    std::optional<glm::vec3> ActorController::position(ActorHandle actor, const World& world) const
    {
        const ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return std::nullopt;
        }
        return world.position(actorRecord->state.object);
    }

    std::optional<ActorState> ActorController::state(ActorHandle actor) const
    {
        const ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return std::nullopt;
        }
        return actorRecord->state;
    }

    std::optional<ActorPathState> ActorController::pathState(ActorHandle actor) const
    {
        const ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return std::nullopt;
        }
        return actorRecord->state.path;
    }

    NavQueryResult ActorController::setPathDestination(
        ActorHandle actor,
        const glm::vec3& destination,
        const NavigationSystem& navigation,
        const NavAgentSettings& agent,
        const World& world)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord || !world.isValid(actorRecord->state.object)) {
            return {NavQueryStatus::InvalidInput, {}, {}, "Invalid actor path request."};
        }

        const std::optional<glm::vec3> currentPosition = world.position(actorRecord->state.object);
        if (!currentPosition) {
            setPathFailure(actorRecord->state.path, NavQueryStatus::InvalidInput, "Actor has no world position.");
            return {NavQueryStatus::InvalidInput, {}, {}, actorRecord->state.path.lastQueryMessage};
        }

        actorRecord->state.path.status = ActorPathStatus::Pathing;
        actorRecord->state.path.destination = destination;
        actorRecord->state.path.settings = actorRecord->settings.path;

        NavQueryResult result = navigation.findPath(*currentPosition, destination, agent);
        actorRecord->state.path.lastQueryStatus = result.status;
        actorRecord->state.path.lastQueryMessage = result.message;
        if (!setPath(actor, result.path, result.path.points.empty() ? destination : result.path.points.back()) ||
            result.status != NavQueryStatus::Success) {
            setPathFailure(
                actorRecord->state.path,
                result.status,
                result.message.empty() ? "Navigation did not return a usable path." : result.message);
            return result;
        }
        actorRecord->state.path.lastQueryStatus = result.status;
        actorRecord->state.path.lastQueryMessage = result.message;
        return result;
    }

    bool ActorController::setPath(ActorHandle actor, NavPath path, const glm::vec3& destination)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord || path.points.size() < 2) {
            return false;
        }

        actorRecord->state.path.status = ActorPathStatus::Moving;
        actorRecord->state.path.path = std::move(path);
        actorRecord->state.path.currentCorner = 1;
        actorRecord->state.path.destination = destination;
        actorRecord->state.path.settings = actorRecord->settings.path;
        actorRecord->state.path.blockedTicks = 0;
        actorRecord->state.path.repathAttemptsUsed = 0;
        actorRecord->blockedPathTicks = 0;
        return true;
    }

    void ActorController::cancelPath(ActorHandle actor)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return;
        }
        actorRecord->state.path.status = ActorPathStatus::Cancelled;
        actorRecord->state.path.path = {};
        actorRecord->state.path.currentCorner = 0;
        actorRecord->state.path.blockedTicks = 0;
        actorRecord->state.path.lastQueryStatus = NavQueryStatus::Success;
        actorRecord->state.path.lastQueryMessage = "Path cancelled.";
        actorRecord->blockedPathTicks = 0;
    }

    void ActorController::clearPath(ActorHandle actor)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord) {
            return;
        }
        actorRecord->state.path = {};
        actorRecord->state.path.settings = actorRecord->settings.path;
        actorRecord->blockedPathTicks = 0;
    }

    void ActorController::forEachActor(const std::function<void(ActorHandle, const ActorState&)>& callback) const
    {
        if (!callback) {
            return;
        }
        for (uint32_t index = 0; index < actors_.size(); ++index) {
            if (actors_[index].alive) {
                callback({index}, actors_[index].state);
            }
        }
    }

    void ActorController::fixedUpdate(ActorHandle actor, const EventQueue& events, const TerrainSystem& terrain, World& world, float dt)
    {
        fixedUpdateInternal(actor, events, terrain, world, nullptr, nullptr, nullptr, nullptr, dt);
    }

    void ActorController::fixedUpdate(
        ActorHandle actor,
        const EventQueue& events,
        const TerrainSystem& terrain,
        World& world,
        const SpatialRegistry& spatialRegistry,
        const BlockingCollisionSystem& collision,
        float dt)
    {
        fixedUpdateInternal(actor, events, terrain, world, &spatialRegistry, &collision, nullptr, nullptr, dt);
    }

    void ActorController::fixedUpdate(
        ActorHandle actor,
        const EventQueue& events,
        const TerrainSystem& terrain,
        World& world,
        const SpatialRegistry& spatialRegistry,
        const BlockingCollisionSystem& collision,
        const NavigationSystem& navigation,
        const NavAgentSettings& navAgent,
        float dt)
    {
        fixedUpdateInternal(actor, events, terrain, world, &spatialRegistry, &collision, &navigation, &navAgent, dt);
    }

    void ActorController::fixedUpdateInternal(
        ActorHandle actor,
        const EventQueue& events,
        const TerrainSystem& terrain,
        World& world,
        const SpatialRegistry* spatialRegistry,
        const BlockingCollisionSystem* collision,
        const NavigationSystem* navigation,
        const NavAgentSettings* navAgent,
        float dt)
    {
        ActorRecord* actorRecord = record(actor);
        if (!actorRecord || !world.isValid(actorRecord->state.object) || dt <= 0.0f) {
            return;
        }

        actorRecord->state.blockedX = false;
        actorRecord->state.blockedZ = false;
        actorRecord->state.firstBlockingObject = {};
        actorRecord->state.firstBlockingObjectId = {};
        actorRecord->state.collisionHitCount = 0;
        actorRecord->state.collisionEnabled = actorRecord->settings.collisionEnabled;
        actorRecord->state.collisionRadius = actorRecord->settings.collisionRadius;
        actorRecord->state.collisionHeight = actorRecord->settings.collisionHeight;
        actorRecord->state.hasMovementDebug = false;

        const glm::vec2 axis = playerMoveAxis(events);
        glm::vec3 velocity{};
        if (hasManualMoveInput(axis)) {
            if (actorRecord->settings.manualInputCancelsPath && activePathStatus(actorRecord->state.path.status)) {
                cancelPath(actor);
            }
            const glm::vec3 direction = glm::normalize(glm::vec3{axis.x, 0.0f, axis.y});
            velocity = direction * actorRecord->settings.movementSpeed;
            const float targetFacing = std::atan2(direction.x, direction.z);
            const float maxFacingStep = actorRecord->settings.facingTurnSpeed * dt;
            actorRecord->state.facingRadians = actorRecord->settings.facingTurnSpeed > 0.0f
                ? moveTowardAngle(actorRecord->state.facingRadians, targetFacing, maxFacingStep)
                : targetFacing;
        }

        const glm::vec3 currentPosition = world.position(actorRecord->state.object).value_or(glm::vec3{});

        if (!hasManualMoveInput(axis) && actorRecord->state.path.status == ActorPathStatus::Moving) {
            ActorPathState& path = actorRecord->state.path;
            while (path.currentCorner < path.path.points.size()) {
                const glm::vec3 target = path.path.points[path.currentCorner];
                const glm::vec2 toTarget{target.x - currentPosition.x, target.z - currentPosition.z};
                const float advanceRadius = path.currentCorner + 1 >= path.path.points.size()
                    ? path.settings.arrivalRadius
                    : path.settings.cornerAdvanceRadius;
                if (glm::length(toTarget) > advanceRadius) {
                    break;
                }
                ++path.currentCorner;
            }

            if (path.currentCorner >= path.path.points.size()) {
                path.status = ActorPathStatus::Arrived;
            } else {
                const glm::vec3 target = path.path.points[path.currentCorner];
                const glm::vec2 toTarget{target.x - currentPosition.x, target.z - currentPosition.z};
                const float distance = glm::length(toTarget);
                if (distance > 0.0001f) {
                    const float stepDistance = std::min(actorRecord->settings.movementSpeed * dt, distance);
                    const glm::vec2 direction2 = toTarget / distance;
                    velocity = glm::vec3{direction2.x * (stepDistance / dt), 0.0f, direction2.y * (stepDistance / dt)};
                    const float targetFacing = std::atan2(direction2.x, direction2.y);
                    const float maxFacingStep = actorRecord->settings.facingTurnSpeed * dt;
                    actorRecord->state.facingRadians = actorRecord->settings.facingTurnSpeed > 0.0f
                        ? moveTowardAngle(actorRecord->state.facingRadians, targetFacing, maxFacingStep)
                        : targetFacing;
                }
            }
        }

        actorRecord->state.velocity = velocity;
        glm::vec3 position = currentPosition + velocity * dt;
        if (hasManualMoveInput(axis) || glm::dot(glm::vec2{velocity.x, velocity.z}, glm::vec2{velocity.x, velocity.z}) > 0.0f) {
            actorRecord->state.desiredPosition = position;
            actorRecord->state.hasMovementDebug = true;
        }

        if (actorRecord->settings.collisionEnabled &&
            spatialRegistry &&
            collision &&
            actorRecord->settings.collisionRadius > 0.0f &&
            actorRecord->settings.collisionHeight > 0.0f) {
            const BlockingCollisionResult collisionResult = collision->resolveActorMovement({
                actorRecord->state.object,
                currentPosition,
                position,
                actorRecord->settings.collisionRadius,
                actorRecord->settings.collisionHeight,
                0.02f,
                4.0f,
                &world,
                spatialRegistry,
            });
            position = collisionResult.resolvedPosition;
            actorRecord->state.blockedX = collisionResult.blockedX;
            actorRecord->state.blockedZ = collisionResult.blockedZ;
            actorRecord->state.firstBlockingObject = collisionResult.firstBlockingObject;
            actorRecord->state.firstBlockingObjectId = collisionResult.firstBlockingObjectId;
            actorRecord->state.collisionHitCount = collisionResult.hitCount;
            if (actorRecord->state.path.status == ActorPathStatus::Moving &&
                (collisionResult.blockedX || collisionResult.blockedZ)) {
                ++actorRecord->blockedPathTicks;
                actorRecord->state.path.blockedTicks = actorRecord->blockedPathTicks;
                if (actorRecord->blockedPathTicks >= actorRecord->state.path.settings.blockedTickLimit) {
                    ActorPathState& path = actorRecord->state.path;
                    if (navigation &&
                        navAgent &&
                        path.repathAttemptsUsed < path.settings.repathAttempts) {
                        path.status = ActorPathStatus::Repathing;
                        ++path.repathAttemptsUsed;
                        NavQueryResult result = navigation->findPath(position, path.destination, *navAgent);
                        path.lastQueryStatus = result.status;
                        path.lastQueryMessage = result.message;
                        const bool meaningfullyDifferent =
                            result.path.points.size() >= 2 &&
                            (path.path.points.size() < 2 ||
                                xzDistanceSquared(result.path.points[1], path.path.points[std::min<size_t>(path.currentCorner, path.path.points.size() - 1)]) >=
                                    path.settings.repathDistanceThreshold * path.settings.repathDistanceThreshold);
                        if (result.status == NavQueryStatus::Success && meaningfullyDifferent) {
                            const uint32_t attemptsUsed = path.repathAttemptsUsed;
                            setPath(actor, std::move(result.path), path.destination);
                            actorRecord->state.path.repathAttemptsUsed = attemptsUsed;
                            actorRecord->state.path.lastQueryStatus = result.status;
                            actorRecord->state.path.lastQueryMessage = result.message.empty()
                                ? "Path repathed after collision block."
                                : result.message;
                        } else {
                            path.status = ActorPathStatus::Blocked;
                            setPathFailure(path, result.status == NavQueryStatus::Success ? NavQueryStatus::NoPath : result.status,
                                result.message.empty() ? "Path blocked and repath did not produce a usable route." : result.message);
                        }
                    } else {
                        path.status = ActorPathStatus::Blocked;
                        setPathFailure(path, NavQueryStatus::NoPath, "Path following blocked by collision.");
                    }
                }
            } else {
                actorRecord->blockedPathTicks = 0;
                actorRecord->state.path.blockedTicks = 0;
            }
        } else {
            actorRecord->blockedPathTicks = 0;
            actorRecord->state.path.blockedTicks = 0;
        }

        if (const std::optional<float> terrainHeight = terrain.sampleHeight(position.x, position.z)) {
            position.y = *terrainHeight + actorRecord->settings.groundOffset;
        }

        actorRecord->state.velocity = (position - currentPosition) / dt;
        if (actorRecord->state.hasMovementDebug) {
            actorRecord->state.resolvedPosition = position;
        }

        if (actorRecord->state.path.status == ActorPathStatus::Moving &&
            actorRecord->state.path.currentCorner >= actorRecord->state.path.path.points.size()) {
            actorRecord->state.path.status = ActorPathStatus::Arrived;
            actorRecord->state.velocity = {};
        }
        world.setPosition(actorRecord->state.object, position);
        world.setRotation(actorRecord->state.object, {0.0f, actorRecord->state.facingRadians, 0.0f});
    }

    ActorController::ActorRecord* ActorController::record(ActorHandle actor)
    {
        if (actor.id >= actors_.size() || !actors_[actor.id].alive) {
            return nullptr;
        }
        return &actors_[actor.id];
    }

    const ActorController::ActorRecord* ActorController::record(ActorHandle actor) const
    {
        if (actor.id >= actors_.size() || !actors_[actor.id].alive) {
            return nullptr;
        }
        return &actors_[actor.id];
    }
}
