#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "Engine/World.hpp"

namespace Engine {
    class SpatialRegistry;

    struct BlockingCollisionRequest {
        WorldObjectHandle actor;
        glm::vec3 currentPosition{};
        glm::vec3 desiredPosition{};
        float radius = 0.45f;
        float height = 1.8f;
        float skinWidth = 0.02f;
        float candidateQueryPadding = 4.0f;
        const World* world = nullptr;
        const SpatialRegistry* spatialRegistry = nullptr;
    };

    struct BlockingCollisionResult {
        glm::vec3 resolvedPosition{};
        bool blockedX = false;
        bool blockedZ = false;
        WorldObjectHandle firstBlockingObject;
        ObjectId firstBlockingObjectId;
        uint32_t hitCount = 0;
    };

    // Tiny kinematic collision helper for actor-vs-static-object blocking.
    // World owns bounds and collision flags; SpatialRegistry only provides
    // nearby candidate lookup.
    class BlockingCollisionSystem {
    public:
        BlockingCollisionResult resolveActorMovement(const BlockingCollisionRequest& request) const;
    };
}
