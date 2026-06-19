#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"

namespace Engine {
    inline constexpr const char* TerrainNavigationAdapterVersion = "terrain_navigation_adapter_t5_v1";

    struct TerrainNavigationSourceIdentity {
        AssetId sourceId;
        std::string sourceHash;
        AssetImportSettingsKey importSettings;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
    };

    struct TerrainNavigationBuildRequest {
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t sourceResolution = 0;
        uint32_t navigationResolution = 0;
        std::vector<float> heights;
        TerrainNavigationSourceIdentity identity;
    };

    struct TerrainNavigationBuildDiagnostics {
        bool valid = false;
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        uint32_t sourceResolution = 0;
        uint32_t navigationResolution = 0;
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        Renderer::Aabb bounds;
        std::string message;
    };

    struct TerrainNavigationBuildResult {
        bool success = false;
        std::optional<NavigationTerrainBuildData> buildData;
        TerrainNavigationBuildDiagnostics diagnostics;
    };

    [[nodiscard]] TerrainNavigationSourceIdentity legacyProceduralTerrainNavigationIdentity(
        const TerrainSettings& settings);
    [[nodiscard]] std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity);
    [[nodiscard]] std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromGeneratedTile(
        const GeneratedTerrainTileData& generated,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity);
    [[nodiscard]] std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromTerrainSystemTile(
        const TerrainSystem& terrain,
        TerrainTileHandle tile,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity);
    [[nodiscard]] TerrainNavigationBuildResult buildTerrainNavigationData(
        const TerrainNavigationBuildRequest& request);
}
