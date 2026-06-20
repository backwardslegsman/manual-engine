# Open World Streaming Roadmap

This roadmap defines the streaming architecture for ManualEngine's modern scene-backed open world. The target is a residency-driven runtime that can bake, cache, deserialize, promote, demote, and eventually serialize world chunks without blocking the main thread. Terrain comes from the imported heightmap path first, with procedural terrain as a fallback source.

The core idea is a set of configurable halos around the active camera or player focus. A halo is a **desired residency state**, not a rendering rule by itself. Terrain, navmesh, physics colliders, scene actors, renderer resources, textures, meshes, materials, animation data, and future save chunks can each use different radii and budgets while sharing one coordinator.

## Goals

- Bake navmesh tile bytes and physics collider payloads for the full heightmap source ahead of gameplay or on demand in worker jobs.
- Stream chunked terrain, scene records, nav tiles, physics colliders, renderer resources, and asset dependencies through explicit async queues.
- Keep the main thread responsible only for live-system promotion/demotion work that must touch Renderer, `NavigationSystem`, `ScenePhysicsWorld`, or `Scene`.
- Use durable identities for all streamed payloads; never serialize runtime handles.
- Support two initial halos: an inner active/live halo and an outer cached/warm halo.
- Allow more halos later for LOD, high-resolution assets, AI/behavior, animation, audio, particles, and editing.
- Add debug counters and CPU timings from the first implementation phase so streaming cost is measurable.

## Non-Goals

- No immediate removal of legacy compatibility modes.
- No automatic full-world bake on every startup.
- No hidden renderer/nav/physics work from setters.
- No editor UI, save-game migration, or world-authoring workflow in the first pass.
- No network replication or multiplayer streaming policy.
- No guarantee that all streamed systems use the same radius; different systems should remain independently tunable.

## Residency Model

Use these states for every streamable chunk or asset dependency:

- `ColdOnDisk`: durable records exist on disk; no live CPU cache or runtime resources exist.
- `ReadQueued`: a disk read/decode job has been requested.
- `CachedCpu`: decoded CPU payloads are resident in memory but not inserted into live systems.
- `PromoteQueued`: main-thread promotion work is pending.
- `LiveActive`: live renderer/nav/physics/scene resources are active.
- `DemoteQueued`: main-thread live-resource release is pending.
- `WriteQueued`: dirty cached data is being serialized or cache-written.
- `Failed`: last transition failed with diagnostics and retry policy.

Dirty variants should be tracked as flags, not separate enum explosions:

- `SourceDirty`
- `DerivedDirty`
- `RuntimeDirty`
- `SaveDirty`

The first implementation should support:

- `ColdOnDisk -> ReadQueued -> CachedCpu -> PromoteQueued -> LiveActive`
- `LiveActive -> DemoteQueued -> CachedCpu`
- `CachedCpu -> WriteQueued -> ColdOnDisk` for generated cache payloads only

Full edited-world save/writeback can come later.

## Halo Policy

Initial policy should expose at least two halos:

1. **Active halo**
   - Terrain render handles exist.
   - Nav tiles are inserted into `NavigationSystem`.
   - Terrain and static scene physics colliders exist in `ScenePhysicsWorld`.
   - Scene actors/components for chunk-owned static content are active.
   - Required renderer mesh/material/texture handles are acquired.
   - Character movement and local path queries can use the chunk.

2. **Cache halo**
   - Terrain CPU chunks are decoded into `TerrainDataset`.
   - Nav tile bytes are loaded from cache files or generated and cached.
   - Physics collider payloads are loaded or generated.
   - Scene chunk actor/component records are decoded but not necessarily activated.
   - Asset dependencies are registered and may be preloaded to CPU or renderer cache according to policy.
   - No live nav insertion, physics body creation, or renderer terrain/instance creation is required yet.

Optional later halos:

- **Far metadata halo:** only chunk manifests, bounds, object counts, dependency lists, and minimap/probe data.
- **Visual LOD halo:** lower terrain/mesh LODs but no physics/nav.
- **AI halo:** behavior/nav active without high-detail rendering.
- **High-detail halo:** high terrain LOD, high texture mips, animation updates, particles, audio, decals.

Use hysteresis for all halo transitions so a chunk does not churn when the camera hovers near a boundary.

## Durable Identity

All streamed data must be keyed by durable identity:

- Terrain source/chunk: `TerrainSourceChunkId`.
- Terrain source asset: `AssetId` plus import settings key.
- Terrain render LOD payload: source chunk ID, render LOD index/resolution, material/render settings, payload version.
- Terrain nav tile: source chunk ID, nav agent/build settings, terrain nav adapter version, scene geometry hash, slope filter, tile padding, tile coord.
- Terrain physics collider: source chunk ID, collider resolution/settings, physics adapter version.
- Static scene chunk: scene chunk ID, owning scene file ID, bounds, actor `SceneObjectId`s.
- Asset dependency: `AssetId`, import settings key, source content hash.
- Live resources: transient renderer/nav/physics/scene handles only, never serialized.

## Async Architecture

Add a streaming coordinator under `src/Engine` rather than letting App own all state transitions.

Candidate module:

- `Engine::OpenWorldStreamingCoordinator`
- `Engine::StreamingResidencyPolicy`
- `Engine::StreamingChunkRecord`
- `Engine::StreamingAssetRecord`
- `Engine::StreamingDiagnostics`
- `Engine::StreamingDebugRecord`

The coordinator should own desired residency and transition queues, but it should not own Renderer, bgfx, Detour, Jolt, or scene storage directly. App or a narrow composition adapter supplies the live-system commit callbacks.

Queue lanes:

- Disk read/decode queue.
- Derived-data generation queue.
- Asset dependency preload queue.
- Main-thread promote queue.
- Main-thread demote queue.
- Disk/cache write queue.
- Validation/manifest scan queue.

Main-thread promotion should be budgeted by time and item count:

- terrain renderer handle creation;
- nav tile insertion;
- physics body/collider creation;
- scene actor/component activation;
- renderer mesh/material/texture acquisition;
- debug metadata refresh.

Worker jobs must operate on immutable snapshots and return plain result structs. No worker job may call Renderer, mutate `Scene`, insert Detour tiles, or touch Jolt world state.

## Streaming Data Classes

### Terrain

- Cache halo owns CPU terrain chunks in `TerrainDataset`.
- Active halo owns renderer terrain handles created from `TerrainRenderLodAdapter`.
- Terrain LOD generation uses `TerrainDerivedCache` first, then generates on miss/stale/corrupt.
- Full-heightmap chunk manifests should be generated once and reused for halo planning.

### Navigation

- Navmesh for the heightmap should be baked per tile/chunk using `TerrainNavigationAdapter` plus optional filtered scene geometry.
- Cache files store Detour tile bytes through `NavigationCache`.
- Cache halo reads or builds tile bytes.
- Active halo inserts tile bytes into `NavigationSystem`.
- Geometry filters should include world-space slope and padded tile bounds. Future walkable/blocker/ignore tags should join the cache identity.

### Physics

- Terrain collider payloads should be generated from authoritative CPU terrain chunks, not render LOD meshes.
- Static scene collision payloads should use CPU authored geometry or future collider authoring data.
- Cache halo reads or builds collider payloads.
- Active halo creates static `ScenePhysicsWorld` bodies/colliders.
- Physics collider simplification and resolution must be part of cache identity.

### Scene Actors

- Chunk-owned actors should deserialize into CPU scene chunk records in the cache halo.
- Active halo creates scene actors/components and maps durable `SceneObjectId` to runtime `SceneActorHandle`.
- Dynamic actors require ownership policy:
  - global actor;
  - chunk-owned actor;
  - migratory actor that changes chunk ownership when crossing boundaries.
- The first pass should keep dynamic actor migration out of scope unless needed for player/test actors.

### Assets

- Chunk manifests list required `AssetId`s and import settings.
- Cache halo can preload source/CPU metadata and optionally acquire renderer resources ahead of active promotion.
- Active halo acquires live renderer handles through `AssetCache`.
- Asset dependencies should have their own residency records so one mesh/texture is shared across chunks and released only when no active/cached users remain.

## Bake Pipeline

Add an explicit bake command/API path before runtime streaming depends on full-world data:

1. Import the full heightmap source.
2. Partition into durable terrain source chunks.
3. Generate terrain chunk payloads or manifests.
4. Generate render LOD payloads for configured LODs.
5. Generate nav tile bytes for configured agents/build settings.
6. Generate terrain physics collider payloads.
7. Record asset dependencies, source hashes, versions, bounds, and payload hashes.
8. Validate all manifests without creating live runtime resources.

The bake pipeline may be implemented as synchronous tools first, then worker-backed jobs. Runtime should still be able to generate missing derived payloads on demand when policy allows it.

## Runtime Phases

### Phase S0 - Streaming Contract And Instrumentation

Status: implemented as a contract and instrumentation foundation. `Engine::OpenWorldStreaming` now defines the stable residency states, dirty flags, transition lanes, payload kinds, display-name helpers, diagnostics counters, live-resource counters, cache counters, failure fields, and a no-op diagnostics snapshot. The modern Debug UI includes a Streaming tab that can display no-op diagnostics or S1 planner diagnostics.

- Keep S0 behavior inert: no halo planning, async queueing, disk IO, derived generation, asset preload, live promotion, live demotion, bake commands, or cache writes.
- Keep streaming diagnostics as plain Engine data with no Renderer/bgfx, Jolt, Detour/Recast, App, scene serialization file I/O, or runtime-handle dependencies.
- Use S0 names and counters as the compatibility surface for later streaming phases.

Exit criteria:

- Streaming terms and counters are stable before implementation.

### Phase S1 - Chunk Manifest And Halo Planner

Status: implemented as a planning-only Engine layer. `Engine::OpenWorldStreaming` now has in-memory durable manifest records, streaming chunk keys for terrain source chunks, future scene chunks, and asset dependencies, configurable per-payload active/cache halo policies, deterministic XZ bounds-distance planning, hysteresis retention, per-payload transition caps, stable manifest record hashes, and planner diagnostics. Results are desired residency only; S1 still performs no disk IO, derived generation, asset preload, renderer upload, nav insertion, physics body creation, scene mutation, or App streaming policy change.

- Durable manifest records use stable identity (`TerrainSourceChunkId`, `AssetId`, or future scene chunk stable IDs), source/settings hashes, bounds, estimated bytes, payload availability, dirty flags, and debug names.
- Halo planning computes `LiveActive`, `CachedCpu`, or `ColdOnDisk` from focus-to-bounds distance using independent policies per payload kind.
- Hysteresis keeps active/cached records from churning near boundaries, and transition caps limit desired state changes deterministically by payload kind.
- Invalid bounds, malformed keys, unsupported payloads, and non-finite focus points are skipped with diagnostics rather than asserting.
- Keep results as desired state only; later phases consume the plan to enqueue reads, generation, promotion, demotion, and cache writes.

Tests:

- deterministic chunk selection;
- hysteresis prevents boundary churn;
- independent terrain/nav/physics/asset radii;
- transition caps limit desired changes deterministically;
- malformed records and non-finite focus values fail cleanly;
- large heightmap manifests can be scanned without loading payloads.

### Phase S2 - Async Read/Decode Cache Halo

Status: implemented as a cache-halo read/decode coordinator. `Engine::OpenWorldStreamingCacheHalo` consumes S1 halo plans, tracks transient residency records by durable streaming key and payload kind, queues `DiskReadDecode` jobs through `AsyncWorkQueue`, merges completed plain results, stores cache hits as CPU payloads, and reports read queue/cache diagnostics. It still does not generate missing data, write cache files, preload renderer assets, insert nav tiles, create physics bodies/colliders, activate scene actors, or change App streaming policy.

- Terrain chunk reads use existing `TerrainDerivedCache::readChunk` descriptors and return CPU height payloads.
- Navigation tile reads use existing `NavigationCache::readTileCache` descriptors and return Detour tile bytes as inert CPU cache payloads.
- Asset dependencies can enter `CachedCpu` as metadata-only records; Renderer/`AssetCache` preload is deferred.
- Physics collider and scene chunk payloads report unsupported/missing descriptor status unless tests/tools provide fake read descriptors; real file formats remain later-phase work.
- Request generations, cancellation, stale completion accounting, queue caps, merge caps, and last-failure diagnostics are tracked deterministically.

Tests:

- cache halo queues reads without blocking main thread;
- stale read results are discarded after desired state changes;
- missing/corrupt payloads report diagnostics and can enqueue generation when policy allows.
- duplicate desired plans do not enqueue duplicate reads;
- cache hits store CPU payloads without promotion;
- queue and merge caps are deterministic.

### Phase S3 - Budgeted Main-Thread Promotion/Demotion

Status: implemented as a live-halo transition contract. `Engine::OpenWorldStreamingLiveHalo` consumes S1 plans and S2 cached CPU payloads, tracks transient live residency records by durable key and payload kind, schedules `MainThreadPromote`/`MainThreadDemote` work through `FrameBudget`/`MainThreadWorkQueue`, and reports live-resource, pending promote/demote, stale queued work, and callback failure diagnostics. Live mutation is callback-owned; the streaming core stores only runtime promotion tokens and remains independent from renderer, navigation, physics, scene, App, and serialized handle types.

- Promote cached payloads only when S2 has already produced a CPU cache hit; missing payloads are diagnosed instead of generating data or blocking.
- Execute promotion/demotion callbacks on the main thread under existing budget categories/priorities.
- Revalidate request generations before invoking callbacks so stale queued work cannot mutate live systems.
- Allow callers to map callbacks to renderer terrain handles, nav tile insertion, scene physics collider creation, scene actor activation, or asset dependency acquisition without adding those dependencies to the streaming core.
- Demote live records back to `CachedCpu` or `ColdOnDisk` according to the current halo plan.
- Keep all live-handle storage outside durable manifests, cache payloads, and serialized data.

Tests:

- promotion creates expected live resources only inside active halo through fake callbacks;
- demotion releases live resources deterministically through fake callbacks;
- duplicate updates do not enqueue duplicate work while a promotion/demotion is pending;
- missing cached payloads, callback failures, stale queued work, and queue caps are diagnosed deterministically;
- runtime handles never enter serialized/cache payloads or public streaming headers.

### Phase S4 - Full Heightmap Bake And Runtime Cache Use

Status: implemented as an Engine API plus tests, with no App startup integration or standalone bake executable yet. `Engine::OpenWorldStreamingBake` imports a full heightmap, partitions it into durable terrain source chunks, writes cache payloads for terrain chunk heights, renderer-independent terrain LOD meshes, navigation tile bytes, and renderer-independent terrain physics collider meshes, then returns an `OpenWorldStreamingBakeManifest` containing S1 manifest records and S2 read descriptors. `OpenWorldStreamingDerivedGenerationHalo` adds an explicit worker-generation path for cache miss/stale/corrupt cases when policy enables generation; read-only runtime cache use remains the default.

- Full-heightmap baking consumes heightmap import settings, terrain derived-cache settings, navigation cache/build/agent settings, render LOD requests, physics collider resolution, and optional scene-geometry/filter identity.
- Cache identity includes source file hash, source `AssetId`, import settings, chunk coordinate/resolution/size, render LOD settings, nav build/agent/profile settings, terrain nav adapter version, scene-geometry hash/slope/tile padding, physics collider resolution/settings, and payload version strings.
- Terrain chunk and render LOD payloads use `TerrainDerivedCache`; nav tile bytes use `NavigationCache`; physics collider payloads are now cached through `TerrainDerivedCache` as CPU mesh data only.
- Bake manifests can be converted directly into S1 `StreamingChunkManifest` records plus S2 `StreamingReadDescriptorTable` entries without storing runtime handles.
- Runtime generation-on-miss runs through `AsyncWorkQueue`, returns plain cached payloads, and never mutates Renderer, `NavigationSystem`, `ScenePhysicsWorld`, `Scene`, or App state.
- S4 still does not add a CLI bake tool, App debug button, default-scene streaming integration, live promotion behavior, scene chunk file formats, or asset dependency streaming.

Tests:

- tiny full-heightmap bake writes deterministic terrain chunk, render LOD, nav tile, and physics collider payloads;
- repeated bake produces stable cache identities and S2 can read baked payloads into CPU cache variants;
- source/import/render/nav/slope/physics setting changes invalidate the relevant cache identities;
- read-only policy does not generate missing data, while explicit generation-on-miss queues worker jobs and returns plain cached payloads;
- corrupt/stale/missing cache results remain diagnostics and do not partially promote live resources.

### Phase S5 - Asset Dependency Streaming

Status: implemented as an Engine API plus tests, with no App streaming policy integration yet. `Engine::OpenWorldStreamingAssets` adds asset dependency descriptors, durable manifest/read-descriptor helpers, metadata cache warming, and an asset residency adapter that uses S3 promotion/demotion callbacks to acquire and release live `AssetCache` resources. Static mesh and texture dependencies are first-class live promotion targets; material, skinned mesh, authored scene, animation, and terrain material-set dependencies remain metadata/diagnostic records until their ownership is made streamable.

- Asset dependency manifest records are keyed by durable `AssetId` plus import settings and never by `AssetHandle`, `CachedTexture`, `CachedStaticMesh`, renderer handles, or opaque handles.
- Cache halo warming stores metadata-only `StreamingMetadataPayload` records for asset dependencies without touching `AssetCache` or renderer resources.
- Active halo promotion uses `OpenWorldStreamingAssetResidency` callbacks to acquire `StaticMesh` and `Texture` dependencies from `AssetCache` on the main thread and to release them deterministically on demotion.
- Shared dependency promotion reuses an existing live acquisition and tracks reference counts; final demotion releases the cached mesh/texture exactly once.
- Missing required assets fail promotion with diagnostics; missing optional assets may promote metadata-only; unsupported asset types fail explicitly with `UnsupportedPayload`.
- Streaming diagnostics and the Debug UI Streaming tab now expose asset dependency manifest, metadata hit, live mesh/texture, missing, unsupported, shared reference, and release-latency counters.

Tests:

- asset manifest records use durable `AssetId` plus import settings and exclude runtime handles;
- metadata cache halo records become `CachedCpu` without touching `AssetCache`;
- static mesh and texture promotion/demotion call cache callbacks under S3 live-halo ordering;
- shared dependencies acquire once and release only after the last demotion;
- required missing, optional missing, and unsupported dependency cases are diagnosed deterministically.

### Phase S6 - Scene Chunk Serialization Integration

- Extend scene serialization from whole-scene core records to chunked scene actor/component payloads.
- Deserialize chunk records into cache halo records.
- Activate actors/components only in active halo.
- Add actor migration policy for dynamic actors crossing chunk boundaries.

Tests:

- stable `SceneObjectId` survives chunk unload/reload;
- runtime handles are regenerated safely;
- dynamic actor ownership does not duplicate actors across chunks.

### Phase S7 - LOD And Multi-Halo Refinement

- Add finer halos for high-detail render LODs, texture mips, AI behavior, animation ticking, particles/audio, and far metadata.
- Add per-system residency policies and debug views.
- Add predictive prefetch based on camera velocity and path/goal direction.

Tests:

- high-detail resources promote only in inner halos;
- far metadata stays cheap;
- prefetch does not evict active resources under pressure.

## Debug Counters And Timings

Expose these from the first code phase and show them in the Debug UI:

- Current focus chunk and camera/player world position.
- Desired chunk counts by residency state.
- Actual chunk counts by residency state.
- Transition counts this frame by lane.
- Queue depth by lane.
- Active worker jobs by lane.
- Completed, cancelled, stale, failed jobs by lane.
- Main-thread promote/demote items run/deferred.
- Main-thread promote/demote time in milliseconds.
- Worker read/decode/generate/write time in milliseconds.
- Bytes read, bytes written, and estimated resident memory.
- Cache hits/misses/stale/corrupt/writes by payload kind.
- Live renderer terrain/mesh/material/texture counts from streaming.
- Live nav tile count and pending nav tile count.
- Live physics body/collider count from streaming.
- Live scene actor/component count from streaming.
- Asset dependency count, pending asset load count, live asset count.
- Last failure lane, chunk ID, status, and message.
- Hysteresis churn count and eviction-blocked count.

Debug draw should include:

- Active halo bounds.
- Cache halo bounds.
- Per-chunk residency color overlay.
- Queued/promoting/demoting chunk markers.
- Failed chunk markers.
- Optional nav tile and physics collider overlays for active chunks.

## Performance Rules

- No disk IO on the main thread except explicit debug/tool commands.
- No CPU bake/generation on the main thread except tiny test fixtures.
- No unbounded live-system promotion in one frame.
- No worker job mutates live Engine/Renderer/Navigation/Physics systems.
- Every generated payload has a cache key and version.
- Every queue result carries the request generation so stale results are ignored.
- Every expensive transition has a debug timing bucket.

## Open Questions

- What default active/cache radii should the modern sample use for terrain, nav, physics, scene actors, and assets?
- Should cache halo preload renderer resources or stop at CPU payloads for the first pass?
- Should full-heightmap bake be a standalone executable/tool target or an in-App debug command first?
- What compression format should chunk payloads use after the raw binary format is validated?
- How much border padding is required for nav/physics seams at terrain chunk boundaries?
- Which dynamic actor classes are global versus chunk-owned versus migratory?

## Acceptance For First Usable Streaming Version

- Moving the camera updates desired residency without blocking frame time.
- Cache halo asynchronously reads terrain/nav/physics/scene payloads.
- Active halo promotes cached chunks under a main-thread budget.
- Leaving active halo demotes live resources without losing cached CPU data.
- Leaving cache halo releases CPU cache records or writes dirty generated payloads when policy requires it.
- Debug UI shows queue sizes, CPU timings, cache hit/miss/stale/corrupt counts, live resource counts, and last failures.
- Runtime handles never appear in cache files, serialized scene chunks, or manifests.
