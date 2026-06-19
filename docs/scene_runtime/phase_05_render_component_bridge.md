# Phase 5 Plan: Render Component Bridge

## Summary

Add a renderer bridge that lets `Engine::Scene` actors own renderer-facing component metadata and synchronize that metadata to existing `Renderer` handles through an explicit Engine-owned bridge system.

This phase is an adapter layer only. It does not replace `Renderer::Scene`, rewrite `AssetCache`, migrate `AuthoredScene` or `AnimatedModel`, add serialization, add editor UI, add async loading, add renderer-side knowledge of scene storage, or convert procedural `World` objects.

## Goals

- Let scene actors drive static mesh instances, lights, and camera view data through public Engine APIs.
- Keep renderer handles owned by Renderer and acquired/released only through explicit bridge synchronization.
- Keep asset identity separate from renderer resource handles by allowing component references to use `AssetHandle`/`AssetId` where practical and compatibility renderer handles where needed.
- Use scene transforms as the source of instance/light/camera world placement.
- Register a scene system that syncs renderer state during the scheduler's `PreRender` phase after `Scene::updateWorldTransforms()`.
- Preserve existing `World`, `AuthoredScene`, `PartitionedAuthoredScene`, `AnimatedModel`, `AssetCache`, renderer submission, render groups, render layers, materials, and debug stats behavior.
- Add renderer-stub tests for bridge behavior without linking bgfx, App, Navigation, Physics, scripting, or optional large assets.

## Non-Goals

- No authored-scene adapter or animated-model adapter migration.
- No automatic import, async asset load, hot reload, or file watching.
- No serialization of render components or asset references.
- No physics, navigation, scripting, reflection, names, tags, editor inspectors, or debug UI.
- No renderer API redesign and no direct renderer dependency inside generic scene storage.
- No terrain component. Terrain remains owned by `TerrainSystem` in this phase.
- No skeletal animation sampling. Skinned rendering can define bridge-compatible metadata, but active animator/skeleton ownership is Phase 7.

## Ownership Model

- `Engine::Scene` owns actor lifetime, transforms, component records, and scheduler order.
- `Engine::SceneRenderBridge` owns the mapping between scene actors/components and live renderer handles.
- `Renderer` owns GPU/resource handles:
  - `Renderer::StaticMeshHandle`
  - `Renderer::SkinnedMeshHandle`
  - `Renderer::MeshInstanceHandle`
  - `Renderer::SkinnedMeshInstanceHandle`
  - `Renderer::MaterialHandle`
  - `Renderer::LightHandle`
  - `Renderer::RenderGroupHandle`
- `Engine::AssetCache` remains responsible for reusable renderer mesh/texture acquisition and release.
- `Engine::AssetRegistry` remains CPU metadata/identity only. It can identify source assets, but it does not create renderer resources.

The bridge may store renderer handles in bridge-private records, not in generic scene component storage. Public component APIs should return scene handles or bridge component handles, not renderer handles, unless an explicit diagnostic method exposes them for tests/debugging.

## Proposed Files

- `src/Engine/Scene/SceneRenderBridge.hpp`
- `src/Engine/Scene/SceneRenderBridge.cpp`
- `tests/scene/SceneRenderBridgeTestRunner.cpp`
- optional `tests/scene/RendererBridgeStubs.cpp` if link-time renderer stubs are clearer than an interface adapter.

Keep the existing `Scene.hpp`/`Scene.cpp` changes narrow. If Phase 1 metadata-only components are sufficient, the bridge can maintain typed records outside `Scene` and use `SceneComponentHandle` only for attachment identity. If typed component storage is needed, add only the minimum scene API required to attach/query bridge-owned component types.

## Public Types

Add Engine-side render component descriptors that mirror renderer-facing data without exposing storage internals.

```cpp
namespace Engine {
    struct SceneMeshComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneLightComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneCameraComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneMaterialReference {
        AssetId assetId;
        AssetHandle asset;
        Renderer::MaterialHandle rendererMaterial;
    };

    struct SceneStaticMeshReference {
        AssetId assetId;
        AssetHandle asset;
        Renderer::StaticMeshHandle rendererMesh;
        std::filesystem::path compatibilityPath;
    };

    struct SceneSkinnedMeshReference {
        AssetId assetId;
        AssetHandle asset;
        Renderer::SkinnedMeshHandle rendererMesh;
    };
}
```

Exact naming may change during implementation, but the contracts should stay clear:

- Runtime component handles are transient and generation-counted.
- Asset IDs are stable metadata references.
- Renderer handles are renderer-owned and must be treated as live-resource references only.
- Compatibility paths are allowed only to keep current `AssetCache::acquireStaticMesh(path)` flows working before registry-backed resource loading exists.

## Static Mesh Component

`SceneMeshComponentDescriptor` should include:

- owner actor
- static mesh reference
- optional material override reference
- render visibility:
  - `Renderer::RenderLayer`
  - `Renderer::VisibilityFlags`
  - max draw distance
- optional render group reference
- bounds policy:
  - renderer/default mesh bounds for this phase
  - optional explicit local bounds only if needed by tests
- enabled flag

Behavior:

- Attaching a mesh component to an invalid, stale, pending-destroy, or flushed actor fails.
- The component creates no renderer instance until the bridge is connected to a renderer/cache context and synced.
- On sync, the bridge creates one `Renderer::MeshInstanceHandle` for each enabled component with a valid mesh resource.
- The instance transform is set from `Scene::worldMatrix(actor)`.
- Visibility, material override, render group, render layer, and max draw distance are applied deterministically.
- Disabling or removing the component destroys its renderer instance.
- Destroying/flushing the owner actor releases the component's renderer instance.
- Updating mesh/material/visibility marks the component dirty without touching unrelated components.

## Skinned Mesh Component

Phase 5 should define the bridge surface for skinned mesh instances but keep animation ownership out of scope.

Descriptor fields:

- owner actor
- skinned mesh reference
- optional material override if renderer supports it later; current `Renderer::SkinnedMeshInstanceHandle` does not expose material override APIs, so this may remain metadata-only.
- render visibility:
  - layer
  - max draw distance
  - optional render group
- optional joint matrix palette supplied directly by caller for bridge tests/tools.
- enabled flag

Behavior:

- The bridge creates/destroys `Renderer::SkinnedMeshInstanceHandle` explicitly.
- The actor world matrix drives `Renderer::setSkinnedInstanceTransform`.
- If a joint palette is supplied, the bridge calls `Renderer::setSkinnedInstanceJointMatrices`.
- Animator, skeleton, clip sampling, skin binding, and palette generation remain Phase 7.
- If implementation cannot support useful skinned behavior without Phase 7 state, keep a typed descriptor and tests for create/destroy/transform sync only.

## Light Component

`SceneLightComponentDescriptor` should include:

- owner actor
- renderer light type:
  - directional
  - point
  - spot
- color
- intensity
- range
- inner/outer cone angles
- enabled flag
- optional name for diagnostics

Behavior:

- The owner actor's world transform drives light position and direction.
- Point and spot lights use actor world translation as `Renderer::LightDescriptor::position`.
- Directional and spot lights derive direction from actor orientation. Define the forward axis explicitly in the plan/implementation, preferably scene local `-Z` to match common camera/light convention unless current renderer usage requires another axis.
- Updating descriptor fields marks only that light dirty.
- Disabling or removing a light component destroys or disables the renderer light according to the simplest renderer contract. Prefer destroying on removal and using descriptor `enabled=false` for disabled live components.
- Bridge sync preserves renderer forward-light budget behavior and relies on existing `Renderer::SceneDrawStats` diagnostics.

## Camera Component

Camera components should not take over the App camera in this phase. They provide scene-owned camera metadata and a way to build a renderer view when App chooses to consume it.

Descriptor fields:

- owner actor
- projection mode:
  - perspective
  - orthographic, optional if not needed yet
- vertical field of view
- orthographic height
- near/far planes
- viewport size or aspect source
- layer mask
- distance-culling toggle
- enabled/primary flags

Behavior:

- The bridge can query active cameras deterministically in actor/component slot order.
- `buildRenderView(camera, viewId, viewport)` returns a `Renderer::RenderView` using the actor world transform and projection settings.
- Camera components do not call bgfx view setup directly. App remains responsible for bgfx view rect/reset and deciding which camera to render.
- Existing `OrbitCameraController` and App camera path remain unchanged.

## Bridge System API Shape

```cpp
namespace Engine {
    struct SceneRenderBridgeContext {
        AssetCache* assetCache = nullptr;
        AssetRegistry* assetRegistry = nullptr;
    };

    struct SceneRenderBridgeDiagnostics {
        uint32_t meshComponentCount = 0;
        uint32_t liveMeshInstanceCount = 0;
        uint32_t skinnedMeshComponentCount = 0;
        uint32_t liveSkinnedInstanceCount = 0;
        uint32_t lightComponentCount = 0;
        uint32_t liveLightCount = 0;
        uint32_t cameraComponentCount = 0;
        uint32_t dirtyTransformCount = 0;
        uint32_t dirtyResourceCount = 0;
        uint32_t missingAssetCount = 0;
        uint32_t rendererCreateFailureCount = 0;
        uint32_t rendererDestroyCount = 0;
        std::vector<std::string> warnings;
    };

    class SceneRenderBridge {
    public:
        explicit SceneRenderBridge(Scene& scene);
        ~SceneRenderBridge();

        SceneMeshComponentHandle attachMesh(SceneActorHandle actor, const SceneMeshComponentDescriptor& descriptor);
        bool detachMesh(SceneMeshComponentHandle component);
        bool setMeshDescriptor(SceneMeshComponentHandle component, const SceneMeshComponentDescriptor& descriptor);

        SceneLightComponentHandle attachLight(SceneActorHandle actor, const SceneLightComponentDescriptor& descriptor);
        bool detachLight(SceneLightComponentHandle component);
        bool setLightDescriptor(SceneLightComponentHandle component, const SceneLightComponentDescriptor& descriptor);

        SceneCameraComponentHandle attachCamera(SceneActorHandle actor, const SceneCameraComponentDescriptor& descriptor);
        bool detachCamera(SceneCameraComponentHandle component);
        bool setCameraDescriptor(SceneCameraComponentHandle component, const SceneCameraComponentDescriptor& descriptor);

        void sync(const SceneRenderBridgeContext& context);
        void releaseRendererResources();
        SceneRenderBridgeDiagnostics diagnostics() const;
    };
}
```

Implementation may use one handle type plus component kind if that is simpler, but typed handles make the public API harder to misuse.

## Scheduler Integration

- The bridge should register one scene system while the scene is `Unloaded` or `Loaded`.
- The system runs during `SceneTickPhase::PreRender`.
- `Scene::tickFrame` and explicit `tickPhase(PreRender)` already refresh transforms before callbacks; the bridge should assume world matrices are current but may call `updateWorldTransforms()` defensively only if needed.
- Registration/unregistration must follow Phase 3 scheduler rules. If the bridge is constructed after scene start, it should fail clearly rather than mutating scheduler state illegally.
- The bridge should also expose manual `sync(...)` for tests and App composition that does not yet use the scene scheduler.

## Dirty Tracking

Track dirty state inside bridge records:

- transform dirty
- resource dirty
- material dirty
- visibility dirty
- light descriptor dirty
- camera descriptor dirty
- pending destroy

Dirty rules:

- Transform changes should be detected by comparing actor world matrices or by an explicit bridge `markActorTransformDirty` helper if scene dirty notifications are added.
- Component descriptor updates mark only relevant dirty bits.
- Actor destroy/pending-destroy makes components non-renderable immediately.
- `flushDestroyedActors` cleanup can be handled by bridge sync scanning for invalid owners and releasing renderer resources.
- Full bridge sync remains acceptable in Phase 5 if the test surface is small, but the public diagnostics should distinguish full scans from actual renderer updates.

## Asset And Resource Resolution

Phase 5 should support two practical resource paths:

1. Direct renderer handle path for tests and transitional callers.
2. Compatibility path through `AssetCache` for static meshes/textures where a source path is available.

Rules:

- If a direct renderer mesh/material handle is provided, use it without taking ownership.
- If a static mesh source path is provided and `AssetCache` is available, acquire a cached mesh and release it when the component no longer needs it.
- If an `AssetHandle`/`AssetId` is provided but no resource resolver exists, report a missing-resource diagnostic and do not create a renderer instance.
- Do not run Assimp imports directly from the render bridge unless going through existing `AssetCache` compatibility APIs.
- Do not create or destroy renderer materials from asset metadata in this phase unless the component descriptor already carries a valid `Renderer::MaterialHandle`.

Future phases can replace compatibility paths with an explicit registry-backed resource resolver after the authored-scene and animation adapters prove the shape.

## Lifetime And Shutdown

- Bridge destruction should call `releaseRendererResources()` if resources are still live.
- `releaseRendererResources()` must destroy instances/lights created by the bridge and release any `AssetCache` acquisitions owned by bridge records.
- Shutdown order:
  1. stop scene or stop bridge sync
  2. release bridge renderer resources
  3. release cache-owned assets
  4. shut down Renderer
- Double release and stale component handles are clean no-ops.
- Renderer resource release must be deterministic in component slot order.

## Renderer Boundary Options

To keep tests independent from bgfx, use one of these implementation approaches:

- Preferred: introduce a small `SceneRendererFacade` interface in Engine with methods that mirror only the renderer calls the bridge needs. Production facade forwards to `Renderer::*`; tests use a fake facade.
- Acceptable: link tests against local renderer stubs that provide the required `Renderer::*` symbols without bgfx.

Do not link `manual_engine_scene_render_bridge_tests` against Renderer, bgfx, App, Navigation, Physics, or scripting.

## Diagnostics

Diagnostics should include:

- component counts by type
- live renderer handle counts by type
- resource acquire/release counts
- missing asset/resource count
- invalid owner count
- dirty update counts
- renderer create/destroy failure counts if renderer/facade can report failure
- last warnings/messages

Diagnostics are plain Engine data. ImGui display can be added later through existing App/Renderer debug composition.

## Build Integration

- Add `SceneRenderBridge.cpp` to `manual_engine`.
- Add `manual_engine_scene_render_bridge_tests` with:
  - `tests/scene/SceneRenderBridgeTestRunner.cpp`
  - `src/Engine/Scene/Scene.cpp`
  - `src/Engine/Scene/SceneRenderBridge.cpp`
  - `src/Engine/AssetRegistry.cpp` only if tests cover asset IDs/handles.
  - renderer facade fake or renderer stubs.
- Link only CPU dependencies needed by the target, likely `glm::glm`.
- Do not link Renderer, bgfx, SDL, App, Navigation, Physics, scripting, or optional heavy asset fixtures.

## Test Plan

- `AttachMeshRejectsInvalidActor`: invalid/stale/pending-destroy actors cannot receive mesh components.
- `MeshSyncCreatesInstance`: enabled mesh component creates one renderer instance on sync.
- `MeshTransformSyncUsesSceneWorldMatrix`: parent/child hierarchy world transform is pushed to the renderer instance.
- `MeshDescriptorUpdatesDirtyOnlyAffectedInstance`: material, visibility, layer, max distance, and render group updates affect only the component being changed.
- `MeshRemovalDestroysInstance`: detaching a mesh component destroys its renderer instance and invalidates stale component handles.
- `ActorDestroyReleasesMeshInstance`: destroying/flushing an owner actor releases associated renderer resources.
- `AssetCacheStaticMeshAcquireRelease`: source-path mesh components acquire and release `AssetCache` meshes exactly once per live component lifetime.
- `MissingAssetDoesNotCreateInstance`: unresolved `AssetHandle`/`AssetId` reports diagnostics and creates no renderer instance.
- `LightSyncCreatesAndUpdatesDescriptor`: light component creates a renderer light and updates position/direction from actor transform.
- `LightDisableAndRemoval`: disabled light updates renderer descriptor or releases renderer light according to chosen behavior.
- `CameraBuildRenderView`: camera component builds deterministic view/projection data without calling bgfx setup.
- `PreRenderSystemSyncsAfterTransformRefresh`: scheduler-driven bridge sync observes latest world matrices during `PreRender`.
- `SkinnedComponentCreatesInstanceAndPalette`: skinned component creates a skinned instance and submits caller-provided joint matrices.
- `ShutdownReleasesAllResources`: bridge shutdown releases all owned renderer/cache resources in deterministic order.
- `TestTargetStaysIndependent`: CMake target remains independent from Renderer, bgfx, SDL, App, Navigation, Physics, and scripting.

## Implementation Sequence

1. Add bridge descriptors, typed handles, diagnostics, and fake renderer/facade test harness.
2. Implement static mesh component attach/update/detach and manual sync.
3. Add transform sync from scene world matrices.
4. Add renderer resource lifetime and shutdown behavior.
5. Add light component support.
6. Add camera component query/build-render-view support.
7. Add minimal skinned component support with direct joint palette input.
8. Add scheduler `PreRender` registration helper.
9. Wire CMake and tests.
10. Update system contracts, engine overview, and work log.

## Acceptance Criteria

- Scene actors can own mesh, light, and camera component records.
- Bridge sync creates, updates, and destroys renderer-facing resources deterministically.
- Renderer does not read scene actor/component storage directly.
- Existing `World`, `AuthoredScene`, `PartitionedAuthoredScene`, `AnimatedModel`, `AssetCache`, and renderer behavior remain unchanged.
- Tests prove transform sync, resource lifetime, invalid handle behavior, scheduler `PreRender` ordering, and target independence.
- Full build, full CTest, and `git diff --check` pass.

## Out Of Scope

- Authored scene conversion to scene actors/components.
- Animated model conversion, skeleton assets, animator state, or clip sampling.
- Renderer material/texture asset resolver beyond explicit handles or existing `AssetCache` compatibility paths.
- Serialization of render components.
- Editor UI, debug scene browser, or reflection.
- Async resource loading, streaming, file watching, or hot reload.
- Physics, navigation, scripting, terrain components, or procedural `World` migration.

## Assumptions

- `Scene::PreRender` remains the correct sync point because Phase 3 refreshes transforms immediately before `PreRender` callbacks.
- `AssetCache` is available only when App/composition chooses to provide it; direct renderer handles keep tests and transitional call sites simple.
- Bridge-owned records can live outside generic scene storage for Phase 5, as long as owner actor validity and scene component identity remain explicit.
- Future authored-scene and animated-model adapters will consume this bridge rather than bypassing it with app-local renderer instance management.
