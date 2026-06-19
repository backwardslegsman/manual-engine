# Phase 1 Plan: Scene Kernel

## Summary

Add the first scene runtime shell after the preliminary stabilization work is complete. This phase creates runtime identity, actor ownership, and opaque component attachment only. It must not add transforms, renderer components, asset registry integration, serialization, physics, scripting, navigation integration, or authored-scene conversion.

## Key Changes

- Add `Engine::Scene` as a renderer-independent owner for actor records and opaque component records.
- Add generation-counted `ActorHandle` and `ComponentHandle`; stale handles must fail validation after destruction and slot reuse.
- Add a stable scene object ID type for future serialization, but do not wire it into save/load yet.
- Add a minimal component type ID and attach/detach/query API that stores opaque component ownership without defining concrete components.
- Add actor lifecycle states for active, pending destroy, and destroyed; destruction should be deterministic and safe during iteration.
- Keep `World`, `AuthoredScene`, `PartitionedAuthoredScene`, and `AnimatedModel` behavior unchanged.

## Test Plan

- Create/destroy actors and verify handle validity.
- Reuse actor/component slots and verify stale handles are rejected.
- Attach/detach opaque components by type ID.
- Mark actors pending destroy during iteration and verify deterministic cleanup.
- Verify stable scene IDs remain distinct from runtime handles.
- Build tests without depending on Renderer, Assets, Physics, App, Navigation, or scripting code.

## Assumptions

- Runtime handles are transient and generation-counted.
- Stable scene IDs are future serialization identity only.
- Component storage starts opaque and minimal; concrete transform/render/physics components come in later phases.
- No migration from procedural `ObjectId` is attempted in this phase.
