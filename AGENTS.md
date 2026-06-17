# ManualEngine Agent Guide

ManualEngine is intended to grow into a simple open world game engine. Keep changes boring, explicit, and easy to inspect.

## Architecture Priorities

- Prefer small subsystems with clear ownership over broad framework code.
- Public headers are the contract. Keep them documented, stable, and free of implementation storage details.
- Use handle-based APIs for renderer-owned GPU resources and future world-owned entities.
- Keep `src/App` as composition/sample code only. Engine behavior belongs under `src/Engine`, rendering under `src/Renderer`, and import/asset decoding under `src/Assets`.
- Use Dear ImGui for debug display and runtime debug knobs. Keep debug UI wiring in renderer/app composition code, not buried inside engine simulation systems.
- Do not introduce an ECS, task graph, scripting layer, editor layer, or streaming system until a simpler interface proves insufficient.
- Keep `docs/engine_overview.md` current when subsystem ownership or data flow changes.
- Consult `docs/system_contracts.md` before changing cross-system behavior, and update it when adding features, public interface contracts, or initialization dependencies.
- Consult `docs/system_inboxes.md` before adding message-driven cross-system communication; prefer receiver-owned typed inboxes and explicit publish-only sinks over global bus registries.
- Consult `docs/authored_scene_roadmap.md` before adding glTF/PBR authored scene loading, material, texture, lighting, or scene streaming work.
- After each feature/refactor pass, append a short dated entry to `docs/work_log.md` describing what changed and why.

## Open World Direction

Add systems in this order unless a task says otherwise:

1. Engine loop and world ownership: fixed timestep hooks, scene/world lifetime, and clean startup/shutdown.
2. Camera and input: free camera, player-style camera, and SDL input state abstraction.
3. Spatial organization: simple world chunks/sectors with explicit load/unload APIs.
4. Asset cache: renderer/asset resource reuse, reference lifetime, and central path resolution.
5. Terrain: heightfield or mesh-tile terrain with material assignment.
6. Visibility: frustum culling and distance culling before any advanced occlusion.
7. Persistence: lightweight world description files only after runtime APIs are stable.

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
