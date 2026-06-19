# Phase 3 Plan: Tick Scheduler

## Summary

Add a renderer-independent scene lifecycle and tick scheduler on top of the Phase 1 scene kernel and Phase 2 transform hierarchy. This phase defines how scene-owned systems are registered, ordered, started, ticked, stopped, and unloaded without moving existing `World`, renderer, navigation, physics, authored-scene, or animation runtime behavior into the scene yet.

This phase must stay small and explicit. It introduces scheduling contracts and CPU-only test fixtures, not gameplay systems, renderer submission, physics simulation, navigation queries, scripting, serialization, async streaming, editor UI, or authored-scene conversion.

## Goals

- Give `Engine::Scene` a clear lifecycle shell that future scene systems can plug into without app-local frame sequencing growing further.
- Make update phase order explicit before render, physics, animation, navigation, scripting, or native behavior hooks depend on it.
- Preserve existing fixed-step behavior by defining where fixed and variable scene phases run, without replacing `Engine::FixedStepLoop` or current `App` frame flow.
- Keep live scene mutation main-thread owned. Worker jobs may produce plain data for future phases, but scheduler callbacks in this phase run synchronously on the caller thread.
- Add diagnostics that explain phase order, skipped work, lifecycle state, and basic timing without depending on Dear ImGui.

## Public Types

- Add a lightweight scheduler API to `src/Engine/Scene/Scene.hpp` / `Scene.cpp`; avoid a broad framework unless `Scene` becomes hard to inspect.
- Add `SceneLifecycleState`:
  - `Unloaded`
  - `Loaded`
  - `Started`
  - `Stopping`
- Add `SceneTickPhase` in fixed execution order:
  - `BeginFrame`
  - `FixedPrePhysics`
  - `FixedPhysics`
  - `FixedPostPhysics`
  - `VariableAnimation`
  - `VariableUpdate`
  - `PreRender`
  - `EndFrame`
- Add `SceneTickContext`:
  - `SceneTickPhase phase`
  - `float deltaSeconds`
  - `uint64_t frameIndex`
  - `uint32_t fixedStepIndex`
  - `bool fixedStep`
  - `bool paused`
- Add `SceneSystemHandle { uint32_t index = UINT32_MAX; uint32_t generation = 0; }` with generation-counted validity and equality helpers.
- Add `SceneSystemDescriptor`:
  - stable `std::string name`
  - `std::vector<SceneTickPhase> phases`
  - optional lifecycle callbacks: load, start, stop, unload
  - tick callback receiving `Scene&` and `const SceneTickContext&`
  - `bool enabled = true`
- Add `SceneSchedulerDiagnostics`:
  - lifecycle state
  - frame index
  - total registered/enabled system counts
  - last phase executed
  - skipped callback count
  - failed callback count, if callbacks return status
  - per-phase callback counts and last duration in microseconds

## Scene API Shape

```cpp
namespace Engine {
    class Scene {
    public:
        SceneSystemHandle registerSystem(SceneSystemDescriptor descriptor);
        bool unregisterSystem(SceneSystemHandle system);
        bool setSystemEnabled(SceneSystemHandle system, bool enabled);
        bool contains(SceneSystemHandle system) const;

        SceneLifecycleState lifecycleState() const;
        bool load();
        bool start();
        bool stop();
        bool unload();

        void tickFrame(float deltaSeconds);
        void tickFixed(float fixedDeltaSeconds);
        void tickPhase(SceneTickPhase phase, float deltaSeconds);
        void setPaused(bool paused);
        bool paused() const;

        SceneSchedulerDiagnostics schedulerDiagnostics() const;
    };
}
```

Implementation can adjust names for clarity, but the behavior must remain explicit and narrow.

## Lifecycle Rules

- `load()` is valid only from `Unloaded`; it runs registered load callbacks in registration order and moves to `Loaded` only if all callbacks complete.
- `start()` is valid from `Loaded`; it runs start callbacks in registration order and moves to `Started`.
- `stop()` is valid from `Started`; it moves through `Stopping`, runs stop callbacks in reverse registration order, and returns to `Loaded`.
- `unload()` is valid from `Loaded`; it runs unload callbacks in reverse registration order and returns to `Unloaded`.
- Invalid lifecycle transitions return `false` and do not run callbacks.
- Destroying scene actors/components during callbacks is allowed only through existing public `Scene` APIs. The scheduler must not bypass lifecycle or handle validation.
- System registration/unregistration is allowed while unloaded or loaded. If attempted while started, the first implementation should reject it rather than adding deferred mutation queues.
- Disabled systems skip tick callbacks but still remain registered. Lifecycle callbacks run only for enabled systems in this phase.

## Tick Rules

- `tickFrame(deltaSeconds)` is a variable-frame call and runs these phases in order:
  1. `BeginFrame`
  2. `VariableAnimation`
  3. `VariableUpdate`
  4. `PreRender`
  5. `EndFrame`
- `tickFixed(fixedDeltaSeconds)` runs one fixed simulation step:
  1. `FixedPrePhysics`
  2. `FixedPhysics`
  3. `FixedPostPhysics`
- `tickPhase(phase, deltaSeconds)` runs one explicit phase and is mainly for tests/tools.
- Tick calls only execute in `Started`. Calls in `Unloaded`, `Loaded`, or `Stopping` are no-ops that update skipped diagnostics.
- `paused = true` suppresses fixed phases and `VariableUpdate`, but still allows `BeginFrame`, `VariableAnimation`, `PreRender`, and `EndFrame` so visual interpolation, render prep, and diagnostics can keep running.
- Systems run in deterministic registration order within each phase.
- A system may register for multiple phases; it receives one callback per matching phase.
- The scheduler does not own fixed timestep accumulation. Existing `FixedStepLoop` or `App` calls `tickFixed()` the desired number of times.
- `Scene::updateWorldTransforms()` should be called by the scheduler at a deterministic point before `PreRender`, after all earlier variable/fixed transform mutation for that frame. Do not expose transform dirtiness as a separate system yet.

## Diagnostics And Failure Policy

- Use `std::chrono::steady_clock` to record per-phase elapsed microseconds in diagnostics.
- Diagnostics are plain Engine data and must not depend on ImGui, Renderer, App, or platform timing APIs.
- If callbacks are `void`, failures are limited to invalid lifecycle transitions and skipped phases.
- If callback status is added, use a simple enum and continue executing remaining systems while recording failed callback count. Do not add exceptions or error isolation frameworks.
- Keep diagnostics reset behavior explicit: per-frame counts reset at `BeginFrame`; lifetime registration counts do not.

## Out Of Scope

- No native actor/component behavior hooks beyond scheduler-level system callbacks.
- No Lua, reflection, serialization, asset registry, renderer bridge, physics, navigation, authored-scene adapter, or animation adapter.
- No async work queue integration, dependency graph solver, priority scheduler, task graph, thread pool, or global event bus.
- No automatic fixed timestep accumulator; existing engine/app timing owns accumulation.
- No debug UI. Future UI should read `SceneSchedulerDiagnostics`.

## Test Plan

- `LifecycleTransitions`: valid load/start/stop/unload transitions run callbacks in the expected order.
- `InvalidLifecycleTransitionsAreNoOps`: invalid transitions return false and do not run callbacks.
- `FramePhaseOrder`: `tickFrame()` executes variable phases in documented order.
- `FixedPhaseOrder`: `tickFixed()` executes fixed phases in documented order and increments fixed step index.
- `RegisteredSystemOrderIsDeterministic`: systems in the same phase run by registration order.
- `SystemEnableDisable`: disabled systems skip tick and lifecycle callbacks.
- `PausedSceneSkipsSimulation`: paused scenes skip fixed phases and variable update but still run begin/render/end phases.
- `TickIgnoredWhenNotStarted`: ticks before start or after stop do not call systems and update skipped diagnostics.
- `UnregisterInvalidatesHandle`: stale system handles fail validation after unregister/reuse.
- `RegisterWhileStartedRejected`: started scenes reject registration/unregistration.
- `TransformUpdateBeforePreRender`: transform world matrices are refreshed before `PreRender` callbacks observe them.
- `DiagnosticsReportPhaseCounts`: diagnostics record last phase, per-phase callback counts, skipped counts, and timing fields.
- `DestroyActorDuringTick`: callbacks can destroy/flush actors through public APIs without invalidating scheduler iteration.

## Build Integration

- Extend `manual_engine_scene_tests`; do not create Renderer, Assets, App, Navigation, Physics, scripting, or platform dependencies.
- Keep scheduler tests CPU-only and deterministic.
- Build `manual_engine` and run full CTest after implementation.

## Assumptions

- `Scene` remains the owner of actors, components, transforms, hierarchy, and the scheduler shell.
- `SceneSystemHandle` is transient and generation-counted, like actor/component handles, and must not be serialized.
- Existing `World`, `AuthoredScene`, `PartitionedAuthoredScene`, `AnimatedModel`, `ActorController`, `ActorSelection`, `FixedStepLoop`, and App frame flow remain unchanged.
- Future renderer, physics, navigation, animation, scripting, and native behavior phases will register scene systems against this scheduler rather than changing this phase's core ordering rules.
