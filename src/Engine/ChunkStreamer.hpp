#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/ObjectId.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"

namespace Engine {
    struct ChunkContent {
        TerrainTileHandle terrain;
        std::vector<WorldObjectHandle> objects;
        Renderer::RenderGroupHandle renderGroup;
    };

    struct GeneratedChunkProp {
        ObjectId objectId;
        std::string archetypeId;
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{1.0f};
        Renderer::Aabb localBounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        glm::vec3 angularVelocity{};
        bool collisionEnabled = false;
    };

    struct GeneratedChunkData {
        ChunkCoord coord;
        GeneratedTerrainTileData terrain;
        std::vector<GeneratedChunkProp> props;
        std::string renderGroupName;
    };

    using ChunkContentFactory = std::function<ChunkContent(ChunkCoord, World&, TerrainSystem&)>;

    // Synchronous square-grid streamer for XZ world chunks. The streamer owns
    // chunk membership; World owns object state and Renderer owns draw data.
    class ChunkStreamer {
    public:
        explicit ChunkStreamer(ChunkSettings settings = {});

        bool update(
            const glm::vec3& centerWorldPosition,
            World& world,
            TerrainSystem& terrain,
            const ChunkContentFactory& factory,
            SpatialRegistry* spatialRegistry = nullptr
        );
        void loadChunk(ChunkCoord coord, World& world, TerrainSystem& terrain, const ChunkContentFactory& factory);
        bool registerLoadedChunk(ChunkCoord coord, ChunkContent content);
        void unloadChunk(ChunkCoord coord, World& world, TerrainSystem& terrain, SpatialRegistry* spatialRegistry = nullptr);
        void unloadAll(World& world, TerrainSystem& terrain, SpatialRegistry* spatialRegistry = nullptr);

        bool isLoaded(ChunkCoord coord) const;
        size_t loadedChunkCount() const;
        ChunkCoord coordForWorldPosition(const glm::vec3& position) const;
        std::vector<ChunkCoord> desiredChunksAround(ChunkCoord center) const;
        void forEachLoadedChunkContent(
            const std::function<void(TerrainTileHandle, const std::vector<WorldObjectHandle>&, Renderer::RenderGroupHandle)>& visitor
        ) const;

        const ChunkSettings& settings() const;

    private:
        struct ActiveChunk {
            TerrainTileHandle terrain;
            std::vector<WorldObjectHandle> objects;
            Renderer::RenderGroupHandle renderGroup;
        };

        ChunkSettings settings_;
        std::unordered_map<ChunkCoord, ActiveChunk, ChunkCoordHash> activeChunks_;
    };
}
