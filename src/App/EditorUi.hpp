#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "App/EditorProjectSettings.hpp"
#include "App/EditorLiveApply.hpp"
#include "App/EditorRebuildCoordinator.hpp"
#include "App/EditorSettingsReflection.hpp"
#include "Engine/Reflection.hpp"

namespace ManualEngine::App {

    class EditorToolHooks;
    struct EditorToolScripts;

    struct EditorUiPropertyRow {
        EditorSettingsReflectionTarget target;
        EditorSettingsReflectedPropertyId property = EditorSettingsReflectedPropertyId::SchemaVersion;
        std::string objectName;
        std::string objectDisplayName;
        std::string category;
        std::string propertyName;
        std::string displayName;
        std::string units;
        Engine::ReflectedValueType type = Engine::ReflectedValueType::None;
        Engine::ReflectedPropertyFlag flags = Engine::ReflectedPropertyFlag::None;
        Engine::ReflectedValue currentValue;
        Engine::ReflectedValue baselineValue;
        std::vector<std::string> enumLabels;
        bool dirty = false;
        bool requiresExplicitApply = false;
        bool lightweightPending = false;
        bool advanced = false;
        bool readOnly = false;
        bool visible = true;
        bool supported = true;
        std::string diagnostic;
    };

    struct EditorUiDirtySummary {
        uint32_t dirtyPropertyCount = 0;
        uint32_t rebuildRequiredCount = 0;
        uint32_t lightweightPendingCount = 0;
    };

    struct EditorUiState {
        EditorProjectSettings settings;
        EditorProjectSettings baselineSettings;
        std::filesystem::path profilePath;
        std::string profileStatus;
        EditorProjectSettingsValidationResult validation;
        Engine::ReflectionRegistry reflectionRegistry;
        EditorRebuildCoordinatorState rebuildCoordinator;
        Engine::ReflectionResult lastEditResult;
        EditorProjectSettingsValidationResult liveValidation;
        std::string lastLiveApplyMessage;
        EditorToolHooks* toolHooks = nullptr;
        EditorToolScripts* toolScripts = nullptr;
        std::string selectedCategory = "Project";
        std::string searchText;
        bool showAdvanced = false;
        bool initialized = false;
    };

    void initializeEditorUiState(
        EditorUiState& state,
        const EditorProjectSettings& settings,
        const std::filesystem::path& profilePath,
        std::string profileStatus,
        EditorProjectSettingsValidationResult validation);

    [[nodiscard]] std::vector<std::string> editorUiCategories(const EditorUiState& state);
    [[nodiscard]] std::vector<EditorUiPropertyRow> buildEditorUiPropertyRows(const EditorUiState& state);
    [[nodiscard]] EditorUiDirtySummary editorUiDirtySummary(const std::vector<EditorUiPropertyRow>& rows);
    [[nodiscard]] Engine::ReflectionResult setEditorUiValue(
        EditorUiState& state,
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property,
        const Engine::ReflectedValue& value);
    [[nodiscard]] Engine::OpenWorldStreamingBuildResult runEditorUiRebuildCommand(
        EditorUiState& state,
        EditorRebuildCommand command);
    [[nodiscard]] bool applyEditorUiLightweightRuntimeSettings(EditorUiState& state);
    [[nodiscard]] bool applyEditorUiLightweightRuntimeSettings(
        EditorUiState& state,
        EditorLiveApplyHost* host);
    [[nodiscard]] bool reloadEditorUiStreamingRuntime(
        EditorUiState& state,
        EditorLiveApplyHost* host);
    [[nodiscard]] EditorProjectSettingsValidationResult validateEditorUiCurrentProfile(
        EditorUiState& state,
        bool requireSavedBuildManifest);
    [[nodiscard]] EditorProjectSettingsValidationResult validateEditorLiveApplySettings(
        const EditorProjectSettings& settings,
        bool requireSavedBuildManifest);

    void showEditorUi(EditorUiState& state, EditorLiveApplyHost* host = nullptr);

}
