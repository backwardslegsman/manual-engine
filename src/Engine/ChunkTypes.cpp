#include "Engine/ChunkTypes.hpp"

#include <cstdint>

namespace Engine {
    size_t ChunkCoordHash::operator()(const ChunkCoord& coord) const
    {
        const uint64_t x = static_cast<uint32_t>(coord.x);
        const uint64_t z = static_cast<uint32_t>(coord.z);
        return static_cast<size_t>((x << 32u) ^ z);
    }
}
