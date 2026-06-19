# T0 Heightmap Terrain Preflight

This preflight contract locks the decisions needed before Phase T1 heightmap import planning. It is documentation-only. It does not add importer code, tests, utilities, runtime terrain types, cache code, renderer work, navigation work, physics work, or serialization.

## Coordinate Contract

Imported heightmaps default to configurable north-up:

- Image top-left is northwest.
- Image columns map to world `+X`.
- Image rows map to world `-Z`.
- Sample values map to world `Y`.
- Import settings must be able to flip rows and axes when a source uses a different convention.

T1 should treat this as the default convention for all imported heightmaps. Any fixture or validation asset that uses a different convention must express that through import settings rather than custom importer behavior.

## Real Validation Asset

The repo contains a real heightmap for validation:

- Path: `assets/heightmaps/47_648_-122_332_13_505_505_16bit.png`
- Dimensions: `505 x 505`
- Format: 16-bit grayscale PNG
- Approximate file size: 511 KB

This asset is opt-in validation data. It should not be required by normal CTest runs. T1 may use it for manual or explicitly enabled validation of decode correctness, import scale, chunk extraction, generated cache identity, LOD generation, navigation data, and physics collider generation.

## Fixture Policy

Normal automated tests should use tiny deterministic fixtures:

- known-value heightmaps for decode and normalization checks;
- chunk-boundary fixtures for border behavior;
- slope fixtures for material rule and normal/slope diagnostics;
- missing/stale source fixtures for diagnostics and asset identity;
- procedural settings fixtures for compatibility with non-heightmap sources.

The real heightmap should remain outside default automated tests so failures stay small and normal test runs do not depend on larger validation assets.

## T1 Import Settings Contract

Phase T1 import settings must include:

- source path;
- optional source asset ID override for tests and tools;
- input channel selection;
- bit-depth and normalization behavior;
- sample spacing in world units;
- height scale;
- height offset;
- source origin;
- axis and row flip flags;
- chunk sizing;
- border sample policy;
- missing or invalid sample policy.

T1 must decode CPU data only. It must not call `Renderer::loadTexture`, allocate bgfx resources, create renderer terrain handles, build navigation tiles, create physics colliders, or mutate live terrain runtime state.

## Chunking Contract

Phase T1 does not need to implement the full runtime terrain owner, but it must define imported chunk descriptors that can become stable runtime chunks later.

Chunking rules:

- Source chunks use stable source-space coordinates.
- Chunk identity must be durable enough for future chunked serialize/deserialize.
- Runtime chunk handles must be transient and must never be serialized.
- Adjacent chunks must have a defined border policy so later LOD, navigation, and physics outputs can avoid cracks.
- Chunk descriptors should preserve enough source revision and import settings identity to invalidate stale derived data.

## Asset And Cache Boundary

Source heightmaps are source assets. Generated outputs are derived assets.

Source assets:

- heightmap source files;
- procedural source descriptors;
- terrain material-set descriptors;
- optional material masks.

Derived assets:

- imported chunk height payloads;
- render LOD CPU mesh payloads;
- navigation build data or final tile bytes;
- physics collider triangle meshes;
- material layer weights or masks;
- terrain diagnostics summaries.

Generated outputs need explicit cache keys. They must not be treated as live renderer, navigation, or physics handles. Live renderer/nav/physics resources remain explicit main-thread resources created from derived data.

## T1 Readiness Checklist

T1 planning is ready when:

- the configurable north-up coordinate contract is referenced by the T1 plan;
- the real heightmap facts and opt-in validation role are recorded;
- tiny fixture policy is preserved for normal CTest;
- CPU-only importer dependency direction is explicit;
- source asset identity and derived cache identity are separated;
- durable source/chunk identity expectations are documented;
- T1 non-goals explicitly exclude renderer, nav, physics, terrain runtime mutation, and serialization.
