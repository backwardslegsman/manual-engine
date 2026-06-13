#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct ProceduralPropArchetype {
        std::string id;
        glm::vec3 scale{1.0f};
        Renderer::Aabb localBounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        float terrainYOffset = 0.0f;
        glm::vec3 angularVelocity{};
        uint32_t materialSlot = 0;
    };

    struct ProceduralPropSpawn {
        std::string archetypeId;
        glm::vec3 localPosition{};
        glm::vec3 scale{1.0f};
        Renderer::Aabb localBounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        float terrainYOffset = 0.0f;
        glm::vec3 angularVelocity{};
        uint32_t materialSlot = 0;
    };

    struct ProceduralChunkContentSettings {
        float chunkSize = 16.0f;
    };

    // Small deterministic content planner for demo/open-world chunk props.
    // It describes what to spawn; App still creates renderer/world resources.
    class ProceduralChunkContentConfig {
    public:
        explicit ProceduralChunkContentConfig(ProceduralChunkContentSettings settings = {});

        static ProceduralChunkContentConfig sampleOpenWorldConfig();

        void addArchetype(const ProceduralPropArchetype& archetype);
        std::vector<ProceduralPropSpawn> propsForChunk(ChunkCoord coord) const;
        const ProceduralChunkContentSettings& settings() const;

    private:
        const ProceduralPropArchetype& archetypeFor(ChunkCoord coord, uint32_t slot) const;
        uint32_t deterministicIndex(ChunkCoord coord, uint32_t salt, uint32_t count) const;

        ProceduralChunkContentSettings settings_;
        std::vector<ProceduralPropArchetype> archetypes_;
    };
}
