# ManualEngine Overview

ManualEngine is currently a deliberately small SDL + bgfx open-world prototype. The guiding shape is: `src/App` composes services and owns platform/frame flow, `src/Engine` owns deterministic simulation and world-state rules, `src/Renderer` owns bgfx resources/submission, and `src/Assets` decodes source files into CPU-side data.

Keep this document current whenever subsystem ownership, data flow, or save-facing contracts change. Use `docs/system_contracts.md` as the detailed checklist for currently available systems, renderer features, initialization order, and cross-system contracts. Use `docs/system_inboxes.md` when adding message-driven cross-system input. Use `docs/performance_roadmap.md` when planning worker offload, frame budgeting, streaming, and hitch-reduction work. Use `docs/authored_scene_roadmap.md` when planning glTF/PBR authored scene loading and streaming work. Use `docs/scene_component_roadmap.md` when planning scene, actor/entity, component, asset registry, physics, scripting, serialization, or cross-runtime integration work.

## Runtime Ownership

- App owns SDL window lifetime, bgfx init/shutdown, frame pumping, debug UI composition, sample asset/material creation, and explicit service wiring.
- Engine owns world object lifetime, stable object identity, the renderer-independent scene kernel, transform hierarchy, tick scheduler shell, scene render bridge records, scene authored and animated adapter resource bindings, CPU-only asset registry metadata, fixed-step actor updates, chunk streaming membership, async CPU work primitives, main-thread frame budgeting, terrain CPU data, procedural content rules, biome sampling, input mapping, interaction events, picking helpers, persistence records, and simple debug/editor mutations.
- Renderer owns GPU resources, renderer handles, materials, terrain draw buffers, mesh instances, render groups, culling, draw batching, atmosphere state, and Dear ImGui rendering.
- Asset registry is an Engine metadata service for stable asset IDs, source paths, import settings, dependencies, and stale/missing diagnostics. Asset cache remains the Engine service that deduplicates reusable renderer mesh/texture handles. Terrain draw buffers remain transient and are not cached.

## Main Data Flow

- SDL events are fed to Dear ImGui first. If ImGui wants mouse or keyboard capture, the game input snapshot does not receive those events.
- `InputState` records physical SDL input for the frame. `InputMapping` converts it to semantic `InputActionEvent`s.
- Camera, actor controller, and interaction systems consume semantic events rather than physical keys/buttons.
- Future message-driven system inputs should use receiver-owned typed inboxes exposed through publish-only sinks. App/composition code wires sink references and owns explicit flush order; avoid global dynamic event bus registries.
- `World` owns transient object handles, transforms, optional stable `ObjectId`s, CPU bounds, collision flags, angular velocity, and explicit renderer instance bindings.
- `World::syncRenderState()` pushes CPU transforms into renderer instances. Renderer resources never own world object lifetime.
- Chunk streaming owns loaded chunk membership, render group metadata, terrain tile handles, and prop object lists. App stages missing desired chunks through `AsyncWorkQueue`, where jobs produce plain generated terrain/prop data and optional Detour tile bytes. Main-thread load commit is split into budgeted work items that create render groups, CPU terrain tiles, renderer terrain buffers, world objects, renderer instances, spatial entries, and live navigation tiles. Runtime unload also runs in budgeted phases for nav removal, prop destruction, terrain destruction, render-group destruction, and dirty finalization. Chunk streaming does not own mesh/material/texture assets.
- Terrain is generated as deterministic CPU heightfield tiles using biome-configured shape parameters. Renderer LOD mesh generation uses worker-safe CPU tile snapshots and produces plain vertex/index data; the main thread only validates tile generation and uploads/replaces bgfx terrain handles under the frame budget. Gameplay height queries and terrain diagnostics use CPU tile data, not active render LOD.
- Navigation is an Engine service wrapped by `NavigationSystem`; Recast/Detour types stay behind that boundary. Runtime nav tile sync is cache/worker-first and chunk-capped: baseline chunks enqueue cache read jobs, cache misses enqueue worker Recast byte builds, and the main thread only inserts completed tile bytes into the live Detour navmesh. Actors can follow nav paths through Engine-owned path state and may wait for pending local tiles instead of forcing a synchronous build. `NavigationConnectivitySystem` derives coarse loaded-chunk border portals above those local tiles and stores explicit source/neighbor portal pairs; normal updates rebuild changed chunks and neighbors through phased main-thread steps for chunk setup, edge sampling, relinking, and finalization. `WorldNavigationGraph` builds deterministic chunk-level routes for hierarchical actor move commands, keeps multiple edge candidates when a chunk border has multiple connected portal pairs, and recenters only after the active center moves beyond a configurable chunk threshold. Runtime graph construction uses worker-safe terrain/biome/connectivity snapshots and the main thread only swaps finished graph cache data into the live graph after pending connectivity phases finish. `NavigationCache` stores versioned derived baseline tile/connectivity/graph artifacts; the active profile ID and navigation source resolution participate in cache identity. Cache file reads and writes use worker-safe helpers with immutable manifest/settings snapshots, while the live cache object remains main-thread stats/debug state. Write-through defaults off so cache generation is an explicit debug action rather than surprise frame-time disk I/O. Navigation tile diagnostics, portal diagnostics, and actor command diagnostics are plain Engine data surfaced through Dear ImGui for tuning. Actors try direct local Detour paths first, then consume coarse routes by requesting local paths to paired source/neighbor portal waypoints; same-chunk local failures may detour through an adjacent chunk. Chunk streaming remains camera/App-owned.
- App treats chunk streaming, navigation tiles, connectivity, world graph data, renderer visibility metadata, and picking as derived work behind dirty flags and the `FrameBudget`/`MainThreadWorkQueue`. Chunk changes, explicit rebuilds, terrain LOD rebuilds, save/load, and persistent edits mark the relevant systems dirty; stationary frames should not repeat those expensive rebuilds. Renderer metadata reapply is chunk-incremental: local changes update only touched chunks, while global debug visibility changes process loaded chunks over capped budgeted steps.
- Simple group-command formation math lives in Engine actor command helpers so App composition does not become the owner of reusable movement rules.
- V1 terrain material assignment is one renderer material per tile, selected from the primary chunk biome's configured terrain color. Biome boundaries are not blended yet.
- Procedural props are generated from chunk coord, biome rules, archetype descriptors, and explicit local slots. Stable procedural IDs use chunk coord + archetype ID + slot.
- Save/load stores high-level state: player position, camera state, settings, persistent object overrides, removed object IDs, and custom object serials. Baseline terrain and procedural content regenerate.

## Current Feature Surface

- Static mesh and texture loading through renderer APIs and `AssetCache`.
- CPU-only asset registry with generation-counted `AssetHandle` values, stable `AssetId` metadata, import settings identity, dependency tracking, and stale/missing source diagnostics.
- Renderer material handles with base color, normal, metallic, and roughness factors.
- Renderer scene submission through `RenderView`, with CPU frustum culling, distance culling, render layers, render groups, draw stats, and conservative CPU batching by mesh/material.
- Renderer atmosphere state: sky clear color, exponential fog, and directional sun settings. Fog is disabled by default and can be enabled from the debug UI.
- Renderer debug draw primitives are transient per-frame submission aids for bounds, chunk borders, frustums, actor movement diagnostics, and navigation visualization. Debug primitive generation is capped globally and by expensive categories so debug visualization cannot grow without bound.
- Dear ImGui debug panel for renderer stats, world save/edit controls, camera, biomes, picking, interactions, terrain LOD, spatial registry, and player actor status, grouped into tabs so dense diagnostics stay readable.
- Debug builds keep continuous hover picking and timing visibility. Release builds disable debug UI/draw hooks and update picking only for commands that need a fresh target.
- Debug builds record long-frame diagnostics with the last hitch's CPU bucket summary and slowest budgeted main-thread work item.
- Default renderer distance culling is finite for gameplay-scale visibility: props draw to about `160m`, terrain tiles to about `280m`, with a camera far plane of about `900m`. Debug UI can still tune these values at runtime.
- Fixed-step engine loop, world object ownership, kinematic actors, reusable actor path-following state, simple blocking collision, and terrain grounding.
- Scene kernel with generation-counted `SceneActorHandle`, `SceneComponentHandle`, and `SceneSystemHandle` values, stable-but-unserialized `SceneObjectId` metadata, metadata-only component attachment, scene-local transform hierarchy state, and a CPU-only lifecycle/tick scheduler.
- Scene render bridge with generation-counted mesh, skinned mesh, light, and camera component handles, explicit backend sync to renderer handles, and camera `RenderView` construction. The release authored sample path syncs static authored content through this bridge.
- Scene authored adapter that converts already-imported static authored scene CPU data into scene actors, hierarchy transforms, adapter-owned renderer meshes/materials/textures, and scene render bridge mesh/light components. Release authored mode uses this path; debug authored mode still uses `PartitionedAuthoredScene`, async authored loading, and authored caches for diagnostics and streaming validation.
- Scene animated adapter that converts already-imported animated scene CPU data into scene actors, skeleton bindings, animator state, adapter-owned renderer skinned meshes/materials/textures, and scene render bridge skinned mesh components. The release authored sample includes a KayKit animated character through this adapter; the debug App animated sample still uses the existing `AnimatedModel`, cache, async, and renderer skinned path.
- Sim/orbit camera with free and player-follow modes.
- Sparse grid spatial registry for gameplay/debug queries.
- CPU debug picking against object bounds and loaded terrain.
- Deterministic chunk streaming with async generated biome-shaped terrain tiles, biome-driven procedural props, staged main-thread commits, and fixed-ms main-thread frame budgeting.
- Worker-generated terrain renderer LOD meshes with budgeted main-thread upload commits and debug counters for queued, completed, stale, and committed LOD transitions.
- Incremental renderer visibility metadata reapply for terrain/prop material, layer, distance, and render-group settings.
- Capped debug draw generation with counters for generated, submitted, and clipped primitives.
- Chunk-capped runtime navigation sync, phased connectivity refresh, and threshold-based coarse graph recentering.
- Current sample streaming profile uses `24m` chunks, a load radius of `12` chunks, and a coarse graph radius of `64` chunks. This preserves the earlier large world-space coverage while making individual chunk/nav-tile commits smaller.
- Recast/Detour loaded-chunk navigation tile generation with static prop blockers, cache/worker-first runtime tile builds, lower-resolution nav source geometry, nearest/path query support, loaded-chunk portal connectivity metadata, generated coarse world navigation graph/routes, derived navigation cache records, YAML navigation profile loading, hierarchical route-following commands for the player or selected demo actors, right-click object interaction, semantic stop/cancel commands, and navigation debug draw/diagnostic/tuning controls.
- YAML-backed input mapping, object archetype definitions, and biome definitions with safe fallback defaults.
- Stable `ObjectId` identity and save-backed persistent object overrides/removals.
- Tag-based interaction handling for inspectable, removable, and resource-node archetypes.

## Save-Facing Contracts

- `WorldObjectHandle` is transient and must not be serialized.
- `ObjectId` is the save/persistence key.
- `SceneActorHandle` and `SceneComponentHandle` are transient scene runtime handles and must not be serialized; `SceneObjectId` is reserved for future scene serialization and is not part of current save/load.
- `SceneMeshComponentHandle`, `SceneSkinnedMeshComponentHandle`, `SceneLightComponentHandle`, and `SceneCameraComponentHandle` are transient render bridge handles and must not be serialized.
- `SceneSkeletonHandle` and `SceneAnimatorHandle` are transient animated adapter handles and must not be serialized; future durable animated-scene identity will use scene/asset serialization contracts.
- `AssetHandle` is a transient asset registry runtime handle and must not be serialized. `AssetId` is the stable asset identity reserved for future scene/component references; current save/load still uses existing world persistence contracts.
- Procedural prop IDs are generated by `ObjectId::proceduralProp(coord, archetypeId, localSlot)`.
- Changing archetype IDs, procedural slot assignment, chunk coordinate derivation, or default biome rules can change regenerated object IDs/content and should be treated as save-affecting unless a migration is added.
- Persistent object overrides store absolute world transforms and owning chunks. Removed object records suppress regenerated baseline procedural props by stable ID.
- Debug/editor tools should mutate persistence through `WorldObjectOverrides`.

## Encapsulation Guidance

- Keep new engine behavior out of `src/App/main.cpp`; App should translate UI/sample intent into public service calls.
- Add public Engine APIs for reusable simulation behavior before adding more sample-only lambdas.
- Keep renderer APIs handle-based and avoid exposing bgfx resource lifetime outside Renderer.
- Keep config parsing in focused catalog/mapping systems, not in the main loop.
- Keep debug UI display/control structs plain and renderer-facing; actual simulation mutation belongs in App via Engine service APIs.
- Prefer small deterministic systems over broad managers until concrete complexity requires a larger abstraction.
