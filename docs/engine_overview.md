# ManualEngine Overview

ManualEngine is now a modern scene-backed SDL + bgfx open-world prototype. `src/App` owns platform lifetime and executable composition for the sample and editor targets, `src/Engine` owns deterministic CPU/runtime systems, `src/Renderer` owns bgfx resources and Dear ImGui presentation, and `src/Assets` decodes source files into CPU data.

The former procedural compatibility stack has been removed from active code. There is no `World`, `ActorController`, `BlockingCollisionSystem`, `TerrainSystem`, legacy authored scene owner, legacy animated model owner, or scene-world migration bridge in the runtime or tests. Historical documents may mention those systems as migration context only.

## Runtime Ownership

- App owns SDL window lifetime, bgfx init/shutdown, frame pumping, modern default scene composition, debug UI snapshot composition, camera/input wiring, and explicit live-resource promotion callbacks for streaming. The sample and editor executables share the modern scene launch path while keeping target identity in App entrypoints; the editor entrypoint also loads a durable App-side project profile before startup. Editor project settings reflection, editor-only ImGui settings panels, saved-build rebuild coordination, explicit lightweight live apply, streaming-only rebuilt runtime reload, native editor hooks, and Lua tool scripting are App-side metadata/control surfaces over that profile and are separate from live scene reflection.
- Engine owns the `Scene` kernel, scene transform hierarchy, scheduler, scene render bridge records, authored/animated scene adapters, scene physics, scene character movement, scene navigation, terrain dataset records, terrain adapters, asset registry/cache metadata, behavior hooks, Lua scripting, scene serialization, debug visualization requests, fixed-step timing, async CPU work, main-thread frame budgeting, and open-world streaming residency coordinators.
- Renderer owns bgfx resources, renderer handles, terrain draw buffers, mesh/skinned instances, materials, terrain material sets, render groups, culling, draw batching, atmosphere state, debug primitive submission, and Dear ImGui rendering.
- Assets importers decode source files into CPU-side scene, mesh, image, animation, and heightmap data. Importers do not own live renderer, physics, navigation, or scene resources.

## Main Data Flow

- SDL events are offered to Dear ImGui first. If ImGui captures input, gameplay input does not receive those events.
- `InputState` records physical SDL input for the frame. `InputMapping` publishes semantic `InputActionEvent`s to `EventQueue`.
- Cursor clicks are projected through `CursorTrace` into world rays from the active view-projection matrix. Navigation path requests, future actor selection, and future in-engine GUI hit tests consume those rays explicitly instead of treating the camera position as the click target.
- The modern default scene is the only startup runtime. It composes `Scene`, scheduler systems, `SceneRenderBridge`, scene authored/animated adapters, `TerrainDataset`, terrain render/navigation/physics adapters, `ScenePhysicsWorld`, `SceneNavigationService`, `SceneCharacterMovementSystem`, and `OpenWorldStreamingRuntime`.
- Terrain comes from imported heightmap data when available and a generated `TerrainDataset` source as fallback. Renderer terrain, navigation tile bytes, and scene-physics terrain colliders are explicit adapter outputs and never appear as side effects of loading a terrain chunk.
- Open-world streaming owns desired/cache/live residency state for durable terrain, scene chunk, nav, physics, render LOD, and asset dependency records. Worker queues read/decode or generate CPU payloads; main-thread callbacks promote/demote live renderer, navigation, physics, scene, and asset-cache resources.
- The editor may reload rebuilt saved streaming data without restarting the whole scene. That path validates the rebuilt manifest first, releases only streaming-owned terrain render handles, nav tiles/connectivity, terrain physics colliders, streamed terrain chunks, pending streaming work, and cached streaming runtime state, then reinitializes `OpenWorldStreamingRuntime` from the manifest and lets the normal frame loop repromote live resources.
- Navigation queries read loaded Detour tiles through `NavigationSystem` and `SceneNavigationService`. Adjacent-tile movement uses border-aware terrain nav baking plus `NavigationConnectivitySystem` portal metadata over loaded tiles.
- Scene physics owns Jolt-backed scene bodies/colliders. Character movement is kinematic and uses explicit physics queries and optional scene navigation paths. Grounded movement can exclude the current walkable triangle-mesh ground collider or bounded zero-distance static triangle-mesh bounds hits from retry sweeps so coarse mesh bounds do not block horizontal path following. Physics debug draw can inspect transient live collider shape snapshots as wireframe boxes, spheres, capsules, and triangle meshes.
- Scene serialization stores durable scene actor/component identity and metadata only. Runtime handles, renderer resources, physics bodies, nav tiles, terrain chunk handles, streaming tokens, Lua VM state, and behavior callbacks are never serialized.

## Current Feature Surface

- Modern scene actor/component runtime with transform hierarchy, lifecycle, scheduler phases, render bridge sync, reflection/opaque access, native behavior hooks, Lua scripting, and binary core scene serialization.
- Editor Lua tool scripts are one-shot App-side commands loaded from `scripts/editor`. They inspect and edit reflected `EditorProjectSettings` and invoke validated editor commands, but they do not run as scene scheduler systems and do not receive Renderer, physics, navigation, streaming, Lua VM, or App storage handles.
- Scene authored adapter for static glTF/FBX CPU imports and scene animated adapter for skinned glTF/FBX CPU imports.
- `TerrainDataset` CPU terrain ownership for imported heightmap chunks and procedural fallback chunks.
- Terrain render LOD, navigation build-data, physics collider, material metadata/render, derived cache, and serialization-prep adapters.
- Open-world streaming S0-S8 stack: manifest/halo planning, async cache reads, optional generation-on-miss, budgeted live promotion/demotion, full-heightmap bake, asset dependency streaming, scene chunk streaming, multi-halo variants, saved-build validation/rebuild, and modern default/editor integration.
- `NavigationSystem`, `SceneNavigationService`, `NavigationConnectivitySystem`, `NavigationCache`, scene navigation geometry registry, and border-aware terrain nav generation.
- `ScenePhysicsWorld` with static/kinematic/dynamic bodies and headless raycast, capsule sweep, overlap, and closest-point queries.
- `SceneCharacterMovementSystem` with grounding, collision-constrained kinematic capsule motion, and optional path following.
- Renderer scene submission with culling, render layers, terrain material sets, atmosphere/fog controls, debug draw, and Dear ImGui modern debug tabs.
- Asset registry and asset cache for stable metadata IDs and reusable renderer mesh/texture acquisitions.

## Save-Facing Contracts

Detailed scene binary and durable identity conventions live in `docs/scene_runtime/serialization_conventions.md`.

- `SceneActorHandle`, `SceneComponentHandle`, scene render bridge handles, scene physics handles, scene character handles, terrain runtime handles, navigation handles, asset runtime handles, `OpaqueHandle`, Lua handles, behavior handles, and streaming runtime tokens are transient and must not be serialized.
- Durable identity uses `SceneObjectId`, `AssetId`, and terrain source/chunk identities such as `TerrainSourceChunkId` and `TerrainChunkStableIdentity`.
- Saved streaming builds and derived caches are disposable generated data, not save-game data. They may contain durable IDs, source hashes, settings keys, payload versions, and CPU payload files, but never live runtime handles.
- Scene binary serialization currently round-trips core scene actor/component records only. Renderer, physics, navigation, character, terrain, authored/animated, Lua, behavior, and streaming live resources are reconstructed only by explicit future systems.

## Encapsulation Guidance

- Keep reusable behavior under `src/Engine`, renderer/bgfx ownership under `src/Renderer`, source decoding under `src/Assets`, and executable composition under `src/App`.
- Do not hide expensive work behind setters. Loading, baking, streaming, renderer uploads, nav insertion, and physics body creation should be explicit calls or streaming promotion callbacks.
- Keep debug UI structs plain and modern-system-facing. Debug controls should call public Engine/Renderer APIs, not mutate internal storage.
- Prefer durable IDs in manifests, caches, and serialized files. Runtime handles are valid only inside the current process.
- Add new cross-system contracts to `docs/system_contracts.md` in the same change that introduces the behavior.
