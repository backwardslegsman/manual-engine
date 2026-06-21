#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/OpenWorldStreamingRuntime.hpp"
#include "Engine/OrbitCamera.hpp"
#include "Renderer/DebugUi.hpp"
#include "Renderer/Scene.hpp"

namespace ManualEngine::App {

    struct EditorCameraProfileSettings {
        Engine::CameraSettings settings;
        Engine::CameraFollowSettings follow;
        glm::vec3 pivotOffsetFromFocus{0.0f, 12.0f, 0.0f};
        float yawRadians = glm::radians(35.0f);
        float pitchRadians = glm::radians(-46.0f);
        float distance = 170.0f;
        Engine::CameraMode mode = Engine::CameraMode::Free;
    };

    struct EditorProjectSettings {
        uint32_t schemaVersion = 1;
        std::string projectName = "ManualEngine Default";
        Engine::OpenWorldStreamingRuntimeSettings streaming;
        Renderer::DebugUi::RendererDebugSettings renderer;
        Renderer::DebugDrawSettings debugDraw;
        EditorCameraProfileSettings camera;
    };

    struct EditorProjectSettingsValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    struct EditorProjectSettingsLoadResult {
        EditorProjectSettings settings;
        bool loaded = false;
        bool usedFallback = true;
        EditorProjectSettingsValidationResult validation;
        std::string message;
    };

    struct EditorProjectSettingsSaveResult {
        bool success = false;
        std::string message;
    };

    [[nodiscard]] EditorProjectSettings defaultEditorProjectSettings();
    [[nodiscard]] EditorProjectSettingsValidationResult validateEditorProjectSettings(
        const EditorProjectSettings& settings);
    [[nodiscard]] EditorProjectSettingsLoadResult loadEditorProjectSettings(
        const std::filesystem::path& path);
    [[nodiscard]] EditorProjectSettingsSaveResult saveEditorProjectSettings(
        const std::filesystem::path& path,
        const EditorProjectSettings& settings);
    [[nodiscard]] Engine::OpenWorldStreamingRuntimeSettings streamingRuntimeSettingsFromEditorProject(
        const EditorProjectSettings& settings);

}
