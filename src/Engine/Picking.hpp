#pragma once

#include <optional>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/World.hpp"

namespace Engine {
    struct Ray {
        glm::vec3 origin{};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
    };

    struct ObjectPickHit {
        WorldObjectHandle object;
        glm::vec3 position{};
        float distance = 0.0f;
        ChunkCoord cell;
    };

    struct TerrainPickHit {
        glm::vec3 position{};
        float distance = 0.0f;
        ChunkCoord chunk;
    };

    struct DebugPickResult {
        std::optional<ObjectPickHit> object;
        std::optional<TerrainPickHit> terrain;
    };

    struct DebugSelectionState {
        Ray ray;
        glm::vec2 mousePosition{};
        std::optional<ObjectPickHit> hoveredObject;
        std::optional<ObjectPickHit> selectedObject;
        std::optional<TerrainPickHit> terrainHit;
    };

    Ray screenPointToRay(
        const glm::vec2& screenPosition,
        const glm::ivec2& viewportSize,
        const glm::mat4& view,
        const glm::mat4& projection
    );

    std::optional<float> intersectRayAabb(const Ray& ray, const Renderer::Aabb& bounds, float maxDistance);

    std::optional<ObjectPickHit> pickNearestObject(
        const Ray& ray,
        const SpatialRegistry& spatialRegistry,
        const World& world,
        float maxDistance,
        float queryMargin = 2.0f
    );
}
