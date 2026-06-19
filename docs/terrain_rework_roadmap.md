# Terrain Rework Roadmap

This roadmap defines a full terrain-system rework for ManualEngine. The target is a chunked terrain runtime where heightmap import is first-class, procedural terrain remains supported, generated assets are cached, and future chunked serialization can be added without replacing the runtime model.

The first implementation pass should focus on import, chunking, runtime ownership, derived-data cache identity, render LOD generation, navigation build data, physics collider generation hooks, and terrain material metadata. Runtime scene serialization, terrain editing, and durable chunk save/load are design constraints for this roadmap, but they are not part of the first pass.

Before planning Phase T1, complete the docs-only preflight contract in `docs/terrain_runtime/t0_heightmap_preflight.md`. T0 locks the coordinate convention, fixture policy, real-heightmap validation role, import settings inputs, chunk identity expectations, and source-versus-derived asset boundary that T1 depends on.

## Goals

- Treat heightmap import as the primary terrain source path.
- Keep procedural terrain as a compatible source path using the same chunk and derived-data contracts.
- Store terrain as deterministic CPU chunk data that can feed renderer LODs, navigation build input, physics colliders, picking, diagnostics, and future serialization.
- Cache generated terrain-derived assets with explicit source/import/settings identity.
- Support chunked load/unload and future chunked serialize/deserialize from files.
- Define terrain material metadata for PBR layer mapping driven by height, slope, world position, masks, and procedural rules.
- Keep gameplay queries on CPU terrain data, not active renderer LOD buffers.
- Keep renderer, navigation, physics, and asset-cache integration explicit.

## Non-Goals For The First Pass

- No scene serialization or chunk file save/load implementation.
- No terrain editor, sculpting tools, painting UI, or brush system.
- No automatic migration of existing procedural world saves.
- No virtual texturing, clipmaps, geomorphing, tessellation, or GPU terrain generation.
- No dynamic terrain deformation.
- No automatic App streaming policy rewrite beyond the minimum needed to consume the new terrain owner.
- No hidden navmesh or physics rebuilds from terrain setters.

## Current Baseline

Useful existing pieces:

- `Engine::TerrainSystem` owns CPU heightfield tiles, height sampling, picking/raycast helpers, terrain diagnostics, generated terrain tile data, renderer terrain handles, render LOD build inputs, and navigation build-data extraction.
- Terrain render LOD work already has worker-safe build inputs and main-thread renderer commit APIs.
- `NavigationTerrainBuildData` can already be produced from terrain tiles and generated terrain data.
- `SceneNavigationGeometryRegistry` can merge scene geometry with optional terrain build data for navigation snapshots.
- `ScenePhysicsWorld` supports static triangle mesh colliders from CPU vertices and indices.
- `Renderer::MaterialDescriptor` supports PBR texture slots and factors.
- `AssetRegistry` can track stable asset identity, source metadata, dependencies, stale/missing state, and import settings, but it has no terrain-specific asset types yet.

Current limitations:

- Terrain source data is procedural or raw explicit heights only; there is no CPU heightmap importer.
- Terrain tiles carry one renderer material handle, so terrain material layering and slope/world-position blending are not modeled.
- Terrain draw buffers are transient and not tied to generated asset cache identity.
- Terrain physics colliders are not created from terrain data.
- Navigation and renderer LOD rebuild dirtying is App-driven and should remain explicit, but the new terrain source owner needs clearer derived-data invalidation.
- Future serialization identity for terrain chunks does not exist yet.

Existing validation asset:

- `assets/heightmaps/47_648_-122_332_13_505_505_16bit.png` is a real large 16-bit heightmap available for manual or opt-in validation.
- Default automated tests should still use tiny deterministic fixtures so CTest stays fast and failure output stays easy to inspect.

## Target Architecture

The rework should separate terrain into five explicit layers:

1. **Source assets**
   - Heightmaps, procedural descriptors, material-set descriptors, optional masks, and future serialized terrain chunks.
   - Registered through `AssetRegistry` with stable source identity and import settings.

2. **Imported source data**
   - CPU-only decoded height samples and metadata.
   - No renderer handles, no bgfx resources, no live navigation or physics objects.

3. **Runtime terrain chunks**
   - Loaded CPU terrain chunk records with stable source/chunk identity, runtime handles, height samples, bounds, material metadata references, and dirty flags.
   - Own gameplay height queries and diagnostics.

4. **Derived terrain assets**
   - Renderer LOD meshes, navigation build geometry, physics collider meshes, material weight data, debug samples, and future serialized chunk payloads.
   - Cache keys must include terrain source ID, chunk coordinate, import settings, terrain generation version, material-rule version, and output kind.

5. **Live system resources**
   - Renderer terrain handles, navigation tile bytes, scene physics bodies/colliders, and debug records.
   - Created and destroyed by explicit adapters or commit APIs on the main thread.

## Core Data Model

Add or evolve toward these concepts:

- `TerrainSourceId`
  - Stable identity for a heightmap source, procedural generator, or future serialized terrain dataset.
  - Should map to `AssetId` once terrain-specific asset types exist.

- `TerrainChunkCoord`
  - Chunk coordinate in terrain source space. It may match existing `ChunkCoord`, but the contract should allow non-world-origin heightmaps and imported terrain datasets.

- `TerrainChunkId`
  - Durable identity composed from terrain source ID plus chunk coordinate.
  - Intended for future serialization and cache paths.

- `TerrainChunkHandle`
  - Runtime generation-counted handle for loaded chunk records.
  - Must not be serialized.

- `TerrainChunkData`
  - CPU height samples, sample spacing, origin, size, resolution, local/world bounds, material metadata references, and source revision.
  - Should be independent of renderer, navigation, and physics handles.

- `TerrainMaterialSet`
  - PBR terrain layer metadata, layer textures, rule constants, tiling settings, color-space hints, and fallback layer.
  - Runtime material resource handles should be produced by renderer-facing adapters, not stored as durable terrain identity.

- `TerrainDerivedAssetKey`
  - Deterministic cache key for generated LOD mesh, nav build data, physics collider mesh, material weights, and future chunk serialization.

## Heightmap Import

Heightmap import should be CPU-only and should not call `Renderer::loadTexture`.

Import settings should include:

- source path and optional source asset ID override for tests/tools;
- input channel selection;
- bit-depth and normalization behavior;
- explicit height scale and height offset;
- horizontal sample spacing;
- world origin and coordinate orientation;
- row/column flip options;
- missing/no-data behavior;
- chunk size in world units or samples;
- border sample policy for seamless chunk extraction;
- optional resampling filter;
- optional material mask paths.

Importer output should include:

- decoded source dimensions;
- source sample format;
- height range before and after scaling;
- generated chunk descriptors;
- warnings for clamping, unsupported format, non-square assumptions, missing border samples, or precision loss;
- dependency records for material masks and referenced textures.

Supported first formats can be deliberately small:

- PNG/TGA/JPEG through a CPU image decoder for 8-bit and 16-bit cases if available;
- a simple text/binary fixture format for deterministic tests if image decoding precision is awkward;
- optional EXR/TIFF support deferred unless a dependency is already acceptable.

## Procedural Terrain Compatibility

Procedural terrain should become another terrain source type rather than a separate runtime path.

- Procedural generator settings become source/import settings that participate in cache identity.
- Generated procedural chunks should produce the same `TerrainChunkData` shape as imported heightmap chunks.
- Biome sampling can remain procedural, but biome outputs should become material-rule inputs rather than directly choosing one material handle per tile.
- Existing deterministic chunk generation should be preserved until the new source layer proves stable.

## Chunking Contract

Terrain chunking should be explicit and deterministic:

- Chunks have stable source-space coordinates and world bounds.
- Adjacent chunks share compatible edge samples.
- Runtime chunks may load independently, but source import should define border behavior so LOD, physics, and nav data do not crack.
- Chunk records should keep CPU height data at the authoritative gameplay resolution.
- Derived outputs may use separate resolutions for rendering, navigation, and physics.
- Chunk load/unload should mark derived renderer, navigation, physics, and debug outputs dirty, but should not rebuild them synchronously.

Future serialization needs:

- chunk identity independent from runtime handles;
- source revision and import settings version stored with serialized chunks;
- per-chunk payloads that can be invalidated when source/import/material rules change;
- a format that can stream one chunk without loading the whole terrain dataset.

## Generated Asset Cache

Generated terrain outputs should be cached as derived assets, not treated as source assets.

Cache candidates:

- imported heightmap chunk height data;
- render LOD vertex/index buffers as CPU data;
- navigation build geometry or final tile bytes, depending on existing navigation cache policy;
- physics collider triangle meshes;
- material layer weights or masks;
- terrain diagnostics summaries.

Cache keys should include:

- terrain source ID;
- chunk coordinate;
- source content hash or procedural settings hash;
- import settings hash;
- terrain runtime schema version;
- derived output kind;
- output resolution or LOD index;
- material-rule version where relevant;
- navigation/physics agent or collider settings where relevant.

The cache should remain explicit:

- worker jobs may read/write derived payload files;
- live renderer, navigation, and physics resources are still main-thread commits;
- cache misses enqueue work rather than building inside gameplay queries;
- debug tools may expose cache rebuild actions.

## Terrain Materials And PBR Mapping

The rework should introduce terrain material metadata before changing renderer shaders.

Material layers should support:

- base color, normal, metallic, roughness, metallic-roughness, occlusion, and emissive texture references;
- texture color-space and sampler hints;
- world-space or UV-space tiling scale;
- triplanar or planar projection mode as future options;
- scalar factors matching renderer PBR material fields;
- debug name and stable layer ID.

Material rules should support:

- slope ranges;
- height ranges;
- world-position bands or noise inputs;
- biome/procedural tags;
- imported mask textures;
- priority and blend falloff;
- fallback layer selection.

Recommended first rendering model:

- CPU owns terrain material rules and can produce deterministic coarse layer weights for diagnostics/tests.
- Renderer initially supports a small fixed number of terrain layers per tile.
- GPU evaluates final PBR blending from terrain normal, world position, and uploaded rule/layer data.
- If texture binding limits become a blocker, add texture arrays or atlases as a separate renderer phase.

Do not use render LOD normals as the gameplay/material source of truth. CPU terrain normals/slope from authoritative height data should drive deterministic rule evaluation.

## Renderer Integration

Renderer integration should remain an adapter/commit layer:

- Runtime terrain chunks own CPU height data.
- Worker jobs build `TerrainRenderMeshData` or a successor plain data structure.
- Main thread commits renderer terrain handles.
- Material metadata resolves to renderer texture/material resources explicitly through `AssetCache` or a future terrain material resource cache.
- Terrain render resources can still be transient, but generated CPU mesh payloads may be cached.

Expected renderer changes after metadata is stable:

- terrain-specific material descriptor or terrain material-set resource;
- terrain layer texture binding strategy;
- optional per-vertex or per-tile layer weight data;
- diagnostics for submitted layer count, missing textures, fallback layers, and material rule coverage.

## Navigation Integration

Navigation should consume terrain through explicit build snapshots:

- Terrain chunks produce `NavigationTerrainBuildData` or a narrow successor.
- Build data should use navigation resolution, not render LOD resolution.
- Dirty terrain chunks mark navigation chunks dirty, but no terrain setter should build or insert live navmesh tiles.
- Existing navigation cache identity should include terrain source/import/settings revisions.
- Scene static geometry and terrain data should continue merging through `SceneNavigationGeometryRegistry` or a successor build-input aggregator.

## Physics Integration

Physics should consume terrain through explicit static-collider adapters:

- Terrain chunks produce CPU triangle mesh collider data at a physics-specific resolution.
- `ScenePhysicsWorld` creates static bodies/colliders on the main thread from that CPU data.
- Collider lifetime should follow terrain chunk lifetime.
- Dirty terrain chunks should mark collider rebuild requests, not rebuild inside height setters.
- Visual LOD meshes must not be used as physics source data.

Deferred:

- dynamic terrain deformation;
- material-dependent friction/restitution lookup;
- terrain collision events;
- physics debug rendering beyond existing debug request records.

## Future Serialization Design Constraints

Serialization is not in scope for the first pass, but the design must preserve these constraints:

- Runtime handles must not be serialized.
- Serialized terrain should reference stable terrain source IDs, material set IDs, chunk coordinates, source revisions, and import settings.
- Chunk files should be independently readable and writable.
- Serialized chunks should distinguish source-authored data from derived cached data.
- Cache payloads should be rebuildable and disposable.
- Edited terrain chunks should be representable as overrides against imported/procedural source data.
- Missing source assets should still allow loading serialized override chunks if their format supports it.

## Diagnostics And Debugging

Add diagnostics with each phase:

- import source size, height range, decode format, warnings, and chunk count;
- chunk load/unload counts and memory use;
- cache hit/miss/stale/write counts by derived output kind;
- render LOD build queue, commit count, stale result count, and active LOD distribution;
- material rule layer coverage and fallback coverage;
- navigation and physics dirty chunk counts;
- per-chunk source revision and derived-data revision;
- debug samples for slope, material layer, nav walkability, and collider bounds.

Diagnostics should be plain Engine data. Dear ImGui presentation remains App/Renderer composition.

## Implementation Phases

### Phase T0: Heightmap Terrain Preflight

Detailed preflight contract: `docs/terrain_runtime/t0_heightmap_preflight.md`.

Deliverables:

- Document the default configurable north-up heightmap coordinate convention.
- Record the real `assets/heightmaps` validation asset facts and opt-in role.
- Lock the fixture policy for small deterministic CTest inputs.
- Define the T1 import settings inputs and CPU-only importer boundary.
- Clarify source chunk identity, future serialization constraints, and generated derived-asset cache boundaries.

Exit criteria:

- T1 can be planned without reopening coordinate, fixture, decode-boundary, and source-versus-derived identity decisions.

### Phase T1: Terrain Source And Heightmap Import Plan

Status: initial CPU-only implementation added. `Assets::loadHeightmapImage` decodes 8-bit and 16-bit heightmap images into normalized CPU samples, and `Engine::importHeightmapTerrain` applies explicit import settings to produce world-size chunk descriptors. Terrain-specific asset registry types are present for heightmap, terrain source, terrain material set, and terrain chunk metadata. Runtime terrain ownership, derived cache files, renderer LOD commit, navigation build data, physics colliders, material rules, and serialization remain later phases.

Deliverables:

- Add CPU-only heightmap import types and settings.
- Add terrain-specific asset registry types or metadata conventions.
- Decode small committed fixtures into deterministic height samples.
- Convert imported source data into chunk descriptors.
- Keep output independent from renderer, nav, physics, and App streaming.

Exit criteria:

- A heightmap fixture can be decoded into deterministic chunk height data.
- Import settings changes produce distinct asset/cache identity.

### Phase T2: Runtime Terrain Dataset And Chunk Ownership

Status: initial CPU-only implementation added. `Engine::TerrainDataset` owns renderer-independent terrain source and loaded chunk records with generation-counted runtime handles. Imported heightmap chunks and procedural chunks now share sampling, raycast, bounds, diagnostics, and generated-tile compatibility paths. Existing `TerrainSystem`, App chunk streaming, renderer LOD commits, navigation build data, physics colliders, generated cache files, material rules, and serialization remain unchanged.

Deliverables:

- Introduce source-owned runtime terrain chunk records with generation-counted handles.
- Support chunk create/load/unload from imported and procedural sources.
- Preserve CPU height sampling, bounds, raycasts, and diagnostics.
- Keep compatibility with existing `TerrainSystem` until the new owner can replace it.

Exit criteria:

- Heightmap and procedural chunks share the same runtime query path.
- Gameplay height queries do not depend on renderer terrain handles.

### Phase T3: Derived Asset Cache

Status: initial CPU-only implementation added. `Engine::TerrainDerivedCache` stores imported terrain chunk height payloads and renderer-independent LOD mesh payloads under deterministic manifest identity. Cache records use YAML manifests plus binary payload files, validate payload hashes/magic/counts, expose synchronous worker-safe helpers and `AsyncWorkQueue` wrappers, and keep live renderer, navigation, physics, App streaming, material weights, and serialization outside the cache.

Deliverables:

- Add terrain derived-data cache key types.
- Cache imported chunk height payloads and render LOD CPU mesh outputs first.
- Add stale detection from source/import/settings changes.
- Keep live renderer resources outside the generated cache.

Exit criteria:

- Re-running a terrain import can reuse valid generated chunk/LOD payloads.
- Cache misses enqueue explicit work and report diagnostics.

### Phase T4: Render LOD Adapter

Status: initial implementation added. `Engine::TerrainRenderLodAdapter` now builds renderer LOD mesh data from immutable terrain chunk snapshots, supports both `TerrainDataset` chunks and legacy `TerrainSystem` tile inputs, converts T3 renderer-independent cache payloads into current renderer mesh data, and lets the App async terrain LOD worker path use read-only derived-cache probes before generating meshes. `TerrainSystem` remains the runtime terrain owner and renderer commit path.

Deliverables:

- Rebuild terrain renderer LOD generation on the new chunk data model.
- Preserve budgeted main-thread renderer commits.
- Keep skirts as the first seam strategy.
- Add tests for edge compatibility and stale generation rejection.

Exit criteria:

- Imported heightmap chunks render with existing single-material terrain path.
- LOD generation remains worker-safe.

### Phase T5: Navigation Data Adapter

Status: initial implementation added. `Engine::TerrainNavigationAdapter` now builds existing `NavigationTerrainBuildData` from immutable terrain snapshots, supports `TerrainDataset` chunks, generated tile data, and legacy `TerrainSystem` tiles, and preserves the previous terrain sampling/downsampling/winding contract. App navigation build-data creation now goes through the adapter while blocker append, nav cache async flow, live tile insertion, connectivity, graph routing, and chunk streaming remain unchanged. Navigation cache identity now includes terrain source/import identity and the terrain navigation adapter version.

Deliverables:

- Generate navigation build data from new terrain chunks.
- Include terrain source/import settings in navigation cache identity.
- Mark dirty chunks without synchronous navmesh builds.
- Preserve merge behavior with scene navigation geometry.

Exit criteria:

- Imported heightmap terrain can produce deterministic nav build input.
- Existing navigation tests remain compatible.

### Phase T6: Physics Collider Adapter

Status: initial implementation added. `Engine::TerrainPhysicsColliderAdapter` now builds static triangle mesh collider payloads from immutable terrain chunk snapshots, supports `TerrainDataset` chunks, generated tile data, and legacy `TerrainSystem` tiles, and can explicitly create/release one static `ScenePhysicsWorld` body/collider per terrain chunk. Dirty collider chunks are tracked separately from render and navigation outputs. App gameplay, `BlockingCollisionSystem`, terrain ownership, navigation, renderer LOD, cache writes, and serialization remain unchanged.

Deliverables:

- Generate static triangle mesh collider data from terrain chunks at physics resolution.
- Create and destroy scene physics static bodies/colliders through explicit adapter calls.
- Track dirty collider chunks separately from render/nav outputs.

Exit criteria:

- Imported heightmap terrain can create static physics colliders without using renderer LOD meshes.
- Collider rebuild requests are explicit and chunk-scoped.

### Phase T7: Terrain Material Metadata

Status: initial CPU-only implementation added. `Engine::TerrainMaterialMetadata` now defines in-memory terrain material sets, PBR layer texture metadata, deterministic height/slope/world-position rules, validation diagnostics, `AssetRegistry` texture dependency registration, and chunk coverage evaluation from authoritative CPU heights. YAML loading, mask sampling, cached material weights, shader blending, terrain material renderer resources, and App terrain rendering changes remain deferred.

Deliverables:

- Define `TerrainMaterialSet` and layer/rule metadata.
- Track PBR texture dependencies through `AssetRegistry`.
- Add CPU rule evaluation for slope/height/world-position diagnostics.
- Keep renderer shader changes deferred until metadata is stable.

Exit criteria:

- A terrain chunk can report expected material layer coverage from rules.
- PBR texture dependencies are registered without allocating renderer resources.

### Phase T8: Layered Terrain Rendering

Deliverables:

- Add renderer support for a small fixed number of PBR terrain layers.
- Add explicit terrain material resource creation/release.
- Upload layer rule constants and optional weight/mask data.
- Add fallback behavior for missing layers/textures.

Exit criteria:

- Terrain can blend materials by slope and world position.
- Renderer diagnostics report terrain layer usage and fallback paths.

### Phase T9: Serialization Preparation

Deliverables:

- Freeze durable terrain identity shape.
- Document chunk file payload boundaries.
- Add tests for stable IDs and cache invalidation decisions.
- Do not implement full scene/world serialization yet.

Exit criteria:

- Future chunked serialize/deserialize can be added without changing runtime handles or cache keys.

## Test Strategy

Use small deterministic fixtures:

- tiny heightmaps with known values;
- chunk-boundary fixtures for seam behavior;
- slope fixtures for material rule classification;
- missing/stale source fixtures;
- procedural settings fixtures;
- cache identity fixtures.

Avoid optional large terrain assets in default CTest. Use the real `assets/heightmaps/47_648_-122_332_13_505_505_16bit.png` heightmap for manual or opt-in validation of import scale, chunk extraction, cache behavior, LOD generation, nav data, and collider generation.

## Risks

- Terrain material blending can outgrow current shader/resource binding limits quickly.
- Heightmap coordinate conventions can cause subtle mirrored or offset terrain if not explicit.
- Cache keys can become invalid if source/import/material versions are incomplete.
- Using render LOD data for navigation or physics would create gameplay instability.
- Large heightmaps can create import hitches unless decoding, chunk extraction, and derived builds are worker-friendly.
- Future serialization will be awkward if chunk identity is not separated from runtime handles from the start.

## First-Pass Definition Of Done

The first full implementation pass should be considered successful when:

- Heightmap import creates deterministic terrain chunks.
- Procedural terrain can use the same chunk runtime path.
- Generated render LOD CPU data is cacheable and committed under frame budget.
- Navigation build data can be generated from imported chunks.
- Physics collider data can be generated or queued from imported chunks, even if automatic App integration is deferred.
- Terrain material metadata can describe PBR layers and slope/world-position rules.
- All live renderer, navigation, and physics resources remain explicit derived resources.
- Future chunked serialization has stable IDs and payload boundaries documented, but no serialization runtime is required yet.
