#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Biome.hpp"
#include "Engine/ObjectArchetype.hpp"
#include "Engine/ObjectId.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct ProceduralPropSpawn {
        // Stable persistence key for this regenerated baseline prop. Removed
        // object records and persistent transform overrides compare this ID
        // against future propsForChunk() output.
        ObjectId objectId;
        // Archetype IDs and local slots are part of the procedural ID contract.
        // Treat renames or slot changes as save-breaking without migration.
        std::string archetypeId;
        uint32_t localSlot = 0;
        glm::vec3 localPosition{};
        glm::vec3 scale{1.0f};
        Renderer::Aabb localBounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        float terrainYOffset = 0.0f;
        glm::vec3 angularVelocity{};
    };

    struct ProceduralChunkContentSettings {
        float chunkSize = 16.0f;
        // Optional external biome rules. The caller owns this system and must
        // keep it alive for the content config lifetime.
        const BiomeSystem* biomes = nullptr;
    };

    // Small deterministic content planner for demo/open-world chunk props.
    // It describes what to spawn; App still creates renderer/world resources.
    // Slot numbering and archetype IDs are part of the save-facing procedural
    // identity contract.
    class ProceduralChunkContentConfig {
    public:
        explicit ProceduralChunkContentConfig(ProceduralChunkContentSettings settings = {});

        static ProceduralChunkContentConfig sampleOpenWorldConfig(
            const ObjectArchetypeCatalog& archetypes,
            const BiomeSystem* biomes = nullptr);

        void addArchetype(const ObjectArchetypeDescriptor& archetype);
        std::vector<ProceduralPropSpawn> propsForChunk(ChunkCoord coord) const;
        const ObjectArchetypeDescriptor* archetypeById(std::string_view id) const;
        const ProceduralChunkContentSettings& settings() const;

    private:
        const ObjectArchetypeDescriptor& archetypeFor(ChunkCoord coord, uint32_t slot) const;
        const ObjectArchetypeDescriptor& archetypeForBiome(const BiomeDescriptor* biome, ChunkCoord coord, uint32_t slot) const;
        uint32_t deterministicIndex(ChunkCoord coord, uint32_t salt, uint32_t count) const;
        float deterministicUnit(ChunkCoord coord, uint32_t salt) const;

        ProceduralChunkContentSettings settings_;
        std::vector<ObjectArchetypeDescriptor> archetypes_;
    };
}
