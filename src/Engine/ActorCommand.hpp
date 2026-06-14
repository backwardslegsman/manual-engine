#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace Engine {
    // Deterministic XZ formation slot used by simple multi-actor move commands.
    // Slot 0 is the requested destination; later slots fill expanding rings.
    glm::vec2 formationOffsetForActorIndex(size_t index, float spacing);
}
