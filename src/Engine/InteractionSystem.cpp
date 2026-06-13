#include "Engine/InteractionSystem.hpp"

#include <utility>

namespace Engine {
    InteractionSystem::InteractionSystem(InteractionSystemSettings settings)
        : settings_(std::move(settings))
    {
    }

    void InteractionSystem::publishEvents(
        const EventQueue& inputEvents,
        const DebugSelectionState& picking,
        EventQueue& outputEvents) const
    {
        for (const InputActionEvent& event : inputEvents.inputActions()) {
            if (pressedDigitalAction(event, settings_.selectAction)) {
                outputEvents.publish(makeTargetedEvent(InteractionAction::Select, picking));
            } else if (pressedDigitalAction(event, settings_.interactAction)) {
                outputEvents.publish(makeTargetedEvent(InteractionAction::Interact, picking));
            } else if (pressedDigitalAction(event, settings_.removeObjectAction)) {
                outputEvents.publish(makeTargetedEvent(InteractionAction::RemoveObject, picking));
            } else if (pressedDigitalAction(event, settings_.placeMarkerAction)) {
                outputEvents.publish(makeTargetedEvent(InteractionAction::PlaceMarker, picking));
            }
        }
    }

    InteractionEvent InteractionSystem::makeTargetedEvent(InteractionAction action, const DebugSelectionState& picking) const
    {
        InteractionEvent event;
        event.action = action;

        if (action == InteractionAction::RemoveObject) {
            if (picking.selectedObject) {
                event.target = InteractionTargetType::Object;
                event.object = picking.selectedObject->object;
                event.objectId = picking.selectedObject->objectId;
                event.objectHitPosition = picking.selectedObject->position;
                event.chunk = picking.selectedObject->cell;
                event.distance = picking.selectedObject->distance;
            }
            return event;
        }

        if (action == InteractionAction::PlaceMarker) {
            if (picking.terrainHit) {
                event.target = InteractionTargetType::Terrain;
                event.terrainHitPosition = picking.terrainHit->position;
                event.chunk = picking.terrainHit->chunk;
                event.distance = picking.terrainHit->distance;
            }
            return event;
        }

        const std::optional<ObjectPickHit>& objectTarget =
            action == InteractionAction::Interact && picking.selectedObject
                ? picking.selectedObject
                : picking.hoveredObject;
        if (objectTarget) {
            event.target = InteractionTargetType::Object;
            event.object = objectTarget->object;
            event.objectId = objectTarget->objectId;
            event.objectHitPosition = objectTarget->position;
            event.chunk = objectTarget->cell;
            event.distance = objectTarget->distance;
            return event;
        }

        if (picking.terrainHit) {
            event.target = InteractionTargetType::Terrain;
            event.terrainHitPosition = picking.terrainHit->position;
            event.chunk = picking.terrainHit->chunk;
            event.distance = picking.terrainHit->distance;
        }
        return event;
    }

    bool InteractionSystem::pressedDigitalAction(const InputActionEvent& event, const std::string& actionName)
    {
        return event.action == actionName &&
            event.phase == InputActionPhase::Pressed &&
            event.payloadType == InputActionPayloadType::Digital &&
            event.digitalValue;
    }
}
