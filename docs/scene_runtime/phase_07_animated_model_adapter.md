# Phase 7 Plan: Animated Model Adapter

## Summary

Add a scene-runtime adapter for the existing animated model pipeline. The adapter consumes already-imported or cached animated CPU data, creates scene actors for animated nodes, preserves skeleton and mesh bindings, owns animation component state, and drives `SceneRenderBridge` skinned mesh components with sampled joint palettes.

This phase does not replace `Engine::AnimatedModel`, animated model cache files, async animated import/cache jobs, renderer skinned mesh resources, skinned shaders, or the current App animated sample path in one step. It creates a parallel scene-component path that proves animated characters can live as scene actors and participate in scene transform, scheduler, and render bridge ownership.

## Goals

- Convert skeletal/animated `Assets::Assimp::ImportedScene` data into scene actor hierarchy and animation bindings.
- Preserve existing animated model import, cache, diagnostics, pose evaluation, crossfade, and renderer skinned mesh behavior.
- Add scene-facing skeleton, animator, and skinned mesh component records without making Renderer read scene storage.
- Drive skinned renderer instances through `SceneRenderBridge` using scene world transforms and sampled joint matrices.
- Keep animation evaluation deterministic, CPU-side, and testable without App, bgfx, navigation, physics, scripting, or optional large assets.
- Provide plain diagnostics for imported skeleton/skin/clip data, component creation, pose sampling, palette upload, invalid references, and skipped unsupported records.

## Non-Goals

- No animation graph, state machine, blend tree, IK, retargeting, additive animation, morph targets, ragdoll, cloth, animation compression, GPU compute skinning, or editor timeline.
- No replacement of `AnimatedModel`, `AnimatedModelCache`, `AnimatedModelAsync`, or current debug App animated sample flow.
- No static authored scene migration beyond using shared scene actor hierarchy and render bridge concepts.
- No asset registry-backed resource resolution yet; direct renderer handles and existing cache/import payloads remain the active resource path.
- No serialization of skeletons, clips, animator state, or component bindings.
- No Lua/native behavior hooks beyond scheduler integration needed to tick animator systems.
- No physics, navigation, or gameplay character movement coupling.
- No required optional KayKit/Sponza assets in normal tests.

## Proposed Files

- `src/Engine/Scene/AnimatedSceneAdapter.hpp`
- `src/Engine/Scene/AnimatedSceneAdapter.cpp`
- `tests/scene/AnimatedSceneAdapterTestRunner.cpp`

Add the adapter source to `manual_engine`.

Add a `manual_engine_animated_scene_adapter_tests` target using only CPU/resource dependencies:

- `tests/scene/AnimatedSceneAdapterTestRunner.cpp`
- existing renderer ownership stubs or focused skinned-renderer stubs
- `src/Assets/Assimp/Importer.cpp`
- `src/Engine/AssetCache.cpp`
- `src/Engine/ImportedSceneResources.cpp`
- `src/Engine/AnimatedModel.cpp`
- `src/Engine/AnimatedModelCache.cpp` if cache payload fixtures are covered
- `src/Engine/Scene/Scene.cpp`
- `src/Engine/Scene/SceneRenderBridge.cpp`
- `src/Engine/Scene/AnimatedSceneAdapter.cpp`

The test target must not link App, Navigation, Physics, scripting, bgfx, or optional heavy assets.

## Public API Shape

Exact names may change during implementation, but the adapter should expose these concepts:

```cpp
namespace Engine {
    struct SceneSkeletonHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneAnimatorHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneAnimatedNodeBinding {
        uint32_t importedNodeIndex = UINT32_MAX;
        SceneActorHandle actor;
    };

    struct SceneSkeletonBinding {
        uint32_t importedSkinIndex = UINT32_MAX;
        SceneSkeletonHandle skeleton;
        std::vector<SceneActorHandle> jointActors;
        std::vector<uint32_t> importedJointIndices;
    };

    struct SceneAnimatorState {
        uint32_t clipIndex = 0;
        float timeSeconds = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
        AnimationCrossfadeState crossfade;
    };

    struct SceneAnimatedMeshBinding {
        uint32_t importedNodeIndex = UINT32_MAX;
        uint32_t importedMeshIndex = UINT32_MAX;
        uint32_t importedSkinIndex = UINT32_MAX;
        SceneSkinnedMeshComponentHandle component;
    };

    struct SceneAnimatedResourceBinding {
        std::vector<Renderer::SkinnedMeshHandle> skinnedMeshes;
        std::vector<Renderer::StaticMeshHandle> bindPoseMeshes;
        std::vector<Renderer::MaterialHandle> materials;
        std::vector<CachedTexture> textures;
    };

    struct SceneAnimatedAdapterSettings {
        bool loadTextures = true;
        bool createBindPoseMeshes = false;
        bool createSkinnedMeshes = true;
        Renderer::RenderLayer renderLayer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
        uint32_t defaultClipIndex = 0;
        bool playOnStart = true;
        bool loop = true;
        float playbackSpeed = 1.0f;
        std::string materialNamePrefix = "SceneAnimatedMaterial";
        std::string textureDebugNamePrefix = "SceneAnimated";
    };

    struct SceneAnimatedAdapterDiagnostics {
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedSkinCount = 0;
        uint32_t importedJointCount = 0;
        uint32_t importedAnimationCount = 0;
        uint32_t createdActorCount = 0;
        uint32_t createdSkeletonCount = 0;
        uint32_t createdAnimatorCount = 0;
        uint32_t createdSkinnedMeshCount = 0;
        uint32_t createdSkinnedMeshComponentCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t textureFallbackCount = 0;
        uint32_t invalidNodeReferenceCount = 0;
        uint32_t invalidSkinReferenceCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        uint32_t invalidJointReferenceCount = 0;
        uint32_t unsupportedInterpolationCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t overBudgetJointCount = 0;
        std::vector<std::string> warnings;
    };

    struct SceneAnimatedAdapterResult {
        bool success = false;
        std::string message;
        std::vector<SceneAnimatedNodeBinding> nodes;
        std::vector<SceneSkeletonBinding> skeletons;
        std::vector<SceneAnimatedMeshBinding> skinnedMeshes;
        SceneAnimatorHandle animator;
        SceneAnimatedResourceBinding resources;
        SceneAnimatedAdapterDiagnostics diagnostics;
    };

    SceneAnimatedAdapterResult adaptAnimatedModelToScene(
        const Assets::Assimp::ImportedScene& importedScene,
        const std::filesystem::path& sourcePath,
        AssetCache& assetCache,
        Scene& scene,
        SceneRenderBridge& renderBridge,
        const SceneAnimatedAdapterSettings& settings = {});

    bool updateSceneAnimator(
        Scene& scene,
        SceneRenderBridge& renderBridge,
        SceneAnimatorHandle animator,
        float deltaSeconds);

    void releaseSceneAnimatedAdapterResources(
        SceneAnimatedResourceBinding& resources,
        AssetCache& assetCache);
}
```

The adapter should expose:

- deterministic imported-node-to-actor bindings;
- skeleton and joint actor bindings;
- animator state that can be queried and mutated through narrow APIs;
- skinned mesh component bindings;
- resource bindings that can be released explicitly;
- diagnostics that explain skipped, invalid, or clamped animation data.

## Data Flow

1. Caller imports CPU animated data with `Assets::Assimp::importScene`, reads an `AnimatedModelCachePayload`, or reuses existing async import results.
2. Caller creates `Engine::Scene` and `SceneRenderBridge`.
3. Adapter validates that the imported scene contains skeletal or animation data.
4. Adapter creates one scene actor per imported node in node order, including empty skeleton hierarchy nodes.
5. Adapter applies imported local transforms to scene actors and recreates parent/child links with keep-local behavior.
6. Adapter creates material and texture resources through existing imported-scene resource helpers and `AssetCache`.
7. Adapter creates renderer skinned mesh resources from imported skinned mesh primitives, preserving material references and skin indices.
8. Adapter creates skeleton bindings that map imported joints to scene actor handles and inverse bind matrices.
9. Adapter creates an animator record with default playback state and clip selection.
10. Adapter attaches `SceneSkinnedMeshComponentDescriptor` records through `SceneRenderBridge`, initially with bind-pose or first sampled joint matrices.
11. During `VariableAnimation` or explicit update, animator state advances, samples a pose, and updates bridge skinned mesh descriptors with joint matrices.
12. During `PreRender`, `SceneRenderBridge` syncs skinned instances and joint palettes to the renderer backend.
13. Caller releases bridge-created renderer instances first, then adapter-created skinned meshes/materials/textures.

## Component Model

Phase 7 should keep animated component storage in the adapter/bridge layer instead of adding generic ECS storage:

- **Skeleton record:** immutable or rarely-mutated imported joint metadata, inverse bind matrices, joint actor mapping, and bind pose transforms.
- **Animator record:** playback state, crossfade state, current sampled pose, diagnostics, and enabled flag.
- **Skinned mesh record:** scene actor owner, renderer skinned mesh handle, skeleton reference, skin index, render settings, and bridge component handle.

These records are scene-runtime records, not serialized records. Future reflection/serialization phases can decide how to persist them.

## Transform And Skeleton Rules

- Imported node transforms are copied into scene local transforms.
- Imported skeleton joint actors are ordinary scene actors; their world matrices should match imported bind/world transforms at initial load.
- Skinning matrices are computed from sampled joint model transforms and inverse bind matrices, matching existing `AnimatedModel` math.
- Actor world transform drives skinned instance placement through `SceneRenderBridge`.
- Joint palette matrices are local to the skinned component and should not require Renderer to know about scene hierarchy.
- If an animation channel targets a node outside the skeleton, apply it to the corresponding scene actor when safe, or diagnose and skip if the node mapping is invalid.
- Root motion extraction is deferred. Animation sampling may update joint actor local transforms, but actor locomotion remains outside this phase.

## Animation Playback Rules

- Animator state mirrors existing `AnimationPlaybackState`: clip, time, speed, loop, and playing.
- Advancing playback must be deterministic for a given delta.
- Paused scene scheduler or disabled animator state must not advance time.
- Crossfade should reuse existing `AnimationCrossfadeState`, `beginCrossfade`, `advanceCrossfade`, and `blendSkeletonPoses` semantics where possible.
- Unsupported interpolation modes should fall back exactly as current `AnimatedModel` sampling does and increment diagnostics.
- Invalid clip indices should reject mutation or fall back to a valid clip with diagnostics; avoid silent wraparound in public setter APIs.

## Scheduler Integration

- Register an optional scene system for `VariableAnimation`.
- The system should:
  1. skip when scene lifecycle is not started, disabled, or paused according to scheduler rules;
  2. advance enabled animator records;
  3. sample poses;
  4. update joint actor transforms where applicable;
  5. update `SceneRenderBridge` skinned mesh descriptors with the latest joint matrices.
- Manual `updateSceneAnimator(...)` remains available for tests and App composition.
- `PreRender` sync remains owned by `SceneRenderBridge`.
- Do not introduce async task scheduling in the scene scheduler.

## Resource Ownership

- `Scene` owns actors and hierarchy.
- Animated adapter owns renderer skinned mesh handles, optional bind-pose static mesh handles, renderer material handles, and cached texture acquisitions.
- `SceneRenderBridge` owns skinned mesh component records and live renderer skinned instances created by sync.
- `AssetCache` owns cached texture lifetime and must outlive adapter resource bindings.
- Existing `AnimatedModel` keeps its own ownership path; do not share live renderer handles between `AnimatedModel` and the scene adapter.
- Release order:
  1. release `SceneRenderBridge` renderer resources;
  2. unregister animator scheduler system if registered;
  3. destroy scene actors or scene;
  4. call `releaseSceneAnimatedAdapterResources`;
  5. release/shutdown `AssetCache`;
  6. shut down Renderer.

## Diagnostics

Diagnostics should report:

- imported node, mesh, primitive, material, texture, skin, joint, and animation counts;
- created actor, skeleton, animator, skinned mesh, component, material, and texture counts;
- texture success/failure/fallback counts;
- invalid node, joint, skin, mesh, primitive material, and animation channel references;
- unsupported interpolation fallback counts;
- over-budget joint palette and truncated influence counts;
- sampled clip/time, active crossfade state, and last pose validity;
- warnings inherited from imported scene diagnostics and generated by the adapter.

Diagnostics are plain Engine data. Debug UI wiring is optional follow-up and should not be required for this phase.

## Build Integration

- Add `AnimatedSceneAdapter.cpp` to `manual_engine`.
- Add `manual_engine_animated_scene_adapter_tests`.
- Reuse renderer ownership stubs for skinned mesh/material/texture symbols.
- Link only required CPU dependencies such as `assimp::assimp` and `glm::glm`.
- Keep the new test target independent from App, Navigation, Physics, scripting, bgfx, and optional heavy assets.
- Keep existing authored scene and animated model tests unchanged and passing.

## Test Plan

- `FixtureCreatesActorsForAllImportedNodes`: skinned animation fixture creates one scene actor per imported node.
- `FixturePreservesSkeletonHierarchy`: scene parent/child links preserve imported node and joint hierarchy.
- `FixtureCreatesSkeletonBindings`: imported skin joints map to valid scene actors and preserve inverse bind matrices.
- `FixtureCreatesSkinnedMeshComponents`: skinned mesh node references attach scene render bridge skinned components with valid skeleton/skin bindings.
- `FixtureCreatesRendererSkinnedMeshesAndMaterials`: renderer skinned mesh descriptors preserve vertices, influences, materials, and joint limits.
- `BindPoseMatchesAnimatedModelRuntime`: adapter bind pose joint matrices match existing `AnimatedModel::bindPose()` for the fixture.
- `SampledClipMatchesAnimatedModelRuntime`: adapter sampled clip palettes match existing `AnimatedModel::sampleClip()` at several times.
- `AnimatorAdvancesPlayback`: animator state advances clip time deterministically and respects speed, loop, and playing flags.
- `AnimatorCrossfadeUpdatesPalette`: crossfade state blends poses and updates skinned component joint matrices.
- `BridgeSyncCreatesSkinnedInstances`: fake backend sync creates skinned instances and receives transform/render settings/joint matrices.
- `SchedulerVariableAnimationUpdatesBeforePreRender`: registered animator system updates joint matrices before bridge pre-render sync.
- `InvalidReferencesAreDiagnosed`: synthetic invalid skins, joints, channels, mesh refs, and material refs produce diagnostics without crashing.
- `StaticSceneRejectedOrNoOps`: non-animated authored fixture fails cleanly or returns a no-op result with a clear message.
- `AsyncCachePayloadCanCommitThroughAdapter`: existing animated cache payload can be adapted without source reimport.
- `ResourceReleaseIsDeterministic`: bridge and adapter releases destroy skinned instances, skinned meshes, materials, and textures exactly once; double release is a no-op.
- `ExistingAnimatedModelRuntimeStillWorks`: current animated model tests continue to pass unchanged.

## Implementation Sequence

1. Add adapter public types, diagnostics, and empty release helpers.
2. Implement node actor creation and hierarchy reconstruction from imported nodes.
3. Implement skeleton binding creation from imported skins and joints.
4. Implement material/texture conversion using existing helper functions.
5. Implement renderer skinned mesh creation from imported skinned primitives and influence data.
6. Attach skinned mesh components through `SceneRenderBridge`.
7. Implement animator records, playback state mutation, pose sampling, and bridge descriptor palette updates.
8. Add optional scheduler registration for `VariableAnimation`.
9. Add fixture tests with fake render backend/stubs.
10. Wire CMake and update docs/work log.

## Acceptance Criteria

- A committed skinned animation fixture can be adapted into scene actors, skeleton bindings, animator state, skinned mesh components, and bridge-synced skinned renderer instances.
- Adapter bind pose and sampled clip palettes match the existing `AnimatedModel` runtime for the same imported fixture.
- Animator playback and crossfade can update skinned component joint palettes deterministically.
- Existing `AnimatedModel`, `AnimatedModelCache`, `AnimatedModelAsync`, static authored scene, and render bridge tests remain behavior-compatible.
- No App, Navigation, Physics, scripting, serialization, state machine, IK, or optional heavy asset dependency is introduced.
- Full build, full CTest, and `git diff --check` pass.

## Assumptions

- Phase 7 consumes already-imported or cached CPU animated scene data; path loading and async import remain existing animated model responsibilities.
- Existing animated model pose math remains the source of truth for skeleton sampling, blending, and crossfade behavior.
- `SceneRenderBridge` skinned mesh descriptors are sufficient for renderer submission by direct renderer handles.
- One animator per adapted imported animated scene is enough for the first adapter pass; multi-character scene ownership can be added after the single-asset path is stable.
- Root motion, gameplay locomotion, and character movement are later phases.
- Optional FBX/KayKit validation remains disabled by default.
