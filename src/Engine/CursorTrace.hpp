#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "Engine/NavigationRuntime.hpp"

namespace Engine {
    enum class CursorTraceStatus {
        Success,
        InvalidInput,
        NoHit,
    };

    struct CursorWorldRay {
        CursorTraceStatus status = CursorTraceStatus::InvalidInput;
        glm::vec2 screenPosition{};
        glm::ivec2 viewportSize{};
        glm::vec3 origin{};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        std::string message;
    };

    struct CursorNavigationProjectionResult {
        CursorTraceStatus status = CursorTraceStatus::NoHit;
        CursorWorldRay ray;
        NavigationProjectionResult projection;
        glm::vec3 sampledPoint{};
        float distance = 0.0f;
        uint32_t sampleCount = 0;
        std::string message;
    };

    [[nodiscard]] const char* cursorTraceStatusName(CursorTraceStatus status);

    [[nodiscard]] CursorWorldRay cursorWorldRayFromViewProjection(
        glm::vec2 screenPosition,
        glm::ivec2 viewportSize,
        const glm::mat4& viewProjection);

    [[nodiscard]] CursorNavigationProjectionResult projectCursorRayToNavigation(
        SceneNavigationService& navigation,
        const CursorWorldRay& ray,
        const NavigationAgentConfig& agent = {},
        const NavigationQueryFilter& filter = {},
        float maxDistance = 2048.0f,
        float stepDistance = 1.0f);
}
