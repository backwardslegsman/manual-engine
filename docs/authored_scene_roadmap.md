# Authored PBR Scene Loading And Streaming Roadmap

This roadmap prepares ManualEngine to load, render, and eventually stream complex authored scenes such as Intel's Sponza 2022 asset under `assets/main_sponza`. The goal is not a one-off importer hack. The end state should be a stable authored-scene pipeline that can coexist with procedural world streaming and can scale to large static environments with PBR materials.

## Goal

- Load authored glTF-style scenes with scene graph transforms, meshes, materials, textures, and lights.
- Render PBR materials with correct texture channel semantics, transparency handling, culling, and texture sampling.
- Own authored scene lifetime explicitly through Engine/Renderer handles.
- Keep bgfx resources renderer-owned and avoid leaking third-party asset-loader details into gameplay systems.
- Support large scene startup without unbounded stalls, and evolve toward chunk/sector streaming for authored scenes.
- Make Release mode capable of using Sponza as the default visual scene while preserving Debug/procedural workflows.

## Guiding Rules

- Prefer glTF 2.0 as the first authored-scene contract. FBX/USD may remain source/interchange assets, but runtime work should target one predictable format first.
- Do not make `src/App/main.cpp` the scene loader. App may choose which scene to load, but reusable import, resource creation, and lifetime rules belong under `src/Assets`, `src/Engine`, and `src/Renderer`.
- Importers produce CPU-side scene data. Renderer creates GPU resources from that data.
- Keep authored scene object identity separate from save-facing procedural `ObjectId` until persistence requirements are concrete.
- Future authored-scene serialization should follow `docs/scene_runtime/serialization_conventions.md`: persist imported scene asset identity and durable node/binding descriptors, not renderer handles, cache acquisitions, or live adapter resources.
- Keep scene loading failure explicit. Missing textures/material features should report diagnostics and use intentional fallbacks.
- Support large scenes through budgets, worker-safe decode stages, and explicit resource ownership before adding automatic streaming.
- Keep Sponza-specific compatibility useful as validation, but avoid hardcoding Sponza paths or material names into renderer systems.
- Do not run heavy optional asset tests by default. Use `MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1` only for changes where Sponza-scale validation meaningfully exercises the code under review.

## Current State Snapshot

The Sponza glTF currently exposes:

- 155 nodes, 115 mesh nodes, and 405 indexed triangle primitives.
- 28 PBR materials and 72 glTF texture references.
- Texture attributes using base color, normal, and glTF metallic-roughness packed maps.
- Vertex streams including `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`, `TEXCOORD_1`, and `COLOR_0`.
- `KHR_lights_punctual` metadata and an HDR environment texture.
- Alpha and double-sided material cases.

Implemented baseline:

- CPU authored scene import preserves scene graph structure, transforms, meshes, materials, textures, lights, skeletal/animation records, bounds, and diagnostics.
- Static authored runtime ownership creates renderer resources through eager and partitioned loaders.
- Partitioned authored scenes support sector manifests, budgeted main-thread sector commits, cache reads/writes, async source import/cache work, and diagnostics summaries.
- Renderer material, texture, vertex, lighting, alpha, PBR, static mesh, skinned mesh, and debug stats paths have been expanded to support authored assets.
- Release authored mode can load a meaningful authored scene or fall back to committed lightweight assets when optional Sponza assets are absent.

Remaining broad gaps:

- No scene/component integration yet; authored scenes still live behind authored scene owners rather than general actors/components.
- No persistent prebuilt compressed texture pipeline or GPU-ready scene package.
- No occlusion culling, hierarchical LOD, or advanced visibility system for large authored environments.
- Full HDR IBL, skybox/environment capture, shadows, and high-end material fidelity remain future renderer work.

## Phase A1 - Authored Scene CPU Import Boundary

Create an Engine/Assets-facing scene import model that preserves authored scene structure without creating GPU resources.

Scope:
- Add CPU-side imported scene types:
  - scene nodes with parent/child relationships;
  - local and world transforms;
  - mesh references and primitive/material assignments;
  - material descriptors with glTF PBR fields;
  - texture references with sampler and color-space intent;
  - optional light records;
  - import diagnostics and unsupported-feature warnings.
- Add a glTF-first importer path, using Assimp only if it preserves all required glTF data reliably. If not, add a focused glTF loader instead of piling more assumptions into the static mesh importer.
- Preserve node transforms rather than relying on all geometry being pre-baked into one mesh.
- Keep existing `importStaticMesh` intact for simple mesh assets.

Acceptance:
- A headless test imports Sponza and reports the expected node, mesh, primitive, material, texture, and light counts.
- Imported mesh-node world transforms match glTF node transforms for representative nodes.
- Import diagnostics identify alpha, double-sided, second UV, vertex color, and unsupported extension usage.

## Phase A2 - Authored Scene Runtime Ownership

Add a runtime scene resource owner that creates renderer resources from imported scene data and tears them down safely.

Scope:
- Add an `Engine::AuthoredScene` or similar owner for:
  - renderer static mesh handles;
  - renderer material handles;
  - renderer texture handles;
  - renderer mesh instances;
  - optional render groups/sectors;
  - scene bounds and diagnostics.
- Route texture and mesh reuse through `AssetCache` or a scene-local cache with explicit release semantics.
- Create one renderer instance per authored mesh node or per primitive grouping as needed.
- Preserve scene/world transform handoff without requiring `World` objects for every static scene piece.
- Add clear shutdown ordering: instances, meshes/materials/textures, then renderer shutdown.

Acceptance:
- A headless or renderer-smoke path can load and unload the Sponza scene without leaked handles.
- Scene bounds are valid and useful for camera placement.
- Failed resource creation reports which asset/material/texture failed and what fallback was used.

## Phase A3 - glTF Material Semantics

Teach renderer materials enough glTF semantics to represent Sponza correctly.

Scope:
- Extend `Renderer::MaterialDescriptor` with:
  - base color factor/texture;
  - metallic factor;
  - roughness factor;
  - packed metallic-roughness texture plus channel mapping;
  - normal texture and normal scale;
  - occlusion texture and strength;
  - emissive factor/texture;
  - alpha mode: opaque, mask, blend;
  - alpha cutoff;
  - double-sided flag;
  - texture color-space/sampler hints.
- Add fallback behavior for missing or unsupported texture slots.
- Add debug material diagnostics: texture slot validity, alpha mode, culling mode, and packed channel interpretation.

Acceptance:
- Sponza materials map without losing base color, normal, metallic-roughness, alpha, and double-sided intent.
- Packed glTF metallic-roughness maps sample roughness and metallic from the correct channels.
- Existing procedural materials still work without needing to populate every new field.

## Phase A4 - Vertex Format And Mesh Import Completeness

Support the vertex data needed by modern authored scenes while preserving simple meshes.

Scope:
- Extend mesh vertex data for:
  - tangent handedness (`vec4` tangent or equivalent);
  - `TEXCOORD_1`;
  - `COLOR_0`;
  - optional missing-stream fallbacks.
- Update bgfx vertex layout and shaders accordingly.
- Ensure tangent basis generation or import is consistent and documented.
- Keep bounds generation correct with node transforms and large static scenes.

Acceptance:
- Imported Sponza primitives retain second UV and vertex color where present.
- Normal mapping uses tangent handedness correctly.
- Existing terrain and fallback cube paths compile and render after layout changes.

## Phase A5 - Texture Pipeline Foundation

Replace naive large-texture handling with a predictable renderer texture contract.

Scope:
- Add texture descriptors with:
  - color space: sRGB or linear;
  - sampler wrap/filter modes;
  - mip usage;
  - intended material slot.
- Generate mipmaps at load time when acceptable, or add an offline/preprocessed texture path.
- Support repeat wrapping for authored material textures.
- Add texture cache diagnostics and estimated GPU memory reporting.
- Define a preferred runtime texture format path for large authored scenes, likely compressed GPU-ready textures as a later step.

Acceptance:
- Base color textures are treated as sRGB; normal/metallic/roughness/occlusion are treated as linear data.
- Sponza texture sampling uses repeat and mip filtering instead of clamp/no-mip defaults.
- Loading reports texture count and memory estimates.
- A missing texture produces a stable fallback and diagnostic, not a silent black material unless black is intentional.

## Phase A6 - Opaque And Transparent Render Paths

Split material submission into stable opaque and transparent paths.

Scope:
- Add per-material render state:
  - opaque;
  - alpha mask;
  - alpha blend;
  - double-sided/cull state.
- Sort opaque draws for batching/material locality.
- Sort blended draws back-to-front after opaque depth writes.
- Avoid writing depth for blended materials unless a documented mode requires it.
- Preserve renderer stats by opaque/transparent pass.

Acceptance:
- Sponza decals/glass/lamp glass render in the correct pass.
- Opaque scene pieces continue to depth-test and batch predictably.
- Debug stats identify transparent draw counts and material states.

## Phase A7 - glTF-Compatible PBR Shader

Upgrade the mesh shader from a simple lit material to a stable glTF metallic-roughness PBR implementation.

Scope:
- Implement a Cook-Torrance style metallic-roughness BRDF suitable for forward rendering.
- Use correct view direction from camera position and world position.
- Use normal mapping with imported tangent basis.
- Apply base color, metallic, roughness, occlusion, emissive, alpha, and vertex color where supported.
- Add tone mapping and exposure controls.
- Keep shader feature complexity bounded through uniforms/flags before introducing many variants.

Acceptance:
- Sponza renders with plausible PBR material response under the existing sun/ambient model.
- Procedural terrain/props continue to render acceptably with simple material descriptors.
- Debug controls expose exposure and key lighting parameters.

## Phase A8 - Authored Lighting And Environment

Add enough authored-scene lighting support for complex static scenes.

Scope:
- Import glTF punctual lights into plain Engine/Renderer light descriptors.
- Decide whether zero-intensity exported lights are ignored, overridden by debug defaults, or repaired through scene settings.
- Add a bounded forward-light path for a small number of directional/point/spot lights, or a clustered/tiled path if the simple path proves insufficient.
- Add HDR environment loading as a renderer resource.
- Add simple image-based lighting:
  - irradiance/diffuse ambient;
  - prefiltered specular or a simpler interim approximation;
  - BRDF integration lookup if needed.

Acceptance:
- Sponza can be lit without relying only on constant ambient plus one sun.
- HDR environment contributes visibly and can be toggled/debugged.
- Light count limits and fallback behavior are explicit.

## Phase A9 - Release Scene Mode

Add a clean runtime mode that launches an authored scene in Release without carrying the full procedural sample setup.

Scope:
- Add a small scene selection/config path:
  - Debug/default procedural sample;
  - Release/default Sponza authored scene;
  - fallback procedural or fallback cube if authored scene load fails.
- Set camera start, movement bounds, near/far planes, and draw distances from scene bounds or config.
- Keep navigation/procedural chunk systems disabled or isolated unless explicitly needed for authored scene mode.
- Preserve debug UI in Debug builds for authored scene inspection.

Acceptance:
- Release build starts in Sponza mode when assets are present.
- Missing Sponza assets produce a clear fallback and log message.
- Debug build can still run the procedural open-world sample.
- The authored scene can be loaded, viewed, and shut down without requiring procedural chunks.

## Phase A10 - Streaming-Friendly Scene Partitioning

Move from full-scene startup loading toward explicit authored scene partitioning.

Scope:
- Partition imported scenes into sectors/cells based on authored nodes, bounds, or generated spatial chunks.
- Build per-sector resource manifests:
  - nodes/instances;
  - mesh references;
  - material/texture references;
  - light references;
  - bounds.
- Keep shared resources reference-counted across sectors.
- Add scene visibility/streaming policy driven by camera position and sector bounds.
- Use `AsyncWorkQueue` for CPU decode and file I/O where safe.
- Use `MainThreadWorkQueue` for budgeted renderer resource creation/destruction.

Acceptance:
- Sponza can be partitioned and loaded sector-by-sector with stable visual convergence.
- Moving the camera does not require loading all sector resources in one frame.
- Resource reference counts remain correct when adjacent sectors share textures/materials.

## Phase A11 - Preprocessed Runtime Scene Cache

Add a derived-data cache for authored scenes so Release startup does not parse and upload everything from source assets every run.

Scope:
- Define a versioned authored-scene cache identity:
  - source asset path/hash;
  - importer version;
  - material pipeline version;
  - texture processing settings;
  - renderer vertex format version.
- Store preprocessed mesh buffers, material records, sector manifests, and texture metadata.
- Optionally store GPU-ready compressed textures or point to a texture build output directory.
- Keep cache files derived, deletable, and separate from save data.

Acceptance:
- Loading Sponza from cache is faster than source glTF/PNG parsing.
- Cache mismatch is reported and treated as a miss.
- Cache generation can be explicit/debug-driven before automatic Release use.

## Phase A12 - Stability, Diagnostics, And Performance Gates

Make authored scene support maintainable, measurable, and hard to regress.

Scope:
- Add tests for:
  - Sponza import counts/diagnostics;
  - scene graph transforms;
  - material mapping;
  - texture descriptor color-space/sampler interpretation;
  - sector partition determinism;
  - load/unload lifetime.
- Add runtime diagnostics:
  - scene load phase timings;
  - mesh/material/texture counts;
  - estimated CPU/GPU memory;
  - sector loaded/pending/unloaded counts;
  - transparent/opaque draw stats;
  - missing/unsupported feature warnings.
- Add performance smoke thresholds where stable enough, or produce profiling artifacts without flaky hard limits.

Acceptance:
- Authored scene loading has automated coverage beyond a visual manual check.
- Debug UI can explain why a material/texture/sector looks wrong or is missing.
- Release Sponza startup and shutdown are repeatable and do not leak renderer resources.

## Suggested Priority

1. A1 CPU scene import boundary.
2. A2 runtime authored scene ownership.
3. A3 glTF material semantics.
4. A4 vertex format completeness.
5. A5 texture pipeline foundation.
6. A6 opaque/transparent render paths.
7. A7 PBR shader.
8. A9 Release scene mode.
9. A8 authored lighting/environment.
10. A10 streaming-friendly scene partitioning.
11. A11 preprocessed runtime scene cache.
12. A12 stability, diagnostics, and performance gates.

A9 can happen before A8 if the scene only needs to become launchable under approximate lighting. For a quality target, A8 and A12 should not be skipped.

## Deferred

- Skeletal animation and skinned meshes.
- USD runtime loading.
- Full material graph support.
- Ray-traced lighting or path tracing.
- Full editor placement/authoring UX for imported scenes.
- Physics collision generation from authored meshes.
- Navigation generation from arbitrary authored indoor geometry.
- Runtime texture virtual memory or sparse bindless material systems.
- Automatic asset download or license management.
