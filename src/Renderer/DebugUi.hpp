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
        float propMaxDrawDistance = 160.0f;
        float terrainMaxDrawDistance = 280.0f;
        bool sceneCharacterExperimental = false;
        std::string sceneMode = "Procedural";
        std::string sceneStatus;
        bool hasAuthoredSceneDiagnostics = false;
        std::string authoredScenePath;
        std::string authoredSourceFormat;
        std::string authoredCacheStatus;
        std::string authoredCacheMessage;
        std::string authoredAsyncPhase;
        std::string authoredAsyncMessage;
        uint32_t authoredImportedNodes = 0;
        uint32_t authoredImportedMeshes = 0;
        uint32_t authoredImportedPrimitives = 0;
        uint32_t authoredImportedMaterials = 0;
        uint32_t authoredImportedTextures = 0;
        uint32_t authoredImportedLights = 0;
        uint32_t authoredCreatedMeshes = 0;
        uint32_t authoredCreatedMaterials = 0;
        uint32_t authoredCreatedInstances = 0;
        uint32_t authoredCreatedLights = 0;
        uint32_t authoredTextureLoaded = 0;
        uint32_t authoredTextureFailed = 0;
        uint32_t authoredTextureFallback = 0;
        uint64_t authoredTextureBytes = 0;
        uint32_t authoredTotalSectors = 0;
        uint32_t authoredLoadedSectors = 0;
        uint32_t authoredPendingLoadSectors = 0;
        uint32_t authoredPendingUnloadSectors = 0;
        uint32_t authoredFailedSectors = 0;
        uint64_t authoredSectorBytes = 0;
        uint32_t authoredActiveLights = 0;
        uint32_t authoredDisabledZeroLights = 0;
        uint32_t authoredSkippedOverBudgetLights = 0;
        uint32_t authoredWarnings = 0;
        float authoredCacheReadMs = 0.0f;
        float authoredImportMs = 0.0f;
        float authoredCacheWriteMs = 0.0f;
        uint32_t authoredAsyncQueued = 0;
        uint32_t authoredAsyncCompleted = 0;
        uint32_t authoredAsyncFailed = 0;
        uint32_t authoredAsyncPending = 0;
        bool hasAnimationDiagnostics = false;
        bool animationLoaded = false;
        bool animationEnabled = false;
        bool animationPlaying = false;
        bool animationLooping = true;
        std::string animationPath;
        std::string animationStatus;
        std::string animationAsyncPhase;
        std::string animationAsyncMessage;
        std::string animationCacheStatus;
        std::string animationCacheIdentity;
        std::string animationCacheMessage;
        std::string animationLastWarning;
        uint32_t animationAsyncQueued = 0;
        uint32_t animationAsyncCompleted = 0;
        uint32_t animationAsyncFailed = 0;
        uint32_t animationAsyncPending = 0;
        uint32_t animationCreatedSkinnedMeshCount = 0;
        uint32_t animationTextureFallbackCount = 0;
        uint32_t animationWarningCount = 0;
        float animationCacheReadMs = 0.0f;
        float animationImportMs = 0.0f;
        float animationCacheWriteMs = 0.0f;
        uint32_t animationClipIndex = 0;
        uint32_t animationClipCount = 0;
        uint32_t animationJointCount = 0;
        uint32_t animationSkinnedInstanceCount = 0;
        uint32_t animationSampledFrameCount = 0;
        uint32_t animationFailedPoseUpdateCount = 0;
        uint32_t animationCompletedCrossfadeCount = 0;
        uint32_t animationCrossfadeTargetClipIndex = UINT32_MAX;
        float animationTimeSeconds = 0.0f;
        float animationClipDurationSeconds = 0.0f;
        float animationPlaybackSpeed = 1.0f;
        float animationCrossfadeElapsedSeconds = 0.0f;
        float animationCrossfadeDurationSeconds = 0.0f;
        float animationCrossfadeWeight = 0.0f;
        bool animationCrossfadeActive = false;
    };

    struct TerrainLodDebugStats {
        std::array<uint32_t, TerrainLodDebugLevelCount> counts{};
        std::string cameraChunkDiagnostics;
        std::string hoveredChunkDiagnostics;
        std::string selectedChunkDiagnostics;
        std::string cameraBiomeGeneration;
        float activeNavMaxSlopeDegrees = 0.0f;
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

    struct NavigationAgentDebugSettings {
        float radius = 0.45f;
        float height = 1.8f;
        float maxSlopeDegrees = 45.0f;
        float maxClimb = 0.45f;
    };

    struct NavigationBuildDebugSettings {
        float cellSize = 0.3f;
        float cellHeight = 0.2f;
    };

    struct NavigationDebugStats {
        uint32_t loadedTiles = 0;
        uint32_t polygonEdgeCount = 0;
        uint32_t blockerVertexCount = 0;
        uint32_t blockerTriangleCount = 0;
        uint32_t connectivityChunkCount = 0;
        uint32_t connectivityPortalCount = 0;
        uint32_t connectivityConnectedPortalCount = 0;
        uint32_t connectivityPartialChunkCount = 0;
        uint32_t connectivityBlockedChunkCount = 0;
        std::string cameraChunkConnectivitySummary;
        std::string selectedChunkConnectivitySummary;
        std::string hoveredChunkTileSummary;
        std::string cameraChunkTileSummary;
        std::string selectedChunkTileSummary;
        std::string hoveredChunkPortalSummary;
        std::string cameraChunkPortalSummary;
        std::string selectedChunkPortalSummary;
        uint32_t worldGraphNodeCount = 0;
        uint32_t worldGraphEdgeCount = 0;
        uint32_t worldGraphBlockedEdgeCount = 0;
        bool hasWorldGraph = false;
        int32_t worldGraphCenterX = 0;
        int32_t worldGraphCenterZ = 0;
        std::string lastWorldRouteStatus;
        uint32_t lastWorldRouteChunkCount = 0;
        float lastWorldRouteCost = 0.0f;
        std::string lastWorldRouteMessage;
        bool hasLastRebuiltChunk = false;
        int32_t lastRebuiltChunkX = 0;
        int32_t lastRebuiltChunkZ = 0;
        bool selectedObjectNavBlocking = false;
        uint32_t selectedActorCount = 0;
        std::string selectedActorSummary;
        bool hasLastGroupDestination = false;
        glm::vec3 lastGroupDestination{};
        uint32_t lastGroupCommandSuccessCount = 0;
        uint32_t lastGroupCommandFailureCount = 0;
        std::string lastGroupCommandStatus;
        std::string lastGroupCommandFailureSummary;
        std::string lastBuildStatus;
        std::string lastBuildMessage;
        std::string lastQueryStatus;
        std::string lastQueryMessage;
        bool hasNearestPoint = false;
        std::string nearestPointStatus;
        glm::vec3 nearestPoint{};
        std::string currentPathStatus;
        uint32_t currentPathPointCount = 0;
        std::string selectedActorCommandSummary;
        std::string activeNavigationProfileId;
        NavigationAgentDebugSettings agent;
        NavigationBuildDebugSettings build;
        uint32_t navigationResolution = 0;
        std::string cacheIdentity;
        uint32_t cacheTileHits = 0;
        uint32_t cacheTileMisses = 0;
        uint32_t cacheTileStale = 0;
        uint32_t cacheTileWrites = 0;
        uint32_t cacheConnectivityHits = 0;
        uint32_t cacheConnectivityMisses = 0;
        uint32_t cacheConnectivityWrites = 0;
        uint32_t cacheGraphHits = 0;
        uint32_t cacheGraphMisses = 0;
        uint32_t cacheGraphWrites = 0;
        uint32_t navTileCacheHitsThisFrame = 0;
        uint32_t navTileCacheMissesThisFrame = 0;
        uint32_t navTileCacheStaleOrCorruptThisFrame = 0;
        uint32_t navTileCacheLoadFailuresThisFrame = 0;
        uint32_t cacheReadJobsQueuedThisFrame = 0;
        uint32_t cacheReadJobsCompletedThisFrame = 0;
        uint32_t cacheWriteJobsQueuedThisFrame = 0;
        uint32_t cacheWriteJobsCompletedThisFrame = 0;
        uint32_t cacheWriteJobsFailedThisFrame = 0;
        uint32_t navTileWorkerBuildsQueuedThisFrame = 0;
        uint32_t navTileWorkerBuildsCompletedThisFrame = 0;
        uint32_t navTileWorkerBuildsFailedThisFrame = 0;
        uint32_t graphWorkerBuildsQueuedThisFrame = 0;
        uint32_t graphWorkerBuildsCompletedThisFrame = 0;
        uint32_t graphWorkerBuildsFailedThisFrame = 0;
        float lastGraphWorkerBuildMs = 0.0f;
        int32_t lastGraphWorkerBuildCenterX = 0;
        int32_t lastGraphWorkerBuildCenterZ = 0;
        std::string lastGraphWorkerBuildMessage;
        uint32_t navTileReadyChunks = 0;
        uint32_t navTilePendingChunks = 0;
        uint32_t navTileFailedChunks = 0;
        uint32_t navTileSyncChunksThisFrame = 0;
        uint32_t navTileSyncDeferredChunks = 0;
        uint32_t connectivityChunksThisFrame = 0;
        uint32_t connectivityDeferredChunks = 0;
        uint32_t connectivityStepsThisFrame = 0;
        uint32_t connectivitySamplesThisFrame = 0;
        int32_t connectivityActiveChunkX = 0;
        int32_t connectivityActiveChunkZ = 0;
        std::string connectivityLastStepLabel;
        std::string cacheLastPath;
        std::string cacheLastMessage;
        float previousFrameCpuMs = 0.0f;
        float eventPollingMs = 0.0f;
        float inputMappingMs = 0.0f;
        float cameraUpdateMs = 0.0f;
        float chunkStreamingMs = 0.0f;
        float terrainLodMs = 0.0f;
        uint32_t terrainLodRebuildsThisFrame = 0;
        uint32_t terrainLodPendingRebuilds = 0;
        uint32_t terrainLodJobsQueuedThisFrame = 0;
        uint32_t terrainLodJobsCompletedThisFrame = 0;
        uint32_t terrainLodJobsFailedThisFrame = 0;
        uint32_t terrainLodCommitsThisFrame = 0;
        uint32_t terrainLodStaleResultsThisFrame = 0;
        uint32_t terrainLodCacheHitsThisFrame = 0;
        uint32_t terrainLodCacheMissesThisFrame = 0;
        uint32_t terrainLodCacheStaleThisFrame = 0;
        uint32_t terrainLodCacheCorruptThisFrame = 0;
        uint32_t terrainLodGeneratedThisFrame = 0;
        uint32_t terrainLodPendingJobs = 0;
        uint32_t terrainLodCompletedResults = 0;
        float lastTerrainLodBuildMs = 0.0f;
        std::string lastTerrainLodMessage;
        uint32_t visibilityChunksProcessedThisFrame = 0;
        uint32_t visibilityChunksDeferred = 0;
        uint32_t visibilityTerrainUpdatedThisFrame = 0;
        uint32_t visibilityInstancesUpdatedThisFrame = 0;
        bool visibilityFullReapplyPending = false;
        float budgetDrainMs = 0.0f;
        float navTileSyncMs = 0.0f;
        float connectivityMs = 0.0f;
        float worldGraphMs = 0.0f;
        float fixedUpdateMs = 0.0f;
        float worldSyncMs = 0.0f;
        float pickingMs = 0.0f;
        float nearestNavigationPointMs = 0.0f;
        float interactionMs = 0.0f;
        float debugPrimitiveEnqueueMs = 0.0f;
        float drawSubmissionMs = 0.0f;
        float debugPrimitiveDrawMs = 0.0f;
        DebugDrawStats debugDrawStats;
        float debugUiBuildMs = 0.0f;
        float debugUiRenderMs = 0.0f;
        float bgfxFrameMs = 0.0f;
        float postFrameRequestsMs = 0.0f;
        uint32_t asyncWorkerCount = 0;
        uint32_t asyncPendingChunkJobs = 0;
        uint32_t asyncCompletedChunks = 0;
        uint32_t asyncPendingUnloads = 0;
        uint32_t asyncCancelledJobs = 0;
        uint32_t asyncStaleJobs = 0;
        uint32_t asyncCommittedLoadsThisFrame = 0;
        uint32_t asyncCommittedUnloadsThisFrame = 0;
        float asyncAverageTerrainGenerationMs = 0.0f;
        float asyncMaxTerrainGenerationMs = 0.0f;
        float asyncAverageNavigationBuildMs = 0.0f;
        float asyncMaxNavigationBuildMs = 0.0f;
        float frameBudgetMs = 0.0f;
        float frameBudgetUsedMs = 0.0f;
        float frameBudgetOverrunMs = 0.0f;
        uint32_t frameBudgetItemsRun = 0;
        uint32_t frameBudgetItemsDeferred = 0;
        uint32_t mainThreadPendingWorkItems = 0;
        std::array<float, 5> frameBudgetCategoryMs{};
        float slowestBudgetItemMs = 0.0f;
        std::string slowestBudgetItemLabel;
        float longFrameThresholdMs = 50.0f;
        uint32_t longFrameCount = 0;
        float lastLongFrameMs = 0.0f;
        std::string lastLongFrameSummary;
    };

    struct NavigationDebugControls {
        bool rebuildVisibleTilesRequested = false;
        bool generateVisibleCacheRequested = false;
        bool refreshSelectedOrVisibleCacheRequested = false;
        bool rebuildConnectivityRequested = false;
        bool clearCacheStatsRequested = false;
        bool cacheEnabled = true;
        bool cacheWriteThrough = false;
        bool asyncTerrainEnabled = true;
        bool asyncNavigationEnabled = true;
        bool mainThreadBudgetEnabled = true;
        float mainThreadBudgetMs = 2.0f;
        uint32_t propSpawnBatchSize = 8;
        uint32_t terrainLodRebuildsPerFrame = 1;
        uint32_t visibilityReapplyChunksPerStep = 8;
        uint32_t navTileSyncChunksPerFrame = 8;
        uint32_t connectivityChunksPerFrame = 8;
        uint32_t connectivitySamplesPerStep = 9;
        uint32_t worldGraphRecenterThresholdChunks = 8;
        uint32_t workerThreadCount = 1;
        uint32_t portalSamplesPerEdge = 9;
        float portalEdgeInset = 1.5f;
        float portalEdgeBandWidth = 4.0f;
        float portalMergeDistance = 2.0f;
        float portalNeighborLinkDistance = 6.0f;
        NavigationAgentDebugSettings agent;
        NavigationBuildDebugSettings build;
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
        std::string pathStatus;
        glm::vec3 pathDestination{};
        uint32_t pathPointCount = 0;
        uint32_t pathCurrentCorner = 0;
        float pathArrivalRadius = 0.0f;
        float pathCornerAdvanceRadius = 0.0f;
        uint32_t pathBlockedTicks = 0;
        uint32_t pathRepathAttemptsUsed = 0;
        std::string pathLastQueryStatus;
        std::string pathLastQueryMessage;
        std::string routeStatus;
        uint32_t routeCurrentWaypoint = 0;
        uint32_t routeWaypointCount = 0;
        glm::vec3 routeFinalDestination{};
        std::string routeMessage;
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
        const NavigationDebugStats& navigation = {},
        NavigationDebugControls* navigationControls = nullptr,
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
