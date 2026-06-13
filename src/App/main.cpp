#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/AssetCache.hpp"
#include "Engine/ChunkStreamer.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/FixedStepLoop.hpp"
#include "Engine/InputMapping.hpp"
#include "Engine/OrbitCamera.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"
#include "Engine/input.hpp"
#include "Renderer/DebugUi.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

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

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, 1280, 720);

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
    const std::array chunkTextures = {
        redTexture.handle,
        greenTexture.handle,
        blueTexture.handle,
        yellowTexture.handle,
        cyanTexture.handle,
    };

    Engine::World world;
    Engine::TerrainSystem terrain({16.0f, 17, 1.25f});
    Engine::ChunkStreamer chunkStreamer({16.0f, 1});
    const Engine::ChunkContentFactory chunkFactory =
        [mesh, chunkTextures, cyanTexture](Engine::ChunkCoord coord, Engine::World& targetWorld, Engine::TerrainSystem& targetTerrain) {
            Engine::ChunkContent content;
            content.terrain = targetTerrain.createTile(coord, cyanTexture.handle);
            content.objects.reserve(4);

            const float chunkSize = 16.0f;
            const glm::vec3 chunkOrigin{
                static_cast<float>(coord.x) * chunkSize,
                0.0f,
                static_cast<float>(coord.z) * chunkSize,
            };

            const std::array localPositions = {
                glm::vec3{4.0f, 0.0f, 4.0f},
                glm::vec3{12.0f, 0.0f, 4.0f},
                glm::vec3{4.0f, 0.0f, 12.0f},
                glm::vec3{12.0f, 0.0f, 12.0f},
            };

            for (uint32_t index = 0; index < localPositions.size(); ++index) {
                const Engine::WorldObjectHandle object = targetWorld.createObject();
                const Renderer::MeshInstanceHandle instance = Renderer::createInstance(mesh);
                const int32_t textureCount = static_cast<int32_t>(chunkTextures.size());
                const int32_t textureSeed = coord.x * 13 + coord.z * 7 + static_cast<int32_t>(index);
                const int32_t textureIndex = ((textureSeed % textureCount) + textureCount) % textureCount;

                Renderer::setInstanceBaseColorTexture(instance, chunkTextures[static_cast<uint32_t>(textureIndex)]);
                targetWorld.attachRendererInstance(object, instance);
                glm::vec3 position = chunkOrigin + localPositions[index];
                position.y = targetTerrain.sampleHeight(position.x, position.z).value_or(0.0f) + 0.65f;
                targetWorld.setPosition(object, position);
                targetWorld.setScale(object, {0.65f, 0.65f, 0.65f});
                targetWorld.setAngularVelocity(object, {0.0f, 0.25f + 0.05f * static_cast<float>(index), 0.0f});
                content.objects.push_back(object);
            }

            return content;
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
    chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory);
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

        int width = 1280;
        int height = 720;
        SDL_GetWindowSize(window, &width, &height);
        input.setViewportSize(width, height);
        if (debugUiEnabled) {
            Renderer::DebugUi::beginFrame();
        }
        inputMapping.publishEvents(input, events);
        camera.update(events, loop.frameDeltaSeconds());
        chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory);

        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));

        const float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
        const Engine::CameraMatrices cameraMatrices = camera.matrices(aspect);
        bgfx::setViewTransform(0, &cameraMatrices.view, &cameraMatrices.projection);
        const Renderer::RenderView renderView{
            0,
            cameraMatrices.view,
            cameraMatrices.projection,
            cameraMatrices.projection * cameraMatrices.view,
            camera.position(),
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
        };

        while (loop.shouldRunFixedUpdate()) {
            world.fixedUpdate(loop.fixedDeltaSeconds());
            loop.consumeFixedUpdate();
        }
        world.syncRenderState();

        const Renderer::SceneDrawStats drawStats = Renderer::drawScene(renderView);
        if (debugUiEnabled) {
            Renderer::DebugUi::showRendererStats(drawStats);
            Renderer::DebugUi::render(1, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        }
        bgfx::touch(0);
        bgfx::frame();
        events.clear();
    }

    chunkStreamer.unloadAll(world, terrain);
    world.syncRenderState();

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
