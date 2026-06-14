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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Engine/ActorCommand.hpp"
#include "Engine/ActorController.hpp"
#include "Engine/ActorSelection.hpp"
#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Biome.hpp"
#include "Engine/BlockingCollision.hpp"
#include "Engine/ChunkStreamer.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/FixedStepLoop.hpp"
#include "Engine/InputMapping.hpp"
#include "Engine/InteractionHandlerSystem.hpp"
#include "Engine/InteractionSystem.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/NavigationCache.hpp"
#include "Engine/NavigationConnectivity.hpp"
#include "Engine/NavigationProfile.hpp"
#include "Engine/ObjectArchetype.hpp"
#include "Engine/OrbitCamera.hpp"
#include "Engine/PersistentObjectEditor.hpp"
#include "Engine/Picking.hpp"
#include "Engine/ProceduralChunkContent.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"
#include "Engine/WorldNavigationGraph.hpp"
#include "Engine/WorldObjectOverrides.hpp"
#include "Engine/WorldState.hpp"
#include "Engine/input.hpp"
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
    constexpr float DebugPickMaxDistance = 500.0f;
    constexpr float DebugPickTerrainStep = 1.0f;
    constexpr float DebugPickObjectQueryMargin = 2.0f;
    constexpr float DestinationClickDragThresholdPixels = 6.0f;
    constexpr float ActorSelectionDragThresholdPixels = 6.0f;
    constexpr float FormationSpacing = 1.5f;
    constexpr float SampleChunkSize = 96.0f;
    constexpr int32_t SampleChunkLoadRadius = 3;
    constexpr int32_t SampleWorldGraphRadius = 16;

    template <typename Function>
    float measureMilliseconds(Function&& function)
    {
        const auto start = std::chrono::steady_clock::now();
        function();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(end - start).count();
    }

    struct FramePerformanceTimings {
        float chunkStreamingMs = 0.0f;
        float navTileSyncMs = 0.0f;
        float connectivityMs = 0.0f;
        float worldGraphMs = 0.0f;
        float pickingMs = 0.0f;
        float drawSubmissionMs = 0.0f;
    };

    struct NavigationSyncResult {
        bool tileSetChanged = false;
        bool blockerStatsUpdated = false;
    };

    struct RuntimeObjectArchetypeVisual {
        std::string id;
        Engine::CachedStaticMesh mesh;
        Engine::CachedTexture texture;
        Renderer::MaterialHandle material;
    };

    struct RuntimeBiomeTerrainMaterial {
        std::string biomeId;
        std::array<uint8_t, 4> color{};
        Engine::CachedTexture texture;
        Renderer::MaterialHandle material;
    };

    struct NavigationBlockerStats {
        uint32_t vertices = 0;
        uint32_t triangles = 0;
    };

    struct GeneratedChunkLoadResult {
        Engine::GeneratedChunkData chunk;
        std::optional<Engine::NavigationTileBuildResult> navigation;
        NavigationBlockerStats blockerStats;
        float terrainGenerationMs = 0.0f;
        float navigationBuildMs = 0.0f;
    };

    struct AsyncStreamingState {
        std::unordered_map<Engine::ChunkCoord, Engine::AsyncJobHandle, Engine::ChunkCoordHash> pendingLoads;
        std::deque<GeneratedChunkLoadResult> completedLoads;
        std::deque<Engine::ChunkCoord> pendingUnloads;
        std::unordered_set<Engine::ChunkCoord, Engine::ChunkCoordHash> desiredChunks;
        uint32_t cancelledJobs = 0;
        uint32_t staleJobs = 0;
        uint32_t committedLoadsThisFrame = 0;
        uint32_t committedUnloadsThisFrame = 0;
        float averageTerrainGenerationMs = 0.0f;
        float maxTerrainGenerationMs = 0.0f;
        float averageNavigationBuildMs = 0.0f;
        float maxNavigationBuildMs = 0.0f;
        uint32_t timingSampleCount = 0;

        void recordTiming(float terrainMs, float navMs)
        {
            ++timingSampleCount;
            const float sampleCount = static_cast<float>(timingSampleCount);
            averageTerrainGenerationMs += (terrainMs - averageTerrainGenerationMs) / sampleCount;
            averageNavigationBuildMs += (navMs - averageNavigationBuildMs) / sampleCount;
            maxTerrainGenerationMs = std::max(maxTerrainGenerationMs, terrainMs);
            maxNavigationBuildMs = std::max(maxNavigationBuildMs, navMs);
        }
    };

    bool validBounds(const Renderer::Aabb& bounds)
    {
        return std::isfinite(bounds.min.x) &&
            std::isfinite(bounds.min.y) &&
            std::isfinite(bounds.min.z) &&
            std::isfinite(bounds.max.x) &&
            std::isfinite(bounds.max.y) &&
            std::isfinite(bounds.max.z) &&
            bounds.min.x <= bounds.max.x &&
            bounds.min.y <= bounds.max.y &&
            bounds.min.z <= bounds.max.z;
    }

    void appendAabbNavigationBlocker(Engine::NavigationTerrainBuildData& buildData, const Renderer::Aabb& bounds)
    {
        if (!validBounds(bounds)) {
            return;
        }

        const uint32_t base = static_cast<uint32_t>(buildData.blockingVertices.size());
        buildData.blockingVertices.push_back({bounds.min.x, bounds.min.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.min.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.min.y, bounds.max.z});
        buildData.blockingVertices.push_back({bounds.min.x, bounds.min.y, bounds.max.z});
        buildData.blockingVertices.push_back({bounds.min.x, bounds.max.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.max.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.max.y, bounds.max.z});
        buildData.blockingVertices.push_back({bounds.min.x, bounds.max.y, bounds.max.z});

        constexpr uint32_t boxIndices[] = {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,
            1, 5, 6, 1, 6, 2,
            2, 6, 7, 2, 7, 3,
            3, 7, 4, 3, 4, 0,
        };
        for (uint32_t index : boxIndices) {
            buildData.blockingIndices.push_back(base + index);
        }

        buildData.bounds.min.x = std::min(buildData.bounds.min.x, bounds.min.x);
        buildData.bounds.min.y = std::min(buildData.bounds.min.y, bounds.min.y);
        buildData.bounds.min.z = std::min(buildData.bounds.min.z, bounds.min.z);
        buildData.bounds.max.x = std::max(buildData.bounds.max.x, bounds.max.x);
        buildData.bounds.max.y = std::max(buildData.bounds.max.y, bounds.max.y);
        buildData.bounds.max.z = std::max(buildData.bounds.max.z, bounds.max.z);
    }

    Renderer::Aabb scaledTranslatedBounds(
        const Renderer::Aabb& localBounds,
        const glm::vec3& position,
        const glm::vec3& scale)
    {
        const glm::vec3 minScaled = localBounds.min * scale;
        const glm::vec3 maxScaled = localBounds.max * scale;
        return {
            glm::min(minScaled, maxScaled) + position,
            glm::max(minScaled, maxScaled) + position,
        };
    }

    uint32_t clearColorFromLinearRgba(const glm::vec4& color)
    {
        const auto channel = [](float value) {
            return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        };
        return (channel(color.r) << 24) |
            (channel(color.g) << 16) |
            (channel(color.b) << 8) |
            channel(color.a);
    }

    uint16_t viewportExtent(int value)
    {
        return static_cast<uint16_t>(std::max(value, 1));
    }

    Renderer::MaterialHandle createMaterial(
        const std::string& name,
        Renderer::TextureHandle baseColorTexture,
        const glm::vec4& baseColorFactor = glm::vec4{1.0f})
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = name;
        descriptor.baseColorTexture = baseColorTexture;
        descriptor.baseColorFactor = baseColorFactor;
        descriptor.metallicFactor = 0.0f;
        descriptor.roughnessFactor = 1.0f;
        return Renderer::createMaterial(descriptor);
    }

    const RuntimeObjectArchetypeVisual* findVisual(
        const std::vector<RuntimeObjectArchetypeVisual>& visuals,
        std::string_view archetypeId)
    {
        const auto visualIt = std::ranges::find_if(visuals, [archetypeId](const RuntimeObjectArchetypeVisual& visual) {
            return visual.id == archetypeId;
        });
        return visualIt == visuals.end() ? nullptr : &*visualIt;
    }

    const RuntimeBiomeTerrainMaterial* findTerrainMaterial(
        const std::vector<RuntimeBiomeTerrainMaterial>& materials,
        std::string_view biomeId)
    {
        const auto materialIt = std::ranges::find_if(materials, [biomeId](const RuntimeBiomeTerrainMaterial& material) {
            return material.biomeId == biomeId;
        });
        return materialIt == materials.end() ? nullptr : &*materialIt;
    }

    constexpr uint32_t debugColorAbgr(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return (static_cast<uint32_t>(a) << 24) |
            (static_cast<uint32_t>(b) << 16) |
            (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(r);
    }

    const char* navStatusName(Engine::NavQueryStatus status)
    {
        switch (status) {
            case Engine::NavQueryStatus::Success:
                return "success";
            case Engine::NavQueryStatus::NotInitialized:
                return "not initialized";
            case Engine::NavQueryStatus::NoTile:
                return "no tile";
            case Engine::NavQueryStatus::NoNearestPoly:
                return "no nearest polygon";
            case Engine::NavQueryStatus::NoPath:
                return "no path";
            case Engine::NavQueryStatus::InvalidInput:
                return "invalid input";
            case Engine::NavQueryStatus::Unsupported:
                return "unsupported";
        }
        return "unknown";
    }

    const char* worldRouteStatusName(Engine::WorldNavRouteStatus status)
    {
        switch (status) {
            case Engine::WorldNavRouteStatus::Success:
                return "success";
            case Engine::WorldNavRouteStatus::InvalidEndpoint:
                return "invalid endpoint";
            case Engine::WorldNavRouteStatus::NoGraph:
                return "no graph";
            case Engine::WorldNavRouteStatus::NoRoute:
                return "no route";
        }
        return "unknown";
    }

    const char* navEdgeDirectionName(Engine::NavEdgeDirection direction)
    {
        switch (direction) {
            case Engine::NavEdgeDirection::North:
                return "N";
            case Engine::NavEdgeDirection::South:
                return "S";
            case Engine::NavEdgeDirection::East:
                return "E";
            case Engine::NavEdgeDirection::West:
                return "W";
            case Engine::NavEdgeDirection::Count:
                break;
        }
        return "?";
    }

    std::string makeConnectivitySummary(const Engine::ChunkNavConnectivity* connectivity)
    {
        if (!connectivity) {
            return "no connectivity";
        }

        std::string summary = "chunk " +
            std::to_string(connectivity->coord.x) +
            "," +
            std::to_string(connectivity->coord.z) +
            " biome " +
            connectivity->biomeId +
            " portals";
        for (uint32_t index = 0; index < Engine::NavEdgeDirectionCount; ++index) {
            summary += " ";
            summary += navEdgeDirectionName(static_cast<Engine::NavEdgeDirection>(index));
            summary += ":";
            summary += std::to_string(connectivity->portalsByEdge[index].size());
        }
        summary += connectivity->partial ? " partial" : " complete";
        return summary;
    }

    const char* tileSourceName(Engine::NavigationTileSource source)
    {
        switch (source) {
            case Engine::NavigationTileSource::Unknown:
                return "unknown";
            case Engine::NavigationTileSource::LiveBuild:
                return "live build";
            case Engine::NavigationTileSource::Cache:
                return "cache";
        }
        return "unknown";
    }

    std::string formatFloat(float value, int precision = 2)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return buffer;
    }

    std::string makeTileDiagnosticsSummary(const std::optional<Engine::NavigationTileDiagnostics>& diagnostics)
    {
        if (!diagnostics) {
            return "no tile diagnostics";
        }

        std::string summary = "chunk " +
            std::to_string(diagnostics->coord.x) +
            "," +
            std::to_string(diagnostics->coord.z) +
            " " +
            tileSourceName(diagnostics->source) +
            " status " +
            navStatusName(diagnostics->status) +
            " terrain tris " +
            std::to_string(diagnostics->terrainTriangleCount) +
            " walkable " +
            std::to_string(diagnostics->walkableTerrainTriangleCount) +
            " blockers " +
            std::to_string(diagnostics->blockerTriangleCount) +
            " hf " +
            std::to_string(diagnostics->heightfieldWidth) +
            "x" +
            std::to_string(diagnostics->heightfieldHeight) +
            " spans " +
            std::to_string(diagnostics->compactSpanCount) +
            " contours " +
            std::to_string(diagnostics->contourCount) +
            " polys " +
            std::to_string(diagnostics->navPolygonCount) +
            " detail tris " +
            std::to_string(diagnostics->detailTriangleCount);
        if (!diagnostics->message.empty()) {
            summary += " - ";
            summary += diagnostics->message;
        }
        return summary;
    }

    std::string makeTerrainDiagnosticsSummary(const std::optional<Engine::TerrainTileDiagnostics>& diagnostics)
    {
        if (!diagnostics) {
            return "no terrain diagnostics";
        }

        return "chunk " +
            std::to_string(diagnostics->coord.x) +
            "," +
            std::to_string(diagnostics->coord.z) +
            " biome " +
            diagnostics->biomeId +
            " height " +
            formatFloat(diagnostics->minHeight) +
            ".." +
            formatFloat(diagnostics->maxHeight) +
            " avg " +
            formatFloat(diagnostics->averageHeight) +
            " slope max/avg " +
            formatFloat(diagnostics->maxSlopeDegrees, 1) +
            "/" +
            formatFloat(diagnostics->averageSlopeDegrees, 1) +
            " walkable " +
            formatFloat(diagnostics->navWalkableTrianglePercent, 1) +
            "% res " +
            std::to_string(diagnostics->resolution);
    }

    std::string makeBiomeGenerationSummary(const Engine::BiomeDescriptor* biome)
    {
        if (!biome) {
            return "no biome descriptor";
        }

        return biome->id +
            " base " +
            formatFloat(biome->baseHeight) +
            " rolling amp/fx/fz " +
            formatFloat(biome->rollingAmplitude) +
            "/" +
            formatFloat(biome->rollingFrequencyX, 3) +
            "/" +
            formatFloat(biome->rollingFrequencyZ, 3) +
            " detail amp/f " +
            formatFloat(biome->detailAmplitude) +
            "/" +
            formatFloat(biome->detailFrequency, 3) +
            " nav slope hint " +
            formatFloat(biome->maxNavSlopeDegreesHint, 1);
    }

    std::string makePortalDiagnosticsSummary(const Engine::ChunkPortalDiagnostics* diagnostics)
    {
        if (!diagnostics) {
            return "no portal diagnostics";
        }

        std::string summary = "chunk " +
            std::to_string(diagnostics->coord.x) +
            "," +
            std::to_string(diagnostics->coord.z);
        for (uint32_t index = 0; index < Engine::NavEdgeDirectionCount; ++index) {
            const Engine::NavigationPortalEdgeDiagnostics& edge = diagnostics->edges[index];
            summary += " ";
            summary += navEdgeDirectionName(static_cast<Engine::NavEdgeDirection>(index));
            summary += "[s:";
            summary += std::to_string(edge.sampleCount);
            summary += " a:";
            summary += std::to_string(edge.acceptedPortalCount);
            summary += " c:";
            summary += std::to_string(edge.connectedPortalCount);
            summary += " no:";
            summary += std::to_string(edge.rejectedNoNearestPolyCount);
            summary += " band:";
            summary += std::to_string(edge.rejectedEdgeBandCount);
            summary += " reach:";
            summary += std::to_string(edge.rejectedCenterReachabilityCount);
            summary += " merge:";
            summary += std::to_string(edge.mergedDuplicateCount);
            summary += "]";
        }
        return summary;
    }

    std::string makeActorCommandDiagnosticsSummary(const Engine::ActorCommandDiagnostics& diagnostics)
    {
        if (!diagnostics.hasCommand) {
            return "no command";
        }

        std::string summary = "direct " +
            std::string{navStatusName(diagnostics.directLocalStatus)} +
            (diagnostics.directPathComplete ? " complete" : " incomplete");
        if (!diagnostics.directLocalMessage.empty()) {
            summary += " (";
            summary += diagnostics.directLocalMessage;
            summary += ")";
        }
        if (diagnostics.routeAttempted) {
            summary += "; route ";
            summary += worldRouteStatusName(diagnostics.routeStatus);
            if (!diagnostics.routeMessage.empty()) {
                summary += " (";
                summary += diagnostics.routeMessage;
                summary += ")";
            }
        }
        if (diagnostics.hasCurrentWaypointChunk) {
            summary += "; waypoint ";
            summary += std::to_string(diagnostics.currentWaypointIndex);
            summary += " chunk ";
            summary += std::to_string(diagnostics.currentWaypointChunk.x);
            summary += ",";
            summary += std::to_string(diagnostics.currentWaypointChunk.z);
            summary += diagnostics.localTileAvailable ? " tile loaded" : " tile missing";
        }
        if (!diagnostics.finalReason.empty()) {
            summary += "; ";
            summary += diagnostics.finalReason;
        }
        return summary;
    }

    const char* actorPathStatusName(Engine::ActorPathStatus status)
    {
        switch (status) {
            case Engine::ActorPathStatus::Idle:
                return "idle";
            case Engine::ActorPathStatus::Pathing:
                return "pathing";
            case Engine::ActorPathStatus::Moving:
                return "moving";
            case Engine::ActorPathStatus::Blocked:
                return "blocked";
            case Engine::ActorPathStatus::Repathing:
                return "repathing";
            case Engine::ActorPathStatus::Arrived:
                return "arrived";
            case Engine::ActorPathStatus::Failed:
                return "failed";
            case Engine::ActorPathStatus::Cancelled:
                return "cancelled";
        }
        return "unknown";
    }

    const char* actorRouteStatusName(Engine::ActorRouteStatus status)
    {
        switch (status) {
            case Engine::ActorRouteStatus::None:
                return "none";
            case Engine::ActorRouteStatus::Planning:
                return "planning";
            case Engine::ActorRouteStatus::MovingToWaypoint:
                return "moving to waypoint";
            case Engine::ActorRouteStatus::WaitingForLocalTile:
                return "waiting for local tile";
            case Engine::ActorRouteStatus::Arrived:
                return "arrived";
            case Engine::ActorRouteStatus::Failed:
                return "failed";
            case Engine::ActorRouteStatus::Cancelled:
                return "cancelled";
        }
        return "unknown";
    }

    Renderer::DebugUi::NavigationAgentDebugSettings toDebugAgentSettings(const Engine::NavAgentSettings& settings)
    {
        return {
            settings.radius,
            settings.height,
            settings.maxSlopeDegrees,
            settings.maxClimb,
        };
    }

    Engine::NavAgentSettings toEngineAgentSettings(const Renderer::DebugUi::NavigationAgentDebugSettings& settings)
    {
        return {
            settings.radius,
            settings.height,
            settings.maxSlopeDegrees,
            settings.maxClimb,
        };
    }

    Renderer::DebugUi::NavigationBuildDebugSettings toDebugBuildSettings(const Engine::NavBuildSettings& settings)
    {
        return {
            settings.cellSize,
            settings.cellHeight,
        };
    }

    void applyDebugBuildSettings(
        const Renderer::DebugUi::NavigationBuildDebugSettings& debugSettings,
        Engine::NavBuildSettings& settings)
    {
        settings.cellSize = debugSettings.cellSize;
        settings.cellHeight = debugSettings.cellHeight;
    }

    std::string joinTags(const std::vector<std::string>& tags)
    {
        std::string result;
        for (const std::string& tag : tags) {
            if (!result.empty()) {
                result += ", ";
            }
            result += tag;
        }
        return result;
    }

    bool pressedAction(const Engine::EventQueue& events, std::string_view actionName)
    {
        return std::ranges::any_of(events.inputActions(), [actionName](const Engine::InputActionEvent& event) {
            return event.action == actionName &&
                event.payloadType == Engine::InputActionPayloadType::Digital &&
                event.phase == Engine::InputActionPhase::Pressed;
        });
    }

    bool releasedAction(const Engine::EventQueue& events, std::string_view actionName)
    {
        return std::ranges::any_of(events.inputActions(), [actionName](const Engine::InputActionEvent& event) {
            return event.action == actionName &&
                event.payloadType == Engine::InputActionPayloadType::Digital &&
                event.phase == Engine::InputActionPhase::Released;
        });
    }

    const char* cameraModeName(Engine::CameraMode mode)
    {
        switch (mode) {
            case Engine::CameraMode::Free:
                return "free";
            case Engine::CameraMode::FollowTarget:
                return "follow_target";
        }
        return "free";
    }

    void recenterCameraOnPlayer(Engine::OrbitCameraController& camera, const glm::vec3& playerPosition)
    {
        Engine::CameraState state = camera.state();
        state.pivot = playerPosition + state.followOffset;
        camera.setFollowTarget(playerPosition);
        camera.setState(state);
    }

    Renderer::DebugUi::CameraDebugStats makeCameraDebugStats(const Engine::OrbitCameraController& camera)
    {
        Renderer::DebugUi::CameraDebugStats stats;
        stats.followMode = camera.mode() == Engine::CameraMode::FollowTarget;
        stats.hasTarget = camera.followState().hasTarget;
        stats.pivot = camera.state().pivot;
        stats.targetPosition = camera.followState().targetPosition;
        stats.followOffset = camera.followState().offset;
        stats.followSmoothing = camera.followSettings().followSmoothing;
        stats.maxFollowLag = camera.followSettings().maxFollowLag;
        return stats;
    }

    Renderer::DebugUi::BiomeDebugStats makeBiomeDebugStats(
        const Engine::BiomeSystem& biomes,
        const Engine::TerrainSystem& terrain,
        const std::vector<RuntimeBiomeTerrainMaterial>& terrainMaterials,
        const std::array<uint8_t, 4>& fallbackTerrainColor,
        const Engine::OrbitCameraController& camera,
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::World& world,
        const Engine::DebugSelectionState& selection)
    {
        Renderer::DebugUi::BiomeDebugStats stats;
        stats.valid = true;

        const Engine::ChunkCoord cameraChunk = terrain.coordForWorldPosition(camera.state().pivot.x, camera.state().pivot.z);
        const Engine::BiomeSample cameraSample = biomes.sampleChunk(cameraChunk, terrain.settings().chunkSize);
        stats.cameraBiomeId = cameraSample.id;
        stats.cameraChunkX = cameraChunk.x;
        stats.cameraChunkZ = cameraChunk.z;
        if (const Engine::BiomeDescriptor* biome = biomes.descriptor(cameraSample.id)) {
            stats.cameraBiomeDisplayName = biome->displayName;
        }
        if (const RuntimeBiomeTerrainMaterial* material = findTerrainMaterial(terrainMaterials, cameraSample.id)) {
            stats.cameraTerrainMaterialBiomeId = material->biomeId;
            stats.cameraTerrainColor = material->color;
            stats.cameraTerrainUsesFallback = false;
        } else {
            stats.cameraTerrainMaterialBiomeId = cameraSample.id;
            stats.cameraTerrainColor = fallbackTerrainColor;
            stats.cameraTerrainUsesFallback = true;
        }

        if (const std::optional<glm::vec3> playerPosition = actors.position(playerActor, world)) {
            const Engine::ChunkCoord playerChunk = terrain.coordForWorldPosition(playerPosition->x, playerPosition->z);
            stats.hasPlayerBiome = true;
            stats.playerBiomeId = biomes.sampleChunk(playerChunk, terrain.settings().chunkSize).id;
            stats.playerChunkX = playerChunk.x;
            stats.playerChunkZ = playerChunk.z;
        }

        if (selection.hoveredObject) {
            stats.hasHoveredBiome = true;
            stats.hoveredBiomeId = biomes.sampleChunk(selection.hoveredObject->cell, terrain.settings().chunkSize).id;
            stats.hoveredChunkX = selection.hoveredObject->cell.x;
            stats.hoveredChunkZ = selection.hoveredObject->cell.z;
        }

        if (selection.terrainHit) {
            const Engine::BiomeSample hitSample =
                biomes.sample(selection.terrainHit->position.x, selection.terrainHit->position.z);
            stats.hasTerrainHitBiome = true;
            stats.terrainHitBiomeId = hitSample.id;
            stats.terrainHitChunkX = selection.terrainHit->chunk.x;
            stats.terrainHitChunkZ = selection.terrainHit->chunk.z;
            stats.moisture = hitSample.moisture;
            stats.roughness = hitSample.roughness;
            stats.elevation = hitSample.elevation;
        }

        return stats;
    }

    const char* interactionActionName(Engine::InteractionAction action)
    {
        switch (action) {
            case Engine::InteractionAction::Select:
                return "select";
            case Engine::InteractionAction::Interact:
                return "interact";
            case Engine::InteractionAction::RemoveObject:
                return "remove_object";
            case Engine::InteractionAction::PlaceMarker:
                return "place_marker";
        }
        return "unknown";
    }

    const char* interactionTargetName(Engine::InteractionTargetType target)
    {
        switch (target) {
            case Engine::InteractionTargetType::None:
                return "none";
            case Engine::InteractionTargetType::Object:
                return "object";
            case Engine::InteractionTargetType::Terrain:
                return "terrain";
        }
        return "unknown";
    }

    const char* interactionOutcomeName(Engine::InteractionOutcomeType outcome)
    {
        switch (outcome) {
            case Engine::InteractionOutcomeType::None:
                return "none";
            case Engine::InteractionOutcomeType::SelectObject:
                return "select_object";
            case Engine::InteractionOutcomeType::ClearSelection:
                return "clear_selection";
            case Engine::InteractionOutcomeType::Inspect:
                return "inspect";
            case Engine::InteractionOutcomeType::RemoveObject:
                return "remove_object";
            case Engine::InteractionOutcomeType::HarvestResource:
                return "harvest_resource";
            case Engine::InteractionOutcomeType::PlaceMarker:
                return "place_marker";
            case Engine::InteractionOutcomeType::Rejected:
                return "rejected";
        }
        return "unknown";
    }

    Renderer::DebugUi::InteractionDebugStats makeInteractionDebugStats(
        const Engine::InteractionOutcome& outcome)
    {
        const Engine::InteractionEvent& event = outcome.event;
        Renderer::DebugUi::InteractionDebugStats stats;
        stats.hasLastInteraction = true;
        stats.action = interactionActionName(event.action);
        stats.target = interactionTargetName(event.target);
        stats.outcome = interactionOutcomeName(outcome.type);
        stats.stableId = outcome.objectId.toString();
        stats.archetypeId = outcome.archetypeId;
        stats.archetypeDisplayName = outcome.archetypeDisplayName;
        stats.archetypeTags = outcome.archetypeTags;
        stats.chunkX = event.chunk.x;
        stats.chunkZ = event.chunk.z;
        stats.position = outcome.position;
        stats.distance = event.distance;
        stats.resourceId = outcome.resourceId;
        stats.resourceAmount = outcome.resourceAmount;
        stats.status = outcome.status;
        return stats;
    }

    Renderer::DebugUi::DebugPickingStats makeDebugPickingStats(
        const Engine::DebugSelectionState& selection,
        const Engine::World& world,
        const Engine::ObjectArchetypeCatalog& objectArchetypes,
        const Engine::WorldObjectOverrides& objectOverrides,
        const Engine::ChunkStreamer& chunkStreamer)
    {
        Renderer::DebugUi::DebugPickingStats stats;
        stats.mousePosition = selection.mousePosition;
        stats.rayOrigin = selection.ray.origin;
        stats.rayDirection = selection.ray.direction;

        if (selection.hoveredObject) {
            stats.hasHoveredObject = true;
            stats.hoveredObjectId = selection.hoveredObject->object.id;
            stats.hoveredStableId = selection.hoveredObject->objectId.toString();
            stats.hoveredObjectPosition = selection.hoveredObject->position;
            stats.hoveredObjectDistance = selection.hoveredObject->distance;
            stats.hoveredObjectCellX = selection.hoveredObject->cell.x;
            stats.hoveredObjectCellZ = selection.hoveredObject->cell.z;
        }

        if (selection.selectedObject && world.isValid(selection.selectedObject->object)) {
            stats.hasSelectedObject = true;
            stats.selectedObjectId = selection.selectedObject->object.id;
            stats.selectedStableId = world.objectId(selection.selectedObject->object)
                .value_or(selection.selectedObject->objectId)
                .toString();
            Engine::ObjectId selectedStableId = Engine::ObjectId::fromString(stats.selectedStableId);
            stats.selectedIsProcedural = Engine::parseProceduralObjectId(
                selectedStableId,
                stats.selectedArchetypeId,
                stats.selectedLocalSlot
            );
            if (stats.selectedArchetypeId.empty()) {
                stats.selectedArchetypeId = Engine::archetypeIdFromObjectId(selectedStableId);
            }
            if (const Engine::ObjectArchetypeDescriptor* archetype = objectArchetypes.find(stats.selectedArchetypeId)) {
                stats.selectedArchetypeDisplayName = archetype->displayName;
                stats.selectedArchetypeTags = joinTags(archetype->tags);
            }
            if (const std::optional<Engine::Transform> transform = world.transform(selection.selectedObject->object)) {
                stats.selectedObjectPosition = transform->position;
                stats.selectedObjectRotation = transform->rotation;
                stats.selectedObjectScale = transform->scale;
                const Engine::ChunkCoord chunk = chunkStreamer.coordForWorldPosition(transform->position);
                stats.selectedOwnerChunkX = chunk.x;
                stats.selectedOwnerChunkZ = chunk.z;
            } else {
                stats.selectedObjectPosition = world.position(selection.selectedObject->object).value_or(selection.selectedObject->position);
            }
            stats.selectedIsCustom = Engine::isCustomObjectId(selectedStableId);
            stats.selectedHasPersistentOverride = objectOverrides.persistentObject(selectedStableId).has_value();
            stats.selectedEditable = selectedStableId.isValid() && selectedStableId != Engine::ObjectId::player();
            stats.selectedCanReset = stats.selectedEditable && (stats.selectedIsCustom || stats.selectedHasPersistentOverride);
        }

        if (selection.terrainHit) {
            stats.hasTerrainHit = true;
            stats.terrainHitPosition = selection.terrainHit->position;
            stats.terrainHitDistance = selection.terrainHit->distance;
            stats.terrainHitChunkX = selection.terrainHit->chunk.x;
            stats.terrainHitChunkZ = selection.terrainHit->chunk.z;
        }

        return stats;
    }

    Engine::WorldStateSnapshot makeWorldStateSnapshot(
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::World& world,
        const Engine::OrbitCameraController& camera,
        const Engine::ChunkStreamer& chunkStreamer,
        const Engine::TerrainSystem& terrain,
        const Engine::WorldObjectOverrides& objectOverrides)
    {
        Engine::WorldStateSnapshot snapshot;
        snapshot.settings.seed = 0;
        snapshot.settings.chunkSize = chunkStreamer.settings().chunkSize;
        snapshot.settings.loadRadiusChunks = chunkStreamer.settings().loadRadiusChunks;
        snapshot.settings.terrainHeightScale = terrain.settings().heightScale;
        snapshot.player.position = actors.position(playerActor, world).value_or(camera.state().pivot);
        snapshot.camera = camera.state();
        objectOverrides.writeToSnapshot(snapshot);
        return snapshot;
    }

    Renderer::DebugUi::PlayerActorDebugStats makePlayerActorDebugStats(
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::World& world,
        const Engine::TerrainSystem& terrain)
    {
        Renderer::DebugUi::PlayerActorDebugStats stats;
        const std::optional<Engine::ActorState> state = actors.state(playerActor);
        if (!state || !world.isValid(state->object)) {
            return stats;
        }

        stats.valid = true;
        stats.worldObjectId = state->object.id;
        stats.stableId = world.objectId(state->object).value_or(Engine::ObjectId{}).toString();
        stats.position = world.position(state->object).value_or(glm::vec3{});
        stats.velocity = state->velocity;
        stats.facingRadians = state->facingRadians;
        stats.collisionEnabled = state->collisionEnabled;
        stats.collisionRadius = state->collisionRadius;
        stats.collisionHeight = state->collisionHeight;
        stats.blockedX = state->blockedX;
        stats.blockedZ = state->blockedZ;
        stats.collisionHitCount = state->collisionHitCount;
        stats.firstBlockingObjectId = state->firstBlockingObject.id;
        stats.firstBlockingStableId = state->firstBlockingObjectId.toString();
        stats.pathStatus = actorPathStatusName(state->path.status);
        stats.pathDestination = state->path.destination;
        stats.pathPointCount = static_cast<uint32_t>(state->path.path.points.size());
        stats.pathCurrentCorner = state->path.currentCorner;
        stats.pathArrivalRadius = state->path.settings.arrivalRadius;
        stats.pathCornerAdvanceRadius = state->path.settings.cornerAdvanceRadius;
        stats.pathBlockedTicks = state->path.blockedTicks;
        stats.pathRepathAttemptsUsed = state->path.repathAttemptsUsed;
        stats.pathLastQueryStatus = navStatusName(state->path.lastQueryStatus);
        stats.pathLastQueryMessage = state->path.lastQueryMessage;
        stats.routeStatus = actorRouteStatusName(state->route.status);
        stats.routeCurrentWaypoint = state->route.currentWaypointIndex;
        stats.routeWaypointCount = static_cast<uint32_t>(state->route.route.portalWaypoints.size());
        stats.routeFinalDestination = state->route.finalDestination;
        stats.routeMessage = state->route.lastRouteMessage;
        if (const std::optional<float> groundHeight = terrain.sampleHeight(stats.position.x, stats.position.z)) {
            stats.hasGroundHeight = true;
            stats.groundHeight = *groundHeight;
        }
        return stats;
    }

    void enqueueDebugPrimitives(
        const Renderer::RenderView& renderView,
        const Renderer::DebugDrawSettings& settings,
        const Engine::DebugSelectionState& selection,
        const Engine::World& world,
        const Engine::ChunkStreamer& chunkStreamer,
        const Engine::TerrainSystem& terrain,
        const Engine::NavigationSystem& navigation,
        const Engine::NavigationConnectivitySystem& navigationConnectivity,
        const Engine::WorldNavigationGraph& worldNavigationGraph,
        const Engine::WorldNavRoute& lastWorldRoute,
        const std::unordered_set<Engine::ChunkCoord, Engine::ChunkCoordHash>& navigationChunks,
        const Engine::NavAgentSettings& navAgent,
        Engine::WorldObjectHandle playerObject,
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::ActorSelection& actorSelection,
        const std::vector<glm::vec3>& formationDestinations,
        const std::vector<glm::vec3>& failedFormationDestinations)
    {
        Renderer::clearDebugPrimitives();
        if (!settings.enabled) {
            return;
        }

        constexpr uint32_t SelectedColor = debugColorAbgr(255, 220, 40);
        constexpr uint32_t CollisionColor = debugColorAbgr(255, 64, 64);
        constexpr uint32_t ChunkColor = debugColorAbgr(70, 120, 255);
        constexpr uint32_t TerrainColor = debugColorAbgr(80, 220, 240);
        constexpr uint32_t TerrainSlopeWarningColor = debugColorAbgr(255, 90, 40);
        constexpr uint32_t NavigationColor = debugColorAbgr(160, 255, 80);
        constexpr uint32_t NavigationMeshColor = debugColorAbgr(80, 180, 255);
        constexpr uint32_t NavigationNearestColor = debugColorAbgr(255, 255, 80);
        constexpr uint32_t NavigationBlockerColor = debugColorAbgr(255, 120, 40);
        constexpr uint32_t NavigationPortalColor = debugColorAbgr(255, 220, 80);
        constexpr uint32_t NavigationLinkColor = debugColorAbgr(80, 255, 220);
        constexpr uint32_t WorldGraphNodeColor = debugColorAbgr(180, 160, 255);
        constexpr uint32_t WorldGraphEdgeColor = debugColorAbgr(110, 100, 180);
        constexpr uint32_t WorldGraphRouteColor = debugColorAbgr(255, 255, 120);
        constexpr uint32_t FrustumColor = debugColorAbgr(255, 80, 255);
        constexpr uint32_t ActorColor = debugColorAbgr(80, 255, 120);
        constexpr uint32_t ActorCompletedPathColor = debugColorAbgr(60, 120, 80);
        constexpr uint32_t ActorWarningColor = debugColorAbgr(255, 180, 40);

        if (settings.selectedBounds && selection.selectedObject && world.isValid(selection.selectedObject->object)) {
            if (const std::optional<Renderer::Aabb> bounds = world.worldBounds(selection.selectedObject->object)) {
                Renderer::addDebugAabb(*bounds, SelectedColor);
            }
        }

        const auto addCollisionBounds = [&](Engine::WorldObjectHandle object) {
            if (!world.isValid(object) || !world.collisionEnabled(object)) {
                return;
            }
            if (const std::optional<Renderer::Aabb> bounds = world.worldBounds(object)) {
                Renderer::addDebugAabb(*bounds, CollisionColor);
            }
        };

        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                if (settings.chunkBorders) {
                    if (const std::optional<Engine::ChunkCoord> coord = terrain.tileCoord(terrainTile)) {
                        const float chunkSize = terrain.settings().chunkSize;
                        const float minX = static_cast<float>(coord->x) * chunkSize;
                        const float minZ = static_cast<float>(coord->z) * chunkSize;
                        float y = 0.0f;
                        if (const std::optional<Renderer::Aabb> bounds = terrain.tileWorldBounds(terrainTile)) {
                            y = bounds->max.y + 0.05f;
                        }
                        Renderer::addDebugXZRect(minX, minZ, minX + chunkSize, minZ + chunkSize, y, ChunkColor);
                    }
                }

                if (settings.terrainTileBounds) {
                    if (const std::optional<Renderer::Aabb> bounds = terrain.tileWorldBounds(terrainTile)) {
                        Renderer::addDebugAabb(*bounds, TerrainColor);
                    }
                }

                if (settings.terrainSlopeWarnings) {
                    for (const glm::vec3& sample : terrain.slopeWarningSamples(terrainTile, navAgent.maxSlopeDegrees, 96)) {
                        Renderer::addDebugLine(
                            sample + glm::vec3{0.0f, 0.05f, 0.0f},
                            sample + glm::vec3{0.0f, 0.75f, 0.0f},
                            TerrainSlopeWarningColor);
                    }
                }

                if (settings.collisionBounds) {
                    for (Engine::WorldObjectHandle object : objects) {
                        addCollisionBounds(object);
                    }
                }

                if (settings.navigationBlockerBounds) {
                    for (Engine::WorldObjectHandle object : objects) {
                        if (!world.isValid(object) || object.id == playerObject.id || !world.collisionEnabled(object)) {
                            continue;
                        }
                        if (const std::optional<Renderer::Aabb> bounds = world.worldBounds(object)) {
                            Renderer::addDebugAabb(*bounds, NavigationBlockerColor);
                        }
                    }
                }
            }
        );

        if (settings.collisionBounds) {
            addCollisionBounds(playerObject);
        }

        if (settings.navigationTileBounds) {
            for (Engine::ChunkCoord coord : navigationChunks) {
                if (const std::optional<Renderer::Aabb> bounds = navigation.tileBounds(coord)) {
                    Renderer::addDebugAabb(*bounds, NavigationColor);
                }
            }
        }

        if (settings.navigationMeshEdges) {
            const Engine::NavigationDebugGeometry geometry = navigation.debugGeometry();
            for (const Engine::NavDebugLine& edge : geometry.polygonEdges) {
                Renderer::addDebugLine(edge.a, edge.b, NavigationMeshColor);
            }
        }

        if (settings.navigationNearestPoint && selection.terrainHit) {
            const Engine::NavQueryResult nearest = navigation.nearestNavigablePoint(selection.terrainHit->position, navAgent);
            if (nearest.status == Engine::NavQueryStatus::Success) {
                const float markerSize = 0.25f;
                Renderer::addDebugXZRect(
                    nearest.point.x - markerSize,
                    nearest.point.z - markerSize,
                    nearest.point.x + markerSize,
                    nearest.point.z + markerSize,
                    nearest.point.y + 0.08f,
                    NavigationNearestColor
                );
            }
        }

        if (settings.navigationPortals || settings.navigationConnectivityLinks) {
            for (const auto& [coord, connectivity] : navigationConnectivity.all()) {
                (void)coord;
                for (const std::vector<Engine::ChunkNavPortal>& portals : connectivity.portalsByEdge) {
                    for (const Engine::ChunkNavPortal& portal : portals) {
                        if (settings.navigationPortals) {
                            constexpr float PortalMarkerSize = 0.22f;
                            Renderer::addDebugXZRect(
                                portal.position.x - PortalMarkerSize,
                                portal.position.z - PortalMarkerSize,
                                portal.position.x + PortalMarkerSize,
                                portal.position.z + PortalMarkerSize,
                                portal.position.y + 0.18f,
                                NavigationPortalColor
                            );
                        }

                        if (!settings.navigationConnectivityLinks || !portal.connectedToLoadedNeighbor) {
                            continue;
                        }

                        const glm::vec3 a = portal.position + glm::vec3{0.0f, 0.22f, 0.0f};
                        const glm::vec3 b = portal.connectedNeighborPosition + glm::vec3{0.0f, 0.22f, 0.0f};
                        Renderer::addDebugLine(a, b, NavigationLinkColor);
                    }
                }
            }
        }

        if (settings.cameraFrustum) {
            Renderer::addDebugFrustum(glm::inverse(renderView.viewProjection), FrustumColor);
        }

        if (settings.worldNavigationGraphNodes) {
            for (const auto& [coord, node] : worldNavigationGraph.nodes()) {
                (void)coord;
                constexpr float NodeMarkerSize = 1.4f;
                Renderer::addDebugXZRect(
                    node.position.x - NodeMarkerSize,
                    node.position.z - NodeMarkerSize,
                    node.position.x + NodeMarkerSize,
                    node.position.z + NodeMarkerSize,
                    node.position.y + 0.3f,
                    WorldGraphNodeColor
                );
            }
        }

        if (settings.worldNavigationGraphEdges) {
            for (const Engine::WorldNavEdge& edge : worldNavigationGraph.edges()) {
                if (edge.blocked) {
                    continue;
                }
                const Engine::WorldNavNode* from = worldNavigationGraph.node(edge.from);
                const Engine::WorldNavNode* to = worldNavigationGraph.node(edge.to);
                if (from && to) {
                    Renderer::addDebugLine(
                        from->position + glm::vec3{0.0f, 0.45f, 0.0f},
                        to->position + glm::vec3{0.0f, 0.45f, 0.0f},
                        WorldGraphEdgeColor
                    );
                }
            }
        }

        if (settings.worldNavigationRoute && lastWorldRoute.status == Engine::WorldNavRouteStatus::Success) {
            for (size_t index = 1; index < lastWorldRoute.portalWaypoints.size(); ++index) {
                Renderer::addDebugLine(
                    lastWorldRoute.portalWaypoints[index - 1] + glm::vec3{0.0f, 0.65f, 0.0f},
                    lastWorldRoute.portalWaypoints[index] + glm::vec3{0.0f, 0.65f, 0.0f},
                    WorldGraphRouteColor
                );
            }
        }

        if (settings.selectedBounds) {
            for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                if (const std::optional<Engine::ActorState> actorState = actors.state(actor)) {
                    if (world.isValid(actorState->object)) {
                        if (const std::optional<Renderer::Aabb> bounds = world.worldBounds(actorState->object)) {
                            Renderer::addDebugAabb(*bounds, SelectedColor);
                        }
                    }
                }
            }
        }

        if (settings.actorDestination && settings.navigationCurrentPath) {
            const auto drawActorPath = [&](Engine::ActorHandle actor) {
                const std::optional<Engine::ActorState> actorState = actors.state(actor);
                if (!actorState) {
                    return;
                }

                const glm::vec3 currentPosition = world.position(actorState->object).value_or(actorState->resolvedPosition);
                const Engine::ActorPathState& path = actorState->path;
                if (path.path.points.size() >= 2) {
                    for (size_t index = 1; index < path.path.points.size(); ++index) {
                        const uint32_t segmentColor = index < path.currentCorner
                            ? ActorCompletedPathColor
                            : ActorColor;
                        Renderer::addDebugLine(path.path.points[index - 1], path.path.points[index], segmentColor);
                    }
                }

                if (path.status == Engine::ActorPathStatus::Moving ||
                    path.status == Engine::ActorPathStatus::Blocked ||
                    path.status == Engine::ActorPathStatus::Repathing ||
                    path.status == Engine::ActorPathStatus::Failed) {
                    const uint32_t markerColor =
                        (path.status == Engine::ActorPathStatus::Blocked ||
                            path.status == Engine::ActorPathStatus::Repathing ||
                            path.status == Engine::ActorPathStatus::Failed)
                        ? ActorWarningColor
                        : ActorColor;
                    const float destinationMarker = 0.45f;
                    Renderer::addDebugXZRect(
                        path.destination.x - destinationMarker,
                        path.destination.z - destinationMarker,
                        path.destination.x + destinationMarker,
                        path.destination.z + destinationMarker,
                        path.destination.y + 0.08f,
                        markerColor
                    );
                    if (path.currentCorner < path.path.points.size()) {
                        constexpr uint32_t TargetCornerColor = debugColorAbgr(180, 255, 180);
                        const glm::vec3 corner = path.path.points[path.currentCorner];
                        const float cornerMarker = 0.3f;
                        Renderer::addDebugXZRect(
                            corner.x - cornerMarker,
                            corner.z - cornerMarker,
                            corner.x + cornerMarker,
                            corner.z + cornerMarker,
                            corner.y + 0.1f,
                            TargetCornerColor
                        );
                    }
                }

                if (actorState->hasMovementDebug) {
                    Renderer::addDebugLine(currentPosition, actorState->desiredPosition, ActorColor);
                    const float markerSize = 0.25f;
                    Renderer::addDebugXZRect(
                        actorState->resolvedPosition.x - markerSize,
                        actorState->resolvedPosition.z - markerSize,
                        actorState->resolvedPosition.x + markerSize,
                        actorState->resolvedPosition.z + markerSize,
                        actorState->resolvedPosition.y + 0.05f,
                        ActorColor
                    );
                }
            };

            if (actorSelection.selectedActors().empty()) {
                drawActorPath(playerActor);
            } else {
                for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                    drawActorPath(actor);
                }
            }

            const auto drawActorRouteWarning = [&](Engine::ActorHandle actor) {
                const std::optional<Engine::ActorState> actorState = actors.state(actor);
                if (!actorState ||
                    (actorState->route.status != Engine::ActorRouteStatus::WaitingForLocalTile &&
                        actorState->route.status != Engine::ActorRouteStatus::Failed)) {
                    return;
                }

                glm::vec3 marker = actorState->route.finalDestination;
                if (actorState->route.currentWaypointIndex < actorState->route.route.portalWaypoints.size()) {
                    marker = actorState->route.route.portalWaypoints[actorState->route.currentWaypointIndex];
                }
                constexpr float RouteWarningMarkerSize = 0.6f;
                Renderer::addDebugXZRect(
                    marker.x - RouteWarningMarkerSize,
                    marker.z - RouteWarningMarkerSize,
                    marker.x + RouteWarningMarkerSize,
                    marker.z + RouteWarningMarkerSize,
                    marker.y + 0.25f,
                    ActorWarningColor
                );
            };
            if (actorSelection.selectedActors().empty()) {
                drawActorRouteWarning(playerActor);
            } else {
                for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                    drawActorRouteWarning(actor);
                }
            }

            for (const glm::vec3& destination : formationDestinations) {
                const float markerSize = 0.35f;
                Renderer::addDebugXZRect(
                    destination.x - markerSize,
                    destination.z - markerSize,
                    destination.x + markerSize,
                    destination.z + markerSize,
                    destination.y + 0.12f,
                    ActorColor
                );
            }
            for (const glm::vec3& destination : failedFormationDestinations) {
                const float markerSize = 0.4f;
                Renderer::addDebugXZRect(
                    destination.x - markerSize,
                    destination.z - markerSize,
                    destination.x + markerSize,
                    destination.z + markerSize,
                    destination.y + 0.16f,
                    ActorWarningColor
                );
            }
        }
    }
}

int main(int, char**)
{
    SDL_Window* window = nullptr;
    if (Renderer::initWindow(window) != 0) {
        return 1;
    }

    if (Renderer::init_bgfx(window) != 0) {
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
    bgfx::reset(static_cast<uint32_t>(currentWidth), static_cast<uint32_t>(currentHeight), WindowResetFlags);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColorFromLinearRgba(atmosphere.skyColor), 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, viewportExtent(currentWidth), viewportExtent(currentHeight));

    Engine::AssetCache assetCache;
    const Engine::CachedTexture yellowTexture = assetCache.acquireSolidTexture(220, 190, 70);
    const Engine::CachedTexture squadRedTexture = assetCache.acquireSolidTexture(220, 80, 80);
    const Engine::CachedTexture squadGreenTexture = assetCache.acquireSolidTexture(80, 200, 120);
    const Engine::CachedTexture squadBlueTexture = assetCache.acquireSolidTexture(90, 140, 230);
    const Engine::CachedTexture cyanTexture = assetCache.acquireSolidTexture(80, 190, 210);
    constexpr std::array<uint8_t, 4> FallbackTerrainColor{80, 190, 210, 255};
    const Renderer::MaterialHandle yellowMaterial = createMaterial("sample.player", yellowTexture.handle);
    const Renderer::MaterialHandle squadRedMaterial = createMaterial("sample.squad.red", squadRedTexture.handle);
    const Renderer::MaterialHandle squadGreenMaterial = createMaterial("sample.squad.green", squadGreenTexture.handle);
    const Renderer::MaterialHandle squadBlueMaterial = createMaterial("sample.squad.blue", squadBlueTexture.handle);
    const std::array<Renderer::MaterialHandle, 3> squadMaterials{
        squadRedMaterial,
        squadGreenMaterial,
        squadBlueMaterial,
    };
    const Renderer::MaterialHandle cyanMaterial = createMaterial("sample.terrain", cyanTexture.handle);

    const Engine::ObjectArchetypeLoadResult archetypeLoad =
        Engine::ObjectArchetypeCatalog::loadFromYaml("assets/config/object_archetypes.yaml");
    if (!archetypeLoad.success) {
        SDL_Log("Using default object archetypes: %s", archetypeLoad.error.c_str());
    }
    const Engine::ObjectArchetypeCatalog objectArchetypes = archetypeLoad.catalog;

    const Engine::BiomeLoadResult biomeLoad = Engine::BiomeSystem::loadFromYaml("assets/config/biomes.yaml");
    if (!biomeLoad.success) {
        SDL_Log("Using default biomes: %s", biomeLoad.error.c_str());
    }
    const Engine::BiomeSystem biomes = biomeLoad.system;

    std::vector<RuntimeBiomeTerrainMaterial> biomeTerrainMaterials;
    for (const Engine::BiomeDescriptor* biome : biomes.all()) {
        if (!biome) {
            continue;
        }

        const auto [r, g, b, a] = biome->terrainColor;
        Engine::CachedTexture terrainTexture = assetCache.acquireSolidTexture(r, g, b, a);
        const Renderer::MaterialHandle terrainMaterial =
            createMaterial("biome.terrain." + biome->id, terrainTexture.handle);
        biomeTerrainMaterials.push_back({
            biome->id,
            biome->terrainColor,
            terrainTexture,
            terrainMaterial,
        });
    }

    std::vector<RuntimeObjectArchetypeVisual> archetypeVisuals;
    for (const Engine::ObjectArchetypeDescriptor* archetype : objectArchetypes.all()) {
        if (!archetype) {
            continue;
        }

        Engine::CachedStaticMesh archetypeMesh = archetype->meshPath.empty()
            ? assetCache.acquireFallbackCubeMesh()
            : assetCache.acquireStaticMesh(archetype->meshPath);
        if (archetypeMesh.handle.id == UINT32_MAX) {
            SDL_Log("Falling back to generated cube mesh for archetype '%s'.", archetype->id.c_str());
            archetypeMesh = assetCache.acquireFallbackCubeMesh();
        }

        const auto [r, g, b, a] = archetype->solidColor;
        Engine::CachedTexture archetypeTexture = assetCache.acquireSolidTexture(r, g, b, a);
        const Renderer::MaterialHandle archetypeMaterial =
            createMaterial("archetype." + archetype->id, archetypeTexture.handle);
        archetypeVisuals.push_back({
            archetype->id,
            archetypeMesh,
            archetypeTexture,
            archetypeMaterial,
        });
    }

    Engine::CachedStaticMesh playerMesh = assetCache.acquireFallbackCubeMesh();
    const Renderer::StaticMeshHandle playerStaticMesh = playerMesh.handle;
    Renderer::DebugUi::RendererDebugSettings debugSettings;
    Renderer::DebugDrawSettings debugDrawSettings;
    Renderer::DebugUi::WorldSaveDebugControls worldSaveControls;

    Engine::World world;
    Engine::ActorController actors;
    Engine::BlockingCollisionSystem blockingCollision;
    Engine::SpatialRegistry spatialRegistry({SampleChunkSize, true});
    Engine::TerrainSettings terrainSettings;
    terrainSettings.chunkSize = SampleChunkSize;
    terrainSettings.resolution = 33;
    terrainSettings.heightScale = 1.25f;
    terrainSettings.biomes = &biomes;
    terrainSettings.lodLevels = {{
        {0.0f, 33},
        {128.0f, 17},
        {256.0f, 9},
        {384.0f, 5},
        {576.0f, 3},
        {896.0f, 2},
    }};
    Engine::TerrainSystem terrain(terrainSettings);
    const Engine::NavigationProfileLoadResult navigationProfileLoad =
        Engine::loadNavigationProfileFromYaml("assets/config/navigation_profiles.yaml");
    if (navigationProfileLoad.usedFallback) {
        SDL_Log("Using default navigation profile: %s", navigationProfileLoad.message.c_str());
    }
    std::string activeNavigationProfileId = navigationProfileLoad.profile.id;
    Engine::NavBuildSettings navBuildSettings = navigationProfileLoad.profile.build;
    Engine::NavAgentSettings playerNavAgent = navigationProfileLoad.profile.agent;
    Engine::NavigationSystem navigation(navBuildSettings);
    Engine::NavigationConnectivitySystem navigationConnectivity;
    Engine::WorldNavigationGraph worldNavigationGraph({SampleWorldGraphRadius, SampleChunkSize});
    Engine::NavigationCacheSettings navigationCacheSettings;
    const Engine::NavigationCacheManifest navigationCacheManifest = Engine::NavigationCache::buildManifest(
        navigationCacheSettings,
        SampleChunkSize,
        SampleWorldGraphRadius,
        navBuildSettings,
        playerNavAgent,
        activeNavigationProfileId,
        "assets/config/biomes.yaml",
        "assets/config/object_archetypes.yaml");
    Engine::NavigationCache navigationCache(navigationCacheSettings, navigationCacheManifest);
    navigationCache.ensureManifest();
    const auto refreshNavigationCacheManifest = [&]() {
        navigationCache = Engine::NavigationCache(
            navigationCacheSettings,
            Engine::NavigationCache::buildManifest(
                navigationCacheSettings,
                SampleChunkSize,
                SampleWorldGraphRadius,
                navBuildSettings,
                playerNavAgent,
                activeNavigationProfileId,
                "assets/config/biomes.yaml",
                "assets/config/object_archetypes.yaml"));
        navigationCache.ensureManifest();
    };
    Engine::WorldNavRoute lastWorldRoute;
    Engine::ChunkCoord currentWorldGraphCenter;
    bool hasWorldGraphCenter = false;
    Engine::AsyncWorkQueue asyncWork;
    AsyncStreamingState asyncStreaming;
    uint32_t chunkLoadCommitBudget = 1;
    uint32_t chunkUnloadCommitBudget = 1;
    bool asyncTerrainEnabled = true;
    bool asyncNavigationEnabled = true;
    Renderer::DebugUi::NavigationDebugControls navigationDebugControls;
    navigationDebugControls.agent = toDebugAgentSettings(playerNavAgent);
    navigationDebugControls.build = toDebugBuildSettings(navBuildSettings);
    navigationDebugControls.portalSamplesPerEdge = navigationConnectivity.settings().samplesPerEdge;
    navigationDebugControls.portalEdgeInset = navigationConnectivity.settings().edgeInset;
    navigationDebugControls.portalEdgeBandWidth = navigationConnectivity.settings().edgeBandWidth;
    navigationDebugControls.portalMergeDistance = navigationConnectivity.settings().portalMergeDistance;
    navigationDebugControls.portalNeighborLinkDistance = navigationConnectivity.settings().neighborLinkDistance;
    navigationDebugControls.workerThreadCount = asyncWork.workerCount();
    navigationDebugControls.chunkLoadCommitBudget = chunkLoadCommitBudget;
    navigationDebugControls.chunkUnloadCommitBudget = chunkUnloadCommitBudget;
    navigationDebugControls.asyncTerrainEnabled = asyncTerrainEnabled;
    navigationDebugControls.asyncNavigationEnabled = asyncNavigationEnabled;
    std::unordered_set<Engine::ChunkCoord, Engine::ChunkCoordHash> navigationChunks;
    NavigationBlockerStats navigationBlockerStats;
    std::optional<Engine::ChunkCoord> lastRebuiltNavigationChunk;
    Engine::WorldObjectHandle playerObject;
    Engine::ActorHandle playerActor;
    std::vector<Engine::ActorHandle> demoActors;
    std::vector<Engine::WorldObjectHandle> demoActorObjects;
    Engine::ActorSelection actorSelection;
    bool actorSelectionPressActive = false;
    bool actorSelectionDragging = false;
    glm::vec2 actorSelectionDragStart{};
    glm::vec2 actorSelectionDragEnd{};
    bool actorSelectionDragReleased = false;
    glm::vec2 actorSelectionReleaseStart{};
    glm::vec2 actorSelectionReleaseEnd{};
    std::optional<glm::vec3> lastGroupCommandDestination;
    uint32_t lastGroupCommandSuccessCount = 0;
    uint32_t lastGroupCommandFailureCount = 0;
    std::string lastGroupCommandStatus;
    std::string lastGroupCommandFailureSummary;
    std::vector<glm::vec3> lastFormationDestinations;
    std::vector<glm::vec3> lastFailedFormationDestinations;
    Engine::ChunkStreamer chunkStreamer({SampleChunkSize, SampleChunkLoadRadius});
    Engine::WorldObjectOverrides objectOverrides;
    const Engine::ProceduralChunkContentConfig chunkContentConfig =
        Engine::ProceduralChunkContentConfig::sampleOpenWorldConfig(objectArchetypes, &biomes, SampleChunkSize);
    bool debugUiWantsMouse = false;
    bool debugUiWantsKeyboard = false;
    bool destinationClickTracking = false;
    glm::vec2 destinationClickPressPosition{};
    bool navigationTilesDirty = true;
    bool navigationConnectivityDirty = true;
    bool worldGraphDirty = true;
    bool debugVisibilityDirty = true;
    bool pickingDirty = true;
    FramePerformanceTimings frameTimings;
    const auto cancelCommandedActorPaths = [&]() {
        actors.cancelPath(playerActor);
        for (Engine::ActorHandle actor : demoActors) {
            actors.cancelPath(actor);
        }
    };
    const auto terrainMaterialForBiome = [&](std::string_view biomeId) {
        if (const RuntimeBiomeTerrainMaterial* material = findTerrainMaterial(biomeTerrainMaterials, biomeId)) {
            return material->material;
        }
        return cyanMaterial;
    };
    const auto terrainMaterialForChunk = [&](Engine::ChunkCoord coord) {
        return terrainMaterialForBiome(terrain.sampleChunkBiome(coord).id);
    };
    const auto terrainMaterialForTile = [&](Engine::TerrainTileHandle handle) {
        const std::optional<Engine::BiomeSample> tileBiome = terrain.tileBiome(handle);
        return tileBiome ? terrainMaterialForBiome(tileBiome->id) : cyanMaterial;
    };
    const auto generateChunkData = [&](
        Engine::ChunkCoord coord,
        const Engine::WorldStateSnapshot& overrideSnapshot,
        bool buildNavigation,
        Engine::NavAgentSettings navAgentSnapshot,
        Engine::NavBuildSettings navBuildSettingsSnapshot,
        std::optional<Engine::NavigationTileCacheData> cachedNavigationTile) {
        GeneratedChunkLoadResult result;
        result.chunk.coord = coord;
        result.chunk.renderGroupName = "chunk " + std::to_string(coord.x) + "," + std::to_string(coord.z);

        Engine::WorldObjectOverrides localOverrides;
        localOverrides.replaceFromSnapshot(overrideSnapshot, SampleChunkSize);
        Engine::TerrainSettings workerTerrainSettings = terrainSettings;
        workerTerrainSettings.createRendererResources = false;
        Engine::TerrainSystem workerTerrain(workerTerrainSettings);

        result.terrainGenerationMs = measureMilliseconds([&]() {
            result.chunk.terrain = workerTerrain.generateTileData(coord);
            const std::vector<Engine::ProceduralPropSpawn> propSpawns = chunkContentConfig.propsForChunk(coord);
            result.chunk.props.reserve(propSpawns.size());

            const glm::vec3 chunkOrigin{
                static_cast<float>(coord.x) * SampleChunkSize,
                0.0f,
                static_cast<float>(coord.z) * SampleChunkSize,
            };
            std::unordered_set<std::string> baselineObjectIds;
            const auto appendGeneratedProp = [&](const Engine::ObjectId& objectId,
                                                 std::string_view archetypeId,
                                                 const glm::vec3& position,
                                                 const glm::vec3& rotation,
                                                 const glm::vec3& scale,
                                                 const Renderer::Aabb& localBounds,
                                                 const glm::vec3& angularVelocity) {
                const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(archetypeId);
                result.chunk.props.push_back(Engine::GeneratedChunkProp{
                    objectId,
                    std::string(archetypeId),
                    position,
                    rotation,
                    scale,
                    localBounds,
                    angularVelocity,
                    archetype && Engine::hasTag(*archetype, "blocking"),
                });
            };

            for (const Engine::ProceduralPropSpawn& spawn : propSpawns) {
                baselineObjectIds.insert(spawn.objectId.toString());
                if (localOverrides.isRemoved(spawn.objectId)) {
                    continue;
                }

                if (const std::optional<Engine::SavedPersistentObject> persistent = localOverrides.persistentObject(spawn.objectId)) {
                    if (persistent->chunk != coord) {
                        continue;
                    }

                    const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(persistent->archetypeId);
                    appendGeneratedProp(
                        persistent->id,
                        persistent->archetypeId,
                        persistent->position,
                        persistent->rotation,
                        persistent->scale,
                        archetype ? archetype->localBounds : spawn.localBounds,
                        archetype ? archetype->angularVelocity : spawn.angularVelocity);
                    continue;
                }

                glm::vec3 position = chunkOrigin + spawn.localPosition;
                position.y = Engine::TerrainSystem::sampleGeneratedHeight(
                    result.chunk.terrain,
                    position.x,
                    position.z).value_or(0.0f) + spawn.terrainYOffset;
                appendGeneratedProp(
                    spawn.objectId,
                    spawn.archetypeId,
                    position,
                    {},
                    spawn.scale,
                    spawn.localBounds,
                    spawn.angularVelocity);
            }

            for (const Engine::SavedPersistentObject& persistent : localOverrides.persistentObjectsForChunk(coord)) {
                if (baselineObjectIds.contains(persistent.id.toString()) || localOverrides.isRemoved(persistent.id)) {
                    continue;
                }

                const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(persistent.archetypeId);
                if (!archetype) {
                    continue;
                }

                appendGeneratedProp(
                    persistent.id,
                    persistent.archetypeId,
                    persistent.position,
                    persistent.rotation,
                    persistent.scale,
                    archetype->localBounds,
                    archetype->angularVelocity);
            }
        });

        if (buildNavigation) {
            result.navigationBuildMs = measureMilliseconds([&]() {
                if (cachedNavigationTile) {
                    Engine::NavigationTileBuildResult cachedResult;
                    cachedResult.status = Engine::NavQueryStatus::Success;
                    cachedResult.message = "Loaded terrain navigation tile from cache.";
                    cachedResult.tileData = *cachedNavigationTile;
                    cachedResult.diagnostics.coord = coord;
                    cachedResult.diagnostics.status = Engine::NavQueryStatus::Success;
                    cachedResult.diagnostics.message = cachedResult.message;
                    cachedResult.diagnostics.source = Engine::NavigationTileSource::Cache;
                    cachedResult.diagnostics.bounds = cachedNavigationTile->bounds;
                    cachedResult.diagnostics.agent = navAgentSnapshot;
                    cachedResult.diagnostics.build = navBuildSettingsSnapshot;
                    result.navigation = std::move(cachedResult);
                    return;
                }

                std::optional<Engine::NavigationTerrainBuildData> buildData =
                    Engine::TerrainSystem::navigationBuildData(result.chunk.terrain);
                if (!buildData) {
                    return;
                }
                for (const Engine::GeneratedChunkProp& prop : result.chunk.props) {
                    if (!prop.collisionEnabled) {
                        continue;
                    }
                    appendAabbNavigationBlocker(
                        *buildData,
                        scaledTranslatedBounds(prop.localBounds, prop.position, prop.scale));
                    result.blockerStats.vertices += 8;
                    result.blockerStats.triangles += 12;
                }
                result.navigation =
                    Engine::NavigationSystem::buildTerrainTileData(*buildData, navAgentSnapshot, navBuildSettingsSnapshot);
            });
        }

        return result;
    };
    const Engine::ChunkContentFactory chunkFactory =
        [&terrainMaterialForChunk, &debugSettings, &spatialRegistry, &chunkContentConfig, &objectOverrides, &archetypeVisuals](
            Engine::ChunkCoord coord,
            Engine::World& targetWorld,
            Engine::TerrainSystem& targetTerrain) {
            Engine::ChunkContent content;
            Renderer::RenderGroupDescriptor groupDescriptor;
            groupDescriptor.name = "chunk " + std::to_string(coord.x) + "," + std::to_string(coord.z);
            groupDescriptor.hasChunkCoord = true;
            groupDescriptor.chunkX = coord.x;
            groupDescriptor.chunkZ = coord.z;
            content.renderGroup = Renderer::createRenderGroup(groupDescriptor);
            const Renderer::MaterialHandle terrainMaterial = terrainMaterialForChunk(coord);
            content.terrain = targetTerrain.createTile(coord, terrainMaterial);
            Renderer::setTerrainMaterial(targetTerrain.rendererTerrain(content.terrain), terrainMaterial);
            Renderer::setTerrainRenderLayer(targetTerrain.rendererTerrain(content.terrain), Renderer::RenderLayer::Terrain);
            Renderer::setTerrainVisibilityFlags(targetTerrain.rendererTerrain(content.terrain), Renderer::VisibilityFlags::Visible);
            Renderer::setTerrainMaxDrawDistance(targetTerrain.rendererTerrain(content.terrain), debugSettings.terrainMaxDrawDistance);
            Renderer::setTerrainRenderGroup(targetTerrain.rendererTerrain(content.terrain), content.renderGroup);
            const std::vector<Engine::ProceduralPropSpawn> propSpawns = chunkContentConfig.propsForChunk(coord);
            content.objects.reserve(propSpawns.size());

            const float chunkSize = chunkContentConfig.settings().chunkSize;
            const glm::vec3 chunkOrigin{
                static_cast<float>(coord.x) * chunkSize,
                0.0f,
                static_cast<float>(coord.z) * chunkSize,
            };

            std::unordered_set<std::string> baselineObjectIds;
            const auto spawnObject = [&](
                const Engine::ObjectId& objectId,
                const glm::vec3& position,
                const glm::vec3& rotation,
                const glm::vec3& scale,
                const Renderer::Aabb& localBounds,
                const glm::vec3& angularVelocity,
                std::string_view archetypeId) {
                const RuntimeObjectArchetypeVisual* visual = findVisual(archetypeVisuals, archetypeId);
                if (!visual || visual->mesh.handle.id == UINT32_MAX) {
                    return;
                }

                const Engine::WorldObjectHandle object = targetWorld.createObject(objectId);
                const Renderer::MeshInstanceHandle instance = Renderer::createInstance(visual->mesh.handle);

                Renderer::setInstanceMaterial(instance, visual->material);
                Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
                Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
                Renderer::setInstanceMaxDrawDistance(instance, debugSettings.propMaxDrawDistance);
                Renderer::setInstanceRenderGroup(instance, content.renderGroup);
                targetWorld.attachRendererInstance(object, instance);
                targetWorld.setPosition(object, position);
                targetWorld.setRotation(object, rotation);
                targetWorld.setScale(object, scale);
                targetWorld.setAngularVelocity(object, angularVelocity);
                targetWorld.setLocalBounds(object, localBounds);
                if (const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(archetypeId)) {
                    targetWorld.setCollisionEnabled(object, Engine::hasTag(*archetype, "blocking"));
                }
                spatialRegistry.insert(object, position);
                content.objects.push_back(object);
            };

            for (const Engine::ProceduralPropSpawn& spawn : propSpawns) {
                baselineObjectIds.insert(spawn.objectId.toString());
                if (objectOverrides.isRemoved(spawn.objectId)) {
                    continue;
                }

                if (const std::optional<Engine::SavedPersistentObject> persistent = objectOverrides.persistentObject(spawn.objectId)) {
                    if (persistent->chunk != coord) {
                        continue;
                    }

                    const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(persistent->archetypeId);
                    spawnObject(
                        persistent->id,
                        persistent->position,
                        persistent->rotation,
                        persistent->scale,
                        archetype ? archetype->localBounds : spawn.localBounds,
                        archetype ? archetype->angularVelocity : spawn.angularVelocity,
                        persistent->archetypeId
                    );
                    continue;
                }

                glm::vec3 position = chunkOrigin + spawn.localPosition;
                position.y = targetTerrain.sampleHeight(position.x, position.z).value_or(0.0f) + spawn.terrainYOffset;
                spawnObject(
                    spawn.objectId,
                    position,
                    {},
                    spawn.scale,
                    spawn.localBounds,
                    spawn.angularVelocity,
                    spawn.archetypeId
                );
            }

            for (const Engine::SavedPersistentObject& persistent : objectOverrides.persistentObjectsForChunk(coord)) {
                if (baselineObjectIds.contains(persistent.id.toString()) || objectOverrides.isRemoved(persistent.id)) {
                    continue;
                }

                const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(persistent.archetypeId);
                if (!archetype) {
                    continue;
                }

                spawnObject(
                    persistent.id,
                    persistent.position,
                    persistent.rotation,
                    persistent.scale,
                    archetype->localBounds,
                    archetype->angularVelocity,
                    persistent.archetypeId
                );
            }

            return content;
        };
    const auto commitGeneratedChunk = [&](GeneratedChunkLoadResult& generated) {
        if (chunkStreamer.isLoaded(generated.chunk.coord)) {
            return false;
        }

        Engine::ChunkContent content;
        Renderer::RenderGroupDescriptor groupDescriptor;
        groupDescriptor.name = generated.chunk.renderGroupName;
        groupDescriptor.hasChunkCoord = true;
        groupDescriptor.chunkX = generated.chunk.coord.x;
        groupDescriptor.chunkZ = generated.chunk.coord.z;
        content.renderGroup = Renderer::createRenderGroup(groupDescriptor);

        const Renderer::MaterialHandle terrainMaterial = terrainMaterialForBiome(generated.chunk.terrain.biome.id);
        content.terrain = terrain.createTileFromGenerated(generated.chunk.terrain, terrainMaterial);
        const Renderer::TerrainHandle rendererTerrain = terrain.rendererTerrain(content.terrain);
        Renderer::setTerrainMaterial(rendererTerrain, terrainMaterial);
        Renderer::setTerrainRenderLayer(rendererTerrain, Renderer::RenderLayer::Terrain);
        Renderer::setTerrainVisibilityFlags(rendererTerrain, Renderer::VisibilityFlags::Visible);
        Renderer::setTerrainMaxDrawDistance(rendererTerrain, debugSettings.terrainMaxDrawDistance);
        Renderer::setTerrainRenderGroup(rendererTerrain, content.renderGroup);

        content.objects.reserve(generated.chunk.props.size());
        for (const Engine::GeneratedChunkProp& prop : generated.chunk.props) {
            const RuntimeObjectArchetypeVisual* visual = findVisual(archetypeVisuals, prop.archetypeId);
            if (!visual || visual->mesh.handle.id == UINT32_MAX) {
                continue;
            }

            const Engine::WorldObjectHandle object = world.createObject(prop.objectId);
            const Renderer::MeshInstanceHandle instance = Renderer::createInstance(visual->mesh.handle);
            Renderer::setInstanceMaterial(instance, visual->material);
            Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
            Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
            Renderer::setInstanceMaxDrawDistance(instance, debugSettings.propMaxDrawDistance);
            Renderer::setInstanceRenderGroup(instance, content.renderGroup);
            world.attachRendererInstance(object, instance);
            world.setPosition(object, prop.position);
            world.setRotation(object, prop.rotation);
            world.setScale(object, prop.scale);
            world.setAngularVelocity(object, prop.angularVelocity);
            world.setLocalBounds(object, prop.localBounds);
            world.setCollisionEnabled(object, prop.collisionEnabled);
            spatialRegistry.insert(object, prop.position);
            content.objects.push_back(object);
        }

        if (!chunkStreamer.registerLoadedChunk(generated.chunk.coord, content)) {
            for (Engine::WorldObjectHandle object : content.objects) {
                spatialRegistry.remove(object);
                world.destroyObjectAndRendererInstance(object);
            }
            terrain.destroyTile(content.terrain);
            Renderer::destroyRenderGroup(content.renderGroup);
            return false;
        }

        if (generated.navigation &&
            generated.navigation->status == Engine::NavQueryStatus::Success &&
            generated.navigation->tileData) {
            const Engine::NavigationTileHandle handle =
                navigation.loadTerrainTileFromCache(*generated.navigation->tileData, generated.navigation->diagnostics);
            if (handle.id != UINT32_MAX) {
                navigationChunks.insert(generated.chunk.coord);
                lastRebuiltNavigationChunk = generated.chunk.coord;
                navigationConnectivityDirty = true;
                worldGraphDirty = true;
                if (navigationDebugControls.cacheEnabled &&
                    navigationDebugControls.cacheWriteThrough &&
                    generated.navigation->diagnostics.source != Engine::NavigationTileSource::Cache &&
                    !objectOverrides.hasOverridesForChunk(generated.chunk.coord)) {
                    navigationCache.writeTile(*generated.navigation->tileData);
                }
            }
        }

        asyncStreaming.recordTiming(generated.terrainGenerationMs, generated.navigationBuildMs);
        debugVisibilityDirty = true;
        pickingDirty = true;
        return true;
    };
    const auto applyDebugVisibilitySettings = [&]() {
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle renderGroup) {
                const Renderer::TerrainHandle rendererTerrain = terrain.rendererTerrain(terrainTile);
                Renderer::setTerrainMaterial(rendererTerrain, terrainMaterialForTile(terrainTile));
                Renderer::setTerrainRenderLayer(rendererTerrain, Renderer::RenderLayer::Terrain);
                Renderer::setTerrainVisibilityFlags(rendererTerrain, Renderer::VisibilityFlags::Visible);
                Renderer::setTerrainMaxDrawDistance(rendererTerrain, debugSettings.terrainMaxDrawDistance);
                Renderer::setTerrainRenderGroup(rendererTerrain, renderGroup);

                for (Engine::WorldObjectHandle object : objects) {
                    const Renderer::MeshInstanceHandle instance = world.rendererInstance(object);
                    Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
                    Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
                    Renderer::setInstanceMaxDrawDistance(instance, debugSettings.propMaxDrawDistance);
                    Renderer::setInstanceRenderGroup(instance, renderGroup);
                }
            }
        );
    };
    const auto updateSpatialRegistryForLoadedObjects = [&]() {
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                for (Engine::WorldObjectHandle object : objects) {
                    const std::optional<glm::vec3> position = world.position(object);
                    if (position) {
                        spatialRegistry.update(object, *position);
                    } else {
                        spatialRegistry.remove(object);
                    }
                }
            }
        );
        for (Engine::WorldObjectHandle object : demoActorObjects) {
            if (const std::optional<glm::vec3> position = world.position(object)) {
                spatialRegistry.update(object, *position);
            }
        }
    };
    const auto buildNavigationDataForChunk = [&](
        Engine::TerrainTileHandle terrainTile,
        const std::vector<Engine::WorldObjectHandle>& objects,
        NavigationBlockerStats* blockerStats = nullptr) -> std::optional<Engine::NavigationTerrainBuildData> {
        std::optional<Engine::NavigationTerrainBuildData> buildData = terrain.navigationBuildData(terrainTile);
        if (!buildData) {
            return std::nullopt;
        }

        for (Engine::WorldObjectHandle object : objects) {
            if (object.id == UINT32_MAX || object.id == playerObject.id || !world.isValid(object) || !world.collisionEnabled(object)) {
                continue;
            }

            const std::optional<Renderer::Aabb> bounds = world.worldBounds(object);
            if (!bounds || !validBounds(*bounds)) {
                continue;
            }

            appendAabbNavigationBlocker(*buildData, *bounds);
            if (blockerStats) {
                blockerStats->vertices += 8;
                blockerStats->triangles += 12;
            }
        }

        return buildData;
    };
    const auto recomputeNavigationBlockerStats = [&]() {
        NavigationBlockerStats stats;
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                for (Engine::WorldObjectHandle object : objects) {
                    if (object.id == UINT32_MAX || object.id == playerObject.id || !world.isValid(object) || !world.collisionEnabled(object)) {
                        continue;
                    }
                    const std::optional<Renderer::Aabb> bounds = world.worldBounds(object);
                    if (!bounds || !validBounds(*bounds)) {
                        continue;
                    }
                    stats.vertices += 8;
                    stats.triangles += 12;
                }
            });
        navigationBlockerStats = stats;
    };
    const auto loadedChunksHaveNavigationOverrides = [&]() {
        bool hasOverrides = false;
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>&, Renderer::RenderGroupHandle) {
                if (hasOverrides) {
                    return;
                }
                const std::optional<Engine::ChunkCoord> coord = terrain.tileCoord(terrainTile);
                hasOverrides = coord && objectOverrides.hasOverridesForChunk(*coord);
            }
        );
        return hasOverrides;
    };
    const auto rebuildNavigationConnectivity = [&]() {
        std::vector<Engine::ChunkCoord> loadedNavChunks;
        loadedNavChunks.reserve(navigationChunks.size());
        for (Engine::ChunkCoord coord : navigationChunks) {
            loadedNavChunks.push_back(coord);
        }
        std::ranges::sort(loadedNavChunks, [](Engine::ChunkCoord lhs, Engine::ChunkCoord rhs) {
            return lhs.x == rhs.x ? lhs.z < rhs.z : lhs.x < rhs.x;
        });
        if (navigationDebugControls.cacheEnabled && !loadedChunksHaveNavigationOverrides()) {
            Engine::NavigationConnectivityCacheData cachedConnectivity;
            bool loadedAll = !loadedNavChunks.empty();
            for (Engine::ChunkCoord coord : loadedNavChunks) {
                if (std::optional<Engine::ChunkNavConnectivity> cached = navigationCache.loadConnectivity(coord)) {
                    cachedConnectivity.chunks.push_back(*cached);
                } else {
                    loadedAll = false;
                    break;
                }
            }
            if (loadedAll) {
                navigationConnectivity.loadCacheData(cachedConnectivity);
                return;
            }
        }
        navigationConnectivity.rebuild(loadedNavChunks, navigation, terrain, playerNavAgent);
        if (navigationDebugControls.cacheEnabled &&
            navigationDebugControls.cacheWriteThrough &&
            !loadedChunksHaveNavigationOverrides()) {
            for (const auto& [_, connectivity] : navigationConnectivity.all()) {
                navigationCache.writeConnectivity(connectivity);
            }
        }
    };
    const auto rebuildWorldNavigationGraph = [&](Engine::ChunkCoord centerChunk) {
        if (navigationDebugControls.cacheEnabled && !loadedChunksHaveNavigationOverrides()) {
            if (const std::optional<Engine::WorldNavigationGraphCacheData> cachedGraph =
                    navigationCache.loadGraph(centerChunk)) {
                worldNavigationGraph.loadCacheData(*cachedGraph);
                currentWorldGraphCenter = centerChunk;
                hasWorldGraphCenter = true;
                return;
            }
        }
        worldNavigationGraph.rebuild(centerChunk, terrain, navigationConnectivity);
        currentWorldGraphCenter = centerChunk;
        hasWorldGraphCenter = true;
        if (navigationDebugControls.cacheEnabled &&
            navigationDebugControls.cacheWriteThrough &&
            !loadedChunksHaveNavigationOverrides()) {
            navigationCache.writeGraph(worldNavigationGraph.cacheData());
        }
    };
    const auto updateWorldNavigationGraphCenter = [&](const glm::vec3& centerWorldPosition) {
        const Engine::ChunkCoord centerChunk =
            terrain.coordForWorldPosition(centerWorldPosition.x, centerWorldPosition.z);
        if (!hasWorldGraphCenter || !(centerChunk == currentWorldGraphCenter)) {
            currentWorldGraphCenter = centerChunk;
            hasWorldGraphCenter = true;
            worldGraphDirty = true;
        }
    };
    const auto rebuildNavigationTile = [&](Engine::ChunkCoord targetCoord, bool cancelActivePath = true) {
        bool rebuilt = false;
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                if (rebuilt) {
                    return;
                }
                const std::optional<Engine::ChunkCoord> coord = terrain.tileCoord(terrainTile);
                if (!coord || *coord != targetCoord) {
                    return;
                }

                navigation.destroyTile(targetCoord);
                navigationChunks.erase(targetCoord);
                const bool chunkHasOverrides = objectOverrides.hasOverridesForChunk(targetCoord);
                if (navigationDebugControls.cacheEnabled && !chunkHasOverrides) {
                    if (const std::optional<Engine::NavigationTileCacheData> cachedTile = navigationCache.loadTile(targetCoord)) {
                        const Engine::NavigationTileHandle handle = navigation.loadTerrainTileFromCache(*cachedTile);
                        if (handle.id != UINT32_MAX) {
                            navigationChunks.insert(targetCoord);
                            lastRebuiltNavigationChunk = targetCoord;
                            rebuilt = true;
                            return;
                        }
                    }
                }
                if (const std::optional<Engine::NavigationTerrainBuildData> buildData =
                        buildNavigationDataForChunk(terrainTile, objects)) {
                    const Engine::NavigationTileHandle handle = navigation.buildTerrainTile(*buildData, playerNavAgent);
                    if (handle.id != UINT32_MAX) {
                        navigationChunks.insert(targetCoord);
                        lastRebuiltNavigationChunk = targetCoord;
                        rebuilt = true;
                        if (navigationDebugControls.cacheEnabled &&
                            navigationDebugControls.cacheWriteThrough &&
                            !chunkHasOverrides) {
                            if (const std::optional<Engine::NavigationTileCacheData> tile = navigation.tileCacheData(targetCoord)) {
                                navigationCache.writeTile(*tile);
                            }
                        }
                    }
                }
            }
        );

        if (rebuilt && cancelActivePath) {
            cancelCommandedActorPaths();
        }
        navigationConnectivityDirty = navigationConnectivityDirty || rebuilt;
        worldGraphDirty = worldGraphDirty || rebuilt;
        return rebuilt;
    };
    const auto rebuildNavigationTiles = [&](const std::vector<Engine::ChunkCoord>& coords, bool cancelActivePath = true) {
        bool anyRebuilt = false;
        for (Engine::ChunkCoord coord : coords) {
            anyRebuilt = rebuildNavigationTile(coord, false) || anyRebuilt;
        }
        if (anyRebuilt && cancelActivePath) {
            cancelCommandedActorPaths();
        }
        return anyRebuilt;
    };
    const auto syncNavigationTiles = [&]() {
        NavigationSyncResult result;
        std::unordered_set<Engine::ChunkCoord, Engine::ChunkCoordHash> loadedChunks;
        NavigationBlockerStats syncedBlockerStats;
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                const std::optional<Engine::ChunkCoord> coord = terrain.tileCoord(terrainTile);
                if (!coord) {
                    return;
                }

                loadedChunks.insert(*coord);
                if (navigation.hasTile(*coord)) {
                    return;
                }

                const std::optional<Engine::NavigationTerrainBuildData> buildData =
                    buildNavigationDataForChunk(terrainTile, objects, &syncedBlockerStats);
                result.blockerStatsUpdated = true;
                const bool chunkHasOverrides = objectOverrides.hasOverridesForChunk(*coord);
                if (navigationDebugControls.cacheEnabled && !chunkHasOverrides) {
                    if (const std::optional<Engine::NavigationTileCacheData> cachedTile = navigationCache.loadTile(*coord)) {
                        const Engine::NavigationTileHandle handle = navigation.loadTerrainTileFromCache(*cachedTile);
                        if (handle.id != UINT32_MAX) {
                            navigationChunks.insert(*coord);
                            lastRebuiltNavigationChunk = *coord;
                            result.tileSetChanged = true;
                            return;
                        }
                    }
                }
                if (buildData) {
                    const Engine::NavigationTileHandle handle = navigation.buildTerrainTile(*buildData, playerNavAgent);
                    if (handle.id != UINT32_MAX) {
                        navigationChunks.insert(*coord);
                        lastRebuiltNavigationChunk = *coord;
                        result.tileSetChanged = true;
                        if (navigationDebugControls.cacheEnabled &&
                            navigationDebugControls.cacheWriteThrough &&
                            !chunkHasOverrides) {
                            if (const std::optional<Engine::NavigationTileCacheData> tile = navigation.tileCacheData(*coord)) {
                                navigationCache.writeTile(*tile);
                            }
                        }
                    }
                }
            }
        );
        if (result.blockerStatsUpdated) {
            navigationBlockerStats = syncedBlockerStats;
        }

        std::vector<Engine::ChunkCoord> staleChunks;
        for (Engine::ChunkCoord coord : navigationChunks) {
            if (!loadedChunks.contains(coord)) {
                staleChunks.push_back(coord);
            }
        }
        for (Engine::ChunkCoord coord : staleChunks) {
            navigation.destroyTile(coord);
            navigationChunks.erase(coord);
            result.tileSetChanged = true;
        }
        return result;
    };

    bool running = true;
    Engine::FixedStepLoop loop;
    Engine::InputState input;
    Engine::EventQueue events;
    Engine::InteractionSystem interactionSystem;
    Engine::InteractionHandlerSystem interactionHandlers;
    Engine::InputMappingLoadResult inputMappingLoad = Engine::InputMapping::loadFromYaml("assets/config/input.yaml");
    if (!inputMappingLoad.success) {
        SDL_Log("Using default input mapping: %s", inputMappingLoad.error.c_str());
    }
    Engine::InputMapping inputMapping = inputMappingLoad.mapping;

    Engine::CameraSettings cameraSettings;
    cameraSettings.farPlane = 5000.0f;
    cameraSettings.maxDistance = 700.0f;
    cameraSettings.edgePanSpeed = 70.0f;
    cameraSettings.mousePanSensitivity = 0.18f;
    cameraSettings.zoomSensitivity = 32.0f;
    cameraSettings.minPivotXZ = {-1536.0f, -1536.0f};
    cameraSettings.maxPivotXZ = {1536.0f, 1536.0f};
    Engine::CameraState cameraState;
    cameraState.pivot = {0.0f, 0.0f, 0.0f};
    cameraState.yawRadians = 0.0f;
    cameraState.pitchRadians = glm::radians(-40.0f);
    cameraState.distance = 180.0f;
    Engine::OrbitCameraController camera(cameraSettings, cameraState);
    Renderer::DebugUi::CameraDebugControls cameraDebugControls;
    cameraDebugControls.followSmoothing = camera.followSettings().followSmoothing;
    cameraDebugControls.maxFollowLag = camera.followSettings().maxFollowLag;
    Engine::DebugSelectionState debugSelection;
    Renderer::DebugUi::InteractionDebugStats interactionStats;
    const auto processNavigationDirty = [&]() {
        if (navigationTilesDirty) {
            NavigationSyncResult syncResult;
            frameTimings.navTileSyncMs = measureMilliseconds([&]() {
                syncResult = syncNavigationTiles();
            });
            navigationTilesDirty = false;
            navigationConnectivityDirty = navigationConnectivityDirty || syncResult.tileSetChanged;
        }
        if (navigationConnectivityDirty) {
            frameTimings.connectivityMs = measureMilliseconds([&]() {
                rebuildNavigationConnectivity();
            });
            navigationConnectivityDirty = false;
            worldGraphDirty = true;
        }
        if (worldGraphDirty && hasWorldGraphCenter) {
            frameTimings.worldGraphMs = measureMilliseconds([&]() {
                rebuildWorldNavigationGraph(currentWorldGraphCenter);
            });
            worldGraphDirty = false;
        }
    };
    const auto enqueueChunkLoad = [&](Engine::ChunkCoord coord) {
        if (chunkStreamer.isLoaded(coord) || asyncStreaming.pendingLoads.contains(coord)) {
            return;
        }
        const bool alreadyCompleted = std::ranges::any_of(
            asyncStreaming.completedLoads,
            [&](const GeneratedChunkLoadResult& result) {
                return result.chunk.coord == coord;
            });
        if (alreadyCompleted) {
            return;
        }

        Engine::WorldStateSnapshot overrideSnapshot;
        objectOverrides.writeToSnapshot(overrideSnapshot);
        const Engine::NavAgentSettings navAgentSnapshot = playerNavAgent;
        const Engine::NavBuildSettings navBuildSettingsSnapshot = navBuildSettings;
        const bool buildNav = asyncNavigationEnabled;
        const bool chunkHasOverrides = objectOverrides.hasOverridesForChunk(coord);
        std::optional<Engine::NavigationTileCacheData> cachedNavigationTile;
        if (buildNav && navigationDebugControls.cacheEnabled && !chunkHasOverrides) {
            cachedNavigationTile = navigationCache.loadTile(coord);
        }

        if (!asyncTerrainEnabled) {
            asyncStreaming.completedLoads.push_back(generateChunkData(
                coord,
                overrideSnapshot,
                buildNav,
                navAgentSnapshot,
                navBuildSettingsSnapshot,
                cachedNavigationTile));
            return;
        }

        const Engine::AsyncJobHandle handle = asyncWork.submit(
            "chunk " + std::to_string(coord.x) + "," + std::to_string(coord.z),
            [&, coord, overrideSnapshot, buildNav, navAgentSnapshot, navBuildSettingsSnapshot, cachedNavigationTile](std::stop_token stopToken) -> std::any {
                if (stopToken.stop_requested()) {
                    return GeneratedChunkLoadResult{};
                }
                return generateChunkData(
                    coord,
                    overrideSnapshot,
                    buildNav,
                    navAgentSnapshot,
                    navBuildSettingsSnapshot,
                    cachedNavigationTile);
            });
        if (handle.id != UINT64_MAX) {
            asyncStreaming.pendingLoads.insert({coord, handle});
        }
    };
    const auto updateChunkStreamingAsync = [&](const glm::vec3& centerWorldPosition) {
        asyncStreaming.committedLoadsThisFrame = 0;
        asyncStreaming.committedUnloadsThisFrame = 0;
        bool changed = false;

        const Engine::ChunkCoord center = chunkStreamer.coordForWorldPosition(centerWorldPosition);
        const std::vector<Engine::ChunkCoord> desired = chunkStreamer.desiredChunksAround(center);
        asyncStreaming.desiredChunks = {desired.begin(), desired.end()};

        std::vector<Engine::ChunkCoord> loadedChunks;
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>&, Renderer::RenderGroupHandle) {
                if (const std::optional<Engine::ChunkCoord> coord = terrain.tileCoord(terrainTile)) {
                    loadedChunks.push_back(*coord);
                }
            });
        for (Engine::ChunkCoord coord : loadedChunks) {
            if (asyncStreaming.desiredChunks.contains(coord)) {
                continue;
            }
            if (std::ranges::find(asyncStreaming.pendingUnloads, coord) == asyncStreaming.pendingUnloads.end()) {
                asyncStreaming.pendingUnloads.push_back(coord);
            }
        }

        for (auto it = asyncStreaming.pendingLoads.begin(); it != asyncStreaming.pendingLoads.end();) {
            if (!asyncStreaming.desiredChunks.contains(it->first)) {
                asyncWork.cancel(it->second);
                it = asyncStreaming.pendingLoads.erase(it);
                ++asyncStreaming.cancelledJobs;
            } else {
                ++it;
            }
        }

        for (Engine::ChunkCoord coord : desired) {
            enqueueChunkLoad(coord);
        }

        for (Engine::AsyncCompletedJob& completed : asyncWork.pollCompleted()) {
            if (completed.cancelled) {
                ++asyncStreaming.staleJobs;
                continue;
            }
            if (completed.result.type() == typeid(std::exception_ptr)) {
                ++asyncStreaming.staleJobs;
                continue;
            }
            if (completed.result.type() != typeid(GeneratedChunkLoadResult)) {
                continue;
            }
            GeneratedChunkLoadResult result = std::any_cast<GeneratedChunkLoadResult>(std::move(completed.result));
            asyncStreaming.pendingLoads.erase(result.chunk.coord);
            if (!asyncStreaming.desiredChunks.contains(result.chunk.coord) || chunkStreamer.isLoaded(result.chunk.coord)) {
                ++asyncStreaming.staleJobs;
                continue;
            }
            asyncStreaming.completedLoads.push_back(std::move(result));
        }

        for (uint32_t index = 0; index < chunkLoadCommitBudget && !asyncStreaming.completedLoads.empty(); ++index) {
            GeneratedChunkLoadResult generated = std::move(asyncStreaming.completedLoads.front());
            asyncStreaming.completedLoads.pop_front();
            if (!asyncStreaming.desiredChunks.contains(generated.chunk.coord)) {
                ++asyncStreaming.staleJobs;
                continue;
            }
            changed = commitGeneratedChunk(generated) || changed;
            ++asyncStreaming.committedLoadsThisFrame;
        }

        for (uint32_t index = 0; index < chunkUnloadCommitBudget && !asyncStreaming.pendingUnloads.empty(); ++index) {
            const Engine::ChunkCoord coord = asyncStreaming.pendingUnloads.front();
            asyncStreaming.pendingUnloads.pop_front();
            if (asyncStreaming.desiredChunks.contains(coord) || !chunkStreamer.isLoaded(coord)) {
                continue;
            }
            chunkStreamer.unloadChunk(coord, world, terrain, &spatialRegistry);
            navigation.destroyTile(coord);
            navigationChunks.erase(coord);
            navigationConnectivityDirty = true;
            worldGraphDirty = true;
            changed = true;
            ++asyncStreaming.committedUnloadsThisFrame;
        }

        if (changed) {
            recomputeNavigationBlockerStats();
        }
        return changed;
    };
    const bool initialChunksChanged = updateChunkStreamingAsync(camera.state().pivot);
    navigationTilesDirty = navigationTilesDirty || initialChunksChanged;
    debugVisibilityDirty = debugVisibilityDirty || initialChunksChanged;
    pickingDirty = pickingDirty || initialChunksChanged;
    updateWorldNavigationGraphCenter(camera.state().pivot);
    processNavigationDirty();
    debugVisibilityDirty = terrain.updateLods(camera.position()) || debugVisibilityDirty;
    if (debugVisibilityDirty) {
        applyDebugVisibilitySettings();
        debugVisibilityDirty = false;
    }

    playerObject = world.createObject(Engine::ObjectId::player());
    const Renderer::MeshInstanceHandle playerInstance = Renderer::createInstance(playerStaticMesh);
    Renderer::setInstanceMaterial(playerInstance, yellowMaterial);
    Renderer::setInstanceRenderLayer(playerInstance, Renderer::RenderLayer::Props);
    Renderer::setInstanceVisibilityFlags(playerInstance, Renderer::VisibilityFlags::Visible);
    Renderer::setInstanceMaxDrawDistance(playerInstance, 0.0f);
    world.attachRendererInstance(playerObject, playerInstance);
    world.setScale(playerObject, {0.55f, 1.15f, 0.55f});
    world.setLocalBounds(playerObject, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
    world.setCollisionEnabled(playerObject, false);
    playerActor = actors.createActor(playerObject, {6.0f, 1.15f, 20.0f});
    actors.setPosition(playerActor, {0.0f, 0.0f, 0.0f}, &terrain, &world);
    if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
        spatialRegistry.insert(playerObject, *playerPosition);
    }

    const auto createDemoActor = [&](size_t index, const glm::vec3& position) {
        const Engine::ObjectId objectId =
            Engine::ObjectId::fromString("demo/squad/" + std::to_string(index));
        const Engine::WorldObjectHandle object = world.createObject(objectId);
        const Renderer::MeshInstanceHandle instance = Renderer::createInstance(playerStaticMesh);
        Renderer::setInstanceMaterial(instance, squadMaterials[index % squadMaterials.size()]);
        Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
        Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
        Renderer::setInstanceMaxDrawDistance(instance, 0.0f);
        world.attachRendererInstance(object, instance);
        world.setScale(object, {0.5f, 1.0f, 0.5f});
        world.setLocalBounds(object, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
        world.setCollisionEnabled(object, false);

        Engine::ActorControllerSettings settings;
        settings.movementSpeed = 6.0f;
        settings.groundOffset = 1.0f;
        settings.facingTurnSpeed = 20.0f;
        settings.manualInputCancelsPath = false;
        const Engine::ActorHandle actor = actors.createActor(object, settings);
        actors.setPosition(actor, position, &terrain, &world);
        if (const std::optional<glm::vec3> actorPosition = world.position(object)) {
            spatialRegistry.insert(object, *actorPosition);
        }
        demoActors.push_back(actor);
        demoActorObjects.push_back(object);
    };
    createDemoActor(0, {1.8f, 0.0f, 0.0f});
    createDemoActor(1, {-1.8f, 0.0f, 0.0f});
    createDemoActor(2, {0.0f, 0.0f, 1.8f});

    world.syncRenderState();

    const auto clearPendingChunkWork = [&]() {
        for (const auto& [_, handle] : asyncStreaming.pendingLoads) {
            asyncWork.cancel(handle);
        }
        asyncStreaming.pendingLoads.clear();
        asyncStreaming.completedLoads.clear();
        asyncStreaming.pendingUnloads.clear();
        asyncWork.pollCompleted();
    };

    const auto reloadLoadedChunks = [&]() {
        clearPendingChunkWork();
        chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
        navigation.clear();
        navigationChunks.clear();
        navigationConnectivity.clear();
        worldNavigationGraph.clear();
        hasWorldGraphCenter = false;
        const bool chunksChanged = updateChunkStreamingAsync(camera.state().pivot);
        navigationTilesDirty = true;
        navigationConnectivityDirty = true;
        worldGraphDirty = true;
        debugVisibilityDirty = true;
        pickingDirty = true;
        updateWorldNavigationGraphCenter(camera.state().pivot);
        processNavigationDirty();
        cancelCommandedActorPaths();
        debugVisibilityDirty = terrain.updateLods(camera.position()) || debugVisibilityDirty || chunksChanged;
        if (debugVisibilityDirty) {
            applyDebugVisibilitySettings();
            debugVisibilityDirty = false;
        }
        world.syncRenderState();
    };

    Engine::PersistentObjectEditor objectEditor{{
        &world,
        &spatialRegistry,
        &objectOverrides,
        &objectArchetypes,
        chunkStreamer.settings().chunkSize,
    }};

    const auto applyEditResult = [&](const Engine::PersistentObjectEditResult& result) {
        debugVisibilityDirty = true;
        pickingDirty = true;
        if (result.reloadChunks) {
            reloadLoadedChunks();
        } else if (debugSelection.selectedObject &&
            world.isValid(debugSelection.selectedObject->object) &&
            world.collisionEnabled(debugSelection.selectedObject->object)) {
            rebuildNavigationTile(debugSelection.selectedObject->cell);
            processNavigationDirty();
        }
        if (result.clearSelection) {
            debugSelection = {};
        }
        return result.status;
    };

    const auto resolveInteractionRequest = [&](const Engine::InteractionEvent& event) {
        Engine::InteractionRequest request;
        request.event = event;
        request.objectId = event.objectId;
        if (event.target == Engine::InteractionTargetType::Object && world.isValid(event.object)) {
            request.objectId = world.objectId(event.object).value_or(event.objectId);
        }

        request.archetypeId = Engine::archetypeIdFromObjectId(request.objectId);
        request.archetype = objectArchetypes.find(request.archetypeId);
        return request;
    };

    while (running) {
        frameTimings = {};
        loop.beginFrame();
        input.beginFrame();

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

        int width = currentWidth;
        int height = currentHeight;
        SDL_GetWindowSize(window, &width, &height);
        width = std::max(width, 1);
        height = std::max(height, 1);
        if (width != currentWidth || height != currentHeight) {
            currentWidth = width;
            currentHeight = height;
            bgfx::reset(static_cast<uint32_t>(currentWidth), static_cast<uint32_t>(currentHeight), WindowResetFlags);
        }
        input.setViewportSize(width, height);
        if (debugUiEnabled) {
            Renderer::DebugUi::beginFrame(viewportExtent(width), viewportExtent(height));
        }
        debugUiWantsMouse = debugUiEnabled && Renderer::DebugUi::wantsMouseCapture();
        debugUiWantsKeyboard = debugUiEnabled && Renderer::DebugUi::wantsKeyboardCapture();
        inputMapping.publishEvents(input, events);
        playerNavAgent = toEngineAgentSettings(navigationDebugControls.agent);
        if (pressedAction(events, "player.set_destination")) {
            destinationClickTracking = true;
            destinationClickPressPosition = input.mousePosition();
        }
        bool destinationClickRequested = false;
        if (releasedAction(events, "player.set_destination")) {
            if (destinationClickTracking) {
                const glm::vec2 delta = input.mousePosition() - destinationClickPressPosition;
                destinationClickRequested = glm::dot(delta, delta) <=
                    DestinationClickDragThresholdPixels * DestinationClickDragThresholdPixels;
            }
            destinationClickTracking = false;
        }
        if (pressedAction(events, "player.cancel_destination")) {
            if (actorSelection.selectedActors().empty()) {
                actors.cancelPath(playerActor);
            } else {
                for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                    actors.cancelPath(actor);
                }
            }
            lastGroupCommandStatus = "Destination cancelled.";
            lastGroupCommandSuccessCount = 0;
            lastGroupCommandFailureCount = 0;
            lastGroupCommandFailureSummary.clear();
            lastFormationDestinations.clear();
            lastFailedFormationDestinations.clear();
            worldSaveControls.status = lastGroupCommandStatus;
        }
        if (pressedAction(events, "player.stop")) {
            if (actorSelection.selectedActors().empty()) {
                actors.cancelPath(playerActor);
                lastGroupCommandStatus = "Stopped player actor.";
            } else {
                for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                    actors.cancelPath(actor);
                }
                lastGroupCommandStatus = std::string{"Stopped "} +
                    std::to_string(actorSelection.selectedActors().size()) +
                    " selected actor(s).";
            }
            lastGroupCommandSuccessCount = 0;
            lastGroupCommandFailureCount = 0;
            lastGroupCommandFailureSummary.clear();
            lastFormationDestinations.clear();
            lastFailedFormationDestinations.clear();
            worldSaveControls.status = lastGroupCommandStatus;
        }
        if (input.wasMouseButtonPressed(Engine::MouseButton::Left)) {
            actorSelectionPressActive = true;
            actorSelectionDragging = false;
            actorSelectionDragStart = input.mousePosition();
            actorSelectionDragEnd = actorSelectionDragStart;
        }
        if (actorSelectionPressActive && input.isMouseButtonDown(Engine::MouseButton::Left)) {
            actorSelectionDragEnd = input.mousePosition();
            const glm::vec2 delta = actorSelectionDragEnd - actorSelectionDragStart;
            if (glm::dot(delta, delta) >
                ActorSelectionDragThresholdPixels * ActorSelectionDragThresholdPixels) {
                actorSelectionDragging = true;
            }
        }
        if (actorSelectionPressActive && input.wasMouseButtonReleased(Engine::MouseButton::Left)) {
            if (actorSelectionDragging) {
                actorSelectionDragReleased = true;
                actorSelectionReleaseStart = actorSelectionDragStart;
                actorSelectionReleaseEnd = input.mousePosition();
            }
            actorSelectionPressActive = false;
            actorSelectionDragging = false;
        }
        camera.followSettings().followSmoothing = cameraDebugControls.followSmoothing;
        camera.followSettings().maxFollowLag = cameraDebugControls.maxFollowLag;

        const std::optional<glm::vec3> playerCameraTarget = actors.position(playerActor, world);
        if (pressedAction(events, "camera.toggle_follow")) {
            camera.setMode(camera.mode() == Engine::CameraMode::FollowTarget
                ? Engine::CameraMode::Free
                : Engine::CameraMode::FollowTarget);
            SDL_Log("Camera mode: %s", cameraModeName(camera.mode()));
        }
        if (pressedAction(events, "camera.recenter")) {
            if (camera.mode() == Engine::CameraMode::FollowTarget) {
                camera.resetFollowOffset();
            }
            if (playerCameraTarget) {
                recenterCameraOnPlayer(camera, *playerCameraTarget);
            }
        }
        if (cameraDebugControls.setFreeModeRequested) {
            cameraDebugControls.setFreeModeRequested = false;
            camera.setMode(Engine::CameraMode::Free);
        }
        if (cameraDebugControls.setFollowModeRequested) {
            cameraDebugControls.setFollowModeRequested = false;
            camera.setMode(Engine::CameraMode::FollowTarget);
        }
        if (cameraDebugControls.recenterRequested) {
            cameraDebugControls.recenterRequested = false;
            if (camera.mode() == Engine::CameraMode::FollowTarget) {
                camera.resetFollowOffset();
            }
            if (playerCameraTarget) {
                recenterCameraOnPlayer(camera, *playerCameraTarget);
            }
        }

        const glm::vec3 previousCameraPivot = camera.state().pivot;
        camera.update(events, loop.frameDeltaSeconds(), playerCameraTarget);
        bool chunksChanged = false;
        frameTimings.chunkStreamingMs = measureMilliseconds([&]() {
            chunksChanged = updateChunkStreamingAsync(camera.state().pivot);
        });
        if (chunksChanged) {
            navigationTilesDirty = true;
            debugVisibilityDirty = true;
            pickingDirty = true;
        }
        if (glm::distance(previousCameraPivot, camera.state().pivot) > 0.001f) {
            pickingDirty = true;
        }
        updateWorldNavigationGraphCenter(camera.state().pivot);
        processNavigationDirty();
        if (terrain.updateLods(camera.position())) {
            debugVisibilityDirty = true;
        }
        if (debugVisibilityDirty) {
            applyDebugVisibilitySettings();
            debugVisibilityDirty = false;
        }
        Renderer::setAtmosphereSettings(atmosphere);
        if (BuildDebugToolsEnabled) {
            Renderer::setDebugDrawSettings(debugDrawSettings);
        }

        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColorFromLinearRgba(atmosphere.skyColor), 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, viewportExtent(width), viewportExtent(height));
        const bgfx::ViewId viewOrder[] = {0, 1};
        bgfx::setViewOrder(0, 2, viewOrder);

        const float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
        const Engine::CameraMatrices cameraMatrices = camera.matrices(aspect);
        bgfx::setViewTransform(0, &cameraMatrices.view, &cameraMatrices.projection);
        const Renderer::RenderView renderView{
            0,
            cameraMatrices.view,
            cameraMatrices.projection,
            cameraMatrices.projection * cameraMatrices.view,
            camera.position(),
            viewportExtent(width),
            viewportExtent(height),
            debugSettings.layerMask,
            debugSettings.enableDistanceCulling,
        };

        if (actorSelectionDragReleased) {
            actorSelectionDragReleased = false;
            actorSelection.setSelectedActors(Engine::selectActorsInScreenRect(
                actors,
                world,
                renderView.viewProjection,
                input.viewportSize(),
                actorSelectionReleaseStart,
                actorSelectionReleaseEnd
            ));
            debugSelection.selectedObject = std::nullopt;
            worldSaveControls.status = "Selected " +
                std::to_string(actorSelection.selectedActors().size()) +
                " actor(s).";
        }

        while (loop.shouldRunFixedUpdate()) {
            world.fixedUpdate(loop.fixedDeltaSeconds());
            actors.fixedUpdate(
                playerActor,
                events,
                terrain,
                world,
                spatialRegistry,
                blockingCollision,
                navigation,
                playerNavAgent,
                loop.fixedDeltaSeconds());
            Engine::EventQueue actorNoInputEvents;
            for (Engine::ActorHandle actor : demoActors) {
                actors.fixedUpdate(
                    actor,
                    actorNoInputEvents,
                    terrain,
                    world,
                    spatialRegistry,
                    blockingCollision,
                    navigation,
                    playerNavAgent,
                    loop.fixedDeltaSeconds());
            }
            updateSpatialRegistryForLoadedObjects();
            if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
                spatialRegistry.update(playerObject, *playerPosition);
            }
            loop.consumeFixedUpdate();
        }
        world.syncRenderState();

        const bool interactionNeedsPicking =
            pressedAction(events, "interaction.select") ||
            pressedAction(events, "interaction.interact") ||
            pressedAction(events, "interaction.place_marker");
        const bool pickingNeeded =
            BuildDebugToolsEnabled ||
            pickingDirty ||
            destinationClickRequested ||
            interactionNeedsPicking;
        if (pickingNeeded) {
            frameTimings.pickingMs = measureMilliseconds([&]() {
                debugSelection.mousePosition = input.mousePosition();
                debugSelection.ray = Engine::screenPointToRay(
                    debugSelection.mousePosition,
                    input.viewportSize(),
                    cameraMatrices.view,
                    cameraMatrices.projection
                );
                debugSelection.hoveredObject = Engine::pickNearestObject(
                    debugSelection.ray,
                    spatialRegistry,
                    world,
                    DebugPickMaxDistance,
                    DebugPickObjectQueryMargin
                );
                debugSelection.terrainHit = terrain.raycast(debugSelection.ray, DebugPickMaxDistance, DebugPickTerrainStep);
            });
            pickingDirty = false;
        }
        std::optional<Engine::NavQueryResult> nearestNavigationPoint;
        if (BuildDebugToolsEnabled &&
            debugUiEnabled &&
            debugDrawSettings.navigationNearestPoint &&
            debugSelection.terrainHit) {
            nearestNavigationPoint = navigation.nearestNavigablePoint(debugSelection.terrainHit->position, playerNavAgent);
        }
        if (destinationClickRequested) {
            if (debugSelection.hoveredObject) {
                Engine::InteractionEvent interaction;
                interaction.action = Engine::InteractionAction::Interact;
                interaction.target = Engine::InteractionTargetType::Object;
                interaction.object = debugSelection.hoveredObject->object;
                interaction.objectId = debugSelection.hoveredObject->objectId;
                interaction.objectHitPosition = debugSelection.hoveredObject->position;
                interaction.chunk = debugSelection.hoveredObject->cell;
                interaction.distance = debugSelection.hoveredObject->distance;
                Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
                worldSaveControls.status = outcome.status;
                interactionStats = makeInteractionDebugStats(outcome);
            } else if (debugSelection.terrainHit) {
                std::vector<Engine::ActorHandle> commandedActors = actorSelection.selectedActors();
                if (commandedActors.empty()) {
                    commandedActors.push_back(playerActor);
                }
                lastWorldRoute = {};
                lastGroupCommandDestination = debugSelection.terrainHit->position;
                lastGroupCommandSuccessCount = 0;
                lastGroupCommandFailureCount = 0;
                lastGroupCommandStatus.clear();
                lastGroupCommandFailureSummary.clear();
                lastFormationDestinations.clear();
                lastFailedFormationDestinations.clear();

                for (size_t index = 0; index < commandedActors.size(); ++index) {
                    const glm::vec2 offset =
                        Engine::formationOffsetForActorIndex(index, FormationSpacing);
                    glm::vec3 destination = debugSelection.terrainHit->position + glm::vec3{offset.x, 0.0f, offset.y};
                    if (const std::optional<float> height = terrain.sampleHeight(destination.x, destination.z)) {
                        destination.y = *height;
                    }

                    const Engine::WorldNavRoute route = actors.setRouteDestination(
                        commandedActors[index],
                        destination,
                        worldNavigationGraph,
                        navigation,
                        playerNavAgent,
                        world);
                    if (index == 0) {
                        lastWorldRoute = route;
                    }
                    if (route.status == Engine::WorldNavRouteStatus::Success) {
                        ++lastGroupCommandSuccessCount;
                        lastFormationDestinations.push_back(destination);
                    } else {
                        const Engine::NavQueryResult localPath = actors.setPathDestination(
                            commandedActors[index],
                            destination,
                            navigation,
                            playerNavAgent,
                            world);
                        if (localPath.status == Engine::NavQueryStatus::Success) {
                            ++lastGroupCommandSuccessCount;
                            lastFormationDestinations.push_back(destination);
                        } else {
                            ++lastGroupCommandFailureCount;
                            lastFailedFormationDestinations.push_back(destination);
                            if (lastGroupCommandFailureSummary.empty()) {
                                lastGroupCommandFailureSummary =
                                    std::string{"Route failed for actor "} +
                                    std::to_string(commandedActors[index].id) +
                                    ": " +
                                    worldRouteStatusName(route.status) +
                                    (route.message.empty() ? "" : ". " + route.message);
                            }
                        }
                    }
                }

                lastGroupCommandStatus =
                    std::string{"Move command: "} +
                    std::to_string(lastGroupCommandSuccessCount) +
                    " command(s), " +
                    std::to_string(lastGroupCommandFailureCount) +
                    " failure(s).";
                worldSaveControls.status = lastGroupCommandStatus;
            } else {
                lastGroupCommandStatus = "Move failed: no terrain hit under cursor.";
                lastGroupCommandSuccessCount = 0;
                lastGroupCommandFailureCount = 1;
                lastGroupCommandFailureSummary = lastGroupCommandStatus;
                lastFormationDestinations.clear();
                lastFailedFormationDestinations.clear();
                worldSaveControls.status = lastGroupCommandStatus;
            }
        }
        if (debugSelection.selectedObject && !world.isValid(debugSelection.selectedObject->object)) {
            debugSelection.selectedObject = std::nullopt;
        }

        interactionSystem.publishEvents(events, debugSelection, events);
        for (const Engine::InteractionEvent& interaction : events.interactionEvents()) {
            Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
            if (outcome.type == Engine::InteractionOutcomeType::SelectObject) {
                if (outcome.event.target == Engine::InteractionTargetType::Object && world.isValid(outcome.event.object)) {
                    debugSelection.selectedObject = Engine::ObjectPickHit{
                        outcome.event.object,
                        world.objectId(outcome.event.object).value_or(outcome.objectId),
                        outcome.event.objectHitPosition,
                        outcome.event.distance,
                        outcome.event.chunk,
                    };
                }
            } else if (outcome.type == Engine::InteractionOutcomeType::ClearSelection) {
                debugSelection.selectedObject = std::nullopt;
            } else if (outcome.type == Engine::InteractionOutcomeType::RemoveObject) {
                outcome.status = applyEditResult(objectEditor.removeObject(
                    outcome.event.object,
                    outcome.event.objectId,
                    outcome.event.objectHitPosition
                ));
            } else if (outcome.type == Engine::InteractionOutcomeType::PlaceMarker) {
                outcome.status = outcome.event.target == Engine::InteractionTargetType::Terrain
                    ? applyEditResult(objectEditor.placeArchetype(
                        Engine::TerrainPickHit{
                            outcome.event.terrainHitPosition,
                            outcome.event.distance,
                            outcome.event.chunk,
                        },
                        worldSaveControls.placeArchetypeId.data()
                    ))
                    : "Place failed: no terrain hit under cursor.";
            }
            worldSaveControls.status = outcome.status;
            interactionStats = makeInteractionDebugStats(outcome);
        }

        if (BuildDebugToolsEnabled) {
            enqueueDebugPrimitives(
                renderView,
                debugDrawSettings,
                debugSelection,
                world,
                chunkStreamer,
                terrain,
                navigation,
                navigationConnectivity,
                worldNavigationGraph,
                lastWorldRoute,
                navigationChunks,
                playerNavAgent,
                playerObject,
                actors,
                playerActor,
                actorSelection,
                lastFormationDestinations,
                lastFailedFormationDestinations
            );
        }
        Renderer::SceneDrawStats drawStats;
        frameTimings.drawSubmissionMs = measureMilliseconds([&]() {
            drawStats = Renderer::drawScene(renderView);
        });
        if (BuildDebugToolsEnabled) {
            Renderer::drawDebugPrimitives(renderView);
        }
        if (debugUiEnabled) {
            Renderer::DebugUi::TerrainLodDebugStats terrainLods;
            terrainLods.counts = terrain.lodCounts();
            terrainLods.activeNavMaxSlopeDegrees = playerNavAgent.maxSlopeDegrees;
            const auto terrainTileForChunk = [&](Engine::ChunkCoord targetCoord) -> Engine::TerrainTileHandle {
                Engine::TerrainTileHandle result;
                chunkStreamer.forEachLoadedChunkContent(
                    [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>&, Renderer::RenderGroupHandle) {
                        if (result.id != UINT32_MAX) {
                            return;
                        }
                        const std::optional<Engine::ChunkCoord> coord = terrain.tileCoord(terrainTile);
                        if (coord && *coord == targetCoord) {
                            result = terrainTile;
                        }
                    });
                return result;
            };
            const auto terrainDiagnosticsForChunk = [&](Engine::ChunkCoord coord) {
                const Engine::TerrainTileHandle terrainTile = terrainTileForChunk(coord);
                return terrain.tileDiagnostics(terrainTile, &playerNavAgent);
            };
            const Engine::ChunkCoord cameraTerrainChunk =
                terrain.coordForWorldPosition(camera.state().pivot.x, camera.state().pivot.z);
            terrainLods.cameraChunkDiagnostics =
                makeTerrainDiagnosticsSummary(terrainDiagnosticsForChunk(cameraTerrainChunk));
            terrainLods.cameraBiomeGeneration =
                makeBiomeGenerationSummary(biomes.descriptor(terrain.sampleChunkBiome(cameraTerrainChunk).id));
            if (debugSelection.terrainHit) {
                terrainLods.hoveredChunkDiagnostics =
                    makeTerrainDiagnosticsSummary(terrainDiagnosticsForChunk(debugSelection.terrainHit->chunk));
            } else if (debugSelection.hoveredObject) {
                terrainLods.hoveredChunkDiagnostics =
                    makeTerrainDiagnosticsSummary(terrainDiagnosticsForChunk(debugSelection.hoveredObject->cell));
            }
            if (debugSelection.selectedObject && world.isValid(debugSelection.selectedObject->object)) {
                if (const std::optional<glm::vec3> selectedPosition = world.position(debugSelection.selectedObject->object)) {
                    terrainLods.selectedChunkDiagnostics =
                        makeTerrainDiagnosticsSummary(terrainDiagnosticsForChunk(
                            terrain.coordForWorldPosition(selectedPosition->x, selectedPosition->z)));
                }
            }
            const float spatialQueryRadius = 24.0f;
            const Engine::ChunkCoord currentSpatialCell = spatialRegistry.cellForPosition(camera.state().pivot);
            Renderer::DebugUi::SpatialRegistryDebugStats spatialStats;
            spatialStats.activeCells = static_cast<uint32_t>(spatialRegistry.activeCellCount());
            spatialStats.registeredObjects = static_cast<uint32_t>(spatialRegistry.registeredObjectCount());
            spatialStats.currentCellX = currentSpatialCell.x;
            spatialStats.currentCellZ = currentSpatialCell.z;
            spatialStats.objectsInCurrentCell = static_cast<uint32_t>(spatialRegistry.objectsInCell(currentSpatialCell).size());
            spatialStats.objectsNearCamera = static_cast<uint32_t>(spatialRegistry.objectsInRadius(camera.state().pivot, spatialQueryRadius).size());
            spatialStats.nearQueryRadius = spatialQueryRadius;
            Renderer::DebugUi::NavigationDebugStats navigationStats;
            navigationStats.activeNavigationProfileId = activeNavigationProfileId;
            navigationStats.loadedTiles = static_cast<uint32_t>(navigation.tileCount());
            navigationStats.polygonEdgeCount = static_cast<uint32_t>(navigation.debugGeometry().polygonEdges.size());
            navigationStats.blockerVertexCount = navigationBlockerStats.vertices;
            navigationStats.blockerTriangleCount = navigationBlockerStats.triangles;
            const Engine::NavigationConnectivityStats connectivityStats = navigationConnectivity.stats();
            navigationStats.connectivityChunkCount = connectivityStats.chunkCount;
            navigationStats.connectivityPortalCount = connectivityStats.totalPortals;
            navigationStats.connectivityConnectedPortalCount = connectivityStats.connectedPortals;
            navigationStats.connectivityPartialChunkCount = connectivityStats.partialChunks;
            navigationStats.connectivityBlockedChunkCount = connectivityStats.blockedChunks;
            navigationStats.cameraChunkConnectivitySummary = makeConnectivitySummary(
                navigationConnectivity.connectivity(terrain.coordForWorldPosition(camera.state().pivot.x, camera.state().pivot.z)));
            const Engine::ChunkCoord cameraChunk =
                terrain.coordForWorldPosition(camera.state().pivot.x, camera.state().pivot.z);
            navigationStats.cameraChunkTileSummary = makeTileDiagnosticsSummary(navigation.tileDiagnostics(cameraChunk));
            navigationStats.cameraChunkPortalSummary =
                makePortalDiagnosticsSummary(navigationConnectivity.portalDiagnostics(cameraChunk));
            if (debugSelection.terrainHit) {
                navigationStats.hoveredChunkTileSummary =
                    makeTileDiagnosticsSummary(navigation.tileDiagnostics(debugSelection.terrainHit->chunk));
                navigationStats.hoveredChunkPortalSummary =
                    makePortalDiagnosticsSummary(navigationConnectivity.portalDiagnostics(debugSelection.terrainHit->chunk));
            } else if (debugSelection.hoveredObject) {
                navigationStats.hoveredChunkTileSummary =
                    makeTileDiagnosticsSummary(navigation.tileDiagnostics(debugSelection.hoveredObject->cell));
                navigationStats.hoveredChunkPortalSummary =
                    makePortalDiagnosticsSummary(navigationConnectivity.portalDiagnostics(debugSelection.hoveredObject->cell));
            }
            if (debugSelection.selectedObject && world.isValid(debugSelection.selectedObject->object)) {
                const std::optional<glm::vec3> selectedPosition = world.position(debugSelection.selectedObject->object);
                if (selectedPosition) {
                    const Engine::ChunkCoord selectedChunk =
                        terrain.coordForWorldPosition(selectedPosition->x, selectedPosition->z);
                    navigationStats.selectedChunkConnectivitySummary =
                        makeConnectivitySummary(navigationConnectivity.connectivity(selectedChunk));
                    navigationStats.selectedChunkTileSummary =
                        makeTileDiagnosticsSummary(navigation.tileDiagnostics(selectedChunk));
                    navigationStats.selectedChunkPortalSummary =
                        makePortalDiagnosticsSummary(navigationConnectivity.portalDiagnostics(selectedChunk));
                }
            }
            const Engine::WorldNavigationGraphStats graphStats = worldNavigationGraph.stats();
            navigationStats.worldGraphNodeCount = graphStats.nodeCount;
            navigationStats.worldGraphEdgeCount = graphStats.edgeCount;
            navigationStats.worldGraphBlockedEdgeCount = graphStats.blockedEdgeCount;
            navigationStats.hasWorldGraph = graphStats.hasGraph;
            navigationStats.worldGraphCenterX = graphStats.centerChunk.x;
            navigationStats.worldGraphCenterZ = graphStats.centerChunk.z;
            navigationStats.lastWorldRouteStatus = worldRouteStatusName(lastWorldRoute.status);
            navigationStats.lastWorldRouteChunkCount = static_cast<uint32_t>(lastWorldRoute.chunkSequence.size());
            navigationStats.lastWorldRouteCost = lastWorldRoute.totalCost;
            navigationStats.lastWorldRouteMessage = lastWorldRoute.message;
            if (lastRebuiltNavigationChunk) {
                navigationStats.hasLastRebuiltChunk = true;
                navigationStats.lastRebuiltChunkX = lastRebuiltNavigationChunk->x;
                navigationStats.lastRebuiltChunkZ = lastRebuiltNavigationChunk->z;
            }
            navigationStats.selectedObjectNavBlocking =
                debugSelection.selectedObject &&
                world.isValid(debugSelection.selectedObject->object) &&
                world.collisionEnabled(debugSelection.selectedObject->object);
            navigationStats.selectedActorCount = static_cast<uint32_t>(actorSelection.selectedActors().size());
            for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                if (const std::optional<Engine::ActorState> actorState = actors.state(actor)) {
                    if (!navigationStats.selectedActorSummary.empty()) {
                        navigationStats.selectedActorSummary += "; ";
                    }
                    navigationStats.selectedActorSummary += "actor ";
                    navigationStats.selectedActorSummary += std::to_string(actor.id);
                    navigationStats.selectedActorSummary += " object ";
                    navigationStats.selectedActorSummary += std::to_string(actorState->object.id);
                    if (const std::optional<Engine::ObjectId> objectId = world.objectId(actorState->object)) {
                        navigationStats.selectedActorSummary += " ";
                        navigationStats.selectedActorSummary += objectId->toString();
                    }
                    navigationStats.selectedActorSummary += " [";
                    navigationStats.selectedActorSummary += actorPathStatusName(actorState->path.status);
                    navigationStats.selectedActorSummary += "/";
                    navigationStats.selectedActorSummary += actorRouteStatusName(actorState->route.status);
                    navigationStats.selectedActorSummary += "]";
                    if (navigationStats.selectedActorCommandSummary.empty()) {
                        navigationStats.selectedActorCommandSummary =
                            makeActorCommandDiagnosticsSummary(actorState->commandDiagnostics);
                    }
                }
            }
            if (navigationStats.selectedActorCommandSummary.empty()) {
                if (const std::optional<Engine::ActorState> playerState = actors.state(playerActor)) {
                    navigationStats.selectedActorCommandSummary =
                        makeActorCommandDiagnosticsSummary(playerState->commandDiagnostics);
                }
            }
            if (lastGroupCommandDestination) {
                navigationStats.hasLastGroupDestination = true;
                navigationStats.lastGroupDestination = *lastGroupCommandDestination;
                navigationStats.lastGroupCommandSuccessCount = lastGroupCommandSuccessCount;
                navigationStats.lastGroupCommandFailureCount = lastGroupCommandFailureCount;
                navigationStats.lastGroupCommandStatus = lastGroupCommandStatus;
                navigationStats.lastGroupCommandFailureSummary = lastGroupCommandFailureSummary;
            }
            navigationStats.lastBuildStatus = navStatusName(navigation.lastBuildStatus());
            navigationStats.lastBuildMessage = navigation.lastBuildMessage();
            navigationStats.lastQueryStatus = navStatusName(navigation.lastQueryStatus());
            navigationStats.lastQueryMessage = navigation.lastQueryMessage();
            if (nearestNavigationPoint) {
                navigationStats.nearestPointStatus = navStatusName(nearestNavigationPoint->status);
                navigationStats.hasNearestPoint = nearestNavigationPoint->status == Engine::NavQueryStatus::Success;
                navigationStats.nearestPoint = nearestNavigationPoint->point;
            }
            if (actorSelection.selectedActors().empty()) {
                if (const std::optional<Engine::ActorPathState> path = actors.pathState(playerActor)) {
                    navigationStats.currentPathStatus = actorPathStatusName(path->status);
                    navigationStats.currentPathPointCount = static_cast<uint32_t>(path->path.points.size());
                }
            } else {
                navigationStats.currentPathStatus = "selected actors";
                uint32_t selectedPathPoints = 0;
                for (Engine::ActorHandle actor : actorSelection.selectedActors()) {
                    if (const std::optional<Engine::ActorPathState> path = actors.pathState(actor)) {
                        selectedPathPoints += static_cast<uint32_t>(path->path.points.size());
                    }
                }
                navigationStats.currentPathPointCount = selectedPathPoints;
            }
            navigationStats.agent = toDebugAgentSettings(playerNavAgent);
            navigationStats.build = toDebugBuildSettings(navigation.settings());
            const Engine::NavigationCacheStats cacheStats = navigationCache.stats();
            navigationStats.cacheIdentity = navigationCache.manifest().identityHash;
            navigationStats.cacheTileHits = cacheStats.tileHits;
            navigationStats.cacheTileMisses = cacheStats.tileMisses;
            navigationStats.cacheTileStale = cacheStats.tileStale;
            navigationStats.cacheTileWrites = cacheStats.tileWrites;
            navigationStats.cacheConnectivityHits = cacheStats.connectivityHits;
            navigationStats.cacheConnectivityMisses = cacheStats.connectivityMisses;
            navigationStats.cacheConnectivityWrites = cacheStats.connectivityWrites;
            navigationStats.cacheGraphHits = cacheStats.graphHits;
            navigationStats.cacheGraphMisses = cacheStats.graphMisses;
            navigationStats.cacheGraphWrites = cacheStats.graphWrites;
            navigationStats.cacheLastPath = cacheStats.lastPath.string();
            navigationStats.cacheLastMessage = cacheStats.lastMessage;
            navigationStats.chunkStreamingMs = frameTimings.chunkStreamingMs;
            navigationStats.navTileSyncMs = frameTimings.navTileSyncMs;
            navigationStats.connectivityMs = frameTimings.connectivityMs;
            navigationStats.worldGraphMs = frameTimings.worldGraphMs;
            navigationStats.pickingMs = frameTimings.pickingMs;
            navigationStats.drawSubmissionMs = frameTimings.drawSubmissionMs;
            navigationStats.asyncWorkerCount = asyncWork.workerCount();
            navigationStats.asyncPendingChunkJobs = static_cast<uint32_t>(asyncStreaming.pendingLoads.size());
            navigationStats.asyncCompletedChunks = static_cast<uint32_t>(asyncStreaming.completedLoads.size());
            navigationStats.asyncPendingUnloads = static_cast<uint32_t>(asyncStreaming.pendingUnloads.size());
            navigationStats.asyncCancelledJobs = asyncStreaming.cancelledJobs;
            navigationStats.asyncStaleJobs = asyncStreaming.staleJobs;
            navigationStats.asyncCommittedLoadsThisFrame = asyncStreaming.committedLoadsThisFrame;
            navigationStats.asyncCommittedUnloadsThisFrame = asyncStreaming.committedUnloadsThisFrame;
            navigationStats.asyncAverageTerrainGenerationMs = asyncStreaming.averageTerrainGenerationMs;
            navigationStats.asyncMaxTerrainGenerationMs = asyncStreaming.maxTerrainGenerationMs;
            navigationStats.asyncAverageNavigationBuildMs = asyncStreaming.averageNavigationBuildMs;
            navigationStats.asyncMaxNavigationBuildMs = asyncStreaming.maxNavigationBuildMs;
            const Renderer::DebugUi::DebugPickingStats pickingStats =
                makeDebugPickingStats(debugSelection, world, objectArchetypes, objectOverrides, chunkStreamer);
            const Renderer::DebugUi::PlayerActorDebugStats playerStats =
                makePlayerActorDebugStats(actors, playerActor, world, terrain);
            const Renderer::DebugUi::CameraDebugStats cameraStats = makeCameraDebugStats(camera);
            const Renderer::DebugUi::BiomeDebugStats biomeStats =
                makeBiomeDebugStats(
                    biomes,
                    terrain,
                    biomeTerrainMaterials,
                    FallbackTerrainColor,
                    camera,
                    actors,
                    playerActor,
                    world,
                    debugSelection);
            const uint32_t previousLayerMask = debugSettings.layerMask;
            const bool previousDistanceCulling = debugSettings.enableDistanceCulling;
            const float previousPropMaxDistance = debugSettings.propMaxDrawDistance;
            const float previousTerrainMaxDistance = debugSettings.terrainMaxDrawDistance;
            Renderer::DebugUi::showRendererDebug(
                drawStats,
                debugSettings,
                atmosphere,
                debugDrawSettings,
                terrainLods,
                spatialStats,
                navigationStats,
                &navigationDebugControls,
                cameraStats,
                &cameraDebugControls,
                biomeStats,
                pickingStats,
                interactionStats,
                &worldSaveControls,
                playerStats
            );
            if (debugSettings.layerMask != previousLayerMask ||
                debugSettings.enableDistanceCulling != previousDistanceCulling ||
                debugSettings.propMaxDrawDistance != previousPropMaxDistance ||
                debugSettings.terrainMaxDrawDistance != previousTerrainMaxDistance) {
                debugVisibilityDirty = true;
            }
            asyncTerrainEnabled = navigationDebugControls.asyncTerrainEnabled;
            asyncNavigationEnabled = navigationDebugControls.asyncNavigationEnabled;
            chunkLoadCommitBudget = std::max(navigationDebugControls.chunkLoadCommitBudget, 1u);
            chunkUnloadCommitBudget = std::max(navigationDebugControls.chunkUnloadCommitBudget, 1u);
            Renderer::DebugUi::render(1, viewportExtent(width), viewportExtent(height));
        }
        bgfx::touch(0);
        bgfx::frame();
        if (navigationDebugControls.rebuildVisibleTilesRequested) {
            navigationDebugControls.rebuildVisibleTilesRequested = false;
            playerNavAgent = toEngineAgentSettings(navigationDebugControls.agent);
            applyDebugBuildSettings(navigationDebugControls.build, navBuildSettings);
            refreshNavigationCacheManifest();
            navigation = Engine::NavigationSystem(navBuildSettings);
            navigationChunks.clear();
            navigationConnectivity.clear();
            worldNavigationGraph.clear();
            hasWorldGraphCenter = false;
            navigationTilesDirty = true;
            navigationConnectivityDirty = true;
            worldGraphDirty = true;
            updateWorldNavigationGraphCenter(camera.state().pivot);
            processNavigationDirty();
            cancelCommandedActorPaths();
            worldSaveControls.status = "Rebuilt visible navigation tiles.";
        }
        if (navigationDebugControls.rebuildConnectivityRequested) {
            navigationDebugControls.rebuildConnectivityRequested = false;
            Engine::NavigationConnectivitySettings settings = navigationConnectivity.settings();
            settings.samplesPerEdge = std::max(navigationDebugControls.portalSamplesPerEdge, 1u);
            settings.edgeInset = navigationDebugControls.portalEdgeInset;
            settings.edgeBandWidth = navigationDebugControls.portalEdgeBandWidth;
            settings.portalMergeDistance = navigationDebugControls.portalMergeDistance;
            settings.neighborLinkDistance = navigationDebugControls.portalNeighborLinkDistance;
            navigationConnectivity.setSettings(settings);
            navigationConnectivityDirty = true;
            worldGraphDirty = true;
            processNavigationDirty();
            worldSaveControls.status = "Rebuilt navigation connectivity and graph.";
        }
        if (navigationDebugControls.clearCacheStatsRequested) {
            navigationDebugControls.clearCacheStatsRequested = false;
            navigationCache.clearStats();
        }
        if (navigationDebugControls.generateVisibleCacheRequested) {
            navigationDebugControls.generateVisibleCacheRequested = false;
            navigationCache.ensureManifest();
            for (Engine::ChunkCoord coord : navigationChunks) {
                if (const std::optional<Engine::NavigationTileCacheData> tile = navigation.tileCacheData(coord)) {
                    navigationCache.writeTile(*tile);
                }
            }
            for (const auto& [_, connectivity] : navigationConnectivity.all()) {
                navigationCache.writeConnectivity(connectivity);
            }
            if (worldNavigationGraph.stats().hasGraph) {
                navigationCache.writeGraph(worldNavigationGraph.cacheData());
            }
            worldSaveControls.status = "Generated visible navigation cache records.";
        }
        if (navigationDebugControls.refreshSelectedOrVisibleCacheRequested) {
            navigationDebugControls.refreshSelectedOrVisibleCacheRequested = false;
            std::vector<Engine::ChunkCoord> coords;
            if (debugSelection.selectedObject && world.isValid(debugSelection.selectedObject->object)) {
                if (const std::optional<glm::vec3> selectedPosition = world.position(debugSelection.selectedObject->object)) {
                    coords.push_back(terrain.coordForWorldPosition(selectedPosition->x, selectedPosition->z));
                }
            }
            if (coords.empty()) {
                coords.reserve(navigationChunks.size());
                for (Engine::ChunkCoord coord : navigationChunks) {
                    coords.push_back(coord);
                }
            }
            const bool cacheEnabled = navigationDebugControls.cacheEnabled;
            const bool cacheWriteThrough = navigationDebugControls.cacheWriteThrough;
            navigationDebugControls.cacheEnabled = false;
            navigationDebugControls.cacheWriteThrough = false;
            rebuildNavigationTiles(coords, true);
            navigationDebugControls.cacheEnabled = cacheEnabled;
            navigationDebugControls.cacheWriteThrough = cacheWriteThrough;
            processNavigationDirty();
            navigationCache.ensureManifest();
            for (Engine::ChunkCoord coord : coords) {
                if (const std::optional<Engine::NavigationTileCacheData> tile = navigation.tileCacheData(coord)) {
                    navigationCache.writeTile(*tile);
                }
            }
            for (const auto& [_, connectivity] : navigationConnectivity.all()) {
                navigationCache.writeConnectivity(connectivity);
            }
            if (worldNavigationGraph.stats().hasGraph) {
                navigationCache.writeGraph(worldNavigationGraph.cacheData());
            }
            worldSaveControls.status = "Refreshed navigation cache records.";
        }
        if (worldSaveControls.saveRequested) {
            worldSaveControls.saveRequested = false;
            std::string error;
            const Engine::WorldStateSnapshot snapshot =
                makeWorldStateSnapshot(actors, playerActor, world, camera, chunkStreamer, terrain, objectOverrides);
            if (Engine::saveWorldState(worldSaveControls.path.data(), snapshot, &error)) {
                worldSaveControls.status = "Saved world state.";
            } else {
                worldSaveControls.status = "Save failed: " + error;
            }
        }
        if (worldSaveControls.loadRequested) {
            worldSaveControls.loadRequested = false;
            const Engine::WorldStateIoResult result = Engine::loadWorldState(worldSaveControls.path.data());
            if (result.success) {
                objectOverrides.replaceFromSnapshot(result.snapshot, result.snapshot.settings.chunkSize);
                camera.setState(result.snapshot.camera);
                clearPendingChunkWork();
                chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
                spatialRegistry.clear();
                navigation.clear();
                navigationChunks.clear();
                navigationConnectivity.clear();
                worldNavigationGraph.clear();
                hasWorldGraphCenter = false;
                updateChunkStreamingAsync(camera.state().pivot);
                navigationTilesDirty = true;
                navigationConnectivityDirty = true;
                worldGraphDirty = true;
                debugVisibilityDirty = true;
                pickingDirty = true;
                updateWorldNavigationGraphCenter(camera.state().pivot);
                processNavigationDirty();
                debugVisibilityDirty = terrain.updateLods(camera.position()) || debugVisibilityDirty;
                cancelCommandedActorPaths();
                actors.setPosition(playerActor, result.snapshot.player.position, &terrain, &world);
                if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
                    spatialRegistry.insert(playerObject, *playerPosition);
                    if (camera.mode() == Engine::CameraMode::FollowTarget) {
                        recenterCameraOnPlayer(camera, *playerPosition);
                    }
                }
                for (Engine::WorldObjectHandle object : demoActorObjects) {
                    if (const std::optional<glm::vec3> position = world.position(object)) {
                        spatialRegistry.insert(object, *position);
                    }
                }
                if (debugVisibilityDirty) {
                    applyDebugVisibilitySettings();
                    debugVisibilityDirty = false;
                }
                world.syncRenderState();
                debugSelection = {};
                worldSaveControls.status = "Loaded world state.";
            } else {
                worldSaveControls.status = "Load failed: " + result.error;
            }
        }
        if (worldSaveControls.removeSelectedRequested) {
            worldSaveControls.removeSelectedRequested = false;
            Engine::InteractionEvent interaction;
            interaction.action = Engine::InteractionAction::RemoveObject;
            if (debugSelection.selectedObject) {
                interaction.target = Engine::InteractionTargetType::Object;
                interaction.object = debugSelection.selectedObject->object;
                interaction.objectId = debugSelection.selectedObject->objectId;
                interaction.objectHitPosition = debugSelection.selectedObject->position;
                interaction.chunk = debugSelection.selectedObject->cell;
                interaction.distance = debugSelection.selectedObject->distance;
            }
            Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
            if (outcome.type == Engine::InteractionOutcomeType::RemoveObject) {
                outcome.status = applyEditResult(objectEditor.removeObject(
                    outcome.event.object,
                    outcome.event.objectId,
                    outcome.event.objectHitPosition
                ));
            }
            worldSaveControls.status = outcome.status;
            interactionStats = makeInteractionDebugStats(outcome);
        }
        if (worldSaveControls.persistSelectedRequested) {
            worldSaveControls.persistSelectedRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.persistSelectedTransform(debugSelection));
        }
        if (worldSaveControls.resetSelectedOverrideRequested) {
            worldSaveControls.resetSelectedOverrideRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.resetSelectedOverride(debugSelection));
        }
        if (worldSaveControls.nudgeSelectedPositiveXRequested) {
            worldSaveControls.nudgeSelectedPositiveXRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {worldSaveControls.editMoveStep, 0.0f, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedNegativeXRequested) {
            worldSaveControls.nudgeSelectedNegativeXRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {-worldSaveControls.editMoveStep, 0.0f, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedPositiveYRequested) {
            worldSaveControls.nudgeSelectedPositiveYRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, worldSaveControls.editMoveStep, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedNegativeYRequested) {
            worldSaveControls.nudgeSelectedNegativeYRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, -worldSaveControls.editMoveStep, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedPositiveZRequested) {
            worldSaveControls.nudgeSelectedPositiveZRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, 0.0f, worldSaveControls.editMoveStep}));
        }
        if (worldSaveControls.nudgeSelectedNegativeZRequested) {
            worldSaveControls.nudgeSelectedNegativeZRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, 0.0f, -worldSaveControls.editMoveStep}));
        }
        if (worldSaveControls.rotateSelectedPositiveYawRequested) {
            worldSaveControls.rotateSelectedPositiveYawRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.rotateSelectedYaw(debugSelection, worldSaveControls.editRotateStepDegrees));
        }
        if (worldSaveControls.rotateSelectedNegativeYawRequested) {
            worldSaveControls.rotateSelectedNegativeYawRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.rotateSelectedYaw(debugSelection, -worldSaveControls.editRotateStepDegrees));
        }
        if (worldSaveControls.moveSelectedToTerrainRequested) {
            worldSaveControls.moveSelectedToTerrainRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.moveSelectedToTerrain(debugSelection));
        }
        if (worldSaveControls.placeArchetypeRequested) {
            worldSaveControls.placeArchetypeRequested = false;
            if (!debugSelection.terrainHit) {
                worldSaveControls.status = "Place failed: no terrain hit under cursor.";
            } else {
                Engine::InteractionEvent interaction;
                interaction.action = Engine::InteractionAction::PlaceMarker;
                interaction.target = Engine::InteractionTargetType::Terrain;
                interaction.terrainHitPosition = debugSelection.terrainHit->position;
                interaction.chunk = debugSelection.terrainHit->chunk;
                interaction.distance = debugSelection.terrainHit->distance;
                Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
                if (outcome.type == Engine::InteractionOutcomeType::PlaceMarker) {
                    outcome.status = applyEditResult(objectEditor.placeArchetype(
                        Engine::TerrainPickHit{
                            outcome.event.terrainHitPosition,
                            outcome.event.distance,
                            outcome.event.chunk,
                        },
                        worldSaveControls.placeArchetypeId.data()
                    ));
                }
                worldSaveControls.status = outcome.status;
                interactionStats = makeInteractionDebugStats(outcome);
            }
        }
        events.clear();
    }

    clearPendingChunkWork();
    asyncWork.shutdown();
    navigation.clear();
    navigationChunks.clear();
    navigationConnectivity.clear();
    worldNavigationGraph.clear();
    chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
    spatialRegistry.clear();
    world.syncRenderState();

    Renderer::destroyMaterial(yellowMaterial);
    Renderer::destroyMaterial(squadRedMaterial);
    Renderer::destroyMaterial(squadGreenMaterial);
    Renderer::destroyMaterial(squadBlueMaterial);
    Renderer::destroyMaterial(cyanMaterial);
    for (const RuntimeBiomeTerrainMaterial& material : biomeTerrainMaterials) {
        Renderer::destroyMaterial(material.material);
    }
    for (const RuntimeObjectArchetypeVisual& visual : archetypeVisuals) {
        Renderer::destroyMaterial(visual.material);
    }

    assetCache.release(yellowTexture);
    assetCache.release(squadRedTexture);
    assetCache.release(squadGreenTexture);
    assetCache.release(squadBlueTexture);
    assetCache.release(cyanTexture);
    for (const RuntimeBiomeTerrainMaterial& material : biomeTerrainMaterials) {
        assetCache.release(material.texture);
    }
    assetCache.release(playerMesh);
    for (const RuntimeObjectArchetypeVisual& visual : archetypeVisuals) {
        assetCache.release(visual.texture);
        assetCache.release(visual.mesh);
    }
    assetCache.shutdown();

    if (debugUiEnabled) {
        Renderer::DebugUi::shutdown();
    }
    Renderer::shutdownSceneRenderer();
    bgfx::shutdown();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
