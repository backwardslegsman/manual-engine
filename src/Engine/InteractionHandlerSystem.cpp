#include "Engine/InteractionHandlerSystem.hpp"

#include <sstream>
#include <utility>

namespace Engine {
    InteractionOutcome InteractionHandlerSystem::handle(const InteractionRequest& request) const
    {
        const InteractionEvent& event = request.event;
        if (event.action == InteractionAction::Select) {
            if (event.target == InteractionTargetType::Object) {
                InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::SelectObject);
                outcome.status = "Selected object.";
                return outcome;
            }

            InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::ClearSelection);
            outcome.status = event.target == InteractionTargetType::Terrain
                ? "Cleared object selection on terrain."
                : "Cleared object selection.";
            return outcome;
        }

        if (event.action == InteractionAction::PlaceMarker) {
            if (event.target != InteractionTargetType::Terrain) {
                return rejected(request, "Place failed: no terrain hit under cursor.");
            }

            InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::PlaceMarker);
            outcome.status = "Place marker requested.";
            return outcome;
        }

        if (event.target != InteractionTargetType::Object) {
            if (event.action == InteractionAction::Interact && event.target == InteractionTargetType::Terrain) {
                InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::Inspect);
                std::ostringstream status;
                status << "Inspect terrain chunk " << event.chunk.x << ", " << event.chunk.z << ".";
                outcome.status = status.str();
                return outcome;
            }
            return rejected(request, "Interaction rejected: no object target.");
        }

        if (!request.archetype) {
            return rejected(request, "Interaction rejected: missing archetype descriptor.");
        }

        if (event.action == InteractionAction::RemoveObject) {
            if (!hasTag(*request.archetype, "removable")) {
                return rejected(request, "Remove rejected: archetype is not removable.");
            }

            InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::RemoveObject);
            outcome.status = "Remove requested for " + request.archetype->displayName + ".";
            return outcome;
        }

        if (event.action == InteractionAction::Interact) {
            if (hasTag(*request.archetype, "resource_node")) {
                InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::HarvestResource);
                outcome.resourceId = request.archetype->resourceId.empty()
                    ? request.archetype->id
                    : request.archetype->resourceId;
                outcome.resourceAmount = request.archetype->resourceAmount == 0
                    ? 1
                    : request.archetype->resourceAmount;
                std::ostringstream status;
                status << "Harvested " << outcome.resourceAmount << " " << outcome.resourceId
                    << " from " << request.archetype->displayName << ".";
                outcome.status = status.str();
                return outcome;
            }

            if (hasTag(*request.archetype, "inspectable")) {
                InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::Inspect);
                std::ostringstream status;
                status << "Inspect " << request.archetype->displayName
                    << " [" << request.objectId.toString() << "]"
                    << " tags: " << outcome.archetypeTags << ".";
                outcome.status = status.str();
                return outcome;
            }

            return rejected(request, "Interact rejected: archetype is not inspectable or a resource node.");
        }

        return baseOutcome(request, InteractionOutcomeType::None);
    }

    InteractionOutcome InteractionHandlerSystem::baseOutcome(const InteractionRequest& request, InteractionOutcomeType type)
    {
        InteractionOutcome outcome;
        outcome.type = type;
        outcome.event = request.event;
        outcome.objectId = request.objectId;
        outcome.archetypeId = request.archetypeId;
        outcome.position = request.event.target == InteractionTargetType::Terrain
            ? request.event.terrainHitPosition
            : request.event.objectHitPosition;
        if (request.archetype) {
            outcome.archetypeDisplayName = request.archetype->displayName;
            outcome.archetypeTags = tagsToString(*request.archetype);
        }
        return outcome;
    }

    InteractionOutcome InteractionHandlerSystem::rejected(const InteractionRequest& request, std::string message)
    {
        InteractionOutcome outcome = baseOutcome(request, InteractionOutcomeType::Rejected);
        outcome.status = std::move(message);
        return outcome;
    }
}
