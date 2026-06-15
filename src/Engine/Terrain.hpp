#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Biome.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Picking.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct TerrainTileHandle {
        uint32_t id = UINT32_MAX;
    };

    struct TerrainLodLevel {
        float startDistance = 0.0f;
        uint32_t resolution = 33;
    };

    constexpr uint32_t TerrainLodLevelCount = 6;

    struct TerrainSettings {
        float chunkSize = 16.0f;
        uint32_t resolution = 33;
        uint32_t navigationResolution = 17;
        float heightScale = 2.0f;
        float skirtDepth = 2.0f;
        bool createRendererResources = true;
        // Optional external biome rules. The caller owns this system and must
        // keep it alive for the TerrainSystem lifetime.
        const BiomeSystem* biomes = nullptr;
        std::array<TerrainLodLevel, TerrainLodLevelCount> lodLevels{{
            {0.0f, 33},
            {32.0f, 17},
            {64.0f, 9},
            {96.0f, 5},
            {144.0f, 3},
            {224.0f, 2},
        }};
    };

    struct TerrainTile {
        bool alive = false;
        ChunkCoord coord;
        glm::vec3 origin{};
        float size = 16.0f;
        uint32_t resolution = 17;
        std::vector<float> heights;
        Renderer::TerrainHandle rendererTerrain;
        Renderer::MaterialHandle material;
        uint32_t currentLod = 0;
        BiomeSample biome;
    };

    struct TerrainTileDiagnostics {
        ChunkCoord coord;
        std::string biomeId;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        float averageHeight = 0.0f;
        float maxSlopeDegrees = 0.0f;
        float averageSlopeDegrees = 0.0f;
        float navWalkableTrianglePercent = 100.0f;
        uint32_t resolution = 0;
        float chunkSize = 0.0f;
    };

    struct GeneratedTerrainTileData {
        ChunkCoord coord;
        glm::vec3 origin{};
        float size = 16.0f;
        uint32_t resolution = 17;
        std::vector<float> heights;
        BiomeSample biome;
    };

    struct TerrainLodUpdateResult {
        uint32_t rebuiltCount = 0;
        uint32_t pendingCount = 0;

        bool rebuiltAny() const { return rebuiltCount > 0; }
    };

    // Owns loaded CPU heightfield tiles and their matching renderer terrain
    // tiles. Gameplay height queries always use CPU tile data; renderer LOD is
    // only a draw-mesh detail.
    class TerrainSystem {
    public:
        explicit TerrainSystem(TerrainSettings settings = {});

        TerrainTileHandle createTile(ChunkCoord coord, Renderer::MaterialHandle material);
        GeneratedTerrainTileData generateTileData(ChunkCoord coord) const;
        TerrainTileHandle createCpuTileFromGenerated(
            const GeneratedTerrainTileData& generated,
            Renderer::MaterialHandle material);
        TerrainTileHandle createTileFromGenerated(
            const GeneratedTerrainTileData& generated,
            Renderer::MaterialHandle material);
        Renderer::TerrainHandle ensureRendererTerrain(TerrainTileHandle handle);
        TerrainTileHandle createTileFromHeights(
            ChunkCoord coord,
            std::span<const float> heights,
            Renderer::MaterialHandle material = {});
        void destroyTile(TerrainTileHandle handle);
        Renderer::TerrainHandle rendererTerrain(TerrainTileHandle handle) const;
        bool updateLods(const glm::vec3& cameraPosition);
        TerrainLodUpdateResult updateLodsBudgeted(const glm::vec3& cameraPosition, uint32_t maxRebuilds);
        std::array<uint32_t, TerrainLodLevelCount> lodCounts() const;

        std::optional<float> sampleHeight(float worldX, float worldZ) const;
        std::optional<TerrainPickHit> raycast(
            const Ray& ray,
            float maxDistance = 500.0f,
            float stepDistance = 1.0f,
            uint32_t refinementIterations = 8
        ) const;
        float generatedHeight(float worldX, float worldZ) const;
        BiomeSample sampleBiome(float worldX, float worldZ) const;
        BiomeSample sampleChunkBiome(ChunkCoord coord) const;
        std::optional<BiomeSample> tileBiome(TerrainTileHandle handle) const;
        std::optional<ChunkCoord> tileCoord(TerrainTileHandle handle) const;
        std::optional<Renderer::Aabb> tileWorldBounds(TerrainTileHandle handle) const;
        std::optional<TerrainTileDiagnostics> tileDiagnostics(
            TerrainTileHandle handle,
            const NavAgentSettings* agent = nullptr) const;
        std::vector<glm::vec3> slopeWarningSamples(
            TerrainTileHandle handle,
            float maxSlopeDegrees,
            uint32_t maxSamples = 128) const;
        std::optional<NavigationTerrainBuildData> navigationBuildData(TerrainTileHandle handle) const;
        std::optional<NavigationTerrainBuildData> navigationBuildData(
            TerrainTileHandle handle,
            uint32_t navigationResolution) const;
        static std::optional<float> sampleGeneratedHeight(
            const GeneratedTerrainTileData& generated,
            float worldX,
            float worldZ);
        static std::optional<NavigationTerrainBuildData> navigationBuildData(
            const GeneratedTerrainTileData& generated);
        static std::optional<NavigationTerrainBuildData> navigationBuildData(
            const GeneratedTerrainTileData& generated,
            uint32_t navigationResolution);
        ChunkCoord coordForWorldPosition(float worldX, float worldZ) const;

        const TerrainSettings& settings() const;

    private:
        TerrainTile* tile(TerrainTileHandle handle);
        const TerrainTile* tile(TerrainTileHandle handle) const;
        TerrainTileHandle storeTile(TerrainTile terrain);
        const TerrainTile* tileForCoord(ChunkCoord coord) const;
        float tileHeightAt(const TerrainTile& tile, uint32_t x, uint32_t z) const;
        uint32_t chooseLod(const TerrainTile& tile, const glm::vec3& cameraPosition) const;
        Renderer::TerrainHandle createRendererTerrain(const TerrainTile& tile, uint32_t lodIndex) const;

        TerrainSettings settings_;
        std::vector<TerrainTile> tiles_;
    };
}
