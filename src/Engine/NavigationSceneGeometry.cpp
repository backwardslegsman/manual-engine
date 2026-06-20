#include "Engine/NavigationSceneGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
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

    Renderer::Aabb paddedBounds(Renderer::Aabb bounds, float padding)
    {
        const float safePadding = std::isfinite(padding) ? std::max(padding, 0.0f) : 0.0f;
        bounds.min -= glm::vec3{safePadding};
        bounds.max += glm::vec3{safePadding};
        return bounds;
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

    Renderer::Aabb boundsForTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        Renderer::Aabb bounds = emptyBounds();
        includePoint(bounds, a);
        includePoint(bounds, b);
        includePoint(bounds, c);
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

    bool triangleSlopeAccepted(
        const glm::vec3& a,
        const glm::vec3& b,
        const glm::vec3& c,
        float maxWalkableSlopeDegrees,
        bool& degenerate)
    {
        degenerate = false;
        const glm::vec3 cross = glm::cross(b - a, c - a);
        const float length = glm::length(cross);
        if (!std::isfinite(length) || length <= 0.000001f) {
            degenerate = true;
            return false;
        }

        const glm::vec3 normal = cross / length;
        const float up = std::clamp(std::abs(normal.y), 0.0f, 1.0f);
        const float slopeDegrees = glm::degrees(std::acos(up));
        const float safeSlope = std::isfinite(maxWalkableSlopeDegrees)
            ? std::clamp(maxWalkableSlopeDegrees, 0.0f, 90.0f)
            : 45.0f;
        return slopeDegrees <= safeSlope;
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
        const NavigationTerrainBuildData* terrainBase,
        const SceneNavigationGeometryBuildSettings& settings)
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
        diagnostics_.consideredTriangleCount = 0;
        diagnostics_.boundsCulledTriangleCount = 0;
        diagnostics_.slopeCulledTriangleCount = 0;
        diagnostics_.degenerateTriangleCount = 0;
        diagnostics_.appendedTriangleCount = 0;
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

        const Renderer::Aabb effectiveBounds = paddedBounds(request.bounds, settings.tileBoundsPadding);
        if (!validAabb(request.bounds) || !validAabb(effectiveBounds)) {
            diagnostics_.warnings.push_back("Invalid navigation scene geometry build bounds");
            refreshDiagnostics();
            return std::nullopt;
        }

        NavigationTerrainBuildData result;
        result.coord = request.coord;
        result.bounds = request.bounds;

        if (terrainBase) {
            result.rasterizationBounds = terrainBase->rasterizationBounds;
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
            if (!intersects(sourceWorldBounds, effectiveBounds)) {
                ++diagnostics_.outOfBoundsSourceCount;
                ++diagnostics_.skippedSourceCount;
                return;
            }

            std::vector<glm::vec3> worldVertices;
            worldVertices.reserve(descriptor.vertices.size());
            for (const glm::vec3& vertex : descriptor.vertices) {
                const glm::vec3 worldVertex = glm::vec3(transform * glm::vec4(vertex, 1.0f));
                if (!finiteVec3(worldVertex)) {
                    ++diagnostics_.nonFiniteGeometryCount;
                    ++diagnostics_.skippedSourceCount;
                    return;
                }
                worldVertices.push_back(worldVertex);
            }

            std::vector<glm::vec3>& targetVertices = descriptor.role == SceneNavigationSourceRole::Walkable
                ? result.vertices
                : result.blockingVertices;
            std::vector<uint32_t>& targetIndices = descriptor.role == SceneNavigationSourceRole::Walkable
                ? result.indices
                : result.blockingIndices;

            std::unordered_map<uint32_t, uint32_t> remappedIndices;
            const auto appendVertex = [&](uint32_t sourceIndex) {
                if (const auto found = remappedIndices.find(sourceIndex); found != remappedIndices.end()) {
                    return found->second;
                }
                const uint32_t targetIndex = static_cast<uint32_t>(targetVertices.size());
                targetVertices.push_back(worldVertices[sourceIndex]);
                remappedIndices.emplace(sourceIndex, targetIndex);
                return targetIndex;
            };

            uint32_t acceptedTriangles = 0;
            for (size_t index = 0; index + 2 < descriptor.indices.size(); index += 3) {
                const uint32_t ia = descriptor.indices[index];
                const uint32_t ib = descriptor.indices[index + 1];
                const uint32_t ic = descriptor.indices[index + 2];
                ++diagnostics_.consideredTriangleCount;

                const glm::vec3& a = worldVertices[ia];
                const glm::vec3& b = worldVertices[ib];
                const glm::vec3& c = worldVertices[ic];
                const Renderer::Aabb triangleBounds = boundsForTriangle(a, b, c);
                if (!validAabb(triangleBounds) || !intersects(triangleBounds, effectiveBounds)) {
                    ++diagnostics_.boundsCulledTriangleCount;
                    continue;
                }

                if (descriptor.role == SceneNavigationSourceRole::Walkable) {
                    bool degenerate = false;
                    if (!triangleSlopeAccepted(a, b, c, settings.maxWalkableSlopeDegrees, degenerate)) {
                        if (degenerate) {
                            ++diagnostics_.degenerateTriangleCount;
                        } else {
                            ++diagnostics_.slopeCulledTriangleCount;
                        }
                        continue;
                    }
                }

                targetIndices.push_back(appendVertex(ia));
                targetIndices.push_back(appendVertex(ib));
                targetIndices.push_back(appendVertex(ic));
                ++acceptedTriangles;
            }

            if (acceptedTriangles == 0) {
                ++diagnostics_.skippedSourceCount;
                return;
            }
            if (descriptor.role == SceneNavigationSourceRole::Walkable) {
                diagnostics_.walkableVertexCount += static_cast<uint32_t>(remappedIndices.size());
                diagnostics_.walkableTriangleCount += acceptedTriangles;
            } else {
                diagnostics_.blockerVertexCount += static_cast<uint32_t>(remappedIndices.size());
                diagnostics_.blockerTriangleCount += acceptedTriangles;
            }
            diagnostics_.appendedTriangleCount += acceptedTriangles;
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
