#include "Engine/ActorCommand.hpp"

#include <array>

namespace Engine {
    glm::vec2 formationOffsetForActorIndex(size_t index, float spacing)
    {
        if (index == 0 || spacing <= 0.0f) {
            return {};
        }

        const std::array<glm::vec2, 8> ringOffsets{
            glm::vec2{1.0f, 0.0f},
            glm::vec2{-1.0f, 0.0f},
            glm::vec2{0.0f, 1.0f},
            glm::vec2{0.0f, -1.0f},
            glm::vec2{1.0f, 1.0f},
            glm::vec2{-1.0f, 1.0f},
            glm::vec2{1.0f, -1.0f},
            glm::vec2{-1.0f, -1.0f},
        };
        const size_t adjusted = index - 1;
        const size_t ring = adjusted / ringOffsets.size() + 1;
        return ringOffsets[adjusted % ringOffsets.size()] * spacing * static_cast<float>(ring);
    }
}
