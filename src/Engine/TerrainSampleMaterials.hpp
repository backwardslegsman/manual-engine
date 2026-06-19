#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "Engine/TerrainMaterialMetadata.hpp"

namespace Engine {
    struct TerrainSampleBiomeMaterialInput {
        std::string biomeId;
        std::array<uint8_t, 4> color{};
    };

    [[nodiscard]] TerrainMaterialSet makeSampleProceduralTerrainMaterialSet(
        std::span<const TerrainSampleBiomeMaterialInput> biomeMaterials,
        const std::array<uint8_t, 4>& fallbackColor);
}
