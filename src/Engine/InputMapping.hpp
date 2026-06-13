#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/EventQueue.hpp"
#include "Engine/input.hpp"

namespace Engine {
    struct InputMappingLoadResult;

    class InputMapping {
    public:
        static InputMapping defaultCameraMapping();
        static InputMappingLoadResult loadFromYaml(const std::filesystem::path& path);

        void publishEvents(const InputState& input, EventQueue& events) const;

    private:
        struct AxisKeys {
            std::vector<Key> positiveX;
            std::vector<Key> negativeX;
            std::vector<Key> positiveY;
            std::vector<Key> negativeY;
        };

        struct MouseDragAxis {
            MouseButton button = MouseButton::Left;
            glm::vec2 scale{1.0f, 1.0f};
        };

        struct Axis2Action {
            std::string action;
            AxisKeys keys;
            std::optional<MouseDragAxis> mouseDrag;
            bool edgeScroll = false;
            int edgeScrollMarginPixels = 18;
        };

        struct ScalarAction {
            std::string action;
            float mouseWheelYScale = 0.0f;
        };

        struct DigitalAction {
            std::string action;
            std::vector<Key> keys;
            std::vector<MouseButton> mouseButtons;
        };

        std::vector<Axis2Action> axis2Actions_;
        std::vector<ScalarAction> scalarActions_;
        std::vector<DigitalAction> digitalActions_;
    };

    struct InputMappingLoadResult {
        bool success = false;
        std::string error;
        InputMapping mapping;
    };
}
