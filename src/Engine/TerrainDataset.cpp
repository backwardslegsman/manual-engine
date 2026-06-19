#include "Engine/TerrainDataset.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Engine {
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

        bool validHeights(uint32_t resolution, const std::vector<float>& heights)
        {
            return resolution >= 2 && heights.size() == static_cast<size_t>(resolution) * resolution;
        }

        float heightAt(const TerrainChunkData& chunk, uint32_t x, uint32_t z)
        {
            return chunk.heights[static_cast<size_t>(z) * chunk.resolution + x];
        }

        float sampleChunkHeight(const TerrainChunkData& chunk, float worldX, float worldZ)
        {
            const float localX = std::clamp(worldX - chunk.origin.x, 0.0f, chunk.size);
            const float localZ = std::clamp(worldZ - chunk.origin.z, 0.0f, chunk.size);
            const float normalizedX = localX / chunk.size * static_cast<float>(chunk.resolution - 1u);
            const float normalizedZ = localZ / chunk.size * static_cast<float>(chunk.resolution - 1u);
            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), chunk.resolution - 1u);
            const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), chunk.resolution - 1u);
            const uint32_t x1 = std::min(x0 + 1u, chunk.resolution - 1u);
            const uint32_t z1 = std::min(z0 + 1u, chunk.resolution - 1u);
            const float tx = normalizedX - static_cast<float>(x0);
            const float tz = normalizedZ - static_cast<float>(z0);

            const float h00 = heightAt(chunk, x0, z0);
            const float h10 = heightAt(chunk, x1, z0);
            const float h01 = heightAt(chunk, x0, z1);
            const float h11 = heightAt(chunk, x1, z1);
            const float hx0 = h00 + (h10 - h00) * tx;
            const float hx1 = h01 + (h11 - h01) * tx;
            return hx0 + (hx1 - hx0) * tz;
        }

        bool containsWorldPosition(const TerrainChunkData& chunk, float worldX, float worldZ)
        {
            return worldX >= chunk.origin.x &&
                worldX <= chunk.origin.x + chunk.size &&
                worldZ >= chunk.origin.z &&
                worldZ <= chunk.origin.z + chunk.size;
        }

        float proceduralHeight(const TerrainProceduralSourceSettings& settings, const BiomeSystem* biomes, float worldX, float worldZ)
        {
            const BiomeSample sample = biomes ? biomes->sample(worldX, worldZ) : BiomeSystem::sampleDefaults().sample(worldX, worldZ);
            const BiomeDescriptor* biome = biomes ? biomes->descriptor(sample.id) : nullptr;
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
            return baseHeight + (rolling + detail) * settings.heightScale * heightScale;
        }

        BiomeSample proceduralBiome(const TerrainProceduralSourceSettings& settings, TerrainSourceChunkCoord coord)
        {
            const ChunkCoord chunkCoord{coord.x, coord.z};
            return settings.biomes
                ? settings.biomes->sampleChunk(chunkCoord, settings.chunkSize)
                : BiomeSystem::sampleDefaults().sampleChunk(chunkCoord, settings.chunkSize);
        }
    }

    TerrainSourceHandle TerrainDataset::registerSource(TerrainSourceDescriptor descriptor)
    {
        descriptor.defaultChunkSize = std::max(descriptor.defaultChunkSize, 1.0f);
        descriptor.defaultResolution = std::max(descriptor.defaultResolution, 2u);
        descriptor.procedural.chunkSize = std::max(descriptor.procedural.chunkSize, 1.0f);
        descriptor.procedural.resolution = std::max(descriptor.procedural.resolution, 2u);

        for (uint32_t index = 0; index < sources_.size(); ++index) {
            SourceRecord& source = sources_[index];
            if (!source.alive) {
                source.alive = true;
                source.generation = nextGeneration(source.generation);
                source.descriptor = std::move(descriptor);
                return {index, source.generation};
            }
        }

        SourceRecord source;
        source.alive = true;
        source.generation = nextGeneration(source.generation);
        source.descriptor = std::move(descriptor);
        sources_.push_back(std::move(source));
        return {static_cast<uint32_t>(sources_.size() - 1u), sources_.back().generation};
    }

    bool TerrainDataset::unregisterSource(TerrainSourceHandle source)
    {
        SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            countInvalidRequest();
            return false;
        }

        for (ChunkRecord& chunk : chunks_) {
            if (chunk.alive && chunk.data.source == source) {
                chunk.alive = false;
                chunk.data = {};
            }
        }

        sourceRecord->alive = false;
        sourceRecord->descriptor = {};
        return true;
    }

    bool TerrainDataset::contains(TerrainSourceHandle source) const
    {
        return record(source) != nullptr;
    }

    std::optional<TerrainSourceDescriptor> TerrainDataset::sourceMetadata(TerrainSourceHandle source) const
    {
        const SourceRecord* sourceRecord = record(source);
        if (!sourceRecord) {
            return std::nullopt;
        }
        return sourceRecord->descriptor;
    }

    std::vector<TerrainSourceHandle> TerrainDataset::sources() const
    {
        std::vector<TerrainSourceHandle> handles;
        for (uint32_t index = 0; index < sources_.size(); ++index) {
            if (sources_[index].alive) {
                handles.push_back({index, sources_[index].generation});
            }
        }
        return handles;
    }

    TerrainChunkHandle TerrainDataset::loadImportedChunk(
        TerrainSourceHandle source,
        const TerrainImportedChunk& chunk)
    {
        const SourceRecord* sourceRecord = record(source);
        if (!sourceRecord ||
            sourceRecord->descriptor.type != TerrainDatasetSourceType::HeightmapImported ||
            chunk.size <= 0.0f ||
            !validHeights(chunk.resolution, chunk.heights)) {
            countInvalidRequest();
            return {};
        }

        TerrainChunkData data;
        data.source = source;
        data.id = chunk.id;
        if (!isValid(data.id.source)) {
            data.id.source = sourceRecord->descriptor.sourceId;
        }
        data.coord = chunk.coord;
        data.origin = chunk.origin;
        data.size = chunk.size;
        data.resolution = chunk.resolution;
        data.heights = chunk.heights;
        data.sourceType = TerrainDatasetSourceType::HeightmapImported;
        data.warnings = chunk.warnings;
        return storeChunk(std::move(data));
    }

    TerrainChunkHandle TerrainDataset::loadProceduralChunk(
        TerrainSourceHandle source,
        TerrainSourceChunkCoord coord)
    {
        const SourceRecord* sourceRecord = record(source);
        if (!sourceRecord || sourceRecord->descriptor.type != TerrainDatasetSourceType::Procedural) {
            countInvalidRequest();
            return {};
        }

        const TerrainProceduralSourceSettings& settings = sourceRecord->descriptor.procedural;
        TerrainChunkData data;
        data.source = source;
        data.id = {sourceRecord->descriptor.sourceId, coord};
        data.coord = coord;
        data.origin = {
            settings.origin.x + static_cast<float>(coord.x) * settings.chunkSize,
            0.0f,
            settings.origin.z + static_cast<float>(coord.z) * settings.chunkSize,
        };
        data.size = settings.chunkSize;
        data.resolution = settings.resolution;
        data.sourceType = TerrainDatasetSourceType::Procedural;
        data.heights.resize(static_cast<size_t>(data.resolution) * data.resolution);

        const float spacing = data.size / static_cast<float>(data.resolution - 1u);
        for (uint32_t z = 0; z < data.resolution; ++z) {
            for (uint32_t x = 0; x < data.resolution; ++x) {
                const float worldX = data.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = data.origin.z + static_cast<float>(z) * spacing;
                data.heights[static_cast<size_t>(z) * data.resolution + x] =
                    proceduralHeight(settings, settings.biomes, worldX, worldZ);
            }
        }

        return storeChunk(std::move(data));
    }

    bool TerrainDataset::unloadChunk(TerrainChunkHandle chunk)
    {
        ChunkRecord* chunkRecord = record(chunk);
        if (!chunkRecord) {
            countInvalidRequest();
            return false;
        }
        chunkRecord->alive = false;
        chunkRecord->data = {};
        return true;
    }

    bool TerrainDataset::contains(TerrainChunkHandle chunk) const
    {
        return record(chunk) != nullptr;
    }

    std::optional<TerrainChunkData> TerrainDataset::chunk(TerrainChunkHandle chunk) const
    {
        const ChunkRecord* chunkRecord = record(chunk);
        if (!chunkRecord) {
            return std::nullopt;
        }
        return chunkRecord->data;
    }

    std::vector<TerrainChunkHandle> TerrainDataset::chunks() const
    {
        std::vector<TerrainChunkHandle> handles;
        for (uint32_t index = 0; index < chunks_.size(); ++index) {
            if (chunks_[index].alive) {
                handles.push_back({index, chunks_[index].generation});
            }
        }
        return handles;
    }

    std::optional<TerrainChunkHandle> TerrainDataset::chunkForCoord(
        TerrainSourceHandle source,
        TerrainSourceChunkCoord coord) const
    {
        if (!contains(source)) {
            return std::nullopt;
        }
        for (uint32_t index = 0; index < chunks_.size(); ++index) {
            const ChunkRecord& chunk = chunks_[index];
            if (chunk.alive && chunk.data.source == source && chunk.data.coord == coord) {
                return TerrainChunkHandle{index, chunk.generation};
            }
        }
        return std::nullopt;
    }

    std::optional<TerrainChunkHandle> TerrainDataset::chunkForWorldPosition(float worldX, float worldZ) const
    {
        for (uint32_t index = 0; index < chunks_.size(); ++index) {
            const ChunkRecord& chunk = chunks_[index];
            if (chunk.alive && containsWorldPosition(chunk.data, worldX, worldZ)) {
                return TerrainChunkHandle{index, chunk.generation};
            }
        }
        return std::nullopt;
    }

    std::optional<float> TerrainDataset::sampleHeight(float worldX, float worldZ) const
    {
        const std::optional<TerrainChunkHandle> handle = chunkForWorldPosition(worldX, worldZ);
        if (!handle) {
            return std::nullopt;
        }
        const ChunkRecord* chunkRecord = record(*handle);
        if (!chunkRecord || !validHeights(chunkRecord->data.resolution, chunkRecord->data.heights)) {
            return std::nullopt;
        }
        return sampleChunkHeight(chunkRecord->data, worldX, worldZ);
    }

    std::optional<TerrainDatasetRaycastHit> TerrainDataset::raycast(
        const TerrainDatasetRay& ray,
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
                const std::optional<TerrainChunkHandle> hitChunk = chunkForWorldPosition(hitPoint.x, hitPoint.z);
                if (!hitHeight || !hitChunk) {
                    return std::nullopt;
                }
                hitPoint.y = *hitHeight;
                const ChunkRecord* chunk = record(*hitChunk);
                return TerrainDatasetRaycastHit{*hitChunk, chunk ? chunk->data.id : TerrainSourceChunkId{}, hitPoint, hitDistance};
            }

            hasPreviousSample = true;
            previousDistance = distance;
            previousSignedHeight = signedHeight;
        }

        return std::nullopt;
    }

    std::optional<TerrainDatasetBounds> TerrainDataset::chunkWorldBounds(TerrainChunkHandle chunk) const
    {
        const ChunkRecord* chunkRecord = record(chunk);
        if (!chunkRecord || chunkRecord->data.heights.empty()) {
            return std::nullopt;
        }
        const auto [minIt, maxIt] = std::minmax_element(chunkRecord->data.heights.begin(), chunkRecord->data.heights.end());
        return TerrainDatasetBounds{
            {chunkRecord->data.origin.x, *minIt, chunkRecord->data.origin.z},
            {chunkRecord->data.origin.x + chunkRecord->data.size, *maxIt, chunkRecord->data.origin.z + chunkRecord->data.size},
        };
    }

    std::optional<TerrainChunkDiagnostics> TerrainDataset::chunkDiagnostics(TerrainChunkHandle chunk) const
    {
        const ChunkRecord* chunkRecord = record(chunk);
        if (!chunkRecord || !validHeights(chunkRecord->data.resolution, chunkRecord->data.heights)) {
            return std::nullopt;
        }

        const TerrainChunkData& data = chunkRecord->data;
        const auto [minIt, maxIt] = std::minmax_element(data.heights.begin(), data.heights.end());
        TerrainChunkDiagnostics diagnostics;
        diagnostics.valid = true;
        diagnostics.id = data.id;
        diagnostics.coord = data.coord;
        diagnostics.sourceType = data.sourceType;
        diagnostics.resolution = data.resolution;
        diagnostics.size = data.size;
        diagnostics.minHeight = *minIt;
        diagnostics.maxHeight = *maxIt;
        diagnostics.averageHeight = std::accumulate(data.heights.begin(), data.heights.end(), 0.0f) /
            static_cast<float>(data.heights.size());
        diagnostics.warningCount = static_cast<uint32_t>(data.warnings.size());

        const float spacing = data.size / static_cast<float>(data.resolution - 1u);
        float slopeSum = 0.0f;
        uint32_t slopeCount = 0;
        for (uint32_t z = 0; z < data.resolution; ++z) {
            for (uint32_t x = 0; x < data.resolution; ++x) {
                const float left = heightAt(data, x > 0 ? x - 1u : x, z);
                const float right = heightAt(data, std::min(x + 1u, data.resolution - 1u), z);
                const float down = heightAt(data, x, z > 0 ? z - 1u : z);
                const float up = heightAt(data, x, std::min(z + 1u, data.resolution - 1u));
                const float slope = slopeDegreesFromNormal(terrainNormalFromHeights(left, right, down, up, spacing));
                diagnostics.maxSlopeDegrees = std::max(diagnostics.maxSlopeDegrees, slope);
                slopeSum += slope;
                ++slopeCount;
            }
        }
        diagnostics.averageSlopeDegrees = slopeCount > 0 ? slopeSum / static_cast<float>(slopeCount) : 0.0f;
        return diagnostics;
    }

    TerrainDatasetDiagnostics TerrainDataset::diagnostics() const
    {
        TerrainDatasetDiagnostics diagnostics;
        diagnostics.invalidRequestCount = invalidRequestCount_;
        double heightSum = 0.0;
        uint64_t heightCount = 0;
        bool hasHeight = false;
        for (const SourceRecord& source : sources_) {
            if (source.alive) {
                ++diagnostics.sourceCount;
            }
        }
        for (const ChunkRecord& chunk : chunks_) {
            if (!chunk.alive) {
                continue;
            }
            ++diagnostics.loadedChunkCount;
            diagnostics.estimatedBytes += sizeof(ChunkRecord);
            diagnostics.estimatedBytes += static_cast<uint64_t>(chunk.data.heights.size() * sizeof(float));
            diagnostics.estimatedBytes += static_cast<uint64_t>(chunk.data.warnings.size() * sizeof(std::string));
            switch (chunk.data.sourceType) {
                case TerrainDatasetSourceType::HeightmapImported:
                    ++diagnostics.importedChunkCount;
                    break;
                case TerrainDatasetSourceType::Procedural:
                    ++diagnostics.proceduralChunkCount;
                    break;
                case TerrainDatasetSourceType::Generated:
                    ++diagnostics.generatedChunkCount;
                    break;
            }
            diagnostics.warnings.insert(diagnostics.warnings.end(), chunk.data.warnings.begin(), chunk.data.warnings.end());
            for (float height : chunk.data.heights) {
                if (!hasHeight) {
                    diagnostics.minHeight = height;
                    diagnostics.maxHeight = height;
                    hasHeight = true;
                } else {
                    diagnostics.minHeight = std::min(diagnostics.minHeight, height);
                    diagnostics.maxHeight = std::max(diagnostics.maxHeight, height);
                }
                heightSum += height;
                ++heightCount;
            }
        }
        diagnostics.averageHeight = heightCount > 0 ? static_cast<float>(heightSum / static_cast<double>(heightCount)) : 0.0f;
        std::sort(diagnostics.warnings.begin(), diagnostics.warnings.end());
        diagnostics.warnings.erase(std::unique(diagnostics.warnings.begin(), diagnostics.warnings.end()), diagnostics.warnings.end());
        return diagnostics;
    }

    std::optional<TerrainDatasetGeneratedTileData> TerrainDataset::generatedTileData(TerrainChunkHandle chunk) const
    {
        const ChunkRecord* chunkRecord = record(chunk);
        if (!chunkRecord || !validHeights(chunkRecord->data.resolution, chunkRecord->data.heights)) {
            return std::nullopt;
        }

        TerrainDatasetGeneratedTileData generated;
        generated.coord = chunkRecord->data.coord;
        generated.origin = chunkRecord->data.origin;
        generated.size = chunkRecord->data.size;
        generated.resolution = chunkRecord->data.resolution;
        generated.heights = chunkRecord->data.heights;
        const SourceRecord* source = record(chunkRecord->data.source);
        if (source && source->descriptor.type == TerrainDatasetSourceType::Procedural) {
            generated.biome = proceduralBiome(source->descriptor.procedural, chunkRecord->data.coord);
        }
        return generated;
    }

    TerrainDataset::SourceRecord* TerrainDataset::record(TerrainSourceHandle source)
    {
        if (!isValid(source) || source.index >= sources_.size()) {
            return nullptr;
        }
        SourceRecord& sourceRecord = sources_[source.index];
        return sourceRecord.alive && sourceRecord.generation == source.generation ? &sourceRecord : nullptr;
    }

    const TerrainDataset::SourceRecord* TerrainDataset::record(TerrainSourceHandle source) const
    {
        if (!isValid(source) || source.index >= sources_.size()) {
            return nullptr;
        }
        const SourceRecord& sourceRecord = sources_[source.index];
        return sourceRecord.alive && sourceRecord.generation == source.generation ? &sourceRecord : nullptr;
    }

    TerrainDataset::ChunkRecord* TerrainDataset::record(TerrainChunkHandle chunk)
    {
        if (!isValid(chunk) || chunk.index >= chunks_.size()) {
            return nullptr;
        }
        ChunkRecord& chunkRecord = chunks_[chunk.index];
        return chunkRecord.alive && chunkRecord.generation == chunk.generation ? &chunkRecord : nullptr;
    }

    const TerrainDataset::ChunkRecord* TerrainDataset::record(TerrainChunkHandle chunk) const
    {
        if (!isValid(chunk) || chunk.index >= chunks_.size()) {
            return nullptr;
        }
        const ChunkRecord& chunkRecord = chunks_[chunk.index];
        return chunkRecord.alive && chunkRecord.generation == chunk.generation ? &chunkRecord : nullptr;
    }

    uint32_t TerrainDataset::nextGeneration(uint32_t generation) const
    {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    TerrainSourceHandle TerrainDataset::sourceHandleForIndex(uint32_t index) const
    {
        return index < sources_.size() && sources_[index].alive
            ? TerrainSourceHandle{index, sources_[index].generation}
            : TerrainSourceHandle{};
    }

    TerrainChunkHandle TerrainDataset::chunkHandleForIndex(uint32_t index) const
    {
        return index < chunks_.size() && chunks_[index].alive
            ? TerrainChunkHandle{index, chunks_[index].generation}
            : TerrainChunkHandle{};
    }

    TerrainChunkHandle TerrainDataset::storeChunk(TerrainChunkData data)
    {
        for (uint32_t index = 0; index < chunks_.size(); ++index) {
            ChunkRecord& chunk = chunks_[index];
            if (!chunk.alive) {
                chunk.alive = true;
                chunk.generation = nextGeneration(chunk.generation);
                chunk.data = std::move(data);
                return {index, chunk.generation};
            }
        }

        ChunkRecord chunk;
        chunk.alive = true;
        chunk.generation = nextGeneration(chunk.generation);
        chunk.data = std::move(data);
        chunks_.push_back(std::move(chunk));
        return {static_cast<uint32_t>(chunks_.size() - 1u), chunks_.back().generation};
    }

    void TerrainDataset::countInvalidRequest() const
    {
        ++invalidRequestCount_;
    }
}
