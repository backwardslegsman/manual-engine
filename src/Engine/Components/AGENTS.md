# Component Guide

`src/Engine/Components` is reserved for future reusable scene components.

- Components should be plain engine data plus narrow behavior hooks. Avoid direct bgfx, SDL, Lua, or importer dependencies.
- Use asset handles or stable asset IDs for asset references once the asset registry exists. Do not store raw renderer handles as serialized component state.
- Renderer-facing components should be consumed by a render submission system that owns renderer handle synchronization.
- Physics, navigation, animation, and scripting components should interact through public Engine services, not each other's internal storage.
- Reflected component variables must go through validated getter/setter APIs once reflection exists.
- Component serialization should store stable IDs and asset references, never transient runtime handles.
