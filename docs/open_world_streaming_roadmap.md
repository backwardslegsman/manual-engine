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
- Terrain nav tiles should be generated from a border-expanded source sampling apron while retaining the original durable tile coord and tile bounds. This gives Recast neighbor geometry at chunk seams without creating extra runtime terrain chunks or changing nav tile identity.
- Cache files store Detour tile bytes through `NavigationCache`.
- Cache halo reads or builds tile bytes.
- Active halo inserts tile bytes into `NavigationSystem`.
- Geometry filters should include world-space slope and padded tile bounds. Scene/static geometry filtering should use the normal tile padding plus the terrain nav border padding so authored geometry near tile edges participates in seam builds. Future walkable/blocker/ignore tags should join the cache identity.

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

Status: implemented as an Engine API plus tests, with no App streaming policy integration yet. `Engine::OpenWorldStreamingSceneChunks` adds durable scene chunk manifest helpers, scene-binary read descriptors, cached CPU scene chunk payloads, and main-thread promotion/demotion callbacks that restore core `Scene` actors, local transforms, hierarchy, and metadata-only components from existing Phase 13 scene binaries.

- Scene chunk records use existing `StreamingChunkKeyKind::SceneChunk` and `StreamingPayloadKind::SceneChunk` with stable chunk IDs, world bounds, source/settings hashes, estimated bytes, ownership policy, debug names, and optional `AssetId` dependency metadata.
- Cache-halo read/decode supports scene chunk binary payload files through `readSceneBinary`; valid payloads become `StreamingSceneChunkPayload` records, while corrupt/stale/invalid files are diagnostics and never mutate a live scene.
- Active-halo promotion is callback-owned and main-thread-only. It validates the cached scene payload, creates actors with preserved `SceneObjectId`, restores hierarchy, attaches metadata-only components, and returns only a streaming runtime token for later demotion.
- Demotion destroys only actors created by that chunk binding and can flush destroyed actor slots deterministically.
- Actor ownership policy is explicit: `ChunkOwnedStatic` actors live only with the chunk; `Global` actors must already exist and are not duplicated or destroyed by the chunk; `Migratory` actors are claim-tracked by `SceneObjectId` so overlapping chunks cannot duplicate them. Automatic bounds-based migration is still deferred.
- Scene chunk diagnostics and the Debug UI Streaming tab expose manifest, cached payload, promote/demote, created/destroyed actor/component, duplicate stable ID, invalid parent/component, and unsupported ownership counters.
- S6 still does not stream renderer resources, physics bodies/colliders, nav tiles, terrain chunks, behavior hooks, Lua VM state, App gameplay, or `AssetCache` resources. Those remain separate payloads or later integration work.

Tests:

- scene chunk manifest records use stable IDs and exclude runtime handles;
- valid scene binaries read into cache-halo `StreamingSceneChunkPayload` records;
- corrupt scene binaries and invalid cached scene records fail before live mutation;
- promotion/demotion round-trips actors, hierarchy, and metadata-only components while regenerating transient handles;
- duplicate chunk-owned IDs are rejected;
- `Global` and `Migratory` ownership policies prevent duplicate actors across chunks.

### Phase S7 - LOD And Multi-Halo Refinement

Status: implemented as an Engine planner/API refinement plus tests, with no App streaming policy integration yet. `Engine::OpenWorldStreaming` now supports variant IDs on durable streaming keys, per-record halo profiles, per-profile residency policies, detail levels, priority bias, predictive focus input, and profile/prefetch diagnostics. Existing S1-S6 callers continue to use the default profile and empty variant ID.

- Durable streaming identity now includes optional `variantId`, so one chunk can carry independent records such as `terrain_lod_0`, `terrain_lod_2`, `scene_metadata`, `texture_mip_high`, or future behavior/animation/audio variants.
- Manifest records can specify `StreamingHaloProfile`, `detailLevel`, and `priorityBias`. Default profile behavior preserves the older per-payload active/cache policy.
- Profile policies allow far metadata and cache-only records to warm CPU payloads without live promotion, while high-detail records use smaller active/cache radii and independent transition caps.
- `StreamingFocusInput` adds current position, optional velocity prediction, prediction distance limits, and optional goal/path focus points. Predictive focus upgrades cold records to cache by default but does not request live promotion unless a policy explicitly allows predictive live work.
- Decision ordering now prioritizes live work, cache work, profile priority, distance, priority bias, payload kind, and durable key. Transition caps are applied per payload/profile pair.
- Debug UI streaming stats now expose profile counts, variant record counts, high-detail candidates, active vs predictive candidates, predictive prefetches, retained prefetches, and profile-limited transitions.
- S7 still does not implement real texture mip streaming, behavior/animation tick migration, audio/particles, editor halos, or default App streaming integration.

Tests:

- variant keys distinguish multiple payload records for the same durable chunk;
- old empty-variant/default-profile records preserve S1-S6 behavior;
- terrain render LOD variants produce independent decisions;
- far metadata and cache-only profiles stay cached without live promotion;
- high-detail profile records promote only in inner halos;
- per-profile caps limit high-detail transitions without blocking standard live records;
- predictive velocity/goal focus prefetches cache records without requesting live promotion by default;
- active-focus work outranks predictive prefetch under transition pressure.

### Phase S8 - Modern Runtime Streaming Integration And Roadmap Closure

Status: implemented as the roadmap completion milestone. `Engine::OpenWorldStreamingRuntime` now coordinates saved-build validation, S1 halo planning, S2 cache reads, S4 generation hooks, and S3 live promotion/demotion for the modern default runtime. The Debug and Release modern default scene validates `generated/open_world_streaming/modern_default/manifest.yaml`, rebuilds heightmap-derived streaming cache data when the saved build is missing or version/source/settings identities change, and feeds real streaming diagnostics into the Debug UI.

- The modern default streaming runtime coordinator wraps the existing planner, cache halo, generation halo, and live halo. The coordinator owns transient residency snapshots, queue updates, generation policy, promotion/demotion callback wiring, and debug diagnostics for the default scene, while live-system mutation remains callback-owned.
- Build the startup streaming manifest from the full-heightmap bake manifest, scene chunk records, terrain render LOD records, navigation tile records, physics collider records, and asset dependency records. Manifest records must keep using durable terrain chunk IDs, scene chunk IDs, `AssetId`s, variant IDs, source hashes, settings hashes, and version strings; no runtime handles may enter the manifest.
- On startup, validate the saved streaming build on disk before the modern scene starts. A build is current only when its source file hashes, import settings, chunk sizing, render LOD settings, nav settings, slope/tile filtering settings, physics collider settings, asset dependency identities, scene chunk schema, payload versions, and streaming bake/runtime version strings match the active runtime.
- If no current streaming build exists, or if any relevant version/source/settings identity changed, rebuild the heightmap-derived streaming data explicitly before using it. The rebuild may run as startup/tool work with progress diagnostics, but it must not be hidden inside frame setters or repeated every frame. The output becomes the new saved streaming build for later launches.
- Reuse current saved build artifacts when validation succeeds. Repeated startup with unchanged inputs should avoid heightmap import, render LOD generation, nav tile generation, and physics collider generation except for cache reads needed by the cache halo.
- Feed camera/player focus into `planStreamingHalo` each frame. Predictive focus should use camera/player velocity and optional path/goal hints to warm cache halo records without promoting them live unless the profile policy explicitly allows predictive live promotion.
- Wire cache-halo reads for terrain chunks, terrain render LOD payloads, navigation tile bytes, terrain physics collider payloads, scene binary chunks, and metadata-only asset dependencies. Cache misses/stale/corrupt records use read-only or generation-on-miss policy according to runtime settings, and generation jobs run through `AsyncWorkQueue`.
- Wire live-halo promotion and demotion callbacks for the default scene:
  - terrain render LOD payloads create/destroy renderer terrain handles under a main-thread budget;
  - navigation tile payloads insert/remove live `NavigationSystem` tiles under a main-thread budget;
  - terrain physics collider payloads create/destroy `ScenePhysicsWorld` static terrain colliders under a main-thread budget;
  - scene chunk payloads promote/demote core scene actors/components through `OpenWorldStreamingSceneChunks`;
  - static mesh and texture asset dependencies promote/demote through `OpenWorldStreamingAssets` and `AssetCache`;
  - unsupported material, skinned mesh, animation, behavior, audio, particle, and editor payload variants remain metadata/diagnostic records until their owners provide streamable callbacks.
- Keep active-halo demotion from losing cache-halo CPU residency. Leaving the cache halo releases CPU cache payloads or queues generated-cache writes only when policy requires it.
- Add an explicit startup/build status line and Debug UI streaming counters for build validation, rebuild reason, bake progress, source/version mismatch, cache reuse, queue sizes, CPU timings, cache hit/miss/stale/corrupt/write counts, generation jobs, live promotions/demotions, live resource counts, and last failures.
- Compatibility modes remain available, but the Debug and Release modern default scene uses the S8 streaming runtime for heightmap-derived streaming build validation and terrain/nav/physics/render payload residency instead of relying on eager fixed-patch startup as the normal path.
- Adjacent streamed nav tiles are connected through `NavigationConnectivitySystem` portal records. Navigation tile promotion refreshes the promoted tile plus cardinal neighbors, tile demotion removes/relinks affected connectivity, and `SceneNavigationService` can stitch loaded-tile routes through connected portals when a direct Detour path is unavailable. Terrain nav tiles are now also baked from border-aware sample aprons (`terrain_navigation_adapter_border_v1`) so seam continuity is primarily handled by the tile bytes themselves, with portals remaining as the routing/debug layer.

Tests:

- missing saved streaming build triggers an explicit heightmap bake/rebuild and writes a reusable build manifest;
- unchanged source/settings/version identities reuse the saved streaming build on the next startup;
- changing the heightmap source hash, import settings, render LOD settings, nav settings, slope/tile filtering settings, physics collider settings, scene chunk schema, asset dependency identity, or payload version invalidates the saved build and rebuilds only the affected payload classes where practical;
- modern default runtime feeds camera/player focus into the planner and updates desired residency without blocking frame time;
- cache halo asynchronously reads terrain chunk, terrain LOD, nav tile, physics collider, scene chunk, and asset dependency records from the saved build;
- generation-on-miss runs only when runtime policy enables it and never mutates live renderer, navigation, physics, scene, or App state from worker jobs;
- active halo promotes/demotes renderer terrain, nav tiles, terrain physics colliders, scene chunks, and supported asset dependencies under main-thread budgets;
- leaving active halo demotes live resources while retaining cached CPU payloads inside cache halo;
- leaving cache halo releases CPU payloads or queues dirty generated writes according to policy;
- Debug UI displays build validation/rebuild status, queue sizes, timings, cache results, generation jobs, live resource counts, and last failures from the real runtime coordinator;
- runtime handles never appear in saved streaming build manifests, derived cache files, serialized scene chunks, or debug/export payloads.

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
- How much border padding is required for physics seams at terrain chunk boundaries? Terrain nav seams now use border-aware bake settings in cache identity.
- Which dynamic actor classes are global versus chunk-owned versus migratory?

## Acceptance For First Usable Streaming Version

- Default Debug and Release modern scenes use the S8 streaming runtime instead of eager fixed-patch terrain/nav/physics loading.
- Startup validates a saved streaming build and rebuilds the heightmap-derived build data when it is missing, stale, corrupt, or version/source/settings identity no longer matches.
- Moving the camera updates desired residency without blocking frame time.
- Cache halo asynchronously reads terrain/nav/physics/scene payloads.
- Active halo promotes cached chunks under a main-thread budget.
- Leaving active halo demotes live resources without losing cached CPU data.
- Leaving cache halo releases CPU cache records or writes dirty generated payloads when policy requires it.
- Debug UI shows queue sizes, CPU timings, cache hit/miss/stale/corrupt counts, live resource counts, and last failures.
- Runtime handles never appear in cache files, serialized scene chunks, or manifests.
