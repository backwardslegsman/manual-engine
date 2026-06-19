#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/HeightmapImporter.hpp"
#include "Engine/AssetRegistry.hpp"
#include "Engine/TerrainImport.hpp"

namespace {
    struct TestFailure {
        std::string testName;
        std::string message;
    };

    struct TestContext {
        std::string name;
        std::vector<TestFailure>& failures;

        void expect(bool condition, std::string message)
        {
            if (!condition) {
                failures.push_back({name, std::move(message)});
            }
        }
    };

    bool near(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    std::filesystem::path tempPath(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_heightmap_" + std::string{name});
    }

    void writeU32BE(std::vector<uint8_t>& output, uint32_t value)
    {
        output.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
        output.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
        output.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
        output.push_back(static_cast<uint8_t>(value & 0xffu));
    }

    void writeU16LE(std::vector<uint8_t>& output, uint16_t value)
    {
        output.push_back(static_cast<uint8_t>(value & 0xffu));
        output.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    }

    uint32_t crc32(const uint8_t* data, size_t size)
    {
        uint32_t crc = 0xffffffffu;
        for (size_t index = 0; index < size; ++index) {
            crc ^= data[index];
            for (uint32_t bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
            }
        }
        return crc ^ 0xffffffffu;
    }

    uint32_t adler32(const std::vector<uint8_t>& data)
    {
        constexpr uint32_t mod = 65521u;
        uint32_t a = 1u;
        uint32_t b = 0u;
        for (uint8_t byte : data) {
            a = (a + byte) % mod;
            b = (b + a) % mod;
        }
        return (b << 16u) | a;
    }

    void appendChunk(
        std::vector<uint8_t>& png,
        const char type[4],
        const std::vector<uint8_t>& data)
    {
        writeU32BE(png, static_cast<uint32_t>(data.size()));
        const size_t typeOffset = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        const uint32_t crc = crc32(png.data() + typeOffset, png.size() - typeOffset);
        writeU32BE(png, crc);
    }

    std::vector<uint8_t> zlibStored(const std::vector<uint8_t>& raw)
    {
        std::vector<uint8_t> data;
        data.push_back(0x78u);
        data.push_back(0x01u);

        size_t offset = 0;
        while (offset < raw.size()) {
            const size_t remaining = raw.size() - offset;
            const uint16_t blockSize = static_cast<uint16_t>(std::min<size_t>(remaining, 65535u));
            const bool finalBlock = offset + blockSize == raw.size();
            data.push_back(finalBlock ? 0x01u : 0x00u);
            writeU16LE(data, blockSize);
            writeU16LE(data, static_cast<uint16_t>(~blockSize));
            data.insert(data.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
            offset += blockSize;
        }

        writeU32BE(data, adler32(raw));
        return data;
    }

    std::filesystem::path writeGrayPng8(
        std::string_view name,
        uint32_t width,
        uint32_t height,
        const std::vector<uint8_t>& samples)
    {
        const std::filesystem::path path = tempPath(name);
        std::vector<uint8_t> png{0x89u, 'P', 'N', 'G', '\r', '\n', 0x1au, '\n'};
        std::vector<uint8_t> ihdr;
        writeU32BE(ihdr, width);
        writeU32BE(ihdr, height);
        ihdr.push_back(8u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        appendChunk(png, "IHDR", ihdr);

        std::vector<uint8_t> raw;
        for (uint32_t y = 0; y < height; ++y) {
            raw.push_back(0u);
            for (uint32_t x = 0; x < width; ++x) {
                raw.push_back(samples[static_cast<size_t>(y) * width + x]);
            }
        }
        appendChunk(png, "IDAT", zlibStored(raw));
        appendChunk(png, "IEND", {});

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return path;
    }

    std::filesystem::path writeGrayPng16(
        std::string_view name,
        uint32_t width,
        uint32_t height,
        const std::vector<uint16_t>& samples)
    {
        const std::filesystem::path path = tempPath(name);
        std::vector<uint8_t> png{0x89u, 'P', 'N', 'G', '\r', '\n', 0x1au, '\n'};
        std::vector<uint8_t> ihdr;
        writeU32BE(ihdr, width);
        writeU32BE(ihdr, height);
        ihdr.push_back(16u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        appendChunk(png, "IHDR", ihdr);

        std::vector<uint8_t> raw;
        for (uint32_t y = 0; y < height; ++y) {
            raw.push_back(0u);
            for (uint32_t x = 0; x < width; ++x) {
                const uint16_t sample = samples[static_cast<size_t>(y) * width + x];
                raw.push_back(static_cast<uint8_t>((sample >> 8u) & 0xffu));
                raw.push_back(static_cast<uint8_t>(sample & 0xffu));
            }
        }
        appendChunk(png, "IDAT", zlibStored(raw));
        appendChunk(png, "IEND", {});

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return path;
    }

    Engine::TerrainHeightmapImportSettings basicSettings(const std::filesystem::path& path)
    {
        Engine::TerrainHeightmapImportSettings settings;
        settings.sourcePath = path;
        settings.sampleSpacing = 1.0f;
        settings.heightScale = 1.0f;
        settings.heightOffset = 0.0f;
        settings.sourceOrigin = {0.0f, 0.0f, 0.0f};
        settings.chunkWorldSize = 1.0f;
        settings.chunkResolution = 2;
        settings.sourceIdOverride = Engine::AssetId{12345};
        return settings;
    }

    void decodeGrayscale8BitPng(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("decode8.png", 2, 2, {0, 64, 128, 255});
        const Assets::HeightmapImportResult result = Assets::loadHeightmapImage(path);
        ctx.expect(result.success, "8-bit PNG failed to decode: " + result.message);
        ctx.expect(result.image.width == 2 && result.image.height == 2, "8-bit PNG dimensions were wrong");
        ctx.expect(result.image.channels == 1, "8-bit PNG channel count was wrong");
        ctx.expect(result.image.bitDepth == 8, "8-bit PNG bit depth was wrong");
        ctx.expect(result.image.samples.size() == 4, "8-bit PNG sample count was wrong");
        if (result.image.samples.size() == 4) {
            ctx.expect(near(result.image.samples[0], 0.0f), "8-bit first sample was wrong");
            ctx.expect(near(result.image.samples[3], 1.0f), "8-bit last sample was wrong");
        }
    }

    void decodeGrayscale16BitPng(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng16("decode16.png", 2, 2, {0, 32768, 49152, 65535});
        const Assets::HeightmapImportResult result = Assets::loadHeightmapImage(path);
        ctx.expect(result.success, "16-bit PNG failed to decode: " + result.message);
        ctx.expect(result.image.width == 2 && result.image.height == 2, "16-bit PNG dimensions were wrong");
        ctx.expect(result.image.channels == 1, "16-bit PNG channel count was wrong");
        ctx.expect(result.image.bitDepth == 16, "16-bit PNG bit depth was wrong");
        ctx.expect(result.image.samples.size() == 4, "16-bit PNG sample count was wrong");
        if (result.image.samples.size() == 4) {
            ctx.expect(near(result.image.samples[0], 0.0f), "16-bit first sample was wrong");
            ctx.expect(near(result.image.samples[3], 1.0f), "16-bit last sample was wrong");
        }
    }

    void importAppliesHeightScaleAndOffset(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("scale_offset.png", 2, 2, {0, 255, 0, 255});
        Engine::TerrainHeightmapImportSettings settings = basicSettings(path);
        settings.heightScale = 10.0f;
        settings.heightOffset = -2.0f;
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(result.success, "scaled import failed: " + result.message);
        ctx.expect(!result.chunks.empty(), "scaled import produced no chunks");
        if (!result.chunks.empty()) {
            const auto [minIt, maxIt] = std::minmax_element(result.chunks.front().heights.begin(), result.chunks.front().heights.end());
            ctx.expect(near(*minIt, -2.0f), "height offset was not applied to minimum");
            ctx.expect(near(*maxIt, 8.0f), "height scale was not applied to maximum");
        }
    }

    void northUpCoordinatesMapRowsToNegativeZ(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("north_up.png", 2, 2, {10, 20, 30, 40});
        Engine::TerrainHeightmapImportSettings settings = basicSettings(path);
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(result.success, "north-up import failed: " + result.message);
        ctx.expect(result.chunks.size() == 1, "north-up import produced wrong chunk count");
        if (result.chunks.size() == 1 && result.chunks.front().heights.size() == 4) {
            const std::vector<float>& heights = result.chunks.front().heights;
            ctx.expect(near(heights[0], 30.0f / 255.0f), "southwest sample did not come from bottom-left image pixel");
            ctx.expect(near(heights[1], 40.0f / 255.0f), "southeast sample did not come from bottom-right image pixel");
            ctx.expect(near(heights[2], 10.0f / 255.0f), "northwest sample did not come from top-left image pixel");
            ctx.expect(near(heights[3], 20.0f / 255.0f), "northeast sample did not come from top-right image pixel");
        }
    }

    void flipRowsAndAxesChangeSampling(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("flips.png", 2, 2, {10, 20, 30, 40});
        Engine::TerrainHeightmapImportSettings settings = basicSettings(path);
        settings.flipRows = true;
        settings.flipColumns = true;
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(result.success, "flipped import failed: " + result.message);
        if (result.chunks.size() == 1 && result.chunks.front().heights.size() == 4) {
            const std::vector<float>& heights = result.chunks.front().heights;
            ctx.expect(near(heights[0], 20.0f / 255.0f), "flipped southwest sample was wrong");
            ctx.expect(near(heights[3], 30.0f / 255.0f), "flipped northeast sample was wrong");
        }
    }

    void worldSizeChunksGenerateExpectedCountAndBounds(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("chunk_count.png", 5, 5, std::vector<uint8_t>(25, 64));
        Engine::TerrainHeightmapImportSettings settings = basicSettings(path);
        settings.chunkWorldSize = 2.0f;
        settings.chunkResolution = 3;
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(result.success, "chunk-count import failed: " + result.message);
        ctx.expect(result.chunks.size() == 4, "5x5 source with 2m chunks did not create 2x2 chunks");
        if (result.chunks.size() == 4) {
            ctx.expect(result.chunks[0].coord.x == 0 && result.chunks[0].coord.z == 0, "first chunk coord was wrong");
            ctx.expect(result.chunks[3].coord.x == 1 && result.chunks[3].coord.z == 1, "last chunk coord was wrong");
            ctx.expect(near(result.chunks[0].origin.x, 0.0f) && near(result.chunks[0].origin.z, -2.0f), "first chunk origin was wrong");
            ctx.expect(near(result.chunks[3].origin.x, 2.0f) && near(result.chunks[3].origin.z, -4.0f), "last chunk origin was wrong");
        }
    }

    void chunkEdgesShareHeights(TestContext& ctx)
    {
        std::vector<uint8_t> samples;
        for (uint32_t y = 0; y < 5; ++y) {
            for (uint32_t x = 0; x < 5; ++x) {
                samples.push_back(static_cast<uint8_t>(x * 20 + y * 10));
            }
        }
        const std::filesystem::path path = writeGrayPng8("shared_edges.png", 5, 5, samples);
        Engine::TerrainHeightmapImportSettings settings = basicSettings(path);
        settings.chunkWorldSize = 2.0f;
        settings.chunkResolution = 3;
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(result.success, "shared-edge import failed: " + result.message);
        ctx.expect(result.chunks.size() >= 2, "shared-edge import did not create adjacent chunks");
        if (result.chunks.size() >= 2) {
            const Engine::TerrainImportedChunk& left = result.chunks[0];
            const Engine::TerrainImportedChunk& right = result.chunks[1];
            for (uint32_t row = 0; row < left.resolution; ++row) {
                const float leftEdge = left.heights[static_cast<size_t>(row) * left.resolution + (left.resolution - 1u)];
                const float rightEdge = right.heights[static_cast<size_t>(row) * right.resolution];
                ctx.expect(near(leftEdge, rightEdge), "adjacent chunk edge heights did not match");
            }
        }
    }

    void partialEdgeChunksClampAndDiagnose(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("partial_edge.png", 4, 4, std::vector<uint8_t>(16, 127));
        Engine::TerrainHeightmapImportSettings settings = basicSettings(path);
        settings.chunkWorldSize = 2.0f;
        settings.chunkResolution = 3;
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(result.success, "partial-edge import failed: " + result.message);
        ctx.expect(result.chunks.size() == 4, "partial-edge import did not create expected chunks");
        ctx.expect(!result.warnings.empty(), "partial-edge import did not report clamp warning");
        const bool anyClamped = std::any_of(result.chunks.begin(), result.chunks.end(), [](const Engine::TerrainImportedChunk& chunk) {
            return chunk.clampedSampleCount > 0;
        });
        ctx.expect(anyClamped, "partial-edge chunks did not count clamped samples");
    }

    void importSettingsCreateStableDistinctKeys(TestContext& ctx)
    {
        Engine::TerrainHeightmapImportSettings first;
        Engine::TerrainHeightmapImportSettings second;
        second.heightScale = 2.0f;
        const Engine::AssetImportSettingsKey firstKey = Engine::terrainHeightmapImportSettingsKey(first);
        const Engine::AssetImportSettingsKey firstAgain = Engine::terrainHeightmapImportSettingsKey(first);
        const Engine::AssetImportSettingsKey secondKey = Engine::terrainHeightmapImportSettingsKey(second);
        ctx.expect(firstKey == firstAgain, "same import settings did not produce stable key");
        ctx.expect(firstKey != secondKey, "different import settings did not produce distinct key");
        ctx.expect(firstKey.pipeline == "heightmap_terrain", "heightmap settings pipeline was wrong");
    }

    void assetRegistryRecognizesTerrainAssetTypes(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("registry_heightmap.png", 2, 2, {0, 1, 2, 3});
        Engine::AssetRegistry registry;

        Engine::AssetDescriptor heightmap;
        heightmap.sourcePath = path;
        heightmap.type = Engine::AssetType::Heightmap;
        heightmap.settings = {"heightmap_terrain", "1", "default"};
        const Engine::AssetHandle heightmapHandle = registry.registerAsset(heightmap);
        const std::optional<Engine::AssetMetadata> heightmapMetadata = registry.metadata(heightmapHandle);
        ctx.expect(heightmapMetadata.has_value(), "heightmap asset metadata was missing");
        if (heightmapMetadata) {
            ctx.expect(heightmapMetadata->sourceFormat == Engine::AssetSourceFormat::Image, "heightmap asset was not detected as image");
        }

        for (Engine::AssetType type : {
            Engine::AssetType::TerrainSource,
            Engine::AssetType::TerrainMaterialSet,
            Engine::AssetType::TerrainChunk,
        }) {
            Engine::AssetDescriptor descriptor;
            descriptor.type = type;
            descriptor.settings = {"terrain", "1", "generated"};
            const Engine::AssetHandle handle = registry.registerAsset(descriptor);
            const std::optional<Engine::AssetMetadata> metadata = registry.metadata(handle);
            ctx.expect(metadata.has_value(), "generated terrain asset metadata was missing");
            if (metadata) {
                ctx.expect(metadata->sourceFormat == Engine::AssetSourceFormat::Generated, "terrain generated asset did not use generated format");
                ctx.expect(metadata->status == Engine::AssetStatus::Registered, "terrain generated asset was not registered");
            }
        }
    }

    void missingOrInvalidHeightmapFailsCleanly(TestContext& ctx)
    {
        Engine::TerrainHeightmapImportSettings settings = basicSettings(tempPath("does_not_exist.png"));
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(settings);
        ctx.expect(!result.success, "missing heightmap import unexpectedly succeeded");
        ctx.expect(!result.message.empty(), "missing heightmap import did not report a message");
    }

    void rendererTexturePathIsNotRequired(TestContext& ctx)
    {
        const std::filesystem::path path = writeGrayPng8("no_renderer.png", 2, 2, {0, 255, 255, 0});
        const Engine::TerrainHeightmapTerrainImportResult result = Engine::importHeightmapTerrain(basicSettings(path));
        ctx.expect(result.success, "CPU heightmap import failed in renderer-independent test target: " + result.message);
        ctx.expect(!result.chunks.empty(), "CPU heightmap import produced no chunks");
    }
}

int main()
{
    std::vector<TestFailure> failures;

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"DecodeGrayscale8BitPng", decodeGrayscale8BitPng},
        {"DecodeGrayscale16BitPng", decodeGrayscale16BitPng},
        {"ImportAppliesHeightScaleAndOffset", importAppliesHeightScaleAndOffset},
        {"NorthUpCoordinatesMapRowsToNegativeZ", northUpCoordinatesMapRowsToNegativeZ},
        {"FlipRowsAndAxesChangeSampling", flipRowsAndAxesChangeSampling},
        {"WorldSizeChunksGenerateExpectedCountAndBounds", worldSizeChunksGenerateExpectedCountAndBounds},
        {"ChunkEdgesShareHeights", chunkEdgesShareHeights},
        {"PartialEdgeChunksClampAndDiagnose", partialEdgeChunksClampAndDiagnose},
        {"ImportSettingsCreateStableDistinctKeys", importSettingsCreateStableDistinctKeys},
        {"AssetRegistryRecognizesTerrainAssetTypes", assetRegistryRecognizesTerrainAssetTypes},
        {"MissingOrInvalidHeightmapFailsCleanly", missingOrInvalidHeightmapFailsCleanly},
        {"RendererTexturePathIsNotRequired", rendererTexturePathIsNotRequired},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Heightmap import tests passed (" << tests.size() << " tests)\n";
    return 0;
}
