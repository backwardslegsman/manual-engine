#pragma once

#include <string>

#include <glm/glm.hpp>

#include "Engine/EventQueue.hpp"
#include "Engine/ObjectArchetype.hpp"

namespace Engine {
    enum class InteractionOutcomeType {
        None,
        SelectObject,
        ClearSelection,
        Inspect,
        RemoveObject,
        HarvestResource,
        PlaceMarker,
        Rejected,
    };

    struct InteractionRequest {
        InteractionEvent event;
        ObjectId objectId;
        std::string archetypeId;
        const ObjectArchetypeDescriptor* archetype = nullptr;
    };

    struct InteractionOutcome {
        InteractionOutcomeType type = InteractionOutcomeType::None;
        std::string status;
        InteractionEvent event;
        ObjectId objectId;
        std::string archetypeId;
        std::string archetypeDisplayName;
        std::string archetypeTags;
        glm::vec3 position{};
        std::string resourceId;
        uint32_t resourceAmount = 0;
    };

    class InteractionHandlerSystem {
    public:
        InteractionOutcome handle(const InteractionRequest& request) const;

    private:
        static InteractionOutcome baseOutcome(const InteractionRequest& request, InteractionOutcomeType type);
        static InteractionOutcome rejected(const InteractionRequest& request, std::string message);
    };
}
