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

    struct CameraDebugStats {
        bool followMode = false;
        bool hasTarget = false;
        glm::vec3 pivot{};
        glm::vec3 targetPosition{};
        glm::vec3 followOffset{};
        float followSmoothing = 0.0f;
        float maxFollowLag = 0.0f;
    };

    struct CameraDebugControls {
        bool setFreeModeRequested = false;
        bool setFollowModeRequested = false;
        bool recenterRequested = false;
        float followSmoothing = 0.0f;
        float maxFollowLag = 0.0f;
    };

    struct BiomeDebugStats {
        bool valid = false;
        std::string cameraBiomeId;
        std::string cameraBiomeDisplayName;
        int32_t cameraChunkX = 0;
        int32_t cameraChunkZ = 0;
        std::string cameraTerrainMaterialBiomeId;
        std::array<uint8_t, 4> cameraTerrainColor{};
        bool cameraTerrainUsesFallback = false;
        bool hasPlayerBiome = false;
        std::string playerBiomeId;
        int32_t playerChunkX = 0;
        int32_t playerChunkZ = 0;
        bool hasHoveredBiome = false;
        std::string hoveredBiomeId;
        int32_t hoveredChunkX = 0;
        int32_t hoveredChunkZ = 0;
        bool hasTerrainHitBiome = false;
        std::string terrainHitBiomeId;
        int32_t terrainHitChunkX = 0;
        int32_t terrainHitChunkZ = 0;
        float moisture = 0.0f;
        float roughness = 0.0f;
        float elevation = 0.0f;
    };

    struct DebugPickingStats {
        glm::vec2 mousePosition{};
        glm::vec3 rayOrigin{};
        glm::vec3 rayDirection{};
        bool hasHoveredObject = false;
        uint32_t hoveredObjectId = UINT32_MAX;
        std::string hoveredStableId;
        glm::vec3 hoveredObjectPosition{};
        float hoveredObjectDistance = 0.0f;
        int32_t hoveredObjectCellX = 0;
        int32_t hoveredObjectCellZ = 0;
        bool hasSelectedObject = false;
        uint32_t selectedObjectId = UINT32_MAX;
        std::string selectedStableId;
        bool selectedIsProcedural = false;
        std::string selectedArchetypeId;
        std::string selectedArchetypeDisplayName;
        std::string selectedArchetypeTags;
        uint32_t selectedLocalSlot = 0;
        glm::vec3 selectedObjectPosition{};
        glm::vec3 selectedObjectRotation{};
        glm::vec3 selectedObjectScale{1.0f};
        int32_t selectedOwnerChunkX = 0;
        int32_t selectedOwnerChunkZ = 0;
        bool selectedEditable = false;
        bool selectedIsCustom = false;
        bool selectedHasPersistentOverride = false;
        bool selectedCanReset = false;
        bool hasTerrainHit = false;
        glm::vec3 terrainHitPosition{};
        float terrainHitDistance = 0.0f;
        int32_t terrainHitChunkX = 0;
        int32_t terrainHitChunkZ = 0;
    };

    struct InteractionDebugStats {
        bool hasLastInteraction = false;
        std::string action;
        std::string target;
        std::string outcome;
        std::string stableId;
        std::string archetypeId;
        std::string archetypeDisplayName;
        std::string archetypeTags;
        int32_t chunkX = 0;
        int32_t chunkZ = 0;
        glm::vec3 position{};
        float distance = 0.0f;
        std::string resourceId;
        uint32_t resourceAmount = 0;
        std::string status;
    };

    struct WorldSaveDebugControls {
        std::array<char, 260> path{"saves/debug_world.yaml"};
        bool saveRequested = false;
        bool loadRequested = false;
        bool removeSelectedRequested = false;
        bool persistSelectedRequested = false;
        bool resetSelectedOverrideRequested = false;
        bool moveSelectedToTerrainRequested = false;
        bool nudgeSelectedPositiveXRequested = false;
        bool nudgeSelectedNegativeXRequested = false;
        bool nudgeSelectedPositiveYRequested = false;
        bool nudgeSelectedNegativeYRequested = false;
        bool nudgeSelectedPositiveZRequested = false;
        bool nudgeSelectedNegativeZRequested = false;
        bool rotateSelectedPositiveYawRequested = false;
        bool rotateSelectedNegativeYawRequested = false;
        bool placeArchetypeRequested = false;
        float editMoveStep = 1.0f;
        float editRotateStepDegrees = 15.0f;
        std::array<char, 64> placeArchetypeId{"camp_marker"};
        std::string status;
    };

    struct PlayerActorDebugStats {
        bool valid = false;
        uint32_t worldObjectId = UINT32_MAX;
        std::string stableId;
        glm::vec3 position{};
        glm::vec3 velocity{};
        float facingRadians = 0.0f;
        bool hasGroundHeight = false;
        float groundHeight = 0.0f;
        bool collisionEnabled = false;
        float collisionRadius = 0.0f;
        float collisionHeight = 0.0f;
        bool blockedX = false;
        bool blockedZ = false;
        uint32_t collisionHitCount = 0;
        uint32_t firstBlockingObjectId = UINT32_MAX;
        std::string firstBlockingStableId;
    };

    bool init(SDL_Window* window);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame(uint16_t width, uint16_t height);
    bool wantsMouseCapture();
    bool wantsKeyboardCapture();
    void showRendererDebug(
        const SceneDrawStats& stats,
        RendererDebugSettings& settings,
        AtmosphereSettings& atmosphere,
        DebugDrawSettings& debugDraw,
        const TerrainLodDebugStats& terrainLods = {},
        const SpatialRegistryDebugStats& spatial = {},
        const CameraDebugStats& camera = {},
        CameraDebugControls* cameraControls = nullptr,
        const BiomeDebugStats& biome = {},
        const DebugPickingStats& picking = {},
        const InteractionDebugStats& interaction = {},
        WorldSaveDebugControls* worldSave = nullptr,
        const PlayerActorDebugStats& player = {}
    );
    void render(bgfx::ViewId viewId, uint16_t width, uint16_t height);
}
