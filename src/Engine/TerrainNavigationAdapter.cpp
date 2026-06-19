#include "Engine/TerrainNavigationAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace Engine {
    namespace {
        constexpr AssetId LegacyProceduralTerrainSourceId{0x7465727261696e01ull};

        std::optional<float> sampleHeight(
            const glm::vec3& origin,
            float size,
            uint32_t resolution,
            const std::vector<float>& heights,
            float worldX,
            float worldZ)
        {
            if (resolution < 2 ||
                size <= 0.0f ||
                heights.size() != static_cast<size_t>(resolution) * resolution) {
                return std::nullopt;
            }

            const float localX = std::clamp(worldX - origin.x, 0.0f, size);
            const float localZ = std::clamp(worldZ - origin.z, 0.0f, size);
            const float normalizedX = localX / size * static_cast<float>(resolution - 1u);
            const float normalizedZ = localZ / size * static_cast<float>(resolution - 1u);
            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), resolution - 1u);
            const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), resolution - 1u);
            const uint32_t x1 = std::min(x0 + 1u, resolution - 1u);
            const uint32_t z1 = std::min(z0 + 1u, resolution - 1u);
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

        Renderer::Aabb boundsFor(const TerrainNavigationBuildRequest& request)
        {
            const auto [minIt, maxIt] = std::minmax_element(request.heights.begin(), request.heights.end());
            return Renderer::Aabb{
                {request.origin.x, minIt != request.heights.end() ? *minIt : 0.0f, request.origin.z},
                {request.origin.x + request.size, maxIt != request.heights.end() ? *maxIt : 0.0f, request.origin.z + request.size},
            };
        }

        std::string sourceTypeName(TerrainDatasetSourceType type)
        {
            switch (type) {
                case TerrainDatasetSourceType::HeightmapImported: return "heightmap_imported";
                case TerrainDatasetSourceType::Procedural: return "procedural";
                case TerrainDatasetSourceType::Generated: return "generated";
            }
            return "unknown";
        }
    }

    TerrainNavigationSourceIdentity legacyProceduralTerrainNavigationIdentity(const TerrainSettings& settings)
    {
        TerrainNavigationSourceIdentity identity;
        identity.sourceId = LegacyProceduralTerrainSourceId;
        identity.sourceType = TerrainDatasetSourceType::Procedural;
        identity.importSettings = {"legacy_procedural_terrain", "1", "runtime"};

        std::ostringstream stream;
        stream << "chunk=" << settings.chunkSize
               << ";resolution=" << settings.resolution
               << ";heightScale=" << settings.heightScale
               << ";navResolution=" << settings.navigationResolution;
        identity.sourceHash = stream.str();
        return identity;
    }

    std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity)
    {
        const std::optional<TerrainChunkData> data = dataset.chunk(chunk);
        if (!data ||
            data->resolution < 2 ||
            data->size <= 0.0f ||
            data->heights.size() != static_cast<size_t>(data->resolution) * data->resolution) {
            return std::nullopt;
        }

        TerrainNavigationBuildRequest request;
        request.chunkId = data->id;
        request.coord = {data->coord.x, data->coord.z};
        request.origin = data->origin;
        request.size = data->size;
        request.sourceResolution = data->resolution;
        request.navigationResolution = std::max(navigationResolution, 2u);
        request.heights = data->heights;
        request.identity = std::move(identity);
        request.identity.sourceType = data->sourceType;
        return request;
    }

    std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromGeneratedTile(
        const GeneratedTerrainTileData& generated,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity)
    {
        if (generated.resolution < 2 ||
            generated.size <= 0.0f ||
            generated.heights.size() != static_cast<size_t>(generated.resolution) * generated.resolution) {
            return std::nullopt;
        }

        TerrainNavigationBuildRequest request;
        request.chunkId = {identity.sourceId, {generated.coord.x, generated.coord.z}};
        request.coord = generated.coord;
        request.origin = generated.origin;
        request.size = generated.size;
        request.sourceResolution = generated.resolution;
        request.navigationResolution = std::max(navigationResolution, 2u);
        request.heights = generated.heights;
        request.identity = std::move(identity);
        return request;
    }

    std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromTerrainSystemTile(
        const TerrainSystem& terrain,
        TerrainTileHandle tile,
        uint32_t navigationResolution,
        TerrainNavigationSourceIdentity identity)
    {
        const std::optional<TerrainRenderMeshBuildInput> input = terrain.renderMeshBuildInput(tile, 0);
        if (!input ||
            input->cpuResolution < 2 ||
            input->size <= 0.0f ||
            input->heights.size() != static_cast<size_t>(input->cpuResolution) * input->cpuResolution) {
            return std::nullopt;
        }

        TerrainNavigationBuildRequest request;
        request.chunkId = {identity.sourceId, {input->coord.x, input->coord.z}};
        request.coord = input->coord;
        request.origin = input->origin;
        request.size = input->size;
        request.sourceResolution = input->cpuResolution;
        request.navigationResolution = std::max(navigationResolution, 2u);
        request.heights = input->heights;
        request.identity = std::move(identity);
        return request;
    }

    TerrainNavigationBuildResult buildTerrainNavigationData(const TerrainNavigationBuildRequest& request)
    {
        TerrainNavigationBuildResult result;
        result.diagnostics.chunkId = request.chunkId;
        result.diagnostics.coord = request.coord;
        result.diagnostics.sourceResolution = request.sourceResolution;
        result.diagnostics.navigationResolution = request.navigationResolution;
        result.diagnostics.bounds = boundsFor(request);

        if (request.sourceResolution < 2 ||
            request.navigationResolution < 2 ||
            request.size <= 0.0f ||
            request.heights.size() != static_cast<size_t>(request.sourceResolution) * request.sourceResolution) {
            result.diagnostics.message = "Invalid terrain navigation build request.";
            return result;
        }

        NavigationTerrainBuildData buildData;
        buildData.coord = request.coord;
        buildData.bounds = result.diagnostics.bounds;
        const uint32_t resolution = std::max(request.navigationResolution, 2u);
        buildData.vertices.reserve(static_cast<size_t>(resolution) * resolution);
        buildData.indices.reserve(static_cast<size_t>(resolution - 1u) * (resolution - 1u) * 6u);

        const float spacing = request.size / static_cast<float>(resolution - 1u);
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = request.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = request.origin.z + static_cast<float>(z) * spacing;
                buildData.vertices.push_back({
                    worldX,
                    sampleHeight(request.origin, request.size, request.sourceResolution, request.heights, worldX, worldZ)
                        .value_or(request.heights.front()),
                    worldZ,
                });
            }
        }

        for (uint32_t z = 0; z + 1u < resolution; ++z) {
            for (uint32_t x = 0; x + 1u < resolution; ++x) {
                const uint32_t topLeft = z * resolution + x;
                const uint32_t topRight = topLeft + 1u;
                const uint32_t bottomLeft = (z + 1u) * resolution + x;
                const uint32_t bottomRight = bottomLeft + 1u;
                buildData.indices.push_back(topLeft);
                buildData.indices.push_back(bottomLeft);
                buildData.indices.push_back(topRight);
                buildData.indices.push_back(topRight);
                buildData.indices.push_back(bottomLeft);
                buildData.indices.push_back(bottomRight);
            }
        }

        result.success = true;
        result.buildData = std::move(buildData);
        result.diagnostics.valid = true;
        result.diagnostics.vertexCount = static_cast<uint32_t>(result.buildData->vertices.size());
        result.diagnostics.triangleCount = static_cast<uint32_t>(result.buildData->indices.size() / 3u);
        result.diagnostics.message = "Terrain navigation build data generated from " + sourceTypeName(request.identity.sourceType) + " terrain.";
        return result;
    }
}
