#include "Engine/ActorSelection.hpp"

#include <algorithm>
#include <optional>

namespace Engine {
    namespace {
        bool sameActor(ActorHandle lhs, ActorHandle rhs)
        {
            return lhs.id == rhs.id;
        }

        bool projectToScreen(
            const glm::vec3& worldPosition,
            const glm::mat4& viewProjection,
            const glm::ivec2& viewportSize,
            glm::vec2& screenPosition)
        {
            if (viewportSize.x <= 0 || viewportSize.y <= 0) {
                return false;
            }

            const glm::vec4 clip = viewProjection * glm::vec4{worldPosition, 1.0f};
            if (clip.w <= 0.0001f) {
                return false;
            }

            const glm::vec3 ndc = glm::vec3{clip} / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f) {
                return false;
            }

            screenPosition = {
                (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportSize.x),
                (0.5f - ndc.y * 0.5f) * static_cast<float>(viewportSize.y),
            };
            return true;
        }
    }

    void ActorSelection::clear()
    {
        selectedActors_.clear();
    }

    void ActorSelection::setSingle(ActorHandle actor)
    {
        selectedActors_.clear();
        if (actor.id != UINT32_MAX) {
            selectedActors_.push_back(actor);
        }
    }

    void ActorSelection::toggle(ActorHandle actor)
    {
        if (actor.id == UINT32_MAX) {
            return;
        }

        const auto actorIt = std::ranges::find_if(selectedActors_, [actor](ActorHandle selected) {
            return sameActor(actor, selected);
        });
        if (actorIt == selectedActors_.end()) {
            selectedActors_.push_back(actor);
        } else {
            selectedActors_.erase(actorIt);
        }
    }

    void ActorSelection::setSelectedActors(std::vector<ActorHandle> actors)
    {
        selectedActors_.clear();
        for (ActorHandle actor : actors) {
            if (actor.id != UINT32_MAX && !contains(actor)) {
                selectedActors_.push_back(actor);
            }
        }
    }

    bool ActorSelection::contains(ActorHandle actor) const
    {
        return std::ranges::any_of(selectedActors_, [actor](ActorHandle selected) {
            return sameActor(actor, selected);
        });
    }

    const std::vector<ActorHandle>& ActorSelection::selectedActors() const
    {
        return selectedActors_;
    }

    std::vector<ActorHandle> selectActorsInScreenRect(
        const ActorController& actors,
        const World& world,
        const glm::mat4& viewProjection,
        const glm::ivec2& viewportSize,
        const glm::vec2& rectStart,
        const glm::vec2& rectEnd)
    {
        const glm::vec2 min{
            std::min(rectStart.x, rectEnd.x),
            std::min(rectStart.y, rectEnd.y),
        };
        const glm::vec2 max{
            std::max(rectStart.x, rectEnd.x),
            std::max(rectStart.y, rectEnd.y),
        };

        std::vector<ActorHandle> selected;
        actors.forEachActor([&](ActorHandle actor, const ActorState& state) {
            if (!world.isValid(state.object)) {
                return;
            }

            const std::optional<glm::vec3> position = world.position(state.object);
            if (!position) {
                return;
            }

            glm::vec2 screenPosition;
            if (!projectToScreen(*position, viewProjection, viewportSize, screenPosition)) {
                return;
            }

            if (screenPosition.x >= min.x &&
                screenPosition.x <= max.x &&
                screenPosition.y >= min.y &&
                screenPosition.y <= max.y) {
                selected.push_back(actor);
            }
        });
        return selected;
    }
}
