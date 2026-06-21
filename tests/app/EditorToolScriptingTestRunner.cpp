#include "App/EditorToolScripting.hpp"
#include "App/EditorUi.hpp"

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
        uint32_t applyCount = 0;
        uint32_t reloadCount = 0;
    };

    bool fakeApply(
        void* user,
        const ManualEngine::App::EditorProjectSettings&,
        std::string& message)
    {
        ++static_cast<FakeLiveHost*>(user)->applyCount;
        message = "fake apply";
        return true;
    }

    bool fakeReload(
        void* user,
        const ManualEngine::App::EditorProjectSettings& settings,
        std::string& message,
        Engine::OpenWorldStreamingBuildResult& result)
    {
        ++static_cast<FakeLiveHost*>(user)->reloadCount;
        result.success = true;
        result.message = "fake reload";
        result.fingerprint = Engine::openWorldStreamingRuntimeFingerprint(
            ManualEngine::App::streamingRuntimeSettingsFromEditorProject(settings));
        message = result.message;
        return true;
    }

    ManualEngine::App::EditorLiveApplyHost makeHost(FakeLiveHost& fake)
    {
        return {&fake, fakeApply, fakeReload};
    }

    std::filesystem::path tempRoot(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_editor_tools_" + std::string{name});
    }

    void writeScript(const std::filesystem::path& dir, std::string_view name, std::string_view source)
    {
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / name, std::ios::trunc);
        file << source;
    }

    ManualEngine::App::EditorUiState makeState()
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        settings.streaming.savedBuildManifestPath = tempRoot("state") / "missing_manifest.yaml";
        ManualEngine::App::EditorUiState state;
        ManualEngine::App::initializeEditorUiState(
            state,
            settings,
            "projects/default.editor.yaml",
            "test profile",
            ManualEngine::App::validateEditorProjectSettings(settings));
        return state;
    }

    ManualEngine::App::EditorToolScripts makeScripts(std::string_view name)
    {
        ManualEngine::App::EditorToolScripts scripts;
        scripts.scriptDirectory = tempRoot(name);
        std::filesystem::remove_all(scripts.scriptDirectory);
        return scripts;
    }

    void discoveryHandlesMissingAndLuaFiles(TestContext& ctx)
    {
        ManualEngine::App::EditorToolScripts scripts = makeScripts("discovery");
        ManualEngine::App::rescanEditorToolScripts(scripts);
        ctx.expect(scripts.scripts.empty(), "missing script directory should discover no scripts");
        ctx.expect(!scripts.diagnostics.empty(), "missing script directory should report diagnostic");

        writeScript(scripts.scriptDirectory, "b.lua", "editor.log('b')");
        writeScript(scripts.scriptDirectory, "a.lua", "editor.log('a')");
        std::ofstream ignored(scripts.scriptDirectory / "ignored.txt", std::ios::trunc);
        ignored << "no";
        ManualEngine::App::rescanEditorToolScripts(scripts);
        ctx.expect(scripts.scripts.size() == 2, "script discovery should include only lua files");
        ctx.expect(scripts.scripts[0].displayName == "a.lua", "scripts should be sorted by display name");
    }

    void descriptorsAreScriptVisible(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        uint32_t propertyCount = 0;
        for (const Engine::ReflectedObjectDescriptor& object : state.reflectionRegistry.objects()) {
            for (const Engine::ReflectedPropertyDescriptor& property : object.properties) {
                if (!Engine::hasFlag(property.flags, Engine::ReflectedPropertyFlag::EditorVisible)) {
                    continue;
                }
                ++propertyCount;
                ctx.expect(
                    Engine::hasFlag(property.flags, Engine::ReflectedPropertyFlag::ScriptVisible),
                    "editor-visible setting should be script-visible: " + object.name + "." + property.name);
                ctx.expect(
                    property.type != Engine::ReflectedValueType::OpaqueHandle,
                    "editor tool settings should not expose opaque handles");
            }
        }
        ctx.expect(propertyCount > 0, "expected reflected editor properties");
    }

    void luaReadsAndWritesSettings(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        ManualEngine::App::EditorToolHooks hooks;
        ManualEngine::App::EditorToolScripts scripts = makeScripts("read_write");
        state.toolHooks = &hooks;
        state.toolScripts = &scripts;
        writeScript(scripts.scriptDirectory, "edit.lua", R"lua(
            local renderer = { object = "EditorRendererSettings", index = 0 }
            local old = editor.settings.get(renderer, "propMaxDrawDistance")
            editor.log("old=" .. tostring(old))
            local ok = editor.settings.set(renderer, "propMaxDrawDistance", 345)
            if ok ~= true then error("valid set failed") end
            local terrain = { object = "EditorTerrainImportSettings", index = 0 }
            local bad = editor.settings.set(terrain, "sampleSpacing", -1)
            if bad ~= false then error("invalid set should fail") end
        )lua");
        ManualEngine::App::rescanEditorToolScripts(scripts);
        const bool ran = ManualEngine::App::runEditorToolScript(scripts, 0, state, nullptr);
        ctx.expect(ran, "read/write script should succeed");
        ctx.expect(state.settings.renderer.propMaxDrawDistance == 345.0f, "script should update renderer setting");
        ctx.expect(state.settings.streaming.bake.heightmap.sampleSpacing > 0.0f, "invalid script edit should not mutate setting");
        ctx.expect(!scripts.scripts[0].logs.empty(), "script should collect editor.log output");
    }

    void commandsAndHooksRouteThroughEditorPaths(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        ManualEngine::App::EditorToolHooks hooks;
        ManualEngine::App::EditorToolScripts scripts = makeScripts("commands");
        FakeLiveHost fake;
        ManualEngine::App::EditorLiveApplyHost host = makeHost(fake);
        uint32_t settingEvents = 0;
        uint32_t validationEvents = 0;
        uint32_t applyEvents = 0;
        uint32_t reloadEvents = 0;
        uint32_t scriptFinishedEvents = 0;
        hooks.addHook([&](const ManualEngine::App::EditorToolEvent& event, ManualEngine::App::EditorToolContext&) {
            if (event.type == ManualEngine::App::EditorToolEventType::SettingsChanged) ++settingEvents;
            if (event.type == ManualEngine::App::EditorToolEventType::ValidationCompleted) ++validationEvents;
            if (event.type == ManualEngine::App::EditorToolEventType::LightweightApplyCompleted) ++applyEvents;
            if (event.type == ManualEngine::App::EditorToolEventType::StreamingReloadCompleted) ++reloadEvents;
            if (event.type == ManualEngine::App::EditorToolEventType::ScriptFinished) ++scriptFinishedEvents;
        });
        state.toolHooks = &hooks;
        state.toolScripts = &scripts;
        writeScript(scripts.scriptDirectory, "commands.lua", R"lua(
            editor.settings.set({ object = "EditorRendererSettings", index = 0 }, "terrainMaxDrawDistance", 444)
            local validation = editor.commands.validate({ require_saved_build = false })
            if validation.valid ~= true then error("validation should pass") end
            local dirty = editor.dirty()
            if dirty.lightweight_property_count < 1 then error("expected lightweight dirty setting") end
            if editor.commands.apply_lightweight() ~= true then error("apply should succeed") end
            editor.commands.reload_streaming()
        )lua");
        ManualEngine::App::rescanEditorToolScripts(scripts);
        const bool ran = ManualEngine::App::runEditorToolScript(scripts, 0, state, &host);
        ctx.expect(ran, "command script should succeed");
        ctx.expect(fake.applyCount == 1, "script should route apply through live host");
        ctx.expect(settingEvents == 1, "expected one settings changed hook");
        ctx.expect(validationEvents == 1, "expected one validation hook");
        ctx.expect(applyEvents == 1, "expected one apply hook");
        ctx.expect(reloadEvents == 1, "expected one reload hook");
        ctx.expect(scriptFinishedEvents == 1, "expected one script finished hook");
    }

    void failedScriptRecordsDiagnostics(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        ManualEngine::App::EditorToolScripts scripts = makeScripts("failed");
        writeScript(scripts.scriptDirectory, "fail.lua", "error('expected failure')");
        ManualEngine::App::rescanEditorToolScripts(scripts);
        const bool ran = ManualEngine::App::runEditorToolScript(scripts, 0, state, nullptr);
        ctx.expect(!ran, "failing script should report failure");
        ctx.expect(
            scripts.scripts[0].status == ManualEngine::App::EditorToolScriptStatus::Failed,
            "failing script should store failed status");
        ctx.expect(
            scripts.scripts[0].message.find("expected failure") != std::string::npos,
            "failing script should store error message");
    }

    void manualEngineDoesNotLinkEditorToolScripting(TestContext& ctx)
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
        ctx.expect(
            manualSection.find("EditorToolScripting.cpp") == std::string::npos,
            "manual_engine should not link EditorToolScripting.cpp");
    }

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> Tests = {
        {"DiscoveryHandlesMissingAndLuaFiles", discoveryHandlesMissingAndLuaFiles},
        {"DescriptorsAreScriptVisible", descriptorsAreScriptVisible},
        {"LuaReadsAndWritesSettings", luaReadsAndWritesSettings},
        {"CommandsAndHooksRouteThroughEditorPaths", commandsAndHooksRouteThroughEditorPaths},
        {"FailedScriptRecordsDiagnostics", failedScriptRecordsDiagnostics},
        {"ManualEngineDoesNotLinkEditorToolScripting", manualEngineDoesNotLinkEditorToolScripting},
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
        std::cerr << ctx.failures << " editor tool scripting test failure(s)\n";
        return 1;
    }
    std::cout << Tests.size() << " editor tool scripting tests passed\n";
    return 0;
}
