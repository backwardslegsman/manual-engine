# Scene And Component Runtime Roadmap

This roadmap consolidates the existing procedural world, authored scene, animated model, renderer, navigation, cache, and async work into a shared scene/component runtime. It is not a greenfield rewrite. Existing systems should be adapted behind stable scene, asset, and component contracts in small steps.

Use this document for long-range architectural work that crosses `src/Engine`, `src/Assets`, `src/Renderer`, and `src/App`. Keep `docs/authored_scene_roadmap.md`, `docs/animation_roadmap.md`, and `docs/navigation_roadmap.md` for subsystem-specific detail.

## Principles

- Keep `Renderer` handle-based. Scene components submit through engine systems; renderer code should not consume component storage directly.
- Keep source import CPU-only. Importers produce data and records, not renderer handles.
- Distinguish runtime handles, stable serialized IDs, asset handles, renderer handles, physics handles, and navigation handles.
- Preserve synchronous APIs while adding async/cache-backed paths where needed.
- Add debug and diagnostics with each phase instead of deferring all visibility to the end.
- Keep heavy optional assets out of normal tests unless a change specifically depends on large-asset behavior.

## High-Level Roadmap

1. Scene kernel: scene, actor/entity handles, component storage, ownership, and lifecycle shell.
2. Transform hierarchy: local/world transforms, parent/child links, roots, attach/detach, dirty propagation.
3. Tick scheduler: explicit scene lifecycle and ordered frame phases.
4. Asset registry: asset handles, metadata, import records, dependency tracking, cache identity.
5. Render component bridge: mesh, skinned mesh, camera, light, and material-reference components submit through renderer-facing systems.
6. Authored scene adapter: load imported static scenes into scene actors/components while preserving current authored-scene owner paths.
7. Animated model adapter: integrate skeletons, clips, animator state, and skinned rendering as components.
8. Navigation runtime API: expose pathfinding, projection, agent config, and debug draw through scene-friendly services.
9. Navigation scene geometry: build/update navmesh input from static scene geometry, terrain, and transforms.
10. Physics integration: add Jolt-backed static colliders, capsule colliders, queries, transform sync, and debug draw.
10.5. Terrain runtime alignment: reconcile the new terrain dataset, navigation, physics, material, and serialization prep adapters with scene ownership before character movement depends on them.
11. Character movement: capsule movement, grounding, slope/step logic, path following, and movement variables.
12. Reflection and opaque APIs: property metadata, getter/setter access, system boundaries, script visibility.
13. Serialization: scenes, actors, components, hierarchy, asset references, exposed variables, and scene config.
14. Native behavior hooks: scene, actor, and component lifecycle/update hooks.
15. Lua scripting: Lua lifecycle hooks, opaque handles, reflected variables, and exposed system APIs.
16. Debug visualization unification: transforms, navmesh, paths, physics, skeletons, cameras, resources, and scene state.

## 1. Scene Kernel Roadmap

Goal: Introduce a central runtime container without replacing existing systems in one pass.

Phase 1 implementation note: the initial kernel uses `SceneActorHandle` instead of `ActorHandle` to avoid the existing procedural actor-controller handle name. Component storage is metadata-only and keyed by caller-supplied `SceneComponentTypeId`; transforms, renderer bridges, registry integration, serialization, physics, scripting, navigation, names, and tags remain later-phase work.

- Add `SceneHandle`, `ActorHandle`, `ComponentHandle`, and generation-counted validity.
- Add stable `SceneObjectId` or equivalent serialized identity separate from transient handles.
- Follow the handle and identity rules in `docs/system_contracts.md` before adding storage or serialization-facing APIs.
- Add `Scene` ownership for actors and component storage.
- Add actor lifecycle states: created, active, pending destroy, destroyed.
- Add component lifecycle states and explicit owner actor relationship.
- Add minimal component type registration keyed by stable type IDs.
- Add scene-local name/tag metadata only after handles and IDs are stable.
- Add tests for handle invalidation, double destroy, iteration while destroying, and stable ID preservation.
- Keep existing `World`, `AuthoredScene`, and `AnimatedModel` owners operational during this phase.

Exit criteria:

- A scene can create/destroy actors and attach opaque components.
- Runtime handles are safe against stale reuse.
- No renderer, physics, nav, or scripting dependency is required.

## 2. Transform Hierarchy Roadmap

Goal: Make transforms a first-class scene feature that can replace ad hoc object/node transform ownership over time.

Detailed implementation plan: `docs/scene_runtime/phase_02_transform_hierarchy.md`.

- Add scene-owned actor transform data with local translation, rotation, scale, and cached world matrix.
- Use `SceneActorHandle` as the hierarchy identity; do not add separate transform handles or migrate `World` yet.
- Treat every active actor as transformable with an identity default; pending-destroy and stale actors are excluded from transform and hierarchy queries.
- Support roots, parent/child relationships, attach, detach, and reparent operations with explicit preserve-world versus keep-local behavior.
- Add dirty propagation from changed actors to descendants, lazy world-matrix refresh for individual queries, and deterministic full-scene update in root/child order.
- Reject invalid parents, self-parenting, and cycles without mutation, returning explicit status.
- On parent destruction, detach surviving children to roots while preserving their world transforms.
- Defer bounds hooks, renderer sync, authored-scene conversion, serialization, physics, navigation, scripting, names, tags, and editor UI to later phases.
- Add CPU-only scene tests for hierarchy composition, non-uniform scale, preserve-world operations, dirty propagation, deterministic roots/children, cycle rejection, destroy cleanup, pending-destroy exclusion, and slot reuse cleanup.

Exit criteria:

- Scene transforms can reproduce imported authored-scene node transforms.
- Systems can query world transforms without owning hierarchy logic themselves.

## 3. Tick Scheduler Roadmap

Goal: Formalize scene lifecycle and frame phase ordering while preserving existing fixed-step behavior.

Detailed implementation plan: `docs/scene_runtime/phase_03_tick_scheduler.md`.

- Add a renderer-independent scene lifecycle shell with explicit `load`, `start`, `stop`, and `unload` transitions.
- Add generation-counted scene system handles and narrow scheduler registration for lifecycle/tick callbacks.
- Define fixed phases separately from variable frame phases; keep fixed timestep accumulation owned by existing `FixedStepLoop`/App code.
- Run systems deterministically by registration order within each phase and reject registration mutation while the scene is started.
- Keep all scheduler callbacks main-thread/synchronous in this phase; worker jobs may only be future producers of plain data.
- Add pause semantics that skip simulation phases while allowing begin/render/end style phases to continue for diagnostics and visual prep.
- Refresh scene world transforms before the pre-render phase so future renderer bridges can consume stable matrices.
- Add plain Engine diagnostics for lifecycle state, phase counts, skipped work, callback counts, and phase timing without ImGui or Renderer dependencies.
- Defer task graphs, async scheduling, native actor/component hooks, Lua, renderer submission, physics, navigation, serialization, editor UI, and authored-scene integration to later phases.
- Add CPU-only scene tests for lifecycle transitions, phase order, deterministic system order, pause behavior, invalid transitions, stale system handles, transform refresh before pre-render, diagnostics, and actor destruction during callbacks.

Exit criteria:

- Scene systems can be ordered without app-local sequencing growing further.
- Existing renderer/nav/async commit rules remain explicit.

## 4. Asset Registry Roadmap

Goal: Create stable asset handles and import records that sit above current import/cache systems.

Detailed implementation plan: `docs/scene_runtime/phase_04_asset_registry.md`.

- Add CPU-only `Engine::AssetRegistry` ownership for stable asset identity and metadata records.
- Add generation-counted `AssetHandle` values and stable `AssetId` values distinct from scene, renderer, physics, navigation, and save-facing identities.
- Track source path, canonical path, source format, content hash, import settings identity, asset type, dependencies, and diagnostics without creating renderer resources.
- Keep `AssetCache` as the renderer-resource cache; do not replace authored-scene cache files, path-based importers, or current `AuthoredScene`/`AnimatedModel` owners.
- Add helper paths for imported scene metadata and dependency extraction from existing Assimp CPU import records.
- Add queries by handle, stable ID, canonical path, dependencies, dependents, type/status counts, and stale/missing diagnostics.
- Defer component migration, serialization, hot reload, async import, disk asset database, package formats, editor UI, and cache rewrite to later phases.
- Add CPU-only tests for stable registration, duplicate handling, import settings identity, stale/missing refresh, dependency queries, imported-scene dependency extraction, and target independence from Renderer/App/Navigation/Physics/scripting.

Exit criteria:

- Components can reference assets by handle or stable ID instead of raw paths.
- Existing path-based loaders still work as compatibility APIs.

## 5. Render Component Bridge Roadmap

Goal: Let scene components drive renderer handles without coupling renderer internals to component storage.

Detailed implementation plan: `docs/scene_runtime/phase_05_render_component_bridge.md`.

- Add an Engine-owned scene render bridge that maps scene actor/component records to existing renderer handles without making Renderer read scene storage.
- Add static mesh, skinned mesh, light, and camera component descriptors with explicit material, visibility, render layer, max draw distance, and render group fields where current renderer APIs support them.
- Use scene world transforms as the source for renderer instance, light, and camera placement, with bridge sync running during `PreRender` after transform refresh.
- Keep renderer resource ownership explicit: Renderer owns renderer handles, `AssetCache` owns cached renderer asset references, and the bridge owns only the mapping/lifetime of resources it creates or acquires.
- Support transitional direct renderer-handle references and path/`AssetCache` compatibility while leaving registry-backed resource resolution for a later adapter.
- Add dirty tracking for transform, resource, material, visibility, light, and camera updates without hiding import or allocation work behind generic scene setters.
- Preserve existing `World`, `AuthoredScene`, `PartitionedAuthoredScene`, `AnimatedModel`, `AssetCache`, renderer submission, render layers, render groups, material paths, and diagnostics behavior.
- Add tests with renderer facades or stubs for component creation, transform sync, descriptor update, removal, actor destruction cleanup, scheduler pre-render ordering, diagnostics, and no-leak shutdown.

Exit criteria:

- A scene actor with mesh/light/camera components can render through existing renderer APIs.
- Renderer does not know about actor or component storage.

## 6. Authored Scene Adapter Roadmap

Goal: Adapt existing static authored scene loading into the scene runtime without losing streaming/cache/async behavior.

Detailed implementation plan: `docs/scene_runtime/phase_06_authored_scene_adapter.md`.

- Add an Engine-owned adapter that consumes already-imported `Assets::Assimp::ImportedScene` CPU data and creates scene actors, hierarchy, render bridge mesh components, and render bridge light components.
- Preserve imported local transforms and node parent/child relationships in `Engine::Scene`; compare scene-computed world matrices against imported fixture world transforms in tests.
- Reuse existing imported-scene resource conversion helpers for vertex, material, texture, alpha, sampler, and fallback policy.
- Create renderer static mesh and material handles as explicit adapter resources, while renderer instances/lights remain owned by `SceneRenderBridge`.
- Keep eager `AuthoredScene`, `PartitionedAuthoredScene`, authored cache, async authored loading, and current App-authored runtime behavior unchanged.
- Diagnose but skip skinned mesh, skin, joint, and animation records; animated adaptation remains Phase 7.
- Defer sector streaming through scene actors, serialization, editor UI, physics, navigation geometry, and asset hot reload.
- Add fixture tests for node mapping, hierarchy/world transform preservation, mesh/light component creation, resource release, bridge sync, invalid references, and unchanged legacy authored loader behavior.

Exit criteria:

- Static authored scenes can appear as scene-managed actors/components.
- Existing release authored demo behavior remains intact.

## 7. Animated Model Adapter Roadmap

Goal: Integrate the existing animation pipeline as scene components.

Detailed implementation plan: `docs/scene_runtime/phase_07_animated_model_adapter.md`.

- Add a scene animated adapter that consumes already-imported or cached animated `Assets::Assimp::ImportedScene` CPU data and creates scene actors for imported nodes.
- Preserve imported node transforms and skeleton hierarchy through `Engine::Scene`; joint actors remain normal scene actors and runtime handles remain transient.
- Add scene-facing skeleton, animator, and skinned mesh binding records without introducing a generic ECS, animation graph, state machine, reflection, serialization, scripting, physics, or navigation coupling.
- Reuse existing `AnimatedModel` pose math, playback state, crossfade helpers, diagnostics conventions, material/texture mapping, renderer skinned mesh descriptors, cache payloads, and async import/cache outputs.
- Attach skinned mesh components through `SceneRenderBridge` and update their joint matrices from sampled poses during explicit adapter updates or a `VariableAnimation` scene scheduler system.
- Keep renderer resource ownership explicit: the adapter owns created skinned meshes, optional bind-pose meshes, materials, and cached textures; the render bridge owns live skinned instances.
- Preserve existing `AnimatedModel`, `AnimatedModelCache`, `AnimatedModelAsync`, debug App animated sample flow, and authored static scene behavior during migration.
- Defer asset registry resource resolution, root motion, locomotion, IK, retargeting, animation graphs, additive layers, morph targets, editor UI, Lua/native hooks, and serialization.
- Add fixture tests comparing adapter bind poses, sampled clip palettes, playback advancement, and crossfade output against the existing `AnimatedModel` runtime, plus bridge sync, scheduler ordering, invalid-reference diagnostics, cache-payload commit, and leak-free release.

Exit criteria:

- An actor can own an animator and skinned mesh component that plays an imported clip.
- Existing animated fixture still renders through the skinned shader path.

## 8. Navigation Runtime API Roadmap

Goal: Expose the existing navigation query surface through stable scene-friendly Engine APIs while preserving the current procedural chunk navigation implementation.

Scope:

- Add a runtime API layer for navigation queries, agent settings, query filters, path handles/results, debug requests, and diagnostics.
- Keep Recast/Detour private to `Navigation.cpp` and existing navigation implementation files; public callers should not include Detour headers or consume Detour refs.
- Keep Phase 8 query-only for scene integration. Do not build navmesh from scene render components yet; that is Phase 9.
- Preserve current `NavigationSystem`, `NavigationConnectivitySystem`, `WorldNavigationGraph`, `NavigationCache`, actor command pathing, chunk streaming, async tile builds, and debug UI behavior.
- Keep existing procedural world and actor navigation paths operational through adapters or compatibility calls while the new API is introduced.

Public API Shape:

- Add scene-friendly navigation identity types under `src/Engine`, likely in a narrow `NavigationRuntime.hpp` / `.cpp` pair:
  - `NavigationWorldHandle` or service-owned runtime handle if more than one navigation context is needed.
  - `NavigationAgentTypeId` for stable agent profile selection.
  - `NavigationQueryId` for optional diagnostics correlation, not required for synchronous use.
  - `NavigationPathHandle` only if cached path ownership is introduced; otherwise return value paths are preferred for Phase 8.
  - `NavigationQueryStatus::{Success, InvalidInput, NoNavigationData, NoTile, NoNearestPoly, NoPath, PartialPath, UnsupportedAgent, Cancelled}`.
  - `NavigationProjectionResult`, `NavigationPathResult`, `NavigationReachabilityResult`, and `NavigationRaycastResult`.
- Define `NavigationAgentConfig` as plain data:
  - radius, height, max step height, max slope, max climb, query extents, max path nodes, and optional profile ID.
  - default values should match the current active player navigation profile.
  - Treat configs as runtime data in Phase 8; asset registry-backed agent config assets can come later.
- Define `NavigationQueryFilter` as plain data:
  - area mask, include/exclude flags, max search radius, allow partial path, require loaded tiles, and optional cost overrides.
  - Keep unsupported filter fields ignored with diagnostics rather than exposing Detour filter internals.
- Define query API methods:
  - `projectPoint(position, agent, filter)`.
  - `findPath(start, goal, agent, filter)`.
  - `findPathFromActor(scene, actor, goal, agent, filter)` as a convenience that reads scene world transform only through public `Scene` APIs.
  - `raycast(start, end, agent, filter)` or `segmentCast` for navmesh line-of-sight.
  - `reachable(start, goal, agent, filter)` as a cheap status/result wrapper over projection/path tests.
  - `pathLength(path)` and `samplePath(path, distance)` helpers if paths are returned as value objects.
- Keep APIs synchronous and CPU-only in this phase. Async tile build/cache work remains owned by the existing chunk/navigation systems.

Service Ownership:

- Introduce an Engine-owned navigation runtime facade that can be composed beside `Scene`, for example `SceneNavigationService`.
- The facade should not own Recast tile generation in Phase 8. It should wrap or reference the existing live `NavigationSystem` and optional `NavigationConnectivitySystem`/`WorldNavigationGraph` where needed.
- Scene integration should use explicit wiring:
  - App or future composition code creates the service with references to the live navigation systems.
  - Scene actors can request paths through the service using `SceneActorHandle` plus public scene transform queries.
  - The service may register a scene scheduler system only for diagnostics/debug-request flushing, not hidden tile generation.
- Do not make `Scene` directly own navigation storage yet. Per-scene ownership can become real when Phase 9 scene geometry supplies nav input and scene lifetime controls navmesh lifetime.

Compatibility Layer:

- Keep existing actor move-command flow intact:
  - Direct local Detour path attempts still work.
  - Hierarchical route fallback still uses `WorldNavigationGraph`.
  - Chunk transition portal behavior remains unchanged.
- Add compatibility adapters so current actor/navigation command code can migrate call sites incrementally:
  - Convert current local path request inputs to `NavigationAgentConfig` and `NavigationQueryFilter`.
  - Convert existing path status/failure reasons to the new `NavigationQueryStatus`.
  - Preserve current waypoint formats until character movement is migrated.
- Existing debug UI may display new diagnostics but should not be required for API use.

Diagnostics And Debug Requests:

- Add plain Engine diagnostics for:
  - query counts by type and status;
  - last path/projection/raycast failure reason;
  - nearest-poly search extents;
  - path node count, length, partial/final status;
  - source tile/chunk availability;
  - agent profile/config used;
  - elapsed microseconds for query calls;
  - rejected invalid scene actor handles or stale inputs.
- Add debug request records that are independent of Renderer and ImGui:
  - draw navmesh polygons/edges for currently loaded tiles;
  - draw requested path corridor and sampled waypoints;
  - draw projection source/result points and failed search extents;
  - draw raycast hit or blocked segment;
  - draw portal links and coarse route segments when the service uses connectivity/graph data.
- The renderer/App debug layer may translate these requests to existing debug primitives later; Phase 8 should define the data and caps, not require new rendering behavior.

Main-Thread And Async Rules:

- Query APIs read live navigation data and should run on the main thread unless the existing systems explicitly provide immutable snapshots.
- Worker jobs may continue producing Detour tile bytes from immutable terrain/chunk inputs, but Phase 8 APIs must not trigger synchronous tile builds as a fallback.
- Missing tiles are normal outcomes and should return `NoTile`/`NoNavigationData` plus diagnostics, not block on generation.
- Debug request buffers should be frame-scoped or explicitly cleared by the owner to avoid unbounded growth.

Testing Plan:

- Add CPU-focused tests that use small deterministic navigation fixtures or existing test nav tile setup:
  - project valid points onto loaded nav data;
  - return `NoTile` or `NoNearestPoly` for missing/unreachable points;
  - find a local path and preserve deterministic waypoint order;
  - report partial or no path for blocked/disconnected areas;
  - validate reachability wrappers;
  - validate ray/segment projection success and blocked cases;
  - reject invalid agent configs, stale scene actors, invalid filters, and NaN inputs without mutation;
  - verify diagnostics counts/statuses and debug request records;
  - confirm no public header exposes Recast/Detour types;
  - confirm existing actor navigation and graph tests still pass.
- Keep tests independent from Renderer, App, Physics, scripting, and scene render bridge unless a specific scene transform convenience API is under test.

Deferred:

- Scene static mesh/terrain nav-source gathering and navmesh rebuilds from scene geometry.
- Navigation components, agent components, and path-following character movement.
- Asset registry-backed navigation profiles.
- Serialization of navigation agent settings or runtime paths.
- Editor/debug UI controls beyond data exposure.
- Off-main-thread query snapshots.
- Dynamic obstacle avoidance, crowd simulation, steering, and locomotion.

Exit criteria:

- Gameplay or scene systems can request projection, local paths, reachability, and raycasts without depending on procedural chunk internals or Detour types.
- Query failures are explicit, diagnosable, and do not force synchronous tile builds.
- Existing procedural actor path following, chunk navigation cache, async tile build, connectivity, and graph behavior remain compatible.

## 9. Navigation Scene Geometry Roadmap

Goal: Feed navigation generation from scene-owned static geometry and terrain without making navigation depend on renderer internals, authored-scene runtime caches, physics, or App composition code.

Status: core source registry implemented. `Engine::SceneNavigationGeometryRegistry` can register CPU scene navigation sources, snapshot enabled static geometry plus optional terrain input into `NavigationTerrainBuildData`, track dirty chunks, and receive opt-in authored scene adapter registrations. Terrain now has a separate `Engine::TerrainNavigationAdapter`; scene navigation composition should consume terrain snapshots through that adapter instead of calling legacy terrain extraction directly. Automatic App streaming/rebuild integration, dynamic obstacles, physics collider extraction, off-mesh links, serialized source identity, and editor/debug UI remain deferred.

Scope:

- Add a CPU-only scene navigation geometry layer that can produce immutable Recast build inputs from loaded scene data.
- Support static mesh geometry, existing terrain navigation build data, and an extension point for future collider/nav-volume sources.
- Keep `NavigationSystem` tile lifetime, async build worker rules, cache behavior, connectivity, graph routing, and runtime query APIs unchanged.
- Do not inspect renderer GPU handles, renderer instance state, `AssetCache` internals, physics colliders, scripting objects, serialized scene files, or editor/debug UI in this milestone.
- Build full regions or full tiles first; add incremental dirty-tile rebuilds only after the source contract is deterministic and tested.

Source ownership and API shape:

- Add an engine-owned scene navigation source API, such as `SceneNavigationGeometrySource` records managed by a small `SceneNavigationGeometryRegistry`.
- Store source records as CPU metadata: source ID, optional owning `SceneActorHandle`, source type, enabled flag, bounds, triangle vertices/indices, area/material hints, blocker/walkable role, and a transform snapshot.
- Treat scene actor handles as transient ownership links only. Durable serialized navigation source identity is deferred to the serialization milestone.
- Let authored/static scene adapters register CPU mesh geometry while they still have imported vertex/index data available. Do not reverse-engineer geometry from `Renderer::StaticMeshHandle`.
- Let terrain contribute through `TerrainNavigationAdapter` requests, then normalize terrain and scene mesh inputs into one deterministic build snapshot. Legacy `TerrainSystem` extraction remains a compatibility input to that adapter, not a direct scene dependency.
- Keep Recast/Detour types private to navigation implementation files; public scene geometry headers expose only engine structs, GLM values, and plain containers.

Build flow:

- Query the registry for enabled sources whose world-space bounds intersect a requested chunk, tile, or region bounds.
- Refresh actor-owned source transforms from `Scene::worldMatrix` before snapshotting, rejecting stale, pending-destroy, invalid, or non-finite actors.
- Transform static mesh vertices into world space and append them in deterministic source order.
- Merge walkable triangles and blocker triangles into a worker-safe build input compatible with `NavigationTerrainBuildData` or a narrow successor struct.
- Submit immutable build snapshots to the existing navigation worker flow; completed navmesh data is committed on the main thread through existing `NavigationSystem` ownership rules.
- Preserve the procedural terrain path as a first-class contributor so existing chunk navigation tests and App movement behavior remain compatible.

Dirty tracking:

- Track source changes that affect navigation input: actor transform changes, source enable/disable, geometry replacement, bounds changes, area-role changes, and source destruction.
- Mark intersecting tiles or regions dirty without synchronously rebuilding them.
- Coalesce dirty regions by frame or explicit rebuild request so repeated transform updates do not spam tile jobs.
- Start with explicit full rebuild and explicit rebuild-region APIs; add automatic incremental rebuild scheduling only after the dirty-region accounting is proven.
- Removing a source invalidates affected generated input and diagnostics immediately, but live navmesh replacement still follows the normal async build and main-thread commit path.

Diagnostics and debug records:

- Report source counts by type and role, included/ignored triangle counts, invalid index counts, non-finite vertex counts, transformed bounds, dirty tile counts, build snapshot timing, worker build timing, cache hit/miss counts, and last warnings.
- Record why sources are ignored: disabled, missing geometry, invalid actor, non-finite transform, invalid indices, out of build bounds, or unsupported source type.
- Add renderer-independent debug request records for source bounds, included triangles, ignored sources, blocker triangles, dirty tiles, and generated tile bounds.
- Keep these debug records independent from ImGui and Renderer; visualization wiring belongs in later debug/app composition work.

Testing plan:

- Add CPU-only tests where a small scene static mesh generates deterministic nav build input and a usable local nav tile.
- Verify actor transforms, parent hierarchy transforms, non-uniform scale, and rotation affect gathered world-space geometry correctly.
- Verify disabled sources, invalid actors, stale handles, malformed indices, empty geometry, and non-finite vertices are rejected without mutation.
- Verify blocker geometry changes path results or produces blocked/partial status through the existing runtime query facade.
- Verify terrain and scene static mesh sources merge in deterministic order.
- Verify dirty tracking marks the expected tiles after transform, geometry, enable/disable, and source removal changes.
- Verify source gathering and tests do not link Renderer, App, Physics, scripting, or bgfx.
- Keep existing `manual_engine_navigation_tests`, `manual_engine_navigation_runtime_tests`, authored scene tests, and scene render bridge tests passing.

Deferred work:

- Dynamic obstacle carving, DetourTileCache obstacle updates, off-mesh links, jump links, climb links, crowd simulation, and multi-agent tile variants.
- Physics collider extraction, nav modifiers from scripts, editor-authored nav volumes, serialized navigation source IDs, streamed scene sector ownership, and asset-registry-driven mesh resolution.
- App migration from procedural chunk-only builds to scene-authored builds.

Exit criteria:

- A scene with static mesh sources can generate worker-safe navigation build input and produce usable local navmesh tiles through the existing `NavigationSystem`.
- Terrain and scene geometry can contribute to the same build snapshot without procedural-specific code leaking into scene APIs.
- Navigation scene geometry APIs do not expose renderer GPU resources, Recast/Detour types, physics internals, scripting state, or serialization assumptions.

## 10. Physics Integration Roadmap

Goal: Add Jolt physics behind engine-owned handles and scene components.

Status: core scene physics facade implemented. `Engine::ScenePhysicsWorld` provides Jolt-backed static, kinematic, and dynamic scene bodies, generation-counted body/collider handles, fixed scheduler sync, direct shape descriptors, raycast/capsule sweep/overlap/closest-point queries, diagnostics, and headless tests. Terrain now has an explicit `Engine::TerrainPhysicsColliderAdapter` that can create static scene-physics triangle mesh bodies from authoritative CPU terrain chunks, but App gameplay terrain-collider wiring remains opt-in/deferred. Procedural `World`, `ActorController`, `BlockingCollisionSystem`, authored collider generation, navigation obstacle integration, App gameplay migration, scripting, serialization, and debug UI remain deferred.

Detailed implementation plan and contract: `docs/scene_runtime/phase_10_physics_integration.md`.

### Scope And Boundaries

- Implement scene-owned physics first. `Engine::Scene` actors are the authoritative owners for physics components, transform sync, and future character movement.
- Keep procedural `World`, `ActorController`, and `BlockingCollisionSystem` unchanged. `BlockingCollisionSystem` remains a legacy kinematic helper and must not become the foundation for scene physics.
- Add Jolt behind a narrow Engine facade. Public Engine headers should expose Engine handles, descriptors, filters, diagnostics, and query results; they should not expose Jolt headers or Jolt object pointers.
- Keep the first implementation CPU/simulation-facing. Renderer integration is limited to debug request data; do not add renderer draw calls, ImGui UI, App migration, serialization, scripting, navigation coupling, or authored-scene automatic collider generation in this phase.
- Use the existing scene scheduler. Physics stepping belongs in fixed phases, with transform writeback before scene render bridge `PreRender`.

### Public API Shape

- Add `Engine::ScenePhysicsWorld` under `src/Engine/Physics`, composed with a `Scene&` rather than embedded directly into `Scene` storage.
- Add generation-counted handles:
  - `ScenePhysicsBodyHandle`
  - `SceneColliderHandle`
  - Optional `ScenePhysicsMaterialHandle` only if material ownership is more than descriptor metadata.
- Add stable plain data types:
  - `ScenePhysicsLayer`, `ScenePhysicsObjectType`, `ScenePhysicsMotionType::{Static, Kinematic, Dynamic}`
  - `ScenePhysicsFilter` with include/exclude layer masks and sensor/query flags.
  - `ScenePhysicsMaterial` with friction, restitution, density, and combine policy defaults.
  - Shape descriptors for box, sphere, capsule, cylinder if Jolt support is clean, convex hull if cheap, and triangle mesh for static scene geometry.
  - Body descriptors with owner `SceneActorHandle`, motion type, enabled/sensor flags, mass settings, damping, gravity factor, initial velocities, and material/filter data.
  - Query result records for raycast, shape cast/sweep, overlap, closest point, and body lookup by owner actor.
  - Diagnostics for body/collider counts, active/dormant counts, invalid owner cleanup, step timing, query counts/statuses, and warnings.
- Add APIs:
  - `createBody`, `destroyBody`, `contains`, `body`, `setBodyEnabled`, `setMotionType`.
  - `attachCollider`, `detachCollider`, `colliders(body)`, `bodyForActor`.
  - `setKinematicTarget`, `setLinearVelocity`, `setAngularVelocity`, `applyImpulse`, `applyForce` if dynamic bodies are included in Phase 10.
  - `syncFromScene`, `stepFixed`, `syncToScene`, and `registerPhysicsSystems`.
  - `raycast`, `sweep`, `overlap`, `closestPoint`.
  - `debugRequests`, `clearDebugRequests`, `diagnostics`.

### Scheduler And Transform Sync

- Register one scene system, or a small ordered set of systems, while the scene is `Unloaded` or `Loaded`.
- Recommended fixed-phase ordering:
  - `FixedPrePhysics`: read dirty scene transforms into static and kinematic physics bodies.
  - `FixedPhysics`: step the physics world using the fixed delta supplied by App/`FixedStepLoop`.
  - `FixedPostPhysics`: write dynamic body transforms back to owning scene actors and mark descendants dirty.
- `Scene::updateWorldTransforms()` already runs before `PreRender`; physics writeback must happen before that refresh.
- Static bodies read scene transforms when created, enabled, reparented, or explicitly marked dirty. Avoid hidden expensive rebuilds in arbitrary setters.
- Kinematic bodies are moved through explicit targets or scene transform sync policy; define one rule and test it.
- Dynamic bodies write world transforms back to scene actors. For parented dynamic actors, either reject dynamic bodies or define local transform reconstruction against the parent with preserve-world behavior.
- Pending-destroy or flushed scene actors invalidate physics owners. Cleanup should be deterministic by physics slot order and must release Jolt bodies/colliders without touching renderer or navigation.

### Collision Data Sources

- Direct descriptors are the primary Phase 10 path. Callers can create box/capsule/sphere/mesh colliders explicitly.
- Static triangle mesh colliders may consume CPU vertices/indices from scene navigation geometry sources or authored adapter CPU data only through explicit helper functions. Do not infer colliders from renderer mesh handles.
- Terrain collider support is owned by `TerrainPhysicsColliderAdapter`, which consumes authoritative CPU terrain chunk data and creates explicit static `ScenePhysicsWorld` bodies/colliders. Scene physics should not infer terrain colliders from renderer LOD meshes or terrain material resources.
- Do not generate colliders automatically during authored scene adaptation in the first implementation unless settings opt in and tests cover the ownership.

### Queries And Filters

- Raycast should return hit position, normal, distance/fraction, body handle, collider handle, owner actor, and material/layer data when available.
- Sweep/shape cast should support at least capsule or sphere if character movement is the next milestone; otherwise document unsupported shapes clearly.
- Overlap should return deterministic hits in a stable order, preferably sorted by body slot or hit distance depending on query type.
- Invalid handles, stale owners, NaN inputs, negative sizes, empty mesh data, and invalid filters must fail cleanly with diagnostics and no mutation.
- Query APIs must not step the simulation, build collision geometry, allocate renderer resources, or mutate scene transforms.

### Debug And Diagnostics

- Add renderer-independent debug request records for body bounds, collider shapes, contacts, raycasts, sweeps, overlaps, sleeping bodies, and broadphase stats.
- Keep debug draw requests as plain Engine data. App/Renderer debug UI may consume them later.
- Diagnostics should expose enough information to see whether physics is idle, stepping, skipping invalid owners, or leaking resources.
- Long-frame profiling should be possible from step elapsed microseconds without coupling to the App long-frame recorder.

### Build And Dependency Integration

- Add Jolt through vcpkg.
- Keep Jolt includes private to `ScenePhysics.cpp` or a private implementation header.
- Add `ScenePhysics.cpp/.hpp` to `manual_engine`.
- Add `manual_engine_scene_physics_tests`.
- Keep the physics test target independent from Renderer, Assets, App, Navigation, scripting, bgfx, and ImGui. Link only Jolt, GLM, and scene runtime sources required for transform/scheduler tests.

### Test Plan

- Body and collider lifecycle:
  - create/destroy static, kinematic, and dynamic bodies;
  - stale handle invalidation on destroy/reuse;
  - owner actor destroy/flush releases bodies and colliders deterministically;
  - shutdown is idempotent and releases all Jolt resources.
- Transform sync:
  - static body reads initial scene transform;
  - kinematic target updates physics transform on fixed step;
  - dynamic body writes back to scene transform after step;
  - hierarchy parent transforms compose correctly for static/kinematic bodies;
  - pending-destroy actors are excluded from sync and queries.
- Simulation:
  - dynamic body falls under gravity onto a static floor;
  - disabled bodies do not collide or move;
  - sensor colliders report overlap without physical response if sensors are included.
- Queries:
  - raycast hits and misses deterministically;
  - overlap returns expected colliders;
  - capsule/sphere sweep detects a wall or floor;
  - closest-point reports point/normal/distance where supported.
- Filters/materials:
  - layer masks include/exclude expected bodies;
  - material settings are stored and surfaced in hits;
  - invalid filter data fails cleanly.
- Scheduler:
  - registered physics systems run in fixed phase order;
  - dynamic writeback is visible to `PreRender` bridge sync in the same frame when App calls fixed ticks before `tickFrame`;
  - paused scene behavior is explicit, tested, and compatible with Phase 3 pause semantics.
- Diagnostics/debug:
  - step/query counters, timing, invalid owner cleanup, body counts, and debug requests update deterministically.

### Deferred Work

- Procedural `World` migration, `ActorController` replacement, and `BlockingCollisionSystem` removal.
- Full App physics gameplay integration.
- Character movement controller, floor snapping, stairs/steps, and path following; those belong to Milestone 11.
- Physics-driven navigation blockers, dynamic obstacle carving, and automatic nav rebuild scheduling.
- Serialization of bodies/colliders/materials.
- Scripting/reflection access.
- Editor/ImGui physics panels and renderer debug draw submission.
- Constraints, ragdolls, vehicles, compound authoring tools, trigger events/inboxes, and continuous collision tuning beyond what Jolt provides by default.

Exit criteria:

- Scene actors can own static, kinematic, and dynamic physics bodies through transient Engine handles.
- Scene transforms and physics body transforms sync through explicit scheduler phases.
- Raycast, sweep or shape-cast, overlap, and closest-point queries work through stable Engine APIs without exposing Jolt.
- Physics tests run headlessly without Renderer, App, Navigation, scripting, bgfx, or ImGui.
- Existing procedural world movement, blocking collision, authored scene sample, render bridge, navigation runtime, and full CTest behavior remain unchanged.

## 10.5. Terrain Runtime Alignment Checkpoint

Goal: Make the scene roadmap aware of the completed terrain rework foundation before character movement, reflection, and serialization build on it.

Status: implementation checkpoint added. The terrain roadmap now provides CPU heightmap import, `TerrainDataset`, derived CPU cache records, render LOD adapter, navigation build-data adapter, scene-physics collider adapter, terrain material metadata/rendering, and serialization prep metadata. `manual_engine_scene_terrain_alignment_tests` verifies that a tiny terrain dataset chunk can explicitly feed `SceneNavigationService`, `ScenePhysicsWorld`, and `TerrainSerializationPrep` without App, Renderer/bgfx, scripting, automatic terrain side effects, or runtime-handle serialization. These are explicit adapters beside the scene runtime; they are not scene components yet and do not automatically migrate App terrain ownership.

Scope:

- Treat `TerrainDataset` chunks and durable `TerrainSourceChunkId` values as the terrain-side identity model for future scene serialization and streaming work.
- Use `TerrainNavigationAdapter` as the terrain contribution path for scene navigation snapshots. Do not call legacy `TerrainSystem` terrain extraction directly from scene systems except through compatibility request helpers.
- Use `TerrainPhysicsColliderAdapter` as the terrain contribution path for scene physics static colliders. Do not derive physics collision from renderer terrain handles, terrain render LOD meshes, or material-layer resources.
- Treat `TerrainMaterialMetadata` and `TerrainMaterialRenderAdapter` as visual/material metadata boundaries. Terrain material sets are not scene render components, and renderer-owned terrain material-set handles remain transient live resources.
- Use `TerrainSerializationPrep` as the starting point for terrain chunk references in the Phase 13 serialization design. Runtime terrain handles, renderer terrain handles, nav tile handles, and physics body/collider handles must remain unserialized.
- Keep procedural App terrain ownership on `TerrainSystem` until a dedicated heightmap-backed App streaming or terrain ownership migration phase is planned.

Cleanup and retroactive work before Milestone 11:

- Audit character movement assumptions so grounding and floor sweeps can hit terrain through `ScenePhysicsWorld` only when a caller explicitly creates terrain collider bindings.
- Keep the terrain alignment composition tests passing as character movement starts depending on terrain. They create a tiny `TerrainDataset` chunk, build `TerrainPhysicsColliderAdapter` and `TerrainNavigationAdapter` outputs, and verify scene physics/navigation handoff without linking App or bgfx.
- Keep terrain material rendering out of character movement and physics decisions; gameplay material classification should use CPU material metadata or future material-weight payloads, not shader results.

Exit criteria:

- Scene navigation, physics, and future serialization plans reference the terrain adapters rather than legacy renderer or procedural terrain internals.
- Character movement can be planned against explicit scene physics and navigation services without assuming automatic terrain collider/navmesh generation.

## 11. Character Movement Roadmap

Goal: Build a reusable character movement component on top of physics and navigation.

Status: initial implementation added. `Engine::SceneCharacterMovementSystem` now owns generation-counted kinematic capsule character records, creates one owned `ScenePhysicsWorld` body/collider per character, performs fixed-step grounding and collision-constrained movement, and can consume `SceneNavigationService` paths. Terrain grounding/pathing is covered through explicit `TerrainPhysicsColliderAdapter` and `TerrainNavigationAdapter` composition tests. Procedural `ActorController`, `BlockingCollisionSystem`, App input/commands, renderer behavior, scripting, serialization, and automatic terrain/nav generation remain unchanged.

### Scope And Boundaries

- Add a scene-owned character movement system under `src/Engine`, composed beside `Scene`, `ScenePhysicsWorld`, and optional `SceneNavigationService`.
- Use `SceneActorHandle` as the owner identity and generation-counted character handles for movement records.
- Treat character bodies as kinematic scene physics bodies with capsule colliders. Do not use dynamic rigid bodies for the first character controller.
- Require callers to create terrain/static colliders and navigation tiles explicitly before movement uses them. No movement API may build terrain colliders, navmesh tiles, renderer resources, or chunk streaming work as a fallback.
- Preserve existing procedural `World`, `ActorController`, `BlockingCollisionSystem`, actor commands, App input, authored scene samples, and animated sample behavior.
- Defer animation root motion, network prediction, crouch/jump, moving platforms, ladders, swimming, vehicles, avoidance/crowd simulation, scripting, serialization, debug UI, and App gameplay migration.

### Public API Shape

- Add generation-counted `SceneCharacterHandle` with `isValid`, equality, and stale-handle behavior matching other Engine runtime handles.
- Add descriptors and state records:
  - `SceneCharacterDescriptor`: owner actor, radius, height, max speed, acceleration, braking, gravity, slope limit, step height, snap distance, physics layer/filter, enabled flag, and debug name.
  - `SceneCharacterMoveInput`: desired world-space direction, desired speed scale, optional face direction, jump flag reserved but ignored in Phase 11.
  - `SceneCharacterPathRequest`: optional path-follow target, agent config/filter, waypoint acceptance radius, repath interval, and allow-partial flag.
  - `SceneCharacterState`: movement mode, grounded flag, floor normal, floor distance, velocity, requested path, active waypoint index, last status, and diagnostics.
  - `SceneCharacterMovementDiagnostics`: character counts, enabled/disabled counts, grounded/falling counts, invalid owner cleanup, failed sweep counts, path query counts, elapsed update microseconds, and warnings.
- Add `SceneCharacterMovementSystem` APIs:
  - `createCharacter`, `destroyCharacter`, `contains`, `characterForActor`, `descriptor`, `state`, `setEnabled`, and `setMoveInput`.
  - `requestPathTo`, `clearPath`, `setNavigationService`, and `updateFixed(deltaSeconds)`.
  - `registerMovementSystem` / `unregisterMovementSystem` for `FixedPostPhysics` or a later fixed phase after physics writeback.
  - `debugRequests`, `clearDebugRequests`, and `diagnostics`.

### Movement Behavior

- On creation, the system creates or reuses an explicit kinematic `ScenePhysicsWorld` body for the owner actor and attaches one capsule collider. If body creation fails, character creation returns an invalid handle and records diagnostics.
- `updateFixed` reads move input, path-following state, and physics queries, then writes the character actor transform through scene APIs or `ScenePhysicsWorld::setKinematicTarget`.
- Grounding uses a downward capsule sweep or ray/closest-point fallback through `ScenePhysicsWorld`; walkable floor requires floor normal slope at or below the character slope limit.
- Horizontal movement uses acceleration toward desired velocity, braking when input is absent, and collision-constrained capsule sweeps with slide response along blocking surfaces.
- Step handling attempts a bounded up-forward-down probe when horizontal sweep hits a low obstacle; it succeeds only within `stepHeight` and if the landing surface is walkable.
- Falling applies gravity while ungrounded and transitions back to walking when a valid floor is found within snap distance.
- Path following consumes `SceneNavigationService` paths as waypoint lists; the movement system requests paths only through the provided service and never triggers tile builds. Missing nav data leaves the character idle or in manual movement mode with a diagnostic status.
- Paused scenes follow scheduler behavior: if fixed ticks do not run, movement does not update. Direct manual `updateFixed` calls with invalid or non-positive delta are clean no-ops with diagnostics.

### Terrain, Navigation, And Physics Composition

- Terrain participation is explicit:
  - terrain collision comes from `TerrainPhysicsColliderAdapter` creating static scene physics bodies before movement updates;
  - terrain pathing comes from `TerrainNavigationAdapter` plus `NavigationSystem` tile insertion before path requests;
  - terrain material rendering and shader-layer rules do not affect character movement.
- Character movement should work equally on simple box/static mesh colliders and terrain colliders because it only talks to `ScenePhysicsWorld`.
- Path following should work equally on terrain-generated nav tiles and scene-geometry nav tiles because it only talks to `SceneNavigationService`.

### Testing Plan

- Add `manual_engine_scene_character_movement_tests` with `Scene.cpp`, `ScenePhysics.cpp`, `NavigationRuntime.cpp`, terrain alignment sources only where a terrain-specific case needs them, and renderer stubs only if navigation AABB types require them.
- Required headless tests:
  - handle lifecycle, stale invalidation, owner destroy/flush cleanup, disabled character no-op, and shutdown idempotence;
  - character creation creates a kinematic capsule body and rejects invalid/stale/parented or malformed descriptors cleanly;
  - grounded detection on a flat static floor and on a terrain collider created through `TerrainPhysicsColliderAdapter`;
  - slope rejection on a steep surface and floor diagnostics for walkable versus unwalkable normals;
  - acceleration, braking, max speed clamping, and falling under gravity;
  - wall collision with slide response and no tunneling through thin static blockers;
  - step-up success for obstacles within `stepHeight` and failure above `stepHeight`;
  - path request through `SceneNavigationService`, waypoint advancement, missing-tile failure, and manual clear;
  - scheduler ordering after physics fixed phases and paused-scene behavior;
  - diagnostics/debug request counts update deterministically;
  - no App, Renderer/bgfx, scripting, serialization, or procedural `ActorController` dependency.

Exit criteria:

- A scene actor can own one kinematic capsule character movement record, move through static scene physics colliders, ground on explicit terrain colliders, and follow a nav path supplied by `SceneNavigationService`.
- Character movement performs no hidden terrain, navigation, renderer, App, serialization, or chunk-streaming work.
- Existing procedural movement and full CTest behavior remain unchanged.

## 12. Reflection And Opaque APIs Roadmap

Goal: Expose component state safely to editors, serialization, native hooks, and Lua.

Status: Implemented. This phase adds the reflection and opaque-access contract only. It does not add a file serializer, editor UI, Lua VM, native behavior hook framework, network replication, hot reload, ECS conversion, or App gameplay migration.

Scope:

- Add an Engine-owned reflection layer that describes approved runtime and serialized properties without exposing subsystem storage.
- Add opaque runtime handles for tools, hooks, and future scripts so callers can address scene objects without including subsystem-specific handle types everywhere.
- Keep stable identity separate from runtime access: `SceneObjectId`, `AssetId`, and `TerrainSourceChunkId` are durable identity; scene actor/component/system/physics/render/navigation/terrain runtime handles remain transient and unserialized.
- Route all reflected reads and writes through public subsystem APIs. Reflection must not return raw pointers, mutable references to internal records, or renderer/physics/navigation implementation objects.
- Support scene-owned systems first: `Scene`, transform hierarchy, `SceneRenderBridge`, `ScenePhysicsWorld`, `SceneCharacterMovementSystem`, asset metadata, terrain durable references, and navigation query configuration where appropriate.
- Keep legacy procedural `World`, `ActorController`, `BlockingCollisionSystem`, App debug panels, and current terrain/rendering runtime behavior unchanged.

Opaque access model:

- Define a compact opaque handle type with a kind tag, slot/generation payload, and optional owner/domain tag so diagnostics can explain mismatched handle use.
- Handle kinds should cover at least scene actors, generic scene components, render bridge mesh/skinned/light/camera components, physics bodies/colliders, scene characters, asset metadata records, terrain sources/chunks, and navigation query/service records.
- Opaque handles are runtime access tokens only. They must never be written directly to scene files, terrain files, cache manifests, or saved gameplay state.
- Provide conversion helpers at subsystem boundaries, such as scene actor handle to opaque handle and opaque handle back to a validated `SceneActorHandle`.
- Invalid, stale, wrong-kind, and wrong-owner opaque handles must fail cleanly with typed status results and diagnostics.
- Opaque APIs should be thread-neutral value APIs; they should not imply async safety or background mutation.

Reflection metadata:

- Add reflected type descriptors for primitive and engine value types: bool, signed/unsigned integers, float, double where needed, string, enum, bitmask, `glm::vec2`, `glm::vec3`, `glm::quat`, `glm::mat4` read-only where needed, `AssetId`, `SceneObjectId`, terrain durable chunk IDs, and opaque handles.
- Add property descriptors with stable property IDs, public names, display/category strings, value type, default value, min/max/range hints, enum labels, unit metadata, flags, and documentation.
- Property flags should include read-only, runtime-only, serializable, editor-visible, script-visible, advanced, transient, asset-reference, stable-reference, and requires-explicit-apply.
- Add reflected object/type descriptors for actor transform state, render component descriptors, physics body/collider descriptors, character movement descriptors/state, asset metadata, terrain serialization-prep references, and selected navigation query settings.
- Descriptor ordering must be deterministic so generated docs, future serialized output, and tests produce stable diffs.
- The metadata registry should allow subsystem-local registration without global static initialization ordering hazards.

Getter/setter API:

- Use a bounded reflected value variant for get/set operations; do not accept untyped `void*` values.
- Getter calls return typed values plus status. Setter calls return status and optional validation messages.
- Setter statuses should distinguish invalid handle, unknown property, type mismatch, read-only property, validation failure, unsupported operation, and subsystem rejection.
- Reflected writes must call the same public APIs as normal runtime code. Examples: transform writes use `Scene::setLocalTransform`, render descriptor writes use bridge descriptor replacement APIs, physics writes use physics world mutators, and character writes use movement-system descriptor/enabled/input APIs.
- Reflected writes that affect transforms, render descriptors, physics bodies, terrain metadata, or character movement must mark the same dirty state or diagnostics as direct API writes.
- Avoid hidden allocation or loading in setters. Asset references may be changed as metadata, but renderer resource creation, physics shape creation, nav tile building, and terrain cache generation remain explicit API calls.

Subsystem adapter plan:

- Scene core adapter:
  - Reflect actor validity, stable object ID, lifecycle state, parent/children queries, local transform, and world matrix read-only.
  - Allow local transform writes through `Scene::setLocalTransform`.
  - Keep hierarchy mutation as explicit API calls rather than generic property writes unless a later editor workflow needs it.
- Render bridge adapter:
  - Reflect mesh, skinned mesh, light, and camera descriptors in terms of scene owner, enabled flag, renderer handle values, layers, culling fields, material overrides, light fields, and camera projection fields.
  - Treat live renderer instance/light handles as internal bridge state, not reflected properties.
  - Require descriptor replacement through bridge APIs; no direct renderer mutation.
- Physics adapter:
  - Reflect body descriptor fields, motion type, enabled state, layer/filter metadata, velocities, and collider descriptor summaries.
  - Allow safe mutators such as enabled state, kinematic target, motion type where supported, velocity, force, and impulse through `ScenePhysicsWorld`.
  - Keep Jolt identifiers, broadphase state, and shape pointers private.
- Character movement adapter:
  - Reflect character descriptor, enabled flag, movement mode/state, velocity, grounding information, active path summary, and diagnostics.
  - Allow descriptor-safe settings and move input to be updated; keep path request APIs explicit because they perform navigation queries.
- Asset and terrain adapters:
  - Reflect `AssetRegistry` metadata, asset IDs, source paths, import settings keys, dependencies, and statuses.
  - Reflect terrain durable IDs and serialization-prep metadata for future serialization. Runtime terrain handles, renderer terrain handles, nav tile handles, terrain physics collider handles, and cache file paths remain outside serialized reflection.
- Navigation adapter:
  - Reflect agent/filter configuration and query diagnostics where useful.
  - Keep pathfinding/projection/raycast operations as explicit query APIs, not property getters with hidden work.

Validation and boundaries:

- All reflected write paths must validate finite numeric values, required ranges, enum values, handle kind/owner, and subsystem invariants before mutation.
- Read-only properties should be common for derived state: world matrices, live renderer counts, physics query results, nav diagnostics, cache diagnostics, and generated terrain bounds.
- The reflection layer should surface clear diagnostics for rejected changes, but it should not attempt rollback across multiple subsystem writes in Phase 12.
- Batch editing can be planned as a later improvement; Phase 12 may provide single-property writes plus enough status detail for callers to build their own UI/serialization validation.
- Change notifications should be explicit and simple: reflected setters return changed/unchanged status and may append subsystem-owned dirty/debug records. A global event bus is out of scope.

Implementation phases inside Phase 12:

1. Reflection core:
   - Added value variant, type/property/object descriptors, registry, status codes, diagnostics, and deterministic descriptor enumeration.
   - Added tests for metadata registration, duplicate descriptor rejection, stable ordering, type validation, default values, range validation, and wrong-kind opaque handles.
2. Opaque handle conversion:
   - Added conversion helpers for scene actors, generic scene components, render bridge components, physics body/collider handles, character handles, asset handles, terrain runtime handles, and terrain durable references.
   - Added tests for invalid/stale/wrong-owner handles and stable-ID separation.
3. Scene and transform adapter:
   - Reflected actor/object metadata and transform properties.
   - Added tests that reflected local transform writes update scene local/world state through `Scene::setLocalTransform`.
4. Runtime subsystem adapters:
   - Added render bridge, physics, character, asset, terrain, and limited navigation metadata descriptors through small isolated files.
   - Added tests that adapter writes route through public APIs and reject unsupported mutation.
5. Documentation and compatibility:
   - Updated `docs/system_contracts.md` and `docs/engine_overview.md` with the reflection boundary, opaque runtime handle rule, and stable-ID serialization boundary.
   - Existing scene, terrain, navigation, physics, render bridge, authored, animated, and procedural tests remain in the regression set.

Test plan:

- Add `manual_engine_scene_reflection_tests` for the core reflection registry and opaque handle conversion.
- Add subsystem adapter coverage either in that target or focused CPU-only targets, keeping them independent from App, Renderer/bgfx implementation, scripting, editor UI, serialization file I/O, and optional sample assets.
- Required cases:
  - descriptor registration is deterministic and duplicate IDs are rejected;
  - property lookup by stable ID and name works;
  - default values and range metadata are exposed;
  - get/set rejects wrong type, read-only property, invalid handle, stale handle, wrong handle kind, and wrong owner;
  - scene transform reflected write uses `Scene::setLocalTransform` and updates dirty/world state correctly;
  - render bridge descriptor reflected write updates only the targeted component record;
  - physics reflected mutators use `ScenePhysicsWorld` APIs and keep Jolt private;
  - character movement reflected settings update descriptor/enabled/input state without creating terrain/nav/renderer side effects;
  - asset and terrain reflected metadata uses stable IDs/durable chunk identity, not transient runtime handles;
  - reflection headers do not include Jolt, Recast/Detour, bgfx, Lua, ImGui, or App headers;
  - existing full CTest remains green.

Exit criteria:

- Tools, future serializers, native hooks, and future Lua bindings can inspect and edit approved scene/runtime properties through typed metadata and opaque handles without direct storage access.
- Runtime opaque handles are clearly separated from stable serialized identity.
- Reflected writes preserve subsystem invariants by routing through existing public APIs.

## 13. Serialization Roadmap

Goal: Persist scene data without serializing transient runtime handles.

- Serialize scene metadata, actors, stable IDs, component records, transform hierarchy, asset references, and exposed variables.
- For terrain references, use `TerrainSerializationPrep` durable chunk identity and payload-boundary metadata; do not serialize `TerrainSourceHandle`, `TerrainChunkHandle`, renderer terrain handles, nav tile handles, terrain collider handles, or scene physics body/collider handles.
- Add versioned scene file format and migration hooks.
- Serialize references by stable ID or asset ID, not runtime handles.
- Add load validation for missing assets, unknown component types, invalid parents, and property mismatches.
- Add deterministic save ordering for clean diffs.
- Add tests for round-trip scenes, hierarchy preservation, missing assets, version migration, and component defaults.

Exit criteria:

- A scene can be saved, loaded, and restored with stable actor/component identity.

## 14. Native Behavior Hooks Roadmap

Goal: Let C++ gameplay code attach behavior to scenes, actors, and components without bypassing scene contracts.

- Define scene hooks: on load, start, tick phases, stop, unload.
- Define actor hooks: on create, start, tick, destroy.
- Define component hooks: on enable, disable, tick, property changed, destroy.
- Keep hook execution ordered by the tick scheduler.
- Restrict hook access through opaque scene/system APIs.
- Add diagnostics for hook errors, order, timing, and disabled hooks.
- Add tests for hook ordering, destruction during callbacks, and failed hook isolation.

Exit criteria:

- Native gameplay behavior can be written against stable runtime APIs without app-local wiring.

## 15. Lua Scripting Roadmap

Goal: Add Lua as a safe scripting layer over reflected, opaque engine APIs.

- Add Lua VM lifetime per scene or app policy.
- Bind opaque scene, actor, component, asset, physics, navigation, and renderer-debug handles.
- Expose reflected property get/set and approved system APIs.
- Add script lifecycle hooks matching native hooks.
- Add error reporting, stack traces, script reload behavior, and execution budget diagnostics.
- Add tests for hook execution, property access, invalid handle behavior, reload, and script errors.

Exit criteria:

- Lua scripts can drive simple actor/component behavior without direct access to engine internals.

## 16. Debug Visualization Roadmap

Goal: Unify debug visibility across scene, renderer, navigation, physics, animation, and assets.

- Add a debug visualization request API that systems can use without depending on ImGui.
- Add categories for transforms, bounds, navmesh, paths, physics shapes, skeletons, cameras, lights, and streaming sectors.
- Add budgets and clipping for expensive debug draws.
- Add per-scene debug settings surfaced through the existing Render tab or future Scene tab.
- Add profile/report output for scene diagnostics and resource counts.
- Preserve release behavior: debug visualization disabled unless explicitly enabled by build/runtime policy.
- Add tests for request capping, category toggles, and no-op release paths where practical.

Exit criteria:

- Debug visual output is consistent and budgeted across systems instead of ad hoc per feature.

## Migration Strategy

1. Add scene kernel and transform hierarchy without touching existing runtime owners.
2. Add renderer bridge using small fixture scenes and renderer stubs.
3. Adapt authored and animated pipelines behind the bridge.
4. Move procedural world objects into scene actors gradually.
5. Expose navigation and physics through scene services.
6. Add reflection and serialization once component shapes are stable.
7. Add hooks and Lua after access boundaries are proven.

Avoid converting `src/App/main.cpp` into the owner of the new architecture. App should compose services, choose demo modes, and display diagnostics; reusable behavior belongs under `src/Engine`, `src/Assets`, or `src/Renderer`.
