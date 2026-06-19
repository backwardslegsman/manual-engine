#include "Engine/NavigationSceneGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>

namespace {
    bool finiteVec3(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool finiteMat4(const glm::mat4& value)
    {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(value[column][row])) {
                    return false;
                }
            }
        }
        return true;
    }

    bool validAabb(const Renderer::Aabb& bounds)
    {
        return finiteVec3(bounds.min) &&
            finiteVec3(bounds.max) &&
            bounds.min.x <= bounds.max.x &&
            bounds.min.y <= bounds.max.y &&
            bounds.min.z <= bounds.max.z;
    }

    bool intersects(const Renderer::Aabb& lhs, const Renderer::Aabb& rhs)
    {
        return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x &&
            lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y &&
            lhs.min.z <= rhs.max.z && lhs.max.z >= rhs.min.z;
    }

    Renderer::Aabb emptyBounds()
    {
        const float max = std::numeric_limits<float>::max();
        return {{max, max, max}, {-max, -max, -max}};
    }

    void includePoint(Renderer::Aabb& bounds, const glm::vec3& point)
    {
        bounds.min = glm::min(bounds.min, point);
        bounds.max = glm::max(bounds.max, point);
    }

    Renderer::Aabb boundsForVertices(const std::vector<glm::vec3>& vertices)
    {
        Renderer::Aabb bounds = emptyBounds();
        for (const glm::vec3& vertex : vertices) {
            includePoint(bounds, vertex);
        }
        return bounds;
    }

    Renderer::Aabb transformBounds(const Renderer::Aabb& bounds, const glm::mat4& transform)
    {
        const glm::vec3 corners[] = {
            {bounds.min.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.min.y, bounds.max.z},
            {bounds.min.x, bounds.min.y, bounds.max.z},
            {bounds.min.x, bounds.max.y, bounds.min.z},
            {bounds.max.x, bounds.max.y, bounds.min.z},
            {bounds.max.x, bounds.max.y, bounds.max.z},
            {bounds.min.x, bounds.max.y, bounds.max.z},
        };

        Renderer::Aabb result = emptyBounds();
        for (const glm::vec3& corner : corners) {
            includePoint(result, glm::vec3(transform * glm::vec4(corner, 1.0f)));
        }
        return result;
    }

    bool validGeometry(const Engine::SceneNavigationSourceDescriptor& descriptor)
    {
        if (descriptor.vertices.empty() ||
            descriptor.indices.size() < 3 ||
            descriptor.indices.size() % 3 != 0) {
            return false;
        }

        return std::ranges::all_of(descriptor.indices, [&](uint32_t index) {
            return index < descriptor.vertices.size();
        });
    }

    bool finiteGeometry(const Engine::SceneNavigationSourceDescriptor& descriptor)
    {
        return std::ranges::all_of(descriptor.vertices, finiteVec3);
    }

    int coordForAxis(float value, float chunkSize)
    {
        return static_cast<int>(std::floor(value / chunkSize));
    }

    uint64_t chunkKey(Engine::ChunkCoord coord)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 32u) |
            static_cast<uint32_t>(coord.z);
    }
}

namespace Engine {
    SceneNavigationSourceHandle SceneNavigationGeometryRegistry::registerSource(
        SceneNavigationSourceDescriptor descriptor)
    {
        SourceRecord record;
        record.occupied = true;
        record.descriptor = std::move(descriptor);
        record.dirtyReason = SceneNavigationDirtyReason::Geometry;

        uint32_t index = 0;
        if (!freeSources_.empty()) {
            index = freeSources_.back();
            freeSources_.pop_back();
            record.generation = nextGeneration(sources_[index].generation);
            sources_[index] = std::move(record);
        } else {
            index = static_cast<uint32_t>(sources_.size());
            record.generation = 1;
            sources_.push_back(std::move(record));
        }

        refreshDiagnostics();
        return {index, sources_[index].generation};
    }

    bool SceneNavigationGeometryRegistry::unregisterSource(SceneNavigationSourceHandle source)
    {
        SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return false;
        }

        if (sourceRecord->lastWorldBounds.has_value()) {
            removedDirtyBounds_.push_back(*sourceRecord->lastWorldBounds);
        }

        sourceRecord->generation = nextGeneration(sourceRecord->generation);
        sourceRecord->occupied = false;
        sourceRecord->descriptor = {};
        sourceRecord->lastWorldBounds.reset();
        sourceRecord->dirtyReason = SceneNavigationDirtyReason::None;
        freeSources_.push_back(source.index);
        refreshDiagnostics();
        return true;
    }

    bool SceneNavigationGeometryRegistry::unregisterSource(
        Scene& scene,
        SceneNavigationSourceHandle source,
        float chunkSize)
    {
        if (!std::isfinite(chunkSize) || chunkSize <= 0.0f) {
            diagnostics_.warnings.push_back("Invalid navigation dirty chunk size");
            return false;
        }

        SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return false;
        }

        if (sourceRecord->lastWorldBounds.has_value()) {
            markChunksForBounds(*sourceRecord->lastWorldBounds, chunkSize);
        }
        if (std::optional<Renderer::Aabb> currentBounds = worldBounds(scene, *sourceRecord)) {
            markChunksForBounds(*currentBounds, chunkSize);
        }

        sourceRecord->generation = nextGeneration(sourceRecord->generation);
        sourceRecord->occupied = false;
        sourceRecord->descriptor = {};
        sourceRecord->lastWorldBounds.reset();
        sourceRecord->dirtyReason = SceneNavigationDirtyReason::None;
        freeSources_.push_back(source.index);
        refreshDiagnostics();
        return true;
    }

    bool SceneNavigationGeometryRegistry::contains(SceneNavigationSourceHandle source) const
    {
        return record(source) != nullptr;
    }

    std::optional<SceneNavigationSourceDescriptor> SceneNavigationGeometryRegistry::descriptor(
        SceneNavigationSourceHandle source) const
    {
        const SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return std::nullopt;
        }
        return sourceRecord->descriptor;
    }

    bool SceneNavigationGeometryRegistry::setSourceEnabled(SceneNavigationSourceHandle source, bool enabled)
    {
        SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return false;
        }
        if (sourceRecord->descriptor.enabled == enabled) {
            return true;
        }
        sourceRecord->descriptor.enabled = enabled;
        sourceRecord->dirtyReason = sourceRecord->dirtyReason | SceneNavigationDirtyReason::Enabled;
        refreshDiagnostics();
        return true;
    }

    bool SceneNavigationGeometryRegistry::setSourceGeometry(
        SceneNavigationSourceHandle source,
        std::vector<glm::vec3> vertices,
        std::vector<uint32_t> indices,
        std::optional<Renderer::Aabb> localBounds)
    {
        SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return false;
        }
        sourceRecord->descriptor.vertices = std::move(vertices);
        sourceRecord->descriptor.indices = std::move(indices);
        sourceRecord->descriptor.localBounds = localBounds;
        sourceRecord->dirtyReason = sourceRecord->dirtyReason | SceneNavigationDirtyReason::Geometry;
        refreshDiagnostics();
        return true;
    }

    bool SceneNavigationGeometryRegistry::markSourceDirty(
        SceneNavigationSourceHandle source,
        SceneNavigationDirtyReason reason)
    {
        SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return false;
        }
        sourceRecord->dirtyReason = sourceRecord->dirtyReason | reason;
        refreshDiagnostics();
        return true;
    }

    void SceneNavigationGeometryRegistry::refreshDirtySources(Scene& scene, float chunkSize)
    {
        if (!std::isfinite(chunkSize) || chunkSize <= 0.0f) {
            diagnostics_.warnings.push_back("Invalid navigation dirty chunk size");
            return;
        }

        for (const Renderer::Aabb& removedBounds : removedDirtyBounds_) {
            markChunksForBounds(removedBounds, chunkSize);
        }
        removedDirtyBounds_.clear();

        for (SourceRecord& source : sources_) {
            if (!source.occupied || source.dirtyReason == SceneNavigationDirtyReason::None) {
                continue;
            }

            if (source.lastWorldBounds.has_value()) {
                markChunksForBounds(*source.lastWorldBounds, chunkSize);
            }
            if (std::optional<Renderer::Aabb> currentBounds = worldBounds(scene, source)) {
                markChunksForBounds(*currentBounds, chunkSize);
                source.lastWorldBounds = *currentBounds;
            }
            source.dirtyReason = SceneNavigationDirtyReason::None;
        }
        refreshDiagnostics();
    }

    void SceneNavigationGeometryRegistry::clearDirty()
    {
        dirtyChunks_.clear();
        for (SourceRecord& source : sources_) {
            if (source.occupied) {
                source.dirtyReason = SceneNavigationDirtyReason::None;
            }
        }
        refreshDiagnostics();
    }

    std::vector<ChunkCoord> SceneNavigationGeometryRegistry::dirtyChunks() const
    {
        return dirtyChunks_;
    }

    std::optional<NavigationTerrainBuildData> SceneNavigationGeometryRegistry::buildNavigationData(
        Scene& scene,
        const SceneNavigationBuildRequest& request,
        const NavigationTerrainBuildData* terrainBase)
    {
        diagnostics_.includedSourceCount = 0;
        diagnostics_.skippedSourceCount = 0;
        diagnostics_.invalidActorCount = 0;
        diagnostics_.invalidGeometryCount = 0;
        diagnostics_.nonFiniteGeometryCount = 0;
        diagnostics_.outOfBoundsSourceCount = 0;
        diagnostics_.disabledSourceCount = 0;
        diagnostics_.walkableVertexCount = 0;
        diagnostics_.walkableTriangleCount = 0;
        diagnostics_.blockerVertexCount = 0;
        diagnostics_.blockerTriangleCount = 0;
        diagnostics_.warnings.clear();
        if (request.captureDebug) {
            debugRequests_.clear();
            SceneNavigationDebugRequest debug;
            debug.type = SceneNavigationDebugRequestType::BuildBounds;
            debug.coord = request.coord;
            debug.bounds = request.bounds;
            debug.message = "Navigation scene geometry build bounds";
            appendDebug(std::move(debug));
        }

        if (!validAabb(request.bounds)) {
            diagnostics_.warnings.push_back("Invalid navigation scene geometry build bounds");
            refreshDiagnostics();
            return std::nullopt;
        }

        NavigationTerrainBuildData result;
        result.coord = request.coord;
        result.bounds = request.bounds;

        if (terrainBase) {
            result.vertices = terrainBase->vertices;
            result.indices = terrainBase->indices;
            result.blockingVertices = terrainBase->blockingVertices;
            result.blockingIndices = terrainBase->blockingIndices;
        }

        const auto appendSource = [&](SourceRecord& source, SceneNavigationSourceHandle handle) {
            SceneNavigationSourceDescriptor& descriptor = source.descriptor;
            if (!descriptor.enabled) {
                ++diagnostics_.disabledSourceCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }
            if (!validGeometry(descriptor)) {
                ++diagnostics_.invalidGeometryCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }
            if (!finiteGeometry(descriptor)) {
                ++diagnostics_.nonFiniteGeometryCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }

            glm::mat4 transform{1.0f};
            if (descriptor.actor.has_value()) {
                std::optional<glm::mat4> world = scene.worldMatrix(*descriptor.actor);
                if (!world.has_value() || !finiteMat4(*world)) {
                    ++diagnostics_.invalidActorCount;
                    ++diagnostics_.skippedSourceCount;
                    return;
                }
                transform = *world;
            }

            const std::optional<Renderer::Aabb> maybeLocalBounds = localBounds(source);
            if (!maybeLocalBounds.has_value()) {
                ++diagnostics_.invalidGeometryCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }
            const Renderer::Aabb sourceWorldBounds = transformBounds(*maybeLocalBounds, transform);
            if (!validAabb(sourceWorldBounds)) {
                ++diagnostics_.nonFiniteGeometryCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }
            source.lastWorldBounds = sourceWorldBounds;
            if (!intersects(sourceWorldBounds, request.bounds)) {
                ++diagnostics_.outOfBoundsSourceCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }

            std::vector<glm::vec3>& targetVertices = descriptor.role == SceneNavigationSourceRole::Walkable
                ? result.vertices
                : result.blockingVertices;
            std::vector<uint32_t>& targetIndices = descriptor.role == SceneNavigationSourceRole::Walkable
                ? result.indices
                : result.blockingIndices;
            const uint32_t baseVertex = static_cast<uint32_t>(targetVertices.size());
            targetVertices.reserve(targetVertices.size() + descriptor.vertices.size());
            for (const glm::vec3& vertex : descriptor.vertices) {
                targetVertices.push_back(glm::vec3(transform * glm::vec4(vertex, 1.0f)));
            }
            targetIndices.reserve(targetIndices.size() + descriptor.indices.size());
            for (uint32_t index : descriptor.indices) {
                targetIndices.push_back(baseVertex + index);
            }

            if (descriptor.role == SceneNavigationSourceRole::Walkable) {
                diagnostics_.walkableVertexCount += static_cast<uint32_t>(descriptor.vertices.size());
                diagnostics_.walkableTriangleCount += static_cast<uint32_t>(descriptor.indices.size() / 3);
            } else {
                diagnostics_.blockerVertexCount += static_cast<uint32_t>(descriptor.vertices.size());
                diagnostics_.blockerTriangleCount += static_cast<uint32_t>(descriptor.indices.size() / 3);
            }
            ++diagnostics_.includedSourceCount;

            if (request.captureDebug) {
                SceneNavigationDebugRequest boundsDebug;
                boundsDebug.type = SceneNavigationDebugRequestType::SourceBounds;
                boundsDebug.source = handle;
                boundsDebug.coord = request.coord;
                boundsDebug.bounds = sourceWorldBounds;
                boundsDebug.message = descriptor.debugName;
                appendDebug(std::move(boundsDebug));
            }
        };

        for (uint32_t index = 0; index < sources_.size(); ++index) {
            SourceRecord& source = sources_[index];
            if (source.occupied && source.descriptor.role == SceneNavigationSourceRole::Walkable) {
                appendSource(source, {index, source.generation});
            }
        }
        for (uint32_t index = 0; index < sources_.size(); ++index) {
            SourceRecord& source = sources_[index];
            if (source.occupied && source.descriptor.role == SceneNavigationSourceRole::Blocker) {
                appendSource(source, {index, source.generation});
            }
        }

        refreshDiagnostics();
        return result;
    }

    SceneNavigationGeometryDiagnostics SceneNavigationGeometryRegistry::diagnostics() const
    {
        return diagnostics_;
    }

    std::vector<SceneNavigationDebugRequest> SceneNavigationGeometryRegistry::debugRequests() const
    {
        return debugRequests_;
    }

    void SceneNavigationGeometryRegistry::clearDebugRequests()
    {
        debugRequests_.clear();
        refreshDiagnostics();
    }

    SceneNavigationGeometryRegistry::SourceRecord* SceneNavigationGeometryRegistry::record(
        SceneNavigationSourceHandle source)
    {
        if (!isValid(source) || source.index >= sources_.size()) {
            return nullptr;
        }
        SourceRecord& sourceRecord = sources_[source.index];
        if (!sourceRecord.occupied || sourceRecord.generation != source.generation) {
            return nullptr;
        }
        return &sourceRecord;
    }

    const SceneNavigationGeometryRegistry::SourceRecord* SceneNavigationGeometryRegistry::record(
        SceneNavigationSourceHandle source) const
    {
        if (!isValid(source) || source.index >= sources_.size()) {
            return nullptr;
        }
        const SourceRecord& sourceRecord = sources_[source.index];
        if (!sourceRecord.occupied || sourceRecord.generation != source.generation) {
            return nullptr;
        }
        return &sourceRecord;
    }

    uint32_t SceneNavigationGeometryRegistry::nextGeneration(uint32_t generation) const
    {
        const uint32_t next = generation + 1;
        return next == 0 ? 1 : next;
    }

    std::optional<Renderer::Aabb> SceneNavigationGeometryRegistry::localBounds(const SourceRecord& source) const
    {
        if (source.descriptor.localBounds.has_value()) {
            if (!validAabb(*source.descriptor.localBounds)) {
                return std::nullopt;
            }
            return *source.descriptor.localBounds;
        }
        if (source.descriptor.vertices.empty() || !finiteGeometry(source.descriptor)) {
            return std::nullopt;
        }
        const Renderer::Aabb bounds = boundsForVertices(source.descriptor.vertices);
        if (!validAabb(bounds)) {
            return std::nullopt;
        }
        return bounds;
    }

    std::optional<Renderer::Aabb> SceneNavigationGeometryRegistry::worldBounds(Scene& scene, SourceRecord& source)
    {
        std::optional<Renderer::Aabb> bounds = localBounds(source);
        if (!bounds.has_value()) {
            return std::nullopt;
        }

        glm::mat4 transform{1.0f};
        if (source.descriptor.actor.has_value()) {
            std::optional<glm::mat4> world = scene.worldMatrix(*source.descriptor.actor);
            if (!world.has_value() || !finiteMat4(*world)) {
                return std::nullopt;
            }
            transform = *world;
        }
        Renderer::Aabb transformed = transformBounds(*bounds, transform);
        if (!validAabb(transformed)) {
            return std::nullopt;
        }
        return transformed;
    }

    void SceneNavigationGeometryRegistry::markChunksForBounds(const Renderer::Aabb& bounds, float chunkSize)
    {
        if (!validAabb(bounds) || !std::isfinite(chunkSize) || chunkSize <= 0.0f) {
            return;
        }

        std::unordered_set<uint64_t> existing;
        existing.reserve(dirtyChunks_.size());
        for (ChunkCoord coord : dirtyChunks_) {
            existing.insert(chunkKey(coord));
        }

        const int minX = coordForAxis(bounds.min.x, chunkSize);
        const int maxX = coordForAxis(bounds.max.x, chunkSize);
        const int minZ = coordForAxis(bounds.min.z, chunkSize);
        const int maxZ = coordForAxis(bounds.max.z, chunkSize);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                ChunkCoord coord{x, z};
                if (existing.insert(chunkKey(coord)).second) {
                    dirtyChunks_.push_back(coord);
                    SceneNavigationDebugRequest debug;
                    debug.type = SceneNavigationDebugRequestType::DirtyChunk;
                    debug.coord = coord;
                    debug.message = "Navigation source marked chunk dirty";
                    appendDebug(std::move(debug));
                }
            }
        }
        std::ranges::sort(dirtyChunks_, [](ChunkCoord lhs, ChunkCoord rhs) {
            if (lhs.z != rhs.z) {
                return lhs.z < rhs.z;
            }
            return lhs.x < rhs.x;
        });
    }

    void SceneNavigationGeometryRegistry::refreshDiagnostics()
    {
        diagnostics_.registeredSourceCount = 0;
        diagnostics_.enabledSourceCount = 0;
        diagnostics_.walkableSourceCount = 0;
        diagnostics_.blockerSourceCount = 0;
        diagnostics_.dirtySourceCount = 0;
        for (const SourceRecord& source : sources_) {
            if (!source.occupied) {
                continue;
            }
            ++diagnostics_.registeredSourceCount;
            if (source.descriptor.enabled) {
                ++diagnostics_.enabledSourceCount;
            }
            if (source.descriptor.role == SceneNavigationSourceRole::Walkable) {
                ++diagnostics_.walkableSourceCount;
            } else {
                ++diagnostics_.blockerSourceCount;
            }
            if (source.dirtyReason != SceneNavigationDirtyReason::None) {
                ++diagnostics_.dirtySourceCount;
            }
        }
        diagnostics_.dirtyChunkCount = static_cast<uint32_t>(dirtyChunks_.size());
        diagnostics_.debugRequestCount = static_cast<uint32_t>(debugRequests_.size());
    }

    void SceneNavigationGeometryRegistry::appendDebug(SceneNavigationDebugRequest request)
    {
        debugRequests_.push_back(std::move(request));
        diagnostics_.debugRequestCount = static_cast<uint32_t>(debugRequests_.size());
    }
}
