# Renderer Guide

`src/Renderer` owns bgfx-facing resources and rendering submission.

- Expose renderer resources through small handles, not raw bgfx handles, unless the API is explicitly a low-level utility.
- Keep public headers focused on renderer contracts: resource creation, instance updates, draw submission, and shutdown.
- Prefer small view/context structs, such as `RenderView`, when render submission needs more camera or viewport metadata.
- Do not let renderer code parse game-world formats directly. Use `src/Assets` import results as input.
- Keep material/shader interfaces documented whenever adding uniforms, samplers, vertex attributes, or shader variants.
- Prefer `MaterialHandle` assignment for renderable appearance; do not expand texture-only override paths.
- V1 materials are renderer-owned handles with explicit destruction, not asset-cache managed resources.
- Favor simple forward rendering, frustum/distance culling, and explicit batching before adding deferred rendering or GPU-driven systems.
- Keep conservative renderer bounds on draw resources for culling, but do not let visibility culling own world object, chunk, or terrain lifetime.
- Treat render groups as renderer metadata for chunk/sector stats, debug views, and future batching only; they must not load, unload, or own engine chunks.
- Keep the public mesh instance API stable while improving batching internally. CPU-side draw item sorting is the preferred first step before bgfx instance buffers.
- Treat render layers, visibility flags, and max draw distance as renderer submission controls only; they must not drive engine simulation or streaming lifetime.
- Terrain LOD selection lives in Engine terrain code; Renderer should only own the active terrain draw buffers it is handed.
- Route renderer debug panels and debug knobs through the Dear ImGui `DebugUi` layer instead of ad hoc SDL/bgfx overlays.
- Keep first-pass atmosphere simple: sky clear color, exponential distance fog, and directional sun settings before adding sky domes, clouds, or time of day.
- Preserve bgfx lifetime ordering: destroy renderer-owned buffers, textures, uniforms, and programs before `bgfx::shutdown()`.
