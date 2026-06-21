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
    using Domain = ManualEngine::App::EditorRebuildDomain;
    using ObjectId = ManualEngine::App::EditorSettingsReflectedObjectId;
    using PropertyId = ManualEngine::App::EditorSettingsReflectedPropertyId;

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

    struct FakeLiveHost {
        ManualEngine::App::EditorProjectSettings lightweightSnapshot;
        ManualEngine::App::EditorProjectSettings reloadSnapshot;
        bool failLightweight = false;
        bool failReload = false;
        uint32_t lightweightApplyCount = 0;
        uint32_t reloadCount = 0;
    };

    bool fakeApplyLightweight(
        void* user,
        const ManualEngine::App::EditorProjectSettings& settings,
        std::string& message)
    {
        auto* fake = static_cast<FakeLiveHost*>(user);
        ++fake->lightweightApplyCount;
        if (fake->failLightweight) {
            message = "fake lightweight apply failed";
            return false;
        }
        fake->lightweightSnapshot = settings;
        message = "fake lightweight apply succeeded";
        return true;
    }

    bool fakeReloadStreaming(
        void* user,
        const ManualEngine::App::EditorProjectSettings& settings,
        std::string& message,
        Engine::OpenWorldStreamingBuildResult& result)
    {
        auto* fake = static_cast<FakeLiveHost*>(user);
        ++fake->reloadCount;
        if (fake->failReload) {
            result.success = false;
            result.message = "fake streaming reload failed";
            message = result.message;
            return false;
        }
        fake->reloadSnapshot = settings;
        result.success = true;
        result.status = Engine::OpenWorldStreamingBuildStatus::ReusedSavedBuild;
        result.fingerprint = Engine::openWorldStreamingRuntimeFingerprint(
            ManualEngine::App::streamingRuntimeSettingsFromEditorProject(settings));
        result.message = "fake streaming reload succeeded";
        message = result.message;
        return true;
    }

    ManualEngine::App::EditorLiveApplyHost makeHost(FakeLiveHost& fake)
    {
        return {
            &fake,
            fakeApplyLightweight,
            fakeReloadStreaming,
        };
    }

    std::filesystem::path tempRoot(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_editor_live_apply_" + std::string{name});
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
            Engine::AssetId{0xE6000000ull + static_cast<uint64_t>(suffix.size())};
        settings.streaming.bake.heightmap.sampleSpacing = 1.0f;
        settings.streaming.bake.heightmap.heightScale = 8.0f;
        settings.streaming.bake.heightmap.sourceOrigin = {0.0f, 0.0f, 4.0f};
        settings.streaming.bake.heightmap.chunkWorldSize = 4.0f;
        settings.streaming.bake.heightmap.chunkResolution = 5;
        settings.streaming.bake.terrainCache.rootPath = root / "terrain";
        settings.streaming.bake.terrainCache.policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
        settings.streaming.bake.navigationCache.rootPath = root / "navigation";
        settings.streaming.bake.navigationCache.worldId = "editor_live_apply_test";
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

    void lightweightApplyAdvancesOnlyLightweightBaseline(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("lightweight_apply");
        FakeLiveHost fake;
        ManualEngine::App::EditorLiveApplyHost host = makeHost(fake);
        const auto streamingBaseline = state.baselineSettings.streaming;
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::Renderer, 0},
            PropertyId::PropMaxDrawDistance,
            333.0f);
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::Camera, 0},
            PropertyId::CameraDistance,
            125.0f);

        const bool applied = ManualEngine::App::applyEditorUiLightweightRuntimeSettings(state, &host);
        const auto dirty = dirtyState(state);
        ctx.expect(applied, "lightweight apply should succeed");
        ctx.expect(fake.lightweightApplyCount == 1, "fake host should be called once");
        ctx.expect(
            fake.lightweightSnapshot.renderer.propMaxDrawDistance == 333.0f,
            "fake host should receive edited renderer setting");
        ctx.expect(
            state.baselineSettings.renderer.propMaxDrawDistance == state.settings.renderer.propMaxDrawDistance,
            "renderer baseline should advance");
        ctx.expect(
            state.baselineSettings.streaming.savedBuildManifestPath == streamingBaseline.savedBuildManifestPath,
            "streaming baseline should not be rewritten by lightweight apply");
        ctx.expect(
            !ManualEngine::App::hasEditorRebuildDomain(dirty.domains, Domain::LightweightRuntime),
            "lightweight domain should be clean after apply");
    }

    void invalidLightweightApplyPreservesRuntimeAndBaseline(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("invalid_lightweight");
        FakeLiveHost fake;
        ManualEngine::App::EditorLiveApplyHost host = makeHost(fake);
        state.settings.camera.distance = -1.0f;
        const float baselineDistance = state.baselineSettings.camera.distance;
        const bool applied = ManualEngine::App::applyEditorUiLightweightRuntimeSettings(state, &host);
        ctx.expect(!applied, "invalid lightweight apply should fail");
        ctx.expect(fake.lightweightApplyCount == 0, "invalid settings should not reach host");
        ctx.expect(
            state.baselineSettings.camera.distance == baselineDistance,
            "invalid lightweight apply should preserve camera baseline");
        ctx.expect(
            state.rebuildCoordinator.status == ManualEngine::App::EditorRebuildCommandStatus::Failed,
            "invalid lightweight apply should record failed status");
    }

    void rebuildSettingsStayDirtyAfterLightweightApply(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("rebuild_stays_dirty");
        FakeLiveHost fake;
        ManualEngine::App::EditorLiveApplyHost host = makeHost(fake);
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainRenderLod, 0},
            PropertyId::RenderResolution,
            uint64_t{3});
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::Renderer, 0},
            PropertyId::PropMaxDrawDistance,
            340.0f);

        const bool applied = ManualEngine::App::applyEditorUiLightweightRuntimeSettings(state, &host);
        const auto dirty = dirtyState(state);
        ctx.expect(applied, "lightweight apply with rebuild dirt should still apply lightweight settings");
        ctx.expect(
            ManualEngine::App::hasEditorRebuildDomain(dirty.domains, Domain::RenderLods),
            "render LOD domain should remain dirty");
        ctx.expect(
            ManualEngine::App::hasEditorRebuildDomain(dirty.domains, Domain::StreamingSavedBuild),
            "saved build domain should remain dirty");
        ctx.expect(
            !ManualEngine::App::hasEditorRebuildDomain(dirty.domains, Domain::LightweightRuntime),
            "lightweight domain should be clean");
    }

    void rebuildThenReloadClearsReloadRequired(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("reload_success");
        FakeLiveHost fake;
        ManualEngine::App::EditorLiveApplyHost host = makeHost(fake);
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainRenderLod, 0},
            PropertyId::RenderResolution,
            uint64_t{3});
        const Engine::OpenWorldStreamingBuildResult rebuild =
            ManualEngine::App::runEditorUiRebuildCommand(
                state,
                ManualEngine::App::EditorRebuildCommand::FullSavedBuild);
        ctx.expect(rebuild.success, "saved build rebuild should succeed before reload");
        ctx.expect(state.rebuildCoordinator.runtimeReloadRequired, "rebuild should set reload required");

        const bool reloaded = ManualEngine::App::reloadEditorUiStreamingRuntime(state, &host);
        ctx.expect(reloaded, "streaming reload should succeed");
        ctx.expect(fake.reloadCount == 1, "fake reload host should be called once");
        ctx.expect(!state.rebuildCoordinator.runtimeReloadRequired, "successful reload should clear reload required");
        ctx.expect(!state.rebuildCoordinator.activeSavedBuildFingerprint.empty(), "active fingerprint should update");
    }

    void reloadFailureKeepsReloadRequired(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState("reload_failure");
        FakeLiveHost fake;
        fake.failReload = true;
        ManualEngine::App::EditorLiveApplyHost host = makeHost(fake);
        const Engine::OpenWorldStreamingBuildResult rebuild =
            ManualEngine::App::runEditorUiRebuildCommand(
                state,
                ManualEngine::App::EditorRebuildCommand::FullSavedBuild);
        ctx.expect(rebuild.success, "saved build rebuild should succeed before failed reload");
        const bool reloaded = ManualEngine::App::reloadEditorUiStreamingRuntime(state, &host);
        ctx.expect(!reloaded, "fake reload should fail");
        ctx.expect(state.rebuildCoordinator.runtimeReloadRequired, "failed reload should keep reload required");
        ctx.expect(
            state.rebuildCoordinator.status == ManualEngine::App::EditorRebuildCommandStatus::Failed,
            "failed reload should record failed status");
    }

    void validationReportsMissingAssetsAndManifest(TestContext& ctx)
    {
        ManualEngine::App::EditorProjectSettings settings = settingsFor("validation_missing");
        settings.streaming.bake.heightmap.sourcePath = tempRoot("validation_missing") / "missing.png";
        settings.streaming.savedBuildManifestPath = tempRoot("validation_missing") / "missing_manifest.yaml";
        const ManualEngine::App::EditorProjectSettingsValidationResult validation =
            ManualEngine::App::validateEditorLiveApplySettings(settings, true);
        bool mentionedHeightmap = false;
        bool mentionedManifest = false;
        for (const std::string& error : validation.errors) {
            mentionedHeightmap = mentionedHeightmap || error.find("heightmap") != std::string::npos;
            mentionedManifest = mentionedManifest || error.find("manifest") != std::string::npos;
        }
        ctx.expect(!validation.valid, "missing source/manifest validation should fail");
        ctx.expect(mentionedHeightmap, "validation should mention missing heightmap");
        ctx.expect(mentionedManifest, "validation should mention missing saved manifest");
    }

    void manualEngineDoesNotLinkEditorLiveApplyCode(TestContext& ctx)
    {
#ifdef MANUAL_ENGINE_SOURCE_DIR
        const std::filesystem::path cmakePath =
            std::filesystem::path{MANUAL_ENGINE_SOURCE_DIR} / "CMakeLists.txt";
#else
        const std::filesystem::path cmakePath = "CMakeLists.txt";
#endif
        std::ifstream file(cmakePath);
        const std::string content{
            std::istreambuf_iterator<char>{file},
            std::istreambuf_iterator<char>{},
        };
        const size_t manualStart = content.find("add_executable(manual_engine");
        const size_t sharedStart = content.find("get_target_property(MANUAL_ENGINE_APP_SOURCES");
        ctx.expect(manualStart != std::string::npos && sharedStart != std::string::npos, "CMake manual_engine section not found");
        const std::string manualSection = content.substr(manualStart, sharedStart - manualStart);
        ctx.expect(manualSection.find("EditorUi.cpp") == std::string::npos, "manual_engine should not link EditorUi.cpp");
        ctx.expect(
            manualSection.find("EditorRebuildCoordinator.cpp") == std::string::npos,
            "manual_engine should not link EditorRebuildCoordinator.cpp");
    }

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> Tests = {
        {"LightweightApplyAdvancesOnlyLightweightBaseline", lightweightApplyAdvancesOnlyLightweightBaseline},
        {"InvalidLightweightApplyPreservesRuntimeAndBaseline", invalidLightweightApplyPreservesRuntimeAndBaseline},
        {"RebuildSettingsStayDirtyAfterLightweightApply", rebuildSettingsStayDirtyAfterLightweightApply},
        {"RebuildThenReloadClearsReloadRequired", rebuildThenReloadClearsReloadRequired},
        {"ReloadFailureKeepsReloadRequired", reloadFailureKeepsReloadRequired},
        {"ValidationReportsMissingAssetsAndManifest", validationReportsMissingAssetsAndManifest},
        {"ManualEngineDoesNotLinkEditorLiveApplyCode", manualEngineDoesNotLinkEditorLiveApplyCode},
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
        std::cerr << ctx.failures << " editor live apply test failure(s)\n";
        return 1;
    }
    std::cout << Tests.size() << " editor live apply tests passed\n";
    return 0;
}
