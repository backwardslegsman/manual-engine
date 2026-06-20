#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Biome.hpp"
#include "Engine/ChunkTypes.hpp"
#include "Engine/TerrainImport.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
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

    struct TerrainRenderMeshData {
        ChunkCoord coord;
        uint64_t generation = 0;
        uint32_t lodIndex = 0;
        uint32_t renderResolution = 2;
        std::vector<Renderer::MeshVertex> vertices;
        std::vector<uint32_t> indices;
        Renderer::Aabb bounds;
    };

    struct TerrainRenderMeshBuildResult {
        bool success = false;
        TerrainRenderMeshData mesh;
        float buildMs = 0.0f;
        std::string message;
    };
}
