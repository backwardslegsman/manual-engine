#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"

namespace Engine {
    inline constexpr const char* TerrainNavigationAdapterVersion = "terrain_navigation_adapter_border_v1";

    struct TerrainNavigationBuildSettings {
        uint32_t navigationResolution = 0;
        float borderPaddingWorld = 0.0f;
        uint32_t borderSampleCount = 0;
        std::optional<Renderer::Aabb> outputTileBounds;
        std::string generatorVersion = TerrainNavigationAdapterVersion;
    };

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
        glm::vec3 tileOrigin{0.0f};
        float tileSize = 0.0f;
        glm::vec3 sampleOrigin{0.0f};
        float sampleSize = 0.0f;
        uint32_t sampleResolution = 0;
        std::vector<float> sampleHeights;
        uint32_t clampedEdgeSampleCount = 0;
        TerrainNavigationBuildSettings settings;
        TerrainNavigationSourceIdentity identity;
    };

    struct TerrainNavigationBuildDiagnostics {
        bool valid = false;
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        uint32_t sourceResolution = 0;
        uint32_t navigationResolution = 0;
        float borderPaddingWorld = 0.0f;
        uint32_t borderSampleCount = 0;
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        Renderer::Aabb bounds;
        Renderer::Aabb rasterizationBounds;
        uint32_t clampedEdgeSampleCount = 0;
        std::string message;
    };

    struct TerrainNavigationBuildResult {
        bool success = false;
        std::optional<NavigationTerrainBuildData> buildData;
        TerrainNavigationBuildDiagnostics diagnostics;
    };

    [[nodiscard]] std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity);
    [[nodiscard]] std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromDatasetNeighborhood(
        const TerrainDataset& dataset,
        TerrainSourceHandle source,
        TerrainSourceChunkCoord coord,
        TerrainNavigationBuildSettings settings,
        TerrainNavigationSourceIdentity identity);
    [[nodiscard]] std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromImportedChunkNeighborhood(
        const std::vector<TerrainImportedChunk>& chunks,
        TerrainSourceChunkId chunkId,
        TerrainNavigationBuildSettings settings,
        TerrainNavigationSourceIdentity identity);
    [[nodiscard]] TerrainNavigationBuildResult buildTerrainNavigationData(
        const TerrainNavigationBuildRequest& request);
}
