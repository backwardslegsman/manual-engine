#include "Engine/ProceduralChunkContent.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {
    namespace {
        constexpr uint32_t AnchorSlotStart = 0;
        constexpr uint32_t AnchorSlotCount = 4;
        constexpr uint32_t BonusSlot = AnchorSlotStart + AnchorSlotCount;

        uint32_t mixCoord(ChunkCoord coord, uint32_t salt)
        {
            uint32_t value = static_cast<uint32_t>(coord.x) * 0x9e3779b1u;
            value ^= static_cast<uint32_t>(coord.z) * 0x85ebca6bu;
            value ^= salt * 0xc2b2ae35u;
            value ^= value >> 16u;
            value *= 0x7feb352du;
            value ^= value >> 15u;
            value *= 0x846ca68bu;
            value ^= value >> 16u;
            return value;
        }
    }

    ProceduralChunkContentConfig::ProceduralChunkContentConfig(ProceduralChunkContentSettings settings)
        : settings_(settings)
    {
        settings_.chunkSize = std::max(settings_.chunkSize, 1.0f);
    }

    ProceduralChunkContentConfig ProceduralChunkContentConfig::sampleOpenWorldConfig(
        const ObjectArchetypeCatalog& archetypes,
        const BiomeSystem* biomes)
    {
        ProceduralChunkContentConfig config{{16.0f, biomes}};
        for (const ObjectArchetypeDescriptor* archetype : archetypes.all()) {
            if (archetype) {
                config.addArchetype(*archetype);
            }
        }
        return config;
    }

    void ProceduralChunkContentConfig::addArchetype(const ObjectArchetypeDescriptor& archetype)
    {
        archetypes_.push_back(archetype);
    }

    std::vector<ProceduralPropSpawn> ProceduralChunkContentConfig::propsForChunk(ChunkCoord coord) const
    {
        if (archetypes_.empty()) {
            return {};
        }

        const BiomeSample biomeSample = settings_.biomes
            ? settings_.biomes->sampleChunk(coord, settings_.chunkSize)
            : BiomeSample{};
        const BiomeDescriptor* biome = settings_.biomes ? settings_.biomes->descriptor(biomeSample.id) : nullptr;
        const float propDensity = biome ? std::clamp(biome->propDensity, 0.0f, 1.0f) : 1.0f;

        const glm::vec3 anchors[] = {
            {4.0f, 0.0f, 4.0f},
            {12.0f, 0.0f, 4.0f},
            {4.0f, 0.0f, 12.0f},
            {12.0f, 0.0f, 12.0f},
        };

        std::vector<ProceduralPropSpawn> spawns;
        spawns.reserve(5);
        static_assert(std::size(anchors) == AnchorSlotCount);
        for (uint32_t index = 0; index < AnchorSlotCount; ++index) {
            const uint32_t localSlot = AnchorSlotStart + index;
            if (deterministicUnit(coord, 300u + localSlot) > propDensity) {
                continue;
            }

            const ObjectArchetypeDescriptor& archetype = archetypeForBiome(biome, coord, localSlot);
            spawns.push_back({
                ObjectId::proceduralProp(coord, archetype.id, localSlot),
                archetype.id,
                localSlot,
                anchors[index],
                archetype.scale,
                archetype.localBounds,
                archetype.terrainYOffset,
                archetype.angularVelocity + glm::vec3{0.0f, 0.03f * static_cast<float>(index), 0.0f},
            });
        }

        const uint32_t bonusChance = static_cast<uint32_t>(std::max(2.0f, std::round(7.0f / std::max(propDensity, 0.25f))));
        if (deterministicIndex(coord, 91u, bonusChance) == 0u) {
            const ObjectArchetypeDescriptor& archetype = archetypeForBiome(biome, coord, 29u);
            spawns.push_back({
                ObjectId::proceduralProp(coord, archetype.id, BonusSlot),
                archetype.id,
                BonusSlot,
                {8.0f, 0.0f, 8.0f},
                archetype.scale * 1.2f,
                archetype.localBounds,
                archetype.terrainYOffset * 1.2f,
                archetype.angularVelocity,
            });
        }

        return spawns;
    }

    const ObjectArchetypeDescriptor* ProceduralChunkContentConfig::archetypeById(std::string_view id) const
    {
        const auto archetypeIt = std::ranges::find_if(archetypes_, [id](const ObjectArchetypeDescriptor& archetype) {
            return archetype.id == id;
        });
        return archetypeIt == archetypes_.end() ? nullptr : &*archetypeIt;
    }

    const ProceduralChunkContentSettings& ProceduralChunkContentConfig::settings() const
    {
        return settings_;
    }

    const ObjectArchetypeDescriptor& ProceduralChunkContentConfig::archetypeFor(ChunkCoord coord, uint32_t slot) const
    {
        return archetypes_[deterministicIndex(coord, slot + 11u, static_cast<uint32_t>(archetypes_.size()))];
    }

    const ObjectArchetypeDescriptor& ProceduralChunkContentConfig::archetypeForBiome(
        const BiomeDescriptor* biome,
        ChunkCoord coord,
        uint32_t slot) const
    {
        if (!biome || biome->archetypeWeights.empty()) {
            return archetypeFor(coord, slot);
        }

        float totalWeight = 0.0f;
        for (const BiomeArchetypeWeight& weight : biome->archetypeWeights) {
            if (weight.weight > 0.0f && archetypeById(weight.archetypeId)) {
                totalWeight += weight.weight;
            }
        }

        if (totalWeight <= 0.0f) {
            return archetypeFor(coord, slot);
        }

        const float pick = deterministicUnit(coord, slot + 101u) * totalWeight;
        float cursor = 0.0f;
        for (const BiomeArchetypeWeight& weight : biome->archetypeWeights) {
            const ObjectArchetypeDescriptor* archetype = archetypeById(weight.archetypeId);
            if (!archetype || weight.weight <= 0.0f) {
                continue;
            }

            cursor += weight.weight;
            if (pick <= cursor) {
                return *archetype;
            }
        }

        return archetypeFor(coord, slot);
    }

    uint32_t ProceduralChunkContentConfig::deterministicIndex(ChunkCoord coord, uint32_t salt, uint32_t count) const
    {
        return count == 0u ? 0u : mixCoord(coord, salt) % count;
    }

    float ProceduralChunkContentConfig::deterministicUnit(ChunkCoord coord, uint32_t salt) const
    {
        return static_cast<float>(mixCoord(coord, salt) & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
    }
}
