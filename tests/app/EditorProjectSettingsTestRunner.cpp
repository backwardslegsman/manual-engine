#include "App/EditorProjectSettings.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
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

    using TestFn = void (*)(TestContext&);

    std::filesystem::path tempRoot()
    {
        return std::filesystem::path{"generated/editor_project_settings_tests"};
    }

    void defaultSettingsValidate(TestContext& ctx)
    {
        const ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        const ManualEngine::App::EditorProjectSettingsValidationResult validation =
            ManualEngine::App::validateEditorProjectSettings(settings);
        ctx.expect(validation.valid, "default editor settings did not validate");
        ctx.expect(settings.streaming.bake.renderLods.size() == 2, "default render LOD count changed");
        ctx.expect(settings.streaming.bake.renderLods[0].renderResolution == 33, "default LOD0 resolution changed");
        ctx.expect(settings.streaming.bake.renderLods[1].renderResolution == 17, "default LOD1 resolution changed");
        ctx.expect(settings.streaming.bake.physicsColliderResolution == 17, "default physics collider resolution changed");
    }

    void committedProfileLoads(TestContext& ctx)
    {
        const ManualEngine::App::EditorProjectSettingsLoadResult result =
            ManualEngine::App::loadEditorProjectSettings("projects/default.editor.yaml");
        ctx.expect(result.loaded, "committed editor profile did not load");
        ctx.expect(!result.usedFallback, "committed editor profile unexpectedly used fallback");
        ctx.expect(result.validation.valid, "committed editor profile did not validate");
        ctx.expect(result.settings.projectName == "ManualEngine Default", "committed profile project name changed");
        ctx.expect(
            result.settings.streaming.bake.heightmap.chunkResolution == 33,
            "committed profile heightmap chunk resolution changed");
        ctx.expect(
            result.settings.streaming.bake.navigationCache.worldId == "modern_default_streaming",
            "committed profile navigation world id changed");
    }

    void saveLoadRoundTripPreservesKeySettings(TestContext& ctx)
    {
        std::filesystem::create_directories(tempRoot());
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        settings.projectName = "Round Trip";
        settings.streaming.bake.physicsColliderResolution = 9;
        settings.renderer.propMaxDrawDistance = 123.0f;
        settings.debugDraw.colliderShapes = true;
        settings.camera.distance = 88.0f;

        const std::filesystem::path path = tempRoot() / "round_trip.editor.yaml";
        const ManualEngine::App::EditorProjectSettingsSaveResult save =
            ManualEngine::App::saveEditorProjectSettings(path, settings);
        ctx.expect(save.success, "failed to save round-trip editor profile");
        const ManualEngine::App::EditorProjectSettingsLoadResult load =
            ManualEngine::App::loadEditorProjectSettings(path);
        ctx.expect(load.loaded, "failed to load round-trip editor profile");
        ctx.expect(load.settings.projectName == "Round Trip", "round-trip project name changed");
        ctx.expect(load.settings.streaming.bake.physicsColliderResolution == 9, "round-trip physics resolution changed");
        ctx.expect(load.settings.renderer.propMaxDrawDistance == 123.0f, "round-trip renderer setting changed");
        ctx.expect(load.settings.debugDraw.colliderShapes, "round-trip debug draw setting changed");
        ctx.expect(load.settings.camera.distance == 88.0f, "round-trip camera distance changed");
    }

    void missingFieldsDefaultFill(TestContext& ctx)
    {
        std::filesystem::create_directories(tempRoot());
        const std::filesystem::path path = tempRoot() / "minimal.editor.yaml";
        {
            std::ofstream file(path, std::ios::trunc);
            file << "schema_version: 1\nproject_name: Minimal\n";
        }

        const ManualEngine::App::EditorProjectSettingsLoadResult load =
            ManualEngine::App::loadEditorProjectSettings(path);
        ctx.expect(load.loaded, "minimal profile did not load");
        ctx.expect(load.validation.valid, "minimal profile did not validate after default-fill");
        ctx.expect(load.settings.projectName == "Minimal", "minimal profile project name was not read");
        ctx.expect(
            load.settings.streaming.bake.heightmap.chunkWorldSize == 64.0f,
            "minimal profile did not default-fill terrain chunk size");
        ctx.expect(
            load.settings.streaming.savedBuildManifestPath ==
                std::filesystem::path{"generated/open_world_streaming/modern_default/manifest.yaml"},
            "minimal profile did not default-fill saved build path");
    }

    void invalidSettingsReportDiagnostics(TestContext& ctx)
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        settings.streaming.bake.heightmap.chunkResolution = 1;
        settings.streaming.bake.navAgent.radius = -1.0f;
        settings.debugDraw.maxDebugLines = 0;
        const ManualEngine::App::EditorProjectSettingsValidationResult validation =
            ManualEngine::App::validateEditorProjectSettings(settings);
        ctx.expect(!validation.valid, "invalid settings unexpectedly validated");
        ctx.expect(validation.errors.size() >= 3, "invalid settings did not report expected diagnostics");
    }

    void invalidYamlFallsBack(TestContext& ctx)
    {
        std::filesystem::create_directories(tempRoot());
        const std::filesystem::path path = tempRoot() / "invalid.editor.yaml";
        {
            std::ofstream file(path, std::ios::trunc);
            file << "schema_version: 1\nnavigation:\n  agent:\n    radius: -1\n";
        }

        const ManualEngine::App::EditorProjectSettingsLoadResult load =
            ManualEngine::App::loadEditorProjectSettings(path);
        ctx.expect(!load.loaded, "invalid profile unexpectedly loaded");
        ctx.expect(load.usedFallback, "invalid profile did not use fallback");
        ctx.expect(
            load.settings.streaming.bake.navAgent.radius ==
                ManualEngine::App::defaultEditorProjectSettings().streaming.bake.navAgent.radius,
            "invalid profile fallback did not restore defaults");
    }

    void streamingMappingKeepsFingerprintInputs(TestContext& ctx)
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        settings.streaming.bake.navigationResolution = 11;
        settings.streaming.bake.navigationCache.worldId = "editor_test_world";
        settings.streaming.bake.renderLods[0].renderResolution = 21;
        const Engine::OpenWorldStreamingRuntimeSettings streaming =
            ManualEngine::App::streamingRuntimeSettingsFromEditorProject(settings);
        ctx.expect(streaming.bake.navigationResolution == 11, "streaming mapping lost navigation resolution");
        ctx.expect(streaming.bake.navigationCache.worldId == "editor_test_world", "streaming mapping lost world id");
        ctx.expect(streaming.bake.renderLods[0].renderResolution == 21, "streaming mapping lost render LOD");
    }

    const std::vector<std::pair<std::string, TestFn>> Tests = {
        {"DefaultSettingsValidate", defaultSettingsValidate},
        {"CommittedProfileLoads", committedProfileLoads},
        {"SaveLoadRoundTripPreservesKeySettings", saveLoadRoundTripPreservesKeySettings},
        {"MissingFieldsDefaultFill", missingFieldsDefaultFill},
        {"InvalidSettingsReportDiagnostics", invalidSettingsReportDiagnostics},
        {"InvalidYamlFallsBack", invalidYamlFallsBack},
        {"StreamingMappingKeepsFingerprintInputs", streamingMappingKeepsFingerprintInputs},
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
        std::cerr << ctx.failures << " editor project settings test failure(s)\n";
        return 1;
    }
    std::cout << Tests.size() << " editor project settings tests passed\n";
    return 0;
}
