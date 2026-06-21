#include "Engine/CursorTrace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>

namespace Engine {
    namespace {
        bool finiteVec2(glm::vec2 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool finiteVec3(glm::vec3 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool finiteVec4(glm::vec4 value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::isfinite(value.w);
        }

        bool finiteMat4(const glm::mat4& value)
        {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!std::isfinite(value[column][row])) {
                        return false;
                    }
                }
            }
            return true;
        }

        glm::vec3 divideByW(glm::vec4 value)
        {
            return {value.x / value.w, value.y / value.w, value.z / value.w};
        }
    }

    const char* cursorTraceStatusName(CursorTraceStatus status)
    {
        switch (status) {
        case CursorTraceStatus::Success:
            return "Success";
        case CursorTraceStatus::InvalidInput:
            return "InvalidInput";
        case CursorTraceStatus::NoHit:
            return "NoHit";
        }
        return "Unknown";
    }

    CursorWorldRay cursorWorldRayFromViewProjection(
        glm::vec2 screenPosition,
        glm::ivec2 viewportSize,
        const glm::mat4& viewProjection)
    {
        CursorWorldRay result;
        result.screenPosition = screenPosition;
        result.viewportSize = viewportSize;

        if (viewportSize.x <= 0 || viewportSize.y <= 0 || !finiteVec2(screenPosition) || !finiteMat4(viewProjection)) {
            result.status = CursorTraceStatus::InvalidInput;
            result.message = "Invalid cursor ray input.";
            return result;
        }

        const float ndcX = (2.0f * screenPosition.x / static_cast<float>(viewportSize.x)) - 1.0f;
        const float ndcY = 1.0f - (2.0f * screenPosition.y / static_cast<float>(viewportSize.y));
        const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
        if (!finiteMat4(inverseViewProjection)) {
            result.status = CursorTraceStatus::InvalidInput;
            result.message = "Cursor ray view-projection matrix is not invertible.";
            return result;
        }

        const glm::vec4 nearClip = inverseViewProjection * glm::vec4{ndcX, ndcY, -1.0f, 1.0f};
        const glm::vec4 farClip = inverseViewProjection * glm::vec4{ndcX, ndcY, 1.0f, 1.0f};
        constexpr float Epsilon = 1.0e-6f;
        if (!finiteVec4(nearClip) || !finiteVec4(farClip) ||
            std::abs(nearClip.w) <= Epsilon || std::abs(farClip.w) <= Epsilon) {
            result.status = CursorTraceStatus::InvalidInput;
            result.message = "Cursor ray unprojection produced invalid clip coordinates.";
            return result;
        }

        const glm::vec3 nearPoint = divideByW(nearClip);
        const glm::vec3 farPoint = divideByW(farClip);
        const glm::vec3 direction = farPoint - nearPoint;
        const float directionLength = glm::length(direction);
        if (!finiteVec3(nearPoint) || !finiteVec3(farPoint) || !std::isfinite(directionLength) || directionLength <= Epsilon) {
            result.status = CursorTraceStatus::InvalidInput;
            result.message = "Cursor ray unprojection produced a degenerate world ray.";
            return result;
        }

        result.status = CursorTraceStatus::Success;
        result.origin = nearPoint;
        result.direction = direction / directionLength;
        result.message = "Cursor world ray built.";
        return result;
    }

    CursorNavigationProjectionResult projectCursorRayToNavigation(
        SceneNavigationService& navigation,
        const CursorWorldRay& ray,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter,
        float maxDistance,
        float stepDistance)
    {
        CursorNavigationProjectionResult result;
        result.ray = ray;

        if (ray.status != CursorTraceStatus::Success || !finiteVec3(ray.origin) || !finiteVec3(ray.direction) ||
            !std::isfinite(maxDistance) || !std::isfinite(stepDistance) || maxDistance <= 0.0f || stepDistance <= 0.0f) {
            result.status = CursorTraceStatus::InvalidInput;
            result.message = "Invalid cursor navigation projection input.";
            return result;
        }

        const float clampedStep = std::max(stepDistance, 0.05f);
        const float acceptanceDistance = std::max(clampedStep * 1.5f, std::max(agent.queryExtents.y, 0.25f));
        const uint32_t maxSamples = static_cast<uint32_t>(std::ceil(maxDistance / clampedStep)) + 1u;
        std::string lastProjectionMessage;
        for (uint32_t sampleIndex = 0; sampleIndex < maxSamples; ++sampleIndex) {
            const float distance = std::min(maxDistance, static_cast<float>(sampleIndex) * clampedStep);
            const glm::vec3 samplePoint = ray.origin + ray.direction * distance;
            if (!finiteVec3(samplePoint)) {
                break;
            }

            NavigationProjectionResult projection = navigation.projectPointInLoadedTiles(samplePoint, agent, filter);
            result.projection = projection;
            result.sampledPoint = samplePoint;
            result.distance = distance;
            result.sampleCount = sampleIndex + 1u;
            lastProjectionMessage = projection.message;
            const float projectionDistance = glm::length(projection.point - samplePoint);
            if (projection.status == NavigationRuntimeStatus::Success &&
                std::isfinite(projectionDistance) &&
                projectionDistance <= acceptanceDistance) {
                result.status = CursorTraceStatus::Success;
                result.message = "Cursor ray projected onto navigation.";
                return result;
            }
        }

        result.status = CursorTraceStatus::NoHit;
        result.message = lastProjectionMessage.empty()
            ? "Cursor ray did not project onto loaded navigation."
            : "Cursor ray did not project onto loaded navigation: " + lastProjectionMessage;
        return result;
    }
}
