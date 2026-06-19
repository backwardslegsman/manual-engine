# Scene Runtime Planning Index

This directory is reserved for follow-up plans and implementation notes for the scene/component runtime expansion.

Start with `docs/scene_component_roadmap.md`. Add focused phase plans here when a roadmap item is ready to implement, for example:

- `phase_01_scene_kernel.md`
- `phase_02_transform_hierarchy.md`
- `phase_03_tick_scheduler.md`
- `phase_04_asset_registry.md`
- `serialization_conventions.md`

Keep phase files scoped. They should describe one implementable slice, test coverage, migration impact, and any compatibility rules for existing procedural, authored scene, animated model, renderer, navigation, cache, and async paths.

Use `serialization_conventions.md` as the durable identity and binary-format checklist before adding save/load fields, stable IDs, or subsystem-specific serialized records. Phase plans may reference it instead of repeating runtime-handle exclusion rules.
