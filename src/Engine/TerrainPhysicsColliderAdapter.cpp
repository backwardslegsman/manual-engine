#include "Engine/TerrainPhysicsColliderAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace Engine {
    namespace {
        constexpr AssetId LegacyProceduralTerrainSourceId{0x7465727261696e01ull};

        [[nodiscard]] bool finite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool validHeights(uint32_t resolution, const std::vector<float>& heights)
        {
            return resolution >= 2 &&
                heights.size() == static_cast<size_t>(resolution) * resolution &&
                std::ranges::all_of(heights, [](float height) { return std::isfinite(height); });
        }

        [[nodiscard]] std::optional<float> sampleHeight(
            const glm::vec3& origin,
            float size,
            uint32_t resolution,
            const std::vector<float>& heights,
            float worldX,
            float worldZ)
        {
            if (resolution < 2 ||
                size <= 0.0f ||
                heights.size() != static_cast<size_t>(resolution) * resolution) {
                return std::nullopt;
            }

            const float localX = std::clamp(worldX - origin.x, 0.0f, size);
            const float localZ = std::clamp(worldZ - origin.z, 0.0f, size);
            const float normalizedX = localX / size * static_cast<float>(resolution - 1u);
            const float normalizedZ = localZ / size * static_cast<float>(resolution - 1u);
            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), resolution - 1u);
            const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), resolution - 1u);
            const uint32_t x1 = std::min(x0 + 1u, resolution - 1u);
            const uint32_t z1 = std::min(z0 + 1u, resolution - 1u);
            const float tx = normalizedX - static_cast<float>(x0);
            const float tz = normalizedZ - static_cast<float>(z0);
            const auto heightAt = [&](uint32_t x, uint32_t z) {
                return heights[static_cast<size_t>(z) * resolution + x];
            };
            const float h00 = heightAt(x0, z0);
            const float h10 = heightAt(x1, z0);
            const float h01 = heightAt(x0, z1);
            const float h11 = heightAt(x1, z1);
            const float hx0 = h00 + (h10 - h00) * tx;
            const float hx1 = h01 + (h11 - h01) * tx;
            return hx0 + (hx1 - hx0) * tz;
        }

        [[nodiscard]] TerrainDatasetBounds boundsFor(
            const glm::vec3& origin,
            float size,
            const std::vector<float>& heights)
        {
            const auto [minIt, maxIt] = std::minmax_element(heights.begin(), heights.end());
            const float minHeight = minIt != heights.end() ? *minIt : origin.y;
            const float maxHeight = maxIt != heights.end() ? *maxIt : origin.y;
            return {
                {origin.x, minHeight, origin.z},
                {origin.x + size, maxHeight, origin.z + size},
            };
        }

        [[nodiscard]] bool sameChunk(TerrainSourceChunkId lhs, TerrainSourceChunkId rhs)
        {
            return lhs == rhs;
        }

        [[nodiscard]] uint32_t reasonBit(TerrainPhysicsColliderDirtyReason reason)
        {
            return static_cast<uint32_t>(reason);
        }
    }

    TerrainPhysicsSourceIdentity legacyProceduralTerrainPhysicsIdentity(
        const TerrainSettings& settings,
        uint32_t colliderResolution)
    {
        TerrainPhysicsSourceIdentity identity;
        identity.sourceId = LegacyProceduralTerrainSourceId;
        identity.sourceType = TerrainDatasetSourceType::Procedural;
        identity.importSettings = {"legacy_procedural_terrain", "t6", "physics_collider_adapter"};

        const uint32_t resolvedColliderResolution =
            colliderResolution > 0 ? colliderResolution : std::max(settings.navigationResolution, 2u);
        std::ostringstream stream;
        stream << "chunk=" << settings.chunkSize
               << ";resolution=" << settings.resolution
               << ";heightScale=" << settings.heightScale
               << ";navResolution=" << settings.navigationResolution
               << ";colliderResolution=" << resolvedColliderResolution;
        identity.sourceHash = stream.str();
        return identity;
    }

    std::optional<TerrainPhysicsColliderBuildRequest> terrainPhysicsColliderRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        uint32_t colliderResolution,
        TerrainPhysicsSourceIdentity identity)
    {
        const std::optional<TerrainChunkData> data = dataset.chunk(chunk);
        if (!data ||
            data->size <= 0.0f ||
            !validHeights(data->resolution, data->heights)) {
            return std::nullopt;
        }

        TerrainPhysicsColliderBuildRequest request;
        request.chunkId = data->id;
        request.coord = {data->coord.x, data->coord.z};
        request.origin = data->origin;
        request.size = data->size;
        request.sourceResolution = data->resolution;
        request.colliderResolution = std::max(colliderResolution, 2u);
        request.heights = data->heights;
        request.identity = std::move(identity);
        request.identity.sourceType = data->sourceType;
        return request;
    }

    std::optional<TerrainPhysicsColliderBuildRequest> terrainPhysicsColliderRequestFromGeneratedTile(
        const GeneratedTerrainTileData& generated,
        uint32_t colliderResolution,
        TerrainPhysicsSourceIdentity identity)
    {
        if (generated.size <= 0.0f || !validHeights(generated.resolution, generated.heights)) {
            return std::nullopt;
        }

        TerrainPhysicsColliderBuildRequest request;
        request.chunkId = {identity.sourceId, {generated.coord.x, generated.coord.z}};
        request.coord = generated.coord;
        request.origin = generated.origin;
        request.size = generated.size;
        request.sourceResolution = generated.resolution;
        request.colliderResolution = std::max(colliderResolution, 2u);
        request.heights = generated.heights;
        request.identity = std::move(identity);
        return request;
    }

    std::optional<TerrainPhysicsColliderBuildRequest> terrainPhysicsColliderRequestFromTerrainSystemTile(
        const TerrainSystem& terrain,
        TerrainTileHandle tile,
        uint32_t colliderResolution,
        TerrainPhysicsSourceIdentity identity)
    {
        const std::optional<TerrainRenderMeshBuildInput> input = terrain.renderMeshBuildInput(tile, 0);
        if (!input ||
            input->size <= 0.0f ||
            !validHeights(input->cpuResolution, input->heights)) {
            return std::nullopt;
        }

        TerrainPhysicsColliderBuildRequest request;
        request.chunkId = {identity.sourceId, {input->coord.x, input->coord.z}};
        request.coord = input->coord;
        request.origin = input->origin;
        request.size = input->size;
        request.sourceResolution = input->cpuResolution;
        request.colliderResolution = std::max(colliderResolution, 2u);
        request.heights = input->heights;
        request.identity = std::move(identity);
        return request;
    }

    TerrainPhysicsColliderBuildResult buildTerrainPhysicsCollider(
        const TerrainPhysicsColliderBuildRequest& request)
    {
        TerrainPhysicsColliderBuildResult result;
        result.diagnostics.chunkId = request.chunkId;
        result.diagnostics.coord = request.coord;
        result.diagnostics.sourceResolution = request.sourceResolution;
        result.diagnostics.colliderResolution = request.colliderResolution;
        result.diagnostics.bounds = boundsFor(request.origin, request.size, request.heights);

        if (!finite(request.origin) ||
            request.size <= 0.0f ||
            request.sourceResolution < 2 ||
            request.colliderResolution < 2 ||
            !validHeights(request.sourceResolution, request.heights)) {
            result.diagnostics.message = "Invalid terrain physics collider build request.";
            return result;
        }

        TerrainPhysicsColliderPayload payload;
        payload.chunkId = request.chunkId;
        payload.coord = request.coord;
        payload.origin = request.origin;
        payload.size = request.size;
        payload.sourceResolution = request.sourceResolution;
        payload.colliderResolution = request.colliderResolution;
        payload.bounds = result.diagnostics.bounds;
        payload.identity = request.identity;

        const uint32_t resolution = request.colliderResolution;
        payload.vertices.reserve(static_cast<size_t>(resolution) * resolution);
        payload.indices.reserve(static_cast<size_t>(resolution - 1u) * (resolution - 1u) * 6u);
        const float spacing = request.size / static_cast<float>(resolution - 1u);
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = request.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = request.origin.z + static_cast<float>(z) * spacing;
                const float height = sampleHeight(
                    request.origin,
                    request.size,
                    request.sourceResolution,
                    request.heights,
                    worldX,
                    worldZ).value_or(request.heights.front());
                payload.vertices.push_back({worldX, height, worldZ});
            }
        }

        for (uint32_t z = 0; z + 1u < resolution; ++z) {
            for (uint32_t x = 0; x + 1u < resolution; ++x) {
                const uint32_t topLeft = z * resolution + x;
                const uint32_t topRight = topLeft + 1u;
                const uint32_t bottomLeft = (z + 1u) * resolution + x;
                const uint32_t bottomRight = bottomLeft + 1u;
                payload.indices.push_back(topLeft);
                payload.indices.push_back(bottomLeft);
                payload.indices.push_back(topRight);
                payload.indices.push_back(topRight);
                payload.indices.push_back(bottomLeft);
                payload.indices.push_back(bottomRight);
            }
        }

        result.success = true;
        result.diagnostics.valid = true;
        result.diagnostics.vertexCount = static_cast<uint32_t>(payload.vertices.size());
        result.diagnostics.triangleCount = static_cast<uint32_t>(payload.indices.size() / 3u);
        result.diagnostics.message = "Terrain physics collider generated.";
        result.payload = std::move(payload);
        return result;
    }

    TerrainPhysicsColliderHandle TerrainPhysicsColliderAdapter::createStaticCollider(
        Scene& scene,
        ScenePhysicsWorld& physics,
        const TerrainPhysicsColliderPayload& payload,
        const TerrainPhysicsColliderCreateDescriptor& descriptor)
    {
        if (payload.vertices.size() < 3 ||
            payload.indices.size() < 3 ||
            payload.indices.size() % 3 != 0 ||
            payload.colliderResolution < 2 ||
            payload.size <= 0.0f ||
            !std::ranges::all_of(payload.vertices, finite) ||
            !std::ranges::all_of(payload.indices, [&](uint32_t index) { return index < payload.vertices.size(); })) {
            return {};
        }

        if (const std::optional<TerrainPhysicsColliderHandle> existing = colliderForChunk(payload.chunkId)) {
            const BindingRecord* existingRecord = record(*existing);
            if (existingRecord) {
                destroyBinding(scene, physics, existing->index, false);
            }
        }

        const SceneActorHandle actor = scene.createActor();
        if (!scene.contains(actor)) {
            return {};
        }

        ScenePhysicsBodyDescriptor bodyDescriptor;
        bodyDescriptor.actor = actor;
        bodyDescriptor.motionType = ScenePhysicsMotionType::Static;
        bodyDescriptor.enabled = descriptor.enabled;
        bodyDescriptor.layer = descriptor.layer;
        bodyDescriptor.material = descriptor.material;

        const ScenePhysicsBodyHandle body = physics.createBody(bodyDescriptor);
        if (!isValid(body)) {
            scene.destroyActor(actor);
            return {};
        }

        ScenePhysicsShapeDescriptor shape;
        shape.type = ScenePhysicsShapeType::StaticTriangleMesh;
        shape.triangleMesh.vertices = payload.vertices;
        shape.triangleMesh.indices = payload.indices;
        const SceneColliderHandle collider = physics.attachCollider(body, shape);
        if (!isValid(collider)) {
            physics.destroyBody(body);
            scene.destroyActor(actor);
            return {};
        }

        TerrainPhysicsColliderBinding binding;
        binding.chunkId = payload.chunkId;
        binding.actor = actor;
        binding.body = body;
        binding.collider = collider;
        binding.debugName = descriptor.debugName;
        TerrainPhysicsColliderHandle handle = storeBinding(std::move(binding));
        markDirty(payload.chunkId, TerrainPhysicsColliderDirtyReason::Geometry);
        return handle;
    }

    bool TerrainPhysicsColliderAdapter::destroyCollider(
        Scene& scene,
        ScenePhysicsWorld& physics,
        TerrainPhysicsColliderHandle handle)
    {
        const BindingRecord* binding = record(handle);
        if (!binding) {
            return false;
        }
        return destroyBinding(scene, physics, handle.index, true);
    }

    bool TerrainPhysicsColliderAdapter::destroyColliderForChunk(
        Scene& scene,
        ScenePhysicsWorld& physics,
        TerrainSourceChunkId chunkId)
    {
        const std::optional<TerrainPhysicsColliderHandle> handle = colliderForChunk(chunkId);
        return handle ? destroyCollider(scene, physics, *handle) : false;
    }

    void TerrainPhysicsColliderAdapter::releaseAll(Scene& scene, ScenePhysicsWorld& physics)
    {
        for (uint32_t index = 0; index < bindings_.size(); ++index) {
            if (bindings_[index].occupied) {
                destroyBinding(scene, physics, index, true);
            }
        }
    }

    bool TerrainPhysicsColliderAdapter::contains(TerrainPhysicsColliderHandle handle) const
    {
        return record(handle) != nullptr;
    }

    std::optional<TerrainPhysicsColliderBinding> TerrainPhysicsColliderAdapter::binding(
        TerrainPhysicsColliderHandle handle) const
    {
        const BindingRecord* bindingRecord = record(handle);
        if (!bindingRecord) {
            return std::nullopt;
        }
        return bindingRecord->binding;
    }

    std::optional<TerrainPhysicsColliderHandle> TerrainPhysicsColliderAdapter::colliderForChunk(
        TerrainSourceChunkId chunkId) const
    {
        for (uint32_t index = 0; index < bindings_.size(); ++index) {
            const BindingRecord& binding = bindings_[index];
            if (binding.occupied && sameChunk(binding.binding.chunkId, chunkId)) {
                return TerrainPhysicsColliderHandle{index, binding.generation};
            }
        }
        return std::nullopt;
    }

    std::vector<TerrainPhysicsColliderHandle> TerrainPhysicsColliderAdapter::colliders() const
    {
        std::vector<TerrainPhysicsColliderHandle> handles;
        for (uint32_t index = 0; index < bindings_.size(); ++index) {
            const BindingRecord& binding = bindings_[index];
            if (binding.occupied) {
                handles.push_back({index, binding.generation});
            }
        }
        return handles;
    }

    void TerrainPhysicsColliderAdapter::markDirty(
        TerrainSourceChunkId chunkId,
        TerrainPhysicsColliderDirtyReason reason)
    {
        if (!isValid(chunkId.source)) {
            return;
        }
        for (TerrainPhysicsColliderDirtyChunk& dirty : dirtyChunks_) {
            if (sameChunk(dirty.chunkId, chunkId)) {
                dirty.reasons |= reasonBit(reason);
                return;
            }
        }
        dirtyChunks_.push_back({chunkId, reasonBit(reason)});
    }

    std::vector<TerrainPhysicsColliderDirtyChunk> TerrainPhysicsColliderAdapter::dirtyChunks() const
    {
        return dirtyChunks_;
    }

    void TerrainPhysicsColliderAdapter::clearDirty()
    {
        dirtyChunks_.clear();
    }

    TerrainPhysicsColliderAdapter::BindingRecord* TerrainPhysicsColliderAdapter::record(
        TerrainPhysicsColliderHandle handle)
    {
        if (!isValid(handle) || handle.index >= bindings_.size()) {
            return nullptr;
        }
        BindingRecord& binding = bindings_[handle.index];
        return binding.occupied && binding.generation == handle.generation ? &binding : nullptr;
    }

    const TerrainPhysicsColliderAdapter::BindingRecord* TerrainPhysicsColliderAdapter::record(
        TerrainPhysicsColliderHandle handle) const
    {
        if (!isValid(handle) || handle.index >= bindings_.size()) {
            return nullptr;
        }
        const BindingRecord& binding = bindings_[handle.index];
        return binding.occupied && binding.generation == handle.generation ? &binding : nullptr;
    }

    TerrainPhysicsColliderHandle TerrainPhysicsColliderAdapter::storeBinding(TerrainPhysicsColliderBinding binding)
    {
        for (uint32_t index = 0; index < bindings_.size(); ++index) {
            BindingRecord& record = bindings_[index];
            if (!record.occupied) {
                record.generation = nextGeneration(record.generation);
                record.occupied = true;
                record.binding = std::move(binding);
                return {index, record.generation};
            }
        }

        BindingRecord record;
        record.generation = 1;
        record.occupied = true;
        record.binding = std::move(binding);
        bindings_.push_back(std::move(record));
        return {static_cast<uint32_t>(bindings_.size() - 1u), bindings_.back().generation};
    }

    bool TerrainPhysicsColliderAdapter::destroyBinding(
        Scene& scene,
        ScenePhysicsWorld& physics,
        uint32_t index,
        bool markRemoval)
    {
        if (index >= bindings_.size() || !bindings_[index].occupied) {
            return false;
        }

        BindingRecord& binding = bindings_[index];
        const TerrainSourceChunkId chunkId = binding.binding.chunkId;
        if (isValid(binding.binding.body) && physics.contains(binding.binding.body)) {
            physics.destroyBody(binding.binding.body);
        }
        if (isValid(binding.binding.actor) && scene.contains(binding.binding.actor)) {
            scene.destroyActor(binding.binding.actor);
        }
        binding.occupied = false;
        binding.binding = {};
        if (markRemoval) {
            markDirty(chunkId, TerrainPhysicsColliderDirtyReason::Removal);
        }
        return true;
    }

    uint32_t TerrainPhysicsColliderAdapter::nextGeneration(uint32_t generation) const
    {
        const uint32_t next = generation + 1u;
        return next == 0 ? 1u : next;
    }
}
