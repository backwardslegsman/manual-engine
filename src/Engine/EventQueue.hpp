#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Engine {
    enum class InputActionPhase {
        Pressed,
        Released,
        Held,
    };

    enum class InputActionPayloadType {
        Digital,
        Axis2,
        Scalar,
    };

    enum class InputActionSource {
        Unknown,
        Keyboard,
        MouseButton,
        MouseDrag,
        MouseWheel,
        EdgeScroll,
    };

    struct InputActionEvent {
        std::string action;
        InputActionPhase phase = InputActionPhase::Held;
        InputActionPayloadType payloadType = InputActionPayloadType::Digital;
        InputActionSource source = InputActionSource::Unknown;
        bool digitalValue = false;
        glm::vec2 axis2Value{};
        float scalarValue = 0.0f;
    };

    // Per-frame event queue. Producers publish during the frame, consumers read
    // the stable event list, and App clears it after all systems have consumed.
    class EventQueue {
    public:
        void publish(const InputActionEvent& event);
        const std::vector<InputActionEvent>& inputActions() const;
        void clear();

    private:
        std::vector<InputActionEvent> inputActions_;
    };
}
