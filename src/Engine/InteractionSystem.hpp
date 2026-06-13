#pragma once

#include <string>

#include "Engine/EventQueue.hpp"
#include "Engine/Picking.hpp"

namespace Engine {
    struct InteractionSystemSettings {
        std::string selectAction = "interaction.select";
        std::string interactAction = "interaction.interact";
        std::string removeObjectAction = "interaction.remove_object";
        std::string placeMarkerAction = "interaction.place_marker";
    };

    // Converts semantic input actions plus current debug picking into
    // target-aware interaction events. It does not mutate world or selection.
    class InteractionSystem {
    public:
        explicit InteractionSystem(InteractionSystemSettings settings = {});

        void publishEvents(
            const EventQueue& inputEvents,
            const DebugSelectionState& picking,
            EventQueue& outputEvents
        ) const;

    private:
        InteractionEvent makeTargetedEvent(InteractionAction action, const DebugSelectionState& picking) const;
        static bool pressedDigitalAction(const InputActionEvent& event, const std::string& actionName);

        InteractionSystemSettings settings_;
    };
}
