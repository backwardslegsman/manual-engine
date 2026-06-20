#include "Engine/TerrainNavigationAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Engine {
    namespace {
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
            const std::vector<float>& heights = request.sampleHeights.empty() ? request.heights : request.sampleHeights;
            const glm::vec3 origin = request.tileSize > 0.0f ? request.tileOrigin : request.origin;
            const float size = request.tileSize > 0.0f ? request.tileSize : request.size;
            if (request.settings.outputTileBounds.has_value()) {
                return *request.settings.outputTileBounds;
            }
            const auto [minIt, maxIt] = std::minmax_element(heights.begin(), heights.end());
            return Renderer::Aabb{
                {origin.x, minIt != heights.end() ? *minIt : 0.0f, origin.z},
                {origin.x + size, maxIt != heights.end() ? *maxIt : 0.0f, origin.z + size},
            };
        }

        Renderer::Aabb rasterizationBoundsFor(const TerrainNavigationBuildRequest& request)
        {
            const std::vector<float>& heights = request.sampleHeights.empty() ? request.heights : request.sampleHeights;
            const glm::vec3 origin = request.sampleResolution >= 2 && request.sampleSize > 0.0f
                ? request.sampleOrigin
                : request.origin;
            const float size = request.sampleResolution >= 2 && request.sampleSize > 0.0f
                ? request.sampleSize
                : request.size;
            const auto [minIt, maxIt] = std::minmax_element(heights.begin(), heights.end());
            return Renderer::Aabb{
                {origin.x, minIt != heights.end() ? *minIt : 0.0f, origin.z},
                {origin.x + size, maxIt != heights.end() ? *maxIt : 0.0f, origin.z + size},
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

        TerrainNavigationBuildSettings normalizedSettings(
            TerrainNavigationBuildSettings settings,
            uint32_t fallbackNavigationResolution)
        {
            settings.navigationResolution = std::max(
                settings.navigationResolution == 0 ? fallbackNavigationResolution : settings.navigationResolution,
                2u);
            settings.borderPaddingWorld = std::isfinite(settings.borderPaddingWorld)
                ? std::max(settings.borderPaddingWorld, 0.0f)
                : 0.0f;
            return settings;
        }

        const TerrainImportedChunk* findChunk(
            const std::vector<TerrainImportedChunk>& chunks,
            TerrainSourceChunkId id)
        {
            const auto found = std::ranges::find_if(chunks, [&](const TerrainImportedChunk& chunk) {
                return chunk.id == id;
            });
            return found == chunks.end() ? nullptr : &*found;
        }

        bool chunkContains(const TerrainImportedChunk& chunk, float x, float z)
        {
            constexpr float Tolerance = 0.0001f;
            return x >= chunk.origin.x - Tolerance &&
                x <= chunk.origin.x + chunk.size + Tolerance &&
                z >= chunk.origin.z - Tolerance &&
                z <= chunk.origin.z + chunk.size + Tolerance;
        }

        std::optional<float> sampleImportedChunks(
            const std::vector<TerrainImportedChunk>& chunks,
            float x,
            float z,
            uint32_t& clampedCount)
        {
            if (chunks.empty()) {
                return std::nullopt;
            }

            glm::vec2 minPoint{std::numeric_limits<float>::max()};
            glm::vec2 maxPoint{std::numeric_limits<float>::lowest()};
            for (const TerrainImportedChunk& chunk : chunks) {
                minPoint.x = std::min(minPoint.x, chunk.origin.x);
                minPoint.y = std::min(minPoint.y, chunk.origin.z);
                maxPoint.x = std::max(maxPoint.x, chunk.origin.x + chunk.size);
                maxPoint.y = std::max(maxPoint.y, chunk.origin.z + chunk.size);
            }

            const float clampedX = std::clamp(x, minPoint.x, maxPoint.x);
            const float clampedZ = std::clamp(z, minPoint.y, maxPoint.y);
            if (clampedX != x || clampedZ != z) {
                ++clampedCount;
            }

            const TerrainImportedChunk* best = nullptr;
            for (const TerrainImportedChunk& chunk : chunks) {
                if (chunkContains(chunk, clampedX, clampedZ)) {
                    best = &chunk;
                    break;
                }
            }
            if (!best) {
                float bestDistance = std::numeric_limits<float>::max();
                for (const TerrainImportedChunk& chunk : chunks) {
                    const float nearestX = std::clamp(clampedX, chunk.origin.x, chunk.origin.x + chunk.size);
                    const float nearestZ = std::clamp(clampedZ, chunk.origin.z, chunk.origin.z + chunk.size);
                    const float dx = clampedX - nearestX;
                    const float dz = clampedZ - nearestZ;
                    const float distance = dx * dx + dz * dz;
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = &chunk;
                    }
                }
            }
            return best ? sampleHeight(best->origin, best->size, best->resolution, best->heights, clampedX, clampedZ)
                        : std::nullopt;
        }

        std::optional<TerrainNavigationBuildRequest> buildNeighborhoodRequest(
            const TerrainImportedChunk& target,
            const std::vector<TerrainImportedChunk>& chunks,
            TerrainNavigationBuildSettings settings,
            TerrainNavigationSourceIdentity identity)
        {
            settings = normalizedSettings(std::move(settings), target.resolution);
            const float tileStep = target.size / static_cast<float>(settings.navigationResolution - 1u);
            const uint32_t borderSamples = settings.borderSampleCount > 0
                ? settings.borderSampleCount
                : static_cast<uint32_t>(std::ceil(settings.borderPaddingWorld / std::max(tileStep, 0.0001f)));
            const float padding = static_cast<float>(borderSamples) * tileStep;
            const uint32_t sampleResolution = settings.navigationResolution + borderSamples * 2u;
            const float sampleSize = target.size + padding * 2.0f;
            const glm::vec3 sampleOrigin{target.origin.x - padding, 0.0f, target.origin.z - padding};

            TerrainNavigationBuildRequest request;
            request.chunkId = target.id;
            request.coord = {target.coord.x, target.coord.z};
            request.origin = target.origin;
            request.size = target.size;
            request.sourceResolution = target.resolution;
            request.navigationResolution = settings.navigationResolution;
            request.heights = target.heights;
            request.tileOrigin = target.origin;
            request.tileSize = target.size;
            request.sampleOrigin = sampleOrigin;
            request.sampleSize = sampleSize;
            request.sampleResolution = sampleResolution;
            request.sampleHeights.reserve(static_cast<size_t>(sampleResolution) * sampleResolution);
            request.settings = std::move(settings);
            request.settings.borderSampleCount = borderSamples;
            request.settings.borderPaddingWorld = padding;
            request.identity = std::move(identity);
            request.identity.sourceType = TerrainDatasetSourceType::HeightmapImported;

            const float sampleStep = sampleSize / static_cast<float>(sampleResolution - 1u);
            for (uint32_t z = 0; z < sampleResolution; ++z) {
                const float worldZ = sampleOrigin.z + static_cast<float>(z) * sampleStep;
                for (uint32_t x = 0; x < sampleResolution; ++x) {
                    const float worldX = sampleOrigin.x + static_cast<float>(x) * sampleStep;
                    request.sampleHeights.push_back(sampleImportedChunks(
                        chunks,
                        worldX,
                        worldZ,
                        request.clampedEdgeSampleCount).value_or(target.heights.front()));
                }
            }
            return request;
        }
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
        request.tileOrigin = data->origin;
        request.tileSize = data->size;
        request.sampleOrigin = data->origin;
        request.sampleSize = data->size;
        request.sampleResolution = std::max(navigationResolution, 2u);
        request.settings.navigationResolution = request.navigationResolution;
        request.identity = std::move(identity);
        request.identity.sourceType = data->sourceType;
        return request;
    }

    std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromDatasetNeighborhood(
        const TerrainDataset& dataset,
        TerrainSourceHandle source,
        TerrainSourceChunkCoord coord,
        TerrainNavigationBuildSettings settings,
        TerrainNavigationSourceIdentity identity)
    {
        const std::optional<TerrainChunkHandle> targetHandle = dataset.chunkForCoord(source, coord);
        if (!targetHandle) {
            return std::nullopt;
        }
        const std::optional<TerrainChunkData> targetData = dataset.chunk(*targetHandle);
        if (!targetData ||
            targetData->resolution < 2 ||
            targetData->size <= 0.0f ||
            targetData->heights.size() != static_cast<size_t>(targetData->resolution) * targetData->resolution) {
            return std::nullopt;
        }

        std::vector<TerrainImportedChunk> chunks;
        for (TerrainChunkHandle handle : dataset.chunks()) {
            const std::optional<TerrainChunkData> data = dataset.chunk(handle);
            if (!data || data->id.source != targetData->id.source) {
                continue;
            }
            TerrainImportedChunk chunk;
            chunk.id = data->id;
            chunk.coord = data->coord;
            chunk.origin = data->origin;
            chunk.size = data->size;
            chunk.resolution = data->resolution;
            chunk.heights = data->heights;
            chunks.push_back(std::move(chunk));
        }
        const TerrainImportedChunk* target = findChunk(chunks, targetData->id);
        if (!target) {
            return std::nullopt;
        }
        return buildNeighborhoodRequest(*target, chunks, std::move(settings), std::move(identity));
    }

    std::optional<TerrainNavigationBuildRequest> terrainNavigationRequestFromImportedChunkNeighborhood(
        const std::vector<TerrainImportedChunk>& chunks,
        TerrainSourceChunkId chunkId,
        TerrainNavigationBuildSettings settings,
        TerrainNavigationSourceIdentity identity)
    {
        const TerrainImportedChunk* target = findChunk(chunks, chunkId);
        if (!target ||
            target->resolution < 2 ||
            target->size <= 0.0f ||
            target->heights.size() != static_cast<size_t>(target->resolution) * target->resolution) {
            return std::nullopt;
        }
        return buildNeighborhoodRequest(*target, chunks, std::move(settings), std::move(identity));
    }

    TerrainNavigationBuildResult buildTerrainNavigationData(const TerrainNavigationBuildRequest& request)
    {
        const bool hasExpandedSamples = request.sampleResolution >= 2 &&
            request.sampleSize > 0.0f &&
            request.sampleHeights.size() == static_cast<size_t>(request.sampleResolution) * request.sampleResolution;
        const uint32_t sourceResolution = hasExpandedSamples ? request.sampleResolution : request.sourceResolution;
        const uint32_t outputResolution = hasExpandedSamples
            ? request.sampleResolution
            : std::max(request.navigationResolution, 2u);
        const glm::vec3 sampleOrigin = hasExpandedSamples ? request.sampleOrigin : request.origin;
        const float sampleSize = hasExpandedSamples ? request.sampleSize : request.size;
        const std::vector<float>& sourceHeights = hasExpandedSamples ? request.sampleHeights : request.heights;

        TerrainNavigationBuildResult result;
        result.diagnostics.chunkId = request.chunkId;
        result.diagnostics.coord = request.coord;
        result.diagnostics.sourceResolution = sourceResolution;
        result.diagnostics.navigationResolution = outputResolution;
        result.diagnostics.borderPaddingWorld = request.settings.borderPaddingWorld;
        result.diagnostics.borderSampleCount = request.settings.borderSampleCount;
        result.diagnostics.clampedEdgeSampleCount = request.clampedEdgeSampleCount;
        result.diagnostics.bounds = boundsFor(request);
        result.diagnostics.rasterizationBounds = rasterizationBoundsFor(request);

        if (sourceResolution < 2 ||
            outputResolution < 2 ||
            request.size <= 0.0f ||
            sampleSize <= 0.0f ||
            sourceHeights.size() != static_cast<size_t>(sourceResolution) * sourceResolution) {
            result.diagnostics.message = "Invalid terrain navigation build request.";
            return result;
        }

        NavigationTerrainBuildData buildData;
        buildData.coord = request.coord;
        buildData.bounds = result.diagnostics.bounds;
        buildData.rasterizationBounds = result.diagnostics.rasterizationBounds;
        const uint32_t resolution = outputResolution;
        buildData.vertices.reserve(static_cast<size_t>(resolution) * resolution);
        buildData.indices.reserve(static_cast<size_t>(resolution - 1u) * (resolution - 1u) * 6u);

        const float spacing = sampleSize / static_cast<float>(resolution - 1u);
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = sampleOrigin.x + static_cast<float>(x) * spacing;
                const float worldZ = sampleOrigin.z + static_cast<float>(z) * spacing;
                buildData.vertices.push_back({
                    worldX,
                    sampleHeight(sampleOrigin, sampleSize, sourceResolution, sourceHeights, worldX, worldZ)
                        .value_or(sourceHeights.front()),
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
