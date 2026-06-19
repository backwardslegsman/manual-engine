#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/AssetRegistry.hpp"

namespace Assets {
    struct HeightmapImage;
}

namespace Engine {
    enum class TerrainHeightmapChannel {
        Red,
        Green,
        Blue,
        Alpha,
        Average,
    };

    enum class TerrainHeightmapBorderPolicy {
        ClampToEdge,
    };

    enum class TerrainHeightmapInvalidSamplePolicy {
        Fail,
    };

    struct TerrainSourceChunkCoord {
        int32_t x = 0;
        int32_t z = 0;

        bool operator==(const TerrainSourceChunkCoord&) const = default;
    };

    struct TerrainSourceChunkId {
        AssetId source;
        TerrainSourceChunkCoord coord;

        bool operator==(const TerrainSourceChunkId&) const = default;
    };

    struct TerrainHeightmapImportSettings {
        std::filesystem::path sourcePath;
        std::optional<AssetId> sourceIdOverride;
        TerrainHeightmapChannel channel = TerrainHeightmapChannel::Red;
        float sampleSpacing = 1.0f;
        float heightScale = 1.0f;
        float heightOffset = 0.0f;
        glm::vec3 sourceOrigin{0.0f};
        bool flipRows = false;
        bool flipColumns = false;
        float chunkWorldSize = 16.0f;
        uint32_t chunkResolution = 17;
        TerrainHeightmapBorderPolicy borderPolicy = TerrainHeightmapBorderPolicy::ClampToEdge;
        TerrainHeightmapInvalidSamplePolicy invalidSamplePolicy = TerrainHeightmapInvalidSamplePolicy::Fail;
    };

    struct TerrainHeightmapSourceMetadata {
        std::filesystem::path sourcePath;
        AssetId sourceId;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        uint32_t bitDepth = 0;
        float normalizedMin = 0.0f;
        float normalizedMax = 0.0f;
        float worldMinHeight = 0.0f;
        float worldMaxHeight = 0.0f;
        glm::vec3 worldMin{0.0f};
        glm::vec3 worldMax{0.0f};
        AssetImportSettingsKey importSettings;
    };

    struct TerrainImportedChunk {
        TerrainSourceChunkId id;
        TerrainSourceChunkCoord coord;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t resolution = 0;
        std::vector<float> heights;
        uint32_t clampedSampleCount = 0;
        std::vector<std::string> warnings;
    };

    struct TerrainHeightmapTerrainImportResult {
        bool success = false;
        std::string message;
        TerrainHeightmapSourceMetadata metadata;
        std::vector<TerrainImportedChunk> chunks;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] AssetImportSettingsKey terrainHeightmapImportSettingsKey(
        const TerrainHeightmapImportSettings& settings);

    [[nodiscard]] TerrainHeightmapTerrainImportResult importHeightmapTerrain(
        const TerrainHeightmapImportSettings& settings);

    [[nodiscard]] TerrainHeightmapTerrainImportResult importHeightmapTerrain(
        const Assets::HeightmapImage& image,
        const TerrainHeightmapImportSettings& settings);
}
