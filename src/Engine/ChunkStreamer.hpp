#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"

namespace Engine {
    struct ChunkContent {
        TerrainTileHandle terrain;
        std::vector<WorldObjectHandle> objects;
    };

    using ChunkContentFactory = std::function<ChunkContent(ChunkCoord, World&, TerrainSystem&)>;

    // Synchronous square-grid streamer for XZ world chunks. The streamer owns
    // chunk membership; World owns object state and Renderer owns draw data.
    class ChunkStreamer {
    public:
        explicit ChunkStreamer(ChunkSettings settings = {});

        void update(const glm::vec3& centerWorldPosition, World& world, TerrainSystem& terrain, const ChunkContentFactory& factory);
        void loadChunk(ChunkCoord coord, World& world, TerrainSystem& terrain, const ChunkContentFactory& factory);
        void unloadChunk(ChunkCoord coord, World& world, TerrainSystem& terrain);
        void unloadAll(World& world, TerrainSystem& terrain);

        bool isLoaded(ChunkCoord coord) const;
        size_t loadedChunkCount() const;
        ChunkCoord coordForWorldPosition(const glm::vec3& position) const;
        std::vector<ChunkCoord> desiredChunksAround(ChunkCoord center) const;

        const ChunkSettings& settings() const;

    private:
        struct ActiveChunk {
            TerrainTileHandle terrain;
            std::vector<WorldObjectHandle> objects;
        };

        ChunkSettings settings_;
        std::unordered_map<ChunkCoord, ActiveChunk, ChunkCoordHash> activeChunks_;
    };
}
