#include "Renderer/DebugUi.hpp"

#ifndef MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#define MANUAL_ENGINE_ENABLE_DEBUG_TOOLS 1
#endif

#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "Renderer/core.hpp"

namespace {
    bgfx::VertexLayout g_imguiVertexLayout;
    bgfx::ProgramHandle g_imguiProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_imguiTextureSampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle g_fontTexture = BGFX_INVALID_HANDLE;
    bool g_initialized = false;

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
            memory
        );

        return bgfx::isValid(g_fontTexture);
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
        if (!g_initialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event);
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

    void showRendererDebug(
        const SceneDrawStats& stats,
        RendererDebugSettings& settings,
        AtmosphereSettings& atmosphere,
        DebugDrawSettings& debugDraw,
        const TerrainLodDebugStats& terrainLods,
        const SpatialRegistryDebugStats& spatial,
        const NavigationDebugStats& navigation,
        NavigationDebugControls* navigationControls,
        const CameraDebugStats& camera,
        CameraDebugControls* cameraControls,
        const BiomeDebugStats& biome,
        const DebugPickingStats& picking,
        const InteractionDebugStats& interaction,
        WorldSaveDebugControls* worldSave,
        const PlayerActorDebugStats& player)
    {
        if (!g_initialized) {
            return;
        }

        auto layerCheckbox = [&settings](const char* label, RenderLayer layer) {
            const uint32_t bit = static_cast<uint32_t>(layer);
            bool enabled = (settings.layerMask & bit) != 0;
            if (ImGui::Checkbox(label, &enabled)) {
                if (enabled) {
                    settings.layerMask |= bit;
                } else {
                    settings.layerMask &= ~bit;
                }
            }
        };

        ImGui::SetNextWindowSize(ImVec2(560.0f, 720.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Renderer Debug");
        if (ImGui::BeginTabBar("DebugTabs")) {
        if (ImGui::BeginTabItem("Render")) {
        ImGui::Text("Layers");
        ImGui::Text("Scene mode: %s", settings.sceneMode.c_str());
        if (!settings.sceneStatus.empty()) {
            ImGui::TextWrapped("%s", settings.sceneStatus.c_str());
        }
        if (settings.hasAuthoredSceneDiagnostics) {
            ImGui::Separator();
            ImGui::Text("Authored Scene");
            if (!settings.authoredScenePath.empty()) {
                ImGui::TextWrapped("Path: %s", settings.authoredScenePath.c_str());
            }
            if (!settings.authoredSourceFormat.empty()) {
                ImGui::Text("Source format: %s", settings.authoredSourceFormat.c_str());
            }
            ImGui::Text("Imported N/M/P/Mt/T/L: %u / %u / %u / %u / %u / %u",
                settings.authoredImportedNodes,
                settings.authoredImportedMeshes,
                settings.authoredImportedPrimitives,
                settings.authoredImportedMaterials,
                settings.authoredImportedTextures,
                settings.authoredImportedLights);
            ImGui::Text("Created mesh/material/instance/light: %u / %u / %u / %u",
                settings.authoredCreatedMeshes,
                settings.authoredCreatedMaterials,
                settings.authoredCreatedInstances,
                settings.authoredCreatedLights);
            ImGui::Text("Textures ok/failed/fallback: %u / %u / %u",
                settings.authoredTextureLoaded,
                settings.authoredTextureFailed,
                settings.authoredTextureFallback);
            ImGui::Text("Texture bytes: %llu", static_cast<unsigned long long>(settings.authoredTextureBytes));
            ImGui::Text("Sectors loaded/pending/failed: %u/%u, %u, %u",
                settings.authoredLoadedSectors,
                settings.authoredTotalSectors,
                settings.authoredPendingLoadSectors + settings.authoredPendingUnloadSectors,
                settings.authoredFailedSectors);
            ImGui::Text("Sector bytes: %llu", static_cast<unsigned long long>(settings.authoredSectorBytes));
            ImGui::Text("Authored lights active/zero/over-budget: %u / %u / %u",
                settings.authoredActiveLights,
                settings.authoredDisabledZeroLights,
                settings.authoredSkippedOverBudgetLights);
            ImGui::Text("Cache: %s", settings.authoredCacheStatus.c_str());
            if (!settings.authoredCacheMessage.empty()) {
                ImGui::TextWrapped("Cache message: %s", settings.authoredCacheMessage.c_str());
            }
            ImGui::Text("Async: %s queued/completed/failed/pending %u/%u/%u/%u",
                settings.authoredAsyncPhase.c_str(),
                settings.authoredAsyncQueued,
                settings.authoredAsyncCompleted,
                settings.authoredAsyncFailed,
                settings.authoredAsyncPending);
            ImGui::Text("Async ms cache/import/write: %.3f / %.3f / %.3f",
                settings.authoredCacheReadMs,
                settings.authoredImportMs,
                settings.authoredCacheWriteMs);
            if (!settings.authoredAsyncMessage.empty()) {
                ImGui::TextWrapped("Async message: %s", settings.authoredAsyncMessage.c_str());
            }
            ImGui::Text("Warnings: %u", settings.authoredWarnings);
        }
        if (settings.hasAnimationDiagnostics) {
            ImGui::Separator();
            ImGui::Text("Animation");
            if (!settings.animationPath.empty()) {
                ImGui::TextWrapped("Path: %s", settings.animationPath.c_str());
            }
            if (!settings.animationStatus.empty()) {
                ImGui::TextWrapped("%s", settings.animationStatus.c_str());
            }
            ImGui::Text("Loaded: %s", settings.animationLoaded ? "yes" : "no");
            ImGui::Text("Async: %s queued/completed/failed/pending %u/%u/%u/%u",
                settings.animationAsyncPhase.c_str(),
                settings.animationAsyncQueued,
                settings.animationAsyncCompleted,
                settings.animationAsyncFailed,
                settings.animationAsyncPending);
            ImGui::Text("Cache: %s identity %s",
                settings.animationCacheStatus.c_str(),
                settings.animationCacheIdentity.c_str());
            if (!settings.animationCacheMessage.empty()) {
                ImGui::TextWrapped("Cache message: %s", settings.animationCacheMessage.c_str());
            }
            ImGui::Text("Async ms cache/import/write: %.3f / %.3f / %.3f",
                settings.animationCacheReadMs,
                settings.animationImportMs,
                settings.animationCacheWriteMs);
            if (!settings.animationAsyncMessage.empty()) {
                ImGui::TextWrapped("Async message: %s", settings.animationAsyncMessage.c_str());
            }
            ImGui::Checkbox("Animation enabled", &settings.animationEnabled);
            ImGui::Checkbox("Animation playing", &settings.animationPlaying);
            ImGui::Checkbox("Animation loop", &settings.animationLooping);
            int clipIndex = static_cast<int>(settings.animationClipIndex);
            const int maxClipIndex = settings.animationClipCount > 0
                ? static_cast<int>(settings.animationClipCount - 1)
                : 0;
            if (ImGui::SliderInt("Animation clip", &clipIndex, 0, maxClipIndex)) {
                settings.animationClipIndex = static_cast<uint32_t>(std::max(clipIndex, 0));
                settings.animationTimeSeconds = 0.0f;
            }
            const float maxTime = std::max(settings.animationClipDurationSeconds, 0.001f);
            ImGui::SliderFloat("Animation time", &settings.animationTimeSeconds, 0.0f, maxTime);
            ImGui::SliderFloat("Animation speed", &settings.animationPlaybackSpeed, -3.0f, 3.0f);
            ImGui::Text("Clips/joints/skinned instances: %u / %u / %u",
                settings.animationClipCount,
                settings.animationJointCount,
                settings.animationSkinnedInstanceCount);
            ImGui::Text("Created skinned meshes: %u", settings.animationCreatedSkinnedMeshCount);
            ImGui::Text("Texture fallbacks: %u", settings.animationTextureFallbackCount);
            ImGui::Text("Sampled/failed pose updates: %u / %u",
                settings.animationSampledFrameCount,
                settings.animationFailedPoseUpdateCount);
            ImGui::Text("Warnings: %u", settings.animationWarningCount);
            if (!settings.animationLastWarning.empty()) {
                ImGui::TextWrapped("Last warning: %s", settings.animationLastWarning.c_str());
            }
            ImGui::Text("Clip duration: %.3fs", settings.animationClipDurationSeconds);
            ImGui::Text("Crossfade: %s", settings.animationCrossfadeActive ? "active" : "inactive");
            if (settings.animationCrossfadeActive) {
                ImGui::Text("Target/elapsed/duration/weight: %u / %.3fs / %.3fs / %.2f",
                    settings.animationCrossfadeTargetClipIndex,
                    settings.animationCrossfadeElapsedSeconds,
                    settings.animationCrossfadeDurationSeconds,
                    settings.animationCrossfadeWeight);
            }
            ImGui::Text("Completed crossfades: %u", settings.animationCompletedCrossfadeCount);
        }
        ImGui::Separator();
        layerCheckbox("Terrain", RenderLayer::Terrain);
        layerCheckbox("Props", RenderLayer::Props);
        layerCheckbox("Debug", RenderLayer::Debug);
        layerCheckbox("Transparent", RenderLayer::Transparent);
        ImGui::Separator();
        ImGui::Checkbox("Distance culling", &settings.enableDistanceCulling);
        ImGui::SliderFloat("Prop max distance", &settings.propMaxDrawDistance, 0.0f, 200.0f);
        ImGui::SliderFloat("Terrain max distance", &settings.terrainMaxDrawDistance, 0.0f, 300.0f);
        ImGui::Checkbox("Experimental scene character movement", &settings.sceneCharacterExperimental);
        ImGui::Separator();
        ImGui::Text("Debug Draw");
        ImGui::Checkbox("Debug draw enabled", &debugDraw.enabled);
        ImGui::Checkbox("Selected bounds", &debugDraw.selectedBounds);
        ImGui::Checkbox("Collision bounds", &debugDraw.collisionBounds);
        ImGui::Checkbox("Chunk borders", &debugDraw.chunkBorders);
        ImGui::Checkbox("Terrain tile bounds", &debugDraw.terrainTileBounds);
        ImGui::Checkbox("Terrain slope warnings", &debugDraw.terrainSlopeWarnings);
        ImGui::Checkbox("Navigation tile bounds", &debugDraw.navigationTileBounds);
        ImGui::Checkbox("Navigation mesh edges", &debugDraw.navigationMeshEdges);
        ImGui::Checkbox("Navigation current path", &debugDraw.navigationCurrentPath);
        ImGui::Checkbox("Navigation nearest point", &debugDraw.navigationNearestPoint);
        ImGui::Checkbox("Navigation blocker bounds", &debugDraw.navigationBlockerBounds);
        ImGui::Checkbox("Navigation portals", &debugDraw.navigationPortals);
        ImGui::Checkbox("Navigation connectivity links", &debugDraw.navigationConnectivityLinks);
        ImGui::Checkbox("World nav graph nodes", &debugDraw.worldNavigationGraphNodes);
        ImGui::Checkbox("World nav graph edges", &debugDraw.worldNavigationGraphEdges);
        ImGui::Checkbox("World nav route", &debugDraw.worldNavigationRoute);
        ImGui::Checkbox("Camera frustum", &debugDraw.cameraFrustum);
        ImGui::Checkbox("Actor destination", &debugDraw.actorDestination);
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
        ImGui::Text("Lights");
        ImGui::Text("Live: %u", stats.liveLightCount);
        ImGui::Text("Active: %u", stats.activeLightCount);
        ImGui::Text("Submitted forward: %u / %u", stats.submittedForwardLightCount, Renderer::MaxForwardLights);
        ImGui::Text("Disabled: %u", stats.disabledLightCount);
        ImGui::Text("Over budget: %u", stats.overBudgetLightCount);
        ImGui::Separator();
        ImGui::Text("Mesh instances");
        ImGui::Text("Live: %u", stats.liveMeshInstances);
        ImGui::Text("Visible: %u", stats.visibleMeshInstances);
        ImGui::Text("Submitted: %u", stats.submittedMeshInstances);
        ImGui::Text("Layer/flag culled: %u", stats.layerOrFlagCulledMeshInstances);
        ImGui::Text("Frustum culled: %u", stats.frustumCulledMeshInstances);
        ImGui::Text("Distance culled: %u", stats.distanceCulledMeshInstances);
        ImGui::Text("Visible draw items: %u", stats.visibleMeshDrawItems);
        ImGui::Text("Submitted draw items: %u", stats.submittedMeshDrawItems);
        ImGui::Text("Batches: %u", stats.meshBatchCount);
        ImGui::Text("Largest batch: %u", stats.largestMeshBatchSize);
        ImGui::Text("Opaque draws: %u / %u", stats.submittedOpaqueMeshDrawItems, stats.visibleOpaqueMeshDrawItems);
        ImGui::Text("Alpha mask draws: %u / %u", stats.submittedAlphaMaskMeshDrawItems, stats.visibleAlphaMaskMeshDrawItems);
        ImGui::Text("Alpha blend draws: %u / %u", stats.submittedAlphaBlendMeshDrawItems, stats.visibleAlphaBlendMeshDrawItems);
        ImGui::Text("Pass batches O/M/B: %u / %u / %u",
            stats.opaqueMeshBatchCount,
            stats.alphaMaskMeshBatchCount,
            stats.alphaBlendMeshBatchCount);
        ImGui::Separator();
        ImGui::Text("Skinned mesh instances");
        ImGui::Text("Live: %u", stats.liveSkinnedMeshInstances);
        ImGui::Text("Visible: %u", stats.visibleSkinnedMeshInstances);
        ImGui::Text("Submitted: %u", stats.submittedSkinnedMeshInstances);
        ImGui::Text("Layer/flag culled: %u", stats.layerOrFlagCulledSkinnedMeshInstances);
        ImGui::Text("Frustum culled: %u", stats.frustumCulledSkinnedMeshInstances);
        ImGui::Text("Distance culled: %u", stats.distanceCulledSkinnedMeshInstances);
        ImGui::Text("Visible draw items: %u", stats.visibleSkinnedMeshDrawItems);
        ImGui::Text("Submitted draw items: %u", stats.submittedSkinnedMeshDrawItems);
        ImGui::Text("Joint palettes submitted/truncated: %u / %u",
            stats.submittedSkinnedJointPaletteCount,
            stats.truncatedSkinnedJointPaletteCount);
        ImGui::Separator();
        ImGui::Text("Terrain tiles");
        ImGui::Text("Live: %u", stats.liveTerrainTiles);
        ImGui::Text("Visible: %u", stats.visibleTerrainTiles);
        ImGui::Text("Submitted: %u", stats.submittedTerrainTiles);
        ImGui::Text("Layered assigned/submitted/fallback: %u / %u / %u",
            stats.assignedLayeredTerrainTiles,
            stats.submittedLayeredTerrainTiles,
            stats.fallbackTerrainTiles);
        ImGui::Text("Layer/flag culled: %u", stats.layerOrFlagCulledTerrainTiles);
        ImGui::Text("Frustum culled: %u", stats.frustumCulledTerrainTiles);
        ImGui::Text("Distance culled: %u", stats.distanceCulledTerrainTiles);
        ImGui::Separator();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Terrain")) {
        ImGui::Text("Terrain LODs");
        for (uint32_t index = 0; index < terrainLods.counts.size(); ++index) {
            ImGui::Text("LOD%u: %u", index, terrainLods.counts[index]);
        }
        ImGui::Text("Active nav max slope: %.1f", terrainLods.activeNavMaxSlopeDegrees);
        if (!terrainLods.cameraChunkDiagnostics.empty()) {
            ImGui::TextWrapped("Camera chunk: %s", terrainLods.cameraChunkDiagnostics.c_str());
        }
        if (!terrainLods.hoveredChunkDiagnostics.empty()) {
            ImGui::TextWrapped("Hovered chunk: %s", terrainLods.hoveredChunkDiagnostics.c_str());
        }
        if (!terrainLods.selectedChunkDiagnostics.empty()) {
            ImGui::TextWrapped("Selected chunk: %s", terrainLods.selectedChunkDiagnostics.c_str());
        }
        if (!terrainLods.cameraBiomeGeneration.empty()) {
            ImGui::TextWrapped("Camera biome generation: %s", terrainLods.cameraBiomeGeneration.c_str());
        }
        ImGui::Separator();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("World")) {
        if (worldSave) {
            ImGui::Text("World Save");
            ImGui::InputText("Save path", worldSave->path.data(), worldSave->path.size());
            if (ImGui::Button("Save World")) {
                worldSave->saveRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load World")) {
                worldSave->loadRequested = true;
            }
            if (ImGui::Button("Remove Selected")) {
                worldSave->removeSelectedRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Persist Selected Transform")) {
                worldSave->persistSelectedRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Selected Override")) {
                worldSave->resetSelectedOverrideRequested = true;
            }
            ImGui::SliderFloat("Move step", &worldSave->editMoveStep, 0.05f, 16.0f);
            ImGui::SliderFloat("Yaw step degrees", &worldSave->editRotateStepDegrees, 1.0f, 90.0f);
            if (ImGui::Button("-X")) {
                worldSave->nudgeSelectedNegativeXRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+X")) {
                worldSave->nudgeSelectedPositiveXRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("-Y")) {
                worldSave->nudgeSelectedNegativeYRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+Y")) {
                worldSave->nudgeSelectedPositiveYRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("-Z")) {
                worldSave->nudgeSelectedNegativeZRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+Z")) {
                worldSave->nudgeSelectedPositiveZRequested = true;
            }
            if (ImGui::Button("Yaw -")) {
                worldSave->rotateSelectedNegativeYawRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Yaw +")) {
                worldSave->rotateSelectedPositiveYawRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Move Selected To Terrain Hit")) {
                worldSave->moveSelectedToTerrainRequested = true;
            }
            ImGui::InputText("Place archetype", worldSave->placeArchetypeId.data(), worldSave->placeArchetypeId.size());
            if (ImGui::Button("Place Archetype")) {
                worldSave->placeArchetypeRequested = true;
            }
            if (!worldSave->status.empty()) {
                ImGui::TextWrapped("%s", worldSave->status.c_str());
            }
            ImGui::Separator();
        }
        ImGui::Text("Player Actor");
        if (player.valid) {
            ImGui::Text("World object: %u", player.worldObjectId);
            ImGui::Text("Stable id: %s", player.stableId.empty() ? "<none>" : player.stableId.c_str());
            ImGui::Text("Position: %.2f, %.2f, %.2f", player.position.x, player.position.y, player.position.z);
            ImGui::Text("Velocity: %.2f, %.2f, %.2f", player.velocity.x, player.velocity.y, player.velocity.z);
            ImGui::Text("Facing radians: %.2f", player.facingRadians);
            ImGui::Text("Collision: %s", player.collisionEnabled ? "enabled" : "disabled");
            ImGui::Text("Collision radius/height: %.2f / %.2f", player.collisionRadius, player.collisionHeight);
            ImGui::Text("Blocked X/Z: %s / %s", player.blockedX ? "yes" : "no", player.blockedZ ? "yes" : "no");
            ImGui::Text("Collision hits: %u", player.collisionHitCount);
            if (player.firstBlockingObjectId != UINT32_MAX) {
                ImGui::Text("First blocker: %u", player.firstBlockingObjectId);
                ImGui::Text("First blocker stable id: %s",
                    player.firstBlockingStableId.empty() ? "<none>" : player.firstBlockingStableId.c_str());
            }
            if (player.hasGroundHeight) {
                ImGui::Text("Ground height: %.2f", player.groundHeight);
            } else {
                ImGui::Text("Ground height: no loaded terrain");
            }
            ImGui::Text("Path status: %s", player.pathStatus.empty() ? "<none>" : player.pathStatus.c_str());
            ImGui::Text("Path destination: %.2f, %.2f, %.2f",
                player.pathDestination.x,
                player.pathDestination.y,
                player.pathDestination.z);
            ImGui::Text("Path corner/count: %u / %u", player.pathCurrentCorner, player.pathPointCount);
            ImGui::Text("Path arrival/corner radius: %.2f / %.2f",
                player.pathArrivalRadius,
                player.pathCornerAdvanceRadius);
            ImGui::Text("Path blocked ticks: %u", player.pathBlockedTicks);
            ImGui::Text("Path repaths used: %u", player.pathRepathAttemptsUsed);
            ImGui::Text("Path query: %s", player.pathLastQueryStatus.empty() ? "<none>" : player.pathLastQueryStatus.c_str());
            if (!player.pathLastQueryMessage.empty()) {
                ImGui::TextWrapped("%s", player.pathLastQueryMessage.c_str());
            }
            ImGui::Text("Route status: %s", player.routeStatus.empty() ? "<none>" : player.routeStatus.c_str());
            ImGui::Text("Route waypoint/count: %u / %u", player.routeCurrentWaypoint, player.routeWaypointCount);
            ImGui::Text("Route final destination: %.2f, %.2f, %.2f",
                player.routeFinalDestination.x,
                player.routeFinalDestination.y,
                player.routeFinalDestination.z);
            if (!player.routeMessage.empty()) {
                ImGui::TextWrapped("%s", player.routeMessage.c_str());
            }
        } else {
            ImGui::Text("No player actor");
        }
        ImGui::Separator();
        ImGui::Text("Spatial Registry");
        ImGui::Text("Active cells: %u", spatial.activeCells);
        ImGui::Text("Registered objects: %u", spatial.registeredObjects);
        ImGui::Text("Camera cell: %d, %d", spatial.currentCellX, spatial.currentCellZ);
        ImGui::Text("Objects in camera cell: %u", spatial.objectsInCurrentCell);
        ImGui::Text("Objects within %.1f: %u", spatial.nearQueryRadius, spatial.objectsNearCamera);
        ImGui::Separator();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Navigation")) {
        ImGui::Text("Navigation");
        if (!navigation.activeNavigationProfileId.empty()) {
            ImGui::Text("Profile: %s", navigation.activeNavigationProfileId.c_str());
        }
        ImGui::Text("Loaded tiles: %u", navigation.loadedTiles);
        ImGui::Text("Polygon edges: %u", navigation.polygonEdgeCount);
        ImGui::Text("Blocker vertices/triangles: %u / %u",
            navigation.blockerVertexCount,
            navigation.blockerTriangleCount);
        ImGui::Text("Connectivity chunks: %u", navigation.connectivityChunkCount);
        ImGui::Text("Connectivity portals connected/total: %u / %u",
            navigation.connectivityConnectedPortalCount,
            navigation.connectivityPortalCount);
        ImGui::Text("Connectivity partial/blocked chunks: %u / %u",
            navigation.connectivityPartialChunkCount,
            navigation.connectivityBlockedChunkCount);
        if (!navigation.cameraChunkConnectivitySummary.empty()) {
            ImGui::TextWrapped("Camera chunk: %s", navigation.cameraChunkConnectivitySummary.c_str());
        }
        if (!navigation.selectedChunkConnectivitySummary.empty()) {
            ImGui::TextWrapped("Selected chunk: %s", navigation.selectedChunkConnectivitySummary.c_str());
        }
        if (!navigation.cameraChunkTileSummary.empty()) {
            ImGui::TextWrapped("Camera tile: %s", navigation.cameraChunkTileSummary.c_str());
        }
        if (!navigation.hoveredChunkTileSummary.empty()) {
            ImGui::TextWrapped("Hovered tile: %s", navigation.hoveredChunkTileSummary.c_str());
        }
        if (!navigation.selectedChunkTileSummary.empty()) {
            ImGui::TextWrapped("Selected tile: %s", navigation.selectedChunkTileSummary.c_str());
        }
        if (!navigation.cameraChunkPortalSummary.empty()) {
            ImGui::TextWrapped("Camera portals: %s", navigation.cameraChunkPortalSummary.c_str());
        }
        if (!navigation.hoveredChunkPortalSummary.empty()) {
            ImGui::TextWrapped("Hovered portals: %s", navigation.hoveredChunkPortalSummary.c_str());
        }
        if (!navigation.selectedChunkPortalSummary.empty()) {
            ImGui::TextWrapped("Selected portals: %s", navigation.selectedChunkPortalSummary.c_str());
        }
        ImGui::Text("World graph: %s", navigation.hasWorldGraph ? "built" : "empty");
        ImGui::Text("World graph center: %d, %d", navigation.worldGraphCenterX, navigation.worldGraphCenterZ);
        ImGui::Text("World graph nodes/edges/blocked: %u / %u / %u",
            navigation.worldGraphNodeCount,
            navigation.worldGraphEdgeCount,
            navigation.worldGraphBlockedEdgeCount);
        ImGui::Text("Last coarse route: %s (%u chunks, %.2f cost)",
            navigation.lastWorldRouteStatus.empty() ? "<none>" : navigation.lastWorldRouteStatus.c_str(),
            navigation.lastWorldRouteChunkCount,
            navigation.lastWorldRouteCost);
        if (!navigation.lastWorldRouteMessage.empty()) {
            ImGui::TextWrapped("%s", navigation.lastWorldRouteMessage.c_str());
        }
        if (navigation.hasLastRebuiltChunk) {
            ImGui::Text("Last rebuilt chunk: %d, %d",
                navigation.lastRebuiltChunkX,
                navigation.lastRebuiltChunkZ);
        } else {
            ImGui::Text("Last rebuilt chunk: none");
        }
        ImGui::Text("Selected nav blocker: %s", navigation.selectedObjectNavBlocking ? "yes" : "no");
        ImGui::Text("Selected actors: %u", navigation.selectedActorCount);
        if (!navigation.selectedActorSummary.empty()) {
            ImGui::TextWrapped("%s", navigation.selectedActorSummary.c_str());
        }
        if (!navigation.selectedActorCommandSummary.empty()) {
            ImGui::TextWrapped("Selected command: %s", navigation.selectedActorCommandSummary.c_str());
        }
        if (navigation.hasLastGroupDestination) {
            ImGui::Text("Last group destination: %.2f, %.2f, %.2f",
                navigation.lastGroupDestination.x,
                navigation.lastGroupDestination.y,
                navigation.lastGroupDestination.z);
            ImGui::Text("Last group command success/failure: %u / %u",
                navigation.lastGroupCommandSuccessCount,
                navigation.lastGroupCommandFailureCount);
            if (!navigation.lastGroupCommandStatus.empty()) {
                ImGui::TextWrapped("%s", navigation.lastGroupCommandStatus.c_str());
            }
            if (!navigation.lastGroupCommandFailureSummary.empty()) {
                ImGui::TextWrapped("%s", navigation.lastGroupCommandFailureSummary.c_str());
            }
        }
        ImGui::Text("Current path: %s (%u points)",
            navigation.currentPathStatus.empty() ? "<none>" : navigation.currentPathStatus.c_str(),
            navigation.currentPathPointCount);
        ImGui::Text("Nearest point: %s", navigation.nearestPointStatus.empty() ? "<none>" : navigation.nearestPointStatus.c_str());
        if (navigation.hasNearestPoint) {
            ImGui::Text("Nearest position: %.2f, %.2f, %.2f",
                navigation.nearestPoint.x,
                navigation.nearestPoint.y,
                navigation.nearestPoint.z);
        }
        ImGui::Text("Last build: %s", navigation.lastBuildStatus.empty() ? "<none>" : navigation.lastBuildStatus.c_str());
        if (!navigation.lastBuildMessage.empty()) {
            ImGui::TextWrapped("%s", navigation.lastBuildMessage.c_str());
        }
        ImGui::Text("Last query: %s", navigation.lastQueryStatus.empty() ? "<none>" : navigation.lastQueryStatus.c_str());
        if (!navigation.lastQueryMessage.empty()) {
            ImGui::TextWrapped("%s", navigation.lastQueryMessage.c_str());
        }
        ImGui::Text("Cache identity: %s", navigation.cacheIdentity.empty() ? "<none>" : navigation.cacheIdentity.c_str());
        ImGui::Text("Cache tiles hit/miss/stale/write: %u / %u / %u / %u",
            navigation.cacheTileHits,
            navigation.cacheTileMisses,
            navigation.cacheTileStale,
            navigation.cacheTileWrites);
        ImGui::Text("Cache connectivity hit/miss/write: %u / %u / %u",
            navigation.cacheConnectivityHits,
            navigation.cacheConnectivityMisses,
            navigation.cacheConnectivityWrites);
        ImGui::Text("Cache graph hit/miss/write: %u / %u / %u",
            navigation.cacheGraphHits,
            navigation.cacheGraphMisses,
            navigation.cacheGraphWrites);
        ImGui::Text("Runtime nav cache hit/miss/stale-or-corrupt/load-fail this frame: %u / %u / %u / %u",
            navigation.navTileCacheHitsThisFrame,
            navigation.navTileCacheMissesThisFrame,
            navigation.navTileCacheStaleOrCorruptThisFrame,
            navigation.navTileCacheLoadFailuresThisFrame);
        ImGui::Text("Async cache read queued/completed: %u / %u",
            navigation.cacheReadJobsQueuedThisFrame,
            navigation.cacheReadJobsCompletedThisFrame);
        ImGui::Text("Async cache write queued/completed/failed: %u / %u / %u",
            navigation.cacheWriteJobsQueuedThisFrame,
            navigation.cacheWriteJobsCompletedThisFrame,
            navigation.cacheWriteJobsFailedThisFrame);
        ImGui::Text("Runtime nav worker queued/completed/failed this frame: %u / %u / %u",
            navigation.navTileWorkerBuildsQueuedThisFrame,
            navigation.navTileWorkerBuildsCompletedThisFrame,
            navigation.navTileWorkerBuildsFailedThisFrame);
        ImGui::Text("World graph worker queued/completed/failed this frame: %u / %u / %u",
            navigation.graphWorkerBuildsQueuedThisFrame,
            navigation.graphWorkerBuildsCompletedThisFrame,
            navigation.graphWorkerBuildsFailedThisFrame);
        ImGui::Text("Last graph worker build: %.2f ms at %d, %d",
            navigation.lastGraphWorkerBuildMs,
            navigation.lastGraphWorkerBuildCenterX,
            navigation.lastGraphWorkerBuildCenterZ);
        if (!navigation.lastGraphWorkerBuildMessage.empty()) {
            ImGui::TextWrapped("%s", navigation.lastGraphWorkerBuildMessage.c_str());
        }
        ImGui::Text("Runtime nav chunks ready/pending/failed: %u / %u / %u",
            navigation.navTileReadyChunks,
            navigation.navTilePendingChunks,
            navigation.navTileFailedChunks);
        ImGui::Text("Nav sync processed/deferred chunks: %u / %u",
            navigation.navTileSyncChunksThisFrame,
            navigation.navTileSyncDeferredChunks);
        ImGui::Text("Connectivity processed/deferred chunks: %u / %u",
            navigation.connectivityChunksThisFrame,
            navigation.connectivityDeferredChunks);
        ImGui::Text("Connectivity steps/samples this frame: %u / %u",
            navigation.connectivityStepsThisFrame,
            navigation.connectivitySamplesThisFrame);
        ImGui::Text("Connectivity active chunk: %d, %d",
            navigation.connectivityActiveChunkX,
            navigation.connectivityActiveChunkZ);
        if (!navigation.connectivityLastStepLabel.empty()) {
            ImGui::TextWrapped("Connectivity step: %s", navigation.connectivityLastStepLabel.c_str());
        }
        if (!navigation.cacheLastPath.empty()) {
            ImGui::TextWrapped("Cache path: %s", navigation.cacheLastPath.c_str());
        }
        if (!navigation.cacheLastMessage.empty()) {
            ImGui::TextWrapped("Cache: %s", navigation.cacheLastMessage.c_str());
        }
        ImGui::Text("CPU probes");
        ImGui::Text("Previous full frame CPU: %.3f ms", navigation.previousFrameCpuMs);
        if (ImGui::BeginTable("CpuProbeTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("System");
            ImGui::TableSetupColumn("ms");
            ImGui::TableSetupColumn("System");
            ImGui::TableSetupColumn("ms");
            ImGui::TableHeadersRow();
            const auto row = [](const char* leftName, float leftMs, const char* rightName, float rightMs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(leftName);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", leftMs);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(rightName);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.3f", rightMs);
            };
            row("SDL/Input events", navigation.eventPollingMs, "Input mapping", navigation.inputMappingMs);
            row("Camera update", navigation.cameraUpdateMs, "Chunk streaming", navigation.chunkStreamingMs);
            row("Terrain LOD", navigation.terrainLodMs, "Budget drain", navigation.budgetDrainMs);
            row("Nav tile sync", navigation.navTileSyncMs, "Connectivity", navigation.connectivityMs);
            row("World graph", navigation.worldGraphMs, "Fixed updates", navigation.fixedUpdateMs);
            row("World sync", navigation.worldSyncMs, "Picking", navigation.pickingMs);
            row("Nearest nav point", navigation.nearestNavigationPointMs, "Interactions/commands", navigation.interactionMs);
            row("Debug enqueue", navigation.debugPrimitiveEnqueueMs, "Scene draw", navigation.drawSubmissionMs);
            row("Debug draw", navigation.debugPrimitiveDrawMs, "Debug UI build", navigation.debugUiBuildMs);
            row("Debug UI render", navigation.debugUiRenderMs, "bgfx frame", navigation.bgfxFrameMs);
            row("Post-frame requests", navigation.postFrameRequestsMs, "", 0.0f);
            ImGui::EndTable();
        }
        ImGui::Text("Terrain LOD rebuilds this frame/pending: %u / %u",
            navigation.terrainLodRebuildsThisFrame,
            navigation.terrainLodPendingRebuilds);
        ImGui::Text("Terrain LOD jobs queued/completed/failed: %u / %u / %u",
            navigation.terrainLodJobsQueuedThisFrame,
            navigation.terrainLodJobsCompletedThisFrame,
            navigation.terrainLodJobsFailedThisFrame);
        ImGui::Text("Terrain LOD commits/stale/pending results: %u / %u / %u",
            navigation.terrainLodCommitsThisFrame,
            navigation.terrainLodStaleResultsThisFrame,
            navigation.terrainLodCompletedResults);
        ImGui::Text("Terrain LOD cache hit/miss/stale/corrupt: %u / %u / %u / %u",
            navigation.terrainLodCacheHitsThisFrame,
            navigation.terrainLodCacheMissesThisFrame,
            navigation.terrainLodCacheStaleThisFrame,
            navigation.terrainLodCacheCorruptThisFrame);
        ImGui::Text("Terrain LOD generated this frame: %u", navigation.terrainLodGeneratedThisFrame);
        ImGui::Text("Terrain LOD pending jobs: %u", navigation.terrainLodPendingJobs);
        ImGui::Text("Last terrain LOD worker build: %.3f ms", navigation.lastTerrainLodBuildMs);
        if (!navigation.lastTerrainLodMessage.empty()) {
            ImGui::TextWrapped("%s", navigation.lastTerrainLodMessage.c_str());
        }
        ImGui::Text("Visibility metadata chunks processed/deferred: %u / %u",
            navigation.visibilityChunksProcessedThisFrame,
            navigation.visibilityChunksDeferred);
        ImGui::Text("Visibility metadata terrain/instances updated: %u / %u",
            navigation.visibilityTerrainUpdatedThisFrame,
            navigation.visibilityInstancesUpdatedThisFrame);
        ImGui::Text("Visibility full reapply pending: %s", navigation.visibilityFullReapplyPending ? "yes" : "no");
        ImGui::Separator();
        ImGui::Text("Debug Draw Budget");
        ImGui::Text("Lines generated/submitted/clipped: %u / %u / %u",
            navigation.debugDrawStats.generatedLines,
            navigation.debugDrawStats.submittedLines,
            navigation.debugDrawStats.clippedLines);
        ImGui::Text("Buffer vertices last frame: %u", navigation.debugDrawStats.lastFramePrimitiveBufferSize);
        ImGui::Text("Nav mesh lines submitted/clipped: %u / %u",
            navigation.debugDrawStats.navMeshEdges.submitted,
            navigation.debugDrawStats.navMeshEdges.clipped);
        ImGui::Text("Graph lines submitted/clipped: %u / %u",
            navigation.debugDrawStats.worldGraphEdges.submitted,
            navigation.debugDrawStats.worldGraphEdges.clipped);
        ImGui::Text("Slope lines submitted/clipped: %u / %u",
            navigation.debugDrawStats.terrainSlopeWarnings.submitted,
            navigation.debugDrawStats.terrainSlopeWarnings.clipped);
        ImGui::Text("Collision lines submitted/clipped: %u / %u",
            navigation.debugDrawStats.collisionBounds.submitted,
            navigation.debugDrawStats.collisionBounds.clipped);
        ImGui::Text("Chunk/bounds lines submitted/clipped: %u / %u",
            navigation.debugDrawStats.chunkBorders.submitted,
            navigation.debugDrawStats.chunkBorders.clipped);
        ImGui::Text("Async workers/pending/completed/unloads: %u / %u / %u / %u",
            navigation.asyncWorkerCount,
            navigation.asyncPendingChunkJobs,
            navigation.asyncCompletedChunks,
            navigation.asyncPendingUnloads);
        ImGui::Text("Async committed load/unload this frame: %u / %u",
            navigation.asyncCommittedLoadsThisFrame,
            navigation.asyncCommittedUnloadsThisFrame);
        ImGui::Text("Async cancelled/stale: %u / %u",
            navigation.asyncCancelledJobs,
            navigation.asyncStaleJobs);
        ImGui::Text("Async terrain avg/max ms: %.3f / %.3f",
            navigation.asyncAverageTerrainGenerationMs,
            navigation.asyncMaxTerrainGenerationMs);
        ImGui::Text("Async nav avg/max ms: %.3f / %.3f",
            navigation.asyncAverageNavigationBuildMs,
            navigation.asyncMaxNavigationBuildMs);
        ImGui::Text("Frame budget used/budget/overrun ms: %.3f / %.3f / %.3f",
            navigation.frameBudgetUsedMs,
            navigation.frameBudgetMs,
            navigation.frameBudgetOverrunMs);
        ImGui::Text("Frame budget items run/deferred/pending: %u / %u / %u",
            navigation.frameBudgetItemsRun,
            navigation.frameBudgetItemsDeferred,
            navigation.mainThreadPendingWorkItems);
        ImGui::Text("Slowest budget item: %.3f ms %s",
            navigation.slowestBudgetItemMs,
            navigation.slowestBudgetItemLabel.empty() ? "<none>" : navigation.slowestBudgetItemLabel.c_str());
        ImGui::Text("Budget ms stream/nav/derived/debug/general: %.3f / %.3f / %.3f / %.3f / %.3f",
            navigation.frameBudgetCategoryMs[0],
            navigation.frameBudgetCategoryMs[1],
            navigation.frameBudgetCategoryMs[2],
            navigation.frameBudgetCategoryMs[3],
            navigation.frameBudgetCategoryMs[4]);
        ImGui::Text("Long frames > %.1f ms: %u", navigation.longFrameThresholdMs, navigation.longFrameCount);
        if (!navigation.lastLongFrameSummary.empty()) {
            ImGui::TextWrapped("Last long frame: %.3f ms - %s",
                navigation.lastLongFrameMs,
                navigation.lastLongFrameSummary.c_str());
        }
        if (navigationControls) {
            if (ImGui::Button("Rebuild Visible Nav Tiles")) {
                navigationControls->rebuildVisibleTilesRequested = true;
            }
            ImGui::Checkbox("Navigation cache enabled", &navigationControls->cacheEnabled);
            ImGui::Checkbox("Navigation cache write-through", &navigationControls->cacheWriteThrough);
            ImGui::Checkbox("Async terrain generation", &navigationControls->asyncTerrainEnabled);
            ImGui::Checkbox("Async navigation generation", &navigationControls->asyncNavigationEnabled);
            ImGui::Checkbox("Main thread frame budget", &navigationControls->mainThreadBudgetEnabled);
            ImGui::SliderFloat("Main thread budget ms", &navigationControls->mainThreadBudgetMs, 0.0f, 16.0f);
            int propBatch = static_cast<int>(navigationControls->propSpawnBatchSize);
            if (ImGui::SliderInt("Prop spawns per budget item", &propBatch, 1, 64)) {
                navigationControls->propSpawnBatchSize = static_cast<uint32_t>(propBatch);
            }
            int terrainLodRebuilds = static_cast<int>(navigationControls->terrainLodRebuildsPerFrame);
            if (ImGui::SliderInt("Terrain LOD rebuilds per frame", &terrainLodRebuilds, 0, 32)) {
                navigationControls->terrainLodRebuildsPerFrame = static_cast<uint32_t>(terrainLodRebuilds);
            }
            int visibilityChunks = static_cast<int>(navigationControls->visibilityReapplyChunksPerStep);
            if (ImGui::SliderInt("Visibility chunks per step", &visibilityChunks, 1, 64)) {
                navigationControls->visibilityReapplyChunksPerStep = static_cast<uint32_t>(visibilityChunks);
            }
            int maxDebugLines = static_cast<int>(debugDraw.maxDebugLines);
            if (ImGui::SliderInt("Max debug lines", &maxDebugLines, 1000, 100000)) {
                debugDraw.maxDebugLines = static_cast<uint32_t>(maxDebugLines);
            }
            int maxNavMeshLines = static_cast<int>(debugDraw.maxNavMeshEdgeLines);
            if (ImGui::SliderInt("Max navmesh edge lines", &maxNavMeshLines, 0, 50000)) {
                debugDraw.maxNavMeshEdgeLines = static_cast<uint32_t>(maxNavMeshLines);
            }
            int maxGraphLines = static_cast<int>(debugDraw.maxWorldGraphEdgeLines);
            if (ImGui::SliderInt("Max graph edge lines", &maxGraphLines, 0, 50000)) {
                debugDraw.maxWorldGraphEdgeLines = static_cast<uint32_t>(maxGraphLines);
            }
            int maxSlopeLines = static_cast<int>(debugDraw.maxTerrainSlopeWarningLines);
            if (ImGui::SliderInt("Max slope warning lines", &maxSlopeLines, 0, 20000)) {
                debugDraw.maxTerrainSlopeWarningLines = static_cast<uint32_t>(maxSlopeLines);
            }
            int maxCollisionAabbs = static_cast<int>(debugDraw.maxCollisionAabbs);
            if (ImGui::SliderInt("Max collision AABBs", &maxCollisionAabbs, 0, 10000)) {
                debugDraw.maxCollisionAabbs = static_cast<uint32_t>(maxCollisionAabbs);
            }
            int maxChunkRects = static_cast<int>(debugDraw.maxChunkBorderRects);
            if (ImGui::SliderInt("Max chunk/bounds rects", &maxChunkRects, 0, 10000)) {
                debugDraw.maxChunkBorderRects = static_cast<uint32_t>(maxChunkRects);
            }
            int navSyncChunks = static_cast<int>(navigationControls->navTileSyncChunksPerFrame);
            if (ImGui::SliderInt("Nav sync chunks per frame", &navSyncChunks, 1, 64)) {
                navigationControls->navTileSyncChunksPerFrame = static_cast<uint32_t>(navSyncChunks);
            }
            int connectivityChunks = static_cast<int>(navigationControls->connectivityChunksPerFrame);
            if (ImGui::SliderInt("Connectivity chunks per frame", &connectivityChunks, 1, 64)) {
                navigationControls->connectivityChunksPerFrame = static_cast<uint32_t>(connectivityChunks);
            }
            int connectivitySamples = static_cast<int>(navigationControls->connectivitySamplesPerStep);
            if (ImGui::SliderInt("Connectivity samples per step", &connectivitySamples, 1, 64)) {
                navigationControls->connectivitySamplesPerStep = static_cast<uint32_t>(connectivitySamples);
            }
            int graphThreshold = static_cast<int>(navigationControls->worldGraphRecenterThresholdChunks);
            if (ImGui::SliderInt("Graph recenter threshold chunks", &graphThreshold, 1, 32)) {
                navigationControls->worldGraphRecenterThresholdChunks = static_cast<uint32_t>(graphThreshold);
            }
            ImGui::Text("Worker threads: %u (restart required to change)", navigationControls->workerThreadCount);
            if (ImGui::Button("Generate Visible Nav Cache")) {
                navigationControls->generateVisibleCacheRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh Selected/Visible Cache")) {
                navigationControls->refreshSelectedOrVisibleCacheRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Cache Stats")) {
                navigationControls->clearCacheStatsRequested = true;
            }
            if (ImGui::Button("Rebuild Connectivity")) {
                navigationControls->rebuildConnectivityRequested = true;
            }
            ImGui::SliderFloat("Agent radius", &navigationControls->agent.radius, 0.1f, 2.0f);
            ImGui::SliderFloat("Agent height", &navigationControls->agent.height, 0.5f, 4.0f);
            ImGui::SliderFloat("Agent max slope", &navigationControls->agent.maxSlopeDegrees, 0.0f, 89.0f);
            ImGui::SliderFloat("Agent max climb", &navigationControls->agent.maxClimb, 0.0f, 2.0f);
            ImGui::SliderFloat("Recast cell size", &navigationControls->build.cellSize, 0.05f, 1.0f);
            ImGui::SliderFloat("Recast cell height", &navigationControls->build.cellHeight, 0.05f, 1.0f);
            int samplesPerEdge = static_cast<int>(navigationControls->portalSamplesPerEdge);
            if (ImGui::SliderInt("Portal samples per edge", &samplesPerEdge, 1, 33)) {
                navigationControls->portalSamplesPerEdge = static_cast<uint32_t>(samplesPerEdge);
            }
            ImGui::SliderFloat("Portal edge inset", &navigationControls->portalEdgeInset, 0.0f, 16.0f);
            ImGui::SliderFloat("Portal edge band width", &navigationControls->portalEdgeBandWidth, 0.1f, 24.0f);
            ImGui::SliderFloat("Portal merge distance", &navigationControls->portalMergeDistance, 0.0f, 12.0f);
            ImGui::SliderFloat("Portal neighbor link distance", &navigationControls->portalNeighborLinkDistance, 0.1f, 24.0f);
        } else {
            ImGui::Text("Agent radius/height: %.2f / %.2f", navigation.agent.radius, navigation.agent.height);
            ImGui::Text("Agent slope/climb: %.1f / %.2f", navigation.agent.maxSlopeDegrees, navigation.agent.maxClimb);
            ImGui::Text("Recast cell size/height: %.2f / %.2f", navigation.build.cellSize, navigation.build.cellHeight);
        }
        ImGui::Text("Navigation source resolution: %u", navigation.navigationResolution);
        ImGui::Separator();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Camera/Biome")) {
        ImGui::Text("Camera");
        if (cameraControls) {
            int cameraMode = camera.followMode ? 1 : 0;
            if (ImGui::Combo("Mode", &cameraMode, "Free\0Follow Player\0")) {
                cameraControls->setFreeModeRequested = cameraMode == 0;
                cameraControls->setFollowModeRequested = cameraMode == 1;
            }
            if (ImGui::Button("Recenter Camera")) {
                cameraControls->recenterRequested = true;
            }
            ImGui::SliderFloat("Follow smoothing", &cameraControls->followSmoothing, 0.0f, 30.0f);
            ImGui::SliderFloat("Max follow lag", &cameraControls->maxFollowLag, 0.0f, 30.0f);
        } else {
            ImGui::Text("Mode: %s", camera.followMode ? "Follow Player" : "Free");
        }
        ImGui::Text("Pivot: %.2f, %.2f, %.2f", camera.pivot.x, camera.pivot.y, camera.pivot.z);
        ImGui::Text("Follow target: %s", camera.hasTarget ? "available" : "missing");
        if (camera.hasTarget) {
            ImGui::Text("Target: %.2f, %.2f, %.2f",
                camera.targetPosition.x,
                camera.targetPosition.y,
                camera.targetPosition.z);
        }
        ImGui::Text("Follow offset: %.2f, %.2f, %.2f",
            camera.followOffset.x,
            camera.followOffset.y,
            camera.followOffset.z);
        ImGui::Text("Smoothing / max lag: %.2f / %.2f", camera.followSmoothing, camera.maxFollowLag);
        ImGui::Separator();
        ImGui::Text("Biome");
        if (biome.valid) {
            ImGui::Text("Camera chunk: %d, %d", biome.cameraChunkX, biome.cameraChunkZ);
            ImGui::Text("Camera biome: %s",
                biome.cameraBiomeDisplayName.empty() ? biome.cameraBiomeId.c_str() : biome.cameraBiomeDisplayName.c_str());
            ImGui::Text("Terrain material: %s%s",
                biome.cameraTerrainMaterialBiomeId.empty() ? "<unknown>" : biome.cameraTerrainMaterialBiomeId.c_str(),
                biome.cameraTerrainUsesFallback ? " (fallback)" : "");
            ImGui::Text("Terrain color RGBA: %u, %u, %u, %u",
                static_cast<uint32_t>(biome.cameraTerrainColor[0]),
                static_cast<uint32_t>(biome.cameraTerrainColor[1]),
                static_cast<uint32_t>(biome.cameraTerrainColor[2]),
                static_cast<uint32_t>(biome.cameraTerrainColor[3]));
            if (biome.hasPlayerBiome) {
                ImGui::Text("Player biome: %s (%d, %d)",
                    biome.playerBiomeId.c_str(),
                    biome.playerChunkX,
                    biome.playerChunkZ);
            }
            if (biome.hasHoveredBiome) {
                ImGui::Text("Hovered biome: %s (%d, %d)",
                    biome.hoveredBiomeId.c_str(),
                    biome.hoveredChunkX,
                    biome.hoveredChunkZ);
            }
            if (biome.hasTerrainHitBiome) {
                ImGui::Text("Terrain hit biome: %s (%d, %d)",
                    biome.terrainHitBiomeId.c_str(),
                    biome.terrainHitChunkX,
                    biome.terrainHitChunkZ);
                ImGui::Text("Moisture/roughness/elevation: %.2f / %.2f / %.2f",
                    biome.moisture,
                    biome.roughness,
                    biome.elevation);
            }
        } else {
            ImGui::Text("No biome system");
        }
        ImGui::Separator();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Picking")) {
        ImGui::Text("Debug Picking");
        ImGui::Text("Mouse: %.1f, %.1f", picking.mousePosition.x, picking.mousePosition.y);
        ImGui::Text("Ray origin: %.2f, %.2f, %.2f", picking.rayOrigin.x, picking.rayOrigin.y, picking.rayOrigin.z);
        ImGui::Text("Ray direction: %.3f, %.3f, %.3f", picking.rayDirection.x, picking.rayDirection.y, picking.rayDirection.z);
        if (picking.hasHoveredObject) {
            ImGui::Text("Hovered object: %u", picking.hoveredObjectId);
            ImGui::Text("Hovered stable id: %s", picking.hoveredStableId.empty() ? "<none>" : picking.hoveredStableId.c_str());
            ImGui::Text("Hovered cell: %d, %d", picking.hoveredObjectCellX, picking.hoveredObjectCellZ);
            ImGui::Text("Hovered hit: %.2f, %.2f, %.2f",
                picking.hoveredObjectPosition.x,
                picking.hoveredObjectPosition.y,
                picking.hoveredObjectPosition.z);
            ImGui::Text("Hovered distance: %.2f", picking.hoveredObjectDistance);
        } else {
            ImGui::Text("Hovered object: none");
        }
        if (picking.hasSelectedObject) {
            ImGui::Text("Selected object: %u", picking.selectedObjectId);
            ImGui::Text("Selected stable id: %s", picking.selectedStableId.empty() ? "<none>" : picking.selectedStableId.c_str());
            if (!picking.selectedArchetypeId.empty()) {
                ImGui::Text("Selected archetype: %s", picking.selectedArchetypeId.c_str());
                ImGui::Text("Selected display: %s", picking.selectedArchetypeDisplayName.empty() ? "<unknown>" : picking.selectedArchetypeDisplayName.c_str());
                ImGui::Text("Selected tags: %s", picking.selectedArchetypeTags.empty() ? "<none>" : picking.selectedArchetypeTags.c_str());
            }
            if (picking.selectedIsProcedural) {
                ImGui::Text("Selected local slot: %u", picking.selectedLocalSlot);
            }
            ImGui::Text("Selected position: %.2f, %.2f, %.2f",
                picking.selectedObjectPosition.x,
                picking.selectedObjectPosition.y,
                picking.selectedObjectPosition.z);
            ImGui::Text("Selected rotation: %.2f, %.2f, %.2f",
                picking.selectedObjectRotation.x,
                picking.selectedObjectRotation.y,
                picking.selectedObjectRotation.z);
            ImGui::Text("Selected scale: %.2f, %.2f, %.2f",
                picking.selectedObjectScale.x,
                picking.selectedObjectScale.y,
                picking.selectedObjectScale.z);
            ImGui::Text("Owner chunk: %d, %d", picking.selectedOwnerChunkX, picking.selectedOwnerChunkZ);
            ImGui::Text("Editable: %s", picking.selectedEditable ? "yes" : "no");
            ImGui::Text("Custom persistent: %s", picking.selectedIsCustom ? "yes" : "no");
            ImGui::Text("Persistent override: %s", picking.selectedHasPersistentOverride ? "yes" : "no");
            ImGui::Text("Can reset: %s", picking.selectedCanReset ? "yes" : "no");
        } else {
            ImGui::Text("Selected object: none");
        }
        if (picking.hasTerrainHit) {
            ImGui::Text("Terrain hit: %.2f, %.2f, %.2f",
                picking.terrainHitPosition.x,
                picking.terrainHitPosition.y,
                picking.terrainHitPosition.z);
            ImGui::Text("Terrain chunk: %d, %d", picking.terrainHitChunkX, picking.terrainHitChunkZ);
            ImGui::Text("Terrain distance: %.2f", picking.terrainHitDistance);
        } else {
            ImGui::Text("Terrain hit: none");
        }
        ImGui::Separator();
        ImGui::Text("Interactions");
        if (interaction.hasLastInteraction) {
            ImGui::Text("Last action: %s", interaction.action.c_str());
            ImGui::Text("Target: %s", interaction.target.c_str());
            ImGui::Text("Outcome: %s", interaction.outcome.empty() ? "<none>" : interaction.outcome.c_str());
            if (!interaction.stableId.empty()) {
                ImGui::Text("Stable id: %s", interaction.stableId.c_str());
            }
            if (!interaction.archetypeId.empty()) {
                ImGui::Text("Archetype: %s", interaction.archetypeId.c_str());
                ImGui::Text("Display: %s", interaction.archetypeDisplayName.empty() ? "<unknown>" : interaction.archetypeDisplayName.c_str());
                ImGui::Text("Tags: %s", interaction.archetypeTags.empty() ? "<none>" : interaction.archetypeTags.c_str());
            }
            if (!interaction.resourceId.empty()) {
                ImGui::Text("Resource: %s x%u", interaction.resourceId.c_str(), interaction.resourceAmount);
            }
            ImGui::Text("Chunk: %d, %d", interaction.chunkX, interaction.chunkZ);
            ImGui::Text("Position: %.2f, %.2f, %.2f",
                interaction.position.x,
                interaction.position.y,
                interaction.position.z);
            ImGui::Text("Distance: %.2f", interaction.distance);
        } else {
            ImGui::Text("Last action: none");
        }
        if (!interaction.status.empty()) {
            ImGui::TextWrapped("%s", interaction.status.c_str());
        }
        ImGui::Separator();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Groups")) {
        if (ImGui::CollapsingHeader("Render Groups")) {
            for (const RenderGroupDrawStats& group : stats.renderGroups) {
                const std::string label = group.hasChunkCoord
                    ? group.name + " (" + std::to_string(group.chunkX) + ", " + std::to_string(group.chunkZ) + ")"
                    : group.name;
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::Text("Mesh live/visible/submitted: %u / %u / %u",
                        group.liveMeshInstances,
                        group.visibleMeshInstances,
                        group.submittedMeshInstances);
                    ImGui::Text("Mesh culled layer/frustum/distance: %u / %u / %u",
                        group.layerOrFlagCulledMeshInstances,
                        group.frustumCulledMeshInstances,
                        group.distanceCulledMeshInstances);
                    ImGui::Text("Terrain live/visible/submitted: %u / %u / %u",
                        group.liveTerrainTiles,
                        group.visibleTerrainTiles,
                        group.submittedTerrainTiles);
                    ImGui::Text("Terrain layered/fallback submitted: %u / %u",
                        group.submittedLayeredTerrainTiles,
                        group.fallbackTerrainTiles);
                    ImGui::Text("Terrain culled layer/frustum/distance: %u / %u / %u",
                        group.layerOrFlagCulledTerrainTiles,
                        group.frustumCulledTerrainTiles,
                        group.distanceCulledTerrainTiles);
                    ImGui::TreePop();
                }
            }
        }
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
                // ImDrawCmd offsets are already relative to this command list.
                bgfx::setIndexBuffer(&indexBuffer, command.IdxOffset, command.ElemCount);
                bgfx::setState(
                    BGFX_STATE_WRITE_RGB |
                    BGFX_STATE_WRITE_A |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                );
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

    void showRendererDebug(
        const SceneDrawStats&,
        RendererDebugSettings&,
        AtmosphereSettings&,
        DebugDrawSettings&,
        const TerrainLodDebugStats&,
        const SpatialRegistryDebugStats&,
        const NavigationDebugStats&,
        NavigationDebugControls*,
        const CameraDebugStats&,
        CameraDebugControls*,
        const BiomeDebugStats&,
        const DebugPickingStats&,
        const InteractionDebugStats&,
        WorldSaveDebugControls*,
        const PlayerActorDebugStats&)
    {
    }

    void render(bgfx::ViewId, uint16_t, uint16_t)
    {
    }
}

#endif
