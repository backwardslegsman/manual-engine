#include "Engine/SpatialRegistry.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {
    SpatialRegistry::SpatialRegistry(SpatialRegistrySettings settings)
        : settings_(settings)
    {
        settings_.cellSize = std::max(settings_.cellSize, 1.0f);
    }

    void SpatialRegistry::insert(WorldObjectHandle object, const glm::vec3& position)
    {
        if (!isValidHandle(object)) {
            return;
        }

        remove(object);
        const ChunkCoord cell = cellForPosition(position);
        objects_[object.id] = {position, cell};
        cells_[cell].push_back(object);
    }

    void SpatialRegistry::remove(WorldObjectHandle object)
    {
        if (!isValidHandle(object)) {
            return;
        }

        const auto objectIt = objects_.find(object.id);
        if (objectIt == objects_.end()) {
            return;
        }

        const ChunkCoord cell = objectIt->second.cell;
        objects_.erase(objectIt);
        removeFromCell(object, cell);
    }

    void SpatialRegistry::update(WorldObjectHandle object, const glm::vec3& position)
    {
        if (!isValidHandle(object)) {
            return;
        }

        const ChunkCoord newCell = cellForPosition(position);
        const auto objectIt = objects_.find(object.id);
        if (objectIt == objects_.end()) {
            insert(object, position);
            return;
        }

        if (objectIt->second.cell == newCell) {
            objectIt->second.position = position;
            return;
        }

        const ChunkCoord oldCell = objectIt->second.cell;
        objectIt->second = {position, newCell};
        removeFromCell(object, oldCell);
        cells_[newCell].push_back(object);
    }

    void SpatialRegistry::clear()
    {
        cells_.clear();
        objects_.clear();
    }

    ChunkCoord SpatialRegistry::cellForPosition(const glm::vec3& position) const
    {
        return {
            static_cast<int32_t>(std::floor(position.x / settings_.cellSize)),
            static_cast<int32_t>(std::floor(position.z / settings_.cellSize)),
        };
    }

    std::vector<SpatialQueryResult> SpatialRegistry::objectsInCell(ChunkCoord cell) const
    {
        std::vector<SpatialQueryResult> results;
        const auto cellIt = cells_.find(cell);
        if (cellIt == cells_.end()) {
            return results;
        }

        results.reserve(cellIt->second.size());
        for (WorldObjectHandle object : cellIt->second) {
            const auto objectIt = objects_.find(object.id);
            if (objectIt != objects_.end()) {
                results.push_back(makeResult(object, objectIt->second));
            }
        }
        return results;
    }

    std::vector<SpatialQueryResult> SpatialRegistry::objectsInRadius(const glm::vec3& center, float radius) const
    {
        if (radius < 0.0f) {
            return {};
        }

        const glm::vec3 min{center.x - radius, center.y, center.z - radius};
        const glm::vec3 max{center.x + radius, center.y, center.z + radius};
        std::vector<SpatialQueryResult> candidates = objectsInAabb(min, max);
        const float radiusSquared = radius * radius;

        candidates.erase(
            std::remove_if(
                candidates.begin(),
                candidates.end(),
                [center, radiusSquared](const SpatialQueryResult& result) {
                    const glm::vec2 offset{
                        result.position.x - center.x,
                        result.position.z - center.z,
                    };
                    return glm::dot(offset, offset) > radiusSquared;
                }
            ),
            candidates.end()
        );
        return candidates;
    }

    std::vector<SpatialQueryResult> SpatialRegistry::objectsInAabb(const glm::vec3& min, const glm::vec3& max) const
    {
        const glm::vec3 queryMin = glm::min(min, max);
        const glm::vec3 queryMax = glm::max(min, max);
        const ChunkCoord minCell = cellForPosition(queryMin);
        const ChunkCoord maxCell = cellForPosition(queryMax);

        std::vector<SpatialQueryResult> results;
        for (int32_t z = minCell.z; z <= maxCell.z; ++z) {
            for (int32_t x = minCell.x; x <= maxCell.x; ++x) {
                const auto cellIt = cells_.find({x, z});
                if (cellIt == cells_.end()) {
                    continue;
                }

                for (WorldObjectHandle object : cellIt->second) {
                    const auto objectIt = objects_.find(object.id);
                    if (objectIt == objects_.end()) {
                        continue;
                    }

                    const glm::vec3& position = objectIt->second.position;
                    if (position.x >= queryMin.x &&
                        position.x <= queryMax.x &&
                        position.z >= queryMin.z &&
                        position.z <= queryMax.z) {
                        results.push_back(makeResult(object, objectIt->second));
                    }
                }
            }
        }
        return results;
    }

    size_t SpatialRegistry::registeredObjectCount() const
    {
        return objects_.size();
    }

    size_t SpatialRegistry::activeCellCount() const
    {
        return cells_.size();
    }

    const SpatialRegistrySettings& SpatialRegistry::settings() const
    {
        return settings_;
    }

    bool SpatialRegistry::isValidHandle(WorldObjectHandle object)
    {
        return object.id != UINT32_MAX;
    }

    void SpatialRegistry::removeFromCell(WorldObjectHandle object, ChunkCoord cell)
    {
        const auto cellIt = cells_.find(cell);
        if (cellIt == cells_.end()) {
            return;
        }

        CellObjects& objects = cellIt->second;
        objects.erase(
            std::remove_if(
                objects.begin(),
                objects.end(),
                [object](WorldObjectHandle candidate) {
                    return candidate.id == object.id;
                }
            ),
            objects.end()
        );
        removeCellIfEmpty(cell);
    }

    void SpatialRegistry::removeCellIfEmpty(ChunkCoord cell)
    {
        if (!settings_.autoRemoveEmptyCells) {
            return;
        }

        const auto cellIt = cells_.find(cell);
        if (cellIt != cells_.end() && cellIt->second.empty()) {
            cells_.erase(cellIt);
        }
    }

    SpatialQueryResult SpatialRegistry::makeResult(WorldObjectHandle object, const ObjectRecord& record) const
    {
        return {object, record.position, record.cell};
    }
}
