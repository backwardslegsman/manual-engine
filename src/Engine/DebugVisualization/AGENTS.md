# Debug Visualization Guide

`src/Engine/DebugVisualization` is reserved for shared debug visualization requests and diagnostics.

- Systems should submit debug visualization requests without depending on Dear ImGui or renderer internals.
- Keep categories explicit: transforms, bounds, navigation, paths, physics, skeletons, cameras, lights, streaming sectors, and resources.
- Debug draw generation must be budgeted and capped.
- Release builds should keep debug visualization disabled unless an explicit runtime/build policy enables it.
- Renderer/App may decide how to display requests, but Engine systems should own the meaning of their diagnostics.
