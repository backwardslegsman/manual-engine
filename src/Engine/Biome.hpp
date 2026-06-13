#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"

namespace Engine {
    using BiomeId = std::string;

    struct BiomeArchetypeWeight {
        std::string archetypeId;
        float weight = 1.0f;
    };

    struct BiomeDescriptor {
        BiomeId id;
        std::string displayName;
        float heightScale = 1.0f;
        float rollingScale = 1.0f;
        float detailScale = 1.0f;
        float propDensity = 1.0f;
        std::vector<BiomeArchetypeWeight> archetypeWeights;
        std::array<uint8_t, 4> terrainColor{80, 190, 210, 255};
    };

    struct BiomeSample {
        BiomeId id;
        float weight = 1.0f;
        float moisture = 0.0f;
        float roughness = 0.0f;
        float elevation = 0.0f;
    };

    struct BiomeSettings {
        uint32_t seed = 0;
        float regionScale = 0.035f;
    };

    struct BiomeLoadResult;

    class BiomeSystem {
    public:
        explicit BiomeSystem(BiomeSettings settings = {});

        static BiomeSystem sampleDefaults();
        static BiomeLoadResult loadFromYaml(const std::filesystem::path& path);

        void add(BiomeDescriptor descriptor);
        BiomeSample sample(float worldX, float worldZ) const;
        BiomeSample sampleChunk(ChunkCoord coord, float chunkSize) const;
        const BiomeDescriptor* descriptor(std::string_view id) const;
        std::vector<const BiomeDescriptor*> all() const;
        const BiomeSettings& settings() const;

    private:
        uint32_t deterministicIndex(float worldX, float worldZ, uint32_t salt, uint32_t count) const;
        float valueNoise(float worldX, float worldZ, uint32_t salt) const;

        BiomeSettings settings_;
        std::vector<BiomeDescriptor> biomes_;
    };

    struct BiomeLoadResult {
        bool success = false;
        std::string error;
        BiomeSystem system;
    };
}
