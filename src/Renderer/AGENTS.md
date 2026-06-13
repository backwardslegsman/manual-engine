# Renderer Guide

`src/Renderer` owns bgfx-facing resources and rendering submission.

- Expose renderer resources through small handles, not raw bgfx handles, unless the API is explicitly a low-level utility.
- Keep public headers focused on renderer contracts: resource creation, instance updates, draw submission, and shutdown.
- Prefer small view/context structs, such as `RenderView`, when render submission needs more camera or viewport metadata.
- Do not let renderer code parse game-world formats directly. Use `src/Assets` import results as input.
- Keep material/shader interfaces documented whenever adding uniforms, samplers, vertex attributes, or shader variants.
- Favor simple forward rendering, frustum/distance culling, and explicit batching before adding deferred rendering or GPU-driven systems.
- Keep conservative renderer bounds on draw resources for culling, but do not let visibility culling own world object, chunk, or terrain lifetime.
- Route renderer debug panels and debug knobs through the Dear ImGui `DebugUi` layer instead of ad hoc SDL/bgfx overlays.
- Preserve bgfx lifetime ordering: destroy renderer-owned buffers, textures, uniforms, and programs before `bgfx::shutdown()`.
