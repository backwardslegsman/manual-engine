#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"

namespace Engine {
    inline constexpr const char* TerrainPhysicsColliderAdapterVersion = "terrain_physics_collider_adapter_t6_v1";

    struct TerrainPhysicsColliderHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    enum class TerrainPhysicsColliderDirtyReason : uint32_t {
        Geometry = 1u << 0u,
        Settings = 1u << 1u,
        EnableState = 1u << 2u,
        Removal = 1u << 3u,
    };

    struct TerrainPhysicsSourceIdentity {
        AssetId sourceId;
        std::string sourceHash;
        AssetImportSettingsKey importSettings;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
    };

    struct TerrainPhysicsColliderBuildRequest {
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t sourceResolution = 0;
        uint32_t colliderResolution = 0;
        std::vector<float> heights;
        TerrainPhysicsSourceIdentity identity;
    };

    struct TerrainPhysicsColliderPayload {
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t sourceResolution = 0;
        uint32_t colliderResolution = 0;
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;
        TerrainDatasetBounds bounds;
        TerrainPhysicsSourceIdentity identity;
    };

    struct TerrainPhysicsColliderBuildDiagnostics {
        bool valid = false;
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        uint32_t sourceResolution = 0;
        uint32_t colliderResolution = 0;
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        TerrainDatasetBounds bounds;
        std::string message;
    };

    struct TerrainPhysicsColliderBuildResult {
        bool success = false;
        std::optional<TerrainPhysicsColliderPayload> payload;
        TerrainPhysicsColliderBuildDiagnostics diagnostics;
    };

    struct TerrainPhysicsColliderCreateDescriptor {
        bool enabled = true;
        ScenePhysicsLayer layer{4u};
        ScenePhysicsMaterial material;
        std::string debugName;
    };

    struct TerrainPhysicsColliderBinding {
        TerrainSourceChunkId chunkId;
        SceneActorHandle actor;
        ScenePhysicsBodyHandle body;
        SceneColliderHandle collider;
        std::string debugName;
    };

    struct TerrainPhysicsColliderDirtyChunk {
        TerrainSourceChunkId chunkId;
        uint32_t reasons = 0;
    };

    [[nodiscard]] constexpr bool isValid(TerrainPhysicsColliderHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(TerrainPhysicsColliderHandle lhs, TerrainPhysicsColliderHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(TerrainPhysicsColliderHandle lhs, TerrainPhysicsColliderHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] std::optional<TerrainPhysicsColliderBuildRequest> terrainPhysicsColliderRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        uint32_t colliderResolution,
        TerrainPhysicsSourceIdentity identity);
    [[nodiscard]] TerrainPhysicsColliderBuildResult buildTerrainPhysicsCollider(
        const TerrainPhysicsColliderBuildRequest& request);

    class TerrainPhysicsColliderAdapter {
    public:
        [[nodiscard]] TerrainPhysicsColliderHandle createStaticCollider(
            Scene& scene,
            ScenePhysicsWorld& physics,
            const TerrainPhysicsColliderPayload& payload,
            const TerrainPhysicsColliderCreateDescriptor& descriptor = {});
        bool destroyCollider(
            Scene& scene,
            ScenePhysicsWorld& physics,
            TerrainPhysicsColliderHandle handle);
        bool destroyColliderForChunk(
            Scene& scene,
            ScenePhysicsWorld& physics,
            TerrainSourceChunkId chunkId);
        void releaseAll(Scene& scene, ScenePhysicsWorld& physics);

        [[nodiscard]] bool contains(TerrainPhysicsColliderHandle handle) const;
        [[nodiscard]] std::optional<TerrainPhysicsColliderBinding> binding(
            TerrainPhysicsColliderHandle handle) const;
        [[nodiscard]] std::optional<TerrainPhysicsColliderHandle> colliderForChunk(
            TerrainSourceChunkId chunkId) const;
        [[nodiscard]] std::vector<TerrainPhysicsColliderHandle> colliders() const;

        void markDirty(TerrainSourceChunkId chunkId, TerrainPhysicsColliderDirtyReason reason);
        [[nodiscard]] std::vector<TerrainPhysicsColliderDirtyChunk> dirtyChunks() const;
        void clearDirty();

    private:
        struct BindingRecord {
            uint32_t generation = 0;
            bool occupied = false;
            TerrainPhysicsColliderBinding binding;
        };

        [[nodiscard]] BindingRecord* record(TerrainPhysicsColliderHandle handle);
        [[nodiscard]] const BindingRecord* record(TerrainPhysicsColliderHandle handle) const;
        [[nodiscard]] TerrainPhysicsColliderHandle storeBinding(TerrainPhysicsColliderBinding binding);
        bool destroyBinding(
            Scene& scene,
            ScenePhysicsWorld& physics,
            uint32_t index,
            bool markRemoval);
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;

        std::vector<BindingRecord> bindings_;
        std::vector<TerrainPhysicsColliderDirtyChunk> dirtyChunks_;
    };
}
