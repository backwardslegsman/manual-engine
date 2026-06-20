#include "Renderer/DebugUi.hpp"

#ifndef MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#define MANUAL_ENGINE_ENABLE_DEBUG_TOOLS 1
#endif

#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "Renderer/core.hpp"

namespace {
    bgfx::VertexLayout g_imguiVertexLayout;
    bgfx::ProgramHandle g_imguiProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_imguiTextureSampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle g_fontTexture = BGFX_INVALID_HANDLE;
    bool g_initialized = false;
    bool g_selectPerformanceTab = true;

    void destroyBgfxResources()
    {
        if (bgfx::isValid(g_fontTexture)) {
            bgfx::destroy(g_fontTexture);
            g_fontTexture = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_imguiTextureSampler)) {
            bgfx::destroy(g_imguiTextureSampler);
            g_imguiTextureSampler = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_imguiProgram)) {
            bgfx::destroy(g_imguiProgram);
            g_imguiProgram = BGFX_INVALID_HANDLE;
        }
    }

    bool createFontTexture()
    {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0) {
            return false;
        }

        const bgfx::Memory* memory = bgfx::copy(pixels, static_cast<uint32_t>(width * height * 4));
        g_fontTexture = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory);

        return bgfx::isValid(g_fontTexture);
    }

    const char* scenePhaseName(size_t index)
    {
        static constexpr const char* names[] = {
            "BeginFrame",
            "FixedPrePhysics",
            "FixedPhysics",
            "FixedPostPhysics",
            "VariableAnimation",
            "VariableUpdate",
            "PreRender",
            "EndFrame",
        };
        return index < std::size(names) ? names[index] : "Unknown";
    }

    const char* streamingResidencyName(size_t index)
    {
        static constexpr const char* names[] = {
            "ColdOnDisk",
            "ReadQueued",
            "CachedCpu",
            "PromoteQueued",
            "LiveActive",
            "DemoteQueued",
            "WriteQueued",
            "Failed",
        };
        return index < std::size(names) ? names[index] : "Unknown";
    }

    const char* streamingLaneName(size_t index)
    {
        static constexpr const char* names[] = {
            "DiskReadDecode",
            "DerivedGeneration",
            "AssetPreload",
            "MainThreadPromote",
            "MainThreadDemote",
            "DiskCacheWrite",
            "ManifestValidation",
        };
        return index < std::size(names) ? names[index] : "Unknown";
    }

    const char* streamingPayloadName(size_t index)
    {
        static constexpr const char* names[] = {
            "TerrainChunk",
            "TerrainRenderLod",
            "NavigationTile",
            "PhysicsCollider",
            "SceneChunk",
            "AssetDependency",
            "Unknown",
        };
        return index < std::size(names) ? names[index] : "Unknown";
    }

    void textVec3(const char* label, const glm::vec3& value)
    {
        ImGui::Text("%s: %.2f, %.2f, %.2f", label, value.x, value.y, value.z);
    }

    void layerCheckbox(const char* label, Renderer::RenderLayer layer, Renderer::DebugUi::RendererDebugSettings& settings)
    {
        const uint32_t bit = static_cast<uint32_t>(layer);
        bool enabled = (settings.layerMask & bit) != 0;
        if (ImGui::Checkbox(label, &enabled)) {
            if (enabled) {
                settings.layerMask |= bit;
            } else {
                settings.layerMask &= ~bit;
            }
        }
    }

    void showPerformance(const Renderer::DebugUi::PerformanceDebugStats& performance)
    {
        ImGui::Text("Startup: %s %.3f ms",
            performance.startupComplete ? "complete" : "pending",
            performance.startupWorkMs);
        if (!performance.startupStatus.empty()) {
            ImGui::TextWrapped("%s", performance.startupStatus.c_str());
        }
        ImGui::Separator();
        ImGui::Text("Frame CPU");
        ImGui::Columns(2, "cpu_probes", false);
        auto row = [](const char* label, float value) {
            ImGui::Text("%s", label);
            ImGui::NextColumn();
            ImGui::Text("%.3f ms", value);
            ImGui::NextColumn();
        };
        row("Previous frame", performance.previousFrameCpuMs);
        row("Event polling", performance.eventPollingMs);
        row("Input mapping", performance.inputMappingMs);
        row("Camera update", performance.cameraUpdateMs);
        row("Scene fixed ticks", performance.sceneFixedTickMs);
        row("Scene frame tick", performance.sceneFrameTickMs);
        row("Debug primitive enqueue", performance.debugPrimitiveEnqueueMs);
        row("Draw submission", performance.drawSubmissionMs);
        row("Debug primitive draw", performance.debugPrimitiveDrawMs);
        row("Debug UI build", performance.debugUiBuildMs);
        row("Debug UI render", performance.debugUiRenderMs);
        row("bgfx frame", performance.bgfxFrameMs);
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::Text("Scene scheduler phases");
        ImGui::Columns(3, "scene_phase_probes", false);
        ImGui::Text("Phase");
        ImGui::NextColumn();
        ImGui::Text("Callbacks");
        ImGui::NextColumn();
        ImGui::Text("Time");
        ImGui::NextColumn();
        for (size_t index = 0; index < performance.scenePhaseMs.size(); ++index) {
            ImGui::Text("%s", scenePhaseName(index));
            ImGui::NextColumn();
            ImGui::Text("%u", performance.scenePhaseCallbacks[index]);
            ImGui::NextColumn();
            ImGui::Text("%.3f ms", performance.scenePhaseMs[index]);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    void showRender(
        const Renderer::SceneDrawStats& stats,
        Renderer::DebugUi::RendererDebugSettings& settings,
        Renderer::AtmosphereSettings& atmosphere,
        Renderer::DebugUi::CameraDebugControls* cameraControls,
        const Renderer::DebugUi::CameraDebugStats& camera)
    {
        ImGui::Text("Render layers");
        layerCheckbox("Terrain", Renderer::RenderLayer::Terrain, settings);
        layerCheckbox("Props", Renderer::RenderLayer::Props, settings);
        layerCheckbox("Debug", Renderer::RenderLayer::Debug, settings);
        layerCheckbox("Transparent", Renderer::RenderLayer::Transparent, settings);
        ImGui::Checkbox("Distance culling", &settings.enableDistanceCulling);
        ImGui::SliderFloat("Prop max distance", &settings.propMaxDrawDistance, 0.0f, 500.0f);
        ImGui::SliderFloat("Terrain max distance", &settings.terrainMaxDrawDistance, 0.0f, 1000.0f);
        ImGui::Separator();

        ImGui::Text("Camera");
        if (cameraControls) {
            int cameraMode = camera.followMode ? 1 : 0;
            if (ImGui::Combo("Mode", &cameraMode, "Free\0Follow Character\0")) {
                cameraControls->setFreeModeRequested = cameraMode == 0;
                cameraControls->setFollowModeRequested = cameraMode == 1;
            }
            if (ImGui::Button("Recenter Camera")) {
                cameraControls->recenterRequested = true;
            }
            ImGui::SliderFloat("Follow smoothing", &cameraControls->followSmoothing, 0.0f, 30.0f);
            ImGui::SliderFloat("Max follow lag", &cameraControls->maxFollowLag, 0.0f, 30.0f);
        }
        ImGui::Text("Mode: %s", camera.followMode ? "Follow Character" : "Free");
        textVec3("Pivot", camera.pivot);
        if (camera.hasTarget) {
            textVec3("Target", camera.targetPosition);
        }
        textVec3("Follow offset", camera.followOffset);
        ImGui::Text("Smoothing / max lag: %.2f / %.2f", camera.followSmoothing, camera.maxFollowLag);
        ImGui::Separator();

        ImGui::Text("Atmosphere");
        ImGui::ColorEdit3("Sky color", &atmosphere.skyColor[0]);
        ImGui::ColorEdit3("Fog color", &atmosphere.fogColor[0]);
        ImGui::Checkbox("Fog enabled", &atmosphere.fogEnabled);
        ImGui::SliderFloat("Fog density", &atmosphere.fogDensity, 0.0f, 0.12f);
        ImGui::SliderFloat3("Sun direction", &atmosphere.sunDirection[0], -1.0f, 1.0f);
        ImGui::ColorEdit3("Sun color", &atmosphere.sunColor[0]);
        ImGui::SliderFloat("Sun intensity", &atmosphere.sunIntensity, 0.0f, 4.0f);
        ImGui::SliderFloat("Exposure", &atmosphere.exposure, 0.0f, 4.0f);
        ImGui::SliderFloat("Ambient intensity", &atmosphere.ambientIntensity, 0.0f, 1.0f);
        ImGui::Checkbox("Environment enabled", &atmosphere.environmentEnabled);
        ImGui::ColorEdit3("Environment diffuse", &atmosphere.environmentDiffuseColor[0]);
        ImGui::SliderFloat("Environment intensity", &atmosphere.environmentDiffuseIntensity, 0.0f, 4.0f);
        ImGui::Separator();

        ImGui::Text("Lights live/active/submitted: %u / %u / %u",
            stats.liveLightCount,
            stats.activeLightCount,
            stats.submittedForwardLightCount);
        ImGui::Text("Mesh instances live/visible/submitted: %u / %u / %u",
            stats.liveMeshInstances,
            stats.visibleMeshInstances,
            stats.submittedMeshInstances);
        ImGui::Text("Mesh draw items visible/submitted/batches: %u / %u / %u",
            stats.visibleMeshDrawItems,
            stats.submittedMeshDrawItems,
            stats.meshBatchCount);
        ImGui::Text("Skinned instances live/visible/submitted: %u / %u / %u",
            stats.liveSkinnedMeshInstances,
            stats.visibleSkinnedMeshInstances,
            stats.submittedSkinnedMeshInstances);
        ImGui::Text("Terrain live/visible/submitted: %u / %u / %u",
            stats.liveTerrainTiles,
            stats.visibleTerrainTiles,
            stats.submittedTerrainTiles);
        ImGui::Text("Terrain layered assigned/submitted/fallback: %u / %u / %u",
            stats.assignedLayeredTerrainTiles,
            stats.submittedLayeredTerrainTiles,
            stats.fallbackTerrainTiles);
        ImGui::Text("Culled mesh layer/frustum/distance: %u / %u / %u",
            stats.layerOrFlagCulledMeshInstances,
            stats.frustumCulledMeshInstances,
            stats.distanceCulledMeshInstances);
        ImGui::Text("Culled terrain layer/frustum/distance: %u / %u / %u",
            stats.layerOrFlagCulledTerrainTiles,
            stats.frustumCulledTerrainTiles,
            stats.distanceCulledTerrainTiles);
    }

    void showStreaming(const Renderer::DebugUi::OpenWorldStreamingDebugStats& streaming)
    {
        ImGui::Text("Open-world streaming: planning, cache reads, and live promotion");
        ImGui::Text("Live promotion is callback-owned and budgeted on the main thread.");
        ImGui::Separator();

        ImGui::Text("Manifest records total/considered/skipped: %u / %u / %u",
            streaming.manifestRecordCount,
            streaming.manifestRecordsConsidered,
            streaming.manifestRecordsSkipped);
        ImGui::Text("Transition candidates/limited: %u / %u",
            streaming.transitionCandidateCount,
            streaming.transitionLimitedCount);
        ImGui::Text("Hysteresis retained / invalid bounds: %u / %u",
            streaming.hysteresisRetainedCount,
            streaming.invalidBoundsCount);
        ImGui::Text("Pending reads / cached CPU payloads: %u / %u",
            streaming.pendingReadCount,
            streaming.cachedCpuPayloadCount);
        ImGui::Text("Stale completions / unsupported reads: %u / %u",
            streaming.staleReadCompletionCount,
            streaming.unsupportedReadCount);
        ImGui::Text("Pending promote / demote: %u / %u",
            streaming.pendingPromoteCount,
            streaming.pendingDemoteCount);
        ImGui::Text("Live payloads / stale live completions: %u / %u",
            streaming.livePayloadCount,
            streaming.stalePromotionCompletionCount);
        ImGui::Text("Promotion failures / demotion failures: %u / %u",
            streaming.failedPromotionCount,
            streaming.failedDemotionCount);
        ImGui::Text("Bake chunks / payload writes: %u / %u",
            streaming.bakeChunkCount,
            streaming.bakePayloadWriteCount);
        ImGui::Text("Generation queued / completed / failed: %u / %u / %u",
            streaming.generationQueuedCount,
            streaming.generationCompletedCount,
            streaming.generationFailedCount);
        ImGui::Text("Cache invalidations: %u", streaming.cacheInvalidationCount);
        if (streaming.hasLastFocus) {
            textVec3("Last focus", streaming.lastFocus);
        }
        ImGui::Separator();

        ImGui::Text("Residency");
        ImGui::Columns(3, "streaming_residency", false);
        ImGui::Text("State");
        ImGui::NextColumn();
        ImGui::Text("Desired");
        ImGui::NextColumn();
        ImGui::Text("Actual");
        ImGui::NextColumn();
        for (size_t index = 0; index < streaming.desiredChunksByState.size(); ++index) {
            ImGui::Text("%s", streamingResidencyName(index));
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.desiredChunksByState[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.actualChunksByState[index]);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();

        ImGui::Text("Desired by payload");
        ImGui::Columns(2, "streaming_payload_residency", false);
        ImGui::Text("Payload");
        ImGui::NextColumn();
        ImGui::Text("Desired");
        ImGui::NextColumn();
        for (size_t index = 0; index < streaming.desiredChunksByPayload.size(); ++index) {
            ImGui::Text("%s", streamingPayloadName(index));
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.desiredChunksByPayload[index]);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();

        ImGui::Text("Queues and timings");
        ImGui::Columns(6, "streaming_lanes", false);
        ImGui::Text("Lane");
        ImGui::NextColumn();
        ImGui::Text("Queued");
        ImGui::NextColumn();
        ImGui::Text("Active");
        ImGui::NextColumn();
        ImGui::Text("Done");
        ImGui::NextColumn();
        ImGui::Text("Failed");
        ImGui::NextColumn();
        ImGui::Text("CPU");
        ImGui::NextColumn();
        for (size_t index = 0; index < streaming.queuedByLane.size(); ++index) {
            ImGui::Text("%s", streamingLaneName(index));
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.queuedByLane[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.activeJobsByLane[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.completedByLane[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.failedByLane[index]);
            ImGui::NextColumn();
            ImGui::Text("%.3f ms", streaming.laneCpuMs[index]);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();

        ImGui::Text("Payload cache");
        ImGui::Columns(6, "streaming_payloads", false);
        ImGui::Text("Payload");
        ImGui::NextColumn();
        ImGui::Text("Hit");
        ImGui::NextColumn();
        ImGui::Text("Miss");
        ImGui::NextColumn();
        ImGui::Text("Stale");
        ImGui::NextColumn();
        ImGui::Text("Corrupt");
        ImGui::NextColumn();
        ImGui::Text("Write");
        ImGui::NextColumn();
        for (size_t index = 0; index < streaming.cacheHitsByPayload.size(); ++index) {
            ImGui::Text("%s", streamingPayloadName(index));
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.cacheHitsByPayload[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.cacheMissesByPayload[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.cacheStaleByPayload[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.cacheCorruptByPayload[index]);
            ImGui::NextColumn();
            ImGui::Text("%u", streaming.cacheWritesByPayload[index]);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();

        ImGui::Text("Main thread promote run/deferred: %u / %u",
            streaming.mainThreadPromoteItemsRun,
            streaming.mainThreadPromoteItemsDeferred);
        ImGui::Text("Main thread demote run/deferred: %u / %u",
            streaming.mainThreadDemoteItemsRun,
            streaming.mainThreadDemoteItemsDeferred);
        ImGui::Text("Bytes read/written/resident: %llu / %llu / %llu",
            static_cast<unsigned long long>(streaming.bytesRead),
            static_cast<unsigned long long>(streaming.bytesWritten),
            static_cast<unsigned long long>(streaming.estimatedResidentBytes));
        ImGui::Text("Live terrain/nav/physics bodies/colliders: %u / %u / %u / %u",
            streaming.liveTerrainRenderHandles,
            streaming.liveNavigationTiles,
            streaming.livePhysicsBodies,
            streaming.livePhysicsColliders);
        ImGui::Text("Live scene actors/components/assets: %u / %u / %u",
            streaming.liveSceneActors,
            streaming.liveSceneComponents,
            streaming.liveAssetDependencies);
        ImGui::Text("Hysteresis churn / eviction blocked: %u / %u",
            streaming.hysteresisChurnCount,
            streaming.evictionBlockedCount);
        if (streaming.hasLastFailure) {
            ImGui::Separator();
            ImGui::Text("Last failure lane/payload: %s / %s",
                streaming.lastFailureLane.c_str(),
                streaming.lastFailurePayload.c_str());
            ImGui::Text("Chunk/status: %s / %s",
                streaming.lastFailureChunk.c_str(),
                streaming.lastFailureStatus.c_str());
            if (!streaming.lastFailureMessage.empty()) {
                ImGui::TextWrapped("%s", streaming.lastFailureMessage.c_str());
            }
        }
    }
}

namespace Renderer::DebugUi {
    bool init(SDL_Window* window)
    {
        if (g_initialized) {
            return true;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        if (!ImGui_ImplSDL3_InitForOther(window)) {
            ImGui::DestroyContext();
            return false;
        }

        g_imguiVertexLayout
            .begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();

        const bgfx::ShaderHandle vertexShader = loadShader("vs_imgui.bin");
        const bgfx::ShaderHandle fragmentShader = loadShader("fs_imgui.bin");
        if (!bgfx::isValid(vertexShader) || !bgfx::isValid(fragmentShader)) {
            if (bgfx::isValid(vertexShader)) {
                bgfx::destroy(vertexShader);
            }
            if (bgfx::isValid(fragmentShader)) {
                bgfx::destroy(fragmentShader);
            }
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        g_imguiProgram = bgfx::createProgram(vertexShader, fragmentShader, true);
        g_imguiTextureSampler = bgfx::createUniform("s_imgui", bgfx::UniformType::Sampler);

        if (!bgfx::isValid(g_imguiProgram) ||
            !bgfx::isValid(g_imguiTextureSampler) ||
            !createFontTexture()) {
            destroyBgfxResources();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        g_initialized = true;
        return true;
    }

    void shutdown()
    {
        if (!g_initialized) {
            return;
        }

        destroyBgfxResources();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        g_initialized = false;
    }

    void processEvent(const SDL_Event& event)
    {
        if (g_initialized) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
    }

    void beginFrame(uint16_t width, uint16_t height)
    {
        if (!g_initialized) {
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    bool wantsMouseCapture()
    {
        return g_initialized && ImGui::GetIO().WantCaptureMouse;
    }

    bool wantsKeyboardCapture()
    {
        return g_initialized && ImGui::GetIO().WantCaptureKeyboard;
    }

    void showModernDebug(
        const SceneDrawStats& stats,
        RendererDebugSettings& settings,
        AtmosphereSettings& atmosphere,
        DebugDrawSettings& debugDraw,
        const ModernDebugUiState& state,
        const CameraDebugStats& camera,
        CameraDebugControls* cameraControls,
        ModernNavigationDebugControls* navigationControls)
    {
        if (!g_initialized) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(560.0f, 640.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("ManualEngine Debug");
        ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable;
        if (ImGui::BeginTabBar("debug_tabs", flags)) {
            ImGuiTabItemFlags performanceFlags = ImGuiTabItemFlags_None;
            if (g_selectPerformanceTab) {
                performanceFlags = ImGuiTabItemFlags_SetSelected;
                g_selectPerformanceTab = false;
            }
            if (ImGui::BeginTabItem("Performance", nullptr, performanceFlags)) {
                showPerformance(state.performance);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Render")) {
                showRender(stats, settings, atmosphere, cameraControls, camera);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scene")) {
                ImGui::Text("Mode: %s", state.scene.mode.empty() ? "<unknown>" : state.scene.mode.c_str());
                if (!state.scene.status.empty()) {
                    ImGui::TextWrapped("%s", state.scene.status.c_str());
                }
                ImGui::Text("Actors/components: %u / %u", state.scene.actorCount, state.scene.componentCount);
                ImGui::Text("Frame/fixed index: %llu / %u",
                    static_cast<unsigned long long>(state.scene.frameIndex),
                    state.scene.fixedStepIndex);
                ImGui::Text("Systems registered/enabled: %u / %u",
                    state.scene.registeredSystemCount,
                    state.scene.enabledSystemCount);
                ImGui::Text("Skipped phase/callback: %u / %u",
                    state.scene.skippedPhaseCount,
                    state.scene.skippedCallbackCount);
                ImGui::Separator();
                ImGui::Text("Render bridge components mesh/skinned/light/camera: %u / %u / %u / %u",
                    state.scene.renderMeshComponents,
                    state.scene.renderSkinnedComponents,
                    state.scene.renderLightComponents,
                    state.scene.renderCameraComponents);
                ImGui::Text("Live render resources mesh/skinned/lights: %u / %u / %u",
                    state.scene.liveMeshInstances,
                    state.scene.liveSkinnedInstances,
                    state.scene.liveRendererLights);
                ImGui::Text("Static assets / animated assets / warnings: %u / %u / %u",
                    state.scene.authoredStaticAssets,
                    state.scene.animatedAssets,
                    state.scene.warnings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Terrain")) {
                ImGui::Text("Source: %s", state.terrain.loadedHeightmap ? "heightmap" : "procedural fallback");
                ImGui::Text("Sources/chunks/rendered: %u / %u / %u",
                    state.terrain.sourceCount,
                    state.terrain.loadedChunkCount,
                    state.terrain.renderedChunkCount);
                ImGui::Text("Nav tiles / physics colliders: %u / %u",
                    state.terrain.navTileCount,
                    state.terrain.physicsColliderCount);
                textVec3("Bounds min", state.terrain.boundsMin);
                textVec3("Bounds max", state.terrain.boundsMax);
                ImGui::Text("Height min/max/avg: %.2f / %.2f / %.2f",
                    state.terrain.minHeight,
                    state.terrain.maxHeight,
                    state.terrain.averageHeight);
                ImGui::Text("Estimated memory: %llu bytes",
                    static_cast<unsigned long long>(state.terrain.memoryEstimateBytes));
                if (!state.terrain.diagnostics.empty()) {
                    ImGui::TextWrapped("%s", state.terrain.diagnostics.c_str());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Navigation")) {
                ImGui::Text("Loaded tiles: %u", state.navigation.loadedTiles);
                ImGui::Text("Polygon edges: %u", state.navigation.polygonEdgeCount);
                ImGui::Text("Scene sources/triangles/appended: %u / %u / %u",
                    state.navigation.sceneSourceCount,
                    state.navigation.sceneTriangleCount,
                    state.navigation.sceneAppendedTriangleCount);
                ImGui::Text("Culled bounds/slope: %u / %u",
                    state.navigation.sceneCulledByBounds,
                    state.navigation.sceneCulledBySlope);
                ImGui::Text("Cache hit/miss/stale/write: %u / %u / %u / %u",
                    state.navigation.cacheHits,
                    state.navigation.cacheMisses,
                    state.navigation.cacheStale,
                    state.navigation.cacheWrites);
                if (!state.navigation.cacheIdentity.empty()) {
                    ImGui::TextWrapped("Cache identity: %s", state.navigation.cacheIdentity.c_str());
                }
                ImGui::Text("Last build: %s", state.navigation.lastBuildStatus.empty() ? "<none>" : state.navigation.lastBuildStatus.c_str());
                if (!state.navigation.lastBuildMessage.empty()) {
                    ImGui::TextWrapped("%s", state.navigation.lastBuildMessage.c_str());
                }
                ImGui::Text("Last query: %s", state.navigation.lastQueryStatus.empty() ? "<none>" : state.navigation.lastQueryStatus.c_str());
                if (!state.navigation.lastQueryMessage.empty()) {
                    ImGui::TextWrapped("%s", state.navigation.lastQueryMessage.c_str());
                }
                if (navigationControls) {
                    if (ImGui::Button("Rebuild Modern Navigation")) {
                        navigationControls->rebuildNavigationRequested = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Cache Stats")) {
                        navigationControls->clearCacheStatsRequested = true;
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Physics")) {
                ImGui::Text("Bodies/colliders/enabled: %u / %u / %u",
                    state.physics.bodyCount,
                    state.physics.colliderCount,
                    state.physics.enabledBodyCount);
                ImGui::Text("Invalid owner cleanup: %u", state.physics.invalidOwnerCleanupCount);
                ImGui::Text("Queries ray/sweep/overlap/closest: %u / %u / %u / %u",
                    state.physics.raycastCount,
                    state.physics.sweepCount,
                    state.physics.overlapCount,
                    state.physics.closestPointCount);
                ImGui::Text("Last step/query: %.3f ms / %.3f ms",
                    static_cast<float>(state.physics.lastStepMicroseconds) / 1000.0f,
                    static_cast<float>(state.physics.lastQueryMicroseconds) / 1000.0f);
                ImGui::Text("Last status: %s", state.physics.lastStatus.empty() ? "<none>" : state.physics.lastStatus.c_str());
                if (!state.physics.lastMessage.empty()) {
                    ImGui::TextWrapped("%s", state.physics.lastMessage.c_str());
                }
                ImGui::Text("Warnings: %u", state.physics.warnings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Character")) {
                ImGui::Text("Character: %s", state.character.hasCharacter ? "valid" : "missing");
                ImGui::Text("Enabled: %s", state.character.enabled ? "yes" : "no");
                ImGui::Text("Mode/grounded: %s / %s",
                    state.character.mode.empty() ? "<unknown>" : state.character.mode.c_str(),
                    state.character.grounded ? "yes" : "no");
                textVec3("Velocity", state.character.velocity);
                textVec3("Floor normal", state.character.floorNormal);
                ImGui::Text("Floor distance: %.3f", state.character.floorDistance);
                ImGui::Text("Path points/waypoint: %u / %u",
                    state.character.activePathPointCount,
                    state.character.activeWaypointIndex);
                ImGui::Text("Characters/fail sweeps/path queries: %u / %u / %u",
                    state.character.characterCount,
                    state.character.failedSweepCount,
                    state.character.pathQueryCount);
                ImGui::Text("Invalid owner cleanup: %u", state.character.invalidOwnerCleanupCount);
                ImGui::Text("Last update: %.3f ms",
                    static_cast<float>(state.character.lastUpdateMicroseconds) / 1000.0f);
                ImGui::Text("Last status: %s", state.character.lastStatus.empty() ? "<none>" : state.character.lastStatus.c_str());
                if (!state.character.lastMessage.empty()) {
                    ImGui::TextWrapped("%s", state.character.lastMessage.c_str());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Streaming")) {
                showStreaming(state.streaming);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug Draw")) {
                ImGui::Checkbox("Debug draw enabled", &debugDraw.enabled);
                ImGui::Checkbox("Terrain chunk bounds", &debugDraw.terrainTileBounds);
                ImGui::Checkbox("Navigation tile bounds", &debugDraw.navigationTileBounds);
                ImGui::Checkbox("Navigation mesh edges", &debugDraw.navigationMeshEdges);
                ImGui::Checkbox("Physics probes", &debugDraw.collisionBounds);
                ImGui::Checkbox("Character path/probes", &debugDraw.navigationCurrentPath);
                ImGui::Checkbox("Camera frustum", &debugDraw.cameraFrustum);
                int maxDebugLines = static_cast<int>(debugDraw.maxDebugLines);
                if (ImGui::SliderInt("Max debug lines", &maxDebugLines, 1000, 100000)) {
                    debugDraw.maxDebugLines = static_cast<uint32_t>(std::max(maxDebugLines, 0));
                }
                int maxNavLines = static_cast<int>(debugDraw.maxNavMeshEdgeLines);
                if (ImGui::SliderInt("Max navmesh lines", &maxNavLines, 0, 50000)) {
                    debugDraw.maxNavMeshEdgeLines = static_cast<uint32_t>(std::max(maxNavLines, 0));
                }
                ImGui::Separator();
                ImGui::Text("Lines generated/submitted/clipped/capped: %u / %u / %u / %u",
                    state.debugVisualization.generatedLines,
                    state.debugVisualization.submittedLines,
                    state.debugVisualization.clippedLines,
                    state.debugVisualization.cappedLines);
                ImGui::Text("Renderer line buffer vertices: %u",
                    state.debugVisualization.lastFramePrimitiveBufferSize);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void render(bgfx::ViewId viewId, uint16_t width, uint16_t height)
    {
        if (!g_initialized || width == 0 || height == 0) {
            return;
        }

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->TotalVtxCount == 0) {
            return;
        }

        const glm::mat4 projection = bgfx::getCaps()->homogeneousDepth
            ? glm::orthoRH_NO(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f, 1000.0f)
            : glm::orthoRH_ZO(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f, 1000.0f);

        bgfx::setViewName(viewId, "Dear ImGui");
        bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
        bgfx::setViewRect(viewId, 0, 0, width, height);
        bgfx::setViewTransform(viewId, nullptr, &projection[0][0]);
        bgfx::touch(viewId);

        const ImVec2 clipOffset = drawData->DisplayPos;
        const ImVec2 framebufferScale = drawData->FramebufferScale;
        for (int commandListIndex = 0; commandListIndex < drawData->CmdListsCount; ++commandListIndex) {
            const ImDrawList* commandList = drawData->CmdLists[commandListIndex];
            bgfx::TransientVertexBuffer vertexBuffer;
            bgfx::TransientIndexBuffer indexBuffer;

            const uint32_t vertexCount = static_cast<uint32_t>(commandList->VtxBuffer.Size);
            const uint32_t indexCount = static_cast<uint32_t>(commandList->IdxBuffer.Size);
            if (!bgfx::getAvailTransientVertexBuffer(vertexCount, g_imguiVertexLayout) ||
                !bgfx::getAvailTransientIndexBuffer(indexCount)) {
                break;
            }

            bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, g_imguiVertexLayout);
            bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount, sizeof(ImDrawIdx) == 4);
            std::memcpy(vertexBuffer.data, commandList->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
            std::memcpy(indexBuffer.data, commandList->IdxBuffer.Data, indexCount * sizeof(ImDrawIdx));

            for (const ImDrawCmd& command : commandList->CmdBuffer) {
                if (command.UserCallback) {
                    command.UserCallback(commandList, &command);
                    continue;
                }

                const float clipMinX = (command.ClipRect.x - clipOffset.x) * framebufferScale.x;
                const float clipMinY = (command.ClipRect.y - clipOffset.y) * framebufferScale.y;
                const float clipMaxX = (command.ClipRect.z - clipOffset.x) * framebufferScale.x;
                const float clipMaxY = (command.ClipRect.w - clipOffset.y) * framebufferScale.y;
                if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) {
                    continue;
                }

                const uint16_t clipX = static_cast<uint16_t>(std::clamp(clipMinX, 0.0f, static_cast<float>(width)));
                const uint16_t clipY = static_cast<uint16_t>(std::clamp(clipMinY, 0.0f, static_cast<float>(height)));
                const uint16_t clipWidth = static_cast<uint16_t>(std::clamp(clipMaxX, 0.0f, static_cast<float>(width)) - clipX);
                const uint16_t clipHeight = static_cast<uint16_t>(std::clamp(clipMaxY, 0.0f, static_cast<float>(height)) - clipY);
                if (clipWidth == 0 || clipHeight == 0) {
                    continue;
                }

                bgfx::setScissor(clipX, clipY, clipWidth, clipHeight);
                bgfx::setTexture(0, g_imguiTextureSampler, g_fontTexture);
                bgfx::setVertexBuffer(0, &vertexBuffer, command.VtxOffset, vertexCount - command.VtxOffset);
                bgfx::setIndexBuffer(&indexBuffer, command.IdxOffset, command.ElemCount);
                bgfx::setState(
                    BGFX_STATE_WRITE_RGB |
                    BGFX_STATE_WRITE_A |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                bgfx::submit(viewId, g_imguiProgram);
            }
        }
    }
}

#else

namespace Renderer::DebugUi {
    bool init(SDL_Window*)
    {
        return false;
    }

    void shutdown()
    {
    }

    void processEvent(const SDL_Event&)
    {
    }

    void beginFrame(uint16_t, uint16_t)
    {
    }

    bool wantsMouseCapture()
    {
        return false;
    }

    bool wantsKeyboardCapture()
    {
        return false;
    }

    void showModernDebug(
        const SceneDrawStats&,
        RendererDebugSettings&,
        AtmosphereSettings&,
        DebugDrawSettings&,
        const ModernDebugUiState&,
        const CameraDebugStats&,
        CameraDebugControls*,
        ModernNavigationDebugControls*)
    {
    }

    void render(bgfx::ViewId, uint16_t, uint16_t)
    {
    }
}

#endif
