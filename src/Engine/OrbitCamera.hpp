#pragma once

#include <glm/glm.hpp>

#include "Engine/EventQueue.hpp"

namespace Engine {
    struct CameraSettings {
        float verticalFovRadians = glm::radians(60.0f);
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        float minPitchRadians = glm::radians(-80.0f);
        float maxPitchRadians = glm::radians(-20.0f);
        float minDistance = 3.0f;
        float maxDistance = 80.0f;

        float keyboardPanSpeed = 12.0f;
        float edgePanSpeed = 10.0f;
        float mousePanSensitivity = 0.03f;
        float rotationSensitivity = 0.01f;
        float zoomSensitivity = 4.0f;

        glm::vec2 minPivotXZ{-100.0f, -100.0f};
        glm::vec2 maxPivotXZ{100.0f, 100.0f};
        int edgeScrollMarginPixels = 18;

        bool enableKeyboardPan = true;
        bool enableEdgePan = true;
        bool enableMousePan = true;
        bool enableMouseRotate = true;
        bool enableZoom = true;
    };

    struct CameraState {
        glm::vec3 pivot{0.0f, 0.0f, 0.0f};
        float yawRadians = 0.0f;
        float pitchRadians = glm::radians(-35.0f);
        float distance = 10.0f;
    };

    struct CameraMatrices {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
    };

    // Constrained sim/strategy camera. The pivot moves on XZ, while yaw,
    // pitch, and distance define the camera position around that pivot.
    class OrbitCameraController {
    public:
        OrbitCameraController();
        explicit OrbitCameraController(CameraSettings settings, CameraState state = {});

        void update(const EventQueue& events, float dt);

        CameraMatrices matrices(float aspectRatio) const;
        glm::mat4 viewMatrix() const;
        glm::mat4 projectionMatrix(float aspectRatio) const;
        glm::vec3 position() const;
        glm::vec3 forward() const;

        const CameraSettings& settings() const;
        CameraSettings& settings();
        const CameraState& state() const;
        void setState(const CameraState& state);

    private:
        void clampState();
        glm::vec3 panRight() const;
        glm::vec3 panForward() const;

        CameraSettings settings_;
        CameraState state_;
    };
}
