#include "Engine/Terrain.hpp"

#include <algorithm>
#include <cmath>

#include "Renderer/VertexLayouts.hpp"

namespace {
    glm::vec3 terrainNormalFromHeights(float left, float right, float down, float up, float spacing)
    {
        return glm::normalize(glm::vec3{left - right, 2.0f * spacing, down - up});
    }

    void pushQuad(
        std::vector<uint32_t>& indices,
        uint32_t topLeft,
        uint32_t topRight,
        uint32_t bottomLeft,
        uint32_t bottomRight)
    {
        indices.push_back(topLeft);
        indices.push_back(bottomLeft);
        indices.push_back(topRight);
        indices.push_back(topRight);
        indices.push_back(bottomLeft);
        indices.push_back(bottomRight);
    }
}

namespace Engine {
    TerrainSystem::TerrainSystem(TerrainSettings settings)
        : settings_(settings)
    {
        settings_.chunkSize = std::max(settings_.chunkSize, 1.0f);
        settings_.resolution = std::max(settings_.resolution, 2u);
        settings_.skirtDepth = std::max(settings_.skirtDepth, 0.0f);
        for (uint32_t index = 0; index < settings_.lodLevels.size(); ++index) {
            TerrainLodLevel& level = settings_.lodLevels[index];
            level.resolution = std::max(level.resolution, 2u);
            level.startDistance = std::max(level.startDistance, 0.0f);
            if (index > 0) {
                level.startDistance = std::max(level.startDistance, settings_.lodLevels[index - 1].startDistance);
            }
        }
    }

    TerrainTileHandle TerrainSystem::createTile(ChunkCoord coord, Renderer::MaterialHandle material)
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
        terrain.material = material;
        terrain.biome = sampleChunkBiome(coord);
        terrain.heights.resize(static_cast<size_t>(settings_.resolution) * settings_.resolution);

        const float spacing = settings_.chunkSize / static_cast<float>(settings_.resolution - 1);
        for (uint32_t z = 0; z < settings_.resolution; ++z) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const float worldX = terrain.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = terrain.origin.z + static_cast<float>(z) * spacing;
                terrain.heights[static_cast<size_t>(z) * settings_.resolution + x] = generatedHeight(worldX, worldZ);
            }
        }

        terrain.currentLod = 0;
        if (settings_.createRendererResources) {
            terrain.rendererTerrain = createRendererTerrain(terrain, terrain.currentLod);
        }

        return storeTile(std::move(terrain));
    }

    TerrainTileHandle TerrainSystem::createTileFromHeights(
        ChunkCoord coord,
        std::span<const float> heights,
        Renderer::MaterialHandle material)
    {
        const size_t expectedHeightCount = static_cast<size_t>(settings_.resolution) * settings_.resolution;
        if (heights.size() != expectedHeightCount) {
            return {};
        }

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
        terrain.material = material;
        terrain.biome = sampleChunkBiome(coord);
        terrain.heights.assign(heights.begin(), heights.end());
        terrain.currentLod = 0;
        if (settings_.createRendererResources) {
            terrain.rendererTerrain = createRendererTerrain(terrain, terrain.currentLod);
        }

        return storeTile(std::move(terrain));
    }

    TerrainTileHandle TerrainSystem::storeTile(TerrainTile terrain)
    {
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

        if (settings_.createRendererResources) {
            Renderer::destroyTerrainTile(terrain->rendererTerrain);
        }
        *terrain = {};
    }

    Renderer::TerrainHandle TerrainSystem::rendererTerrain(TerrainTileHandle handle) const
    {
        const TerrainTile* terrain = tile(handle);
        return terrain ? terrain->rendererTerrain : Renderer::TerrainHandle{};
    }

    bool TerrainSystem::updateLods(const glm::vec3& cameraPosition)
    {
        bool rebuilt = false;
        for (TerrainTile& terrain : tiles_) {
            if (!terrain.alive) {
                continue;
            }

            const uint32_t desiredLod = chooseLod(terrain, cameraPosition);
            if (desiredLod == terrain.currentLod) {
                continue;
            }

            if (!settings_.createRendererResources) {
                terrain.currentLod = desiredLod;
                rebuilt = true;
                continue;
            }
            Renderer::destroyTerrainTile(terrain.rendererTerrain);
            terrain.currentLod = desiredLod;
            terrain.rendererTerrain = createRendererTerrain(terrain, terrain.currentLod);
            rebuilt = true;
        }
        return rebuilt;
    }

    std::array<uint32_t, TerrainLodLevelCount> TerrainSystem::lodCounts() const
    {
        std::array<uint32_t, TerrainLodLevelCount> counts{};
        for (const TerrainTile& terrain : tiles_) {
            if (terrain.alive && terrain.currentLod < counts.size()) {
                ++counts[terrain.currentLod];
            }
        }
        return counts;
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

    std::optional<TerrainPickHit> TerrainSystem::raycast(
        const Ray& ray,
        float maxDistance,
        float stepDistance,
        uint32_t refinementIterations) const
    {
        if (maxDistance <= 0.0f || stepDistance <= 0.0f || glm::length(ray.direction) <= 0.0f) {
            return std::nullopt;
        }

        const glm::vec3 direction = glm::normalize(ray.direction);
        bool hasPreviousSample = false;
        float previousDistance = 0.0f;
        float previousSignedHeight = 0.0f;

        for (float distance = 0.0f; distance <= maxDistance; distance += stepDistance) {
            const glm::vec3 point = ray.origin + direction * distance;
            const std::optional<float> terrainHeight = sampleHeight(point.x, point.z);
            if (!terrainHeight) {
                hasPreviousSample = false;
                continue;
            }

            const float signedHeight = point.y - *terrainHeight;
            if (hasPreviousSample &&
                ((previousSignedHeight >= 0.0f && signedHeight <= 0.0f) ||
                    (previousSignedHeight <= 0.0f && signedHeight >= 0.0f))) {
                float low = previousDistance;
                float high = distance;
                for (uint32_t iteration = 0; iteration < refinementIterations; ++iteration) {
                    const float mid = (low + high) * 0.5f;
                    const glm::vec3 midPoint = ray.origin + direction * mid;
                    const std::optional<float> midHeight = sampleHeight(midPoint.x, midPoint.z);
                    if (!midHeight) {
                        low = mid;
                        continue;
                    }

                    const float midSignedHeight = midPoint.y - *midHeight;
                    if ((previousSignedHeight >= 0.0f && midSignedHeight >= 0.0f) ||
                        (previousSignedHeight <= 0.0f && midSignedHeight <= 0.0f)) {
                        low = mid;
                    } else {
                        high = mid;
                    }
                }

                const float hitDistance = (low + high) * 0.5f;
                glm::vec3 hitPoint = ray.origin + direction * hitDistance;
                const std::optional<float> hitHeight = sampleHeight(hitPoint.x, hitPoint.z);
                if (!hitHeight) {
                    return std::nullopt;
                }
                hitPoint.y = *hitHeight;
                return TerrainPickHit{
                    hitPoint,
                    hitDistance,
                    coordForWorldPosition(hitPoint.x, hitPoint.z),
                };
            }

            hasPreviousSample = true;
            previousDistance = distance;
            previousSignedHeight = signedHeight;
        }

        return std::nullopt;
    }

    float TerrainSystem::generatedHeight(float worldX, float worldZ) const
    {
        const BiomeSample sample = sampleBiome(worldX, worldZ);
        const BiomeDescriptor* biome = settings_.biomes ? settings_.biomes->descriptor(sample.id) : nullptr;
        const float heightScale = biome ? biome->heightScale : 1.0f;
        const float rollingScale = biome ? biome->rollingScale : 1.0f;
        const float detailScale = biome ? biome->detailScale : 1.0f;
        const float rolling = (std::sin(worldX * 0.18f) * 0.65f + std::cos(worldZ * 0.15f) * 0.55f) * rollingScale;
        const float detail = std::sin((worldX + worldZ) * 0.07f) * 0.35f * detailScale;
        return (rolling + detail) * settings_.heightScale * heightScale;
    }

    BiomeSample TerrainSystem::sampleBiome(float worldX, float worldZ) const
    {
        return settings_.biomes ? settings_.biomes->sample(worldX, worldZ) : BiomeSystem::sampleDefaults().sample(worldX, worldZ);
    }

    BiomeSample TerrainSystem::sampleChunkBiome(ChunkCoord coord) const
    {
        return settings_.biomes ? settings_.biomes->sampleChunk(coord, settings_.chunkSize) : BiomeSystem::sampleDefaults().sampleChunk(coord, settings_.chunkSize);
    }

    std::optional<BiomeSample> TerrainSystem::tileBiome(TerrainTileHandle handle) const
    {
        const TerrainTile* terrain = tile(handle);
        if (!terrain) {
            return std::nullopt;
        }
        return terrain->biome;
    }

    std::optional<ChunkCoord> TerrainSystem::tileCoord(TerrainTileHandle handle) const
    {
        const TerrainTile* terrain = tile(handle);
        if (!terrain) {
            return std::nullopt;
        }
        return terrain->coord;
    }

    std::optional<Renderer::Aabb> TerrainSystem::tileWorldBounds(TerrainTileHandle handle) const
    {
        const TerrainTile* terrain = tile(handle);
        if (!terrain || terrain->heights.empty()) {
            return std::nullopt;
        }

        const auto [minIt, maxIt] = std::minmax_element(terrain->heights.begin(), terrain->heights.end());
        return Renderer::Aabb{
            {terrain->origin.x, *minIt, terrain->origin.z},
            {terrain->origin.x + terrain->size, *maxIt, terrain->origin.z + terrain->size},
        };
    }

    std::optional<NavigationTerrainBuildData> TerrainSystem::navigationBuildData(TerrainTileHandle handle) const
    {
        const TerrainTile* terrain = tile(handle);
        const std::optional<Renderer::Aabb> bounds = tileWorldBounds(handle);
        if (!terrain || !bounds || terrain->heights.empty() || terrain->resolution < 2) {
            return std::nullopt;
        }

        NavigationTerrainBuildData buildData;
        buildData.coord = terrain->coord;
        buildData.bounds = *bounds;
        buildData.vertices.reserve(static_cast<size_t>(terrain->resolution) * terrain->resolution);
        buildData.indices.reserve(static_cast<size_t>(terrain->resolution - 1) * (terrain->resolution - 1) * 6);

        const float spacing = terrain->size / static_cast<float>(terrain->resolution - 1);
        for (uint32_t z = 0; z < terrain->resolution; ++z) {
            for (uint32_t x = 0; x < terrain->resolution; ++x) {
                buildData.vertices.push_back({
                    terrain->origin.x + static_cast<float>(x) * spacing,
                    tileHeightAt(*terrain, x, z),
                    terrain->origin.z + static_cast<float>(z) * spacing,
                });
            }
        }

        for (uint32_t z = 0; z + 1 < terrain->resolution; ++z) {
            for (uint32_t x = 0; x + 1 < terrain->resolution; ++x) {
                const uint32_t topLeft = z * terrain->resolution + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = (z + 1) * terrain->resolution + x;
                const uint32_t bottomRight = bottomLeft + 1;
                buildData.indices.push_back(topLeft);
                buildData.indices.push_back(bottomLeft);
                buildData.indices.push_back(topRight);
                buildData.indices.push_back(topRight);
                buildData.indices.push_back(bottomLeft);
                buildData.indices.push_back(bottomRight);
            }
        }

        return buildData;
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

    uint32_t TerrainSystem::chooseLod(const TerrainTile& terrain, const glm::vec3& cameraPosition) const
    {
        const glm::vec3 tileCenter{
            terrain.origin.x + terrain.size * 0.5f,
            0.0f,
            terrain.origin.z + terrain.size * 0.5f,
        };
        const glm::vec3 offset = cameraPosition - tileCenter;
        const float distance = std::sqrt(glm::dot(offset, offset));

        uint32_t chosenLod = 0;
        for (uint32_t index = 0; index < settings_.lodLevels.size(); ++index) {
            if (distance >= settings_.lodLevels[index].startDistance) {
                chosenLod = index;
            }
        }
        return chosenLod;
    }

    Renderer::TerrainHandle TerrainSystem::createRendererTerrain(const TerrainTile& tile, uint32_t lodIndex) const
    {
        const uint32_t lod = std::min<uint32_t>(lodIndex, static_cast<uint32_t>(settings_.lodLevels.size() - 1));
        const uint32_t resolution = std::max(settings_.lodLevels[lod].resolution, 2u);
        const float spacing = tile.size / static_cast<float>(resolution - 1);

        std::vector<Renderer::MeshVertex> vertices;
        vertices.reserve(static_cast<size_t>(resolution) * resolution + static_cast<size_t>(resolution) * 8);
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = tile.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = tile.origin.z + static_cast<float>(z) * spacing;
                const float height = generatedHeight(worldX, worldZ);
                const float left = generatedHeight(worldX - spacing, worldZ);
                const float right = generatedHeight(worldX + spacing, worldZ);
                const float down = generatedHeight(worldX, worldZ - spacing);
                const float up = generatedHeight(worldX, worldZ + spacing);
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
                    static_cast<float>(x) / static_cast<float>(resolution - 1),
                    static_cast<float>(z) / static_cast<float>(resolution - 1),
                });
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6 + static_cast<size_t>(resolution - 1) * 24);
        for (uint32_t z = 0; z + 1 < resolution; ++z) {
            for (uint32_t x = 0; x + 1 < resolution; ++x) {
                const uint32_t topLeft = z * resolution + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = (z + 1) * resolution + x;
                const uint32_t bottomRight = bottomLeft + 1;
                pushQuad(indices, topLeft, topRight, bottomLeft, bottomRight);
            }
        }

        const auto addSkirtVertex = [&](uint32_t sourceIndex) {
            Renderer::MeshVertex vertex = vertices[sourceIndex];
            vertex.py -= settings_.skirtDepth;
            vertices.push_back(vertex);
            return static_cast<uint32_t>(vertices.size() - 1);
        };

        if (settings_.skirtDepth > 0.0f) {
            for (uint32_t x = 0; x + 1 < resolution; ++x) {
                const uint32_t topA = x;
                const uint32_t topB = x + 1;
                const uint32_t topSkirtA = addSkirtVertex(topA);
                const uint32_t topSkirtB = addSkirtVertex(topB);
                pushQuad(indices, topA, topB, topSkirtA, topSkirtB);

                const uint32_t bottomA = (resolution - 1) * resolution + x;
                const uint32_t bottomB = bottomA + 1;
                const uint32_t bottomSkirtB = addSkirtVertex(bottomB);
                const uint32_t bottomSkirtA = addSkirtVertex(bottomA);
                pushQuad(indices, bottomB, bottomA, bottomSkirtB, bottomSkirtA);
            }

            for (uint32_t z = 0; z + 1 < resolution; ++z) {
                const uint32_t leftA = z * resolution;
                const uint32_t leftB = (z + 1) * resolution;
                const uint32_t leftSkirtB = addSkirtVertex(leftB);
                const uint32_t leftSkirtA = addSkirtVertex(leftA);
                pushQuad(indices, leftB, leftA, leftSkirtB, leftSkirtA);

                const uint32_t rightA = z * resolution + (resolution - 1);
                const uint32_t rightB = (z + 1) * resolution + (resolution - 1);
                const uint32_t rightSkirtA = addSkirtVertex(rightA);
                const uint32_t rightSkirtB = addSkirtVertex(rightB);
                pushQuad(indices, rightA, rightB, rightSkirtA, rightSkirtB);
            }
        }

        return Renderer::createTerrainTile(vertices, indices, tile.material);
    }
}
