#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <array>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ActorController.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/ChunkStreamer.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/FixedStepLoop.hpp"
#include "Engine/InputMapping.hpp"
#include "Engine/OrbitCamera.hpp"
#include "Engine/Picking.hpp"
#include "Engine/ProceduralChunkContent.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"
#include "Engine/WorldState.hpp"
#include "Engine/input.hpp"
#include "Renderer/DebugUi.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

namespace {
    constexpr uint32_t WindowResetFlags = BGFX_RESET_VSYNC;
    constexpr float DebugPickMaxDistance = 500.0f;
    constexpr float DebugPickTerrainStep = 1.0f;
    constexpr float DebugPickObjectQueryMargin = 2.0f;

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

    Renderer::DebugUi::DebugPickingStats makeDebugPickingStats(
        const Engine::DebugSelectionState& selection,
        const Engine::World& world)
    {
        Renderer::DebugUi::DebugPickingStats stats;
        stats.mousePosition = selection.mousePosition;
        stats.rayOrigin = selection.ray.origin;
        stats.rayDirection = selection.ray.direction;

        if (selection.hoveredObject) {
            stats.hasHoveredObject = true;
            stats.hoveredObjectId = selection.hoveredObject->object.id;
            stats.hoveredObjectPosition = selection.hoveredObject->position;
            stats.hoveredObjectDistance = selection.hoveredObject->distance;
            stats.hoveredObjectCellX = selection.hoveredObject->cell.x;
            stats.hoveredObjectCellZ = selection.hoveredObject->cell.z;
        }

        if (selection.selectedObject && world.isValid(selection.selectedObject->object)) {
            stats.hasSelectedObject = true;
            stats.selectedObjectId = selection.selectedObject->object.id;
            stats.selectedObjectPosition = world.position(selection.selectedObject->object).value_or(selection.selectedObject->position);
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
        const Engine::TerrainSystem& terrain)
    {
        Engine::WorldStateSnapshot snapshot;
        snapshot.settings.seed = 0;
        snapshot.settings.chunkSize = chunkStreamer.settings().chunkSize;
        snapshot.settings.loadRadiusChunks = chunkStreamer.settings().loadRadiusChunks;
        snapshot.settings.terrainHeightScale = terrain.settings().heightScale;
        snapshot.player.position = actors.position(playerActor, world).value_or(camera.state().pivot);
        snapshot.camera = camera.state();
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
        stats.position = world.position(state->object).value_or(glm::vec3{});
        stats.velocity = state->velocity;
        stats.facingRadians = state->facingRadians;
        if (const std::optional<float> groundHeight = terrain.sampleHeight(stats.position.x, stats.position.z)) {
            stats.hasGroundHeight = true;
            stats.groundHeight = *groundHeight;
        }
        return stats;
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
    const bool debugUiEnabled = Renderer::DebugUi::init(window);
    if (!debugUiEnabled) {
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
    Engine::CachedStaticMesh cachedMesh = assetCache.acquireStaticMesh("assets/models/sample_static.fbx");
    if (cachedMesh.handle.id == UINT32_MAX) {
        SDL_Log("Falling back to generated cube mesh for missing sample static mesh.");
        cachedMesh = assetCache.acquireFallbackCubeMesh();
    }
    const Renderer::StaticMeshHandle mesh = cachedMesh.handle;

    const Engine::CachedTexture redTexture = assetCache.acquireSolidTexture(220, 64, 64);
    const Engine::CachedTexture greenTexture = assetCache.acquireSolidTexture(64, 190, 110);
    const Engine::CachedTexture blueTexture = assetCache.acquireSolidTexture(70, 120, 230);
    const Engine::CachedTexture yellowTexture = assetCache.acquireSolidTexture(220, 190, 70);
    const Engine::CachedTexture cyanTexture = assetCache.acquireSolidTexture(80, 190, 210);
    const auto createSampleMaterial = [](
        const std::string& name,
        Renderer::TextureHandle baseColorTexture,
        const glm::vec4& baseColorFactor = glm::vec4{1.0f}) {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = name;
        descriptor.baseColorTexture = baseColorTexture;
        descriptor.baseColorFactor = baseColorFactor;
        descriptor.metallicFactor = 0.0f;
        descriptor.roughnessFactor = 1.0f;
        return Renderer::createMaterial(descriptor);
    };
    const Renderer::MaterialHandle redMaterial = createSampleMaterial("sample.red", redTexture.handle);
    const Renderer::MaterialHandle greenMaterial = createSampleMaterial("sample.green", greenTexture.handle);
    const Renderer::MaterialHandle blueMaterial = createSampleMaterial("sample.blue", blueTexture.handle);
    const Renderer::MaterialHandle yellowMaterial = createSampleMaterial("sample.yellow", yellowTexture.handle);
    const Renderer::MaterialHandle cyanMaterial = createSampleMaterial("sample.cyan", cyanTexture.handle);
    const std::array chunkMaterials = {
        redMaterial,
        greenMaterial,
        blueMaterial,
        yellowMaterial,
        cyanMaterial,
    };
    Renderer::DebugUi::RendererDebugSettings debugSettings;
    Renderer::DebugUi::WorldSaveDebugControls worldSaveControls;

    Engine::World world;
    Engine::ActorController actors;
    Engine::SpatialRegistry spatialRegistry({16.0f, true});
    Engine::TerrainSystem terrain({16.0f, 33, 1.25f});
    Engine::ChunkStreamer chunkStreamer({16.0f, 1});
    const Engine::ProceduralChunkContentConfig chunkContentConfig =
        Engine::ProceduralChunkContentConfig::sampleOpenWorldConfig();
    const Engine::ChunkContentFactory chunkFactory =
        [mesh, chunkMaterials, cyanMaterial, &debugSettings, &spatialRegistry, &chunkContentConfig](
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
            content.terrain = targetTerrain.createTile(coord, cyanMaterial);
            Renderer::setTerrainMaterial(targetTerrain.rendererTerrain(content.terrain), cyanMaterial);
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

            for (const Engine::ProceduralPropSpawn& spawn : propSpawns) {
                const Engine::WorldObjectHandle object = targetWorld.createObject();
                const Renderer::MeshInstanceHandle instance = Renderer::createInstance(mesh);
                const uint32_t materialIndex = chunkMaterials.empty()
                    ? 0u
                    : spawn.materialSlot % static_cast<uint32_t>(chunkMaterials.size());

                Renderer::setInstanceMaterial(instance, chunkMaterials[materialIndex]);
                Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
                Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
                Renderer::setInstanceMaxDrawDistance(instance, debugSettings.propMaxDrawDistance);
                Renderer::setInstanceRenderGroup(instance, content.renderGroup);
                targetWorld.attachRendererInstance(object, instance);
                glm::vec3 position = chunkOrigin + spawn.localPosition;
                position.y = targetTerrain.sampleHeight(position.x, position.z).value_or(0.0f) + spawn.terrainYOffset;
                targetWorld.setPosition(object, position);
                targetWorld.setScale(object, spawn.scale);
                targetWorld.setAngularVelocity(object, spawn.angularVelocity);
                targetWorld.setLocalBounds(object, spawn.localBounds);
                spatialRegistry.insert(object, position);
                content.objects.push_back(object);
            }

            return content;
        };
    const auto applyDebugVisibilitySettings = [&]() {
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle renderGroup) {
                const Renderer::TerrainHandle rendererTerrain = terrain.rendererTerrain(terrainTile);
                Renderer::setTerrainMaterial(rendererTerrain, cyanMaterial);
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
    };

    bool running = true;
    Engine::FixedStepLoop loop;
    Engine::InputState input;
    Engine::EventQueue events;
    Engine::InputMappingLoadResult inputMappingLoad = Engine::InputMapping::loadFromYaml("assets/config/input.yaml");
    if (!inputMappingLoad.success) {
        SDL_Log("Using default input mapping: %s", inputMappingLoad.error.c_str());
    }
    Engine::InputMapping inputMapping = inputMappingLoad.mapping;

    Engine::CameraSettings cameraSettings;
    cameraSettings.minPivotXZ = {-50.0f, -50.0f};
    cameraSettings.maxPivotXZ = {50.0f, 50.0f};
    Engine::CameraState cameraState;
    cameraState.pivot = {0.0f, 0.0f, 0.0f};
    cameraState.yawRadians = 0.0f;
    cameraState.pitchRadians = glm::radians(-40.0f);
    cameraState.distance = 14.0f;
    Engine::OrbitCameraController camera(cameraSettings, cameraState);
    Engine::DebugSelectionState debugSelection;
    chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
    terrain.updateLods(camera.position());
    applyDebugVisibilitySettings();

    const Engine::WorldObjectHandle playerObject = world.createObject();
    const Renderer::MeshInstanceHandle playerInstance = Renderer::createInstance(mesh);
    Renderer::setInstanceMaterial(playerInstance, yellowMaterial);
    Renderer::setInstanceRenderLayer(playerInstance, Renderer::RenderLayer::Props);
    Renderer::setInstanceVisibilityFlags(playerInstance, Renderer::VisibilityFlags::Visible);
    Renderer::setInstanceMaxDrawDistance(playerInstance, 0.0f);
    world.attachRendererInstance(playerObject, playerInstance);
    world.setScale(playerObject, {0.55f, 1.15f, 0.55f});
    world.setLocalBounds(playerObject, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
    const Engine::ActorHandle playerActor = actors.createActor(playerObject, {6.0f, 1.15f, 20.0f});
    actors.setPosition(playerActor, {0.0f, 0.0f, 0.0f}, &terrain, &world);
    if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
        spatialRegistry.insert(playerObject, *playerPosition);
    }
    world.syncRenderState();

    while (running) {
        loop.beginFrame();
        input.beginFrame();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (debugUiEnabled) {
                Renderer::DebugUi::processEvent(event);
            }
            input.processEvent(event);
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
        inputMapping.publishEvents(input, events);
        camera.update(events, loop.frameDeltaSeconds());
        chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
        terrain.updateLods(camera.position());
        applyDebugVisibilitySettings();
        Renderer::setAtmosphereSettings(atmosphere);

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

        while (loop.shouldRunFixedUpdate()) {
            world.fixedUpdate(loop.fixedDeltaSeconds());
            actors.fixedUpdate(playerActor, events, terrain, world, loop.fixedDeltaSeconds());
            updateSpatialRegistryForLoadedObjects();
            if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
                spatialRegistry.update(playerObject, *playerPosition);
            }
            loop.consumeFixedUpdate();
        }
        world.syncRenderState();

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
        if (debugSelection.selectedObject && !world.isValid(debugSelection.selectedObject->object)) {
            debugSelection.selectedObject = std::nullopt;
        }
        if (input.wasMouseButtonPressed(Engine::MouseButton::Left)) {
            if (debugSelection.hoveredObject) {
                debugSelection.selectedObject = debugSelection.hoveredObject;
            } else if (debugSelection.terrainHit) {
                debugSelection.selectedObject = std::nullopt;
            }
        }

        const Renderer::SceneDrawStats drawStats = Renderer::drawScene(renderView);
        if (debugUiEnabled) {
            Renderer::DebugUi::TerrainLodDebugStats terrainLods;
            terrainLods.counts = terrain.lodCounts();
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
            const Renderer::DebugUi::DebugPickingStats pickingStats = makeDebugPickingStats(debugSelection, world);
            const Renderer::DebugUi::PlayerActorDebugStats playerStats =
                makePlayerActorDebugStats(actors, playerActor, world, terrain);
            Renderer::DebugUi::showRendererDebug(
                drawStats,
                debugSettings,
                atmosphere,
                terrainLods,
                spatialStats,
                pickingStats,
                &worldSaveControls,
                playerStats
            );
            Renderer::DebugUi::render(1, viewportExtent(width), viewportExtent(height));
        }
        bgfx::touch(0);
        bgfx::frame();
        if (worldSaveControls.saveRequested) {
            worldSaveControls.saveRequested = false;
            std::string error;
            const Engine::WorldStateSnapshot snapshot = makeWorldStateSnapshot(actors, playerActor, world, camera, chunkStreamer, terrain);
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
                camera.setState(result.snapshot.camera);
                chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
                spatialRegistry.clear();
                chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
                terrain.updateLods(camera.position());
                actors.setPosition(playerActor, result.snapshot.player.position, &terrain, &world);
                if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
                    spatialRegistry.insert(playerObject, *playerPosition);
                }
                applyDebugVisibilitySettings();
                world.syncRenderState();
                debugSelection = {};
                worldSaveControls.status = "Loaded world state.";
            } else {
                worldSaveControls.status = "Load failed: " + result.error;
            }
        }
        events.clear();
    }

    chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
    spatialRegistry.clear();
    world.syncRenderState();

    Renderer::destroyMaterial(redMaterial);
    Renderer::destroyMaterial(greenMaterial);
    Renderer::destroyMaterial(blueMaterial);
    Renderer::destroyMaterial(yellowMaterial);
    Renderer::destroyMaterial(cyanMaterial);

    assetCache.release(redTexture);
    assetCache.release(greenTexture);
    assetCache.release(blueTexture);
    assetCache.release(yellowTexture);
    assetCache.release(cyanTexture);
    assetCache.release(cachedMesh);
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
