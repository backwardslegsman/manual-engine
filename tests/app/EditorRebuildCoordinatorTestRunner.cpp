#include "App/EditorUi.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using ObjectId = ManualEngine::App::EditorSettingsReflectedObjectId;
    using PropertyId = ManualEngine::App::EditorSettingsReflectedPropertyId;
    using Domain = ManualEngine::App::EditorRebuildDomain;

    struct TestContext {
        int failures = 0;

        void expect(bool condition, const std::string& message)
        {
            if (!condition) {
                ++failures;
                std::cerr << "FAIL: " << message << '\n';
            }
        }
    };

    std::filesystem::path tempRoot(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_editor_rebuild_" + std::string{name});
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

    void appendChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data)
    {
        writeU32BE(png, static_cast<uint32_t>(data.size()));
        const size_t typeOffset = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        writeU32BE(png, crc32(png.data() + typeOffset, png.size() - typeOffset));
    }

    std::vector<uint8_t> zlibStored(const std::vector<uint8_t>& raw)
    {
        std::vector<uint8_t> data{0x78u, 0x01u};
        size_t offset = 0;
        while (offset < raw.size()) {
            const size_t remaining = raw.size() - offset;
            const uint16_t blockSize = static_cast<uint16_t>(std::min<size_t>(remaining, 65535u));
            const bool finalBlock = offset + blockSize == raw.size();
            data.push_back(finalBlock ? 0x01u : 0x00u);
            writeU16LE(data, blockSize);
            writeU16LE(data, static_cast<uint16_t>(~blockSize));
            data.insert(
                data.end(),
                raw.begin() + static_cast<std::ptrdiff_t>(offset),
                raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
            offset += blockSize;
        }
        writeU32BE(data, adler32(raw));
        return data;
    }

    std::filesystem::path writeGrayPng8(const std::filesystem::path& root, std::string_view name)
    {
        std::filesystem::create_directories(root);
        const uint32_t width = 5;
        const uint32_t height = 5;
        const std::vector<uint8_t> samples{
            0, 8, 16, 24, 32,
            8, 16, 24, 32, 40,
            16, 24, 32, 40, 48,
            24, 32, 40, 48, 56,
            32, 40, 48, 56, 64,
        };
        const std::filesystem::path path = root / name;
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

    ManualEngine::App::EditorProjectSettings settingsFor(std::string_view suffix)
    {
        const std::filesystem::path root = tempRoot(suffix);
        std::filesystem::remove_all(root);
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        settings.streaming.savedBuildManifestPath = root / "streaming" / "manifest.yaml";
        settings.streaming.bake.heightmap.sourcePath = writeGrayPng8(root, "height.png");
        settings.streaming.bake.heightmap.sourceIdOverride =
            Engine::AssetId{0xE5000000ull + static_cast<uint64_t>(suffix.size())};
        settings.streaming.bake.heightmap.sampleSpacing = 1.0f;
        settings.streaming.bake.heightmap.heightScale = 8.0f;
        settings.streaming.bake.heightmap.sourceOrigin = {0.0f, 0.0f, 4.0f};
        settings.streaming.bake.heightmap.chunkWorldSize = 4.0f;
        settings.streaming.bake.heightmap.chunkResolution = 5;
        settings.streaming.bake.terrainCache.rootPath = root / "terrain";
        settings.streaming.bake.terrainCache.policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
        settings.streaming.bake.navigationCache.rootPath = root / "navigation";
        settings.streaming.bake.navigationCache.worldId = "editor_rebuild_test";
        settings.streaming.bake.navigationResolution = 5;
        settings.streaming.bake.physicsColliderResolution = 5;
        settings.streaming.bake.renderLods = {{0, 5, 0.5f}};
        settings.streaming.cache.maxReadJobsQueuedPerUpdate = 8;
        settings.streaming.cache.maxCompletedJobsMergedPerUpdate = 8;
        settings.streaming.promotion.maxPromotesQueuedPerUpdate = 8;
        settings.streaming.promotion.maxDemotesQueuedPerUpdate = 8;
        return settings;
    }

    ManualEngine::App::EditorUiState makeState(std::string_view suffix)
    {
        ManualEngine::App::EditorProjectSettings settings = settingsFor(suffix);
        ManualEngine::App::EditorUiState state;
        ManualEngine::App::initializeEditorUiState(
            state,
            settings,
            settings.streaming.savedBuildManifestPath,
            "test profile",
            ManualEngine::App::validateEditorProjectSettings(settings));
        return state;
    }

    ManualEngine::App::EditorRebuildDirtyState dirtyState(ManualEngine::App::EditorUiState& state)
    {
        const auto rows = ManualEngine::App::buildEditorUiPropertyRows(state);
        return ManualEngine::App::computeEditorRebuildDirtyState(
            state.settings,
            state.baselineSettings,
            rows);
    }

    void expectDomain(
        TestContext& ctx,
        ManualEngine::App::EditorRebuildDirtyState dirty,
        Domain domain,
        const std::string& message)
    {
        ctx.expect(ManualEngine::App::hasEditorRebuildDomain(dirty.domains, domain), message);
    }

    void defaultSettingsClean(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("clean");
        const auto dirty = dirtyState(state);
        ctx.expect(dirty.domains == Domain::None, "default settings should not be dirty");
        ctx.expect(!dirty.savedBuildFingerprintDirty, "default fingerprint should not be dirty");
    }

    void terrainAndCacheDirty(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("terrain_dirty");
        const std::filesystem::path replacement = writeGrayPng8(tempRoot("terrain_dirty"), "height2.png");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainImport, 0},
            PropertyId::SourcePath,
            replacement.generic_string());
        const auto dirty = dirtyState(state);
        expectDomain(ctx, dirty, Domain::Terrain, "terrain edit should dirty terrain domain");
        expectDomain(ctx, dirty, Domain::StreamingSavedBuild, "terrain edit should dirty saved build");
    }

    void renderLodDirty(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("lod_dirty");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainRenderLod, 0},
            PropertyId::RenderResolution,
            uint64_t{3});
        const auto dirty = dirtyState(state);
        expectDomain(ctx, dirty, Domain::RenderLods, "render LOD edit should dirty LOD domain");
        expectDomain(ctx, dirty, Domain::StreamingSavedBuild, "render LOD edit should dirty saved build");
    }

    void navigationAndSceneGeometryDirty(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("nav_dirty");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::NavigationAgent, 0},
            PropertyId::AgentRadius,
            0.6f);
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::SceneGeometryFiltering, 0},
            PropertyId::SceneGeometryMaxSlope,
            44.0f);
        const auto dirty = dirtyState(state);
        expectDomain(ctx, dirty, Domain::Navigation, "navigation edit should dirty navigation domain");
        expectDomain(ctx, dirty, Domain::SceneGeometry, "scene geometry edit should dirty scene geometry domain");
        expectDomain(ctx, dirty, Domain::StreamingSavedBuild, "navigation edits should dirty saved build");
    }

    void physicsDirty(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("physics_dirty");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::PhysicsColliders, 0},
            PropertyId::ColliderResolution,
            uint64_t{3});
        const auto dirty = dirtyState(state);
        expectDomain(ctx, dirty, Domain::PhysicsColliders, "physics edit should dirty physics domain");
        expectDomain(ctx, dirty, Domain::StreamingSavedBuild, "physics edit should dirty saved build");
    }

    void lightweightDirtyOnly(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("lightweight_dirty");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::Renderer, 0},
            PropertyId::PropMaxDrawDistance,
            321.0f);
        const auto dirty = dirtyState(state);
        expectDomain(ctx, dirty, Domain::LightweightRuntime, "renderer edit should dirty lightweight domain");
        ctx.expect(
            !ManualEngine::App::hasEditorRebuildDomain(dirty.domains, Domain::StreamingSavedBuild),
            "renderer edit should not dirty saved build");
        ctx.expect(dirty.lightweightPropertyCount == 1, "expected one lightweight dirty property");
    }

    void invalidRebuildPreservesBaseline(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("invalid_rebuild");
        const float previous = state.baselineSettings.streaming.bake.heightmap.sampleSpacing;
        state.settings.streaming.bake.heightmap.sampleSpacing = -1.0f;
        const Engine::OpenWorldStreamingBuildResult result =
            ManualEngine::App::runEditorUiRebuildCommand(
                state,
                ManualEngine::App::EditorRebuildCommand::FullSavedBuild);
        ctx.expect(!result.success, "invalid rebuild should fail");
        ctx.expect(
            state.baselineSettings.streaming.bake.heightmap.sampleSpacing == previous,
            "invalid rebuild should preserve baseline");
        ctx.expect(
            state.rebuildCoordinator.status == ManualEngine::App::EditorRebuildCommandStatus::Failed,
            "invalid rebuild should record failed status");
    }

    void successfulRebuildUpdatesBaseline(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("successful_rebuild");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainRenderLod, 0},
            PropertyId::RenderResolution,
            uint64_t{3});
        const Engine::OpenWorldStreamingBuildResult result =
            ManualEngine::App::runEditorUiRebuildCommand(
                state,
                ManualEngine::App::EditorRebuildCommand::FullSavedBuild);
        ctx.expect(result.success, "saved build rebuild should succeed: " + result.message);
        ctx.expect(std::filesystem::is_regular_file(state.settings.streaming.savedBuildManifestPath), "saved manifest should exist");
        ctx.expect(state.rebuildCoordinator.runtimeReloadRequired, "successful rebuild should require runtime reload");
        const auto dirty = dirtyState(state);
        ctx.expect(!dirty.savedBuildFingerprintDirty, "successful rebuild should advance saved-build baseline");
        ctx.expect(!ManualEngine::App::hasEditorRebuildDomain(dirty.domains, Domain::RenderLods), "LOD domain should be clean after baseline update");
    }

    void failedRebuildPreservesBaseline(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("failed_rebuild");
        state.settings.streaming.bake.heightmap.sourcePath = tempRoot("failed_rebuild") / "missing.png";
        const std::string before = Engine::openWorldStreamingRuntimeFingerprint(
            ManualEngine::App::streamingRuntimeSettingsFromEditorProject(state.baselineSettings));
        const Engine::OpenWorldStreamingBuildResult result =
            ManualEngine::App::runEditorUiRebuildCommand(
                state,
                ManualEngine::App::EditorRebuildCommand::TerrainChunks);
        const std::string after = Engine::openWorldStreamingRuntimeFingerprint(
            ManualEngine::App::streamingRuntimeSettingsFromEditorProject(state.baselineSettings));
        ctx.expect(!result.success, "missing source rebuild should fail");
        ctx.expect(before == after, "failed rebuild should preserve baseline fingerprint");
    }

    void domainButtonsUseFullBackend(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("domain_button");
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::PhysicsColliders, 0},
            PropertyId::ColliderResolution,
            uint64_t{3});
        const Engine::OpenWorldStreamingBuildResult result =
            ManualEngine::App::runEditorUiRebuildCommand(
                state,
                ManualEngine::App::EditorRebuildCommand::PhysicsColliders);
        ctx.expect(result.success, "domain rebuild should use full backend successfully");
        ctx.expect(result.bakeDiagnostics.terrainChunkWrites > 0, "domain rebuild should write terrain chunks");
        ctx.expect(result.bakeDiagnostics.renderLodWrites > 0, "domain rebuild should write render LODs");
        ctx.expect(result.bakeDiagnostics.physicsColliderWrites > 0, "domain rebuild should write physics colliders");
    }

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> Tests = {
        {"DefaultSettingsClean", defaultSettingsClean},
        {"TerrainAndCacheDirty", terrainAndCacheDirty},
        {"RenderLodDirty", renderLodDirty},
        {"NavigationAndSceneGeometryDirty", navigationAndSceneGeometryDirty},
        {"PhysicsDirty", physicsDirty},
        {"LightweightDirtyOnly", lightweightDirtyOnly},
        {"InvalidRebuildPreservesBaseline", invalidRebuildPreservesBaseline},
        {"SuccessfulRebuildUpdatesBaseline", successfulRebuildUpdatesBaseline},
        {"FailedRebuildPreservesBaseline", failedRebuildPreservesBaseline},
        {"DomainButtonsUseFullBackend", domainButtonsUseFullBackend},
    };
}

int main()
{
    TestContext ctx;
    for (const auto& [name, test] : Tests) {
        test(ctx);
        if (ctx.failures == 0) {
            std::cout << "PASS: " << name << '\n';
        }
    }
    if (ctx.failures != 0) {
        std::cerr << ctx.failures << " editor rebuild coordinator test failure(s)\n";
        return 1;
    }
    std::cout << Tests.size() << " editor rebuild coordinator tests passed\n";
    return 0;
}
