#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/ObjectId.hpp"
#include "Engine/World.hpp"

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

    enum class InteractionAction {
        Select,
        Interact,
        RemoveObject,
        PlaceMarker,
    };

    enum class InteractionTargetType {
        None,
        Object,
        Terrain,
    };

    struct InteractionEvent {
        InteractionAction action = InteractionAction::Select;
        InteractionTargetType target = InteractionTargetType::None;
        WorldObjectHandle object;
        ObjectId objectId;
        glm::vec3 objectHitPosition{};
        glm::vec3 terrainHitPosition{};
        ChunkCoord chunk;
        float distance = 0.0f;
    };

    // Per-frame event queue. Producers publish during the frame, consumers read
    // the stable event list, and App clears it after all systems have consumed.
    class EventQueue {
    public:
        void publish(const InputActionEvent& event);
        void publish(const InteractionEvent& event);
        const std::vector<InputActionEvent>& inputActions() const;
        const std::vector<InteractionEvent>& interactionEvents() const;
        void clear();

    private:
        std::vector<InputActionEvent> inputActions_;
        std::vector<InteractionEvent> interactionEvents_;
    };
}
