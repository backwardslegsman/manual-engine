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

    float followAlpha(float smoothing, float dt)
    {
        if (smoothing <= 0.0f || dt <= 0.0f) {
            return 1.0f;
        }
        return std::clamp(1.0f - std::exp(-smoothing * dt), 0.0f, 1.0f);
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
        update(events, dt, std::nullopt);
    }

    void OrbitCameraController::update(const EventQueue& events, float dt, std::optional<glm::vec3> targetPosition)
    {
        if (targetPosition) {
            setFollowTarget(*targetPosition);
        } else {
            clearFollowTarget();
        }

        for (const InputActionEvent& event : events.inputActions()) {
            if (event.action == "camera.pan" && event.payloadType == InputActionPayloadType::Axis2) {
                const glm::vec3 pan = panRight() * event.axis2Value.x + panForward() * event.axis2Value.y;
                if (glm::dot(pan, pan) <= 0.0f) {
                    continue;
                }

                if (event.source == InputActionSource::MouseDrag && settings_.enableMousePan) {
                    if (state_.mode == CameraMode::FollowTarget && followSettings_.allowManualFollowOffset && followState_.hasTarget) {
                        followState_.offset += pan * settings_.mousePanSensitivity;
                        state_.followOffset = followState_.offset;
                    } else {
                        state_.pivot += pan * settings_.mousePanSensitivity;
                    }
                } else if (event.source == InputActionSource::EdgeScroll && settings_.enableEdgePan) {
                    const glm::vec3 delta = glm::normalize(pan) * settings_.edgePanSpeed * dt;
                    if (state_.mode == CameraMode::FollowTarget && followSettings_.allowManualFollowOffset && followState_.hasTarget) {
                        followState_.offset += delta;
                        state_.followOffset = followState_.offset;
                    } else {
                        state_.pivot += delta;
                    }
                } else if (event.source == InputActionSource::Keyboard && settings_.enableKeyboardPan) {
                    const glm::vec3 delta = glm::normalize(pan) * settings_.keyboardPanSpeed * dt;
                    if (state_.mode == CameraMode::FollowTarget && followSettings_.allowManualFollowOffset && followState_.hasTarget) {
                        followState_.offset += delta;
                        state_.followOffset = followState_.offset;
                    } else {
                        state_.pivot += delta;
                    }
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

        updateFollowPivot(dt);
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

    const CameraFollowSettings& OrbitCameraController::followSettings() const
    {
        return followSettings_;
    }

    CameraFollowSettings& OrbitCameraController::followSettings()
    {
        return followSettings_;
    }

    CameraMode OrbitCameraController::mode() const
    {
        return state_.mode;
    }

    void OrbitCameraController::setMode(CameraMode mode)
    {
        state_.mode = mode;
        clampState();
    }

    const CameraFollowState& OrbitCameraController::followState() const
    {
        return followState_;
    }

    void OrbitCameraController::setFollowTarget(const glm::vec3& position)
    {
        followState_.targetPosition = position;
        followState_.hasTarget = true;
    }

    void OrbitCameraController::clearFollowTarget()
    {
        followState_.hasTarget = false;
    }

    void OrbitCameraController::setFollowOffset(const glm::vec3& offset)
    {
        followState_.offset = offset;
        state_.followOffset = offset;
        clampState();
    }

    void OrbitCameraController::resetFollowOffset()
    {
        setFollowOffset({});
    }

    const CameraState& OrbitCameraController::state() const
    {
        return state_;
    }

    void OrbitCameraController::setState(const CameraState& state)
    {
        state_ = state;
        followState_.offset = state_.followOffset;
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
        followSettings_.followSmoothing = std::max(followSettings_.followSmoothing, 0.0f);
        followSettings_.maxFollowLag = std::max(followSettings_.maxFollowLag, 0.0f);

        state_.pitchRadians = std::clamp(state_.pitchRadians, settings_.minPitchRadians, settings_.maxPitchRadians);
        state_.distance = std::clamp(state_.distance, settings_.minDistance, settings_.maxDistance);
        state_.pivot.x = std::clamp(state_.pivot.x, settings_.minPivotXZ.x, settings_.maxPivotXZ.x);
        state_.pivot.z = std::clamp(state_.pivot.z, settings_.minPivotXZ.y, settings_.maxPivotXZ.y);
        state_.followOffset = followState_.offset;
    }

    void OrbitCameraController::updateFollowPivot(float dt)
    {
        if (state_.mode != CameraMode::FollowTarget || !followState_.hasTarget) {
            return;
        }

        const glm::vec3 desiredPivot = followState_.targetPosition + followState_.offset;
        state_.pivot = glm::mix(state_.pivot, desiredPivot, followAlpha(followSettings_.followSmoothing, dt));

        if (followSettings_.maxFollowLag > 0.0f) {
            const glm::vec3 lag = state_.pivot - desiredPivot;
            const float lagDistance = glm::length(lag);
            if (lagDistance > followSettings_.maxFollowLag) {
                state_.pivot = desiredPivot + (lag / lagDistance) * followSettings_.maxFollowLag;
            }
        }
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
