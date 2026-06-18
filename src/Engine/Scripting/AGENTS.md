# Scripting Guide

`src/Engine/Scripting` is reserved for future native behavior hook and Lua integration.

- Scripts should access scenes, actors, components, assets, physics, and navigation through opaque handles and approved APIs.
- Do not expose direct component storage to Lua.
- Reflected property metadata and getter/setter validation should exist before broad scripting access.
- Script hooks must run through the scene tick scheduler with clear phase ordering.
- Script errors should be reported as diagnostics and should not corrupt scene ownership.
- Keep script reload, lifetime, and execution budget behavior explicit.
