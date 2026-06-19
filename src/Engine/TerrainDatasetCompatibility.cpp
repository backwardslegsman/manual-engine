#include "Engine/TerrainDatasetCompatibility.hpp"

namespace Engine {
    std::optional<GeneratedTerrainTileData> toGeneratedTerrainTileData(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk)
    {
        const std::optional<TerrainDatasetGeneratedTileData> source = dataset.generatedTileData(chunk);
        if (!source) {
            return std::nullopt;
        }

        GeneratedTerrainTileData generated;
        generated.coord = {source->coord.x, source->coord.z};
        generated.origin = source->origin;
        generated.size = source->size;
        generated.resolution = source->resolution;
        generated.heights = source->heights;
        generated.biome = source->biome;
        return generated;
    }
}
