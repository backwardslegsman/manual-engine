# Scene Runtime Guide

`src/Engine/Scene` is reserved for the future scene, actor/entity, component, transform hierarchy, lifecycle, and tick scheduling runtime.

- Keep scene runtime types renderer-independent. Renderer resources should be reached through explicit bridge systems and handles.
- Separate transient runtime handles from stable serialized IDs.
- Use generation-counted handles for actors and components.
- Keep component storage and public component APIs narrow until concrete systems need broader queries.
- Transform hierarchy belongs here, not in renderer, importer, or app code.
- Scene tick phases should be explicit and documented before systems depend on them.
- Main-thread ownership rules must remain visible. Worker jobs may produce plain data, but live scene, renderer, physics, navigation, and asset-cache mutation should commit through explicit main-thread APIs.
- Scene runtime is the active ownership model. Do not add dependencies on removed procedural world or legacy authored/animated owners.
- Update `docs/scene_component_roadmap.md` and `docs/system_contracts.md` when public scene contracts change.
