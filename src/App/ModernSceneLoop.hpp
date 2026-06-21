#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "EditorLiveApply.hpp"
#include "EditorProjectSettings.hpp"

namespace ManualEngine::App {

    struct ModernSceneLaunchOptions {
        std::string windowTitle = "ManualEngine";
        std::string debugSceneModeLabel = "Modern default scene";
        bool editorMode = false;
        std::filesystem::path editorProfilePath;
        std::string editorProfileStatus;
        EditorProjectSettingsValidationResult editorProfileValidation;
        std::optional<EditorProjectSettings> editorProjectSettings;
        void* editorUiState = nullptr;
        void (*showEditorUi)(void*, EditorLiveApplyHost*) = nullptr;
    };

    int runModernSceneApp(const ModernSceneLaunchOptions& options);

}
