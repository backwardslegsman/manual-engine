#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "App/EditorProjectSettings.hpp"
#include "Engine/OpenWorldStreamingRuntime.hpp"

namespace ManualEngine::App {

    struct EditorUiPropertyRow;

    enum class EditorRebuildDomain : uint32_t {
        None = 0,
        Terrain = 1u << 0,
        RenderLods = 1u << 1,
        Navigation = 1u << 2,
        SceneGeometry = 1u << 3,
        PhysicsColliders = 1u << 4,
        StreamingSavedBuild = 1u << 5,
        LightweightRuntime = 1u << 6,
    };

    [[nodiscard]] constexpr EditorRebuildDomain operator|(
        EditorRebuildDomain lhs,
        EditorRebuildDomain rhs)
    {
        return static_cast<EditorRebuildDomain>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    constexpr EditorRebuildDomain& operator|=(
        EditorRebuildDomain& lhs,
        EditorRebuildDomain rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    [[nodiscard]] constexpr bool hasEditorRebuildDomain(
        EditorRebuildDomain domains,
        EditorRebuildDomain domain)
    {
        return (static_cast<uint32_t>(domains) & static_cast<uint32_t>(domain)) != 0;
    }

    enum class EditorRebuildCommand {
        TerrainChunks,
        RenderLods,
        Navigation,
        PhysicsColliders,
        FullSavedBuild,
        ApplyLightweightRuntime,
        ReloadStreamingRuntime,
        ValidateCurrentProfile,
    };

    enum class EditorRebuildCommandStatus {
        Idle,
        Running,
        Succeeded,
        Failed,
    };

    struct EditorRebuildDirtyState {
        EditorRebuildDomain domains = EditorRebuildDomain::None;
        uint32_t rebuildPropertyCount = 0;
        uint32_t lightweightPropertyCount = 0;
        std::string baselineFingerprint;
        std::string currentFingerprint;
        bool savedBuildFingerprintDirty = false;
    };

    struct EditorRebuildCoordinatorState {
        EditorRebuildCommandStatus status = EditorRebuildCommandStatus::Idle;
        EditorRebuildCommand lastCommand = EditorRebuildCommand::FullSavedBuild;
        Engine::OpenWorldStreamingBuildResult lastBuildResult;
        EditorRebuildDirtyState lastDirtyState;
        std::vector<std::string> diagnostics;
        std::string activeSavedBuildFingerprint;
        float lastElapsedMs = 0.0f;
        bool runtimeReloadRequired = false;
    };

    [[nodiscard]] const char* editorRebuildCommandName(EditorRebuildCommand command);
    [[nodiscard]] const char* editorRebuildDomainName(EditorRebuildDomain domain);
    [[nodiscard]] EditorRebuildDirtyState computeEditorRebuildDirtyState(
        const EditorProjectSettings& settings,
        const EditorProjectSettings& baselineSettings,
        const std::vector<EditorUiPropertyRow>& rows);
    [[nodiscard]] Engine::OpenWorldStreamingBuildResult runEditorSavedBuildRebuild(
        EditorRebuildCoordinatorState& coordinator,
        const EditorProjectSettings& settings,
        const EditorProjectSettings& baselineSettings,
        const std::vector<EditorUiPropertyRow>& rows,
        EditorRebuildCommand command);
    [[nodiscard]] bool applyEditorLightweightRuntimeBaseline(
        EditorRebuildCoordinatorState& coordinator,
        const EditorProjectSettings& settings,
        const EditorProjectSettings& baselineSettings,
        const std::vector<EditorUiPropertyRow>& rows);

}
