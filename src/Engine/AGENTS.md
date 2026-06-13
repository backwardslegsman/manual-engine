# Engine Guide

`src/Engine` should contain reusable game engine behavior that is not renderer-specific.

- Build toward a small `World` abstraction before adding specialized open-world systems.
- Keep world objects independent from renderer handles where practical; renderer bindings should be explicit.
- Add input as a frame snapshot/state API over SDL events, not as direct polling scattered through gameplay code.
- Keep input behavior mapped through semantic actions loaded from `assets/config/input.yaml`; engine systems should consume input action events instead of physical keys/buttons.
- Use the simple frame `EventQueue` for new engine events until there is concrete pressure for subscriber-owned buses or multiple specialized queues.
- Add camera behavior as engine-level state that produces view/projection inputs for the renderer.
- Keep first-pass actors kinematic and terrain-grounded. Actor controllers should consume semantic input events and write through `World`, not bypass world ownership.
- For open-world features, start with explicit chunk coordinates, load/unload calls, and simple distance-based policies.
- Keep first-pass chunk streaming synchronous and grid-based. Chunks should own membership lists, while `World` owns object state and `Renderer` owns draw resources.
- Keep procedural chunk content behind small descriptor/config interfaces. App code may translate descriptors into renderer/world resources, but placement rules should not live inline in the sample loop.
- Keep terrain as deterministic chunk-aligned heightfield tiles until there is a concrete need for authored heightmaps, terrain editing, LOD, or material splat blending.
- Keep terrain LOD as a renderer-mesh detail owned by `TerrainSystem`; gameplay height queries must continue to use CPU terrain data, not the active render LOD.
- Use simple skirts as the first terrain LOD seam-hiding strategy; defer stitching or geomorphing until visual cracks become a concrete blocker.
- Route reusable static meshes and shared textures through `AssetCache`; keep transient terrain tiles owned by the chunk/terrain systems.
- Keep first-pass persistence high-level: save player/camera state, seed/settings, persistent object overrides, and removed object records while regenerating terrain and baseline procedural content.
- Compare persistent objects, removed procedural props, quests, and interactions by stable `ObjectId`, never by transient `WorldObjectHandle`.
- Keep biome, procedural content, terrain, and persistence rules deterministic from world coordinates, chunk coordinates, stable object IDs, and saved settings.
- Debug/editor tools should mutate durable world state through `WorldObjectOverrides`, not by writing ad hoc serialized scene data.
- Avoid premature ECS architecture. Use plain structs and focused managers until entity/component needs are concrete.
