#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/World.hpp"

namespace Engine {
    struct SpatialRegistrySettings {
        float cellSize = 16.0f;
        bool autoRemoveEmptyCells = true;
    };

    struct SpatialQueryResult {
        WorldObjectHandle object;
        ObjectId objectId;
        glm::vec3 position{};
        ChunkCoord cell;
    };

    // Sparse XZ grid for gameplay/debug object lookups. World owns object
    // lifetime; this registry only tracks last known object positions.
    class SpatialRegistry {
    public:
        explicit SpatialRegistry(SpatialRegistrySettings settings = {});

        void insert(WorldObjectHandle object, const glm::vec3& position);
        void remove(WorldObjectHandle object);
        void update(WorldObjectHandle object, const glm::vec3& position);
        void clear();

        ChunkCoord cellForPosition(const glm::vec3& position) const;
        std::vector<SpatialQueryResult> objectsInCell(ChunkCoord cell) const;
        std::vector<SpatialQueryResult> objectsInRadius(const glm::vec3& center, float radius) const;
        std::vector<SpatialQueryResult> objectsInAabb(const glm::vec3& min, const glm::vec3& max) const;

        size_t registeredObjectCount() const;
        size_t activeCellCount() const;
        const SpatialRegistrySettings& settings() const;

    private:
        struct ObjectRecord {
            glm::vec3 position{};
            ChunkCoord cell;
        };

        using CellObjects = std::vector<WorldObjectHandle>;

        static bool isValidHandle(WorldObjectHandle object);
        void removeFromCell(WorldObjectHandle object, ChunkCoord cell);
        void removeCellIfEmpty(ChunkCoord cell);
        SpatialQueryResult makeResult(WorldObjectHandle object, const ObjectRecord& record) const;

        SpatialRegistrySettings settings_;
        std::unordered_map<ChunkCoord, CellObjects, ChunkCoordHash> cells_;
        std::unordered_map<uint32_t, ObjectRecord> objects_;
    };
}
