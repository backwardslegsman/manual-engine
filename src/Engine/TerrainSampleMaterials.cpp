#include "Engine/TerrainSampleMaterials.hpp"

#include <glm/glm.hpp>

namespace Engine {
    namespace {
        [[nodiscard]] glm::vec4 colorFactorFromBytes(const std::array<uint8_t, 4>& color)
        {
            return {
                static_cast<float>(color[0]) / 255.0f,
                static_cast<float>(color[1]) / 255.0f,
                static_cast<float>(color[2]) / 255.0f,
                static_cast<float>(color[3]) / 255.0f,
            };
        }
    }

    TerrainMaterialSet makeSampleProceduralTerrainMaterialSet(
        std::span<const TerrainSampleBiomeMaterialInput> biomeMaterials,
        const std::array<uint8_t, 4>& fallbackColor)
    {
        const std::array<uint8_t, 4> firstColor = biomeMaterials.empty()
            ? fallbackColor
            : biomeMaterials.front().color;
        const std::array<uint8_t, 4> secondColor = biomeMaterials.size() > 1
            ? biomeMaterials[1].color
            : fallbackColor;

        TerrainMaterialSet materialSet;
        materialSet.id = AssetId{0x7465727261696e31ull};
        materialSet.debugName = "sample.procedural.layered_terrain";
        materialSet.layers.push_back(TerrainMaterialLayer{
            TerrainMaterialLayerId{1},
            "biome fallback",
            {},
            colorFactorFromBytes(firstColor),
            1.0f,
            0.0f,
            0.92f,
            1.0f,
            {},
            {0.08f, 0.08f},
            TerrainMaterialProjectionMode::WorldPlanar,
            true,
        });
        materialSet.layers.push_back(TerrainMaterialLayer{
            TerrainMaterialLayerId{2},
            "steep rock",
            {},
            glm::vec4{0.42f, 0.40f, 0.35f, 1.0f},
            1.0f,
            0.0f,
            0.86f,
            1.0f,
            {},
            {0.12f, 0.12f},
            TerrainMaterialProjectionMode::WorldPlanar,
            false,
        });
        materialSet.layers.push_back(TerrainMaterialLayer{
            TerrainMaterialLayerId{3},
            "highland scrub",
            {},
            glm::mix(colorFactorFromBytes(secondColor), glm::vec4{0.74f, 0.76f, 0.68f, 1.0f}, 0.45f),
            1.0f,
            0.0f,
            0.96f,
            1.0f,
            {},
            {0.06f, 0.06f},
            TerrainMaterialProjectionMode::WorldPlanar,
            false,
        });
        materialSet.layers.push_back(TerrainMaterialLayer{
            TerrainMaterialLayerId{4},
            "dry exposed soil",
            {},
            glm::vec4{0.58f, 0.46f, 0.32f, 1.0f},
            1.0f,
            0.0f,
            0.9f,
            1.0f,
            {},
            {0.1f, 0.1f},
            TerrainMaterialProjectionMode::WorldPlanar,
            false,
        });
        materialSet.rules.push_back(TerrainMaterialRule{
            TerrainMaterialRuleId{1},
            TerrainMaterialLayerId{2},
            100,
            1.0f,
            8.0f,
            std::nullopt,
            TerrainMaterialRange{30.0f, 90.0f},
            std::nullopt,
            std::nullopt,
            {},
            "steep slope rock",
        });
        materialSet.rules.push_back(TerrainMaterialRule{
            TerrainMaterialRuleId{2},
            TerrainMaterialLayerId{3},
            50,
            0.75f,
            0.8f,
            TerrainMaterialRange{1.25f, 64.0f},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            "highland tint",
        });
        materialSet.rules.push_back(TerrainMaterialRule{
            TerrainMaterialRuleId{3},
            TerrainMaterialLayerId{4},
            10,
            0.35f,
            18.0f,
            std::nullopt,
            std::nullopt,
            TerrainMaterialRange{-96.0f, 96.0f},
            std::nullopt,
            {},
            "center soil band",
        });
        return materialSet;
    }
}
