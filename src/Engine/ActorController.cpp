#include "Engine/ActorController.hpp"

#include <algorithm>
#include <cmath>

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

        ActorRecord actor;
        actor.alive = true;
        actor.state.object = object;
        actor.state.collisionEnabled = settings.collisionEnabled;
        actor.state.collisionRadius = settings.collisionRadius;
        actor.state.collisionHeight = settings.collisionHeight;
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

    void ActorController::fixedUpdate(ActorHandle actor, const EventQueue& events, const TerrainSystem& terrain, World& world, float dt)
    {
        fixedUpdateInternal(actor, events, terrain, world, nullptr, nullptr, dt);
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
        fixedUpdateInternal(actor, events, terrain, world, &spatialRegistry, &collision, dt);
    }

    void ActorController::fixedUpdateInternal(
        ActorHandle actor,
        const EventQueue& events,
        const TerrainSystem& terrain,
        World& world,
        const SpatialRegistry* spatialRegistry,
        const BlockingCollisionSystem* collision,
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
        if (glm::dot(axis, axis) > 0.0f) {
            const glm::vec3 direction = glm::normalize(glm::vec3{axis.x, 0.0f, axis.y});
            velocity = direction * actorRecord->settings.movementSpeed;
            const float targetFacing = std::atan2(direction.x, direction.z);
            const float maxFacingStep = actorRecord->settings.facingTurnSpeed * dt;
            actorRecord->state.facingRadians = actorRecord->settings.facingTurnSpeed > 0.0f
                ? moveTowardAngle(actorRecord->state.facingRadians, targetFacing, maxFacingStep)
                : targetFacing;
        }

        actorRecord->state.velocity = velocity;
        const glm::vec3 currentPosition = world.position(actorRecord->state.object).value_or(glm::vec3{});
        glm::vec3 position = currentPosition + velocity * dt;
        if (glm::dot(axis, axis) > 0.0f) {
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
        }

        if (const std::optional<float> terrainHeight = terrain.sampleHeight(position.x, position.z)) {
            position.y = *terrainHeight + actorRecord->settings.groundOffset;
        }

        actorRecord->state.velocity = (position - currentPosition) / dt;
        if (actorRecord->state.hasMovementDebug) {
            actorRecord->state.resolvedPosition = position;
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
