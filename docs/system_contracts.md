# ManualEngine Systems And Contracts

This document is the active contract reference for cross-system behavior. Historical roadmap and work-log entries may mention removed systems, but active code is now modern-only: `Scene`, `TerrainDataset`, open-world streaming, scene physics, scene character movement, scene navigation, scene authored/animated adapters, and modern debug UI.

## Active Runtime Systems

- **Scene kernel:** `Engine::Scene` owns renderer-independent actor records, metadata-only component records, local transform hierarchy, stable `SceneObjectId` metadata, and lifecycle/scheduler state. Scene runtime handles are transient generation-counted tokens.
- **Actor authoring metadata:** `ActorAuthoringStore` owns durable editor/game-facing actor metadata keyed by `SceneObjectId`, including display name, string tags, and one string layer. It must not store `SceneActorHandle` or live subsystem handles; runtime handle lookup is always reconstructed through the active `Scene`.
- **Authored component descriptors:** `ActorComponentDescriptorRegistry` owns typed component authoring metadata by stable `SceneComponentTypeId`. `ActorComponentDescriptorStore` owns durable per-actor component instances keyed by `ActorComponentId`; records contain owner `SceneObjectId`, component type, display name, enabled state, and order only. Runtime bind/unbind/apply callbacks may create transient subsystem records through supplied public context, but must not persist `SceneComponentHandle`, renderer, physics, navigation, Lua, behavior, or streaming handles.
- **Scene render bridge:** `SceneRenderBridge` owns mesh, skinned mesh, light, and camera component records and maps them to live Renderer resources through a `SceneRenderBackend`. Renderer handles remain renderer-owned and transient.
- **Scene authored adapter:** The authored adapter consumes already-imported CPU scene data and creates scene actors, hierarchy, adapter-owned renderer resources, and render bridge components. Callers release bridge-created live instances and adapter resources explicitly.
- **Scene animated adapter:** The animated adapter consumes already-imported animated CPU scene data and creates scene actors, skeleton bindings, animator records, skinned mesh resources, and render bridge components. Shared pose/playback math lives in modern animation pose helpers, not in a legacy animated runtime owner.
- **Scene physics:** `ScenePhysicsWorld` is a Jolt-backed scene-owned facade. Jolt types stay private to implementation files. Static triangle mesh colliders consume CPU vertices/indices only; renderer meshes are not gameplay collision authority. Physics query filters may exclude transient bodies or colliders for follow-up runtime queries; those handles are never durable data. Physics may expose transient debug collider-shape snapshots for visualization only.
- **Scene character movement:** `SceneCharacterMovementSystem` owns kinematic capsule character records, creates its own physics body/collider per character, updates in fixed phases, and uses optional `SceneNavigationService` paths. It does not create terrain colliders, nav tiles, renderer resources, or streaming chunks implicitly. Grounded characters may retry horizontal sweeps without the current walkable triangle-mesh ground collider or a zero-distance static triangle-mesh bounds hit while still colliding with other blockers.
- **Navigation:** `NavigationSystem` owns loaded Detour tiles behind an Engine API. `SceneNavigationService` performs projection, path, reachability, raycast, actor-start path, and loaded-tile portal route queries without building or streaming tiles. `NavigationConnectivitySystem` derives portal metadata from loaded nav tile bounds and tile-local queries.
- **Terrain:** `TerrainDataset` owns CPU terrain source/chunk records. Render LODs, nav build data, physics collider payloads, material metadata/render resources, derived cache payloads, and serialization prep metadata are explicit adapter outputs.
- **Open-world streaming:** Streaming manifests use durable keys and optional variants. Cache-halo work reads/decodes or generates CPU payloads on workers. Live-halo work promotes/demotes renderer, navigation, physics, scene, and asset resources only through main-thread callbacks. Streaming core records may hold runtime tokens for demotion, but never subsystem handles in durable data.
- **Editor live apply:** Editor profile changes may affect live systems only through explicit App-owned commands and public Engine/Renderer APIs. Lightweight renderer/debug/camera settings apply to the editor viewport after validation. Rebuilt streaming data may be reloaded only by App-owned teardown/reinitialize helpers that replace streaming-owned resources and leave authored scene actors/assets, player actor, camera object, static authored physics, renderer scene bridge, and editor UI state intact.
- **Editor hooks and tool scripting:** Editor hooks and Lua tool scripts are App-owned command surfaces over `EditorProjectSettings`. They may read/write only script-visible reflected editor settings and invoke validated editor commands. They must not receive raw `EditorUiState`, `EditorLiveApplyHost`, Renderer handles, Jolt objects, Detour refs, streaming runtime storage, Lua VM internals, or App pointers, and they must not save project YAML implicitly.
- **Cursor tracing:** Cursor screen coordinates are converted to renderer-independent world rays from the active view-projection matrix. Consumers such as navigation, physics, actor selection, or future screen-space UI decide what those rays hit; ray construction alone must not mutate gameplay, selection, streaming, renderer, or scene state.
- **Asset registry/cache:** `AssetRegistry` owns stable metadata IDs and dependency records. `AssetCache` owns reusable renderer static mesh and texture acquisitions. Asset runtime handles and cache acquisitions are transient.
- **Reflection/opaque APIs:** Reflection exposes approved typed properties and validates all writes before forwarding to public subsystem APIs. `OpaqueHandle` is a runtime access token, not durable identity.
- **Behavior hooks and Lua:** Native hooks and Lua scripts run through scene scheduler ordering and mutate state only through public APIs or reflection. Callback handles, closures, VM state, registry refs, and opaque handles are runtime-only.
- **Debug visualization/UI:** Engine systems emit renderer-independent debug requests and diagnostics. App builds modern debug snapshots. Renderer/DebugUi presents them and submits draw primitives.

## Threading And Work Boundaries

- Worker jobs may read source files, decode cache payloads, generate derived CPU payloads, and write derived cache files from immutable inputs.
- Worker jobs must not mutate Renderer/bgfx, `Scene`, `NavigationSystem`, `ScenePhysicsWorld`, `AssetCache` live acquisitions, App state, or runtime handle storage.
- Main-thread work that touches live systems should go through explicit calls or `MainThreadWorkQueue` items with `FrameBudget` accounting.
- Setters should not enqueue hidden expensive work. Streaming, baking, nav insertion, physics body creation, and renderer uploads must be visible API calls or callback-owned promotions.

## Terrain Contracts

- `TerrainSourceHandle` and `TerrainChunkHandle` are transient. Durable terrain identity uses source asset ID plus source-space chunk coordinate.
- Heightmap import decodes CPU source data only. It must not call renderer texture loading, build nav, create physics colliders, or mutate live terrain state.
- `TerrainRenderLodAdapter` builds renderer mesh data from immutable CPU terrain chunks or cache payloads. Renderer handle creation/destruction is a main-thread caller responsibility.
- `TerrainNavigationAdapter` builds `NavigationTerrainBuildData` from authoritative CPU heights. Border-aware requests may sample an expanded apron while preserving the original tile coord and output bounds.
- `TerrainPhysicsColliderAdapter` builds CPU static triangle mesh payloads from authoritative CPU heights and creates scene-physics colliders only through explicit caller-owned handoff.
- Terrain material metadata is CPU durable metadata. Renderer terrain material-set handles are live visual resources and must not be serialized.

## Navigation Contracts

- Query failures such as `NoTile`, `NoNearestPoly`, and `NoPath` are normal outcomes and must not trigger synchronous tile builds.
- Navigation cache files are derived data. Cache identity must include source/import identity, nav settings, adapter versions, border-apron settings, scene geometry hash, slope filter, and tile padding when they affect generated tile bytes.
- Scene navigation geometry stores CPU source descriptors and actor-owned transforms. Snapshotting may filter triangles by padded tile bounds and world-space slope. The registry does not mutate live navmesh state.
- Connectivity metadata is derived from loaded nav tiles. Promotion/demotion should refresh the promoted/demoted tile and cardinal neighbors, preferably through phased rebuilds when work is large.

## Streaming Contracts

- Halos express desired residency, not rendering rules. Different payload kinds and profiles may use different active/cache radii and budgets.
- `ColdOnDisk`, `ReadQueued`, `CachedCpu`, `PromoteQueued`, `LiveActive`, `DemoteQueued`, `WriteQueued`, and `Failed` are transient runtime states.
- Saved streaming build manifests are current only when source hashes, settings keys, payload versions, adapter versions, and required cache payload references match active settings.
- Missing/stale saved heightmap-derived builds may trigger a logged startup rebuild. The public saved-build rebuild API always runs the explicit full bake/write path for the supplied settings and does not promote live runtime resources by itself. Runtime generation-on-miss is allowed only by explicit policy and must run on workers.
- Editor live reload of rebuilt saved builds must validate the saved manifest before teardown when possible, demote/release streaming-owned renderer/nav/physics/terrain resources through the same ownership paths used by streaming demotion/shutdown, restart pending streaming queues, and reinitialize `OpenWorldStreamingRuntime` from the saved manifest. If preflight fails, existing live streaming resources remain active; if teardown has already begun and a later step fails, the editor must report streaming disabled rather than leaving half-promoted resources hidden.
- Leaving the active halo demotes live resources; leaving the cache halo releases CPU payloads unless policy says otherwise.

## Serialization Contracts

- Scene binary serialization uses stable IDs and fixed-width little-endian records. It validates before mutating a live scene, actor authoring metadata store, or authored component metadata store.
- Runtime handles, renderer handles, physics handles, nav handles, terrain handles, asset handles, opaque handles, behavior handles, Lua handles, and streaming tokens are invalid serialization payloads.
- Actor authoring metadata may serialize durable `SceneObjectId`, display name, tags, and layer only; scene transforms remain owned by the core actor table.
- Authored component metadata may serialize durable `ActorComponentId`, owner `SceneObjectId`, `SceneComponentTypeId`, display name, enabled state, and order only. `SceneComponentHandle` and concrete subsystem binding handles are runtime-only; typed descriptor payloads require an owning component registry policy before they can be serialized.
- Durable scene data uses `SceneObjectId`; durable assets use `AssetId`; durable terrain uses `TerrainSourceChunkId` or `TerrainChunkStableIdentity`.
- Derived cache files and saved streaming build manifests are disposable generated data, not save-game files.

## Debug And Diagnostics

- Debug UI is modern-system-facing only. Legacy runtime control panels must not be reintroduced.
- Debug draw and debug visualization are frame-transient diagnostics and must not affect gameplay, pathfinding, physics, streaming, renderer culling, cache identity, or serialization.
- Expensive debug producers must report generated/submitted/clipped/capped counts and respect category/global budgets.

## Update Rule

Before adding or changing a feature that touches more than one subsystem, update this document and `docs/engine_overview.md` in the same change. New message-driven cross-system communication should also follow `docs/system_inboxes.md`.
