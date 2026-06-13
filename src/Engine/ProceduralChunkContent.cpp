#include "Engine/ProceduralChunkContent.hpp"

#include <algorithm>

namespace Engine {
    namespace {
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

    ProceduralChunkContentConfig ProceduralChunkContentConfig::sampleOpenWorldConfig()
    {
        ProceduralChunkContentConfig config{{16.0f}};
        config.addArchetype({
            "tree_cluster",
            {0.55f, 1.15f, 0.55f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            1.15f,
            {0.0f, 0.10f, 0.0f},
            1,
        });
        config.addArchetype({
            "rock",
            {0.75f, 0.45f, 0.70f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            0.45f,
            {0.0f, 0.05f, 0.0f},
            3,
        });
        config.addArchetype({
            "camp_marker",
            {0.85f, 0.85f, 0.85f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            0.85f,
            {0.0f, 0.25f, 0.0f},
            2,
        });
        config.addArchetype({
            "field_marker",
            {0.65f, 0.65f, 0.65f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            0.65f,
            {0.0f, 0.15f, 0.0f},
            0,
        });
        return config;
    }

    void ProceduralChunkContentConfig::addArchetype(const ProceduralPropArchetype& archetype)
    {
        archetypes_.push_back(archetype);
    }

    std::vector<ProceduralPropSpawn> ProceduralChunkContentConfig::propsForChunk(ChunkCoord coord) const
    {
        if (archetypes_.empty()) {
            return {};
        }

        const glm::vec3 anchors[] = {
            {4.0f, 0.0f, 4.0f},
            {12.0f, 0.0f, 4.0f},
            {4.0f, 0.0f, 12.0f},
            {12.0f, 0.0f, 12.0f},
        };

        std::vector<ProceduralPropSpawn> spawns;
        spawns.reserve(5);
        for (uint32_t index = 0; index < std::size(anchors); ++index) {
            const ProceduralPropArchetype& archetype = archetypeFor(coord, index);
            spawns.push_back({
                archetype.id,
                anchors[index],
                archetype.scale,
                archetype.localBounds,
                archetype.terrainYOffset,
                archetype.angularVelocity + glm::vec3{0.0f, 0.03f * static_cast<float>(index), 0.0f},
                archetype.materialSlot,
            });
        }

        if (deterministicIndex(coord, 91u, 7u) == 0u) {
            const ProceduralPropArchetype& archetype = archetypes_[deterministicIndex(coord, 29u, static_cast<uint32_t>(archetypes_.size()))];
            spawns.push_back({
                archetype.id,
                {8.0f, 0.0f, 8.0f},
                archetype.scale * 1.2f,
                archetype.localBounds,
                archetype.terrainYOffset * 1.2f,
                archetype.angularVelocity,
                archetype.materialSlot,
            });
        }

        return spawns;
    }

    const ProceduralChunkContentSettings& ProceduralChunkContentConfig::settings() const
    {
        return settings_;
    }

    const ProceduralPropArchetype& ProceduralChunkContentConfig::archetypeFor(ChunkCoord coord, uint32_t slot) const
    {
        return archetypes_[deterministicIndex(coord, slot + 11u, static_cast<uint32_t>(archetypes_.size()))];
    }

    uint32_t ProceduralChunkContentConfig::deterministicIndex(ChunkCoord coord, uint32_t salt, uint32_t count) const
    {
        return count == 0u ? 0u : mixCoord(coord, salt) % count;
    }
}
