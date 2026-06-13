#include "Engine/OrbitCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace {
    constexpr float halfPi = 1.57079632679f;

    float safeAspect(float aspectRatio)
    {
        return aspectRatio > 0.0f ? aspectRatio : 1.0f;
    }
}

namespace Engine {
    OrbitCameraController::OrbitCameraController()
    {
        clampState();
    }

    OrbitCameraController::OrbitCameraController(CameraSettings settings, CameraState state)
        : settings_(settings),
          state_(state)
    {
        clampState();
    }

    void OrbitCameraController::update(const EventQueue& events, float dt)
    {
        for (const InputActionEvent& event : events.inputActions()) {
            if (event.action == "camera.pan" && event.payloadType == InputActionPayloadType::Axis2) {
                const glm::vec3 pan = panRight() * event.axis2Value.x + panForward() * event.axis2Value.y;
                if (glm::dot(pan, pan) <= 0.0f) {
                    continue;
                }

                if (event.source == InputActionSource::MouseDrag && settings_.enableMousePan) {
                    state_.pivot += pan * settings_.mousePanSensitivity;
                } else if (event.source == InputActionSource::EdgeScroll && settings_.enableEdgePan) {
                    state_.pivot += glm::normalize(pan) * settings_.edgePanSpeed * dt;
                } else if (event.source == InputActionSource::Keyboard && settings_.enableKeyboardPan) {
                    state_.pivot += glm::normalize(pan) * settings_.keyboardPanSpeed * dt;
                }
            } else if (event.action == "camera.rotate" &&
                       event.payloadType == InputActionPayloadType::Axis2 &&
                       event.source == InputActionSource::MouseDrag &&
                       settings_.enableMouseRotate) {
                state_.yawRadians += event.axis2Value.x * settings_.rotationSensitivity;
                state_.pitchRadians += event.axis2Value.y * settings_.rotationSensitivity;
            } else if (event.action == "camera.zoom" &&
                       event.payloadType == InputActionPayloadType::Scalar &&
                       settings_.enableZoom) {
                state_.distance += event.scalarValue * settings_.zoomSensitivity;
            }
        }

        clampState();
    }

    CameraMatrices OrbitCameraController::matrices(float aspectRatio) const
    {
        return {
            viewMatrix(),
            projectionMatrix(aspectRatio),
        };
    }

    glm::mat4 OrbitCameraController::viewMatrix() const
    {
        return glm::lookAt(position(), state_.pivot, {0.0f, 1.0f, 0.0f});
    }

    glm::mat4 OrbitCameraController::projectionMatrix(float aspectRatio) const
    {
        return glm::perspective(
            settings_.verticalFovRadians,
            safeAspect(aspectRatio),
            settings_.nearPlane,
            settings_.farPlane
        );
    }

    glm::vec3 OrbitCameraController::position() const
    {
        const float horizontalDistance = std::cos(state_.pitchRadians) * state_.distance;
        const glm::vec3 offset{
            std::sin(state_.yawRadians) * horizontalDistance,
            std::sin(state_.pitchRadians) * state_.distance,
            std::cos(state_.yawRadians) * horizontalDistance,
        };
        return state_.pivot - offset;
    }

    glm::vec3 OrbitCameraController::forward() const
    {
        return glm::normalize(state_.pivot - position());
    }

    const CameraSettings& OrbitCameraController::settings() const
    {
        return settings_;
    }

    CameraSettings& OrbitCameraController::settings()
    {
        return settings_;
    }

    const CameraState& OrbitCameraController::state() const
    {
        return state_;
    }

    void OrbitCameraController::setState(const CameraState& state)
    {
        state_ = state;
        clampState();
    }

    void OrbitCameraController::clampState()
    {
        const float minPitch = std::clamp(settings_.minPitchRadians, -halfPi + 0.01f, halfPi - 0.01f);
        const float maxPitch = std::clamp(settings_.maxPitchRadians, minPitch, halfPi - 0.01f);
        settings_.minPitchRadians = minPitch;
        settings_.maxPitchRadians = maxPitch;

        settings_.minDistance = std::max(settings_.minDistance, 0.1f);
        settings_.maxDistance = std::max(settings_.maxDistance, settings_.minDistance);
        settings_.nearPlane = std::max(settings_.nearPlane, 0.001f);
        settings_.farPlane = std::max(settings_.farPlane, settings_.nearPlane + 1.0f);

        state_.pitchRadians = std::clamp(state_.pitchRadians, settings_.minPitchRadians, settings_.maxPitchRadians);
        state_.distance = std::clamp(state_.distance, settings_.minDistance, settings_.maxDistance);
        state_.pivot.x = std::clamp(state_.pivot.x, settings_.minPivotXZ.x, settings_.maxPivotXZ.x);
        state_.pivot.z = std::clamp(state_.pivot.z, settings_.minPivotXZ.y, settings_.maxPivotXZ.y);
    }

    glm::vec3 OrbitCameraController::panRight() const
    {
        return glm::normalize(glm::vec3{std::cos(state_.yawRadians), 0.0f, -std::sin(state_.yawRadians)});
    }

    glm::vec3 OrbitCameraController::panForward() const
    {
        return glm::normalize(glm::vec3{std::sin(state_.yawRadians), 0.0f, std::cos(state_.yawRadians)});
    }
}
