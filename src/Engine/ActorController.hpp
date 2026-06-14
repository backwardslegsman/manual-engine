#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/EventQueue.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"
#include "Engine/WorldNavigationGraph.hpp"

namespace Engine {
    class BlockingCollisionSystem;
    class SpatialRegistry;

    struct ActorHandle {
        uint32_t id = UINT32_MAX;
    };

    enum class ActorPathStatus {
        Idle,
        Pathing,
        Moving,
        Blocked,
        Repathing,
        Arrived,
        Failed,
        Cancelled,
    };

    enum class ActorRouteStatus {
        None,
        Planning,
        MovingToWaypoint,
        WaitingForLocalTile,
        Arrived,
        Failed,
        Cancelled,
    };

    struct ActorPathSettings {
        float arrivalRadius = 0.35f;
        float cornerAdvanceRadius = 0.35f;
        uint32_t blockedTickLimit = 30;
        uint32_t repathAttempts = 1;
        float repathDistanceThreshold = 0.75f;
    };

    struct ActorControllerSettings {
        float movementSpeed = 6.0f;
        float groundOffset = 0.65f;
        float facingTurnSpeed = 20.0f;
        bool collisionEnabled = true;
        float collisionRadius = 0.45f;
        float collisionHeight = 1.8f;
        bool manualInputCancelsPath = true;
        ActorPathSettings path;
    };

    struct ActorPathState {
        ActorPathStatus status = ActorPathStatus::Idle;
        NavPath path;
        uint32_t currentCorner = 0;
        glm::vec3 destination{};
        ActorPathSettings settings;
        uint32_t blockedTicks = 0;
        uint32_t repathAttemptsUsed = 0;
        NavQueryStatus lastQueryStatus = NavQueryStatus::Unsupported;
        std::string lastQueryMessage;
    };

    struct ActorRouteState {
        ActorRouteStatus status = ActorRouteStatus::None;
        WorldNavRoute route;
        uint32_t currentWaypointIndex = 0;
        glm::vec3 finalDestination{};
        std::string lastRouteMessage;
    };

    struct ActorCommandDiagnostics {
        bool hasCommand = false;
        glm::vec3 destination{};
        NavQueryStatus directLocalStatus = NavQueryStatus::Unsupported;
        bool directPathComplete = false;
        std::string directLocalMessage;
        bool routeAttempted = false;
        WorldNavRouteStatus routeStatus = WorldNavRouteStatus::NoGraph;
        std::string routeMessage;
        uint32_t currentWaypointIndex = 0;
        bool hasCurrentWaypointChunk = false;
        ChunkCoord currentWaypointChunk{};
        bool localTileAvailable = false;
        std::string finalReason;
    };

    struct ActorState {
        WorldObjectHandle object;
        glm::vec3 velocity{};
        float facingRadians = 0.0f;
        bool collisionEnabled = false;
        float collisionRadius = 0.0f;
        float collisionHeight = 0.0f;
        bool blockedX = false;
        bool blockedZ = false;
        WorldObjectHandle firstBlockingObject;
        ObjectId firstBlockingObjectId;
        uint32_t collisionHitCount = 0;
        glm::vec3 desiredPosition{};
        glm::vec3 resolvedPosition{};
        bool hasMovementDebug = false;
        ActorPathState path;
        ActorRouteState route;
        ActorCommandDiagnostics commandDiagnostics;
    };

    class ActorController {
    public:
        ActorHandle createActor(WorldObjectHandle object, ActorControllerSettings settings = {});
        void destroyActor(ActorHandle actor);

        void setPosition(ActorHandle actor, const glm::vec3& position, const TerrainSystem* terrain = nullptr, World* world = nullptr);
        std::optional<glm::vec3> position(ActorHandle actor, const World& world) const;
        std::optional<ActorState> state(ActorHandle actor) const;
        std::optional<ActorPathState> pathState(ActorHandle actor) const;
        NavQueryResult setPathDestination(
            ActorHandle actor,
            const glm::vec3& destination,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent,
            const World& world);
        bool setPath(ActorHandle actor, NavPath path, const glm::vec3& destination);
        WorldNavRoute setRouteDestination(
            ActorHandle actor,
            const glm::vec3& destination,
            const WorldNavigationGraph& worldGraph,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent,
            const World& world);
        void cancelPath(ActorHandle actor);
        void clearPath(ActorHandle actor);
        void clearRoute(ActorHandle actor);
        void forEachActor(const std::function<void(ActorHandle, const ActorState&)>& callback) const;

        void fixedUpdate(ActorHandle actor, const EventQueue& events, const TerrainSystem& terrain, World& world, float dt);
        void fixedUpdate(
            ActorHandle actor,
            const EventQueue& events,
            const TerrainSystem& terrain,
            World& world,
            const SpatialRegistry& spatialRegistry,
            const BlockingCollisionSystem& collision,
            float dt);
        void fixedUpdate(
            ActorHandle actor,
            const EventQueue& events,
            const TerrainSystem& terrain,
            World& world,
            const SpatialRegistry& spatialRegistry,
            const BlockingCollisionSystem& collision,
            const NavigationSystem& navigation,
            const NavAgentSettings& navAgent,
            float dt);

    private:
        struct ActorRecord {
            bool alive = false;
            ActorState state;
            ActorControllerSettings settings;
            uint32_t blockedPathTicks = 0;
        };

        ActorRecord* record(ActorHandle actor);
        const ActorRecord* record(ActorHandle actor) const;
        void fixedUpdateInternal(
            ActorHandle actor,
            const EventQueue& events,
            const TerrainSystem& terrain,
            World& world,
            const SpatialRegistry* spatialRegistry,
            const BlockingCollisionSystem* collision,
            const NavigationSystem* navigation,
            const NavAgentSettings* navAgent,
            float dt);
        NavQueryResult setPathDestinationInternal(
            ActorHandle actor,
            const glm::vec3& destination,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent,
            const World& world,
            bool clearRouteState);
        bool requestRouteWaypointPath(
            ActorHandle actor,
            ActorRecord& actorRecord,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent,
            const World& world);

        std::vector<ActorRecord> actors_;
    };
}
