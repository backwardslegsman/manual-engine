#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct TerrainTileHandle {
        uint32_t id = UINT32_MAX;
    };

    struct TerrainSettings {
        float chunkSize = 16.0f;
        uint32_t resolution = 17;
        float heightScale = 2.0f;
    };

    struct TerrainTile {
        bool alive = false;
        ChunkCoord coord;
        glm::vec3 origin{};
        float size = 16.0f;
        uint32_t resolution = 17;
        std::vector<float> heights;
        Renderer::TerrainHandle rendererTerrain;
    };

    // Owns loaded CPU heightfield tiles and their matching renderer terrain tiles.
    class TerrainSystem {
    public:
        explicit TerrainSystem(TerrainSettings settings = {});

        TerrainTileHandle createTile(ChunkCoord coord, Renderer::TextureHandle baseColorTexture);
        void destroyTile(TerrainTileHandle handle);

        std::optional<float> sampleHeight(float worldX, float worldZ) const;
        float generatedHeight(float worldX, float worldZ) const;
        ChunkCoord coordForWorldPosition(float worldX, float worldZ) const;

        const TerrainSettings& settings() const;

    private:
        TerrainTile* tile(TerrainTileHandle handle);
        const TerrainTile* tile(TerrainTileHandle handle) const;
        const TerrainTile* tileForCoord(ChunkCoord coord) const;
        float tileHeightAt(const TerrainTile& tile, uint32_t x, uint32_t z) const;

        TerrainSettings settings_;
        std::vector<TerrainTile> tiles_;
    };
}
