#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Engine/ActorController.hpp"
#include "Engine/World.hpp"

namespace Engine {
    // Engine-owned actor selection state for RTS-style command selection.
    // Selection stores ActorHandles only; ActorController/World remain the sources
    // of truth for actor and object validity.
    class ActorSelection {
    public:
        void clear();
        void setSingle(ActorHandle actor);
        void toggle(ActorHandle actor);
        void setSelectedActors(std::vector<ActorHandle> actors);
        bool contains(ActorHandle actor) const;
        const std::vector<ActorHandle>& selectedActors() const;

    private:
        std::vector<ActorHandle> selectedActors_;
    };

    // Projects live actor object positions into the active viewport and returns
    // actors whose projected positions fall inside the screen-space rectangle.
    std::vector<ActorHandle> selectActorsInScreenRect(
        const ActorController& actors,
        const World& world,
        const glm::mat4& viewProjection,
        const glm::ivec2& viewportSize,
        const glm::vec2& rectStart,
        const glm::vec2& rectEnd
    );
}
