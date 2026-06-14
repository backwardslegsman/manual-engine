# ManualEngine Systems And Contracts

This guide is the quick reference for what the prototype currently supports and how the major systems are expected to be wired together. Consult it before adding features that cross subsystem boundaries, and update it whenever a new feature adds an interface contract, ownership rule, or initialization dependency.

## Available Gameplay Systems

- **World ownership:** `Engine::World` owns transient world object handles, transforms, stable object IDs, local/world CPU bounds, collision flags, angular velocity, and optional renderer instance bindings.
- **Stable identity:** `Engine::ObjectId` is the persistence-facing identity. `WorldObjectHandle` is transient and must not be saved.
- **Fixed-step loop:** `Engine::FixedStepLoop` provides clamped frame timing and 60 Hz fixed update ticks.
- **Input mapping:** SDL events flow into `Engine::InputState`; `Engine::InputMapping` publishes semantic input actions from `assets/config/input.yaml`.
- **Event queue:** `Engine::EventQueue` carries frame input action events and interaction events. It is explicitly cleared at the end of the frame.
- **Camera:** `Engine::OrbitCameraController` supports sim free camera and player-follow mode, with configurable pivot bounds, zoom, pitch, panning, and follow offset.
- **Actors:** `Engine::ActorController` owns kinematic actor state, manual movement, path-following state, facing, terrain grounding, and collision-aware movement.
- **Actor selection and commands:** `Engine::ActorSelection` stores selected actors; `Engine::formationOffsetForActorIndex` provides deterministic simple formation slots.
- **Blocking collision:** `Engine::BlockingCollisionSystem` resolves actor cylinder-vs-world-AABB blocking through the sparse spatial registry. It is not a physics engine.
- **Spatial registry:** `Engine::SpatialRegistry` is a sparse XZ grid for debug/gameplay lookup. It does not own world object lifetime.
- **Chunk streaming:** `Engine::ChunkStreamer` owns loaded chunk membership and chunk content handles. It loads/unloads synchronously around a center point.
- **Terrain:** `Engine::TerrainSystem` owns loaded CPU heightfield tiles, height queries, terrain raycasts, optional renderer terrain handles, terrain LOD rebuilds, and navigation build data extraction.
- **Biomes:** `Engine::BiomeSystem` deterministically samples biome IDs and debug noise values from world coordinates and chunk coordinates.
- **Procedural content:** `Engine::ProceduralChunkContentConfig` produces deterministic prop spawn descriptors from chunk coord, biome rules, archetype IDs, and stable local slots.
- **Archetypes:** `Engine::ObjectArchetypeCatalog` loads YAML object descriptors with visual defaults, bounds, tags, terrain offset, resource metadata, and collision/interaction tags.
- **Persistence:** `Engine::WorldObjectOverrides` stores removed procedural IDs and persistent object overrides; `Engine::WorldState` saves high-level world state only.
- **Persistent editing:** `Engine::PersistentObjectEditor` mutates save-backed overrides for selected object transforms, removal/reset, and placement.
- **Picking and selection:** `Engine::Picking` builds cursor rays and performs CPU object/terrain picking for debug selection and interactions.
- **Interactions:** `Engine::InteractionSystem` turns semantic input plus picking into target-aware interaction events; `Engine::InteractionHandlerSystem` resolves tag-based outcomes.
- **Navigation profiles:** `assets/config/navigation_profiles.yaml` selects one active player-sized agent/build profile for nav tile generation and queries.
- **Navigation local layer:** `Engine::NavigationSystem` wraps Recast/Detour. It builds loaded terrain nav tiles, supports nearest-point/path queries, exposes plain diagnostics, and keeps Recast/Detour private to implementation.
- **Navigation connectivity:** `Engine::NavigationConnectivitySystem` derives loaded-chunk edge portals and neighbor links from local nav tiles and records per-edge portal sampling diagnostics.
- **World navigation graph:** `Engine::WorldNavigationGraph` builds a deterministic coarse chunk graph over the generated world and returns coarse routes used by actor hierarchical move commands.
- **Navigation cache:** `Engine::NavigationCache` stores derived baseline nav tile bytes, connectivity metadata, and graph metadata under a versioned manifest. Cache misses fall back to synchronous generation.
- **Frame dirty orchestration:** `src/App` owns dirty flags for nav tile sync, nav connectivity, world graph rebuilds, renderer visibility metadata, and picking. Expensive derived systems should run when inputs change, not every frame.
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
   - Create `Engine::AssetCache`.
   - Acquire fallback/player/debug solid textures and fallback mesh.
   - Load object archetypes from `assets/config/object_archetypes.yaml`, falling back to defaults on failure.
   - Load biomes from `assets/config/biomes.yaml`, falling back to defaults on failure.
   - Create renderer materials for archetypes and biome terrain colors.

3. **Engine services**
   - Load the active navigation profile from `assets/config/navigation_profiles.yaml`, falling back to defaults on failure.
   - Create `World`, `ActorController`, `BlockingCollisionSystem`, `SpatialRegistry`, `TerrainSystem`, `NavigationSystem`, `NavigationConnectivitySystem`, `WorldNavigationGraph`, `NavigationCache`, `WorldObjectOverrides`, `ChunkStreamer`, input/event/interaction systems, and camera.
   - Use one consistent chunk size across spatial registry, terrain, procedural content, chunk streamer, persistent editor, and world graph.
   - Configure camera bounds and terrain LOD distances before creating chunks.

4. **Chunk, nav, and graph bootstrap**
   - Create chunk factory from archetypes, materials, terrain material resolver, overrides, and spatial registry.
   - Stream initial chunks around the camera pivot.
   - Build loaded nav tiles from terrain plus blocking prop bounds.
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
   - Stream chunks and mark derived systems dirty when chunk membership changes.
   - Sync nav tiles, rebuild connectivity/graph, update terrain LODs, and apply renderer visibility only when their dirty flags require it.
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

- **World to renderer:** World objects may hold renderer instance handles, but renderer resources do not own world object lifetime. `World::syncRenderState()` is the transform handoff.
- **World to save:** Save files use `ObjectId`, never `WorldObjectHandle`. Procedural IDs are stable only if chunk coordinate derivation, archetype IDs, and local slot numbering remain stable.
- **Chunk to world:** Chunk streamer owns loaded membership lists. `World` owns the object records. Unloading a chunk must remove its objects from spatial registry and destroy renderer instances through world/chunk cleanup.
- **Terrain to gameplay:** Gameplay height, picking, prop placement, and actor grounding use CPU terrain data, not renderer LOD meshes.
- **Terrain to renderer:** TerrainSystem owns CPU tiles and creates/destroys renderer terrain handles. Renderer only owns the active draw buffers it is handed.
- **CPU-only terrain fixtures:** `TerrainSystem` may be configured not to create renderer resources and may create tiles from explicit height arrays. This mode is for headless tests/tools and must preserve the same CPU height sampling and navigation build data contracts as runtime terrain.
- **Navigation local layer:** `NavigationSystem` only knows loaded nav tiles. Query failure states such as `NoTile`, `NoNearestPoly`, and `NoPath` are normal gameplay outcomes.
- **Navigation connectivity:** Connectivity is derived metadata from loaded local nav tiles. Each connected portal stores the exact paired neighbor portal position; debug links and graph routes must use that explicit pair rather than searching for any connected neighbor portal. Connectivity must be rebuilt after nav tile sync/rebuild/clear and must not load or unload chunks.
- **World graph:** Coarse graph can generate nodes beyond loaded chunks, but it must not build local nav tiles, stream chunks, or move actors. Loaded neighboring chunks may have multiple graph edges, one per connected portal pair, so route planning can choose a nearby usable portal instead of a single chunk-pair default edge.
- **Dirty derived data:** Chunk streaming, terrain LOD rebuilds, explicit nav rebuilds, save/load, and persistence edits are responsible for marking dependent dirty flags. Standing still should not rebuild nav tiles, connectivity, graph data, or renderer visibility metadata.
- **Navigation cache:** Cache files are derived baseline data, not save data. Manifest identity must include settings and source config inputs that affect generated navigation. Chunks with save-backed object overrides bypass baseline cache unless the user explicitly refreshes cache records through debug tools.
- **Navigation diagnostics:** Tile, portal, and actor command diagnostics are read-only Engine data for debug/tuning. They may explain existing decisions but must not change pathfinding, movement, chunk streaming, persistence, or cache validity by themselves.
- **Actors and navigation:** Actors own local Detour path state and hierarchical route state. Move commands try a direct local Detour path first. If direct local pathing fails, actors request a coarse route; same-chunk failures may use an adjacent-chunk detour that leaves and re-enters through portal pairs. Chunk transitions use paired source/neighbor portal waypoints tagged with their intended chunk, tile-specific waypoint snapping, and short validated seam bridge movement so actors can cross between adjacent loaded tiles without requiring perfect Detour edge stitching. If the required local nav tile is unavailable, the actor waits or fails without teleporting or forcing streaming.
- **Streaming and routes:** Chunk streaming remains camera/App-owned. Hierarchical movement may observe loaded tile availability, but actors must not directly load chunks or build nav tiles.
- **Collision:** Runtime collision uses world CPU bounds and collision flags. Renderer bounds are for rendering/culling and must not become gameplay authority.
- **Input:** Engine behavior consumes semantic input actions from `EventQueue`. New gameplay controls should be added to `InputMapping`/YAML rather than hardcoded SDL checks.
- **Interactions:** Interaction systems publish/resolve outcomes; App applies world, renderer, and persistence mutations through Engine services.
- **Debug UI:** Dear ImGui owns debug display/knobs. Debug knobs should mutate runtime state through public Engine/Renderer APIs and should not become hidden simulation state.
- **Release debug tools:** `MANUAL_ENGINE_ENABLE_DEBUG_TOOLS` is `1` for Debug and `0` for Release. Release builds stub Dear ImGui, skip debug primitive gathering/submission, and do not link the ImGui target.
- **Picking cadence:** Debug builds continuously update hover picking for inspection. Release builds run picking only for commands that require a fresh target, such as select/interact/place/move clicks.
- **Renderer visibility:** Render layers, visibility flags, render groups, and max draw distance affect submission only. They must not drive simulation, chunk lifetime, or persistence.
- **Asset cache:** Cache-owned renderer assets must be explicitly released before renderer shutdown. Terrain tile resources are transient and stay outside the cache.

## Update Rule

Before adding or changing a feature that touches more than one subsystem, consult this guide and `docs/engine_overview.md`. Update this guide in the same change when work adds:

- a new gameplay system or renderer feature;
- a new public API/interface contract;
- a new initialization/shutdown dependency;
- a new save-facing identity or serialization rule;
- a changed ownership boundary between App, Engine, Renderer, or Assets;
- a new debug control that mutates runtime behavior.
