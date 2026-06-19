# Phase 1 Plan: Scene Kernel

## Summary

Add the first scene runtime shell after the preliminary stabilization work is complete. This phase creates runtime identity, actor ownership, and metadata-only component attachment. It must not add transforms, renderer components, asset registry integration, serialization, physics, scripting, navigation integration, or authored-scene conversion.

This plan deliberately avoids the existing `Engine::ActorHandle` name used by `ActorController`. The new scene runtime uses `SceneActorHandle` until the procedural actor stack is migrated or renamed in a later phase.

## Key Changes

### Public Types

- Add `src/Engine/Scene/Scene.hpp` and `src/Engine/Scene/Scene.cpp`.
- Add `Engine::Scene` as a renderer-independent owner for scene actor records and metadata-only component records.
- Add generation-counted runtime handles:
  - `SceneActorHandle { uint32_t index = UINT32_MAX; uint32_t generation = 0; }`
  - `SceneComponentHandle { uint32_t index = UINT32_MAX; uint32_t generation = 0; }`
- Add `isValid(SceneActorHandle)`, `isValid(SceneComponentHandle)`, equality operators, and invalid sentinels through default construction.
- Add `SceneObjectId { uint64_t value = 0; }` as future stable serialized identity. `0` is invalid/unassigned. Do not connect it to save/load.
- Add `SceneComponentTypeId { uint32_t value = 0; }`. `0` is invalid. Component type IDs are caller-supplied stable IDs in this phase; no registry or reflection system is added.
- Add actor state enum:
  - `SceneActorState::Active`
  - `SceneActorState::PendingDestroy`
  - destroyed actors are represented by invalid/free slots, not an externally observable live state.

### Scene API

Add this minimum API shape:

```cpp
namespace Engine {
    class Scene {
    public:
        SceneActorHandle createActor(SceneObjectId stableId = {});
        bool destroyActor(SceneActorHandle actor);
        bool flushDestroyedActors();

        bool contains(SceneActorHandle actor) const;
        std::optional<SceneObjectId> stableId(SceneActorHandle actor) const;
        std::optional<SceneActorState> actorState(SceneActorHandle actor) const;

        SceneComponentHandle attachComponent(SceneActorHandle actor, SceneComponentTypeId type);
        bool detachComponent(SceneComponentHandle component);
        bool contains(SceneComponentHandle component) const;
        std::optional<SceneActorHandle> componentOwner(SceneComponentHandle component) const;
        std::optional<SceneComponentTypeId> componentType(SceneComponentHandle component) const;

        std::vector<SceneComponentHandle> components(SceneActorHandle actor) const;
        std::optional<SceneComponentHandle> firstComponent(SceneActorHandle actor, SceneComponentTypeId type) const;

        void forEachActor(const std::function<void(SceneActorHandle)>& callback) const;
    };
}
```

Implementation notes:

- `destroyActor()` marks the actor `PendingDestroy`, invalidates normal `contains(actor)` checks immediately, and detaches its components during `flushDestroyedActors()`.
- `flushDestroyedActors()` reclaims pending actor slots and their component slots deterministically in ascending slot order.
- `forEachActor()` visits active actors only and skips pending-destroy/free slots.
- Attaching a component to a pending-destroy or invalid actor fails and returns an invalid `SceneComponentHandle`.
- Detaching a component immediately invalidates that component handle and removes it from the owner actor's component list.
- Slot reuse must increment generation so stale actor and component handles never validate after reuse.
- Components store only owner handle, type ID, generation, and lifecycle/free-list metadata. No typed payload storage, transform data, names, tags, reflection, or serialization fields are added in this phase.
- Keep `World`, `AuthoredScene`, `PartitionedAuthoredScene`, `AnimatedModel`, `ActorController`, and `ActorSelection` behavior unchanged.

### Build Integration

- Add a new `manual_engine_scene_tests` CTest target.
- The scene test target should compile only the scene kernel implementation and test file. It must not link Renderer, Assets, App, Navigation, Physics, or scripting code.
- Add `src/Engine/Scene/Scene.cpp` to `manual_engine` only if needed for public build coverage; no app code should instantiate `Scene` in this phase.

## Test Plan

- `CreateDestroyActor`: create an actor, verify `contains`, stable ID query, state query, destroy it, verify `contains` is false, flush, and verify stale handle remains invalid.
- `ActorSlotReuseInvalidatesStaleHandle`: create/destroy/flush one actor, create another actor reusing the slot, and verify generation changed.
- `ComponentAttachDetach`: attach multiple metadata-only components with caller-supplied type IDs, query owner/type, query all components, query first component by type, detach one, and verify stale component handle is invalid.
- `DestroyActorDetachesComponents`: attach components, call `destroyActor`, verify new attach fails, flush, and verify all previous component handles are invalid.
- `IterationSkipsPendingDestroy`: create several actors, destroy one during or before iteration, verify iteration visits active actors only and order is deterministic by slot index.
- `InvalidInputsAreNoOps`: destroying invalid/stale actors, detaching invalid/stale components, querying invalid handles, and attaching invalid component type ID must fail cleanly without mutation.
- `StableIdIsNotRuntimeHandle`: verify `SceneObjectId` values can duplicate neither handle index nor generation assumptions and are returned only as explicit stable metadata.

## Assumptions

- Runtime handles are transient and generation-counted.
- Stable scene IDs are future serialization identity only.
- `SceneActorHandle` is used instead of `ActorHandle` to avoid colliding with the existing procedural actor controller.
- Component storage starts metadata-only and minimal; concrete transform/render/physics components come in later phases.
- Pending-destroy actors stop validating immediately and are physically reclaimed only by `flushDestroyedActors()`.
- Free-list reuse is allowed, but generation increments are mandatory.
- No migration from procedural `ObjectId` is attempted in this phase.
