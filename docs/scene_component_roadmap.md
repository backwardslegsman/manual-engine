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
11. Character movement: capsule movement, grounding, slope/step logic, path following, and movement variables.
12. Reflection and opaque APIs: property metadata, getter/setter access, system boundaries, script visibility.
13. Serialization: scenes, actors, components, hierarchy, asset references, exposed variables, and scene config.
14. Native behavior hooks: scene, actor, and component lifecycle/update hooks.
15. Lua scripting: Lua lifecycle hooks, opaque handles, reflected variables, and exposed system APIs.
16. Debug visualization unification: transforms, navmesh, paths, physics, skeletons, cameras, resources, and scene state.

## 1. Scene Kernel Roadmap

Goal: Introduce a central runtime container without replacing existing systems in one pass.

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

- Add `TransformComponent` with local translation, rotation, scale, and cached world matrix.
- Support roots and parent/child relationships.
- Add attach, detach, reparent, preserve-world-transform, and preserve-local-transform operations.
- Add dirty propagation from parent to descendants.
- Add deterministic world-transform update order.
- Add cycle prevention and invalid-parent diagnostics.
- Add bounds hooks for components that need transform-driven world bounds.
- Add tests for hierarchy composition, non-uniform scale, detach behavior, cycle rejection, and dirty propagation.

Exit criteria:

- Scene transforms can reproduce imported authored-scene node transforms.
- Systems can query world transforms without owning hierarchy logic themselves.

## 3. Tick Scheduler Roadmap

Goal: Formalize scene lifecycle and frame phase ordering while preserving existing fixed-step behavior.

- Add scene lifecycle calls: load, start, tick, stop, unload.
- Add frame phases: begin frame, pre-physics, physics, post-physics, animation, render submit, end frame.
- Define fixed-step versus variable-step phase rules.
- Define system registration order and explicit dependencies.
- Define main-thread-only phases and worker-safe phases.
- Add budget integration for phase work that can be split across frames.
- Add diagnostics for phase timings, skipped phases, pending work, and long-frame contributors.
- Add tests for phase order, paused scenes, unload during pending work, and fixed-step accumulation.

Exit criteria:

- Scene systems can be ordered without app-local sequencing growing further.
- Existing renderer/nav/async commit rules remain explicit.

## 4. Asset Registry Roadmap

Goal: Create stable asset handles and import records that sit above current import/cache systems.

- Add `AssetHandle`, `AssetId`, `AssetType`, and asset metadata records.
- Add registry records for source path, canonical path, source format, content hash, import settings, and dependencies.
- Represent imported data as records for static mesh, skinned mesh, skeleton, material, texture, animation clip, authored scene, and navigation source geometry.
- Connect existing Assimp import diagnostics to asset import records.
- Connect authored scene and animated model cache identity to registry metadata.
- Add dependency queries so scenes can report required assets.
- Add invalid/stale/missing asset diagnostics.
- Add tests for stable handle lookup, dependency graph queries, stale detection, and optional-asset skipping.

Exit criteria:

- Components can reference assets by handle or stable ID instead of raw paths.
- Existing path-based loaders still work as compatibility APIs.

## 5. Render Component Bridge Roadmap

Goal: Let scene components drive renderer handles without coupling renderer internals to component storage.

- Add `MeshComponent`, `SkinnedMeshComponent`, `CameraComponent`, `LightComponent`, and material reference fields.
- Add a render submission system that translates scene components into renderer handles and instance updates.
- Keep renderer handle ownership explicit and deterministic.
- Add dirty flags for transform, visibility, material, mesh, light, and camera changes.
- Preserve render layers, max draw distance, render groups, and alpha paths.
- Support static and skinned draw stats through existing renderer diagnostics.
- Add tests with renderer stubs for component creation, update, removal, and no-leak shutdown.

Exit criteria:

- A scene actor with mesh/light/camera components can render through existing renderer APIs.
- Renderer does not know about actor or component storage.

## 6. Authored Scene Adapter Roadmap

Goal: Adapt existing static authored scene loading into the scene runtime without losing streaming/cache/async behavior.

- Map imported scene nodes to scene actors or transform components.
- Map imported mesh nodes to `MeshComponent`s.
- Map imported lights to `LightComponent`s.
- Preserve material and texture mapping through existing renderer/asset policies.
- Keep eager and partitioned authored scene loaders working during migration.
- Add a bridge for sector streaming to create/destroy scene actors or component groups per loaded sector.
- Preserve authored diagnostics, cache status, async status, and profile reports.
- Add tests that compare old authored loader output with scene-adapted output for fixture assets.

Exit criteria:

- Static authored scenes can appear as scene-managed actors/components.
- Existing release authored demo behavior remains intact.

## 7. Animated Model Adapter Roadmap

Goal: Integrate the existing animation pipeline as scene components.

- Add `SkeletonComponent` or skeleton asset reference ownership.
- Add `AnimatorComponent` with clip index, time, speed, loop, playing, and crossfade state.
- Add skinned mesh component binding to skeleton/skin assets.
- Upload sampled poses to renderer skinned instances through the render bridge.
- Preserve existing `AnimatedModel` synchronous, cache, async, and diagnostics APIs during migration.
- Add debug controls for animator state through scene-facing diagnostics.
- Add tests for sampling, crossfade, component removal, cache-hit loading, async loading, and leak-free shutdown.

Exit criteria:

- An actor can own an animator and skinned mesh component that plays an imported clip.
- Existing animated fixture still renders through the skinned shader path.

## 8. Navigation Runtime API Roadmap

Goal: Expose current navigation functionality behind scene-friendly public APIs.

- Keep Recast/Detour private to `Navigation.cpp`.
- Add scene-facing path query, nearest-point projection, ray/segment projection, and reachability APIs.
- Define agent configs as data records or assets.
- Add per-scene navigation service ownership.
- Add debug draw requests for navmesh, portals, paths, and failed queries.
- Preserve existing chunk navigation cache and async tile build behavior.
- Add tests for local pathfinding, projection, invalid queries, and diagnostics.

Exit criteria:

- Gameplay code can request navigation without depending on procedural chunk internals.
- Existing procedural path following remains compatible.

## 9. Navigation Scene Geometry Roadmap

Goal: Feed navigation generation from scene static geometry and terrain.

- Define a nav-source interface for static mesh components, terrain, and optional collider geometry.
- Gather world-space triangles from loaded scene geometry under explicit build settings.
- Add dirty tracking for transform/mesh/collider changes that affect nav input.
- Support full rebuild first, then incremental region/tile updates.
- Preserve worker-safe Recast build inputs and main-thread navmesh commit rules.
- Add diagnostics for source triangle counts, ignored components, build timing, tile counts, and cache status.
- Add tests with small static scene geometry and transformed meshes.

Exit criteria:

- A scene with static meshes can generate usable navmesh input without procedural-specific code.

## 10. Physics Integration Roadmap

Goal: Add Jolt physics behind engine-owned handles and scene components.

- Add physics world ownership per scene.
- Add `StaticColliderComponent`, `CapsuleColliderComponent`, and optional `RigidBodyComponent`.
- Add engine-facing physics handles separate from actor/component handles.
- Add queries: raycast, sweep, overlap, closest point.
- Add transform sync rules for static, kinematic, and dynamic objects.
- Add collision filters, layers, and simple material/friction settings.
- Add debug draw for shapes, contacts, sweeps, and broadphase stats.
- Add tests for body creation/destruction, transform sync, queries, and no-leak shutdown.

Exit criteria:

- Scene actors can own colliders and answer collision queries through a stable physics API.

## 11. Character Movement Roadmap

Goal: Build a reusable character movement component on top of physics and navigation.

- Add `CharacterMovementComponent` with capsule dimensions, max speed, acceleration, gravity, slope limit, and step height.
- Add grounded detection and floor result diagnostics.
- Add collision-constrained movement with slide response.
- Add path-following input that consumes navigation waypoints.
- Add movement modes: walking, falling, disabled.
- Add transform sync to scene hierarchy.
- Add tests for grounding, slope rejection, step behavior, wall slide, path following, and paused movement.

Exit criteria:

- A scene actor can move with a capsule through static colliders and follow a nav path.

## 12. Reflection And Opaque APIs Roadmap

Goal: Expose component state safely to editors, serialization, native hooks, and Lua.

- Add property metadata: name, type, default, range, flags, category, and documentation.
- Add getter/setter APIs that validate access and preserve invariants.
- Add opaque handles for scenes, actors, components, assets, physics objects, and navigation agents.
- Define system access boundaries for read-only and mutating APIs.
- Add change notifications or dirty flags for reflected writes.
- Add tests for property lookup, invalid writes, default values, and system boundary enforcement.

Exit criteria:

- Tools and scripts can inspect and edit approved component variables without direct storage access.

## 13. Serialization Roadmap

Goal: Persist scene data without serializing transient runtime handles.

- Serialize scene metadata, actors, stable IDs, component records, transform hierarchy, asset references, and exposed variables.
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
