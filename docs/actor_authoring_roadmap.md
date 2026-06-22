# Actor Authoring Roadmap

This roadmap covers the next editor/runtime milestone after the completed first editor vertical slice in `docs/editor_roadmap.md`: authoring scene actors, attaching typed components, binding native or Lua behavior, placing actors through the editor, and preparing a later FLECS game-state bridge.

Status: planned. This roadmap is the durable planning record for actor authoring work; it does not introduce runtime changes by itself.

## Goal

Editor-authored actors are existing `Engine::Scene` actors with durable identity, transform hierarchy, authoring metadata, typed component descriptors, and behavior bindings.

The first milestone prioritizes the runtime foundation before richer editor placement. The editor should eventually be able to create an actor, edit its transform and metadata, add/remove registered component types, edit reflected component descriptors, and bind native or Lua behavior without bypassing the existing scene, reflection, behavior, scripting, serialization, physics, navigation, renderer, or streaming ownership rules.

## Architecture Direction

- `Engine::Scene` remains authoritative for actor identity, transform hierarchy, lifecycle, and core scene serialization.
- `SceneActorHandle`, `SceneComponentHandle`, system handles, behavior handles, Lua handles, renderer handles, physics handles, navigation handles, terrain handles, streaming tokens, and opaque handles remain transient runtime state.
- `SceneObjectId` is the durable actor identity used by authoring records, serialization, editor selection, behavior bindings, and future FLECS mapping.
- Components are typed descriptors with optional runtime bindings into existing systems. A component may be pure data, such as stats, or system-backed, such as movement.
- Behavior uses `SceneBehaviorHooks` and `SceneLuaRuntime`; actor authoring should add durable binding metadata, not a second behavior runtime.
- FLECS is planned as a later bridge or mirror for game-world state queries. It is not the source of truth for scene transforms, live subsystem ownership, or the first actor-authoring implementation.

## Non-Goals

- Do not convert the engine to a generic ECS.
- Do not add a FLECS dependency in the first actor-authoring phases.
- Do not force renderer, physics, navigation, streaming, movement, Lua, or behavior systems to adopt FLECS or component iteration.
- Do not serialize renderer resources, physics bodies/colliders, navigation tiles/paths, streaming runtime state, Lua VM state, behavior handles, or opaque handles.
- Do not reopen the completed editor roadmap. This roadmap builds on the editor target, profile, reflection, UI, rebuild, live-apply, and tool-scripting foundation already recorded there.

## Phase A1: Authored Actor Metadata

Goal: add durable authoring metadata on top of existing scene actors.

Status: implemented. `Engine::ActorAuthoringStore` now owns durable actor metadata keyed by `SceneObjectId`, with display name, string tags, and one string layer. A1 also adds reflection descriptors/get-set helpers and optional scene binary serialization for actor metadata. No editor placement, inspector UI, typed component descriptors, behavior binding metadata, or FLECS integration is part of this phase.

Implementation direction:

- Define an Engine-side actor authoring record keyed by `SceneObjectId`.
- Store editor-facing actor metadata such as display name, tags, layers/categories, selection identity, and optional ownership policy hints.
- Keep transform data in `Engine::Scene`; authoring records reference actors by durable identity, not by duplicating transform authority.
- Support actor creation flows that always assign valid `SceneObjectId` values for authored actors.
- Expose actor metadata through reflection for editor inspection and future scripting/tooling.

Exit criteria:

- Authored actors can be created with stable IDs, names, tags/layers, and editable transforms.
- Metadata survives save/load without serializing runtime handles.
- Editor selection can refer to durable actor identity even when runtime handles are recreated.

## Phase A2: Typed Component Descriptor Registry

Goal: define how authored component types are registered, validated, reflected, serialized, and rebound to runtime systems.

Status: implemented. `Engine::ActorComponentDescriptorRegistry` registers typed component descriptors by `SceneComponentTypeId`, and `Engine::ActorComponentDescriptorStore` owns durable component instance metadata keyed by `ActorComponentId`. A2 adds generic metadata reflection, deterministic instance enumeration, fake-tested bind/unbind/apply callback contracts, and optional scene binary serialization for generic component metadata. Concrete `Stats`, `Movement`, and `Sensory` payload schemas remain A3 work.

Implementation direction:

- Add a component descriptor registry with stable component type IDs, display names, categories, default descriptor construction, validation, reflection registration, serialization policy, and runtime binding callbacks.
- Keep `Engine::Scene` component storage lightweight. Scene component records continue to associate an actor with a component type; type-specific descriptor payloads live in an Engine-side authoring/runtime layer.
- Allow component types to be pure data or system-backed.
- Runtime binding callbacks should be explicit: bind, unbind, and apply descriptor changes. These callbacks may create transient subsystem records, but must not hide expensive loading or streaming behind generic setters.
- Reject duplicate component type IDs, missing metadata, invalid defaults, and descriptors that fail validation.

Exit criteria:

- A registered component type can be enumerated by editor tooling.
- Default component descriptors validate.
- Component descriptors can be read/written through reflection without exposing subsystem storage.
- System-backed components can create and destroy transient runtime bindings deterministically.

## Phase A3: First Component Set

Goal: validate the component model with a small set of game-facing components.

Initial components:

- `Stats`: a pure reflected data component for RPG-style attributes and resource values.
- `Movement`: a descriptor-backed component that binds an actor to `SceneCharacterMovementSystem`.
- `Sensory`: an initial descriptor or stub for world-query settings such as radius, field of view, faction filters, and query cadence.

Implementation direction:

- Keep `Stats` as durable plain data with no live subsystem handles.
- Keep `Movement` descriptors durable, but recreate `SceneCharacterHandle` and owned physics collider/body state through `SceneCharacterMovementSystem` at runtime.
- Keep `Sensory` minimal until the world-query service is defined. It should prove descriptor/reflection/editor flow without committing to AI architecture too early.
- Component descriptors should support validation and editor-visible reflection from the start.

Exit criteria:

- The three first components can be attached, inspected, edited, serialized, and restored.
- The movement component can bind/unbind to character movement without serializing `SceneCharacterHandle`.
- Invalid component descriptors fail validation before mutating live runtime state.

## Phase A4: Behavior Binding Metadata

Goal: author behavior attachments for actors and components without exposing runtime callback storage.

Implementation direction:

- Add durable behavior binding descriptors for native C++ behavior types and Lua scripts.
- Store behavior type or script identity, target actor/component stable identity, enabled state, scheduler phases, priority, function names where relevant, and reflected configuration values.
- Bind descriptors to `SceneBehaviorHooks` and `SceneLuaRuntime` after scene actors and component descriptors are restored.
- Keep lifecycle names explicit:
  - `onInit` for binding/load-time setup;
  - `onSpawn` for active runtime promotion;
  - `onDespawn` for runtime demotion or streaming removal;
  - `onDestruct` for permanent actor/component destruction;
  - existing tick and property-change hooks for behavior updates.
- Distinguish spawn/despawn from permanent destruction so streaming demotion does not look like actor deletion.

Exit criteria:

- Native and Lua behaviors can be bound to actor and component targets through durable metadata.
- Behavior binding restore creates transient behavior/script handles through existing systems.
- Behavior failures remain isolated and diagnosable through existing behavior/Lua diagnostics.

## Phase A5: Serialization And Runtime Rebind

Goal: extend scene save/load from metadata-only components to authored actor/component/behavior records.

Implementation direction:

- Extend scene serialization planning to include actor authoring metadata, typed component descriptors, and behavior binding metadata.
- Validate all actor IDs, component type IDs, component descriptors, behavior bindings, script references, and target references before mutating a live scene.
- Load core scene actors and hierarchy first, then restore authored metadata, then instantiate typed components, then bind runtime systems and behavior.
- Rebuild live resources through public subsystem APIs only after durable records validate.
- Preserve deterministic output ordering by stable actor ID, component type ID, component order, and behavior binding order.

Exit criteria:

- Authored actors, component descriptors, and behavior bindings round-trip deterministically.
- Runtime handles are absent from serialized payloads.
- Failed validation leaves the target scene and runtime systems unchanged.

## Phase A6: Editor Selection, Placement, And Inspector

Goal: make the actor-authoring foundation usable from `manual_engine_editor`.

Implementation direction:

- Add editor-side selection state keyed by durable actor/component identity, with runtime-handle lookup performed only through validated scene mappings.
- Add viewport picking and placement using the existing cursor ray boundary.
- Add actor creation, deletion, transform editing, parent/child editing, name/tag/layer editing, and selected-actor diagnostics.
- Add an inspector that enumerates registered component types, adds/removes components, edits reflected descriptors, and shows validation failures.
- Add behavior binding inspection and basic add/remove/edit for native and Lua bindings.
- Keep the first UI Dear ImGui-based and practical. Dockspace, layout persistence, undo/redo, prefab workflows, asset browser integration, and gizmo polish can be separate roadmaps.

Exit criteria:

- A user can create an authored actor in the editor, place it in the scene, edit its transform, attach the first component set, and bind a behavior.
- Invalid edits are rejected with diagnostics before subsystem mutation.
- `manual_engine` remains sample-oriented and does not invoke editor actor-authoring UI.

## Phase A7: Streaming And Spawn/Despawn Semantics

Goal: make actor authoring compatible with open-world streaming ownership.

Implementation direction:

- Define actor ownership policies:
  - `Global`: actor exists independently of streamed chunks.
  - `ChunkOwnedStatic`: actor lifetime follows a specific streamed scene chunk.
  - `Migratory`: actor is claim-tracked by durable identity so overlapping chunks cannot duplicate it.
- Ensure streaming promotion creates runtime bindings and fires spawn-style behavior events.
- Ensure streaming demotion releases runtime bindings and fires despawn-style events without treating the actor as permanently destructed.
- Keep automatic bounds-based migration out of the first pass unless a concrete gameplay need appears.

Exit criteria:

- Chunk-owned actor promotion/demotion does not leak renderer, physics, navigation, movement, behavior, or Lua runtime handles.
- Global actors are not duplicated by chunk reloads.
- Despawn/destruct semantics are visibly distinct in diagnostics and behavior callbacks.

## Phase A8: Future FLECS Bridge

Goal: prepare for FLECS as a game-world state database without making it the scene authority.

Implementation direction:

- Add FLECS only after the actor authoring model is stable.
- Treat FLECS entities as transient mirrors or indexes of authored scene actors and selected component state.
- Maintain explicit mappings between durable scene identity and FLECS entities, such as `SceneObjectId <-> flecs entity`.
- Candidate mirrored data includes tags, relationships, faction, squad/ownership, stats, AI/world-state data, sensory summaries, and query-friendly gameplay state.
- Do not let FLECS own scene transform hierarchy, renderer resources, physics bodies/colliders, navigation tiles/paths, streaming resource lifetime, Lua VM state, or behavior handles.

Exit criteria:

- FLECS-backed queries can find and relate game-state records while `Engine::Scene` remains authoritative for actor transform and live subsystem ownership.
- Destroying/recreating runtime scene handles does not invalidate durable FLECS mapping semantics.
- FLECS integration can be disabled or omitted without breaking actor authoring, serialization, or editor placement.

## Roadmap-Level Interfaces

These names describe the intended shape and may change during implementation:

- `ActorAuthoringRecord`: durable actor metadata keyed by `SceneObjectId`.
- `SceneComponentDescriptorRegistry`: component metadata, reflection, validation, serialization, and runtime binding registry.
- `ComponentDescriptor`: durable typed component payload for pure-data or system-backed components.
- `BehaviorBindingDescriptor`: durable native/Lua behavior attachment metadata.
- `EditorActorSelectionState`: editor selection and inspector state over stable actor/component IDs.
- `SceneFlecsBridge`: future transient mapping between `SceneObjectId` and FLECS entities.

## Test And Acceptance Plan

- Actor metadata creates valid stable IDs and preserves transform/hierarchy authority in `Scene`.
- Component registry rejects duplicate component type IDs and invalid descriptor defaults.
- `Stats` component round-trips as durable reflected data.
- `Movement` component creates and destroys a `SceneCharacterMovementSystem` binding without serializing `SceneCharacterHandle`.
- `Sensory` component validates and reflects descriptor data without requiring an AI runtime.
- Native and Lua behavior bindings call lifecycle/tick/property hooks through existing behavior systems.
- Serialization rejects runtime handles and validates before mutation.
- Editor placement creates actors with durable IDs and editable transforms.
- Editor inspector edits reflected component descriptors without directly mutating subsystem storage.
- Streaming promotion/demotion fires spawn/despawn semantics without leaking runtime bindings.
- Future FLECS bridge tests prove `Scene` remains authoritative and FLECS mappings are transient.

Required verification for implementation passes:

- focused actor-authoring runtime tests;
- serialization and validation tests for new durable records;
- editor UI model tests for selection, placement, inspector rows, and invalid edit rejection;
- behavior/Lua binding tests;
- affected scene, character movement, serialization, and editor tests;
- `cmake --build --preset windows-vs-vcpkg-debug`.

## Assumptions And Constraints

- `Engine::Scene` remains the authoritative actor/transform owner.
- First implementation work starts with runtime foundation, not viewport polish.
- Components may be more complex than ECS data records, but their durable descriptors remain plain validated data.
- Runtime bindings are explicit and transient.
- FLECS is a future bridge/mirror for game-state queries, not a dependency of the initial actor-authoring system.
- Future gameplay systems may opt into FLECS where it helps, but actor authoring must not require every system to become ECS-driven.
