# Phase 6 Plan: Authored Scene Adapter

## Summary

Add an Engine-owned adapter that converts already-imported static authored scene CPU data into `Engine::Scene` actors, transform hierarchy, and `SceneRenderBridge` components.

This phase proves that authored glTF-style scene graphs can appear as scene-runtime actors without replacing the existing eager `AuthoredScene`, `PartitionedAuthoredScene`, authored cache, async authored loading, renderer material mapping, or current App-authored runtime paths.

## Goals

- Convert `Assets::Assimp::ImportedScene` static nodes, mesh references, material mappings, and lights into scene actors and render bridge components.
- Preserve imported node local transforms and parent/child hierarchy through `Engine::Scene`.
- Reuse existing `ImportedSceneResources` material/texture mapping policy and renderer descriptor conversion.
- Reuse `SceneRenderBridge` for renderer instance/light creation instead of calling Renderer directly from the adapter.
- Keep authored adapter output inspectable through plain diagnostics and deterministic mapping tables.
- Add isolated tests against committed lightweight authored-scene fixtures, plus renderer-backend fakes where renderer handles are needed.

## Non-Goals

- No replacement of `loadAuthoredScene`, `loadPartitionedAuthoredScene`, authored scene cache files, async authored loading, or App-authored runtime composition.
- No sector streaming through scene actors yet. Partitioned authored scene runtime remains the owner for streaming in this phase.
- No skinned mesh, skeleton, animation clip, animator, or joint palette integration. Imported skeletal/animation data is diagnosed and skipped for static-scene adaptation.
- No serialization, reflection, editor UI, scripting, physics, navigation geometry extraction, occlusion, LOD, or asset hot reload.
- No new material system. Keep existing renderer material descriptors and texture acquisition helpers.
- No direct Renderer calls from the adapter except through explicit bridge/backend/resource-preparation helpers.

## Proposed Files

- `src/Engine/Scene/AuthoredSceneAdapter.hpp`
- `src/Engine/Scene/AuthoredSceneAdapter.cpp`
- `tests/scene/AuthoredSceneAdapterTestRunner.cpp`

Add the adapter source to `manual_engine`.

Add a new `manual_engine_authored_scene_adapter_tests` target that links only the required CPU/resource dependencies:

- `tests/scene/AuthoredSceneAdapterTestRunner.cpp`
- `src/Assets/Assimp/Importer.cpp`
- `src/Engine/AssetCache.cpp`
- `src/Engine/ImportedSceneResources.cpp`
- `src/Engine/Scene/Scene.cpp`
- `src/Engine/Scene/SceneRenderBridge.cpp`
- `src/Engine/Scene/AuthoredSceneAdapter.cpp`
- test fake render backend and minimal renderer stubs if `AssetCache` or material helpers require renderer symbols.

The target must not link App, Navigation, Physics, scripting, or optional heavy Sponza assets.

## Public API Shape

```cpp
namespace Engine {
    struct SceneAuthoredNodeBinding {
        uint32_t importedNodeIndex = UINT32_MAX;
        SceneActorHandle actor;
        std::vector<SceneMeshComponentHandle> meshComponents;
        std::vector<SceneLightComponentHandle> lightComponents;
    };

    struct SceneAuthoredResourceBinding {
        std::vector<Renderer::StaticMeshHandle> meshes;
        std::vector<Renderer::MaterialHandle> materials;
        std::vector<CachedTexture> textures;
    };

    struct SceneAuthoredAdapterDiagnostics {
        uint32_t importedNodeCount = 0;
        uint32_t createdActorCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t createdRendererMeshCount = 0;
        uint32_t createdMeshComponentCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t fallbackTextureCount = 0;
        uint32_t importedLightCount = 0;
        uint32_t createdLightComponentCount = 0;
        uint32_t skippedUnsupportedLightCount = 0;
        uint32_t skippedSkinnedMeshCount = 0;
        uint32_t skippedAnimationCount = 0;
        uint32_t invalidNodeReferenceCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        std::vector<std::string> warnings;
    };

    struct SceneAuthoredAdapterSettings {
        bool loadTextures = true;
        Renderer::RenderLayer renderLayer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
        std::string materialNamePrefix = "SceneAuthored";
        std::string textureDebugNamePrefix = "SceneAuthored";
    };

    struct SceneAuthoredAdapterResult {
        bool success = false;
        std::string message;
        std::vector<SceneAuthoredNodeBinding> nodes;
        SceneAuthoredResourceBinding resources;
        SceneAuthoredAdapterDiagnostics diagnostics;
    };

    SceneAuthoredAdapterResult adaptImportedSceneToScene(
        Scene& scene,
        SceneRenderBridge& renderBridge,
        const std::filesystem::path& sourcePath,
        const Assets::Assimp::ImportedScene& importedScene,
        AssetCache& assetCache,
        const SceneAuthoredAdapterSettings& settings = {});

    void releaseSceneAuthoredAdapterResources(
        SceneAuthoredResourceBinding& resources,
        AssetCache& assetCache);
}
```

Exact names may change during implementation, but the adapter must expose:

- deterministic imported-node-to-actor bindings;
- created renderer resource bindings that can be released explicitly;
- diagnostics that explain skipped/invalid imported records;
- no ownership hidden in App code.

## Data Flow

1. Caller imports CPU authored data with `Assets::Assimp::importScene` or obtains it from existing cache/payload code.
2. Caller creates an `Engine::Scene` and `SceneRenderBridge`.
3. Adapter validates `ImportedScene.success`.
4. Adapter creates one scene actor per imported node in imported node order.
5. Adapter assigns each actor the imported node local transform and recreates imported parent/child links through `Scene::attachChild(..., preserveWorldTransform = false)`.
6. Adapter creates renderer materials and textures using existing `ImportedSceneResources` helpers and `AssetCache`.
7. Adapter creates renderer static meshes from imported static mesh primitives, assigning converted material handles per primitive.
8. For each node mesh reference, adapter attaches a `SceneMeshComponentDescriptor` to that node actor using the created mesh handle, configured render layer, and max draw distance.
9. For each imported light with a valid associated node, adapter attaches a `SceneLightComponentDescriptor` to that node actor.
10. Caller syncs `SceneRenderBridge` through manual sync or registered `PreRender` system.
11. Caller releases adapter-created resources explicitly before renderer shutdown.

## Transform And Hierarchy Rules

- Imported local transforms are applied as local scene transforms.
- Imported parent/child relationships are recreated in imported node order after all actors exist.
- Adapter must use keep-local attach behavior so authored local transforms remain intact.
- Imported world transforms are not used as source of truth, but tests should compare scene-computed world matrices against imported world transforms.
- Invalid parent or child indices are counted in diagnostics and skipped without failing the whole adaptation unless the root actor set cannot be created.
- Nodes without meshes or lights still create actors so hierarchy remains complete.

## Mesh And Material Rules

- Create one `Renderer::StaticMeshHandle` per imported mesh record.
- Preserve primitive/submesh material indices through `Renderer::StaticSubmeshDescriptor::material`.
- Convert vertices through `importedSceneVertexToMeshVertex`.
- Use a fallback material when an imported primitive references an invalid material index.
- Use `acquireImportedSceneMaterialTextures` and `importedSceneMaterialDescriptor` for material conversion.
- Store all created material handles and acquired cached textures in `SceneAuthoredResourceBinding`.
- Adapter-created renderer meshes/materials are adapter resources and must be explicitly released by `releaseSceneAuthoredAdapterResources`.
- Renderer instances are not adapter resources; they are owned by `SceneRenderBridge` and released by bridge detach/shutdown.

## Light Rules

- Convert imported light type to `Renderer::LightType` where supported.
- Attach lights only when `ImportedSceneLight::nodeIndex` is present and valid.
- The light component actor transform drives renderer light position/direction through `SceneRenderBridge`.
- Zero-intensity lights remain represented but disabled or intensity-zero according to existing renderer descriptor behavior; count disabled zero-intensity lights if useful.
- Unsupported or missing-node lights are skipped with diagnostics.

## Lifetime And Ownership

- `Scene` owns actors and hierarchy.
- `SceneRenderBridge` owns render component records and renderer instances/lights created by sync.
- Adapter owns created renderer static meshes, renderer materials, and cached texture acquisitions through `SceneAuthoredResourceBinding`.
- Caller shutdown order:
  1. release/sync bridge renderer instances and lights;
  2. destroy scene actors or scene;
  3. call `releaseSceneAuthoredAdapterResources`;
  4. release/shutdown `AssetCache`;
  5. shut down Renderer.
- `SceneAuthoredAdapterResult` is a value result, but resource bindings must be moved or explicitly released once.
- Double release should be a no-op where practical.

## Diagnostics

Diagnostics should include:

- imported counts copied from `ImportedSceneDiagnostics`;
- actor/component/resource creation counts;
- texture success/failure/fallback counts;
- skipped skeletal/animation counts;
- invalid node, parent, mesh, material, and light references;
- warnings from imported scene diagnostics and texture/material mapping helpers.

Diagnostics are plain Engine data. Do not add ImGui or App UI in this phase.

## Build Integration

- Add `AuthoredSceneAdapter.cpp` to `manual_engine`.
- Add `manual_engine_authored_scene_adapter_tests`.
- Keep the adapter test target independent from App, Navigation, Physics, scripting, and optional heavy assets.
- If renderer symbols are needed by `AssetCache` or resource helper code, use local test stubs like existing authored-scene tests; do not link bgfx or full Renderer.

## Test Plan

- `FixtureCreatesActorsForAllImportedNodes`: authored fixture imports and creates one scene actor per imported node.
- `FixturePreservesHierarchyAndWorldTransforms`: scene parent/child links and computed world matrices match imported fixture nodes.
- `FixtureCreatesMeshComponentsForMeshNodes`: mesh node references create mesh components on the correct actors.
- `FixtureCreatesRendererMeshesAndMaterials`: adapter creates renderer mesh/material handles using converted submesh/material data.
- `FixtureRegistersTextureAcquisitionsAndDiagnostics`: material texture loading/fallback stats propagate into adapter diagnostics.
- `FixtureCreatesLightComponents`: imported fixture lights attach to valid node actors and preserve type/color/intensity fields.
- `BridgeSyncCreatesInstancesFromAdaptedScene`: fake backend sync creates renderer instances from adapted mesh components using scene transforms.
- `InvalidReferencesAreDiagnosed`: synthetic imported scene with invalid parent/mesh/material/light references reports diagnostics and does not crash.
- `SkinnedAndAnimatedDataSkipped`: skinned animation fixture reports skipped skin/joint/animation counts without creating skinned scene components.
- `ResourceReleaseIsDeterministic`: release helper destroys adapter-created meshes/materials and releases cached textures exactly once.
- `ExistingAuthoredSceneLoaderStillWorks`: existing authored scene tests remain unchanged and pass.

## Implementation Sequence

1. Add adapter public types and empty implementation with diagnostics helpers.
2. Implement node actor creation and hierarchy reconstruction.
3. Implement material/texture conversion using existing helper functions.
4. Implement static mesh creation and mesh component attachment.
5. Implement light component attachment.
6. Implement resource release helper.
7. Add fixture-based adapter tests with fake render backend/stubs.
8. Wire CMake.
9. Update `docs/system_contracts.md`, `docs/engine_overview.md`, and work log.

## Acceptance Criteria

- Static authored fixture can be adapted into scene actors, hierarchy, mesh components, and light components.
- Scene-computed world transforms match imported authored-scene world transforms for fixture nodes.
- Render bridge sync can create renderer-facing mesh instances from adapted components.
- Existing eager and partitioned authored scene loaders continue to compile and pass their tests unchanged.
- No App, Navigation, Physics, scripting, serialization, or partitioned streaming migration is introduced.
- Full build, full CTest, and `git diff --check` pass.

## Assumptions

- Phase 6 consumes already-imported CPU scene data; source import/cache/async ownership remains in existing authored-scene systems.
- Static authored meshes use `Renderer::StaticMeshHandle`; skinned mesh adaptation waits for Phase 7.
- Existing imported material/resource helper policy remains authoritative for texture descriptors, color spaces, sampler hints, alpha modes, and material fallbacks.
- Scene render bridge direct renderer-handle descriptors are sufficient for this adapter pass.
