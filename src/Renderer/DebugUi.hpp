#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include "Renderer/Scene.hpp"

namespace Renderer::DebugUi {
    constexpr uint32_t TerrainLodDebugLevelCount = 6;

    struct RendererDebugSettings {
        uint32_t layerMask = static_cast<uint32_t>(RenderLayer::All);
        bool enableDistanceCulling = true;
        float propMaxDrawDistance = 0.0f;
        float terrainMaxDrawDistance = 0.0f;
    };

    struct TerrainLodDebugStats {
        std::array<uint32_t, TerrainLodDebugLevelCount> counts{};
    };

    struct SpatialRegistryDebugStats {
        uint32_t activeCells = 0;
        uint32_t registeredObjects = 0;
        int32_t currentCellX = 0;
        int32_t currentCellZ = 0;
        uint32_t objectsInCurrentCell = 0;
        uint32_t objectsNearCamera = 0;
        float nearQueryRadius = 0.0f;
    };

    struct DebugPickingStats {
        glm::vec2 mousePosition{};
        glm::vec3 rayOrigin{};
        glm::vec3 rayDirection{};
        bool hasHoveredObject = false;
        uint32_t hoveredObjectId = UINT32_MAX;
        glm::vec3 hoveredObjectPosition{};
        float hoveredObjectDistance = 0.0f;
        int32_t hoveredObjectCellX = 0;
        int32_t hoveredObjectCellZ = 0;
        bool hasSelectedObject = false;
        uint32_t selectedObjectId = UINT32_MAX;
        glm::vec3 selectedObjectPosition{};
        bool hasTerrainHit = false;
        glm::vec3 terrainHitPosition{};
        float terrainHitDistance = 0.0f;
        int32_t terrainHitChunkX = 0;
        int32_t terrainHitChunkZ = 0;
    };

    struct WorldSaveDebugControls {
        std::array<char, 260> path{"saves/debug_world.yaml"};
        bool saveRequested = false;
        bool loadRequested = false;
        std::string status;
    };

    struct PlayerActorDebugStats {
        bool valid = false;
        uint32_t worldObjectId = UINT32_MAX;
        glm::vec3 position{};
        glm::vec3 velocity{};
        float facingRadians = 0.0f;
        bool hasGroundHeight = false;
        float groundHeight = 0.0f;
    };

    bool init(SDL_Window* window);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame(uint16_t width, uint16_t height);
    void showRendererDebug(
        const SceneDrawStats& stats,
        RendererDebugSettings& settings,
        AtmosphereSettings& atmosphere,
        const TerrainLodDebugStats& terrainLods = {},
        const SpatialRegistryDebugStats& spatial = {},
        const DebugPickingStats& picking = {},
        WorldSaveDebugControls* worldSave = nullptr,
        const PlayerActorDebugStats& player = {}
    );
    void render(bgfx::ViewId viewId, uint16_t width, uint16_t height);
}
