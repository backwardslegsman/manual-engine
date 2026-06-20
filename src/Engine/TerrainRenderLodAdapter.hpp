#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainDerivedCache.hpp"

namespace Engine {
    struct TerrainRenderLodSourceIdentity {
        AssetId sourceId;
        std::string sourceHash;
        AssetImportSettingsKey importSettings;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
    };

    struct TerrainRenderLodBuildRequest {
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        uint64_t generation = 0;
        uint32_t lodIndex = 0;
        uint32_t renderResolution = 2;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t cpuResolution = 0;
        std::vector<float> heights;
        float skirtDepth = 0.0f;
        TerrainRenderLodSourceIdentity identity;
        TerrainDerivedCacheSettings cacheSettings;
    };

    struct TerrainRenderLodBuildDiagnostics {
        TerrainDerivedCacheStatus cacheStatus = TerrainDerivedCacheStatus::Miss;
        bool usedCache = false;
        bool generated = false;
        uint32_t cacheHitCount = 0;
        uint32_t cacheMissCount = 0;
        uint32_t cacheStaleCount = 0;
        uint32_t cacheCorruptCount = 0;
        uint32_t generatedCount = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        float buildMs = 0.0f;
        std::string message;
    };

    struct TerrainRenderLodBuildResult {
        bool success = false;
        TerrainRenderMeshBuildResult build;
        TerrainRenderLodBuildDiagnostics diagnostics;
    };

    [[nodiscard]] std::optional<TerrainRenderLodBuildRequest> renderLodRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        const TerrainLodMeshBuildSettings& lod,
        TerrainRenderLodSourceIdentity identity,
        uint64_t generation = 0,
        TerrainDerivedCacheSettings cacheSettings = {});

    [[nodiscard]] TerrainCachedChunkPayload cachedChunkPayloadFromRenderLodRequest(
        const TerrainRenderLodBuildRequest& request);
    [[nodiscard]] TerrainCachedLodMeshPayload cachedLodPayloadFromRenderMesh(
        const TerrainRenderMeshData& mesh,
        const TerrainRenderLodBuildRequest& request);
    [[nodiscard]] TerrainRenderMeshData renderMeshDataFromCachedLodPayload(
        const TerrainCachedLodMeshPayload& payload,
        const TerrainRenderLodBuildRequest& request);
    [[nodiscard]] TerrainRenderLodBuildResult buildTerrainRenderLod(
        const TerrainRenderLodBuildRequest& request);
}
