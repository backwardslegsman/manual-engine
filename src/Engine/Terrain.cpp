#include "Engine/Terrain.hpp"

#include <algorithm>
#include <cmath>

#include "Renderer/VertexLayouts.hpp"

namespace {
    glm::vec3 terrainNormalFromHeights(float left, float right, float down, float up, float spacing)
    {
        return glm::normalize(glm::vec3{left - right, 2.0f * spacing, down - up});
    }
}

namespace Engine {
    TerrainSystem::TerrainSystem(TerrainSettings settings)
        : settings_(settings)
    {
        settings_.chunkSize = std::max(settings_.chunkSize, 1.0f);
        settings_.resolution = std::max(settings_.resolution, 2u);
    }

    TerrainTileHandle TerrainSystem::createTile(ChunkCoord coord, Renderer::TextureHandle baseColorTexture)
    {
        TerrainTile terrain;
        terrain.alive = true;
        terrain.coord = coord;
        terrain.origin = {
            static_cast<float>(coord.x) * settings_.chunkSize,
            0.0f,
            static_cast<float>(coord.z) * settings_.chunkSize,
        };
        terrain.size = settings_.chunkSize;
        terrain.resolution = settings_.resolution;
        terrain.heights.resize(static_cast<size_t>(settings_.resolution) * settings_.resolution);

        const float spacing = settings_.chunkSize / static_cast<float>(settings_.resolution - 1);
        for (uint32_t z = 0; z < settings_.resolution; ++z) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const float worldX = terrain.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = terrain.origin.z + static_cast<float>(z) * spacing;
                terrain.heights[static_cast<size_t>(z) * settings_.resolution + x] = generatedHeight(worldX, worldZ);
            }
        }

        std::vector<Renderer::MeshVertex> vertices;
        vertices.reserve(terrain.heights.size());
        for (uint32_t z = 0; z < settings_.resolution; ++z) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const float worldX = terrain.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = terrain.origin.z + static_cast<float>(z) * spacing;
                const float height = tileHeightAt(terrain, x, z);
                const float left = x > 0 ? tileHeightAt(terrain, x - 1, z) : generatedHeight(worldX - spacing, worldZ);
                const float right = x + 1 < settings_.resolution ? tileHeightAt(terrain, x + 1, z) : generatedHeight(worldX + spacing, worldZ);
                const float down = z > 0 ? tileHeightAt(terrain, x, z - 1) : generatedHeight(worldX, worldZ - spacing);
                const float up = z + 1 < settings_.resolution ? tileHeightAt(terrain, x, z + 1) : generatedHeight(worldX, worldZ + spacing);
                const glm::vec3 normal = terrainNormalFromHeights(left, right, down, up, spacing);

                vertices.push_back({
                    worldX,
                    height,
                    worldZ,
                    normal.x,
                    normal.y,
                    normal.z,
                    1.0f,
                    0.0f,
                    0.0f,
                    static_cast<float>(x) / static_cast<float>(settings_.resolution - 1),
                    static_cast<float>(z) / static_cast<float>(settings_.resolution - 1),
                });
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(settings_.resolution - 1) * (settings_.resolution - 1) * 6);
        for (uint32_t z = 0; z + 1 < settings_.resolution; ++z) {
            for (uint32_t x = 0; x + 1 < settings_.resolution; ++x) {
                const uint32_t topLeft = z * settings_.resolution + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = (z + 1) * settings_.resolution + x;
                const uint32_t bottomRight = bottomLeft + 1;
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }

        terrain.rendererTerrain = Renderer::createTerrainTile(vertices, indices, baseColorTexture);

        for (uint32_t index = 0; index < tiles_.size(); ++index) {
            if (!tiles_[index].alive) {
                tiles_[index] = std::move(terrain);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(tiles_.size());
        tiles_.push_back(std::move(terrain));
        return {id};
    }

    void TerrainSystem::destroyTile(TerrainTileHandle handle)
    {
        TerrainTile* terrain = tile(handle);
        if (!terrain) {
            return;
        }

        Renderer::destroyTerrainTile(terrain->rendererTerrain);
        *terrain = {};
    }

    std::optional<float> TerrainSystem::sampleHeight(float worldX, float worldZ) const
    {
        const TerrainTile* terrain = tileForCoord(coordForWorldPosition(worldX, worldZ));
        if (!terrain) {
            return std::nullopt;
        }

        const float localX = std::clamp(worldX - terrain->origin.x, 0.0f, terrain->size);
        const float localZ = std::clamp(worldZ - terrain->origin.z, 0.0f, terrain->size);
        const float normalizedX = localX / terrain->size * static_cast<float>(terrain->resolution - 1);
        const float normalizedZ = localZ / terrain->size * static_cast<float>(terrain->resolution - 1);
        const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), terrain->resolution - 1);
        const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), terrain->resolution - 1);
        const uint32_t x1 = std::min(x0 + 1, terrain->resolution - 1);
        const uint32_t z1 = std::min(z0 + 1, terrain->resolution - 1);
        const float tx = normalizedX - static_cast<float>(x0);
        const float tz = normalizedZ - static_cast<float>(z0);

        const float h00 = tileHeightAt(*terrain, x0, z0);
        const float h10 = tileHeightAt(*terrain, x1, z0);
        const float h01 = tileHeightAt(*terrain, x0, z1);
        const float h11 = tileHeightAt(*terrain, x1, z1);
        const float hx0 = h00 + (h10 - h00) * tx;
        const float hx1 = h01 + (h11 - h01) * tx;
        return hx0 + (hx1 - hx0) * tz;
    }

    float TerrainSystem::generatedHeight(float worldX, float worldZ) const
    {
        const float rolling = std::sin(worldX * 0.18f) * 0.65f + std::cos(worldZ * 0.15f) * 0.55f;
        const float detail = std::sin((worldX + worldZ) * 0.07f) * 0.35f;
        return (rolling + detail) * settings_.heightScale;
    }

    ChunkCoord TerrainSystem::coordForWorldPosition(float worldX, float worldZ) const
    {
        return {
            static_cast<int32_t>(std::floor(worldX / settings_.chunkSize)),
            static_cast<int32_t>(std::floor(worldZ / settings_.chunkSize)),
        };
    }

    const TerrainSettings& TerrainSystem::settings() const
    {
        return settings_;
    }

    TerrainTile* TerrainSystem::tile(TerrainTileHandle handle)
    {
        if (handle.id >= tiles_.size() || !tiles_[handle.id].alive) {
            return nullptr;
        }
        return &tiles_[handle.id];
    }

    const TerrainTile* TerrainSystem::tile(TerrainTileHandle handle) const
    {
        if (handle.id >= tiles_.size() || !tiles_[handle.id].alive) {
            return nullptr;
        }
        return &tiles_[handle.id];
    }

    const TerrainTile* TerrainSystem::tileForCoord(ChunkCoord coord) const
    {
        for (const TerrainTile& terrain : tiles_) {
            if (terrain.alive && terrain.coord == coord) {
                return &terrain;
            }
        }
        return nullptr;
    }

    float TerrainSystem::tileHeightAt(const TerrainTile& tile, uint32_t x, uint32_t z) const
    {
        return tile.heights[static_cast<size_t>(z) * tile.resolution + x];
    }
}
