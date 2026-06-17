#include "Engine/Terrain.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <ranges>

#include "Renderer/VertexLayouts.hpp"

namespace {
    glm::vec3 terrainNormalFromHeights(float left, float right, float down, float up, float spacing)
    {
        return glm::normalize(glm::vec3{left - right, 2.0f * spacing, down - up});
    }

    float slopeDegreesFromNormal(const glm::vec3& normal)
    {
        const float y = std::clamp(normal.y, -1.0f, 1.0f);
        return glm::degrees(std::acos(y));
    }

    float triangleSlopeDegrees(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        return slopeDegreesFromNormal(normal.y < 0.0f ? -normal : normal);
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

    float sampleHeightFromGrid(
        const glm::vec3& origin,
        float size,
        uint32_t resolution,
        const std::vector<float>& heights,
        float worldX,
        float worldZ)
    {
        if (resolution < 2 || size <= 0.0f || heights.size() != static_cast<size_t>(resolution) * resolution) {
            return 0.0f;
        }

        const float localX = std::clamp(worldX - origin.x, 0.0f, size);
        const float localZ = std::clamp(worldZ - origin.z, 0.0f, size);
        const float normalizedX = localX / size * static_cast<float>(resolution - 1);
        const float normalizedZ = localZ / size * static_cast<float>(resolution - 1);
        const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), resolution - 1);
        const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), resolution - 1);
        const uint32_t x1 = std::min(x0 + 1, resolution - 1);
        const uint32_t z1 = std::min(z0 + 1, resolution - 1);
        const float tx = normalizedX - static_cast<float>(x0);
        const float tz = normalizedZ - static_cast<float>(z0);
        const auto heightAt = [&](uint32_t x, uint32_t z) {
            return heights[static_cast<size_t>(z) * resolution + x];
        };

        const float h00 = heightAt(x0, z0);
        const float h10 = heightAt(x1, z0);
        const float h01 = heightAt(x0, z1);
        const float h11 = heightAt(x1, z1);
        const float hx0 = h00 + (h10 - h00) * tx;
        const float hx1 = h01 + (h11 - h01) * tx;
        return hx0 + (hx1 - hx0) * tz;
    }
}

namespace Engine {
    TerrainSystem::TerrainSystem(TerrainSettings settings)
        : settings_(settings)
    {
        settings_.chunkSize = std::max(settings_.chunkSize, 1.0f);
        settings_.resolution = std::max(settings_.resolution, 2u);
        settings_.navigationResolution = std::max(settings_.navigationResolution, 2u);
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
        return createTileFromGenerated(generateTileData(coord), material);
    }

    GeneratedTerrainTileData TerrainSystem::generateTileData(ChunkCoord coord) const
    {
        GeneratedTerrainTileData generated;
        generated.coord = coord;
        generated.origin = {
            static_cast<float>(coord.x) * settings_.chunkSize,
            0.0f,
            static_cast<float>(coord.z) * settings_.chunkSize,
        };
        generated.size = settings_.chunkSize;
        generated.resolution = settings_.resolution;
        generated.biome = sampleChunkBiome(coord);
        generated.heights.resize(static_cast<size_t>(settings_.resolution) * settings_.resolution);

        const float spacing = settings_.chunkSize / static_cast<float>(settings_.resolution - 1);
        for (uint32_t z = 0; z < settings_.resolution; ++z) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const float worldX = generated.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = generated.origin.z + static_cast<float>(z) * spacing;
                generated.heights[static_cast<size_t>(z) * settings_.resolution + x] = generatedHeight(worldX, worldZ);
            }
        }

        return generated;
    }

    TerrainTileHandle TerrainSystem::createTileFromGenerated(
        const GeneratedTerrainTileData& generated,
        Renderer::MaterialHandle material)
    {
        const TerrainTileHandle handle = createCpuTileFromGenerated(generated, material);
        ensureRendererTerrain(handle);
        return handle;
    }

    TerrainTileHandle TerrainSystem::createCpuTileFromGenerated(
        const GeneratedTerrainTileData& generated,
        Renderer::MaterialHandle material)
    {
        TerrainTile terrain;
        terrain.alive = true;
        terrain.coord = generated.coord;
        terrain.origin = generated.origin;
        terrain.size = generated.size;
        terrain.resolution = generated.resolution;
        terrain.generation = nextTileGeneration_++;
        terrain.material = material;
        terrain.biome = generated.biome;
        terrain.heights = generated.heights;
        if (terrain.resolution < 2 ||
            terrain.heights.size() != static_cast<size_t>(terrain.resolution) * terrain.resolution ||
            terrain.size <= 0.0f) {
            return {};
        }

        terrain.currentLod = 0;
        return storeTile(std::move(terrain));
    }

    Renderer::TerrainHandle TerrainSystem::ensureRendererTerrain(TerrainTileHandle handle)
    {
        TerrainTile* terrain = tile(handle);
        if (!terrain) {
            return {};
        }
        if (!settings_.createRendererResources) {
            return terrain->rendererTerrain;
        }
        if (terrain->rendererTerrain.id != UINT32_MAX) {
            return terrain->rendererTerrain;
        }
        terrain->rendererTerrain = createRendererTerrain(*terrain, terrain->currentLod);
        return terrain->rendererTerrain;
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
        terrain.generation = nextTileGeneration_++;
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
        return updateLodsBudgeted(cameraPosition, UINT32_MAX).rebuiltAny();
    }

    TerrainLodUpdateResult TerrainSystem::updateLodsBudgeted(const glm::vec3& cameraPosition, uint32_t maxRebuilds)
    {
        TerrainLodUpdateResult result;
        for (TerrainTile& terrain : tiles_) {
            if (!terrain.alive) {
                continue;
            }

            const uint32_t desiredLod = chooseLod(terrain, cameraPosition);
            if (desiredLod == terrain.currentLod) {
                continue;
            }

            if (result.rebuiltCount >= maxRebuilds) {
                ++result.pendingCount;
                continue;
            }

            if (!settings_.createRendererResources) {
                terrain.currentLod = desiredLod;
                ++result.rebuiltCount;
                continue;
            }
            Renderer::destroyTerrainTile(terrain.rendererTerrain);
            terrain.currentLod = desiredLod;
            terrain.rendererTerrain = createRendererTerrain(terrain, terrain.currentLod);
            ++result.rebuiltCount;
        }
        return result;
    }

    std::vector<TerrainRenderMeshBuildInput> TerrainSystem::collectRenderLodBuildInputs(
        const glm::vec3& cameraPosition,
        uint32_t maxBuilds,
        std::span<const TerrainTileHandle> pendingTiles) const
    {
        std::vector<TerrainRenderMeshBuildInput> inputs;
        for (uint32_t index = 0; index < tiles_.size(); ++index) {
            const TerrainTile& terrain = tiles_[index];
            if (!terrain.alive) {
                continue;
            }

            const TerrainTileHandle handle{index};
            const bool alreadyPending = std::ranges::any_of(pendingTiles, [handle](TerrainTileHandle pending) {
                return pending.id == handle.id;
            });
            if (alreadyPending) {
                continue;
            }

            const uint32_t desiredLod = chooseLod(terrain, cameraPosition);
            if (desiredLod == terrain.currentLod) {
                continue;
            }

            if (inputs.size() >= maxBuilds) {
                break;
            }

            if (std::optional<TerrainRenderMeshBuildInput> input = renderMeshBuildInput(handle, desiredLod)) {
                inputs.push_back(std::move(*input));
            }
        }
        return inputs;
    }

    std::optional<TerrainRenderMeshBuildInput> TerrainSystem::renderMeshBuildInput(
        TerrainTileHandle handle,
        uint32_t lodIndex) const
    {
        const TerrainTile* terrain = tile(handle);
        if (!terrain || terrain->resolution < 2 || terrain->heights.empty()) {
            return std::nullopt;
        }

        const uint32_t lod = std::min<uint32_t>(lodIndex, static_cast<uint32_t>(settings_.lodLevels.size() - 1));
        TerrainRenderMeshBuildInput input;
        input.tile = handle;
        input.coord = terrain->coord;
        input.generation = terrain->generation;
        input.lodIndex = lod;
        input.renderResolution = std::max(settings_.lodLevels[lod].resolution, 2u);
        input.origin = terrain->origin;
        input.size = terrain->size;
        input.cpuResolution = terrain->resolution;
        input.heights = terrain->heights;
        input.skirtDepth = settings_.skirtDepth;
        return input;
    }

    TerrainRenderMeshBuildResult TerrainSystem::buildRenderMeshData(const TerrainRenderMeshBuildInput& input)
    {
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();

        TerrainRenderMeshBuildResult result;
        result.mesh.tile = input.tile;
        result.mesh.coord = input.coord;
        result.mesh.generation = input.generation;
        result.mesh.lodIndex = input.lodIndex;
        result.mesh.renderResolution = std::max(input.renderResolution, 2u);
        if (input.cpuResolution < 2 ||
            input.size <= 0.0f ||
            input.heights.size() != static_cast<size_t>(input.cpuResolution) * input.cpuResolution) {
            result.message = "Invalid terrain render mesh build input.";
            return result;
        }

        const uint32_t resolution = result.mesh.renderResolution;
        const float spacing = input.size / static_cast<float>(resolution - 1);
        result.mesh.vertices.reserve(static_cast<size_t>(resolution) * resolution + static_cast<size_t>(resolution) * 8);
        result.mesh.bounds.min = {input.origin.x, std::numeric_limits<float>::max(), input.origin.z};
        result.mesh.bounds.max = {input.origin.x + input.size, std::numeric_limits<float>::lowest(), input.origin.z + input.size};

        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = input.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = input.origin.z + static_cast<float>(z) * spacing;
                const float height = sampleHeightFromGrid(input.origin, input.size, input.cpuResolution, input.heights, worldX, worldZ);
                const float left = sampleHeightFromGrid(input.origin, input.size, input.cpuResolution, input.heights, worldX - spacing, worldZ);
                const float right = sampleHeightFromGrid(input.origin, input.size, input.cpuResolution, input.heights, worldX + spacing, worldZ);
                const float down = sampleHeightFromGrid(input.origin, input.size, input.cpuResolution, input.heights, worldX, worldZ - spacing);
                const float up = sampleHeightFromGrid(input.origin, input.size, input.cpuResolution, input.heights, worldX, worldZ + spacing);
                const glm::vec3 normal = terrainNormalFromHeights(left, right, down, up, spacing);
                result.mesh.bounds.min.y = std::min(result.mesh.bounds.min.y, height - input.skirtDepth);
                result.mesh.bounds.max.y = std::max(result.mesh.bounds.max.y, height);

                result.mesh.vertices.push_back({
                    worldX,
                    height,
                    worldZ,
                    normal.x,
                    normal.y,
                    normal.z,
                    1.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    static_cast<float>(x) / static_cast<float>(resolution - 1),
                    static_cast<float>(z) / static_cast<float>(resolution - 1),
                    static_cast<float>(x) / static_cast<float>(resolution - 1),
                    static_cast<float>(z) / static_cast<float>(resolution - 1),
                    0xffffffff,
                });
            }
        }

        result.mesh.indices.reserve(
            static_cast<size_t>(resolution - 1) * (resolution - 1) * 6 +
            static_cast<size_t>(resolution - 1) * 24);
        for (uint32_t z = 0; z + 1 < resolution; ++z) {
            for (uint32_t x = 0; x + 1 < resolution; ++x) {
                const uint32_t topLeft = z * resolution + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = (z + 1) * resolution + x;
                const uint32_t bottomRight = bottomLeft + 1;
                pushQuad(result.mesh.indices, topLeft, topRight, bottomLeft, bottomRight);
            }
        }

        const auto addSkirtVertex = [&](uint32_t sourceIndex) {
            Renderer::MeshVertex vertex = result.mesh.vertices[sourceIndex];
            vertex.py -= input.skirtDepth;
            result.mesh.vertices.push_back(vertex);
            return static_cast<uint32_t>(result.mesh.vertices.size() - 1);
        };

        if (input.skirtDepth > 0.0f) {
            for (uint32_t x = 0; x + 1 < resolution; ++x) {
                const uint32_t topA = x;
                const uint32_t topB = x + 1;
                const uint32_t topSkirtA = addSkirtVertex(topA);
                const uint32_t topSkirtB = addSkirtVertex(topB);
                pushQuad(result.mesh.indices, topA, topB, topSkirtA, topSkirtB);

                const uint32_t bottomA = (resolution - 1) * resolution + x;
                const uint32_t bottomB = bottomA + 1;
                const uint32_t bottomSkirtB = addSkirtVertex(bottomB);
                const uint32_t bottomSkirtA = addSkirtVertex(bottomA);
                pushQuad(result.mesh.indices, bottomB, bottomA, bottomSkirtB, bottomSkirtA);
            }

            for (uint32_t z = 0; z + 1 < resolution; ++z) {
                const uint32_t leftA = z * resolution;
                const uint32_t leftB = (z + 1) * resolution;
                const uint32_t leftSkirtB = addSkirtVertex(leftB);
                const uint32_t leftSkirtA = addSkirtVertex(leftA);
                pushQuad(result.mesh.indices, leftB, leftA, leftSkirtB, leftSkirtA);

                const uint32_t rightA = z * resolution + (resolution - 1);
                const uint32_t rightB = (z + 1) * resolution + (resolution - 1);
                const uint32_t rightSkirtA = addSkirtVertex(rightA);
                const uint32_t rightSkirtB = addSkirtVertex(rightB);
                pushQuad(result.mesh.indices, rightA, rightB, rightSkirtA, rightSkirtB);
            }
        }

        result.success = !result.mesh.vertices.empty() && !result.mesh.indices.empty();
        result.message = result.success ? "Terrain render mesh built." : "Terrain render mesh build produced no geometry.";
        result.buildMs = std::chrono::duration<float, std::milli>(Clock::now() - started).count();
        return result;
    }

    bool TerrainSystem::commitRendererMesh(const TerrainRenderMeshData& mesh)
    {
        TerrainTile* terrain = tile(mesh.tile);
        if (!terrain ||
            terrain->generation != mesh.generation ||
            !settings_.createRendererResources ||
            mesh.vertices.empty() ||
            mesh.indices.empty()) {
            return false;
        }

        Renderer::destroyTerrainTile(terrain->rendererTerrain);
        terrain->currentLod = mesh.lodIndex;
        terrain->rendererTerrain = Renderer::createTerrainTile(mesh.vertices, mesh.indices, terrain->material);
        return terrain->rendererTerrain.id != UINT32_MAX;
    }

    uint64_t TerrainSystem::tileGeneration(TerrainTileHandle handle) const
    {
        const TerrainTile* terrain = tile(handle);
        return terrain ? terrain->generation : 0;
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
        const float baseHeight = biome ? biome->baseHeight : 0.0f;
        const float rollingAmplitude = biome ? biome->rollingAmplitude * biome->rollingScale : 1.0f;
        const float rollingFrequencyX = biome ? biome->rollingFrequencyX : 0.18f;
        const float rollingFrequencyZ = biome ? biome->rollingFrequencyZ : 0.15f;
        const float detailAmplitude = biome ? biome->detailAmplitude * biome->detailScale : 0.35f;
        const float detailFrequency = biome ? biome->detailFrequency : 0.07f;
        const float rolling =
            std::sin(worldX * rollingFrequencyX) * rollingAmplitude +
            std::cos(worldZ * rollingFrequencyZ) * rollingAmplitude;
        const float detail = std::sin((worldX + worldZ) * detailFrequency) * detailAmplitude;
        return baseHeight + (rolling + detail) * settings_.heightScale * heightScale;
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

    std::optional<TerrainTileDiagnostics> TerrainSystem::tileDiagnostics(
        TerrainTileHandle handle,
        const NavAgentSettings* agent) const
    {
        const TerrainTile* terrain = tile(handle);
        if (!terrain || terrain->heights.empty() || terrain->resolution < 2) {
            return std::nullopt;
        }

        TerrainTileDiagnostics diagnostics;
        diagnostics.coord = terrain->coord;
        diagnostics.biomeId = terrain->biome.id;
        diagnostics.resolution = terrain->resolution;
        diagnostics.chunkSize = terrain->size;

        const auto [minIt, maxIt] = std::minmax_element(terrain->heights.begin(), terrain->heights.end());
        diagnostics.minHeight = *minIt;
        diagnostics.maxHeight = *maxIt;
        diagnostics.averageHeight =
            std::accumulate(terrain->heights.begin(), terrain->heights.end(), 0.0f) /
            static_cast<float>(terrain->heights.size());

        const float spacing = terrain->size / static_cast<float>(terrain->resolution - 1);
        float slopeSum = 0.0f;
        uint32_t slopeCount = 0;
        uint32_t walkableTriangles = 0;
        uint32_t totalTriangles = 0;
        const float maxWalkableSlope = agent ? std::clamp(agent->maxSlopeDegrees, 0.0f, 89.0f) : 89.0f;

        for (uint32_t z = 0; z < terrain->resolution; ++z) {
            for (uint32_t x = 0; x < terrain->resolution; ++x) {
                const float left = tileHeightAt(*terrain, x > 0 ? x - 1 : x, z);
                const float right = tileHeightAt(*terrain, std::min(x + 1, terrain->resolution - 1), z);
                const float down = tileHeightAt(*terrain, x, z > 0 ? z - 1 : z);
                const float up = tileHeightAt(*terrain, x, std::min(z + 1, terrain->resolution - 1));
                const float slope = slopeDegreesFromNormal(terrainNormalFromHeights(left, right, down, up, spacing));
                diagnostics.maxSlopeDegrees = std::max(diagnostics.maxSlopeDegrees, slope);
                slopeSum += slope;
                ++slopeCount;
            }
        }

        for (uint32_t z = 0; z + 1 < terrain->resolution; ++z) {
            for (uint32_t x = 0; x + 1 < terrain->resolution; ++x) {
                const glm::vec3 topLeft{
                    terrain->origin.x + static_cast<float>(x) * spacing,
                    tileHeightAt(*terrain, x, z),
                    terrain->origin.z + static_cast<float>(z) * spacing,
                };
                const glm::vec3 topRight{topLeft.x + spacing, tileHeightAt(*terrain, x + 1, z), topLeft.z};
                const glm::vec3 bottomLeft{topLeft.x, tileHeightAt(*terrain, x, z + 1), topLeft.z + spacing};
                const glm::vec3 bottomRight{topLeft.x + spacing, tileHeightAt(*terrain, x + 1, z + 1), topLeft.z + spacing};
                const float slopeA = triangleSlopeDegrees(topLeft, bottomLeft, topRight);
                const float slopeB = triangleSlopeDegrees(topRight, bottomLeft, bottomRight);
                walkableTriangles += slopeA <= maxWalkableSlope ? 1u : 0u;
                walkableTriangles += slopeB <= maxWalkableSlope ? 1u : 0u;
                totalTriangles += 2;
            }
        }

        diagnostics.averageSlopeDegrees = slopeCount > 0 ? slopeSum / static_cast<float>(slopeCount) : 0.0f;
        diagnostics.navWalkableTrianglePercent = totalTriangles > 0
            ? static_cast<float>(walkableTriangles) / static_cast<float>(totalTriangles) * 100.0f
            : 100.0f;
        return diagnostics;
    }

    std::vector<glm::vec3> TerrainSystem::slopeWarningSamples(
        TerrainTileHandle handle,
        float maxSlopeDegrees,
        uint32_t maxSamples) const
    {
        std::vector<glm::vec3> samples;
        const TerrainTile* terrain = tile(handle);
        if (!terrain || terrain->heights.empty() || terrain->resolution < 2 || maxSamples == 0) {
            return samples;
        }

        const float spacing = terrain->size / static_cast<float>(terrain->resolution - 1);
        const float threshold = std::clamp(maxSlopeDegrees, 0.0f, 89.0f);
        const uint32_t stride = std::max(1u, terrain->resolution / 16u);
        for (uint32_t z = 0; z < terrain->resolution && samples.size() < maxSamples; z += stride) {
            for (uint32_t x = 0; x < terrain->resolution && samples.size() < maxSamples; x += stride) {
                const float left = tileHeightAt(*terrain, x > 0 ? x - 1 : x, z);
                const float right = tileHeightAt(*terrain, std::min(x + 1, terrain->resolution - 1), z);
                const float down = tileHeightAt(*terrain, x, z > 0 ? z - 1 : z);
                const float up = tileHeightAt(*terrain, x, std::min(z + 1, terrain->resolution - 1));
                const float slope = slopeDegreesFromNormal(terrainNormalFromHeights(left, right, down, up, spacing));
                if (slope <= threshold) {
                    continue;
                }
                samples.push_back({
                    terrain->origin.x + static_cast<float>(x) * spacing,
                    tileHeightAt(*terrain, x, z),
                    terrain->origin.z + static_cast<float>(z) * spacing,
                });
            }
        }
        return samples;
    }

    std::optional<NavigationTerrainBuildData> TerrainSystem::navigationBuildData(TerrainTileHandle handle) const
    {
        return navigationBuildData(handle, settings_.navigationResolution);
    }

    std::optional<NavigationTerrainBuildData> TerrainSystem::navigationBuildData(
        TerrainTileHandle handle,
        uint32_t navigationResolution) const
    {
        const TerrainTile* terrain = tile(handle);
        const std::optional<Renderer::Aabb> bounds = tileWorldBounds(handle);
        if (!terrain || !bounds || terrain->heights.empty() || terrain->resolution < 2) {
            return std::nullopt;
        }

        const uint32_t resolution = std::max(navigationResolution, 2u);
        NavigationTerrainBuildData buildData;
        buildData.coord = terrain->coord;
        buildData.bounds = *bounds;
        buildData.vertices.reserve(static_cast<size_t>(resolution) * resolution);
        buildData.indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6);

        const float spacing = terrain->size / static_cast<float>(resolution - 1);
        const auto sampleTileHeight = [&](float worldX, float worldZ) {
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
        };
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = terrain->origin.x + static_cast<float>(x) * spacing;
                const float worldZ = terrain->origin.z + static_cast<float>(z) * spacing;
                buildData.vertices.push_back({
                    worldX,
                    sampleTileHeight(worldX, worldZ),
                    worldZ,
                });
            }
        }

        for (uint32_t z = 0; z + 1 < resolution; ++z) {
            for (uint32_t x = 0; x + 1 < resolution; ++x) {
                const uint32_t topLeft = z * resolution + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = (z + 1) * resolution + x;
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

    std::optional<float> TerrainSystem::sampleGeneratedHeight(
        const GeneratedTerrainTileData& generated,
        float worldX,
        float worldZ)
    {
        if (generated.resolution < 2 ||
            generated.size <= 0.0f ||
            generated.heights.size() != static_cast<size_t>(generated.resolution) * generated.resolution) {
            return std::nullopt;
        }

        const float localX = std::clamp(worldX - generated.origin.x, 0.0f, generated.size);
        const float localZ = std::clamp(worldZ - generated.origin.z, 0.0f, generated.size);
        const float normalizedX = localX / generated.size * static_cast<float>(generated.resolution - 1);
        const float normalizedZ = localZ / generated.size * static_cast<float>(generated.resolution - 1);
        const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), generated.resolution - 1);
        const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), generated.resolution - 1);
        const uint32_t x1 = std::min(x0 + 1, generated.resolution - 1);
        const uint32_t z1 = std::min(z0 + 1, generated.resolution - 1);
        const float tx = normalizedX - static_cast<float>(x0);
        const float tz = normalizedZ - static_cast<float>(z0);
        const auto heightAt = [&](uint32_t x, uint32_t z) {
            return generated.heights[static_cast<size_t>(z) * generated.resolution + x];
        };

        const float h00 = heightAt(x0, z0);
        const float h10 = heightAt(x1, z0);
        const float h01 = heightAt(x0, z1);
        const float h11 = heightAt(x1, z1);
        const float hx0 = h00 + (h10 - h00) * tx;
        const float hx1 = h01 + (h11 - h01) * tx;
        return hx0 + (hx1 - hx0) * tz;
    }

    std::optional<NavigationTerrainBuildData> TerrainSystem::navigationBuildData(
        const GeneratedTerrainTileData& generated)
    {
        return navigationBuildData(generated, generated.resolution);
    }

    std::optional<NavigationTerrainBuildData> TerrainSystem::navigationBuildData(
        const GeneratedTerrainTileData& generated,
        uint32_t navigationResolution)
    {
        if (generated.resolution < 2 ||
            generated.size <= 0.0f ||
            generated.heights.size() != static_cast<size_t>(generated.resolution) * generated.resolution) {
            return std::nullopt;
        }

        const uint32_t resolution = std::max(navigationResolution, 2u);
        const auto [minIt, maxIt] = std::minmax_element(generated.heights.begin(), generated.heights.end());
        NavigationTerrainBuildData buildData;
        buildData.coord = generated.coord;
        buildData.bounds = Renderer::Aabb{
            {generated.origin.x, *minIt, generated.origin.z},
            {generated.origin.x + generated.size, *maxIt, generated.origin.z + generated.size},
        };
        buildData.vertices.reserve(static_cast<size_t>(resolution) * resolution);
        buildData.indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6);

        const float spacing = generated.size / static_cast<float>(resolution - 1);
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = generated.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = generated.origin.z + static_cast<float>(z) * spacing;
                buildData.vertices.push_back({
                    worldX,
                    sampleGeneratedHeight(generated, worldX, worldZ).value_or(generated.heights.front()),
                    worldZ,
                });
            }
        }

        for (uint32_t z = 0; z + 1 < resolution; ++z) {
            for (uint32_t x = 0; x + 1 < resolution; ++x) {
                const uint32_t topLeft = z * resolution + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = (z + 1) * resolution + x;
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
        TerrainRenderMeshBuildInput input;
        input.coord = tile.coord;
        input.generation = tile.generation;
        input.lodIndex = lod;
        input.renderResolution = std::max(settings_.lodLevels[lod].resolution, 2u);
        input.origin = tile.origin;
        input.size = tile.size;
        input.cpuResolution = tile.resolution;
        input.heights = tile.heights;
        input.skirtDepth = settings_.skirtDepth;
        TerrainRenderMeshBuildResult result = buildRenderMeshData(input);
        if (!result.success) {
            return {};
        }
        return Renderer::createTerrainTile(result.mesh.vertices, result.mesh.indices, tile.material);
    }
}
