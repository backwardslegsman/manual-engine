#include "EditorUi.hpp"
#include "EditorToolScripting.hpp"
#include "ModernSceneLoop.hpp"

#include <cstdio>
#include <filesystem>

int main(int, char**)
{
    ManualEngine::App::ModernSceneLaunchOptions options;
    options.windowTitle = "ManualEngine Editor";
    options.debugSceneModeLabel = "Editor - modern default scene";
    options.editorMode = true;
    options.editorProfilePath = "projects/default.editor.yaml";

    ManualEngine::App::EditorProjectSettingsLoadResult profile =
        ManualEngine::App::loadEditorProjectSettings(options.editorProfilePath);
    options.editorProjectSettings = profile.settings;
    options.editorProfileStatus = profile.message;
    options.editorProfileValidation = profile.validation;
    ManualEngine::App::EditorUiState editorUi;
    ManualEngine::App::EditorToolHooks editorHooks;
    ManualEngine::App::EditorToolScripts editorScripts;
    ManualEngine::App::rescanEditorToolScripts(editorScripts);
    ManualEngine::App::initializeEditorUiState(
        editorUi,
        profile.settings,
        options.editorProfilePath,
        profile.message,
        profile.validation);
    editorUi.toolHooks = &editorHooks;
    editorUi.toolScripts = &editorScripts;
    options.editorUiState = &editorUi;
    options.showEditorUi = [](void* state, ManualEngine::App::EditorLiveApplyHost* host) {
        ManualEngine::App::showEditorUi(*static_cast<ManualEngine::App::EditorUiState*>(state), host);
    };
    std::fprintf(stderr, "%s\n", profile.message.c_str());
    for (const std::string& error : profile.validation.errors) {
        std::fprintf(stderr, "Editor profile error: %s\n", error.c_str());
    }
    for (const std::string& warning : profile.validation.warnings) {
        std::fprintf(stderr, "Editor profile warning: %s\n", warning.c_str());
    }

    return ManualEngine::App::runModernSceneApp(options);
}
