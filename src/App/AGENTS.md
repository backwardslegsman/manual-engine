# App Layer Guide

`src/App` is the executable composition layer.

- Keep this layer thin: initialize SDL/bgfx, construct engine services, load demo assets, and run the main loop.
- Do not add reusable renderer, world, input, asset, or gameplay behavior here.
- It is acceptable for sample code to use fallbacks so the executable starts without local assets, but document each fallback.
- Prefer calling public subsystem interfaces over reaching into implementation details.
