# ManualEngine Agent Guide

ManualEngine is intended to grow into a simple open world game engine. Keep changes boring, explicit, and easy to inspect.

## Architecture Priorities

- Prefer small subsystems with clear ownership over broad framework code.
- Public headers are the contract. Keep them documented, stable, and free of implementation storage details.
- Use handle-based APIs for renderer-owned GPU resources and transient scene/terrain/streaming runtime records.
- Keep `src/App` as composition/sample code only. Engine behavior belongs under `src/Engine`, rendering under `src/Renderer`, and import/asset decoding under `src/Assets`.
- Use Dear ImGui for debug display and runtime debug knobs. Keep debug UI wiring in renderer/app composition code, not buried inside engine simulation systems.
- Do not introduce an ECS, task graph, or editor layer without a focused roadmap. The active runtime already includes scene scripting and open-world streaming; extend those systems rather than adding parallel frameworks.
- Keep `docs/engine_overview.md` current when subsystem ownership or data flow changes.
- Consult `docs/system_contracts.md` before changing cross-system behavior, and update it when adding features, public interface contracts, or initialization dependencies.
- Consult `docs/system_inboxes.md` before adding message-driven cross-system communication; prefer receiver-owned typed inboxes and explicit publish-only sinks over global bus registries.
- Consult `docs/authored_scene_roadmap.md` before adding glTF/PBR authored scene loading, material, texture, lighting, or scene streaming work.
- Consult `docs/scene_component_roadmap.md` before adding scene, actor/entity, component, transform hierarchy, asset registry, physics, scripting, serialization, or cross-runtime integration work.
- Consult `docs/terrain_rework_roadmap.md` and `docs/open_world_streaming_roadmap.md` before changing terrain ownership, heightmap import, derived cache payloads, baked nav/physics data, or streaming residency behavior.
- After each feature/refactor pass, append a short dated entry to `docs/work_log.md` describing what changed and why.

## Open World Direction

The active architecture is modern scene-backed open-world streaming:

1. `Scene` owns actors/components, transform hierarchy, lifecycle, and scheduler ordering.
2. `TerrainDataset` owns CPU terrain source/chunk records; terrain render/nav/physics/material/cache/serialization outputs are explicit adapters.
3. `OpenWorldStreamingRuntime` owns residency planning, async cache/generation queues, and budgeted live promotion/demotion callbacks.
4. `ScenePhysicsWorld`, `SceneNavigationService`, `NavigationConnectivitySystem`, and `SceneCharacterMovementSystem` are the gameplay runtime path.
5. Legacy procedural world, legacy terrain owner, legacy authored scene owner, and legacy animated model owner have been removed. Do not add compatibility branches for them.

## Code Style

- Use C++20, RAII where ownership is local, and explicit shutdown functions where bgfx lifetime ordering requires it.
- Avoid global mutable state except inside small renderer/engine implementation modules that expose handle APIs.
- Keep comments focused on non-obvious ownership, lifetime, coordinate-system, or API behavior.
- Return clear failure states instead of silently falling back, except in sample/demo code where fallback behavior is documented.
- Do not hide expensive work behind setters; loading, allocation, and streaming should be explicit API calls.
- When `src/App/main.cpp` starts accumulating reusable behavior, extract that behavior into a narrow Engine or Renderer API before extending it further.

## Testing And Verification

- Build with `cmake --build --preset windows-vs-vcpkg-debug`.
- Prefer importer/resource tests for pure logic and runtime smoke tests for SDL/bgfx integration.
- Keep sample assets optional unless they are committed and license-safe.
