#include "App/EditorRebuildCoordinator.hpp"

#include <chrono>
#include <utility>

#include "App/EditorUi.hpp"

namespace ManualEngine::App {
    namespace {
        [[nodiscard]] bool isRebuildCommand(EditorRebuildCommand command)
        {
            return command != EditorRebuildCommand::ApplyLightweightRuntime &&
                command != EditorRebuildCommand::ReloadStreamingRuntime &&
                command != EditorRebuildCommand::ValidateCurrentProfile;
        }

        [[nodiscard]] EditorRebuildDomain domainForRow(const EditorUiPropertyRow& row)
        {
            switch (row.target.object) {
                case EditorSettingsReflectedObjectId::TerrainImport:
                case EditorSettingsReflectedObjectId::TerrainCache:
                    return EditorRebuildDomain::Terrain;
                case EditorSettingsReflectedObjectId::TerrainRenderLod:
                    return EditorRebuildDomain::RenderLods;
                case EditorSettingsReflectedObjectId::NavigationAgent:
                case EditorSettingsReflectedObjectId::NavigationBuild:
                case EditorSettingsReflectedObjectId::TerrainNavigation:
                    return EditorRebuildDomain::Navigation;
                case EditorSettingsReflectedObjectId::SceneGeometryFiltering:
                    return EditorRebuildDomain::SceneGeometry;
                case EditorSettingsReflectedObjectId::PhysicsColliders:
                    return EditorRebuildDomain::PhysicsColliders;
                case EditorSettingsReflectedObjectId::Streaming:
                    return EditorRebuildDomain::StreamingSavedBuild;
                case EditorSettingsReflectedObjectId::Renderer:
                case EditorSettingsReflectedObjectId::DebugDraw:
                case EditorSettingsReflectedObjectId::Camera:
                    return EditorRebuildDomain::LightweightRuntime;
                case EditorSettingsReflectedObjectId::Project:
                    return EditorRebuildDomain::None;
            }
            return EditorRebuildDomain::None;
        }

        void appendBuildDiagnostics(
            EditorRebuildCoordinatorState& coordinator,
            const Engine::OpenWorldStreamingBuildResult& build)
        {
            coordinator.diagnostics.clear();
            if (!build.reason.empty()) {
                coordinator.diagnostics.push_back(build.reason);
            }
            if (!build.message.empty()) {
                coordinator.diagnostics.push_back(build.message);
            }
            if (!build.bakeDiagnostics.message.empty() &&
                build.bakeDiagnostics.message != build.message) {
                coordinator.diagnostics.push_back(build.bakeDiagnostics.message);
            }
            for (const std::string& warning : build.bakeDiagnostics.warnings) {
                coordinator.diagnostics.push_back(warning);
            }
        }
    }

    const char* editorRebuildCommandName(EditorRebuildCommand command)
    {
        switch (command) {
            case EditorRebuildCommand::TerrainChunks:
                return "Rebuild Terrain Chunks";
            case EditorRebuildCommand::RenderLods:
                return "Rebuild Render LODs";
            case EditorRebuildCommand::Navigation:
                return "Rebuild Navigation";
            case EditorRebuildCommand::PhysicsColliders:
                return "Rebuild Physics Colliders";
            case EditorRebuildCommand::FullSavedBuild:
                return "Rebuild Full Saved Build";
            case EditorRebuildCommand::ApplyLightweightRuntime:
                return "Apply Lightweight Runtime Settings";
            case EditorRebuildCommand::ReloadStreamingRuntime:
                return "Reload Rebuilt Streaming Runtime";
            case EditorRebuildCommand::ValidateCurrentProfile:
                return "Validate Current Profile";
        }
        return "Unknown";
    }

    const char* editorRebuildDomainName(EditorRebuildDomain domain)
    {
        switch (domain) {
            case EditorRebuildDomain::Terrain:
                return "Terrain";
            case EditorRebuildDomain::RenderLods:
                return "Render LODs";
            case EditorRebuildDomain::Navigation:
                return "Navigation";
            case EditorRebuildDomain::SceneGeometry:
                return "Scene Geometry";
            case EditorRebuildDomain::PhysicsColliders:
                return "Physics Colliders";
            case EditorRebuildDomain::StreamingSavedBuild:
                return "Streaming Saved Build";
            case EditorRebuildDomain::LightweightRuntime:
                return "Lightweight Runtime";
            case EditorRebuildDomain::None:
                return "None";
        }
        return "Unknown";
    }

    EditorRebuildDirtyState computeEditorRebuildDirtyState(
        const EditorProjectSettings& settings,
        const EditorProjectSettings& baselineSettings,
        const std::vector<EditorUiPropertyRow>& rows)
    {
        EditorRebuildDirtyState dirty;
        dirty.currentFingerprint = Engine::openWorldStreamingRuntimeFingerprint(
            streamingRuntimeSettingsFromEditorProject(settings));
        dirty.baselineFingerprint = Engine::openWorldStreamingRuntimeFingerprint(
            streamingRuntimeSettingsFromEditorProject(baselineSettings));
        dirty.savedBuildFingerprintDirty = dirty.currentFingerprint != dirty.baselineFingerprint;

        for (const EditorUiPropertyRow& row : rows) {
            if (!row.dirty) {
                continue;
            }
            const EditorRebuildDomain domain = domainForRow(row);
            if (domain == EditorRebuildDomain::None) {
                continue;
            }
            dirty.domains |= domain;
            if (row.requiresExplicitApply) {
                ++dirty.rebuildPropertyCount;
                dirty.domains |= EditorRebuildDomain::StreamingSavedBuild;
            } else {
                ++dirty.lightweightPropertyCount;
            }
        }

        if (dirty.savedBuildFingerprintDirty) {
            dirty.domains |= EditorRebuildDomain::StreamingSavedBuild;
        }
        return dirty;
    }

    Engine::OpenWorldStreamingBuildResult runEditorSavedBuildRebuild(
        EditorRebuildCoordinatorState& coordinator,
        const EditorProjectSettings& settings,
        const EditorProjectSettings& baselineSettings,
        const std::vector<EditorUiPropertyRow>& rows,
        EditorRebuildCommand command)
    {
        coordinator.status = EditorRebuildCommandStatus::Running;
        coordinator.lastCommand = command;
        coordinator.lastDirtyState = computeEditorRebuildDirtyState(settings, baselineSettings, rows);
        coordinator.lastElapsedMs = 0.0f;
        coordinator.runtimeReloadRequired = false;
        coordinator.diagnostics.clear();

        Engine::OpenWorldStreamingBuildResult build;
        if (!isRebuildCommand(command)) {
            build.status = Engine::OpenWorldStreamingBuildStatus::Failed;
            build.success = false;
            build.message = "Command does not rebuild saved open-world streaming data.";
            coordinator.status = EditorRebuildCommandStatus::Failed;
            coordinator.lastBuildResult = build;
            appendBuildDiagnostics(coordinator, build);
            return coordinator.lastBuildResult;
        }

        const EditorProjectSettingsValidationResult validation = validateEditorProjectSettings(settings);
        if (!validation.valid) {
            build.status = Engine::OpenWorldStreamingBuildStatus::Failed;
            build.success = false;
            build.message = validation.errors.empty()
                ? "Editor project settings validation failed."
                : validation.errors.front();
            coordinator.status = EditorRebuildCommandStatus::Failed;
            coordinator.lastBuildResult = build;
            appendBuildDiagnostics(coordinator, build);
            for (const std::string& warning : validation.warnings) {
                coordinator.diagnostics.push_back(warning);
            }
            return coordinator.lastBuildResult;
        }

        const auto start = std::chrono::steady_clock::now();
        build = Engine::rebuildOpenWorldStreamingSavedBuild(
            streamingRuntimeSettingsFromEditorProject(settings));
        const auto end = std::chrono::steady_clock::now();
        coordinator.lastElapsedMs =
            std::chrono::duration<float, std::milli>(end - start).count();
        coordinator.status = build.success
            ? EditorRebuildCommandStatus::Succeeded
            : EditorRebuildCommandStatus::Failed;
        coordinator.runtimeReloadRequired = build.success;
        coordinator.lastBuildResult = std::move(build);
        appendBuildDiagnostics(coordinator, coordinator.lastBuildResult);
        return coordinator.lastBuildResult;
    }

    bool applyEditorLightweightRuntimeBaseline(
        EditorRebuildCoordinatorState& coordinator,
        const EditorProjectSettings& settings,
        const EditorProjectSettings& baselineSettings,
        const std::vector<EditorUiPropertyRow>& rows)
    {
        coordinator.lastCommand = EditorRebuildCommand::ApplyLightweightRuntime;
        coordinator.lastDirtyState = computeEditorRebuildDirtyState(settings, baselineSettings, rows);
        coordinator.lastElapsedMs = 0.0f;
        coordinator.diagnostics.clear();
        coordinator.lastBuildResult = {};
        if (!hasEditorRebuildDomain(
                coordinator.lastDirtyState.domains,
                EditorRebuildDomain::LightweightRuntime)) {
            coordinator.status = EditorRebuildCommandStatus::Succeeded;
            coordinator.diagnostics.push_back("No lightweight runtime settings are dirty.");
            return true;
        }
        coordinator.status = EditorRebuildCommandStatus::Succeeded;
        coordinator.diagnostics.push_back(
            "Lightweight runtime settings baseline advanced. Live apply is deferred to Phase E6.");
        return true;
    }

}
