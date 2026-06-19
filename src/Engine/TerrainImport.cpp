#include "Engine/TerrainImport.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>

#include "Assets/HeightmapImporter.hpp"

namespace Engine {
    namespace {
        constexpr uint64_t FnvOffset = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;

        uint64_t hashBytes(const void* data, size_t size, uint64_t hash)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= FnvPrime;
            }
            return hash;
        }

        uint64_t hashText(std::string_view text, uint64_t hash)
        {
            for (unsigned char character : text) {
                hash ^= character;
                hash *= FnvPrime;
            }
            return hash;
        }

        template <typename T>
        uint64_t hashValue(const T& value, uint64_t hash)
        {
            return hashBytes(&value, sizeof(value), hash);
        }

        std::string hexString(uint64_t value)
        {
            std::ostringstream stream;
            stream << std::hex << value;
            return stream.str();
        }

        AssetId makeFallbackSourceId(const std::filesystem::path& path)
        {
            uint64_t hash = FnvOffset;
            hash = hashText(path.lexically_normal().generic_string(), hash);
            hash = hashText("heightmap_source", hash);
            if (hash == 0) {
                hash = 1;
            }
            return {hash};
        }

        bool isFinite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        uint32_t channelIndex(TerrainHeightmapChannel channel)
        {
            switch (channel) {
                case TerrainHeightmapChannel::Red:
                    return 0;
                case TerrainHeightmapChannel::Green:
                    return 1;
                case TerrainHeightmapChannel::Blue:
                    return 2;
                case TerrainHeightmapChannel::Alpha:
                    return 3;
                case TerrainHeightmapChannel::Average:
                default:
                    return 0;
            }
        }

        float samplePixel(
            const Assets::HeightmapImage& image,
            uint32_t x,
            uint32_t y,
            TerrainHeightmapChannel channel)
        {
            const size_t base = (static_cast<size_t>(y) * image.width + x) * image.channels;
            if (channel == TerrainHeightmapChannel::Average) {
                float sum = 0.0f;
                for (uint32_t channelOffset = 0; channelOffset < image.channels; ++channelOffset) {
                    sum += image.samples[base + channelOffset];
                }
                return sum / static_cast<float>(image.channels);
            }
            return image.samples[base + channelIndex(channel)];
        }

        float lerp(float lhs, float rhs, float t)
        {
            return lhs + (rhs - lhs) * t;
        }

        struct SourceSample {
            float normalized = 0.0f;
            bool clamped = false;
        };

        SourceSample sampleSource(
            const Assets::HeightmapImage& image,
            const TerrainHeightmapImportSettings& settings,
            float worldX,
            float worldZ)
        {
            const float sourceWidth = static_cast<float>(image.width - 1u);
            const float sourceHeight = static_cast<float>(image.height - 1u);
            float sourceX = (worldX - settings.sourceOrigin.x) / settings.sampleSpacing;
            float sourceY = (settings.sourceOrigin.z - worldZ) / settings.sampleSpacing;
            if (settings.flipColumns) {
                sourceX = sourceWidth - sourceX;
            }
            if (settings.flipRows) {
                sourceY = sourceHeight - sourceY;
            }

            SourceSample result;
            const float clampedX = std::clamp(sourceX, 0.0f, sourceWidth);
            const float clampedY = std::clamp(sourceY, 0.0f, sourceHeight);
            result.clamped = clampedX != sourceX || clampedY != sourceY;

            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(clampedX)), image.width - 1u);
            const uint32_t y0 = std::min(static_cast<uint32_t>(std::floor(clampedY)), image.height - 1u);
            const uint32_t x1 = std::min(x0 + 1u, image.width - 1u);
            const uint32_t y1 = std::min(y0 + 1u, image.height - 1u);
            const float tx = clampedX - static_cast<float>(x0);
            const float ty = clampedY - static_cast<float>(y0);

            const float h00 = samplePixel(image, x0, y0, settings.channel);
            const float h10 = samplePixel(image, x1, y0, settings.channel);
            const float h01 = samplePixel(image, x0, y1, settings.channel);
            const float h11 = samplePixel(image, x1, y1, settings.channel);
            result.normalized = lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), ty);
            return result;
        }

        bool validate(
            const Assets::HeightmapImage& image,
            const TerrainHeightmapImportSettings& settings,
            std::string& message)
        {
            if (image.width < 2 || image.height < 2 || image.channels == 0 || image.samples.empty()) {
                message = "Heightmap image is empty or too small.";
                return false;
            }
            if (image.samples.size() != static_cast<size_t>(image.width) * image.height * image.channels) {
                message = "Heightmap sample count does not match dimensions.";
                return false;
            }
            if (settings.channel != TerrainHeightmapChannel::Average &&
                channelIndex(settings.channel) >= image.channels) {
                message = "Requested heightmap channel is not present.";
                return false;
            }
            if (!std::isfinite(settings.sampleSpacing) || settings.sampleSpacing <= 0.0f ||
                !std::isfinite(settings.heightScale) || !std::isfinite(settings.heightOffset) ||
                !std::isfinite(settings.chunkWorldSize) || settings.chunkWorldSize <= 0.0f ||
                settings.chunkResolution < 2 || !isFinite(settings.sourceOrigin)) {
                message = "Heightmap import settings contain invalid numeric values.";
                return false;
            }
            if (settings.invalidSamplePolicy != TerrainHeightmapInvalidSamplePolicy::Fail ||
                settings.borderPolicy != TerrainHeightmapBorderPolicy::ClampToEdge) {
                message = "Unsupported heightmap import policy.";
                return false;
            }
            return true;
        }
    }

    AssetImportSettingsKey terrainHeightmapImportSettingsKey(
        const TerrainHeightmapImportSettings& settings)
    {
        uint64_t hash = FnvOffset;
        const uint32_t channel = static_cast<uint32_t>(settings.channel);
        const uint32_t border = static_cast<uint32_t>(settings.borderPolicy);
        const uint32_t invalid = static_cast<uint32_t>(settings.invalidSamplePolicy);
        hash = hashValue(channel, hash);
        hash = hashValue(settings.sampleSpacing, hash);
        hash = hashValue(settings.heightScale, hash);
        hash = hashValue(settings.heightOffset, hash);
        hash = hashValue(settings.sourceOrigin.x, hash);
        hash = hashValue(settings.sourceOrigin.y, hash);
        hash = hashValue(settings.sourceOrigin.z, hash);
        hash = hashValue(settings.flipRows, hash);
        hash = hashValue(settings.flipColumns, hash);
        hash = hashValue(settings.chunkWorldSize, hash);
        hash = hashValue(settings.chunkResolution, hash);
        hash = hashValue(border, hash);
        hash = hashValue(invalid, hash);
        if (settings.sourceIdOverride) {
            hash = hashValue(settings.sourceIdOverride->value, hash);
        }
        return {"heightmap_terrain", "1", hexString(hash)};
    }

    TerrainHeightmapTerrainImportResult importHeightmapTerrain(
        const TerrainHeightmapImportSettings& settings)
    {
        const Assets::HeightmapImportResult loaded = Assets::loadHeightmapImage(settings.sourcePath);
        if (!loaded.success) {
            TerrainHeightmapTerrainImportResult result;
            result.message = loaded.message;
            result.warnings = loaded.warnings;
            return result;
        }
        return importHeightmapTerrain(loaded.image, settings);
    }

    TerrainHeightmapTerrainImportResult importHeightmapTerrain(
        const Assets::HeightmapImage& image,
        const TerrainHeightmapImportSettings& settings)
    {
        TerrainHeightmapTerrainImportResult result;
        std::string validationMessage;
        if (!validate(image, settings, validationMessage)) {
            result.message = std::move(validationMessage);
            return result;
        }

        const auto [minIt, maxIt] = std::minmax_element(image.samples.begin(), image.samples.end());
        const float sourceWidth = static_cast<float>(image.width - 1u) * settings.sampleSpacing;
        const float sourceDepth = static_cast<float>(image.height - 1u) * settings.sampleSpacing;
        const uint32_t chunkCountX = std::max(1u, static_cast<uint32_t>(std::ceil(sourceWidth / settings.chunkWorldSize)));
        const uint32_t chunkCountZ = std::max(1u, static_cast<uint32_t>(std::ceil(sourceDepth / settings.chunkWorldSize)));
        const AssetId sourceId = settings.sourceIdOverride.value_or(makeFallbackSourceId(settings.sourcePath.empty()
            ? image.sourcePath
            : settings.sourcePath));

        result.metadata.sourcePath = settings.sourcePath.empty() ? image.sourcePath : settings.sourcePath;
        result.metadata.sourceId = sourceId;
        result.metadata.width = image.width;
        result.metadata.height = image.height;
        result.metadata.channels = image.channels;
        result.metadata.bitDepth = image.bitDepth;
        result.metadata.normalizedMin = *minIt;
        result.metadata.normalizedMax = *maxIt;
        result.metadata.worldMinHeight = *minIt * settings.heightScale + settings.heightOffset;
        result.metadata.worldMaxHeight = *maxIt * settings.heightScale + settings.heightOffset;
        if (result.metadata.worldMinHeight > result.metadata.worldMaxHeight) {
            std::swap(result.metadata.worldMinHeight, result.metadata.worldMaxHeight);
        }
        result.metadata.worldMin = {
            settings.sourceOrigin.x,
            result.metadata.worldMinHeight,
            settings.sourceOrigin.z - sourceDepth,
        };
        result.metadata.worldMax = {
            settings.sourceOrigin.x + sourceWidth,
            result.metadata.worldMaxHeight,
            settings.sourceOrigin.z,
        };
        result.metadata.importSettings = terrainHeightmapImportSettingsKey(settings);

        result.chunks.reserve(static_cast<size_t>(chunkCountX) * chunkCountZ);
        for (uint32_t chunkZ = 0; chunkZ < chunkCountZ; ++chunkZ) {
            for (uint32_t chunkX = 0; chunkX < chunkCountX; ++chunkX) {
                TerrainImportedChunk chunk;
                chunk.coord = {static_cast<int32_t>(chunkX), static_cast<int32_t>(chunkZ)};
                chunk.id = {sourceId, chunk.coord};
                chunk.origin = {
                    settings.sourceOrigin.x + static_cast<float>(chunkX) * settings.chunkWorldSize,
                    0.0f,
                    settings.sourceOrigin.z - static_cast<float>(chunkZ + 1u) * settings.chunkWorldSize,
                };
                chunk.size = settings.chunkWorldSize;
                chunk.resolution = settings.chunkResolution;
                chunk.heights.reserve(static_cast<size_t>(chunk.resolution) * chunk.resolution);

                const float step = settings.chunkWorldSize / static_cast<float>(chunk.resolution - 1u);
                for (uint32_t localZ = 0; localZ < chunk.resolution; ++localZ) {
                    const float worldZ = chunk.origin.z + static_cast<float>(localZ) * step;
                    for (uint32_t localX = 0; localX < chunk.resolution; ++localX) {
                        const float worldX = chunk.origin.x + static_cast<float>(localX) * step;
                        const SourceSample source = sampleSource(image, settings, worldX, worldZ);
                        if (source.clamped) {
                            ++chunk.clampedSampleCount;
                        }
                        chunk.heights.push_back(source.normalized * settings.heightScale + settings.heightOffset);
                    }
                }

                if (chunk.clampedSampleCount > 0) {
                    chunk.warnings.push_back("chunk samples were clamped to the heightmap edge");
                    result.warnings.push_back("one or more chunk samples were clamped to the heightmap edge");
                }
                result.chunks.push_back(std::move(chunk));
            }
        }

        std::sort(result.warnings.begin(), result.warnings.end());
        result.warnings.erase(std::unique(result.warnings.begin(), result.warnings.end()), result.warnings.end());
        result.success = true;
        result.message = "Heightmap terrain chunks imported.";
        return result;
    }
}
