#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "App/EditorLiveApply.hpp"
#include "App/EditorRebuildCoordinator.hpp"
#include "App/EditorSettingsReflection.hpp"
#include "Engine/Reflection.hpp"

namespace ManualEngine::App {

    struct EditorUiState;

    enum class EditorToolEventType {
        SettingsChanged,
        ValidationCompleted,
        RebuildCompleted,
        LightweightApplyCompleted,
        StreamingReloadCompleted,
        ScriptStarted,
        ScriptFinished,
    };

    enum class EditorToolScriptStatus {
        NotRun,
        Success,
        Failed,
    };

    struct EditorToolEvent {
        EditorToolEventType type = EditorToolEventType::SettingsChanged;
        bool success = true;
        std::string message;
        EditorSettingsReflectionTarget target;
        EditorSettingsReflectedPropertyId property = EditorSettingsReflectedPropertyId::SchemaVersion;
        EditorRebuildCommand command = EditorRebuildCommand::ValidateCurrentProfile;
        std::filesystem::path scriptPath;
    };

    struct EditorToolScriptRecord {
        std::filesystem::path path;
        std::string displayName;
        EditorToolScriptStatus status = EditorToolScriptStatus::NotRun;
        std::string message;
        float elapsedMs = 0.0f;
        std::vector<std::string> logs;
    };

    class EditorToolContext {
    public:
        [[nodiscard]] Engine::ReflectionResult getSetting(
            EditorSettingsReflectionTarget target,
            EditorSettingsReflectedPropertyId property) const;
        [[nodiscard]] Engine::ReflectionResult setSetting(
            EditorSettingsReflectionTarget target,
            EditorSettingsReflectedPropertyId property,
            const Engine::ReflectedValue& value) const;
        [[nodiscard]] EditorProjectSettingsValidationResult validate(bool requireSavedBuildManifest) const;
        [[nodiscard]] Engine::OpenWorldStreamingBuildResult rebuild(EditorRebuildCommand command) const;
        [[nodiscard]] bool applyLightweight() const;
        [[nodiscard]] bool reloadStreaming() const;
        [[nodiscard]] EditorRebuildDirtyState dirty() const;
        void log(std::string message) const;
        [[nodiscard]] bool valid() const;
        [[nodiscard]] const Engine::ReflectionRegistry& reflectionRegistry() const;
        [[nodiscard]] const EditorProjectSettings& settings() const;
        void setLogSink(std::vector<std::string>* logs);

    private:
        friend class EditorToolScripts;
        friend EditorToolContext makeEditorToolContext(EditorUiState&, EditorLiveApplyHost*);

        EditorUiState* state_ = nullptr;
        EditorLiveApplyHost* liveHost_ = nullptr;
        std::vector<std::string>* logs_ = nullptr;
    };

    using EditorToolHook = std::function<void(const EditorToolEvent&, EditorToolContext&)>;

    class EditorToolHooks {
    public:
        uint32_t addHook(EditorToolHook hook);
        bool removeHook(uint32_t id);
        void emit(const EditorToolEvent& event, EditorToolContext& context);
        [[nodiscard]] uint32_t hookCount() const;
        [[nodiscard]] std::vector<EditorToolEvent> recentEvents() const;
        void clearEvents();

    private:
        struct HookRecord {
            uint32_t id = 0;
            EditorToolHook hook;
        };

        std::vector<HookRecord> hooks_;
        std::vector<EditorToolEvent> recentEvents_;
        uint32_t nextId_ = 1;
    };

    struct EditorToolScripts {
        std::filesystem::path scriptDirectory = "scripts/editor";
        std::vector<EditorToolScriptRecord> scripts;
        std::vector<std::string> diagnostics;
        uint32_t instructionBudget = 20000;
    };

    [[nodiscard]] EditorToolContext makeEditorToolContext(
        EditorUiState& state,
        EditorLiveApplyHost* liveHost);
    void emitEditorToolEvent(
        EditorUiState& state,
        EditorLiveApplyHost* liveHost,
        const EditorToolEvent& event);
    void rescanEditorToolScripts(EditorToolScripts& scripts);
    [[nodiscard]] bool runEditorToolScript(
        EditorToolScripts& scripts,
        size_t scriptIndex,
        EditorUiState& state,
        EditorLiveApplyHost* liveHost);
    void showEditorToolScriptsPanel(
        EditorToolScripts& scripts,
        EditorUiState& state,
        EditorLiveApplyHost* liveHost);
    [[nodiscard]] const char* editorToolEventName(EditorToolEventType type);
    [[nodiscard]] const char* editorToolScriptStatusName(EditorToolScriptStatus status);

}
