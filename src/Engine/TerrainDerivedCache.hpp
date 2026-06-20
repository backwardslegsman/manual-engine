#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"

namespace Engine {
    enum class TerrainDerivedCachePolicy {
        Disabled,
        ReadOnly,
        GenerateOnMiss,
        Refresh,
    };

    enum class TerrainDerivedKind {
        ChunkHeights,
        LodMesh,
        PhysicsCollider,
    };

    enum class TerrainDerivedCacheStatus {
        Hit,
        Miss,
        Stale,
        Corrupt,
        WriteSuccess,
        WriteFailed,
        Cancelled,
    };

    struct TerrainDerivedCacheSettings {
        std::filesystem::path rootPath = "generated/terrain_cache";
        uint32_t formatVersion = 1;
        std::string terrainImportVersion = "heightmap_import_t1";
        std::string chunkPayloadVersion = "terrain_chunk_payload_t3";
        std::string lodMeshPayloadVersion = "terrain_lod_mesh_payload_t3";
        std::string physicsColliderPayloadVersion = "terrain_physics_collider_payload_s4";
        TerrainDerivedCachePolicy policy = TerrainDerivedCachePolicy::Disabled;
    };

    struct TerrainDerivedCacheManifest {
        TerrainDerivedCacheSettings settings;
        TerrainDerivedKind kind = TerrainDerivedKind::ChunkHeights;
        TerrainSourceChunkId chunkId;
        AssetImportSettingsKey importSettings;
        std::string sourceHash;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
        float chunkSize = 0.0f;
        uint32_t chunkResolution = 0;
        uint32_t lodIndex = 0;
        uint32_t renderResolution = 0;
        std::string payloadVersion;
        std::string payloadFileName;
        std::string payloadHash;
        std::string identityHash;
    };

    struct TerrainCpuMeshVertex {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
        glm::vec2 uv0{0.0f};
        glm::vec2 uv1{0.0f};
        uint32_t color = 0xffffffff;
    };

    struct TerrainCachedChunkPayload {
        TerrainSourceChunkId chunkId;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t resolution = 0;
        std::vector<float> heights;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
        AssetImportSettingsKey importSettings;
        std::vector<std::string> warnings;
    };

    struct TerrainCachedLodMeshPayload {
        TerrainSourceChunkId chunkId;
        uint32_t lodIndex = 0;
        uint32_t renderResolution = 0;
        TerrainDatasetBounds bounds;
        std::vector<TerrainCpuMeshVertex> vertices;
        std::vector<uint32_t> indices;
        AssetImportSettingsKey importSettings;
    };

    struct TerrainLodMeshBuildSettings {
        uint32_t lodIndex = 0;
        uint32_t renderResolution = 17;
        float skirtDepth = 0.0f;
    };

    struct TerrainDerivedCacheOperationResult {
        TerrainDerivedKind kind = TerrainDerivedKind::ChunkHeights;
        TerrainDerivedCacheStatus status = TerrainDerivedCacheStatus::Miss;
        std::filesystem::path path;
        std::string message;
        uint64_t bytes = 0;
    };

    struct TerrainDerivedCacheChunkReadResult : TerrainDerivedCacheOperationResult {
        std::optional<TerrainCachedChunkPayload> payload;
    };

    struct TerrainDerivedCacheLodMeshReadResult : TerrainDerivedCacheOperationResult {
        std::optional<TerrainCachedLodMeshPayload> payload;
    };

    struct TerrainDerivedCachePhysicsColliderReadResult : TerrainDerivedCacheOperationResult {
        std::optional<TerrainPhysicsColliderPayload> payload;
    };

    using TerrainDerivedCacheWriteResult = TerrainDerivedCacheOperationResult;

    struct TerrainDerivedCacheStats {
        uint32_t hits = 0;
        uint32_t misses = 0;
        uint32_t stale = 0;
        uint32_t corrupt = 0;
        uint32_t writes = 0;
        uint32_t cancelled = 0;
        uint64_t bytesRead = 0;
        uint64_t bytesWritten = 0;
        std::filesystem::path lastPath;
        std::string lastMessage;
    };

    class TerrainDerivedCache {
    public:
        explicit TerrainDerivedCache(TerrainDerivedCacheSettings settings = {});

        [[nodiscard]] static TerrainDerivedCacheManifest buildChunkManifest(
            TerrainDerivedCacheSettings settings,
            const TerrainCachedChunkPayload& payload,
            std::string sourceHash = {});
        [[nodiscard]] static TerrainDerivedCacheManifest buildLodMeshManifest(
            TerrainDerivedCacheSettings settings,
            const TerrainCachedChunkPayload& sourceChunk,
            const TerrainLodMeshBuildSettings& lod,
            std::string sourceHash = {});
        [[nodiscard]] static TerrainDerivedCacheManifest buildPhysicsColliderManifest(
            TerrainDerivedCacheSettings settings,
            const TerrainPhysicsColliderPayload& payload,
            std::string sourceHash = {});
        [[nodiscard]] static std::filesystem::path cacheRoot(const TerrainDerivedCacheManifest& manifest);
        [[nodiscard]] static std::string hashFile(const std::filesystem::path& path);
        [[nodiscard]] static TerrainDerivedCacheChunkReadResult readChunk(const TerrainDerivedCacheManifest& manifest);
        [[nodiscard]] static TerrainDerivedCacheWriteResult writeChunk(
            TerrainDerivedCacheManifest manifest,
            const TerrainCachedChunkPayload& payload);
        [[nodiscard]] static TerrainDerivedCacheLodMeshReadResult readLodMesh(const TerrainDerivedCacheManifest& manifest);
        [[nodiscard]] static TerrainDerivedCacheWriteResult writeLodMesh(
            TerrainDerivedCacheManifest manifest,
            const TerrainCachedLodMeshPayload& payload);
        [[nodiscard]] static TerrainDerivedCachePhysicsColliderReadResult readPhysicsCollider(
            const TerrainDerivedCacheManifest& manifest);
        [[nodiscard]] static TerrainDerivedCacheWriteResult writePhysicsCollider(
            TerrainDerivedCacheManifest manifest,
            const TerrainPhysicsColliderPayload& payload);

        void recordResult(const TerrainDerivedCacheOperationResult& result);
        const TerrainDerivedCacheStats& stats() const;
        void clearStats();

    private:
        TerrainDerivedCacheSettings settings_;
        TerrainDerivedCacheStats stats_;
    };

    [[nodiscard]] TerrainCachedChunkPayload terrainCachedChunkPayloadFromChunk(
        const TerrainChunkData& chunk,
        const AssetImportSettingsKey& settings = {});
    [[nodiscard]] TerrainCachedChunkPayload terrainCachedChunkPayloadFromImported(
        const TerrainImportedChunk& chunk,
        TerrainDatasetSourceType sourceType,
        const AssetImportSettingsKey& settings = {});
    [[nodiscard]] TerrainCachedLodMeshPayload buildTerrainCachedLodMesh(
        const TerrainCachedChunkPayload& chunk,
        const TerrainLodMeshBuildSettings& settings);
}
