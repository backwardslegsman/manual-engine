#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Biome.hpp"
#include "Engine/TerrainImport.hpp"

namespace Engine {
    struct TerrainSourceHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct TerrainChunkHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    enum class TerrainDatasetSourceType {
        HeightmapImported,
        Procedural,
        Generated,
    };

    struct TerrainDatasetBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    struct TerrainProceduralSourceSettings {
        glm::vec3 origin{0.0f};
        float chunkSize = 16.0f;
        uint32_t resolution = 33;
        float heightScale = 2.0f;
        const BiomeSystem* biomes = nullptr;
    };

    struct TerrainSourceDescriptor {
        AssetId sourceId;
        TerrainDatasetSourceType type = TerrainDatasetSourceType::Generated;
        TerrainDatasetBounds bounds;
        float defaultChunkSize = 16.0f;
        uint32_t defaultResolution = 33;
        AssetImportSettingsKey settings;
        std::string debugName;
        TerrainProceduralSourceSettings procedural;
    };

    struct TerrainChunkData {
        TerrainSourceHandle source;
        TerrainSourceChunkId id;
        TerrainSourceChunkCoord coord;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t resolution = 0;
        std::vector<float> heights;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
        std::vector<std::string> warnings;
    };

    struct TerrainDatasetRay {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
    };

    struct TerrainDatasetRaycastHit {
        TerrainChunkHandle chunk;
        TerrainSourceChunkId chunkId;
        glm::vec3 position{0.0f};
        float distance = 0.0f;
    };

    struct TerrainChunkDiagnostics {
        bool valid = false;
        TerrainSourceChunkId id;
        TerrainSourceChunkCoord coord;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
        uint32_t resolution = 0;
        float size = 0.0f;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        float averageHeight = 0.0f;
        float maxSlopeDegrees = 0.0f;
        float averageSlopeDegrees = 0.0f;
        uint32_t warningCount = 0;
    };

    struct TerrainDatasetDiagnostics {
        uint32_t sourceCount = 0;
        uint32_t loadedChunkCount = 0;
        uint32_t importedChunkCount = 0;
        uint32_t proceduralChunkCount = 0;
        uint32_t generatedChunkCount = 0;
        uint32_t invalidRequestCount = 0;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        float averageHeight = 0.0f;
        uint64_t estimatedBytes = 0;
        std::vector<std::string> warnings;
    };

    struct TerrainDatasetGeneratedTileData {
        TerrainSourceChunkCoord coord;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t resolution = 0;
        std::vector<float> heights;
        BiomeSample biome;
    };

    [[nodiscard]] constexpr bool isValid(TerrainSourceHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(TerrainChunkHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(TerrainSourceHandle lhs, TerrainSourceHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(TerrainSourceHandle lhs, TerrainSourceHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(TerrainChunkHandle lhs, TerrainChunkHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(TerrainChunkHandle lhs, TerrainChunkHandle rhs)
    {
        return !(lhs == rhs);
    }

    class TerrainDataset {
    public:
        [[nodiscard]] TerrainSourceHandle registerSource(TerrainSourceDescriptor descriptor);
        bool unregisterSource(TerrainSourceHandle source);
        [[nodiscard]] bool contains(TerrainSourceHandle source) const;
        [[nodiscard]] std::optional<TerrainSourceDescriptor> sourceMetadata(TerrainSourceHandle source) const;
        [[nodiscard]] std::vector<TerrainSourceHandle> sources() const;

        [[nodiscard]] TerrainChunkHandle loadImportedChunk(
            TerrainSourceHandle source,
            const TerrainImportedChunk& chunk);
        [[nodiscard]] TerrainChunkHandle loadProceduralChunk(
            TerrainSourceHandle source,
            TerrainSourceChunkCoord coord);
        bool unloadChunk(TerrainChunkHandle chunk);
        [[nodiscard]] bool contains(TerrainChunkHandle chunk) const;
        [[nodiscard]] std::optional<TerrainChunkData> chunk(TerrainChunkHandle chunk) const;
        [[nodiscard]] std::vector<TerrainChunkHandle> chunks() const;
        [[nodiscard]] std::optional<TerrainChunkHandle> chunkForCoord(
            TerrainSourceHandle source,
            TerrainSourceChunkCoord coord) const;
        [[nodiscard]] std::optional<TerrainChunkHandle> chunkForWorldPosition(
            float worldX,
            float worldZ) const;

        [[nodiscard]] std::optional<float> sampleHeight(float worldX, float worldZ) const;
        [[nodiscard]] std::optional<TerrainDatasetRaycastHit> raycast(
            const TerrainDatasetRay& ray,
            float maxDistance = 500.0f,
            float stepDistance = 1.0f,
            uint32_t refinementIterations = 8) const;
        [[nodiscard]] std::optional<TerrainDatasetBounds> chunkWorldBounds(TerrainChunkHandle chunk) const;
        [[nodiscard]] std::optional<TerrainChunkDiagnostics> chunkDiagnostics(TerrainChunkHandle chunk) const;
        [[nodiscard]] TerrainDatasetDiagnostics diagnostics() const;
        [[nodiscard]] std::optional<TerrainDatasetGeneratedTileData> generatedTileData(TerrainChunkHandle chunk) const;

    private:
        struct SourceRecord {
            uint32_t generation = 0;
            bool alive = false;
            TerrainSourceDescriptor descriptor;
        };

        struct ChunkRecord {
            uint32_t generation = 0;
            bool alive = false;
            TerrainChunkData data;
        };

        [[nodiscard]] SourceRecord* record(TerrainSourceHandle source);
        [[nodiscard]] const SourceRecord* record(TerrainSourceHandle source) const;
        [[nodiscard]] ChunkRecord* record(TerrainChunkHandle chunk);
        [[nodiscard]] const ChunkRecord* record(TerrainChunkHandle chunk) const;
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        [[nodiscard]] TerrainSourceHandle sourceHandleForIndex(uint32_t index) const;
        [[nodiscard]] TerrainChunkHandle chunkHandleForIndex(uint32_t index) const;
        [[nodiscard]] TerrainChunkHandle storeChunk(TerrainChunkData data);
        void countInvalidRequest() const;

        std::vector<SourceRecord> sources_;
        std::vector<ChunkRecord> chunks_;
        mutable uint32_t invalidRequestCount_ = 0;
    };
}
