# Phase 10 Physics Integration Plan

## Summary

Add scene-owned physics behind Engine handles, using Jolt as the private backend. This phase creates the physics world facade, body/collider component records, transform sync, fixed-step scheduler integration, queries, diagnostics, and headless tests. It does not migrate the procedural `World`, `ActorController`, `BlockingCollisionSystem`, authored scene conversion, navigation rebuilds, scripting, serialization, or debug UI.

Implementation status: core facade complete. The implementation lives under `src/Engine/Physics`, with `src/Engine/ScenePhysics.hpp` as a forwarding include for callers that want the shorter Engine path.

## Ownership Model

- `Engine::ScenePhysicsWorld` is composed beside `Engine::Scene` and references a `Scene&`.
- Scene actors own physics bodies through transient generation-counted physics handles.
- Public Engine headers expose descriptors, filters, query results, handles, and diagnostics only. Jolt headers and objects remain private to implementation files.
- Renderer handles, navigation handles, world object handles, and physics handles remain separate identities.
- `BlockingCollisionSystem` remains a legacy procedural-world kinematic helper and is not reused by scene physics.

## Public API

Add `src/Engine/Physics/ScenePhysics.hpp/.cpp` with:

- `ScenePhysicsBodyHandle { uint32_t index = UINT32_MAX; uint32_t generation = 0; }`
- `SceneColliderHandle { uint32_t index = UINT32_MAX; uint32_t generation = 0; }`
- `ScenePhysicsLayer`
- `ScenePhysicsMotionType::{Static, Kinematic, Dynamic}`
- `ScenePhysicsShapeType::{Box, Sphere, Capsule, StaticTriangleMesh}`
- `ScenePhysicsFilter`
- `ScenePhysicsMaterial`
- Shape descriptors for box, sphere, capsule, and static triangle mesh.
- `ScenePhysicsBodyDescriptor` with owner actor, motion type, enabled/sensor flags, material/filter data, mass/damping/gravity defaults, and optional initial velocity.
- Query result types for raycast, sweep or shape cast, overlap, closest point, and owner lookup.
- `ScenePhysicsDiagnostics` with body/collider counts, active/sleeping counts, invalid owner cleanup, step elapsed microseconds, query counts/statuses, and warnings.
- Renderer-independent `ScenePhysicsDebugRequest` records for shapes, bounds, contacts, raycasts, sweeps, overlaps, and broadphase stats.

Primary APIs:

- `createBody`, `destroyBody`, `contains`, `body`, `bodyForActor`.
- `attachCollider`, `detachCollider`, `colliders`.
- `setBodyEnabled`, `setMotionType`, `setKinematicTarget`.
- `setLinearVelocity`, `setAngularVelocity`, `applyImpulse`, `applyForce` if dynamic bodies are in scope for the first pass.
- `syncFromScene`, `stepFixed`, `syncToScene`.
- `registerPhysicsSystems`, `unregisterPhysicsSystems`.
- `raycast`, `sweep`, `overlap`, `closestPoint`.
- `diagnostics`, `debugRequests`, `clearDebugRequests`.

## Scheduler Contract

- Register physics systems only while the scene is `Unloaded` or `Loaded`.
- `FixedPrePhysics`: pull dirty scene transforms into static and kinematic bodies.
- `FixedPhysics`: step the physics backend with the fixed delta supplied by App or `FixedStepLoop`.
- `FixedPostPhysics`: write dynamic body transforms back to scene actors and mark descendants dirty.
- `PreRender` render bridge sync must observe physics writeback when App runs fixed ticks before `Scene::tickFrame`.
- Paused behavior must be explicit. Default recommendation: skip `FixedPhysics` when the scene scheduler is paused, matching Phase 3 fixed-phase pause behavior.

## Transform Rules

- Static bodies read scene transforms at creation, explicit sync, enable, and dirty transform sync. They do not write back.
- Kinematic bodies are controlled by explicit kinematic targets or scene transform sync, not both at the same time.
- Dynamic bodies write world transforms back to scene actors after simulation.
- Parented dynamic bodies are rejected in Phase 10 unless local transform reconstruction against the parent is implemented and tested.
- Pending-destroy, flushed, stale, or default actor handles invalidate physics owners and are cleaned up deterministically.
- Non-finite transforms, invalid scales for shape construction, negative dimensions, empty meshes, and malformed indices reject mutation and report diagnostics.

## Collision Geometry

- Direct shape descriptors are the default Phase 10 path.
- Static triangle mesh colliders use CPU vertices/indices, never renderer mesh handles.
- Optional helpers may convert scene navigation geometry sources or authored adapter CPU mesh data into static mesh collider descriptors, but they must be explicit and opt-in.
- Terrain collider generation is deferred unless it can consume existing CPU terrain data through a narrow helper without changing terrain ownership.

## Queries

- Raycast returns hit actor/body/collider, position, normal, distance/fraction, layer, and material data.
- Capsule sweep is supported as the Phase 10 shape-cast path.
- Overlap returns deterministic body/collider hits in stable order.
- Closest-point reports point/normal/distance where Jolt supports it.
- Queries never step simulation, allocate renderer resources, enqueue workers, build navigation tiles, or mutate scene transforms.

## Diagnostics And Debug Records

- Diagnostics are plain Engine data and do not change simulation outcomes.
- Debug requests are renderer-independent records. App/Renderer may consume them in a later debug visualization phase.
- Step timing and query counters should make idle, stepping, and invalid-owner cleanup behavior visible in tests and debug output.

## Build Plan

- Add Jolt through vcpkg.
- Add physics source files to `manual_engine`.
- Add `manual_engine_scene_physics_tests`.
- Keep the test target independent from Renderer, Assets, App, Navigation, scripting, bgfx, and ImGui.
- Link only Jolt, GLM, and scene runtime sources needed for transform and scheduler tests.

## Test Plan

- Body lifecycle: create/destroy static, kinematic, and dynamic bodies; stale handle invalidation; actor destroy/flush cleanup; idempotent shutdown.
- Collider lifecycle: attach/detach shapes; malformed shapes reject cleanly; body destroy releases colliders.
- Transform sync: static reads actor transform; kinematic target updates backend transform; dynamic body writes back to scene actor; hierarchy transforms compose for static/kinematic bodies.
- Simulation: dynamic body falls onto a static floor; disabled bodies do not move or collide; sensors overlap without response if sensors are implemented.
- Queries: deterministic raycast hit/miss, overlap, capsule or sphere sweep, closest point, invalid input handling, and layer filtering.
- Scheduler: physics systems run in fixed phase order; writeback is visible before render bridge `PreRender`; paused fixed phases do not step.
- Diagnostics/debug: body/collider counts, step timing, query counters, invalid owner cleanup, and debug request records update deterministically.

## Deferred Work

- Procedural `World` migration.
- `ActorController` replacement and character movement.
- Removing or rewriting `BlockingCollisionSystem`.
- Automatic authored scene collider generation.
- Terrain collider integration if not trivial through existing CPU terrain data.
- Navigation dynamic obstacles and automatic nav rebuild scheduling.
- Serialization, reflection, scripting, editor UI, ImGui panels, renderer debug draw submission.
- Constraints, ragdolls, vehicles, trigger inboxes/events, compound collider authoring, and advanced material/filter authoring.

## Acceptance Checks

- `manual_engine` builds.
- Full CTest passes.
- `git diff --check` passes.
- Existing release Sponza + KayKit sample behavior remains visually equivalent.
- Existing procedural open-world movement and blocking collision behavior remain unchanged.
