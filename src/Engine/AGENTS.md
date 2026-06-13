# Engine Guide

`src/Engine` should contain reusable game engine behavior that is not renderer-specific.

- Build toward a small `World` abstraction before adding specialized open-world systems.
- Keep world objects independent from renderer handles where practical; renderer bindings should be explicit.
- Add input as a frame snapshot/state API over SDL events, not as direct polling scattered through gameplay code.
- Keep input behavior mapped through semantic actions loaded from `assets/config/input.yaml`; engine systems should consume input action events instead of physical keys/buttons.
- Use the simple frame `EventQueue` for new engine events until there is concrete pressure for subscriber-owned buses or multiple specialized queues.
- Add camera behavior as engine-level state that produces view/projection inputs for the renderer.
- For open-world features, start with explicit chunk coordinates, load/unload calls, and simple distance-based policies.
- Keep first-pass chunk streaming synchronous and grid-based. Chunks should own membership lists, while `World` owns object state and `Renderer` owns draw resources.
- Keep terrain as deterministic chunk-aligned heightfield tiles until there is a concrete need for authored heightmaps, terrain editing, LOD, or material splat blending.
- Route reusable static meshes and shared textures through `AssetCache`; keep transient terrain tiles owned by the chunk/terrain systems.
- Avoid premature ECS architecture. Use plain structs and focused managers until entity/component needs are concrete.
