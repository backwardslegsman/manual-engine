# ManualEngine Systems And Contracts

This guide is the quick reference for what the prototype currently supports and how the major systems are expected to be wired together. Consult it before adding features that cross subsystem boundaries, and update it whenever a new feature adds an interface contract, ownership rule, or initialization dependency. Future message-driven systems should also follow `docs/system_inboxes.md`.

## Available Gameplay Systems

- **World ownership:** `Engine::World` owns transient world object handles, transforms, stable object IDs, local/world CPU bounds, collision flags, angular velocity, and optional renderer instance bindings.
- **Stable identity:** `Engine::ObjectId` is the persistence-facing identity. `WorldObjectHandle` is transient and must not be saved.
- **Scene kernel:** `Engine::Scene` owns renderer-independent scene actor records, metadata-only component records, scene-local transform hierarchy state, and a CPU-only lifecycle/tick scheduler shell. `SceneActorHandle`, `SceneComponentHandle`, and `SceneSystemHandle` are transient generation-counted runtime handles; `SceneObjectId` is reserved as stable scene identity but is not connected to save/load yet. Scene transform hierarchy and scheduler state are runtime-only and do not drive `World`, physics, navigation, or serialization yet.
- **Scene render bridge:** `Engine::SceneRenderBridge` owns scene mesh, skinned mesh, light, and camera component records plus mappings to live renderer handles through an explicit `SceneRenderBackend`. The bridge syncs scene world transforms to renderer-facing resources during `PreRender` or manual sync; Renderer remains handle-based and does not read scene actor/component storage.
- **Scene authored adapter:** `Engine::adaptImportedSceneToScene` consumes already-imported `Assets::Assimp::ImportedScene` CPU data and creates scene actors, hierarchy links, adapter-owned static mesh/material/texture resources, and scene render bridge mesh/light components. The release authored sample path uses this adapter for static authored scenes; debug authored mode keeps the existing `PartitionedAuthoredScene`, authored cache, and async loading path for diagnostics and streaming validation.
- **Scene animated adapter:** `Engine::SceneAnimatedModelAdapter` consumes already-imported animated `Assets::Assimp::ImportedScene` CPU data, creates scene actors for imported nodes, skeleton/joint bindings, animator records, adapter-owned skinned mesh/material/texture resources, and scene render bridge skinned mesh components. It reuses the shared `AnimatedModel` pose sampling, playback, palette, and skinned vertex packing helpers; the release authored sample uses this adapter for its KayKit animated character while existing debug `AnimatedModel`, cache, async, renderer skinned mesh, and App animated sample paths remain operational.
- **Fixed-step loop:** `Engine::FixedStepLoop` provides clamped frame timing and 60 Hz fixed update ticks.
- **Input mapping:** SDL events flow into `Engine::InputState`; `Engine::InputMapping` publishes semantic input actions from `assets/config/input.yaml`.
- **Event queue:** `Engine::EventQueue` carries frame input action events and interaction events. It is explicitly cleared at the end of the frame. It is not a general event bus registry; new message-driven systems should prefer system-owned typed inboxes with publish-only sinks.
- **Camera:** `Engine::OrbitCameraController` supports sim free camera and player-follow mode, with configurable pivot bounds, zoom, pitch, panning, and follow offset.
- **Actors:** `Engine::ActorController` owns kinematic actor state, manual movement, path-following state, facing, terrain grounding, and collision-aware movement.
- **Actor selection and commands:** `Engine::ActorSelection` stores selected actors; `Engine::formationOffsetForActorIndex` provides deterministic simple formation slots.
- **Blocking collision:** `Engine::BlockingCollisionSystem` resolves actor cylinder-vs-world-AABB blocking through the sparse spatial registry. It is not a physics engine.
- **Spatial registry:** `Engine::SpatialRegistry` is a sparse XZ grid for debug/gameplay lookup. It does not own world object lifetime.
- **Chunk streaming:** `Engine::ChunkStreamer` owns loaded chunk membership and chunk content handles. App now stages desired chunk loads/unloads around a center point, runs expensive generated-data work through `Engine::AsyncWorkQueue`, and commits live chunks on the main thread with a per-frame budget. Runtime unloads may detach chunk content from loaded membership before phased destruction completes; forced reload/shutdown paths must finish those detached unloads before clearing queued work.
- **Async work queue:** `Engine::AsyncWorkQueue` runs CPU-only jobs that return plain generated data or Detour tile bytes. Workers must not touch live `World`, `Renderer`/bgfx, `TerrainSystem` storage, `NavigationSystem`, `SpatialRegistry`, or mutable save state.
- **Main-thread frame budget:** `Engine::FrameBudget` and `Engine::MainThreadWorkQueue` provide a fixed millisecond budget for deferrable main-thread mutations and resource insertion. Renderer/bgfx calls, live world mutation, live terrain storage, live navmesh insertion, and spatial registry mutation still run on the main thread, but chunk commits and derived rebuilds should be split into small budgeted work items when they can be deferred.
- **Terrain:** `Engine::TerrainSystem` owns loaded CPU heightfield tiles, biome-shaped generation, height queries, terrain raycasts, terrain diagnostics, optional renderer terrain handles, terrain LOD rebuilds, and navigation build data extraction. It also exposes plain generated terrain tile data and render LOD mesh build inputs so worker jobs can build heightfields or terrain draw geometry without creating renderer resources. Runtime terrain renderer LOD changes should use worker mesh generation plus budgeted main-thread upload commits so camera movement cannot recreate every visible terrain buffer in one frame.
- **Biomes:** `Engine::BiomeSystem` deterministically samples biome IDs and debug noise values from world coordinates and chunk coordinates.
- **Procedural content:** `Engine::ProceduralChunkContentConfig` produces deterministic prop spawn descriptors from chunk coord, biome rules, archetype IDs, and stable local slots.
- **Archetypes:** `Engine::ObjectArchetypeCatalog` loads YAML object descriptors with visual defaults, bounds, tags, terrain offset, resource metadata, and collision/interaction tags.
- **Persistence:** `Engine::WorldObjectOverrides` stores removed procedural IDs and persistent object overrides; `Engine::WorldState` saves high-level world state only.
- **Persistent editing:** `Engine::PersistentObjectEditor` mutates save-backed overrides for selected object transforms, removal/reset, and placement.
- **Picking and selection:** `Engine::Picking` builds cursor rays and performs CPU object/terrain picking for debug selection and interactions.
- **Interactions:** `Engine::InteractionSystem` turns semantic input plus picking into target-aware interaction events; `Engine::InteractionHandlerSystem` resolves tag-based outcomes.
- **Navigation profiles:** `assets/config/navigation_profiles.yaml` selects one active player-sized agent/build profile for nav tile generation and queries.
- **Navigation local layer:** `Engine::NavigationSystem` wraps Recast/Detour. Runtime tile sync must prefer cache hits, enqueue worker Recast byte builds on misses, and insert only completed tile bytes into the live navmesh on the main thread. Normal gameplay code must not call live `buildTerrainTile()` from the frame path.
- **Navigation connectivity:** `Engine::NavigationConnectivitySystem` derives loaded-chunk edge portals and neighbor links from local nav tiles and records per-edge portal sampling diagnostics. Normal nav tile insert/destroy events should rebuild only changed chunks and neighbors through phased main-thread steps that process chunk setup, one edge or capped sample batch, relinking, and finalization. Full rebuilds are reserved for debug/settings changes but use the same phased mechanism internally.
- **World navigation graph:** `Engine::WorldNavigationGraph` builds a deterministic coarse chunk graph over the generated world and returns coarse routes used by actor hierarchical move commands.
- **Navigation cache:** `Engine::NavigationCache` stores derived baseline nav tile bytes, connectivity metadata, and graph metadata under a versioned manifest. Runtime cache file reads/writes use worker-safe helper jobs; cache misses enqueue worker generation rather than forcing synchronous Recast work.
- **Frame dirty orchestration:** `src/App` owns dirty flags for nav tile sync, nav connectivity, world graph rebuilds, renderer visibility metadata, and picking. Expensive derived systems should run when inputs change, not every frame. Renderer metadata reapply uses dirty chunk sets for local changes and a capped full-reapply queue only when global visibility/debug settings change.
- **Asset registry:** `Engine::AssetRegistry` owns CPU-only asset identity, source metadata, import settings identity, dependency edges, and stale/missing diagnostics. `AssetHandle` is a transient generation-counted runtime handle; `AssetId` is the stable registry identity intended for future scene/component serialization references. The registry does not import payloads, allocate renderer resources, replace path-based loaders, or own `AssetCache` entries.
- **Asset cache:** `Engine::AssetCache` deduplicates reusable renderer meshes and textures; terrain draw buffers remain transient.

## Available Rendering Features

- **Renderer resources:** Static meshes, generated fallback cube mesh, runtime textures, solid textures, terrain tiles, materials, render groups, and mesh instances are exposed through renderer-owned handles.
- **Materials:** `Renderer::MaterialHandle` supports base color texture/factor, optional normal texture, metallic, and roughness factors. Material assignment is preferred over texture-only overrides.
- **Scene interface:** `Renderer::RenderView` carries view ID, matrices, camera position, viewport, layer mask, and distance-culling toggle into scene submission.
- **Culling and layers:** Renderer submission supports visibility flags, render layers, frustum culling, max draw distance, and stats for culled/submitted mesh instances and terrain tiles.
- **Render groups:** Renderer render groups tag terrain/instances with chunk metadata for stats and debug grouping only. They never own chunk lifetime.
- **Batching:** Mesh draw items are sorted/grouped by mesh/submesh/material/render state as a conservative CPU batching path.
- **Terrain rendering:** Terrain tiles use generated heightfield meshes with LOD rebuilds and skirts. One material is assigned per tile from the primary chunk biome.
- **Atmosphere:** Renderer-owned sky clear color, exponential distance fog, directional sun color, direction, and intensity.
- **Debug UI:** Dear ImGui displays renderer stats, terrain LODs, spatial registry, camera, biomes, picking, interactions, save/edit controls, actor state, navigation, graph stats, and debug knobs.
- **Debug draw:** Transient colored line/AABB/XZ-rect/frustum primitives visualize selection, collision, chunk bounds, terrain bounds, nav tiles, navmesh edges, blockers, portals, graph routes, and actor movement.

## Initialization Order

1. **Platform and renderer boot**
   - Create SDL window.
   - Initialize bgfx.
   - Configure renderer vertex layouts.
   - Initialize scene renderer resources.
   - Initialize Dear ImGui debug UI.
   - Read and push initial renderer atmosphere settings.
   - Set initial bgfx reset/view clear/view rect.

2. **Runtime assets and descriptors**
   - `Engine::AssetRegistry` may be created by tools or future scene composition code to track stable source metadata. The current sample boot path still uses path-based imports and `AssetCache` directly.
   - Create `Engine::AssetCache`.
   - Acquire fallback/player/debug solid textures and fallback mesh.
   - Load object archetypes from `assets/config/object_archetypes.yaml`, falling back to defaults on failure.
   - Load biomes from `assets/config/biomes.yaml`, falling back to defaults on failure.
   - Create renderer materials for archetypes and biome terrain colors.

3. **Engine services**
   - Load the active navigation profile from `assets/config/navigation_profiles.yaml`, falling back to defaults on failure.
   - Create `World`, `ActorController`, `BlockingCollisionSystem`, `SpatialRegistry`, `TerrainSystem`, `NavigationSystem`, `NavigationConnectivitySystem`, `WorldNavigationGraph`, `NavigationCache`, `WorldObjectOverrides`, `AsyncWorkQueue`, `FrameBudget`, `MainThreadWorkQueue`, `ChunkStreamer`, input/event/interaction systems, and camera. `Scene` is available as an isolated runtime container but is not part of the sample boot path yet.
   - Use one consistent chunk size across spatial registry, terrain, procedural content, chunk streamer, persistent editor, and world graph.
   - Configure camera bounds and terrain LOD distances before creating chunks.

4. **Chunk, nav, and graph bootstrap**
   - Create chunk factory from archetypes, materials, terrain material resolver, overrides, and spatial registry.
   - Queue initial chunks around the camera pivot and commit completed generated chunks under the configured per-frame budget.
   - Insert worker-built or cached loaded nav tile bytes from terrain plus blocking prop bounds as chunks commit.
   - Rebuild loaded navigation connectivity from loaded nav tiles.
   - Rebuild the coarse world navigation graph around the active graph center.
   - Update terrain LODs and apply renderer visibility/material metadata.

5. **Player and demo actors**
   - Create player world object with `ObjectId::player()`, renderer instance, CPU bounds, and actor state.
   - Create demo squad actor objects and register them in world, renderer, actor controller, and spatial registry.
   - Call `World::syncRenderState()` after initial object transforms are valid.

6. **Frame loop**
   - Begin frame timer and input frame.
   - Feed SDL events to Dear ImGui first; only feed game input when ImGui does not capture mouse/keyboard.
   - Update window size/bgfx reset/view settings as needed.
   - Publish semantic input events.
   - Update camera from semantic events and current player target.
   - Stage chunk streaming: enqueue missing desired chunks, cancel stale pending work, enqueue budgeted load/unload commits, and mark derived systems dirty when loaded membership changes.
   - Begin the main-thread frame budget, drain budgeted commit/rebuild work, sync nav tiles, rebuild connectivity/graph, update terrain LODs, and apply renderer visibility only when their dirty flags require it.
   - Run fixed updates: world angular velocity, player actor with real input, demo actors with empty input, spatial registry updates.
   - Sync world transforms to renderer.
   - Update picking and interaction events.
   - Handle movement commands, hierarchical route requests, local path requests, and coarse route debug state.
   - Enqueue debug primitives.
   - Draw scene, draw debug primitives, draw Dear ImGui, submit `bgfx::frame()`.
   - Process debug save/load/rebuild/edit requests and clear the event queue.

7. **Shutdown**
   - Clear navigation/graph/connectivity state.
   - Unload chunks and clear spatial registry.
   - Destroy renderer materials.
   - Release cached textures/meshes and shut down `AssetCache`.
   - Shut down Dear ImGui, scene renderer, bgfx, SDL window, and SDL.

## Cross-System Contracts

- **Scene runtime identity:** Scene actor and component runtime handles are transient, generation-counted values and must never be serialized. `SceneObjectId` is stable scene identity reserved for future serialization and remains distinct from runtime handles; it is not wired to save/load in the scene kernel phase. Future asset handles identify asset registry records and are distinct from renderer handles. Renderer handles remain renderer-owned GPU/resource identifiers and must not become scene/component storage IDs. Future physics and navigation handles are owned by their systems and are distinct from actor/component handles. Existing `ObjectId` remains the procedural world/save identity until the scene serialization phase defines an explicit migration.
- **Scene transform hierarchy:** `Scene` actor transforms use local TRS data, cached world matrices, parent/child links, and dirty propagation. Hierarchy mutation is main-thread runtime state only; future authored-scene, renderer, physics, navigation, and serialization phases must consume it through explicit bridge APIs rather than reading storage directly.
- **Scene tick scheduler:** `Scene` scheduler phases are synchronous main-thread callbacks with explicit lifecycle transitions and fixed/variable phase order. Existing `FixedStepLoop` and App frame code still own fixed timestep accumulation and decide when to call scene fixed ticks. Future renderer, physics, navigation, animation, scripting, and behavior systems must register through the scheduler rather than adding hidden app-local ordering.
- **Scene render bridge to renderer:** Scene render bridge component handles are transient and must not be serialized. The bridge may create renderer mesh/skinned instances and lights through `SceneRenderBackend`, but renderer handles remain renderer-owned resources. Camera components only build `Renderer::RenderView` data; App still owns bgfx view setup and active-camera selection. Phase 5 uses direct renderer handles only for active resource binding; registry/cache-backed resource resolution is deferred.
- **Authored scene adapter ownership:** The scene authored adapter owns renderer static meshes, renderer materials, and cached texture acquisitions it creates from imported scene CPU data. Callers must release bridge-created renderer instances/lights through `SceneRenderBridge` and release adapter-owned resources through `releaseSceneAuthoredAdapterResources` before shutting down `AssetCache` or Renderer. Imported node hierarchy and local transforms are copied into `Scene`; existing authored scene runtime owners remain unchanged.
- **Animated scene adapter ownership:** The scene animated adapter owns renderer skinned meshes, optional bind-pose meshes, renderer materials, cached texture acquisitions, skeleton records, and animator records it creates from imported animated CPU data. Callers must release bridge-created skinned instances through `SceneRenderBridge` and release adapter-owned resources through `SceneAnimatedModelAdapter::releaseResources` before shutting down `AssetCache` or Renderer. Animator updates run only through explicit adapter calls or an optional `VariableAnimation` scene scheduler system; pre-render renderer sync remains owned by `SceneRenderBridge`.
- **Asset registry identity:** Asset registry records have two identities: transient `AssetHandle` values for runtime lookup and stable `AssetId` values for durable references. Asset IDs are derived from canonical source path, asset type, and import settings unless an explicit tool/test ID is supplied. Dependency edges store `AssetId`s and resolve to live handles only when the referenced assets are currently registered. Registry metadata is CPU-only; it must not become renderer resource ownership or an implicit import pipeline.
- **Asset registry to cache:** `AssetRegistry` tracks source identity and dependencies; `AssetCache` continues to own renderer mesh/texture reuse and explicit release before renderer shutdown. Registry records may describe assets that are missing, stale, generated, or not loaded into renderer memory.
- **World to renderer:** World objects may hold renderer instance handles, but renderer resources do not own world object lifetime. `World::syncRenderState()` is the transform handoff.
- **World to save:** Save files use `ObjectId`, never `WorldObjectHandle`. Procedural IDs are stable only if chunk coordinate derivation, archetype IDs, and local slot numbering remain stable.
- **Chunk to world:** Chunk streamer owns loaded membership lists. `World` owns the object records. Unloading a chunk must remove its objects from spatial registry and destroy renderer instances through world/chunk cleanup. Budgeted unloads should split navigation removal, object destruction, terrain destruction, render-group destruction, and final dirty marking into separate cooperative work items.
- **Async chunk generation:** Worker jobs may generate `GeneratedTerrainTileData`, `GeneratedChunkProp` descriptors, navigation build geometry, and Detour tile bytes from immutable snapshots. Main-thread commit must create render groups, terrain renderer handles, world objects, renderer instances, spatial entries, and live navigation tiles.
- **Budgeted main-thread work:** Deferrable main-thread work should be represented as cooperative `MainThreadWorkItem`s with a category and priority. Atomic work items may overrun the budget, but systems should split large operations into phases before enqueueing. Critical work may run over budget only when explicitly marked.
- **System inboxes:** Cross-system message flow should use receiver-owned typed inboxes and explicit publish-only sink references when direct calls are too tightly coupled. App or an explicit scheduler owns flush order. Avoid global dynamic event bus registries unless a future feature documents concrete ownership, lifetime, type, threading, and shutdown rules.
- **Terrain to gameplay:** Gameplay height, picking, prop placement, diagnostics, and actor grounding use CPU terrain data, not renderer LOD meshes. Biome terrain-shape fields are save/cache-affecting generation inputs.
- **Terrain to renderer:** TerrainSystem owns CPU tiles and creates/destroys renderer terrain handles. Renderer only owns the active draw buffers it is handed. Renderer LOD rebuilds are deferrable visual work: workers may generate `TerrainRenderMeshData` from immutable CPU tile snapshots, but bgfx handle replacement and material/render metadata restoration remain main-thread work. Gameplay height queries continue to use CPU terrain while out-of-budget or pending tiles temporarily keep their previous renderer LOD.
- **CPU-only terrain fixtures:** `TerrainSystem` may be configured not to create renderer resources and may create tiles from explicit height arrays. This mode is for headless tests/tools and must preserve the same CPU height sampling and navigation build data contracts as runtime terrain.
- **Navigation local layer:** `NavigationSystem` only knows loaded nav tiles. Query failure states such as `NoTile`, `NoNearestPoly`, and `NoPath` are normal gameplay outcomes. Missing runtime tiles should become pending cache/worker work, not synchronous Recast builds.
- **Navigation connectivity:** Connectivity is derived metadata from loaded local nav tiles. Each connected portal stores the exact paired neighbor portal position; debug links and graph routes must use that explicit pair rather than searching for any connected neighbor portal. Connectivity must update after nav tile insert/destroy/clear and must not load or unload chunks. Runtime dirty connectivity updates should use `beginRebuild`/`stepRebuild` so Detour portal sampling is cooperative and budgetable by edge/sample count. The world graph must wait until the active connectivity rebuild handle completes before consuming refreshed connectivity data.
- **World graph:** Coarse graph can generate nodes beyond loaded chunks, but it must not build local nav tiles, stream chunks, or move actors. Runtime graph construction should use immutable terrain/biome/connectivity snapshots on worker threads, then swap plain `WorldNavigationGraphCacheData` into the live graph on the main thread. Loaded neighboring chunks may have multiple graph edges, one per connected portal pair, so route planning can choose a nearby usable portal instead of a single chunk-pair default edge. Large graph rebuilds should be coalesced and recentered only after the camera/route center moves a configurable chunk threshold, not every single chunk crossing.
- **Dirty derived data:** Chunk streaming, terrain LOD rebuilds, explicit nav rebuilds, save/load, and persistence edits are responsible for marking dependent dirty flags. Dirty processing should enqueue coalesced budgeted work instead of immediately rebuilding when the result can wait. Standing still should not rebuild nav tiles, connectivity, graph data, or renderer visibility metadata. If a dirty pass can touch many chunks, it should expose a per-frame cap and report deferred work through debug stats.
- **Navigation cache:** Cache files are derived baseline data, not save data. Manifest identity must include settings and source config inputs that affect generated navigation. Chunks with save-backed object overrides bypass baseline cache unless the user explicitly refreshes cache records through debug tools. Cache file reads/writes should run through worker-safe helpers using immutable settings/manifest snapshots; the live `NavigationCache` facade remains main-thread stats/debug state only. Async chunk jobs may use baseline cache hits to avoid Recast generation, but live tile insertion remains main-thread only. Cache write-through defaults off so disk I/O does not create surprise frame hitches.
- **Navigation diagnostics:** Tile, portal, and actor command diagnostics are read-only Engine data for debug/tuning. They may explain existing decisions but must not change pathfinding, movement, chunk streaming, persistence, or cache validity by themselves.
- **Actors and navigation:** Actors own local Detour path state and hierarchical route state. Move commands try a direct local Detour path first. If direct local pathing fails, actors request a coarse route; same-chunk failures may use an adjacent-chunk detour that leaves and re-enters through portal pairs. Chunk transitions use paired source/neighbor portal waypoints tagged with their intended chunk, tile-specific waypoint snapping, and short validated seam bridge movement so actors can cross between adjacent loaded tiles without requiring perfect Detour edge stitching. If the required local nav tile is unavailable, the actor waits or fails without teleporting or forcing streaming.
- **Streaming and routes:** Chunk streaming remains camera/App-owned. Hierarchical movement may observe loaded tile availability, but actors must not directly load chunks or build nav tiles.
- **Collision:** Runtime collision uses world CPU bounds and collision flags. Renderer bounds are for rendering/culling and must not become gameplay authority.
- **Input:** Engine behavior consumes semantic input actions from `EventQueue`. New gameplay controls should be added to `InputMapping`/YAML rather than hardcoded SDL checks. If input starts feeding durable commands into a system-owned inbox, the mapping remains the producer and the receiving system owns the inbox/flush contract.
- **Interactions:** Interaction systems publish/resolve outcomes; App applies world, renderer, and persistence mutations through Engine services.
- **Debug UI:** Dear ImGui owns debug display/knobs. Debug knobs should mutate runtime state through public Engine/Renderer APIs and should not become hidden simulation state.
- **Debug draw:** Debug primitives are transient diagnostic geometry. Expensive categories must be capped and report generated/submitted/clipped counts; clipped debug visualization is acceptable and must not affect gameplay, navigation, rendering culling, or persistence.
- **Long-frame diagnostics:** Debug builds should preserve enough frame timing context to identify rare hitches. The long-frame recorder is diagnostic only and must not change simulation, streaming, renderer, cache, or navigation behavior.
- **Release debug tools:** `MANUAL_ENGINE_ENABLE_DEBUG_TOOLS` is `1` for Debug and `0` for Release. Release builds stub Dear ImGui, skip debug primitive gathering/submission, and do not link the ImGui target.
- **Picking cadence:** Debug builds continuously update hover picking for inspection. Release builds run picking only for commands that require a fresh target, such as select/interact/place/move clicks.
- **Renderer visibility:** Render layers, visibility flags, render groups, materials, and max draw distance affect submission only. They must not drive simulation, chunk lifetime, or persistence. App-owned visibility metadata updates should be incremental by chunk when possible; full loaded-set reapplies are reserved for global debug setting changes and must be budgeted.
- **Asset cache:** Cache-owned renderer assets must be explicitly released before renderer shutdown. Terrain tile resources are transient and stay outside the cache. `AssetId` and `AssetHandle` are not renderer handles and must not be released through renderer or cache APIs.

## Update Rule

Before adding or changing a feature that touches more than one subsystem, consult this guide and `docs/engine_overview.md`. Update this guide in the same change when work adds:

- a new gameplay system or renderer feature;
- a new public API/interface contract;
- a new initialization/shutdown dependency;
- a new save-facing identity or serialization rule;
- a changed ownership boundary between App, Engine, Renderer, or Assets;
- a new debug control that mutates runtime behavior.
