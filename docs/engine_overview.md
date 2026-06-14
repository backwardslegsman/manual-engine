# ManualEngine Overview

ManualEngine is currently a deliberately small SDL + bgfx open-world prototype. The guiding shape is: `src/App` composes services and owns platform/frame flow, `src/Engine` owns deterministic simulation and world-state rules, `src/Renderer` owns bgfx resources/submission, and `src/Assets` decodes source files into CPU-side data.

Keep this document current whenever subsystem ownership, data flow, or save-facing contracts change. Use `docs/system_contracts.md` as the detailed checklist for currently available systems, renderer features, initialization order, and cross-system contracts.

## Runtime Ownership

- App owns SDL window lifetime, bgfx init/shutdown, frame pumping, debug UI composition, sample asset/material creation, and explicit service wiring.
- Engine owns world object lifetime, stable object identity, fixed-step actor updates, chunk streaming, terrain CPU data, procedural content rules, biome sampling, input mapping, interaction events, picking helpers, persistence records, and simple debug/editor mutations.
- Renderer owns GPU resources, renderer handles, materials, terrain draw buffers, mesh instances, render groups, culling, draw batching, atmosphere state, and Dear ImGui rendering.
- Asset cache is an Engine service that deduplicates reusable renderer mesh/texture handles. Terrain draw buffers remain transient and are not cached.

## Main Data Flow

- SDL events are fed to Dear ImGui first. If ImGui wants mouse or keyboard capture, the game input snapshot does not receive those events.
- `InputState` records physical SDL input for the frame. `InputMapping` converts it to semantic `InputActionEvent`s.
- Camera, actor controller, and interaction systems consume semantic events rather than physical keys/buttons.
- `World` owns transient object handles, transforms, optional stable `ObjectId`s, CPU bounds, collision flags, angular velocity, and explicit renderer instance bindings.
- `World::syncRenderState()` pushes CPU transforms into renderer instances. Renderer resources never own world object lifetime.
- Chunk streaming owns loaded chunk membership, render group metadata, terrain tile handles, and prop object lists. It does not own mesh/material/texture assets.
- Terrain is generated as deterministic CPU heightfield tiles with renderer LOD meshes rebuilt as needed. Gameplay height queries use CPU tile data, not active render LOD.
- Navigation is an Engine service wrapped by `NavigationSystem`; Recast/Detour types stay behind that boundary. V1 navigation tiles are synchronous loaded-chunk tiles built from terrain plus conservative static blocker bounds, and actors can follow nav paths through Engine-owned path state. `NavigationConnectivitySystem` derives coarse loaded-chunk border portals above those local tiles, and `WorldNavigationGraph` builds deterministic chunk-level routes for hierarchical actor move commands. `NavigationCache` stores versioned derived baseline tile/connectivity/graph artifacts and falls back to synchronous generation on misses. Actors consume coarse routes by requesting local Detour paths to route waypoints; chunk streaming remains camera/App-owned.
- App treats navigation tiles, connectivity, world graph data, renderer visibility metadata, and picking as derived work behind dirty flags. Chunk changes, explicit rebuilds, terrain LOD rebuilds, save/load, and persistent edits mark the relevant systems dirty; stationary frames should not repeat those expensive rebuilds.
- Simple group-command formation math lives in Engine actor command helpers so App composition does not become the owner of reusable movement rules.
- V1 terrain material assignment is one renderer material per tile, selected from the primary chunk biome's configured terrain color. Biome boundaries are not blended yet.
- Procedural props are generated from chunk coord, biome rules, archetype descriptors, and explicit local slots. Stable procedural IDs use chunk coord + archetype ID + slot.
- Save/load stores high-level state: player position, camera state, settings, persistent object overrides, removed object IDs, and custom object serials. Baseline terrain and procedural content regenerate.

## Current Feature Surface

- Static mesh and texture loading through renderer APIs and `AssetCache`.
- Renderer material handles with base color, normal, metallic, and roughness factors.
- Renderer scene submission through `RenderView`, with CPU frustum culling, distance culling, render layers, render groups, draw stats, and conservative CPU batching by mesh/material.
- Renderer atmosphere state: sky clear color, exponential fog, and directional sun settings.
- Renderer debug draw primitives are transient per-frame submission aids for bounds, chunk borders, frustums, actor movement diagnostics, and navigation visualization.
- Dear ImGui debug panel for renderer stats, world save/edit controls, camera, biomes, picking, interactions, terrain LOD, spatial registry, and player actor status.
- Debug builds keep continuous hover picking and timing visibility. Release builds disable debug UI/draw hooks and update picking only for commands that need a fresh target.
- Fixed-step engine loop, world object ownership, kinematic actors, reusable actor path-following state, simple blocking collision, and terrain grounding.
- Sim/orbit camera with free and player-follow modes.
- Sparse grid spatial registry for gameplay/debug queries.
- CPU debug picking against object bounds and loaded terrain.
- Deterministic chunk streaming with terrain tiles and biome-driven procedural props.
- Recast/Detour loaded-chunk navigation tile generation with static prop blockers, nearest/path query support, loaded-chunk portal connectivity metadata, generated coarse world navigation graph/routes, derived navigation cache records, hierarchical route-following commands for the player or selected demo actors, right-click object interaction, semantic stop/cancel commands, and navigation debug draw/tuning controls.
- YAML-backed input mapping, object archetype definitions, and biome definitions with safe fallback defaults.
- Stable `ObjectId` identity and save-backed persistent object overrides/removals.
- Tag-based interaction handling for inspectable, removable, and resource-node archetypes.

## Save-Facing Contracts

- `WorldObjectHandle` is transient and must not be serialized.
- `ObjectId` is the save/persistence key.
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
