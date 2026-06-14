#include "Engine/ChunkStreamer.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Engine {
    ChunkStreamer::ChunkStreamer(ChunkSettings settings)
        : settings_(settings)
    {
        settings_.chunkSize = std::max(settings_.chunkSize, 1.0f);
        settings_.loadRadiusChunks = std::max(settings_.loadRadiusChunks, 0);
    }

    bool ChunkStreamer::update(
        const glm::vec3& centerWorldPosition,
        World& world,
        TerrainSystem& terrain,
        const ChunkContentFactory& factory,
        SpatialRegistry* spatialRegistry)
    {
        const ChunkCoord center = coordForWorldPosition(centerWorldPosition);
        const std::vector<ChunkCoord> desired = desiredChunksAround(center);
        std::unordered_set<ChunkCoord, ChunkCoordHash> desiredSet(desired.begin(), desired.end());

        std::vector<ChunkCoord> chunksToUnload;
        for (const auto& [coord, _] : activeChunks_) {
            if (!desiredSet.contains(coord)) {
                chunksToUnload.push_back(coord);
            }
        }

        for (ChunkCoord coord : chunksToUnload) {
            unloadChunk(coord, world, terrain, spatialRegistry);
        }

        bool changed = !chunksToUnload.empty();
        for (ChunkCoord coord : desired) {
            const bool wasLoaded = isLoaded(coord);
            loadChunk(coord, world, terrain, factory);
            changed = changed || !wasLoaded;
        }
        return changed;
    }

    void ChunkStreamer::loadChunk(ChunkCoord coord, World& world, TerrainSystem& terrain, const ChunkContentFactory& factory)
    {
        if (isLoaded(coord)) {
            return;
        }

        const ChunkContent content = factory(coord, world, terrain);
        activeChunks_.insert({coord, ActiveChunk{content.terrain, content.objects, content.renderGroup}});
    }

    void ChunkStreamer::unloadChunk(ChunkCoord coord, World& world, TerrainSystem& terrain, SpatialRegistry* spatialRegistry)
    {
        const auto chunkIt = activeChunks_.find(coord);
        if (chunkIt == activeChunks_.end()) {
            return;
        }

        for (WorldObjectHandle object : chunkIt->second.objects) {
            if (spatialRegistry) {
                spatialRegistry->remove(object);
            }
            world.destroyObjectAndRendererInstance(object);
        }
        terrain.destroyTile(chunkIt->second.terrain);
        Renderer::destroyRenderGroup(chunkIt->second.renderGroup);
        activeChunks_.erase(chunkIt);
    }

    void ChunkStreamer::unloadAll(World& world, TerrainSystem& terrain, SpatialRegistry* spatialRegistry)
    {
        std::vector<ChunkCoord> loadedCoords;
        loadedCoords.reserve(activeChunks_.size());
        for (const auto& [coord, _] : activeChunks_) {
            loadedCoords.push_back(coord);
        }

        for (ChunkCoord coord : loadedCoords) {
            unloadChunk(coord, world, terrain, spatialRegistry);
        }
    }

    bool ChunkStreamer::isLoaded(ChunkCoord coord) const
    {
        return activeChunks_.contains(coord);
    }

    size_t ChunkStreamer::loadedChunkCount() const
    {
        return activeChunks_.size();
    }

    ChunkCoord ChunkStreamer::coordForWorldPosition(const glm::vec3& position) const
    {
        return {
            static_cast<int32_t>(std::floor(position.x / settings_.chunkSize)),
            static_cast<int32_t>(std::floor(position.z / settings_.chunkSize)),
        };
    }

    std::vector<ChunkCoord> ChunkStreamer::desiredChunksAround(ChunkCoord center) const
    {
        std::vector<ChunkCoord> chunks;
        const int32_t radius = settings_.loadRadiusChunks;
        chunks.reserve(static_cast<size_t>((radius * 2 + 1) * (radius * 2 + 1)));

        for (int32_t z = center.z - radius; z <= center.z + radius; ++z) {
            for (int32_t x = center.x - radius; x <= center.x + radius; ++x) {
                chunks.push_back({x, z});
            }
        }

        return chunks;
    }

    void ChunkStreamer::forEachLoadedChunkContent(
        const std::function<void(TerrainTileHandle, const std::vector<WorldObjectHandle>&, Renderer::RenderGroupHandle)>& visitor) const
    {
        for (const auto& [_, chunk] : activeChunks_) {
            visitor(chunk.terrain, chunk.objects, chunk.renderGroup);
        }
    }

    const ChunkSettings& ChunkStreamer::settings() const
    {
        return settings_;
    }
}
