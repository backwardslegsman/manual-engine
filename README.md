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

## Repository Notes

Generated build outputs, local IDE configuration, and vcpkg installed packages are ignored by git. Keep shared build configuration in `CMakePresets.json`; use a local `CMakeUserPresets.json` for machine-specific overrides if your vcpkg installation lives somewhere else.
