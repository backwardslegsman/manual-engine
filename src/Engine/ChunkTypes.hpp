#pragma once

#include <cstddef>
#include <cstdint>

namespace Engine {
    struct ChunkCoord {
        int32_t x = 0;
        int32_t z = 0;

        bool operator==(const ChunkCoord& other) const = default;
    };

    struct ChunkCoordHash {
        size_t operator()(const ChunkCoord& coord) const;
    };

    struct ChunkSettings {
        float chunkSize = 16.0f;
        int32_t loadRadiusChunks = 1;
    };
}
