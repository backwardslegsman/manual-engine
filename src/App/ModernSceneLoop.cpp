#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Scene/AnimatedSceneAdapter.hpp"
#include "Engine/Scene/AuthoredSceneAdapter.hpp"
#include "Engine/Scene/RendererSceneRenderBackend.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"
#include "Engine/SceneCharacterMovement.hpp"
#include "Engine/Biome.hpp"
#include "Engine/CursorTrace.hpp"
#include "Engine/DebugVisualization.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/FixedStepLoop.hpp"
#include "Engine/FrameBudget.hpp"
#include "Engine/InputMapping.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/NavigationCacheAsync.hpp"
#include "Engine/NavigationCache.hpp"
#include "Engine/NavigationConnectivity.hpp"
#include "Engine/NavigationProfile.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/OpenWorldStreaming.hpp"
#include "Engine/OpenWorldStreamingRuntime.hpp"
#include "Engine/OrbitCamera.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainImport.hpp"
#include "Engine/TerrainMaterialRenderAdapter.hpp"
#include "Engine/TerrainSampleMaterials.hpp"
#include "Engine/TerrainNavigationAdapter.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"
#include "Engine/TerrainRenderLodAdapter.hpp"
#include "Engine/input.hpp"
#include "ModernSceneLoop.hpp"
#include "Renderer/DebugUi.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

#ifndef MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#define MANUAL_ENGINE_ENABLE_DEBUG_TOOLS 1
#endif

namespace {
    constexpr bool BuildDebugToolsEnabled = MANUAL_ENGINE_ENABLE_DEBUG_TOOLS != 0;
    constexpr uint32_t WindowResetFlags = BGFX_RESET_VSYNC;

    template <typename Function>
    float measureMilliseconds(Function&& function)
    {
        const auto start = std::chrono::steady_clock::now();
        function();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(end - start).count();
    }

    struct FramePerformanceTimings {
        float previousFrameCpuMs = 0.0f;
        float eventPollingMs = 0.0f;
        float inputMappingMs = 0.0f;
        float cameraUpdateMs = 0.0f;
        float sceneFixedTickMs = 0.0f;
        float sceneFrameTickMs = 0.0f;
        float chunkStreamingMs = 0.0f;
        float terrainLodMs = 0.0f;
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
        float debugUiBuildMs = 0.0f;
        float debugUiRenderMs = 0.0f;
        float bgfxFrameMs = 0.0f;
        float postFrameRequestsMs = 0.0f;
    };

    struct NavigationSyncResult {
        bool tileSetChanged = false;
        bool blockerStatsUpdated = false;
        bool hasMoreWork = false;
        uint32_t processedChunks = 0;
        uint32_t deferredChunks = 0;
    };

    struct ConnectivityRebuildResult {
        bool hasMoreWork = false;
        uint32_t processedChunks = 0;
        uint32_t deferredChunks = 0;
        uint32_t samplesProcessed = 0;
        Engine::ChunkCoord activeChunk{};
        std::string stepLabel;
    };

    struct LongFrameDiagnostics {
        float thresholdMs = 50.0f;
        uint32_t count = 0;
        float lastFrameMs = 0.0f;
        std::string lastSummary;
    };

    std::string_view budgetCategoryName(Engine::BudgetCategory category)
    {
        switch (category) {
            case Engine::BudgetCategory::StreamingCommit:
                return "streaming";
            case Engine::BudgetCategory::NavigationCommit:
                return "navigation";
            case Engine::BudgetCategory::DerivedRebuild:
                return "derived";
            case Engine::BudgetCategory::Debug:
                return "debug";
            case Engine::BudgetCategory::General:
                return "general";
            case Engine::BudgetCategory::Count:
                break;
        }
        return "unknown";
    }

    std::string makeLongFrameSummary(
        const FramePerformanceTimings& timings,
        const Engine::FrameBudgetStats& budgetStats,
        uint32_t pendingMainThreadWork)
    {
        char buffer[1024]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "bgfx %.2f, budget %.2f, navSync %.2f, conn %.2f, graph %.2f, draw %.2f, debugUi %.2f/%.2f, "
            "post %.2f, slow item %.2fms [%s] %s, pending work %u",
            timings.bgfxFrameMs,
            timings.budgetDrainMs,
            timings.navTileSyncMs,
            timings.connectivityMs,
            timings.worldGraphMs,
            timings.drawSubmissionMs,
            timings.debugUiBuildMs,
            timings.debugUiRenderMs,
            timings.postFrameRequestsMs,
            budgetStats.slowestItemMs,
            budgetCategoryName(budgetStats.slowestItemCategory).data(),
            budgetStats.slowestItemLabel.empty() ? "<none>" : budgetStats.slowestItemLabel.c_str(),
            pendingMainThreadWork);
        return buffer;
    }

    struct DebugPrimitiveBudget {
        const Renderer::DebugDrawSettings& settings;
        Renderer::DebugDrawStats& stats;
        Engine::DebugVisualizationCollector& collector;

        [[nodiscard]] Engine::DebugVisualizationCategory categoryFor(
            const Renderer::DebugPrimitiveCategoryStats& category) const
        {
            if (&category == &stats.navMeshEdges) {
                return Engine::DebugVisualizationCategory::Navigation;
            }
            if (&category == &stats.worldGraphEdges) {
                return Engine::DebugVisualizationCategory::Navigation;
            }
            if (&category == &stats.terrainSlopeWarnings) {
                return Engine::DebugVisualizationCategory::Terrain;
            }
            if (&category == &stats.collisionBounds) {
                return Engine::DebugVisualizationCategory::SceneBounds;
            }
            if (&category == &stats.chunkBorders) {
                return Engine::DebugVisualizationCategory::Terrain;
            }
            return Engine::DebugVisualizationCategory::Performance;
        }

        bool consume(Renderer::DebugPrimitiveCategoryStats& category, uint32_t maxSubmitted, uint32_t lineCount = 1)
        {
            category.generated += lineCount;
            if (category.submitted + lineCount > maxSubmitted ||
                stats.submittedLines + lineCount > settings.maxDebugLines) {
                category.clipped += lineCount;
                stats.generatedLines += lineCount;
                stats.clippedLines += lineCount;
                return false;
            }
            category.submitted += lineCount;
            return true;
        }

        void addLine(const glm::vec3& a, const glm::vec3& b, uint32_t color)
        {
            (void)collector.addLine(Engine::DebugVisualizationCategory::Performance, a, b, color);
        }

        void addLine(Renderer::DebugPrimitiveCategoryStats& category, uint32_t maxSubmitted, const glm::vec3& a, const glm::vec3& b, uint32_t color)
        {
            if (consume(category, maxSubmitted)) {
                (void)collector.addLine(categoryFor(category), a, b, color);
            }
        }

        void addAabb(const Renderer::Aabb& bounds, uint32_t color)
        {
            (void)collector.addAabb(
                Engine::DebugVisualizationCategory::SceneBounds,
                {bounds.min, bounds.max},
                color);
        }

        void addAabb(Renderer::DebugPrimitiveCategoryStats& category, uint32_t maxSubmitted, const Renderer::Aabb& bounds, uint32_t color)
        {
            if (consume(category, maxSubmitted, 12)) {
                (void)collector.addAabb(categoryFor(category), {bounds.min, bounds.max}, color);
            }
        }

        void addXZRect(float minX, float minZ, float maxX, float maxZ, float y, uint32_t color)
        {
            (void)collector.addLine(Engine::DebugVisualizationCategory::Terrain, {minX, y, minZ}, {maxX, y, minZ}, color);
            (void)collector.addLine(Engine::DebugVisualizationCategory::Terrain, {maxX, y, minZ}, {maxX, y, maxZ}, color);
            (void)collector.addLine(Engine::DebugVisualizationCategory::Terrain, {maxX, y, maxZ}, {minX, y, maxZ}, color);
            (void)collector.addLine(Engine::DebugVisualizationCategory::Terrain, {minX, y, maxZ}, {minX, y, minZ}, color);
        }

        void addXZRect(Renderer::DebugPrimitiveCategoryStats& category, uint32_t maxSubmitted, float minX, float minZ, float maxX, float maxZ, float y, uint32_t color)
        {
            if (consume(category, maxSubmitted, 4)) {
                (void)collector.addLine(categoryFor(category), {minX, y, minZ}, {maxX, y, minZ}, color);
                (void)collector.addLine(categoryFor(category), {maxX, y, minZ}, {maxX, y, maxZ}, color);
                (void)collector.addLine(categoryFor(category), {maxX, y, maxZ}, {minX, y, maxZ}, color);
                (void)collector.addLine(categoryFor(category), {minX, y, maxZ}, {minX, y, minZ}, color);
            }
        }
    };

    const std::filesystem::path DefaultHeightmapPath =
        "assets/heightmaps/47_648_-122_332_13_505_505_16bit.png";
    const std::filesystem::path DefaultSponzaScenePath = "Assets/glTF/Sponza/glTF/Sponza.gltf";
    const std::filesystem::path DefaultKayKitStaticFbxPath = "Assets/fbx/sword_1handed.fbx";
    const std::filesystem::path DefaultKayKitStaticGltfPath = "Assets/gltf/shield_round.gltf";
    const std::filesystem::path ReleaseKayKitAnimatedGltfPath =
        "Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb";
    const std::filesystem::path DefaultKayKitAnimatedFbxPath =
        "Animations/fbx/Rig_Medium/Rig_Medium_MovementBasic.fbx";

    std::vector<Engine::ChunkCoord> chunkAndCardinalNeighbors(Engine::ChunkCoord coord)
    {
        return {
            coord,
            {coord.x + 1, coord.z},
            {coord.x - 1, coord.z},
            {coord.x, coord.z + 1},
            {coord.x, coord.z - 1},
        };
    }

    std::string rebuildModernNavigationConnectivityFromLoadedTiles(
        Engine::NavigationConnectivitySystem& connectivity,
        const Engine::NavigationSystem& navigation)
    {
        connectivity.clear();
        uint32_t rebuilt = 0;
        for (const Engine::NavigationTileDiagnostics& tile : navigation.allTileDiagnostics()) {
            connectivity.rebuildChunk(tile.coord, navigation, {});
            ++rebuilt;
        }
        const Engine::NavigationConnectivityStats stats = connectivity.stats();
        return "Navigation connectivity rebuilt for " + std::to_string(rebuilt) +
            " loaded tiles; portals " + std::to_string(stats.totalPortals) +
            ", connected " + std::to_string(stats.connectedPortals) + ".";
    }

    void applyAuthoredAtmosphereDefaults(Renderer::AtmosphereSettings& atmosphere)
    {
        atmosphere.sunDirection = glm::normalize(glm::vec3{-0.35f, -0.8f, -0.25f});
        atmosphere.sunColor = {1.0f, 0.94f, 0.84f};
        atmosphere.skyColor = {0.52f, 0.69f, 0.92f, 1.0f};
        atmosphere.ambientIntensity = 0.12f;
    }

    Renderer::MaterialHandle createMaterial(std::string name, Renderer::TextureHandle texture)
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = std::move(name);
        descriptor.baseColorTexture = texture;
        descriptor.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
        return Renderer::createMaterial(descriptor);
    }

    void frameCameraForBounds(Engine::OrbitCameraController& camera, const Renderer::Aabb& bounds)
    {
        const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
        const glm::vec3 extent = bounds.max - bounds.min;
        const float radius = std::max({extent.x, extent.y, extent.z, 32.0f}) * 0.75f;
        camera.setMode(Engine::CameraMode::Free);
        camera.clearFollowTarget();
        Engine::CameraState state = camera.state();
        state.pivot = center;
        state.distance = std::clamp(radius, 24.0f, 360.0f);
        camera.setState(state);
    }

#include "AuthoredRuntime.inl"

namespace {

    struct ModernEditorLiveApplyHostState {
        ModernDefaultSceneRuntime* runtime = nullptr;
        Renderer::DebugUi::RendererDebugSettings* rendererDebugSettings = nullptr;
        Renderer::DebugDrawSettings* debugDrawSettings = nullptr;
        Engine::OrbitCameraController* camera = nullptr;
        Renderer::DebugUi::CameraDebugControls* cameraDebugControls = nullptr;
    };

    void applyModernDrawDistanceSettings(
        ModernDefaultSceneRuntime& runtime,
        const Renderer::DebugUi::RendererDebugSettings& debugSettings)
    {
        for (ModernTerrainChunkRuntime& chunk : runtime.terrainChunks) {
            Renderer::setTerrainMaxDrawDistance(chunk.rendererTerrain, debugSettings.terrainMaxDrawDistance);
        }
        for (const auto& [_, terrain] : runtime.streamingTerrainHandles) {
            Renderer::setTerrainMaxDrawDistance(terrain, debugSettings.terrainMaxDrawDistance);
        }
        for (ModernAuthoredAssetRuntime& asset : runtime.authoredAssets) {
            for (const Engine::SceneAuthoredNodeBinding& node : asset.result.nodes) {
                for (Engine::SceneMeshComponentHandle mesh : node.meshComponents) {
                    if (std::optional<Engine::SceneMeshComponentDescriptor> descriptor =
                            runtime.renderBridge.meshDescriptor(mesh)) {
                        descriptor->maxDrawDistance = debugSettings.propMaxDrawDistance;
                        (void)runtime.renderBridge.setMeshDescriptor(mesh, *descriptor);
                    }
                }
            }
        }
        for (ModernAnimatedAssetRuntime& asset : runtime.animatedAssets) {
            for (const Engine::SceneAnimatedMeshBinding& binding : asset.result.skinnedMeshes) {
                if (std::optional<Engine::SceneSkinnedMeshComponentDescriptor> descriptor =
                        runtime.renderBridge.skinnedMeshDescriptor(binding.component)) {
                    descriptor->maxDrawDistance = debugSettings.propMaxDrawDistance;
                    (void)runtime.renderBridge.setSkinnedMeshDescriptor(binding.component, *descriptor);
                }
            }
        }
    }

    bool applyModernEditorLightweightRuntime(
        void* user,
        const ManualEngine::App::EditorProjectSettings& settings,
        std::string& message)
    {
        auto* host = static_cast<ModernEditorLiveApplyHostState*>(user);
        if (!host || !host->runtime || !host->rendererDebugSettings ||
            !host->debugDrawSettings || !host->camera || !host->cameraDebugControls) {
            message = "Editor live apply host is incomplete.";
            return false;
        }

        *host->rendererDebugSettings = settings.renderer;
        host->rendererDebugSettings->enableDistanceCulling =
            BuildDebugToolsEnabled ? host->rendererDebugSettings->enableDistanceCulling : false;
        *host->debugDrawSettings = settings.debugDraw;
        Renderer::setDebugDrawSettings(*host->debugDrawSettings);
        applyModernDrawDistanceSettings(*host->runtime, *host->rendererDebugSettings);

        Engine::CameraSettings cameraSettings = settings.camera.settings;
        cameraSettings.minPivotXZ = {
            host->runtime->bounds.min.x - 40.0f,
            host->runtime->bounds.min.z - 40.0f,
        };
        cameraSettings.maxPivotXZ = {
            host->runtime->bounds.max.x + 40.0f,
            host->runtime->bounds.max.z + 40.0f,
        };
        host->camera->settings() = cameraSettings;
        host->camera->followSettings() = settings.camera.follow;
        host->cameraDebugControls->followSmoothing = settings.camera.follow.followSmoothing;
        host->cameraDebugControls->maxFollowLag = settings.camera.follow.maxFollowLag;

        Engine::CameraState state = host->camera->state();
        state.mode = settings.camera.mode;
        state.pivot = host->runtime->focus + settings.camera.pivotOffsetFromFocus;
        state.yawRadians = settings.camera.yawRadians;
        state.pitchRadians = settings.camera.pitchRadians;
        state.distance = std::clamp(
            settings.camera.distance,
            host->camera->settings().minDistance,
            host->camera->settings().maxDistance);
        host->camera->setState(state);
        if (settings.camera.mode == Engine::CameraMode::Free) {
            host->camera->clearFollowTarget();
        } else if (const std::optional<glm::mat4> playerWorld =
                host->runtime->scene.worldMatrix(host->runtime->playerActor)) {
            host->camera->setFollowTarget(glm::vec3{(*playerWorld)[3]});
        }

        message = "Applied lightweight renderer, debug draw, and camera settings to the editor viewport.";
        return true;
    }

    void releaseModernStreamingLiveResources(ModernDefaultSceneRuntime& runtime)
    {
        for (auto& [_, terrain] : runtime.streamingTerrainHandles) {
            Renderer::destroyTerrainTile(terrain);
        }
        runtime.streamingTerrainHandles.clear();
        runtime.terrainRendererCount = static_cast<uint32_t>(runtime.terrainChunks.size());

        runtime.navigationConnectivity.clear();
        runtime.navigation.clear();
        runtime.streamingNavigationTiles.clear();
        runtime.terrainNavTileCount = 0;

        for (auto& [_, collider] : runtime.streamingPhysicsColliders) {
            runtime.terrainPhysics.destroyCollider(runtime.scene, runtime.physics, collider);
        }
        runtime.streamingPhysicsColliders.clear();
        runtime.terrainPhysicsColliderCount = 0;

        for (auto& [_, chunk] : runtime.streamingTerrainChunks) {
            runtime.terrain.unloadChunk(chunk);
        }
        runtime.streamingTerrainChunks.clear();

        if (Engine::isValid(runtime.terrainSource)) {
            runtime.terrain.unregisterSource(runtime.terrainSource);
            runtime.terrainSource = {};
        }

        runtime.streamingRuntime.shutdown();
        runtime.streamingAsync.restart();
        runtime.streamingMainThread.clear();
    }

    bool reloadModernEditorStreamingRuntime(
        void* user,
        const ManualEngine::App::EditorProjectSettings& settings,
        std::string& message,
        Engine::OpenWorldStreamingBuildResult& result)
    {
        auto* host = static_cast<ModernEditorLiveApplyHostState*>(user);
        if (!host || !host->runtime) {
            message = "Editor streaming reload host is incomplete.";
            result.message = message;
            return false;
        }

        ModernDefaultSceneRuntime& runtime = *host->runtime;
        Engine::OpenWorldStreamingRuntimeSettings runtimeSettings =
            modernStreamingRuntimeSettings({}, &settings);
        runtimeSettings.rebuildWhenStale = false;
        Engine::OpenWorldStreamingRuntime candidate{runtimeSettings};
        result = candidate.initializeFromSavedBuild();
        if (!result.success) {
            message = !result.message.empty()
                ? result.message
                : "Failed to initialize rebuilt saved streaming build.";
            candidate.shutdown();
            return false;
        }

        const Renderer::Aabb manifestBounds = boundsFromStreamingManifest(candidate.manifest());
        Engine::TerrainSourceDescriptor source = modernStreamingTerrainSourceDescriptor(runtimeSettings);
        source.bounds.min = manifestBounds.min;
        source.bounds.max = manifestBounds.max;
        source.defaultChunkSize = runtimeSettings.bake.heightmap.chunkWorldSize;
        source.defaultResolution = runtimeSettings.bake.heightmap.chunkResolution;

        releaseModernStreamingLiveResources(runtime);
        runtime.streamingRuntime = std::move(candidate);
        runtime.terrainSource = runtime.terrain.registerSource(source);
        runtime.bounds = manifestBounds;
        runtime.focus = (manifestBounds.min + manifestBounds.max) * 0.5f;
        runtime.loadedHeightmap = true;
        runtime.usingOpenWorldStreaming = true;
        runtime.navigationStatus = "Reloaded rebuilt open-world streaming runtime from saved manifest.";
        runtime.status = "Modern default scene reloaded streaming runtime from saved build.";
        runtime.warningCount = 0;
        message = runtime.navigationStatus;
        SDL_Log("%s", message.c_str());
        return true;
    }

    std::string_view navStatusName(Engine::NavQueryStatus status)
    {
        switch (status) {
            case Engine::NavQueryStatus::Success:
                return "Success";
            case Engine::NavQueryStatus::NotInitialized:
                return "NotInitialized";
            case Engine::NavQueryStatus::NoTile:
                return "NoTile";
            case Engine::NavQueryStatus::NoNearestPoly:
                return "NoNearestPoly";
            case Engine::NavQueryStatus::NoPath:
                return "NoPath";
            case Engine::NavQueryStatus::InvalidInput:
                return "InvalidInput";
            case Engine::NavQueryStatus::Unsupported:
                return "Unsupported";
        }
        return "Unknown";
    }

    std::string_view scenePhysicsStatusName(Engine::ScenePhysicsQueryStatus status)
    {
        switch (status) {
            case Engine::ScenePhysicsQueryStatus::Success:
                return "Success";
            case Engine::ScenePhysicsQueryStatus::InvalidInput:
                return "InvalidInput";
            case Engine::ScenePhysicsQueryStatus::NoWorld:
                return "NoWorld";
            case Engine::ScenePhysicsQueryStatus::NoHit:
                return "NoHit";
            case Engine::ScenePhysicsQueryStatus::UnsupportedShape:
                return "UnsupportedShape";
        }
        return "Unknown";
    }

    std::string_view sceneCharacterModeName(Engine::SceneCharacterMovementMode mode)
    {
        switch (mode) {
            case Engine::SceneCharacterMovementMode::Disabled:
                return "Disabled";
            case Engine::SceneCharacterMovementMode::Walking:
                return "Walking";
            case Engine::SceneCharacterMovementMode::Falling:
                return "Falling";
        }
        return "Unknown";
    }

    std::string_view sceneCharacterStatusName(Engine::SceneCharacterMovementStatus status)
    {
        switch (status) {
            case Engine::SceneCharacterMovementStatus::Success:
                return "Success";
            case Engine::SceneCharacterMovementStatus::InvalidCharacter:
                return "InvalidCharacter";
            case Engine::SceneCharacterMovementStatus::InvalidActor:
                return "InvalidActor";
            case Engine::SceneCharacterMovementStatus::InvalidDescriptor:
                return "InvalidDescriptor";
            case Engine::SceneCharacterMovementStatus::PhysicsBodyUnavailable:
                return "PhysicsBodyUnavailable";
            case Engine::SceneCharacterMovementStatus::NavigationUnavailable:
                return "NavigationUnavailable";
            case Engine::SceneCharacterMovementStatus::NoPath:
                return "NoPath";
            case Engine::SceneCharacterMovementStatus::Blocked:
                return "Blocked";
        }
        return "Unknown";
    }

    Renderer::DebugUi::OpenWorldStreamingDebugStats toDebugStats(
        const Engine::OpenWorldStreamingDiagnostics& diagnostics)
    {
        Renderer::DebugUi::OpenWorldStreamingDebugStats stats;
        stats.desiredChunksByState = diagnostics.desiredChunksByState;
        stats.actualChunksByState = diagnostics.actualChunksByState;
        stats.desiredChunksByPayload = diagnostics.desiredChunksByPayload;
        stats.desiredChunksByProfile = diagnostics.desiredChunksByProfile;
        stats.transitionLimitedByProfile = diagnostics.transitionLimitedByProfile;
        stats.transitionCountThisFrame = diagnostics.transitionCountThisFrame;
        for (size_t index = 0; index < diagnostics.lanes.size() && index < stats.queuedByLane.size(); ++index) {
            const Engine::StreamingLaneDiagnostics& lane = diagnostics.lanes[index];
            stats.queuedByLane[index] = lane.queuedCount;
            stats.activeJobsByLane[index] = lane.activeJobCount;
            stats.completedByLane[index] = lane.completedCount;
            stats.failedByLane[index] = lane.failedCount;
            stats.laneCpuMs[index] = static_cast<float>(lane.elapsedMicroseconds) / 1000.0f;
            stats.bytesRead += lane.bytesRead;
            stats.bytesWritten += lane.bytesWritten;
        }
        for (size_t index = 0; index < diagnostics.payloads.size() && index < stats.cacheHitsByPayload.size(); ++index) {
            const Engine::StreamingPayloadCacheDiagnostics& payload = diagnostics.payloads[index];
            stats.cacheHitsByPayload[index] = payload.hits;
            stats.cacheMissesByPayload[index] = payload.misses;
            stats.cacheStaleByPayload[index] = payload.stale;
            stats.cacheCorruptByPayload[index] = payload.corrupt;
            stats.cacheWritesByPayload[index] = payload.writes;
        }
        stats.liveTerrainRenderHandles = diagnostics.liveResources.terrainRenderHandles;
        stats.liveNavigationTiles = diagnostics.liveResources.navigationTiles;
        stats.livePhysicsBodies = diagnostics.liveResources.physicsBodies;
        stats.livePhysicsColliders = diagnostics.liveResources.physicsColliders;
        stats.liveSceneActors = diagnostics.liveResources.sceneActors;
        stats.liveSceneComponents = diagnostics.liveResources.sceneComponents;
        stats.liveAssetDependencies = diagnostics.liveResources.assetDependencies;
        stats.manifestRecordCount = diagnostics.manifestRecordCount;
        stats.manifestRecordsConsidered = diagnostics.manifestRecordsConsidered;
        stats.manifestRecordsSkipped = diagnostics.manifestRecordsSkipped;
        stats.transitionCandidateCount = diagnostics.transitionCandidateCount;
        stats.transitionLimitedCount = diagnostics.transitionLimitedCount;
        stats.hysteresisRetainedCount = diagnostics.hysteresisRetainedCount;
        stats.invalidBoundsCount = diagnostics.invalidBoundsCount;
        stats.pendingReadCount = diagnostics.pendingReadCount;
        stats.cachedCpuPayloadCount = diagnostics.cachedCpuPayloadCount;
        stats.pendingPromoteCount = diagnostics.pendingPromoteCount;
        stats.pendingDemoteCount = diagnostics.pendingDemoteCount;
        stats.staleReadCompletionCount = diagnostics.staleReadCompletionCount;
        stats.unsupportedReadCount = diagnostics.unsupportedReadCount;
        stats.stalePromotionCompletionCount = diagnostics.stalePromotionCompletionCount;
        stats.failedPromotionCount = diagnostics.failedPromotionCount;
        stats.failedDemotionCount = diagnostics.failedDemotionCount;
        stats.livePayloadCount = diagnostics.livePayloadCount;
        stats.estimatedResidentBytes = diagnostics.estimatedResidentBytes;
        stats.hasLastFocus = diagnostics.hasLastFocus;
        stats.lastFocus = diagnostics.lastFocus;
        stats.bakeChunkCount = diagnostics.bakeChunkCount;
        stats.bakePayloadWriteCount = diagnostics.bakePayloadWriteCount;
        stats.generationQueuedCount = diagnostics.generationQueuedCount;
        stats.generationCompletedCount = diagnostics.generationCompletedCount;
        stats.generationFailedCount = diagnostics.generationFailedCount;
        stats.cacheInvalidationCount = diagnostics.cacheInvalidationCount;
        stats.assetDependencyManifestCount = diagnostics.assetDependencyManifestCount;
        stats.assetMetadataCacheHitCount = diagnostics.assetMetadataCacheHitCount;
        stats.liveAssetMeshCount = diagnostics.liveAssetMeshCount;
        stats.liveAssetTextureCount = diagnostics.liveAssetTextureCount;
        stats.missingAssetDependencyCount = diagnostics.missingAssetDependencyCount;
        stats.unsupportedAssetDependencyCount = diagnostics.unsupportedAssetDependencyCount;
        stats.sharedAssetReferenceCount = diagnostics.sharedAssetReferenceCount;
        stats.assetReleaseLatencyMicroseconds = diagnostics.assetReleaseLatencyMicroseconds;
        stats.sceneChunkManifestCount = diagnostics.sceneChunkManifestCount;
        stats.cachedSceneChunkPayloadCount = diagnostics.cachedSceneChunkPayloadCount;
        stats.promotedSceneChunkCount = diagnostics.promotedSceneChunkCount;
        stats.demotedSceneChunkCount = diagnostics.demotedSceneChunkCount;
        stats.sceneChunkActorsCreated = diagnostics.sceneChunkActorsCreated;
        stats.sceneChunkComponentsCreated = diagnostics.sceneChunkComponentsCreated;
        stats.sceneChunkActorsDestroyed = diagnostics.sceneChunkActorsDestroyed;
        stats.sceneChunkDuplicateStableIdCount = diagnostics.sceneChunkDuplicateStableIdCount;
        stats.sceneChunkInvalidParentCount = diagnostics.sceneChunkInvalidParentCount;
        stats.sceneChunkInvalidComponentCount = diagnostics.sceneChunkInvalidComponentCount;
        stats.sceneChunkUnsupportedOwnershipCount = diagnostics.sceneChunkUnsupportedOwnershipCount;
        stats.mainThreadPromoteItemsRun = diagnostics.mainThreadPromoteItemsRun;
        stats.mainThreadPromoteItemsDeferred = diagnostics.mainThreadPromoteItemsDeferred;
        stats.mainThreadDemoteItemsRun = diagnostics.mainThreadDemoteItemsRun;
        stats.mainThreadDemoteItemsDeferred = diagnostics.mainThreadDemoteItemsDeferred;
        stats.hysteresisChurnCount = diagnostics.hysteresisChurnCount;
        stats.evictionBlockedCount = diagnostics.evictionBlockedCount;
        stats.variantRecordCount = diagnostics.variantRecordCount;
        stats.activeFocusCandidateCount = diagnostics.activeFocusCandidateCount;
        stats.predictiveCandidateCount = diagnostics.predictiveCandidateCount;
        stats.predictivePrefetchCount = diagnostics.predictivePrefetchCount;
        stats.prefetchRetainedCount = diagnostics.prefetchRetainedCount;
        stats.highDetailCandidateCount = diagnostics.highDetailCandidateCount;
        stats.hasLastFailure = diagnostics.lastFailure.hasFailure;
        if (diagnostics.lastFailure.hasFailure) {
            stats.lastFailureLane = Engine::streamingTransitionLaneName(diagnostics.lastFailure.lane);
            stats.lastFailurePayload = Engine::streamingPayloadKindName(diagnostics.lastFailure.payload);
            stats.lastFailureChunk = diagnostics.lastFailure.chunkId;
            stats.lastFailureStatus = diagnostics.lastFailure.status;
            stats.lastFailureMessage = diagnostics.lastFailure.message;
        }
        return stats;
    }

    Renderer::DebugUi::ModernDebugUiState makeModernDebugUiState(
        const ModernDefaultSceneRuntime& runtime,
        const FramePerformanceTimings& frameTimings,
        float startupWorkMs)
    {
        Renderer::DebugUi::ModernDebugUiState state;
        state.performance.startupComplete = runtime.schedulerStarted;
        state.performance.startupWorkMs = startupWorkMs;
        state.performance.startupStatus = runtime.status;
        state.performance.previousFrameCpuMs = frameTimings.previousFrameCpuMs;
        state.performance.eventPollingMs = frameTimings.eventPollingMs;
        state.performance.inputMappingMs = frameTimings.inputMappingMs;
        state.performance.cameraUpdateMs = frameTimings.cameraUpdateMs;
        state.performance.sceneFixedTickMs = frameTimings.sceneFixedTickMs;
        state.performance.sceneFrameTickMs = frameTimings.sceneFrameTickMs;
        state.performance.debugPrimitiveEnqueueMs = frameTimings.debugPrimitiveEnqueueMs;
        state.performance.drawSubmissionMs = frameTimings.drawSubmissionMs;
        state.performance.debugPrimitiveDrawMs = frameTimings.debugPrimitiveDrawMs;
        state.performance.debugUiBuildMs = frameTimings.debugUiBuildMs;
        state.performance.debugUiRenderMs = frameTimings.debugUiRenderMs;
        state.performance.bgfxFrameMs = frameTimings.bgfxFrameMs;

        const Engine::SceneSchedulerDiagnostics scheduler = runtime.scene.schedulerDiagnostics();
        for (uint32_t index = 0; index < state.performance.scenePhaseMs.size() &&
            index < scheduler.phases.size(); ++index) {
            state.performance.scenePhaseMs[index] =
                static_cast<float>(scheduler.phases[index].elapsedMicroseconds) / 1000.0f;
            state.performance.scenePhaseCallbacks[index] = scheduler.phases[index].callbackCount;
        }

        state.scene.mode = "Modern default scene";
        state.scene.status = runtime.status;
        runtime.scene.forEachActor([&](Engine::SceneActorHandle actor) {
            ++state.scene.actorCount;
            state.scene.componentCount += static_cast<uint32_t>(runtime.scene.components(actor).size());
        });
        state.scene.frameIndex = scheduler.frameIndex;
        state.scene.fixedStepIndex = scheduler.fixedStepIndex;
        state.scene.registeredSystemCount = scheduler.registeredSystemCount;
        state.scene.enabledSystemCount = scheduler.enabledSystemCount;
        state.scene.skippedPhaseCount = scheduler.skippedPhaseCount;
        state.scene.skippedCallbackCount = scheduler.skippedCallbackCount;
        const Engine::SceneRenderBridgeDiagnostics bridge = runtime.renderBridge.diagnostics();
        state.scene.renderMeshComponents = bridge.meshComponentCount;
        state.scene.renderSkinnedComponents = bridge.skinnedMeshComponentCount;
        state.scene.renderLightComponents = bridge.lightComponentCount;
        state.scene.renderCameraComponents = bridge.cameraComponentCount;
        state.scene.liveMeshInstances = bridge.liveMeshInstanceCount;
        state.scene.liveSkinnedInstances = bridge.liveSkinnedInstanceCount;
        state.scene.liveRendererLights = bridge.liveLightCount;
        state.scene.authoredStaticAssets = runtime.staticAssetCount;
        state.scene.animatedAssets = runtime.animatedAssetCount;
        state.scene.warnings = runtime.warningCount;

        const Engine::TerrainDatasetDiagnostics terrain = runtime.terrain.diagnostics();
        state.terrain.loadedHeightmap = runtime.loadedHeightmap;
        state.terrain.sourceCount = terrain.sourceCount;
        state.terrain.loadedChunkCount = terrain.loadedChunkCount;
        state.terrain.renderedChunkCount = runtime.terrainRendererCount;
        state.terrain.navTileCount = runtime.terrainNavTileCount;
        state.terrain.physicsColliderCount = runtime.terrainPhysicsColliderCount;
        state.terrain.boundsMin = runtime.bounds.min;
        state.terrain.boundsMax = runtime.bounds.max;
        state.terrain.minHeight = terrain.minHeight;
        state.terrain.maxHeight = terrain.maxHeight;
        state.terrain.averageHeight = terrain.averageHeight;
        state.terrain.memoryEstimateBytes = terrain.estimatedBytes;
        if (!terrain.warnings.empty()) {
            state.terrain.diagnostics = terrain.warnings.back();
        }

        const Engine::NavigationDebugGeometry navigationGeometry = runtime.navigation.debugGeometry();
        const Engine::SceneNavigationGeometryDiagnostics sceneGeometry = runtime.sceneNavigationGeometry.diagnostics();
        state.navigation.loadedTiles = static_cast<uint32_t>(runtime.navigation.allTileDiagnostics().size());
        state.navigation.polygonEdgeCount = static_cast<uint32_t>(navigationGeometry.polygonEdges.size());
        state.navigation.sceneSourceCount = sceneGeometry.registeredSourceCount;
        state.navigation.sceneTriangleCount = sceneGeometry.consideredTriangleCount;
        state.navigation.sceneAppendedTriangleCount = sceneGeometry.appendedTriangleCount;
        state.navigation.sceneCulledByBounds = sceneGeometry.boundsCulledTriangleCount;
        state.navigation.sceneCulledBySlope = sceneGeometry.slopeCulledTriangleCount;
        state.navigation.cacheHits = runtime.navigationCacheHitCount;
        state.navigation.cacheMisses = runtime.navigationCacheMissCount;
        state.navigation.cacheStale = runtime.navigationCacheStaleCount;
        state.navigation.cacheWrites = runtime.navigationCacheWriteCount;
        const Engine::NavigationConnectivityStats connectivityStats = runtime.navigationConnectivity.stats();
        state.navigation.connectivityChunks = connectivityStats.chunkCount;
        state.navigation.connectivityPortals = connectivityStats.totalPortals;
        state.navigation.connectivityConnectedPortals = connectivityStats.connectedPortals;
        state.navigation.connectivityPartialChunks = connectivityStats.partialChunks;
        state.navigation.connectivityBlockedChunks = connectivityStats.blockedChunks;
        state.navigation.connectivityStatus = runtime.navigationStatus;
        state.navigation.lastBuildStatus = navStatusName(runtime.navigation.lastBuildStatus());
        state.navigation.lastBuildMessage = runtime.navigation.lastBuildMessage();
        state.navigation.lastQueryStatus = navStatusName(runtime.navigation.lastQueryStatus());
        state.navigation.lastQueryMessage = runtime.navigation.lastQueryMessage();

        const Engine::ScenePhysicsDiagnostics physics = runtime.physics.diagnostics();
        state.physics.bodyCount = physics.bodyCount;
        state.physics.colliderCount = physics.colliderCount;
        state.physics.enabledBodyCount = physics.activeBodyCount;
        state.physics.invalidOwnerCleanupCount = physics.invalidOwnerCleanupCount;
        state.physics.raycastCount = physics.raycastCount;
        state.physics.sweepCount = physics.sweepCount;
        state.physics.overlapCount = physics.overlapCount;
        state.physics.closestPointCount = physics.closestPointCount;
        state.physics.lastStepMicroseconds = physics.lastStepMicroseconds;
        state.physics.lastQueryMicroseconds = physics.lastQueryMicroseconds;
        state.physics.lastStatus = scenePhysicsStatusName(physics.lastStatus);
        state.physics.lastMessage = physics.lastMessage;
        state.physics.warnings = static_cast<uint32_t>(physics.warnings.size());

        const Engine::SceneCharacterMovementDiagnostics characters = runtime.characters.diagnostics();
        state.character.characterCount = characters.characterCount;
        state.character.failedSweepCount = characters.failedSweepCount;
        state.character.pathQueryCount = characters.pathQueryCount;
        state.character.invalidOwnerCleanupCount = characters.invalidOwnerCleanupCount;
        state.character.lastUpdateMicroseconds = characters.lastUpdateMicroseconds;
        if (const std::optional<Engine::SceneCharacterState> characterState =
                runtime.characters.state(runtime.playerCharacter)) {
            const std::optional<Engine::SceneCharacterDescriptor> descriptor =
                runtime.characters.descriptor(runtime.playerCharacter);
            state.character.hasCharacter = true;
            state.character.enabled = descriptor ? descriptor->enabled : false;
            state.character.mode = sceneCharacterModeName(characterState->mode);
            state.character.grounded = characterState->grounded;
            state.character.velocity = characterState->velocity;
            state.character.floorNormal = characterState->floorNormal;
            state.character.floorDistance = characterState->floorDistance;
            state.character.activePathPointCount = static_cast<uint32_t>(characterState->activePath.points.size());
            state.character.activeWaypointIndex = characterState->activeWaypointIndex;
            state.character.lastStatus = sceneCharacterStatusName(characterState->lastStatus);
            state.character.lastMessage = characterState->lastMessage;
        }

        const Renderer::DebugDrawStats& debug = Renderer::debugDrawStats();
        state.debugVisualization.enabled = Renderer::debugDrawSettings().enabled;
        state.debugVisualization.generatedLines = debug.generatedLines;
        state.debugVisualization.submittedLines = debug.submittedLines;
        state.debugVisualization.clippedLines = debug.clippedLines;
        state.debugVisualization.lastFramePrimitiveBufferSize = debug.lastFramePrimitiveBufferSize;
        state.streaming = toDebugStats(runtime.streamingRuntime.diagnostics());
        return state;
    }

    Renderer::DebugUi::ModernDebugUiState makeSharedDebugUiState(
        std::string mode,
        std::string status,
        const FramePerformanceTimings& frameTimings)
    {
        Renderer::DebugUi::ModernDebugUiState state;
        state.scene.mode = std::move(mode);
        state.scene.status = std::move(status);
        state.performance.previousFrameCpuMs = frameTimings.previousFrameCpuMs;
        state.performance.eventPollingMs = frameTimings.eventPollingMs;
        state.performance.inputMappingMs = frameTimings.inputMappingMs;
        state.performance.cameraUpdateMs = frameTimings.cameraUpdateMs;
        state.performance.drawSubmissionMs = frameTimings.drawSubmissionMs;
        state.performance.debugPrimitiveDrawMs = frameTimings.debugPrimitiveDrawMs;
        state.performance.debugUiBuildMs = frameTimings.debugUiBuildMs;
        state.performance.debugUiRenderMs = frameTimings.debugUiRenderMs;
        state.performance.bgfxFrameMs = frameTimings.bgfxFrameMs;
        const Renderer::DebugDrawStats& debug = Renderer::debugDrawStats();
        state.debugVisualization.enabled = Renderer::debugDrawSettings().enabled;
        state.debugVisualization.generatedLines = debug.generatedLines;
        state.debugVisualization.submittedLines = debug.submittedLines;
        state.debugVisualization.clippedLines = debug.clippedLines;
        state.debugVisualization.lastFramePrimitiveBufferSize = debug.lastFramePrimitiveBufferSize;
        state.streaming = toDebugStats(Engine::makeEmptyOpenWorldStreamingDiagnostics());
        return state;
    }

    constexpr uint32_t modernDebugColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return (static_cast<uint32_t>(a) << 24) |
            (static_cast<uint32_t>(b) << 16) |
            (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(r);
    }

    bool addCappedDebugLine(
        uint32_t& submitted,
        uint32_t maxSubmitted,
        const glm::vec3& a,
        const glm::vec3& b,
        uint32_t color)
    {
        if (submitted >= maxSubmitted) {
            return false;
        }
        ++submitted;
        Renderer::addDebugLine(a, b, color);
        return true;
    }

    glm::vec3 transformColliderPoint(const Engine::ScenePhysicsDebugColliderShape& collider, const glm::vec3& local)
    {
        return collider.position + collider.rotation * local;
    }

    void addColliderRing(
        const Engine::ScenePhysicsDebugColliderShape& collider,
        uint32_t& submitted,
        uint32_t maxSubmitted,
        const glm::vec3& center,
        const glm::vec3& axisA,
        const glm::vec3& axisB,
        float radius,
        uint32_t color)
    {
        constexpr int Segments = 16;
        constexpr float TwoPi = 6.28318530717958647692f;
        for (int segment = 0; segment < Segments; ++segment) {
            const float a0 = TwoPi * static_cast<float>(segment) / static_cast<float>(Segments);
            const float a1 = TwoPi * static_cast<float>(segment + 1) / static_cast<float>(Segments);
            const glm::vec3 p0 = center + (std::cos(a0) * axisA + std::sin(a0) * axisB) * radius;
            const glm::vec3 p1 = center + (std::cos(a1) * axisA + std::sin(a1) * axisB) * radius;
            if (!addCappedDebugLine(
                    submitted,
                    maxSubmitted,
                    transformColliderPoint(collider, p0),
                    transformColliderPoint(collider, p1),
                    color)) {
                return;
            }
        }
    }

    void addColliderBox(
        const Engine::ScenePhysicsDebugColliderShape& collider,
        uint32_t& submitted,
        uint32_t maxSubmitted,
        uint32_t color)
    {
        const glm::vec3& h = collider.shape.box.halfExtents;
        const glm::vec3 corners[8] = {
            {-h.x, -h.y, -h.z},
            {h.x, -h.y, -h.z},
            {h.x, -h.y, h.z},
            {-h.x, -h.y, h.z},
            {-h.x, h.y, -h.z},
            {h.x, h.y, -h.z},
            {h.x, h.y, h.z},
            {-h.x, h.y, h.z},
        };
        constexpr int Edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (const auto& edge : Edges) {
            if (!addCappedDebugLine(
                    submitted,
                    maxSubmitted,
                    transformColliderPoint(collider, corners[edge[0]]),
                    transformColliderPoint(collider, corners[edge[1]]),
                    color)) {
                return;
            }
        }
    }

    void addColliderSphere(
        const Engine::ScenePhysicsDebugColliderShape& collider,
        uint32_t& submitted,
        uint32_t maxSubmitted,
        uint32_t color)
    {
        const float radius = collider.shape.sphere.radius;
        addColliderRing(collider, submitted, maxSubmitted, {}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, radius, color);
        addColliderRing(collider, submitted, maxSubmitted, {}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius, color);
        addColliderRing(collider, submitted, maxSubmitted, {}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius, color);
    }

    void addColliderCapsule(
        const Engine::ScenePhysicsDebugColliderShape& collider,
        uint32_t& submitted,
        uint32_t maxSubmitted,
        uint32_t color)
    {
        constexpr int HemisphereSegments = 8;
        constexpr float HalfPi = 1.57079632679489661923f;
        const float radius = collider.shape.capsule.radius;
        const float halfHeight = collider.shape.capsule.halfHeight;
        const glm::vec3 top{0.0f, halfHeight, 0.0f};
        const glm::vec3 bottom{0.0f, -halfHeight, 0.0f};
        const glm::vec3 localY{0.0f, 1.0f, 0.0f};
        const glm::vec3 directions[4] = {
            {1.0f, 0.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, -1.0f},
        };

        addColliderRing(collider, submitted, maxSubmitted, top, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius, color);
        addColliderRing(collider, submitted, maxSubmitted, bottom, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius, color);
        for (const glm::vec3& direction : directions) {
            if (!addCappedDebugLine(
                    submitted,
                    maxSubmitted,
                    transformColliderPoint(collider, top + direction * radius),
                    transformColliderPoint(collider, bottom + direction * radius),
                    color)) {
                return;
            }
            for (int segment = 0; segment < HemisphereSegments; ++segment) {
                const float a0 = HalfPi * static_cast<float>(segment) / static_cast<float>(HemisphereSegments);
                const float a1 = HalfPi * static_cast<float>(segment + 1) / static_cast<float>(HemisphereSegments);
                const glm::vec3 top0 = top + direction * std::cos(a0) * radius + localY * std::sin(a0) * radius;
                const glm::vec3 top1 = top + direction * std::cos(a1) * radius + localY * std::sin(a1) * radius;
                const glm::vec3 bottom0 = bottom + direction * std::cos(a0) * radius - localY * std::sin(a0) * radius;
                const glm::vec3 bottom1 = bottom + direction * std::cos(a1) * radius - localY * std::sin(a1) * radius;
                if (!addCappedDebugLine(
                        submitted,
                        maxSubmitted,
                        transformColliderPoint(collider, top0),
                        transformColliderPoint(collider, top1),
                        color) ||
                    !addCappedDebugLine(
                        submitted,
                        maxSubmitted,
                        transformColliderPoint(collider, bottom0),
                        transformColliderPoint(collider, bottom1),
                        color)) {
                    return;
                }
            }
        }
    }

    void addColliderTriangleMesh(
        const Engine::ScenePhysicsDebugColliderShape& collider,
        uint32_t& submitted,
        uint32_t maxSubmitted,
        uint32_t color)
    {
        const std::vector<glm::vec3>& vertices = collider.shape.triangleMesh.vertices;
        const std::vector<uint32_t>& indices = collider.shape.triangleMesh.indices;
        for (size_t index = 0; index + 2 < indices.size(); index += 3) {
            const uint32_t ia = indices[index + 0];
            const uint32_t ib = indices[index + 1];
            const uint32_t ic = indices[index + 2];
            if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
                continue;
            }
            const glm::vec3 a = transformColliderPoint(collider, vertices[ia]);
            const glm::vec3 b = transformColliderPoint(collider, vertices[ib]);
            const glm::vec3 c = transformColliderPoint(collider, vertices[ic]);
            if (!addCappedDebugLine(submitted, maxSubmitted, a, b, color) ||
                !addCappedDebugLine(submitted, maxSubmitted, b, c, color) ||
                !addCappedDebugLine(submitted, maxSubmitted, c, a, color)) {
                return;
            }
        }
    }

    void addColliderShapeDebug(
        const Engine::ScenePhysicsDebugColliderShape& collider,
        uint32_t& submitted,
        uint32_t maxSubmitted,
        uint32_t color)
    {
        switch (collider.shape.type) {
            case Engine::ScenePhysicsShapeType::Box:
                addColliderBox(collider, submitted, maxSubmitted, color);
                break;
            case Engine::ScenePhysicsShapeType::Sphere:
                addColliderSphere(collider, submitted, maxSubmitted, color);
                break;
            case Engine::ScenePhysicsShapeType::Capsule:
                addColliderCapsule(collider, submitted, maxSubmitted, color);
                break;
            case Engine::ScenePhysicsShapeType::StaticTriangleMesh:
                addColliderTriangleMesh(collider, submitted, maxSubmitted, color);
                break;
        }
    }

    void enqueueModernDebugPrimitives(
        const ModernDefaultSceneRuntime& runtime,
        const Renderer::RenderView& renderView,
        const Renderer::DebugDrawSettings& settings)
    {
        Renderer::clearDebugPrimitives();
        if (!settings.enabled) {
            return;
        }

        constexpr uint32_t TerrainColor = modernDebugColor(80, 220, 240);
        constexpr uint32_t NavigationTileColor = modernDebugColor(160, 255, 80);
        constexpr uint32_t NavigationMeshColor = modernDebugColor(80, 180, 255);
        constexpr uint32_t NavigationPortalColor = modernDebugColor(80, 255, 220);
        constexpr uint32_t SceneNavigationColor = modernDebugColor(255, 220, 80);
        constexpr uint32_t PhysicsColor = modernDebugColor(255, 100, 80);
        constexpr uint32_t CharacterColor = modernDebugColor(80, 255, 120);
        constexpr uint32_t CursorTraceColor = modernDebugColor(255, 255, 80);
        constexpr uint32_t CursorTraceHitColor = modernDebugColor(255, 160, 40);
        constexpr uint32_t FrustumColor = modernDebugColor(255, 80, 255);

        if (settings.terrainTileBounds) {
            for (const ModernTerrainChunkRuntime& chunk : runtime.terrainChunks) {
                if (const std::optional<Engine::TerrainDatasetBounds> bounds =
                        runtime.terrain.chunkWorldBounds(chunk.chunk)) {
                    Renderer::addDebugAabb({bounds->min, bounds->max}, TerrainColor);
                }
            }
        }

        if (settings.navigationTileBounds) {
            for (const Engine::NavigationTileDiagnostics& tile : runtime.navigation.allTileDiagnostics()) {
                Renderer::addDebugAabb(tile.bounds, NavigationTileColor);
            }
        }

        if (settings.navigationMeshEdges) {
            const Engine::NavigationDebugGeometry geometry = runtime.navigation.debugGeometry();
            uint32_t submitted = 0;
            for (const Engine::NavDebugLine& edge : geometry.polygonEdges) {
                if (submitted++ >= settings.maxNavMeshEdgeLines) {
                    break;
                }
                Renderer::addDebugLine(edge.a, edge.b, NavigationMeshColor);
            }
            for (const auto& [_, connectivity] : runtime.navigationConnectivity.all()) {
                for (const std::vector<Engine::ChunkNavPortal>& portals : connectivity.portalsByEdge) {
                    for (const Engine::ChunkNavPortal& portal : portals) {
                        const glm::vec3 marker = portal.position + glm::vec3{0.0f, 0.2f, 0.0f};
                        Renderer::addDebugLine(
                            marker + glm::vec3{-0.25f, 0.0f, 0.0f},
                            marker + glm::vec3{0.25f, 0.0f, 0.0f},
                            NavigationPortalColor);
                        Renderer::addDebugLine(
                            marker + glm::vec3{0.0f, 0.0f, -0.25f},
                            marker + glm::vec3{0.0f, 0.0f, 0.25f},
                            NavigationPortalColor);
                        if (portal.connectedToLoadedNeighbor) {
                            Renderer::addDebugLine(
                                portal.position + glm::vec3{0.0f, 0.25f, 0.0f},
                                portal.connectedNeighborPosition + glm::vec3{0.0f, 0.25f, 0.0f},
                                NavigationPortalColor);
                        }
                    }
                }
            }
        }

        if (settings.navigationBlockerBounds) {
            for (const Engine::SceneNavigationDebugRequest& request : runtime.sceneNavigationGeometry.debugRequests()) {
                Renderer::addDebugAabb(request.bounds, SceneNavigationColor);
            }
        }

        if (settings.collisionBounds) {
            for (const Engine::ScenePhysicsDebugRequest& request : runtime.physics.debugRequests()) {
                Renderer::addDebugLine(request.start, request.end, PhysicsColor);
                if (glm::dot(request.extents, request.extents) > 0.0f) {
                    Renderer::Aabb bounds;
                    bounds.min = request.position - request.extents;
                    bounds.max = request.position + request.extents;
                    Renderer::addDebugAabb(bounds, PhysicsColor);
                }
            }
        }

        if (settings.colliderShapes) {
            uint32_t submittedColliderShapeLines = 0;
            for (const Engine::ScenePhysicsDebugColliderShape& collider : runtime.physics.debugColliderShapes()) {
                addColliderShapeDebug(collider, submittedColliderShapeLines, settings.maxColliderShapeLines, PhysicsColor);
                if (submittedColliderShapeLines >= settings.maxColliderShapeLines) {
                    break;
                }
            }
        }

        if (settings.navigationCurrentPath) {
            if (runtime.cursorTraceDebugVisible &&
                runtime.lastCursorTraceRay.status == Engine::CursorTraceStatus::Success) {
                const Engine::CursorWorldRay& ray = runtime.lastCursorTraceRay;
                const Engine::CursorNavigationProjectionResult& projection = runtime.lastCursorTraceProjection;
                const float debugDistance =
                    projection.sampleCount > 0 ? std::max(projection.distance, 8.0f) : 512.0f;
                const glm::vec3 traceEnd = ray.origin + ray.direction * debugDistance;
                Renderer::addDebugLine(ray.origin, traceEnd, CursorTraceColor);
                if (projection.status == Engine::CursorTraceStatus::Success) {
                    const glm::vec3 hit = projection.projection.point + glm::vec3{0.0f, 0.25f, 0.0f};
                    Renderer::addDebugLine(
                        hit + glm::vec3{-0.5f, 0.0f, 0.0f},
                        hit + glm::vec3{0.5f, 0.0f, 0.0f},
                        CursorTraceHitColor);
                    Renderer::addDebugLine(
                        hit + glm::vec3{0.0f, 0.0f, -0.5f},
                        hit + glm::vec3{0.0f, 0.0f, 0.5f},
                        CursorTraceHitColor);
                    Renderer::addDebugLine(traceEnd, projection.projection.point, CursorTraceHitColor);
                }
            }
            for (const Engine::SceneCharacterDebugRequest& request : runtime.characters.debugRequests()) {
                Renderer::addDebugLine(request.start, request.end, CharacterColor);
            }
            if (const std::optional<Engine::SceneCharacterState> character = runtime.characters.state(runtime.playerCharacter)) {
                const std::vector<glm::vec3>& points = character->activePath.points;
                for (size_t index = 1; index < points.size(); ++index) {
                    Renderer::addDebugLine(points[index - 1], points[index], CharacterColor);
                }
            }
        }

        if (settings.cameraFrustum) {
            Renderer::addDebugFrustum(glm::inverse(renderView.viewProjection), FrustumColor);
        }
    }

}

namespace {

    uint16_t viewportExtent(int extent)
    {
        return static_cast<uint16_t>(std::clamp(extent, 1, 65535));
    }

    uint32_t clearColorFromLinearRgba(const glm::vec4& color)
    {
        const auto toByte = [](float value) {
            return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const uint32_t r = toByte(color.r);
        const uint32_t g = toByte(color.g);
        const uint32_t b = toByte(color.b);
        const uint32_t a = toByte(color.a);
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    void resizeBackbufferIfNeeded(SDL_Window* window, int& currentWidth, int& currentHeight)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        width = std::max(width, 1);
        height = std::max(height, 1);
        if (width == currentWidth && height == currentHeight) {
            return;
        }
        currentWidth = width;
        currentHeight = height;
        bgfx::reset(static_cast<uint32_t>(currentWidth), static_cast<uint32_t>(currentHeight), WindowResetFlags);
    }

    bool pressedAction(const Engine::EventQueue& events, std::string_view action)
    {
        for (const Engine::InputActionEvent& event : events.inputActions()) {
            if (event.phase == Engine::InputActionPhase::Pressed && event.action == action) {
                return true;
            }
        }
        return false;
    }

    Renderer::DebugUi::CameraDebugStats makeCameraDebugStats(const Engine::OrbitCameraController& camera)
    {
        Renderer::DebugUi::CameraDebugStats stats;
        stats.followMode = camera.mode() == Engine::CameraMode::FollowTarget;
        stats.hasTarget = camera.followState().hasTarget;
        stats.pivot = camera.state().pivot;
        stats.targetPosition = camera.followState().targetPosition;
        stats.followOffset = camera.state().followOffset;
        stats.followSmoothing = camera.followSettings().followSmoothing;
        stats.maxFollowLag = camera.followSettings().maxFollowLag;
        return stats;
    }


    void shutdownSharedRuntime(
        Engine::AssetCache& assetCache,
        bool debugUiEnabled,
        SDL_Window* window)
    {
        assetCache.shutdown();
        if (debugUiEnabled) {
            Renderer::DebugUi::shutdown();
        }
        Renderer::shutdownSceneRenderer();
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    int runModernDefaultSceneMode(
        SDL_Window* window,
        bool debugUiEnabled,
        Engine::AssetCache& assetCache,
        Renderer::AtmosphereSettings& atmosphere,
        const ManualEngine::App::ModernSceneLaunchOptions& launchOptions)
    {
        applyAuthoredAtmosphereDefaults(atmosphere);
        atmosphere.fogEnabled = true;
        atmosphere.fogDensity = 0.004f;
        Renderer::setAtmosphereSettings(atmosphere);
        const ManualEngine::App::EditorProjectSettings* editorSettings =
            launchOptions.editorProjectSettings ? &*launchOptions.editorProjectSettings : nullptr;

        float startupWorkMs = 0.0f;
        std::unique_ptr<ModernDefaultSceneRuntime> runtime;
        if constexpr (BuildDebugToolsEnabled) {
            startupWorkMs = measureMilliseconds([&]() {
                runtime = startModernDefaultSceneRuntime(assetCache, editorSettings);
            });
            SDL_Log(
                "Modern default scene startup work complete in %.3f ms: %s",
                startupWorkMs,
                runtime ? runtime->status.c_str() : "failed to create runtime");
        } else {
            runtime = startModernDefaultSceneRuntime(assetCache, editorSettings);
        }

        Engine::CameraSettings cameraSettings;
        if (editorSettings) {
            cameraSettings = editorSettings->camera.settings;
        } else {
            cameraSettings.farPlane = 1200.0f;
            cameraSettings.maxDistance = 420.0f;
            cameraSettings.keyboardPanSpeed = 55.0f;
            cameraSettings.edgePanSpeed = 70.0f;
            cameraSettings.mousePanSensitivity = 0.12f;
            cameraSettings.zoomSensitivity = 28.0f;
        }
        cameraSettings.minPivotXZ = {runtime->bounds.min.x - 40.0f, runtime->bounds.min.z - 40.0f};
        cameraSettings.maxPivotXZ = {runtime->bounds.max.x + 40.0f, runtime->bounds.max.z + 40.0f};
        Engine::OrbitCameraController camera(cameraSettings);
        if (editorSettings) {
            camera.followSettings() = editorSettings->camera.follow;
        }
        frameCameraForBounds(camera, runtime->bounds);
        {
            Engine::CameraState state = camera.state();
            if (editorSettings) {
                state.mode = editorSettings->camera.mode;
                state.pivot = runtime->focus + editorSettings->camera.pivotOffsetFromFocus;
                state.yawRadians = editorSettings->camera.yawRadians;
                state.pitchRadians = editorSettings->camera.pitchRadians;
                state.distance = std::clamp(
                    editorSettings->camera.distance,
                    camera.settings().minDistance,
                    camera.settings().maxDistance);
            } else {
                state.mode = Engine::CameraMode::Free;
                state.pivot = runtime->focus + glm::vec3{0.0f, 12.0f, 0.0f};
                state.yawRadians = glm::radians(35.0f);
                state.pitchRadians = glm::radians(-46.0f);
                state.distance = std::clamp(170.0f, camera.settings().minDistance, camera.settings().maxDistance);
            }
            camera.setState(state);
            camera.clearFollowTarget();
        }

        Engine::InputMappingLoadResult inputMappingLoad = Engine::InputMapping::loadFromYaml("assets/config/input.yaml");
        if (!inputMappingLoad.success) {
            SDL_Log("Using default input mapping: %s", inputMappingLoad.error.c_str());
        }
        Engine::InputMapping inputMapping = inputMappingLoad.mapping;
        Engine::InputState input;
        Engine::EventQueue events;
        Engine::FixedStepLoop loop;

        Renderer::DebugUi::RendererDebugSettings debugSettings;
        if (editorSettings) {
            debugSettings = editorSettings->renderer;
        }
        debugSettings.enableDistanceCulling = BuildDebugToolsEnabled ? debugSettings.enableDistanceCulling : false;
        Renderer::DebugDrawSettings debugDrawSettings;
        if (editorSettings) {
            debugDrawSettings = editorSettings->debugDraw;
        }
        Renderer::DebugUi::CameraDebugControls cameraDebugControls;
        cameraDebugControls.followSmoothing = camera.followSettings().followSmoothing;
        cameraDebugControls.maxFollowLag = camera.followSettings().maxFollowLag;
        ModernEditorLiveApplyHostState editorLiveHostState{
            runtime.get(),
            &debugSettings,
            &debugDrawSettings,
            &camera,
            &cameraDebugControls,
        };
        ManualEngine::App::EditorLiveApplyHost editorLiveHost{
            &editorLiveHostState,
            applyModernEditorLightweightRuntime,
            reloadModernEditorStreamingRuntime,
        };
        Renderer::DebugUi::ModernNavigationDebugControls navigationDebugControls;
        bool navigationCursorPathArmed = false;
        const auto measureDebugOnly = [](auto&& function) -> float {
            if constexpr (BuildDebugToolsEnabled) {
                return measureMilliseconds(std::forward<decltype(function)>(function));
            } else {
                function();
                return 0.0f;
            }
        };

        bool running = true;
        bool debugUiWantsMouse = false;
        bool debugUiWantsKeyboard = false;
        int currentWidth = 1280;
        int currentHeight = 720;
        SDL_GetWindowSize(window, &currentWidth, &currentHeight);
        currentWidth = std::max(currentWidth, 1);
        currentHeight = std::max(currentHeight, 1);
        FramePerformanceTimings frameTimings;
        float previousFrameCpuMs = 0.0f;
        float previousDebugUiBuildMs = 0.0f;
        float previousDebugUiRenderMs = 0.0f;
        float previousBgfxFrameMs = 0.0f;

        while (running) {
            std::chrono::steady_clock::time_point frameCpuStart;
            if constexpr (BuildDebugToolsEnabled) {
                frameCpuStart = std::chrono::steady_clock::now();
            }
            frameTimings = {};
            frameTimings.previousFrameCpuMs = previousFrameCpuMs;
            frameTimings.debugUiBuildMs = previousDebugUiBuildMs;
            frameTimings.debugUiRenderMs = previousDebugUiRenderMs;
            frameTimings.bgfxFrameMs = previousBgfxFrameMs;
            loop.beginFrame();
            input.beginFrame();
            events.clear();

            frameTimings.eventPollingMs = measureDebugOnly([&]() {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (debugUiEnabled) {
                        Renderer::DebugUi::processEvent(event);
                    }
                    bool sendToGameInput = true;
                    switch (event.type) {
                        case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        case SDL_EVENT_MOUSE_BUTTON_UP:
                        case SDL_EVENT_MOUSE_MOTION:
                        case SDL_EVENT_MOUSE_WHEEL:
                            sendToGameInput = !debugUiWantsMouse;
                            break;
                        case SDL_EVENT_KEY_DOWN:
                        case SDL_EVENT_KEY_UP:
                        case SDL_EVENT_TEXT_INPUT:
                            sendToGameInput = !debugUiWantsKeyboard;
                            break;
                        default:
                            break;
                    }
                    if (sendToGameInput) {
                        input.processEvent(event);
                    }
                    if (event.type == SDL_EVENT_QUIT) {
                        running = false;
                    }
                }
            });
            if (debugUiEnabled) {
                debugUiWantsMouse = Renderer::DebugUi::wantsMouseCapture();
                debugUiWantsKeyboard = Renderer::DebugUi::wantsKeyboardCapture();
            }
            resizeBackbufferIfNeeded(window, currentWidth, currentHeight);
            input.setViewportSize(currentWidth, currentHeight);
            frameTimings.inputMappingMs = measureDebugOnly([&]() {
                inputMapping.publishEvents(input, events);
            });
            if (pressedAction(events, "app.quit")) {
                running = false;
            }

            const float aspectRatio = static_cast<float>(currentWidth) / static_cast<float>(std::max(currentHeight, 1));
            Engine::EventQueue cameraEvents;
            glm::vec2 playerMoveAxis{0.0f};
            for (const Engine::InputActionEvent& event : events.inputActions()) {
                cameraEvents.publish(event);
                if (event.action == "player.move" &&
                    event.payloadType == Engine::InputActionPayloadType::Axis2 &&
                    event.source == Engine::InputActionSource::Keyboard) {
                    playerMoveAxis += event.axis2Value;
                    Engine::InputActionEvent cameraPan = event;
                    cameraPan.action = "camera.pan";
                    cameraEvents.publish(cameraPan);
                }
            }

            frameTimings.cameraUpdateMs = measureDebugOnly([&]() {
                camera.followSettings().followSmoothing = cameraDebugControls.followSmoothing;
                camera.followSettings().maxFollowLag = cameraDebugControls.maxFollowLag;
                if (cameraDebugControls.setFreeModeRequested) {
                    cameraDebugControls.setFreeModeRequested = false;
                    camera.setMode(Engine::CameraMode::Free);
                    camera.clearFollowTarget();
                }
                if (cameraDebugControls.setFollowModeRequested) {
                    cameraDebugControls.setFollowModeRequested = false;
                    if (const std::optional<glm::mat4> playerWorld = runtime->scene.worldMatrix(runtime->playerActor)) {
                        camera.setMode(Engine::CameraMode::FollowTarget);
                        camera.setFollowTarget(glm::vec3{(*playerWorld)[3]});
                    }
                }
                if (cameraDebugControls.recenterRequested) {
                    cameraDebugControls.recenterRequested = false;
                    frameCameraForBounds(camera, runtime->bounds);
                    camera.clearFollowTarget();
                }
                if (camera.mode() == Engine::CameraMode::FollowTarget) {
                    if (const std::optional<glm::mat4> playerWorld = runtime->scene.worldMatrix(runtime->playerActor)) {
                        camera.setFollowTarget(glm::vec3{(*playerWorld)[3]});
                    } else {
                        camera.setMode(Engine::CameraMode::Free);
                        camera.clearFollowTarget();
                    }
                }
                camera.update(cameraEvents, loop.frameDeltaSeconds());
            });

            {
                Engine::SceneCharacterMoveInput moveInput;
                const float axisLengthSquared = glm::dot(playerMoveAxis, playerMoveAxis);
                if (axisLengthSquared > 0.0001f) {
                    const glm::vec2 axis = playerMoveAxis / std::sqrt(axisLengthSquared);
                    glm::vec3 forward = camera.forward();
                    forward.y = 0.0f;
                    if (glm::dot(forward, forward) <= 0.0001f) {
                        forward = {0.0f, 0.0f, -1.0f};
                    } else {
                        forward = glm::normalize(forward);
                    }
                    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
                    moveInput.direction = right * axis.x + forward * axis.y;
                    moveInput.speedScale = std::clamp(std::sqrt(axisLengthSquared), 0.0f, 1.0f);
                    moveInput.faceDirection = moveInput.direction;
                }
                (void)runtime->characters.setMoveInput(runtime->playerCharacter, moveInput);
            }

            frameTimings.chunkStreamingMs = measureDebugOnly([&]() {
                if (runtime->usingOpenWorldStreaming && runtime->streamingRuntime.initialized()) {
                    runtime->streamingRuntime.pollCompleted(runtime->streamingAsync.pollCompleted());
                    Engine::StreamingFocusInput focus;
                    focus.position = camera.position();
                    if (const std::optional<glm::mat4> playerWorld =
                            runtime->scene.worldMatrix(runtime->playerActor)) {
                        focus.position = glm::vec3{(*playerWorld)[3]};
                        focus.goalFocusPoints.push_back(camera.position());
                    }
                    focus.velocity = glm::vec3{0.0f};
                    focus.predictionSeconds = 1.0f;
                    focus.maxPredictionDistance = 80.0f;
                    runtime->streamingRuntime.update(
                        focus,
                        runtime->streamingAsync,
                        runtime->streamingMainThread,
                        modernStreamingCallbacks(*runtime));
                    runtime->streamingBudget.beginFrame({2.0f, true});
                    runtime->streamingMainThread.drain(runtime->streamingBudget);
                }
            });

            if (std::optional<Engine::SceneTransform> playerTransform =
                    runtime->scene.localTransform(runtime->playerActor)) {
                const float terrainY = runtime->terrain.sampleHeight(
                    playerTransform->translation.x,
                    playerTransform->translation.z).value_or(runtime->focus.y);
                const bool hasTerrainCollider = runtime->terrainPhysicsColliderCount > 0 ||
                    !runtime->streamingPhysicsColliders.empty();
                if (!hasTerrainCollider) {
                    runtime->characters.setEnabled(runtime->playerCharacter, false);
                } else if (const std::optional<Engine::SceneCharacterDescriptor> character =
                        runtime->characters.descriptor(runtime->playerCharacter);
                    character && !character->enabled) {
                    runtime->characters.setEnabled(runtime->playerCharacter, true);
                    (void)resetModernPlayerAboveTerrain(*runtime, 4.0f);
                }
                if (playerTransform->translation.y < terrainY - 8.0f) {
                    if (resetModernPlayerAboveTerrain(*runtime)) {
                        runtime->navigationStatus =
                            "Reset modern test character above terrain after it fell below the loaded surface.";
                    } else {
                        runtime->navigationStatus =
                            "Failed to reset modern test character after it fell below terrain.";
                        ++runtime->warningCount;
                    }
                    SDL_Log("%s", runtime->navigationStatus.c_str());
                }
            }

            frameTimings.sceneFixedTickMs = measureDebugOnly([&]() {
                while (loop.shouldRunFixedUpdate()) {
                    runtime->scene.tickFixed(loop.fixedDeltaSeconds());
                    loop.consumeFixedUpdate();
                }
            });
            frameTimings.sceneFrameTickMs = measureDebugOnly([&]() {
                runtime->scene.tickFrame(loop.frameDeltaSeconds());
            });

            const Engine::CameraMatrices matrices = camera.matrices(aspectRatio);
            const Renderer::RenderView renderView{
                0,
                matrices.view,
                matrices.projection,
                matrices.projection * matrices.view,
                camera.position(),
                viewportExtent(currentWidth),
                viewportExtent(currentHeight),
                debugSettings.layerMask,
                BuildDebugToolsEnabled ? debugSettings.enableDistanceCulling : false,
            };

            if (navigationCursorPathArmed && input.wasMouseButtonPressed(Engine::MouseButton::Left)) {
                navigationCursorPathArmed = false;
                const Engine::CursorWorldRay cursorRay =
                    Engine::cursorWorldRayFromViewProjection(
                        input.mousePosition(),
                        input.viewportSize(),
                        renderView.viewProjection);
                runtime->navigationStatus =
                    requestModernCharacterNavigationTest(*runtime, cursorRay);
                debugDrawSettings.navigationCurrentPath = true;
                debugDrawSettings.navigationMeshEdges = true;
                SDL_Log("%s", runtime->navigationStatus.c_str());
            }

            frameTimings.debugPrimitiveEnqueueMs = measureDebugOnly([&]() {
                if constexpr (BuildDebugToolsEnabled) {
                    enqueueModernDebugPrimitives(*runtime, renderView, debugDrawSettings);
                } else {
                    Renderer::clearDebugPrimitives();
                }
            });
            bgfx::setViewClear(
                0,
                BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                clearColorFromLinearRgba(atmosphere.skyColor),
                1.0f,
                0);
            bgfx::setViewRect(0, 0, 0, viewportExtent(currentWidth), viewportExtent(currentHeight));
            bgfx::setViewTransform(0, &matrices.view, &matrices.projection);
            if (debugUiEnabled) {
                const bgfx::ViewId viewOrder[] = {0, 1};
                bgfx::setViewOrder(0, 2, viewOrder);
            }
            Renderer::SceneDrawStats drawStats;
            frameTimings.drawSubmissionMs = measureDebugOnly([&]() {
                drawStats = Renderer::drawScene(renderView);
            });
            frameTimings.debugPrimitiveDrawMs = measureDebugOnly([&]() {
                if constexpr (BuildDebugToolsEnabled) {
                    Renderer::drawDebugPrimitives(renderView);
                }
            });

            if constexpr (!BuildDebugToolsEnabled) {
                const Engine::CameraState& cameraState = camera.state();
                const glm::vec3 cameraPosition = camera.position();
                SDL_SetWindowTitle(
                    window,
                    ("ManualEngine Modern Default | terrain " + std::to_string(runtime->terrainRendererCount) +
                        " nav " + std::to_string(runtime->terrainNavTileCount) +
                        " meshNav " + std::to_string(runtime->authoredNavigationSourceCount) +
                        " meshPhys " + std::to_string(runtime->authoredPhysicsBodyCount) +
                        " navCache h/m/w " + std::to_string(runtime->navigationCacheHitCount) + "/" +
                        std::to_string(runtime->navigationCacheMissCount + runtime->navigationCacheStaleCount) + "/" +
                        std::to_string(runtime->navigationCacheWriteCount) +
                        " static " + std::to_string(runtime->staticAssetCount) +
                        " animated " + std::to_string(runtime->animatedAssetCount) +
                        " | freecam pos " + std::to_string(static_cast<int>(cameraPosition.x)) + "," +
                        std::to_string(static_cast<int>(cameraPosition.y)) + "," +
                        std::to_string(static_cast<int>(cameraPosition.z)) +
                        " pivot " + std::to_string(static_cast<int>(cameraState.pivot.x)) + "," +
                        std::to_string(static_cast<int>(cameraState.pivot.y)) + "," +
                        std::to_string(static_cast<int>(cameraState.pivot.z))).c_str());
            }

            if (BuildDebugToolsEnabled) {
                Renderer::setDebugDrawSettings(debugDrawSettings);
            }
            if (debugUiEnabled) {
                const auto debugUiBuildStart = std::chrono::steady_clock::now();
                Renderer::DebugUi::beginFrame(viewportExtent(currentWidth), viewportExtent(currentHeight));
                Renderer::DebugUi::ModernDebugUiState debugState =
                    makeModernDebugUiState(*runtime, frameTimings, startupWorkMs);
                debugState.scene.mode = launchOptions.debugSceneModeLabel;
                Renderer::DebugUi::showModernDebug(
                    drawStats,
                    debugSettings,
                    atmosphere,
                    debugDrawSettings,
                    debugState,
                    makeCameraDebugStats(camera),
                    &cameraDebugControls,
                    &navigationDebugControls);
                if (launchOptions.editorMode && launchOptions.showEditorUi && launchOptions.editorUiState) {
                    launchOptions.showEditorUi(launchOptions.editorUiState, &editorLiveHost);
                }
                Renderer::setAtmosphereSettings(atmosphere);
                if (navigationDebugControls.clearCacheStatsRequested) {
                    navigationDebugControls.clearCacheStatsRequested = false;
                    runtime->navigationCacheHitCount = 0;
                    runtime->navigationCacheMissCount = 0;
                    runtime->navigationCacheStaleCount = 0;
                    runtime->navigationCacheWriteCount = 0;
                }
                if (navigationDebugControls.rebuildNavigationRequested) {
                    navigationDebugControls.rebuildNavigationRequested = false;
                    if (runtime->usingOpenWorldStreaming) {
                        runtime->navigationStatus = rebuildModernNavigationConnectivityFromLoadedTiles(
                            runtime->navigationConnectivity,
                            runtime->navigation);
                        SDL_Log("%s", runtime->navigationStatus.c_str());
                    } else if (std::optional<Engine::TerrainSourceDescriptor> source =
                            runtime->terrain.sourceMetadata(runtime->terrainSource)) {
                        rebuildModernNavigationWithSceneGeometry(*runtime, *source);
                    } else {
                        ++runtime->warningCount;
                    }
                }
                if (navigationDebugControls.requestCharacterNavigationTest) {
                    navigationDebugControls.requestCharacterNavigationTest = false;
                    navigationCursorPathArmed = true;
                    runtime->navigationStatus =
                        "Cursor navigation test armed. Left-click the viewport terrain to request a path.";
                    SDL_Log("%s", runtime->navigationStatus.c_str());
                }
                if (navigationDebugControls.clearCharacterPathRequested) {
                    navigationDebugControls.clearCharacterPathRequested = false;
                    if (!runtime->characters.clearPath(runtime->playerCharacter)) {
                        runtime->navigationStatus = "Failed to clear modern character path.";
                        ++runtime->warningCount;
                    } else {
                        runtime->navigationStatus = "Cleared modern character navigation path.";
                    }
                    SDL_Log("%s", runtime->navigationStatus.c_str());
                }
                if (debugSettings.propMaxDrawDistance >= 0.0f || debugSettings.terrainMaxDrawDistance >= 0.0f) {
                    applyModernDrawDistanceSettings(*runtime, debugSettings);
                }
                frameTimings.debugUiBuildMs =
                    std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - debugUiBuildStart).count();
                frameTimings.debugUiRenderMs = measureMilliseconds([&]() {
                    Renderer::DebugUi::render(1, viewportExtent(currentWidth), viewportExtent(currentHeight));
                });
                previousDebugUiBuildMs = frameTimings.debugUiBuildMs;
                previousDebugUiRenderMs = frameTimings.debugUiRenderMs;
            }

            frameTimings.bgfxFrameMs = measureDebugOnly([&]() {
                bgfx::frame();
            });
            previousBgfxFrameMs = frameTimings.bgfxFrameMs;
            if constexpr (BuildDebugToolsEnabled) {
                previousFrameCpuMs =
                    std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameCpuStart).count();
            }
        }

        runtime->shutdown(assetCache);
        runtime.reset();
        shutdownSharedRuntime(assetCache, debugUiEnabled, window);
        return 0;
    }

}

namespace ManualEngine::App {

int runModernSceneApp(const ModernSceneLaunchOptions& options)
{
    SDL_Window* window = nullptr;
    if (Renderer::initWindow(window) != 0) {
        return 1;
    }
    SDL_SetWindowTitle(window, options.windowTitle.c_str());

    if (Renderer::init_bgfx(window) != 0) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Renderer::configureVertexLayouts();
    if (!Renderer::initSceneRenderer()) {
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const bool debugUiEnabled = BuildDebugToolsEnabled && Renderer::DebugUi::init(window);
    if (BuildDebugToolsEnabled && !debugUiEnabled) {
        SDL_Log("Dear ImGui debug UI failed to initialize; continuing without debug UI.");
    }

    Renderer::AtmosphereSettings atmosphere = Renderer::atmosphereSettings();
    Renderer::setAtmosphereSettings(atmosphere);

    int currentWidth = 1280;
    int currentHeight = 720;
    SDL_GetWindowSize(window, &currentWidth, &currentHeight);
    currentWidth = std::max(currentWidth, 1);
    currentHeight = std::max(currentHeight, 1);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColorFromLinearRgba(atmosphere.skyColor), 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, viewportExtent(currentWidth), viewportExtent(currentHeight));

    Engine::AssetCache assetCache;
    return runModernDefaultSceneMode(window, debugUiEnabled, assetCache, atmosphere, options);
}

}
