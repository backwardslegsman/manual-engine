#pragma once

#include <optional>

#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"

namespace Engine {
    [[nodiscard]] std::optional<GeneratedTerrainTileData> toGeneratedTerrainTileData(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk);
}
