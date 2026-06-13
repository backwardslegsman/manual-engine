#include "Engine/Biome.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace {
    uint32_t hashInts(int32_t x, int32_t z, uint32_t seed, uint32_t salt)
    {
        uint32_t value = static_cast<uint32_t>(x) * 0x9e3779b1u;
        value ^= static_cast<uint32_t>(z) * 0x85ebca6bu;
        value ^= seed * 0x27d4eb2du;
        value ^= salt * 0xc2b2ae35u;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float normalizedHash(int32_t x, int32_t z, uint32_t seed, uint32_t salt)
    {
        return static_cast<float>(hashInts(x, z, seed, salt) & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
    }

    std::array<uint8_t, 4> readColor(const YAML::Node& node, std::array<uint8_t, 4> fallback)
    {
        if (!node || !node.IsSequence() || (node.size() != 3 && node.size() != 4)) {
            return fallback;
        }

        std::array<uint8_t, 4> color = fallback;
        for (std::size_t index = 0; index < node.size(); ++index) {
            color[index] = static_cast<uint8_t>(std::clamp(node[index].as<int>(color[index]), 0, 255));
        }
        if (node.size() == 3) {
            color[3] = 255;
        }
        return color;
    }

    std::vector<Engine::BiomeArchetypeWeight> readArchetypeWeights(const YAML::Node& node)
    {
        std::vector<Engine::BiomeArchetypeWeight> weights;
        if (!node || !node.IsMap()) {
            return weights;
        }

        for (const auto& item : node) {
            const std::string id = item.first.as<std::string>(std::string{});
            const float weight = item.second.as<float>(0.0f);
            if (!id.empty() && weight > 0.0f) {
                weights.push_back({id, weight});
            }
        }
        return weights;
    }
}

namespace Engine {
    BiomeSystem::BiomeSystem(BiomeSettings settings)
        : settings_(settings)
    {
        settings_.regionScale = std::max(settings_.regionScale, 0.001f);
    }

    BiomeSystem BiomeSystem::sampleDefaults()
    {
        BiomeSystem system;
        system.add({
            "grassland",
            "Grassland",
            0.85f,
            0.75f,
            0.55f,
            1.0f,
            {{"tree_cluster", 4.0f}, {"rock", 1.0f}, {"field_marker", 0.8f}, {"camp_marker", 0.4f}},
            {80, 180, 110, 255},
        });
        system.add({
            "rocky",
            "Rocky",
            1.35f,
            1.25f,
            1.4f,
            0.9f,
            {{"rock", 5.0f}, {"tree_cluster", 0.8f}, {"field_marker", 0.6f}},
            {140, 130, 105, 255},
        });
        system.add({
            "sparse",
            "Sparse",
            0.6f,
            0.55f,
            0.45f,
            0.45f,
            {{"rock", 1.0f}, {"field_marker", 0.8f}},
            {165, 155, 105, 255},
        });
        system.add({
            "camp_region",
            "Camp Region",
            0.8f,
            0.7f,
            0.65f,
            1.15f,
            {{"camp_marker", 3.0f}, {"field_marker", 2.0f}, {"tree_cluster", 1.2f}, {"rock", 1.0f}},
            {105, 165, 175, 255},
        });
        return system;
    }

    BiomeLoadResult BiomeSystem::loadFromYaml(const std::filesystem::path& path)
    {
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            BiomeSettings settings;
            if (const YAML::Node config = root["biome_settings"]) {
                settings.seed = config["seed"].as<uint32_t>(settings.seed);
                settings.regionScale = config["region_scale"].as<float>(settings.regionScale);
            }

            const YAML::Node biomes = root["biomes"];
            if (!biomes || !biomes.IsMap()) {
                return {false, "biomes must be a YAML map.", sampleDefaults()};
            }

            BiomeSystem system(settings);
            for (const auto& biomeNode : biomes) {
                const std::string id = biomeNode.first.as<std::string>(std::string{});
                const YAML::Node config = biomeNode.second;
                if (id.empty() || !config.IsMap()) {
                    continue;
                }

                BiomeDescriptor descriptor;
                descriptor.id = id;
                descriptor.displayName = config["display_name"].as<std::string>(id);
                descriptor.heightScale = config["height_scale"].as<float>(descriptor.heightScale);
                descriptor.rollingScale = config["rolling_scale"].as<float>(descriptor.rollingScale);
                descriptor.detailScale = config["detail_scale"].as<float>(descriptor.detailScale);
                descriptor.propDensity = config["prop_density"].as<float>(descriptor.propDensity);
                descriptor.archetypeWeights = readArchetypeWeights(config["archetypes"]);
                descriptor.terrainColor = readColor(config["terrain_color"], descriptor.terrainColor);
                system.add(std::move(descriptor));
            }

            if (system.all().empty()) {
                return {false, "biomes did not contain any valid biome descriptors.", sampleDefaults()};
            }

            return {true, {}, std::move(system)};
        } catch (const std::exception& error) {
            std::ostringstream message;
            message << "Failed to load biomes '" << path.string() << "': " << error.what();
            return {false, message.str(), sampleDefaults()};
        }
    }

    void BiomeSystem::add(BiomeDescriptor descriptor)
    {
        if (descriptor.id.empty()) {
            return;
        }

        descriptor.heightScale = std::max(descriptor.heightScale, 0.0f);
        descriptor.rollingScale = std::max(descriptor.rollingScale, 0.0f);
        descriptor.detailScale = std::max(descriptor.detailScale, 0.0f);
        descriptor.propDensity = std::max(descriptor.propDensity, 0.0f);

        const auto existing = std::ranges::find_if(biomes_, [&](const BiomeDescriptor& biome) {
            return biome.id == descriptor.id;
        });
        if (existing != biomes_.end()) {
            *existing = std::move(descriptor);
        } else {
            biomes_.push_back(std::move(descriptor));
        }
    }

    BiomeSample BiomeSystem::sample(float worldX, float worldZ) const
    {
        if (biomes_.empty()) {
            return {};
        }

        const float moisture = valueNoise(worldX, worldZ, 17u);
        const float roughness = valueNoise(worldX, worldZ, 41u);
        const float elevation = valueNoise(worldX, worldZ, 73u);

        uint32_t index = 0;
        if (roughness > 0.68f) {
            const auto rocky = std::ranges::find_if(biomes_, [](const BiomeDescriptor& biome) {
                return biome.id == "rocky";
            });
            index = rocky == biomes_.end() ? deterministicIndex(worldX, worldZ, 3u, static_cast<uint32_t>(biomes_.size())) : static_cast<uint32_t>(rocky - biomes_.begin());
        } else if (moisture < 0.28f) {
            const auto sparse = std::ranges::find_if(biomes_, [](const BiomeDescriptor& biome) {
                return biome.id == "sparse";
            });
            index = sparse == biomes_.end() ? deterministicIndex(worldX, worldZ, 5u, static_cast<uint32_t>(biomes_.size())) : static_cast<uint32_t>(sparse - biomes_.begin());
        } else if (elevation > 0.72f && moisture > 0.45f) {
            const auto camp = std::ranges::find_if(biomes_, [](const BiomeDescriptor& biome) {
                return biome.id == "camp_region";
            });
            index = camp == biomes_.end() ? deterministicIndex(worldX, worldZ, 7u, static_cast<uint32_t>(biomes_.size())) : static_cast<uint32_t>(camp - biomes_.begin());
        } else {
            const auto grassland = std::ranges::find_if(biomes_, [](const BiomeDescriptor& biome) {
                return biome.id == "grassland";
            });
            index = grassland == biomes_.end() ? deterministicIndex(worldX, worldZ, 11u, static_cast<uint32_t>(biomes_.size())) : static_cast<uint32_t>(grassland - biomes_.begin());
        }

        index = std::min<uint32_t>(index, static_cast<uint32_t>(biomes_.size() - 1));
        return {
            biomes_[index].id,
            1.0f,
            moisture,
            roughness,
            elevation,
        };
    }

    BiomeSample BiomeSystem::sampleChunk(ChunkCoord coord, float chunkSize) const
    {
        const float centerX = (static_cast<float>(coord.x) + 0.5f) * chunkSize;
        const float centerZ = (static_cast<float>(coord.z) + 0.5f) * chunkSize;
        return sample(centerX, centerZ);
    }

    const BiomeDescriptor* BiomeSystem::descriptor(std::string_view id) const
    {
        const auto biome = std::ranges::find_if(biomes_, [id](const BiomeDescriptor& descriptor) {
            return descriptor.id == id;
        });
        return biome == biomes_.end() ? nullptr : &*biome;
    }

    std::vector<const BiomeDescriptor*> BiomeSystem::all() const
    {
        std::vector<const BiomeDescriptor*> result;
        result.reserve(biomes_.size());
        for (const BiomeDescriptor& biome : biomes_) {
            result.push_back(&biome);
        }
        return result;
    }

    const BiomeSettings& BiomeSystem::settings() const
    {
        return settings_;
    }

    uint32_t BiomeSystem::deterministicIndex(float worldX, float worldZ, uint32_t salt, uint32_t count) const
    {
        if (count == 0u) {
            return 0u;
        }

        const int32_t x = static_cast<int32_t>(std::floor(worldX * settings_.regionScale));
        const int32_t z = static_cast<int32_t>(std::floor(worldZ * settings_.regionScale));
        return hashInts(x, z, settings_.seed, salt) % count;
    }

    float BiomeSystem::valueNoise(float worldX, float worldZ, uint32_t salt) const
    {
        const int32_t x = static_cast<int32_t>(std::floor(worldX * settings_.regionScale));
        const int32_t z = static_cast<int32_t>(std::floor(worldZ * settings_.regionScale));
        return normalizedHash(x, z, settings_.seed, salt);
    }
}
