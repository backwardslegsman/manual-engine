# Phase 2 Plan: Transform Hierarchy

## Summary

Add scene-owned transform data on top of the Phase 1 scene kernel. This phase introduces local transforms, cached world transforms, root/parent/child relationships, dirty propagation, and deterministic update/query behavior.

This phase must remain renderer-independent. It must not add renderer components, authored-scene conversion, asset registry integration, serialization, physics, scripting, navigation integration, tick scheduling, names, tags, editor UI, or migration from `World`/`AuthoredScene`/`AnimatedModel`.

## Key Changes

### Public Types

- Add transform data to `src/Engine/Scene/Scene.hpp` / `Scene.cpp`; do not create a separate broad transform manager unless the existing `Scene` class becomes hard to inspect.
- Add `Engine::SceneTransform` as the local transform value:
  - `glm::vec3 translation {0.0f, 0.0f, 0.0f}`
  - `glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f}`
  - `glm::vec3 scale {1.0f, 1.0f, 1.0f}`
- Add `Engine::SceneTransformUpdateResult` or equivalent small status enum for attach/reparent failures:
  - `Success`
  - `InvalidActor`
  - `InvalidParent`
  - `SelfParent`
  - `Cycle`
  - `NonDecomposableTransform`
- Store transform state as scene-owned actor metadata, not as typed component payload storage. The public API may call it transform data, but it should not require a real component registry yet.
- Keep runtime actor identity as `SceneActorHandle`; do not introduce a new transform handle.

### Scene API Shape

Add this minimum API surface:

```cpp
namespace Engine {
    struct SceneTransform {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    enum class SceneTransformUpdateResult {
        Success,
        InvalidActor,
        InvalidParent,
        SelfParent,
        Cycle,
        NonDecomposableTransform,
    };

    class Scene {
    public:
        bool hasTransform(SceneActorHandle actor) const;
        bool setLocalTransform(SceneActorHandle actor, const SceneTransform& transform);
        std::optional<SceneTransform> localTransform(SceneActorHandle actor) const;
        std::optional<glm::mat4> localMatrix(SceneActorHandle actor) const;
        std::optional<glm::mat4> worldMatrix(SceneActorHandle actor);

        std::optional<SceneActorHandle> parent(SceneActorHandle actor) const;
        std::vector<SceneActorHandle> children(SceneActorHandle actor) const;
        std::vector<SceneActorHandle> roots() const;

        SceneTransformUpdateResult attachChild(
            SceneActorHandle child,
            SceneActorHandle parent,
            bool preserveWorldTransform = true);
        SceneTransformUpdateResult detachChild(
            SceneActorHandle child,
            bool preserveWorldTransform = true);
        SceneTransformUpdateResult reparent(
            SceneActorHandle child,
            SceneActorHandle newParent,
            bool preserveWorldTransform = true);

        void markTransformDirty(SceneActorHandle actor);
        void updateWorldTransforms();
    };
}
```

Implementation may name `attachChild`/`detachChild` differently if the final API reads better, but the behavior must be equivalent and explicit.

### Transform Behavior

- Every active actor has a default identity transform from creation. There is no optional transform component in this phase.
- `hasTransform(actor)` is true only for active actors and false for invalid, stale, pending-destroy, or flushed actors.
- Local transforms are TRS values; world matrices are cached `parentWorld * localMatrix`.
- Matrix composition order is translation * rotation * scale, using the engine's existing GLM conventions.
- Scale may be non-uniform. Zero and negative scale are allowed because validation policy belongs to future render/physics systems, not the scene kernel.
- Setters must be cheap except for marking descendants dirty. Full world-matrix recomputation happens through `worldMatrix(actor)` lazy evaluation or `updateWorldTransforms()`.
- Dirty propagation marks the actor and all descendants dirty when local transform or hierarchy changes.
- `worldMatrix(actor)` updates only the requested actor's ancestor chain and dirty descendants needed for a correct result.
- `updateWorldTransforms()` updates all active actors deterministically in root/child slot order.
- `forEachActor()` remains active-actor iteration only; do not change Phase 1 lifecycle semantics.

### Hierarchy Behavior

- Actors without parents are roots.
- Parent/child storage is owned by `Scene`; children are returned in deterministic insertion order.
- Reparenting must remove the child from the old parent's child list exactly once and append it to the new parent.
- Attaching to an invalid, stale, pending-destroy, or flushed parent fails without mutation.
- Attaching an actor to itself fails.
- Attaching an ancestor under its descendant fails with `Cycle`.
- Destroying an actor detaches its children to roots before the actor slot is reclaimed. Children preserve world transforms by default so destroying a parent does not move surviving actors.
- Destroying an actor still removes that actor's metadata-only components as Phase 1 defined.
- Pending-destroy actors are excluded from roots, children, transform queries, and hierarchy mutation.

### Preserve-World Rules

- `preserveWorldTransform = true` computes a new local transform so the actor's world matrix remains unchanged after attach/detach/reparent.
- `preserveWorldTransform = false` keeps the actor's local transform unchanged, so its world matrix changes under the new parent.
- For preserve-world operations, matrix decomposition may use GLM decomposition helpers if available; otherwise add a small internal helper with tests for translation, rotation, and non-uniform positive scale.
- If decomposition cannot represent a matrix reliably, return a clear failure result rather than silently changing the actor. Do not add fallback snapping or lossy correction in this phase.

## Out Of Scope

- No imported authored scene node conversion.
- No transform serialization.
- No renderer instance sync, bounds hooks, physics sync, navigation geometry extraction, animation skeleton integration, debug UI, or editor gizmos.
- No transform component type registration, reflection metadata, names, tags, or script access.
- No job system or worker-thread mutation. Scene hierarchy mutation remains main-thread owned.

## Test Plan

- `DefaultActorTransformIsIdentity`: new actors are roots with identity local/world transforms.
- `SetLocalTransformUpdatesWorld`: translation, rotation, scale, and non-uniform scale compose into expected world matrices.
- `AttachChildKeepLocal`: attach with `preserveWorldTransform = false` keeps local transform and changes world transform by parent matrix.
- `AttachChildPreserveWorld`: attach with `preserveWorldTransform = true` preserves world matrix and computes the expected new local transform.
- `DetachChildPreserveWorld`: detach from parent to root while preserving world transform.
- `ReparentPreserveWorldAndKeepLocal`: verify both preserve modes across two parents.
- `DirtyPropagation`: changing a parent marks descendants dirty and recomputes them deterministically.
- `RootsAndChildrenAreDeterministic`: roots and child lists are returned in stable slot/insertion order.
- `CycleAndInvalidParentRejected`: invalid parent, stale handles, self-parenting, and ancestor cycles fail without mutation.
- `DestroyParentDetachesChildren`: destroying/flushing a parent leaves children active roots with preserved world transforms.
- `DestroyChildRemovesFromParent`: destroying/flushing a child removes it from the parent child list.
- `PendingDestroyExcludedFromHierarchyQueries`: pending actors do not appear in roots, children, transforms, or mutation targets.
- `ActorSlotReuseClearsHierarchyState`: reused actor slots start as roots with identity transforms and no old children.

## Build Integration

- Extend `manual_engine_scene_tests`; do not create renderer, asset, app, navigation, physics, or scripting dependencies.
- Keep transform tests CPU-only and deterministic.
- Build `manual_engine` and run full CTest after implementation.

## Assumptions

- Scene transform hierarchy is the next layer on the Phase 1 scene kernel and does not replace `World` transforms yet.
- `SceneActorHandle` remains the only actor identity used by the hierarchy.
- Stable `SceneObjectId` is unaffected; hierarchy serialization waits for the serialization phase.
- Future render, physics, navigation, authored-scene, and animation adapters consume transform data through explicit bridge APIs added in later phases.
