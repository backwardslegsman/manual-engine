#include "Renderer/DebugUi.hpp"

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

        ImGui::Begin("Renderer Debug");
        ImGui::Text("Layers");
        layerCheckbox("Terrain", RenderLayer::Terrain);
        layerCheckbox("Props", RenderLayer::Props);
        layerCheckbox("Debug", RenderLayer::Debug);
        layerCheckbox("Transparent", RenderLayer::Transparent);
        ImGui::Separator();
        ImGui::Checkbox("Distance culling", &settings.enableDistanceCulling);
        ImGui::SliderFloat("Prop max distance", &settings.propMaxDrawDistance, 0.0f, 200.0f);
        ImGui::SliderFloat("Terrain max distance", &settings.terrainMaxDrawDistance, 0.0f, 300.0f);
        ImGui::Separator();
        ImGui::Text("Debug Draw");
        ImGui::Checkbox("Debug draw enabled", &debugDraw.enabled);
        ImGui::Checkbox("Selected bounds", &debugDraw.selectedBounds);
        ImGui::Checkbox("Collision bounds", &debugDraw.collisionBounds);
        ImGui::Checkbox("Chunk borders", &debugDraw.chunkBorders);
        ImGui::Checkbox("Terrain tile bounds", &debugDraw.terrainTileBounds);
        ImGui::Checkbox("Navigation tile bounds", &debugDraw.navigationTileBounds);
        ImGui::Checkbox("Navigation mesh edges", &debugDraw.navigationMeshEdges);
        ImGui::Checkbox("Navigation current path", &debugDraw.navigationCurrentPath);
        ImGui::Checkbox("Navigation nearest point", &debugDraw.navigationNearestPoint);
        ImGui::Checkbox("Navigation blocker bounds", &debugDraw.navigationBlockerBounds);
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
        ImGui::Separator();
        ImGui::Text("Terrain tiles");
        ImGui::Text("Live: %u", stats.liveTerrainTiles);
        ImGui::Text("Visible: %u", stats.visibleTerrainTiles);
        ImGui::Text("Submitted: %u", stats.submittedTerrainTiles);
        ImGui::Text("Layer/flag culled: %u", stats.layerOrFlagCulledTerrainTiles);
        ImGui::Text("Frustum culled: %u", stats.frustumCulledTerrainTiles);
        ImGui::Text("Distance culled: %u", stats.distanceCulledTerrainTiles);
        ImGui::Separator();
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
        } else {
            ImGui::Text("No player actor");
        }
        ImGui::Separator();
        ImGui::Text("Terrain LODs");
        for (uint32_t index = 0; index < terrainLods.counts.size(); ++index) {
            ImGui::Text("LOD%u: %u", index, terrainLods.counts[index]);
        }
        ImGui::Separator();
        ImGui::Text("Spatial Registry");
        ImGui::Text("Active cells: %u", spatial.activeCells);
        ImGui::Text("Registered objects: %u", spatial.registeredObjects);
        ImGui::Text("Camera cell: %d, %d", spatial.currentCellX, spatial.currentCellZ);
        ImGui::Text("Objects in camera cell: %u", spatial.objectsInCurrentCell);
        ImGui::Text("Objects within %.1f: %u", spatial.nearQueryRadius, spatial.objectsNearCamera);
        ImGui::Separator();
        ImGui::Text("Navigation");
        ImGui::Text("Loaded tiles: %u", navigation.loadedTiles);
        ImGui::Text("Polygon edges: %u", navigation.polygonEdgeCount);
        ImGui::Text("Blocker vertices/triangles: %u / %u",
            navigation.blockerVertexCount,
            navigation.blockerTriangleCount);
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
        if (navigationControls) {
            if (ImGui::Button("Rebuild Visible Nav Tiles")) {
                navigationControls->rebuildVisibleTilesRequested = true;
            }
            ImGui::SliderFloat("Agent radius", &navigationControls->agent.radius, 0.1f, 2.0f);
            ImGui::SliderFloat("Agent height", &navigationControls->agent.height, 0.5f, 4.0f);
            ImGui::SliderFloat("Agent max slope", &navigationControls->agent.maxSlopeDegrees, 0.0f, 89.0f);
            ImGui::SliderFloat("Agent max climb", &navigationControls->agent.maxClimb, 0.0f, 2.0f);
            ImGui::SliderFloat("Recast cell size", &navigationControls->build.cellSize, 0.05f, 1.0f);
            ImGui::SliderFloat("Recast cell height", &navigationControls->build.cellHeight, 0.05f, 1.0f);
        } else {
            ImGui::Text("Agent radius/height: %.2f / %.2f", navigation.agent.radius, navigation.agent.height);
            ImGui::Text("Agent slope/climb: %.1f / %.2f", navigation.agent.maxSlopeDegrees, navigation.agent.maxClimb);
            ImGui::Text("Recast cell size/height: %.2f / %.2f", navigation.build.cellSize, navigation.build.cellHeight);
        }
        ImGui::Separator();
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
                    ImGui::Text("Terrain culled layer/frustum/distance: %u / %u / %u",
                        group.layerOrFlagCulledTerrainTiles,
                        group.frustumCulledTerrainTiles,
                        group.distanceCulledTerrainTiles);
                    ImGui::TreePop();
                }
            }
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

            uint32_t indexOffset = 0;
            for (const ImDrawCmd& command : commandList->CmdBuffer) {
                if (command.UserCallback) {
                    command.UserCallback(commandList, &command);
                    indexOffset += command.ElemCount;
                    continue;
                }

                const float clipMinX = (command.ClipRect.x - clipOffset.x) * framebufferScale.x;
                const float clipMinY = (command.ClipRect.y - clipOffset.y) * framebufferScale.y;
                const float clipMaxX = (command.ClipRect.z - clipOffset.x) * framebufferScale.x;
                const float clipMaxY = (command.ClipRect.w - clipOffset.y) * framebufferScale.y;
                if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) {
                    indexOffset += command.ElemCount;
                    continue;
                }

                const uint16_t clipX = static_cast<uint16_t>(std::clamp(clipMinX, 0.0f, static_cast<float>(width)));
                const uint16_t clipY = static_cast<uint16_t>(std::clamp(clipMinY, 0.0f, static_cast<float>(height)));
                const uint16_t clipWidth = static_cast<uint16_t>(std::clamp(clipMaxX, 0.0f, static_cast<float>(width)) - clipX);
                const uint16_t clipHeight = static_cast<uint16_t>(std::clamp(clipMaxY, 0.0f, static_cast<float>(height)) - clipY);
                if (clipWidth == 0 || clipHeight == 0) {
                    indexOffset += command.ElemCount;
                    continue;
                }

                bgfx::setScissor(clipX, clipY, clipWidth, clipHeight);
                bgfx::setTexture(0, g_imguiTextureSampler, g_fontTexture);
                bgfx::setVertexBuffer(0, &vertexBuffer, command.VtxOffset, vertexCount - command.VtxOffset);
                bgfx::setIndexBuffer(&indexBuffer, indexOffset + command.IdxOffset, command.ElemCount);
                bgfx::setState(
                    BGFX_STATE_WRITE_RGB |
                    BGFX_STATE_WRITE_A |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                );
                bgfx::submit(viewId, g_imguiProgram);
                indexOffset += command.ElemCount;
            }
        }
    }
}
