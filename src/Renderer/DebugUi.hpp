#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "Renderer/Scene.hpp"

namespace Renderer::DebugUi {
    struct RendererDebugSettings {
        uint32_t layerMask = static_cast<uint32_t>(RenderLayer::All);
        bool enableDistanceCulling = true;
        float propMaxDrawDistance = 160.0f;
        float terrainMaxDrawDistance = 280.0f;
        std::string sceneMode;
        std::string sceneStatus;
    };

    struct PerformanceDebugStats {
        bool startupComplete = false;
        float startupWorkMs = 0.0f;
        std::string startupStatus;
        float previousFrameCpuMs = 0.0f;
        float eventPollingMs = 0.0f;
        float inputMappingMs = 0.0f;
        float cameraUpdateMs = 0.0f;
        float sceneFixedTickMs = 0.0f;
        float sceneFrameTickMs = 0.0f;
        float debugPrimitiveEnqueueMs = 0.0f;
        float drawSubmissionMs = 0.0f;
        float debugPrimitiveDrawMs = 0.0f;
        float debugUiBuildMs = 0.0f;
        float debugUiRenderMs = 0.0f;
        float bgfxFrameMs = 0.0f;
        std::array<float, 8> scenePhaseMs{};
        std::array<uint32_t, 8> scenePhaseCallbacks{};
    };

    struct ModernSceneDebugStats {
        std::string mode;
        std::string status;
        uint32_t actorCount = 0;
        uint32_t componentCount = 0;
        uint64_t frameIndex = 0;
        uint32_t fixedStepIndex = 0;
        uint32_t registeredSystemCount = 0;
        uint32_t enabledSystemCount = 0;
        uint32_t skippedPhaseCount = 0;
        uint32_t skippedCallbackCount = 0;
        uint32_t renderMeshComponents = 0;
        uint32_t renderSkinnedComponents = 0;
        uint32_t renderLightComponents = 0;
        uint32_t renderCameraComponents = 0;
        uint32_t liveMeshInstances = 0;
        uint32_t liveSkinnedInstances = 0;
        uint32_t liveRendererLights = 0;
        uint32_t authoredStaticAssets = 0;
        uint32_t animatedAssets = 0;
        uint32_t warnings = 0;
    };

    struct ModernTerrainDebugStats {
        bool loadedHeightmap = false;
        uint32_t sourceCount = 0;
        uint32_t loadedChunkCount = 0;
        uint32_t renderedChunkCount = 0;
        uint32_t navTileCount = 0;
        uint32_t physicsColliderCount = 0;
        glm::vec3 boundsMin{};
        glm::vec3 boundsMax{};
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        float averageHeight = 0.0f;
        uint64_t memoryEstimateBytes = 0;
        std::string diagnostics;
    };

    struct ModernNavigationDebugStats {
        uint32_t loadedTiles = 0;
        uint32_t polygonEdgeCount = 0;
        uint32_t sceneSourceCount = 0;
        uint32_t sceneTriangleCount = 0;
        uint32_t sceneAppendedTriangleCount = 0;
        uint32_t sceneCulledByBounds = 0;
        uint32_t sceneCulledBySlope = 0;
        uint32_t cacheHits = 0;
        uint32_t cacheMisses = 0;
        uint32_t cacheStale = 0;
        uint32_t cacheWrites = 0;
        std::string lastBuildStatus;
        std::string lastBuildMessage;
        std::string lastQueryStatus;
        std::string lastQueryMessage;
        std::string cacheIdentity;
    };

    struct ModernPhysicsDebugStats {
        uint32_t bodyCount = 0;
        uint32_t colliderCount = 0;
        uint32_t enabledBodyCount = 0;
        uint32_t invalidOwnerCleanupCount = 0;
        uint32_t raycastCount = 0;
        uint32_t sweepCount = 0;
        uint32_t overlapCount = 0;
        uint32_t closestPointCount = 0;
        uint64_t lastStepMicroseconds = 0;
        uint64_t lastQueryMicroseconds = 0;
        std::string lastStatus;
        std::string lastMessage;
        uint32_t warnings = 0;
    };

    struct ModernCharacterDebugStats {
        bool hasCharacter = false;
        bool enabled = false;
        std::string mode;
        bool grounded = false;
        glm::vec3 velocity{};
        glm::vec3 floorNormal{};
        float floorDistance = 0.0f;
        uint32_t activePathPointCount = 0;
        uint32_t activeWaypointIndex = 0;
        uint32_t characterCount = 0;
        uint32_t failedSweepCount = 0;
        uint32_t pathQueryCount = 0;
        uint32_t invalidOwnerCleanupCount = 0;
        uint64_t lastUpdateMicroseconds = 0;
        std::string lastStatus;
        std::string lastMessage;
    };

    struct DebugVisualizationDebugStats {
        bool enabled = true;
        uint32_t generatedLines = 0;
        uint32_t submittedLines = 0;
        uint32_t clippedLines = 0;
        uint32_t cappedLines = 0;
        uint32_t lastFramePrimitiveBufferSize = 0;
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

    struct ModernNavigationDebugControls {
        bool rebuildNavigationRequested = false;
        bool clearCacheStatsRequested = false;
    };

    struct ModernDebugUiState {
        PerformanceDebugStats performance;
        ModernSceneDebugStats scene;
        ModernTerrainDebugStats terrain;
        ModernNavigationDebugStats navigation;
        ModernPhysicsDebugStats physics;
        ModernCharacterDebugStats character;
        DebugVisualizationDebugStats debugVisualization;
    };

    bool init(SDL_Window* window);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame(uint16_t width, uint16_t height);
    bool wantsMouseCapture();
    bool wantsKeyboardCapture();
    void showModernDebug(
        const SceneDrawStats& stats,
        RendererDebugSettings& settings,
        AtmosphereSettings& atmosphere,
        DebugDrawSettings& debugDraw,
        const ModernDebugUiState& state,
        const CameraDebugStats& camera = {},
        CameraDebugControls* cameraControls = nullptr,
        ModernNavigationDebugControls* navigationControls = nullptr);
    void render(bgfx::ViewId viewId, uint16_t width, uint16_t height);
}
