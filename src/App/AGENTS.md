# App Layer Guide

`src/App` is the executable composition layer.

- Keep this layer thin: initialize SDL/bgfx, construct engine services, load demo assets, and run the main loop.
- Do not add reusable renderer, world, input, asset, or gameplay behavior here.
- It is acceptable for sample code to use fallbacks so the executable starts without local assets, but document each fallback.
- Prefer calling public subsystem interfaces over reaching into implementation details.
- Consult `docs/system_contracts.md` before changing initialization order or cross-system wiring, and update it when App composition establishes a new contract.
- Debug UI control plumbing may live here, but the behavior it triggers should be implemented through public Engine/Renderer interfaces.
- If a helper in `main.cpp` becomes broadly useful or owns state beyond sample composition, move it behind an Engine service before adding more call sites.
- Actor command rules, formation math, selection semantics, and navigation query behavior belong in `src/Engine`; App should only gather the active target/input context and call those APIs.
- Worker job bodies that perform reusable Engine data work, such as cache file I/O or generated-data construction, should live in Engine helpers. App may decide when to enqueue jobs and how to commit results.
