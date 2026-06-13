# ManualEngine

ManualEngine is a C++20 rendering project built with CMake, SDL3, bgfx, and vcpkg.

## Prerequisites

- CMake 3.31 or newer
- Visual Studio 2022 with the C++ desktop workload
- vcpkg, using the Visual Studio bundled vcpkg path configured in `CMakePresets.json`

## Build

Configure the project:

```powershell
cmake --preset windows-vs-vcpkg
```

Build a debug binary:

```powershell
cmake --build --preset windows-vs-vcpkg-debug
```

Build a release binary:

```powershell
cmake --build --preset windows-vs-vcpkg-release
```

The vcpkg manifest in `vcpkg.json` installs the required dependencies:

- bgfx
- SDL3
- assimp
- glm
- imgui
- stb
- yaml-cpp

## Runtime Assets

The sample app looks for `assets/models/sample_static.fbx`. If the file is missing, it falls back to a procedural textured cube so the renderer API can still be exercised.

Shader binaries are generated during the CMake build with bgfx `shaderc` from `assets/shaders/src` into `assets/shaders/dx11`.

Input bindings are loaded from `assets/config/input.yaml`. If the file is missing or invalid, the app falls back to built-in default camera mappings.

Dear ImGui is initialized by the sample app for renderer debug display and runtime debug knobs. Current debug UI shows renderer visibility/submission stats.

The sample world streams a 3x3 set of procedural XZ chunks around the camera pivot. Chunks currently spawn deterministic tinted cube objects and unload them synchronously when they leave range.

Each loaded chunk also owns one procedural heightfield terrain tile. Terrain is generated from deterministic world-space heights so neighboring chunk edges match, and sample props are placed by querying loaded terrain height.

## Repository Notes

Generated build outputs, local IDE configuration, and vcpkg installed packages are ignored by git. Keep shared build configuration in `CMakePresets.json`; use a local `CMakeUserPresets.json` for machine-specific overrides if your vcpkg installation lives somewhere else.

See `AGENTS.md` and the subsystem `AGENTS.md` files under `src` for architecture guidance. The near-term direction for open world work is: engine loop/world ownership, camera/input, simple chunks, asset caching, terrain, then visibility culling.
