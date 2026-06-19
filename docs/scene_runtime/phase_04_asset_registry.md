# Phase 4 Plan: Asset Registry

## Summary

Add a CPU-only `Engine::AssetRegistry` that provides stable asset identity, metadata records, import records, dependency queries, and stale/missing diagnostics above the current path-based import/cache systems.

This phase does not replace `Engine::AssetCache`, `AuthoredScene`, `PartitionedAuthoredScene`, `AnimatedModel`, renderer handles, authored scene cache files, or source importers. It adds a stable registry layer that later scene components can reference by asset handle or asset ID instead of raw paths.

## Goals

- Introduce asset handles and stable asset IDs that are distinct from scene actor/component handles, renderer handles, physics handles, navigation handles, and save-facing `ObjectId`.
- Track source paths, canonical paths, content hashes, source formats, import settings identity, dependencies, and diagnostics without creating GPU resources.
- Preserve existing synchronous path-based loading APIs while creating a compatibility path for registry-backed lookups.
- Give later render, authored-scene, animation, navigation, serialization, and scripting phases a stable asset-reference contract.
- Keep normal tests lightweight and avoid requiring optional Sponza-scale assets.

## Public Types

- Add `src/Engine/AssetRegistry.hpp` and `src/Engine/AssetRegistry.cpp`.
- Add generation-counted runtime handles:
  - `AssetHandle { uint32_t index = UINT32_MAX; uint32_t generation = 0; }`
  - `isValid(AssetHandle)`, equality, inequality.
- Add stable IDs:
  - `AssetId { uint64_t value = 0; }`
  - `0` is invalid/unassigned.
  - Generated from canonical path plus explicit import settings identity in this phase; do not serialize handles.
- Add `AssetType`:
  - `Unknown`
  - `StaticMesh`
  - `Texture`
  - `Material`
  - `AuthoredScene`
  - `SkinnedMesh`
  - `Skeleton`
  - `AnimationClip`
  - `NavigationSource`
- Add `AssetSourceFormat`:
  - `Unknown`
  - `Gltf`
  - `Glb`
  - `Fbx`
  - `Image`
  - `Yaml`
  - `Generated`
- Add `AssetStatus`:
  - `Registered`
  - `Missing`
  - `Stale`
  - `Invalid`
- Add `AssetImportSettingsKey`:
  - stable string `pipeline`
  - stable string `version`
  - optional string `optionsHash`
  - default empty settings are valid and deterministic.
- Add `AssetDescriptor` for registration:
  - source path
  - asset type
  - import settings key
  - optional explicit stable ID override for tests/tools only.
- Add `AssetMetadata`:
  - handle
  - asset ID
  - type
  - status
  - source path as provided
  - canonical absolute path when available
  - source format
  - content hash when file exists
  - import settings key
  - dependency asset IDs
  - diagnostics/warnings.

## Registry API Shape

```cpp
namespace Engine {
    class AssetRegistry {
    public:
        AssetHandle registerAsset(const AssetDescriptor& descriptor);
        bool unregisterAsset(AssetHandle asset);

        bool contains(AssetHandle asset) const;
        std::optional<AssetHandle> findById(AssetId id) const;
        std::optional<AssetHandle> findByCanonicalPath(
            const std::filesystem::path& path,
            AssetType type,
            const AssetImportSettingsKey& settings = {}) const;

        std::optional<AssetMetadata> metadata(AssetHandle asset) const;
        std::optional<AssetMetadata> metadata(AssetId id) const;
        std::vector<AssetHandle> assets() const;

        bool addDependency(AssetHandle asset, AssetHandle dependency);
        bool setDependencies(AssetHandle asset, const std::vector<AssetHandle>& dependencies);
        std::vector<AssetHandle> dependencies(AssetHandle asset) const;
        std::vector<AssetHandle> dependents(AssetHandle asset) const;

        bool refresh(AssetHandle asset);
        void refreshAll();
        AssetRegistryDiagnostics diagnostics() const;
    };
}
```

Implementation may adjust names for clarity, but behavior should stay explicit and narrow.

## Registration Behavior

- Registration canonicalizes source paths with `std::filesystem::weakly_canonical` when possible; if a path is missing, keep a normalized absolute path and mark status `Missing`.
- Re-registering the same canonical path, asset type, and import settings returns the existing live handle.
- Registering the same path with a different type or import settings creates a distinct asset record and asset ID.
- Runtime handles are transient and generation-counted. Unregistering invalidates stale handles; stable `AssetId` remains the identity for later serialization.
- Content hash is computed for existing regular files using a simple deterministic hash helper. Prefer reusing existing hash helpers if practical; otherwise add a small local FNV-1a or equivalent stable file hash utility.
- Source format detection should use existing importer helpers where available:
  - use `Assets::Assimp::detectSceneSourceFormat` for authored scene/model formats.
  - infer common image extensions for textures.
  - keep unknown formats explicit.
- No importer should be run during registration except lightweight source-format detection and file hash. Importing meshes/materials/textures remains explicit future work.

## Import Records And Dependencies

- Store import records as metadata only in this phase:
  - imported scene records may summarize node, mesh, material, texture, light, skin, and animation counts when caller provides imported data.
  - static mesh records may summarize submesh/material counts.
  - texture records may store descriptor identity and image path metadata, but not renderer texture handles.
- Add helper functions that can register dependencies from `Assets::Assimp::ImportedScene`:
  - scene asset depends on texture assets referenced by imported materials/textures.
  - scene asset can register logical material, mesh, skeleton, and animation clip child assets using generated stable IDs under the source asset identity.
- Dependency edges store `AssetId`s internally, but public queries return live handles where possible. Missing dependency IDs should remain visible through diagnostics.
- Dependency insertion must reject self-dependencies and duplicate edges.
- This phase does not require a full dependency graph solver, reload propagation, or topological import execution.

## Diagnostics

- Add `AssetRegistryDiagnostics`:
  - total registered assets
  - live asset count by type
  - missing count
  - stale count
  - duplicate registration count
  - dependency edge count
  - last warning/message.
- `refresh(asset)` recomputes file existence/hash/source format and marks:
  - `Missing` if the file no longer exists.
  - `Stale` if an existing file hash changed from the stored hash.
  - `Registered` if present and unchanged.
- Diagnostics are plain Engine data. No ImGui, Renderer, bgfx, App, or platform UI dependency.

## Build Integration

- Add `src/Engine/AssetRegistry.cpp` to `manual_engine`.
- Add `manual_engine_asset_registry_tests` or extend an existing pure Engine test target. Prefer a new target if linking Assimp importer fixtures would otherwise pull renderer/authored runtime code.
- The registry tests may link `glm` and `assimp` only if they use imported-scene fixture parsing; they must not link Renderer, App, Navigation, Physics, or scripting.
- Keep optional heavy asset tests disabled by default. Use committed lightweight fixtures for dependency extraction.

## Compatibility Rules

- `Engine::AssetCache` remains the renderer-resource cache and continues to own cached renderer mesh/texture handles.
- `AuthoredSceneCache` remains the derived authored-scene payload cache. Asset registry identity may later feed its manifest, but this phase does not rewrite cache files.
- Existing `loadAuthoredScene`, `loadPartitionedAuthoredScene`, `AnimatedModel`, and path-based importer APIs continue to work unchanged.
- Components in later phases should store `AssetHandle` or `AssetId`; renderer resources still use renderer-owned handles.
- Registry records are not save data yet. Serialization integration waits for the serialization phase.

## Test Plan

- `RegisterAssetCreatesStableMetadata`: register a committed fixture path and verify handle, ID, type, canonical path, source format, hash, and status.
- `DuplicateRegistrationReturnsSameHandle`: same path/type/settings returns the existing handle and updates duplicate diagnostics.
- `DifferentSettingsCreateDistinctAsset`: same path with different import settings creates a different asset ID.
- `UnregisterInvalidatesHandle`: stale handle fails after unregister and reused slots increment generation.
- `MissingAssetReportsMissing`: missing path registers with explicit missing status and diagnostic.
- `RefreshDetectsStaleAndMissing`: file hash changes mark stale; deleted/missing files mark missing.
- `DependencyEdgesAreDeterministic`: add/set dependencies, reject duplicates/self-dependencies, query dependencies/dependents in stable order.
- `FindByIdAndPath`: lookup by stable ID and canonical path resolves live handles.
- `ImportedSceneRegistersDependencies`: lightweight authored fixture registers texture/material/mesh dependencies without creating renderer resources.
- `DiagnosticsCountsByTypeAndStatus`: diagnostics count registered, missing, stale, duplicate, and dependency edge totals.
- `AssetRegistryTargetStaysIndependent`: test target builds without Renderer, App, Navigation, Physics, or scripting sources.

## Out Of Scope

- No renderer resource creation or replacement of `AssetCache`.
- No async import jobs, file watching, hot reload, package format, editor browser, or asset database on disk.
- No component migration to asset handles yet.
- No serialization of scene asset references yet.
- No cache rewrite for authored scene or navigation derived data.
- No automatic dependency graph execution or streaming policy.

## Assumptions

- Asset registry lives in `src/Engine` because it is an Engine identity/metadata service above source import and renderer cache.
- Importers remain CPU-only and may provide metadata to the registry, but registry registration itself should stay cheap and explicit.
- Stable IDs must be deterministic across runs for the same canonical path, type, and import settings, but runtime handles are transient.
- Future phases can broaden metadata records after render components and serialization prove the required asset-reference shape.
