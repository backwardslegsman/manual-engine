# Skinned Mesh And Animation Roadmap

This roadmap prepares ManualEngine to import, own, evaluate, and render skinned animated assets. It is intentionally separate from the authored static scene roadmap: large static scenes and animated characters share asset, material, texture, cache, and renderer infrastructure, but they need different runtime ownership and update models.

The first target should be glTF/GLB skeletal animation because its skinning and animation semantics are explicit and predictable. FBX support should come after the engine has a stable internal skeleton, skin, and clip contract.

## Goal

- Import skeletal meshes, skins, skeleton hierarchies, inverse bind matrices, and animation clips into CPU asset data.
- Keep static authored scene loading unchanged.
- Add a runtime owner for animated models with explicit renderer resource lifetime.
- Evaluate animation clips into per-instance poses.
- Render skinned meshes through a bounded renderer contract.
- Preserve clear diagnostics for unsupported animation features instead of silently dropping data.
- Use optional large/local assets, such as KayKit, for validation without making them required test data.

## Guiding Rules

- Treat animation as a separate runtime path from static authored scene sectors.
- Keep import data plain and renderer-free. Assimp/glTF details should not leak into gameplay or renderer APIs.
- Prefer glTF/GLB for required fixtures and tests.
- Use FBX as a later compatibility layer, not the first source of truth.
- Make unsupported skeletal features explicit in diagnostics.
- Keep renderer resource creation and destruction on the main/render thread.
- Keep animation evaluation deterministic and testable before adding blending, state machines, or editor tools.
- Do not require large licensed assets in the repository. Optional validation assets should remain ignored/local.
- Do not run heavy optional asset tests by default. Use `MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1` only for changes where Sponza/KayKit-scale validation meaningfully exercises the code under review.

## Current State Snapshot

The Assimp authored importer now preserves static, skeletal, skin, vertex influence, and animation clip data in CPU-side records. Static authored scene loaders deliberately reject skeletal or animated payloads at the runtime boundary, while `Engine::AnimatedModel` owns the animated path.

Implemented baseline:

- CPU skeleton, skin, joint, influence, clip, channel, and keyframe import records.
- Animated model runtime ownership for materials, textures, bind-pose preview meshes, skinned meshes, and skinned instances.
- CPU bind-pose and clip sampling, deterministic playback advancement, and pairwise crossfade blending.
- Renderer skinned mesh resources, skinned instances, joint palette upload, and skinned shader submission.
- Animated model cache, async import/cache jobs, diagnostics summaries, and profile reports.
- Optional FBX/KayKit validation paths that remain disabled for normal tests unless explicitly requested.

Remaining broad gaps:

- No general scene/component integration for animated assets yet.
- No animation graph, state machine, additive animation, retargeting, IK, or editor-facing controller.
- FBX animation behavior is best-effort through Assimp and not normalized beyond the shared internal contracts.

## Phase B1 - CPU Skeleton And Clip Import Boundary

Extend the asset import boundary so skeletal data can be preserved without creating renderer resources.

Scope:
- Add CPU-side imported animation types:
  - skeleton joints/bones;
  - parent indices and joint names;
  - inverse bind matrices;
  - bind pose/local transforms;
  - skin records and mesh-to-skin associations;
  - per-vertex joint indices and weights;
  - animation clips;
  - animation channels for translation, rotation, and scale;
  - keyframe times, values, and interpolation mode.
- Keep static authored scene import behavior intact.
- Change skinned/animated content from unconditional static-scene failure into:
  - preserved skeletal import data when structurally valid;
  - static authored scene rejection only when trying to load it through the static runtime path.
- Add diagnostics for:
  - missing inverse bind matrices;
  - vertices with no weights;
  - more than four joint influences;
  - unnormalized weights;
  - animation channels targeting missing nodes;
  - unsupported interpolation;
  - multiple skins per mesh if not yet supported.

Acceptance:
- A tiny committed glTF fixture imports a skeleton, skinned mesh, inverse bind matrices, joint weights, and one animation clip.
- Static authored scene tests continue to pass.
- Skinned assets produce structured diagnostics instead of generic import failure.
- Optional KayKit/glTF validation reports skeleton, mesh, clip, and warning counts when local assets exist.

## Phase B2 - Animated Model Runtime Ownership

Add an Engine owner for animated assets and instances without evaluating animation yet.

Scope:
- Add an `Engine::AnimatedModel` or equivalent runtime owner.
- Own imported skeleton, skins, clips, renderer mesh/material handles, texture handles, and diagnostics.
- Reuse existing material and texture mapping where possible.
- Keep static authored scene sector streaming separate.
- Add deterministic shutdown order:
  - animated instances;
  - skinned/static mesh handles;
  - materials;
  - cached textures.
- Reject unsupported imported animation features at the runtime boundary with clear diagnostics.

Acceptance:
- A tiny skinned fixture loads and unloads without renderer resource leaks.
- Runtime diagnostics expose skeleton, skin, clip, mesh, material, and texture counts.
- Static authored scene loading remains behavior-compatible.

## Phase B3 - CPU Pose Evaluation

Implement deterministic animation sampling and pose generation.

Scope:
- Add skeleton pose data:
  - local joint transforms;
  - model-space joint transforms;
  - final skinning matrices.
- Sample translation, rotation, and scale channels at a time.
- Support looping and clamped playback.
- Support linear interpolation for vectors and slerp for rotations.
- Normalize output quaternions and validate transform math.
- Add a simple `AnimationPlayer` state:
  - clip index;
  - playback time;
  - speed;
  - loop flag;
  - playing/paused.

Acceptance:
- Unit tests verify exact joint transforms for a tiny fixture at multiple times.
- Inverse bind and final skinning matrix math is covered by tests.
- Playback can advance deterministically from frame delta.

## Phase B4 - Renderer Skinned Mesh Contract

Extend renderer data structures so skinned meshes can be submitted without deciding final gameplay animation APIs.

Scope:
- Extend or add a mesh vertex format with:
  - joint indices;
  - joint weights;
  - existing authored streams: position, normal, tangent, UVs, color.
- Add skinned mesh descriptors and handles where needed.
- Add bounded joint palette metadata:
  - maximum joints per draw;
  - maximum influences per vertex;
  - fallback behavior when limits are exceeded.
- Decide whether skinned and static mesh resources share internals or use separate resource types.
- Preserve static mesh API compatibility.

Acceptance:
- Skinned mesh descriptors can be created and destroyed through renderer stubs.
- Vertex joint and weight data round-trips through tests.
- Static mesh producers do not need to populate animation fields.

## Phase B5 - Skinned Shader Path

Render skinned meshes through GPU skinning.

Scope:
- Add vertex shader skinning using joint matrix palettes.
- Add uniform or buffer upload for skinning matrices.
- Reuse the existing PBR material path after vertex skinning.
- Keep alpha, double-sided, texture descriptors, and material states compatible with the static mesh path.
- Add renderer stats for skinned draw counts and joint palette usage.

Acceptance:
- A tiny animated fixture renders without crashing in a hidden-window smoke test.
- A known animation pose visibly changes mesh vertex positions.
- Static mesh rendering is unaffected.

## Phase B6 - Animated Runtime Integration

Connect animation evaluation to app/runtime update.

Scope:
- Add per-instance animation state and pose buffers.
- Update active animation players each frame.
- Submit skinned instances with current joint matrices.
- Add basic controls for:
  - play/pause;
  - clip selection;
  - playback speed;
  - loop toggle.
- Expose animation diagnostics in Debug UI:
  - active animated models;
  - active instances;
  - current clip/time;
  - joint count;
  - skinned draw count.

Acceptance:
- A sample animated model can play in the app.
- Debug UI can inspect clip and pose state.
- Load/shutdown cycles leave zero live renderer resources.

## Phase B7 - Animation Blending And State Utilities

Add minimal animation composition after single-clip playback is stable.

Scope:
- Blend two poses by weight.
- Add crossfade between clips.
- Add optional additive pose support only if a real use case exists.
- Add a small animation controller helper, not a full gameplay state machine.
- Keep APIs simple enough for tests and sample code.

Acceptance:
- Crossfade tests verify stable transforms and normalized rotations.
- Runtime sample can transition between two clips without popping.

## Phase B8 - FBX Skeletal Compatibility

Add FBX skeletal and animation support after internal semantics are proven with glTF.

Scope:
- Map Assimp FBX skeletons, skins, and animation stacks into the same CPU contracts.
- Add diagnostics for FBX unit scale, axis metadata, pre/post rotations, animation stacks, and unsupported constraints.
- Avoid custom axis conversion until specific failures justify it.
- Keep FBX optional in normal tests unless a small redistributable fixture is available.

Acceptance:
- Optional KayKit/FBX validation imports skeleton, clips, skin weights, and materials.
- FBX playback uses the same runtime and renderer path as glTF.
- Format-specific warnings explain any lost data.

## Phase B9 - Animation Cache And Async Loading

Extend existing authored cache and async patterns to skeletal assets.

Scope:
- Add animation pipeline version fields to cache identity.
- Cache skeletons, skins, vertex buffers, clips, and diagnostics.
- Keep cache files derived and deletable.
- Add worker-safe import/cache jobs for animated assets.
- Keep renderer resource creation on the main thread.

Acceptance:
- Cache hit produces equivalent skeleton, skin, and clip data to source import.
- Stale/corrupt cache falls back to source import.
- Async import can load an animated payload and commit it through the runtime owner.

## Phase B10 - Stability, Diagnostics, And Profiling

Harden animation support with repeatable tests and reports.

Scope:
- Add diagnostics summary export for animated assets.
- Emit profile reports for:
  - source import;
  - cache read/write;
  - renderer commit;
  - pose evaluation;
  - draw submission;
  - shutdown.
- Add repeated load/shutdown tests.
- Add optional large-asset validation reports.

Acceptance:
- Normal tests assert correctness and leak-free ownership.
- Performance reports are emitted without hard machine-dependent thresholds.
- Debug output explains why an animated asset failed to load or render.

## Suggested Priority

1. B1 CPU skeleton and clip import boundary.
2. B2 animated model runtime ownership.
3. B3 CPU pose evaluation.
4. B4 renderer skinned mesh contract.
5. B5 skinned shader path.
6. B6 animated runtime integration.
7. B7 blending and state utilities.
8. B8 FBX skeletal compatibility.
9. B9 animation cache and async loading.
10. B10 stability, diagnostics, and profiling.

## Deferred

- Full animation state machines.
- Retargeting between skeletons.
- IK and procedural animation.
- Ragdoll or physics-driven animation.
- Morph targets and blend shapes.
- Cloth, hair, and secondary simulation.
- Animation compression.
- GPU compute skinning.
- Crowd animation instancing.
- Editor timeline or animation graph tools.
