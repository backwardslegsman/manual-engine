# Runtime Performance And Streaming Roadmap

This roadmap tracks hitch reduction and frame pacing work for the open-world prototype. It is separate from `navigation_roadmap.md`: navigation features describe movement behavior, while this document describes how expensive runtime work should be scheduled, offloaded, phased, or deferred.

## Goal

- Keep the larger generated world playable without severe camera-movement or background-system hitches.
- Keep live `World`, `Renderer`/bgfx, `TerrainSystem`, `NavigationSystem`, and `SpatialRegistry` mutation on the main thread.
- Move pure CPU generation, cache file I/O, and large derived-data construction to worker jobs when practical.
- Split unavoidable main-thread work into explicit, budgeted phases.
- Preserve debug visibility for long frames so future hitches identify the responsible system.

## Scheduling Rules

- Worker jobs may only consume immutable snapshots and produce plain data or Detour tile bytes.
- bgfx resource creation/destruction, live world object mutation, live Detour tile insertion, and spatial registry mutation remain main-thread only.
- Any main-thread task that can touch many chunks, many objects, many debug lines, or many renderer resources must expose either a per-frame cap or a phased commit path.
- Cache writes are explicit/debug-driven by default. Runtime cache reads are allowed, but should move to workers where possible.
- A system is not considered budgeted if one callback can still run for many milliseconds. Large callbacks must be split further.

## Phase P1 - Async Navigation Cache I/O

Status: implemented. Navigation cache tile/connectivity/graph file reads and writes now have worker-safe helper APIs, and App uses `AsyncWorkQueue` for runtime cache reads and debug/manual cache writes.

Move navigation cache file reads and writes off the main thread.

Scope:
- Add worker jobs for tile cache reads, connectivity cache reads, and graph cache reads.
- Add worker or deferred jobs for cache writes.
- Keep cache manifest identity and stale/mismatch validation unchanged.
- Main thread receives plain results:
  - cache hit with `NavigationTileCacheData`;
  - stale/miss/corrupt result;
  - cache write success/failure diagnostics.
- Keep write-through disabled by default. Manual cache generation/debug refresh may enqueue many cache jobs but must not block the frame.

Acceptance:
- Normal nav sync does not call filesystem I/O directly.
- Cache-warmed chunk movement avoids both Recast builds and main-thread disk reads.
- Long-frame diagnostics can distinguish cache I/O completion/commit from nav insertion.

Deferred:
- Compression, packed archives, external cache generation tools, and multi-agent cache sets.

## Phase P2 - Worker-Built World Navigation Graph

Move the expensive coarse graph rebuild out of the main-frame callback.

Scope:
- Add a plain `WorldNavigationGraphBuildInput` snapshot containing center chunk, settings, biome/terrain sampling settings, and loaded connectivity cache data.
- Add a worker-safe graph build function that produces `WorldNavigationGraphCacheData` or equivalent plain graph data.
- Main thread only swaps loaded graph data into `WorldNavigationGraph`.
- Keep current threshold-based graph recentering.
- Keep graph rebuilds coalesced: one pending graph build per current center/settings identity.

Acceptance:
- `worldGraphMs` becomes near-zero for normal frames except final graph data swap.
- Cancelling/replacing stale graph jobs when the camera moves again does not corrupt active routes.
- Existing route behavior remains unchanged.

Deferred:
- Actor-driven streaming, route reservation, road networks, gates, and off-mesh links.

## Phase P3 - Fine-Grained Connectivity Rebuild

Split loaded-chunk portal connectivity refresh below the chunk level.

Scope:
- Replace per-chunk atomic portal rebuilds with phased work:
  - start chunk diagnostics;
  - rebuild north edge;
  - rebuild south edge;
  - rebuild east edge;
  - rebuild west edge;
  - merge/relink neighbor portals;
  - finalize cache/debug data.
- Add per-edge or per-sample caps when Detour queries are still too expensive.
- Keep full connectivity rebuild only for explicit debug/settings changes, and internally route it through the same phased path.

Acceptance:
- Connectivity update cost is bounded even when many nav tiles arrive in one burst.
- Portal diagnostics remain deterministic.
- Graph rebuild waits for pending connectivity phases to finish.

Deferred:
- Portal quality scoring, async Detour queries against live navmesh, and persistent unloaded-chunk portal cache generation.

## Phase P4 - Worker-Generated Terrain Render LOD Meshes

Keep bgfx upload on the main thread, but generate terrain render mesh data on workers.

Scope:
- Add plain terrain render mesh data:
  - vertices;
  - indices;
  - bounds;
  - target LOD index;
  - source terrain tile identity/generation serial.
- Worker jobs generate LOD terrain vertices, normals, tangents, UVs, indices, and skirts from immutable CPU terrain snapshots.
- Main thread validates the tile is still alive/current, then uploads one or a small number of completed renderer meshes per frame.
- Existing CPU terrain heights remain gameplay authority.

Acceptance:
- Terrain LOD rebuild work no longer includes vertex/index generation on the main thread.
- Terrain LOD transitions remain visually correct and can be cancelled if stale.
- `Terrain LOD` long-frame spikes point only to bgfx upload/destruction, not CPU mesh generation.

Deferred:
- Async bgfx resource creation, terrain geomorphing, stitching, or material blending.

## Phase P5 - Incremental Renderer Visibility Metadata

Avoid full loaded-set scans when only a few renderables or debug knobs change.

Scope:
- Track changed chunks/renderables when:
  - chunk loads;
  - chunk unloads;
  - terrain LOD rebuilds replace terrain renderer handles;
  - object edit/place/remove changes one object;
  - debug layer/distance settings change globally.
- For local changes, reapply metadata only to touched terrain tiles/instances.
- Keep a global full reapply path for debug setting changes that truly affect everything.
- Split full reapply by chunk if needed.

Acceptance:
- `apply renderer visibility metadata` does not scan every loaded chunk after a single tile rebuild or object edit.
- Render group/layer/material/distance metadata remains correct after terrain LOD rebuilds.

Deferred:
- Renderer-owned visibility graph, editor selection layers, and per-material render queues.

## Phase P6 - Debug Geometry Budgeting And Worker Preparation

Prevent debug visualization from becoming the hitch source.

Scope:
- Add hard caps for debug line generation:
  - navmesh edges;
  - graph edges;
  - terrain slope warnings;
  - collision bounds;
  - chunk borders.
- Build expensive debug line lists from immutable snapshots on workers where possible.
- Main thread only copies/submits a capped number of finished debug primitives.
- Add UI counters for generated, submitted, and clipped debug primitives.

Acceptance:
- Enabling navmesh/graph debug draw does not produce large long-frame spikes.
- Debug draw remains visually useful even when capped.

Deferred:
- Persistent debug primitive lifetimes, labels/text rendering in world, gizmo handles, and GPU debug overlays.

## Phase P7 - Further Phased Resource Destruction

Continue splitting resource teardown if long-frame diagnostics point at destruction.

Scope:
- Keep chunk unload phases, but add finer slow-item labels around:
  - terrain renderer destroy;
  - mesh instance destroy batches;
  - render group destroy;
  - nav tile destroy.
- Cap renderer terrain destruction/upload to one or a small number per frame.
- If bgfx destruction/upload still stalls, consider delayed destruction queues that release resources over multiple frames.

Acceptance:
- Chunk boundary crossing does not produce a single large destruction spike.
- Long-frame diagnostics identify which resource type, if any, remains expensive.

Deferred:
- Multi-frame GPU fences, renderer memory pools, and a full render-resource garbage collector.

## Phase P8 - Save/Reload And Editor Operation Budgeting

Route debug editor reloads and save-backed object edits through the same streaming pipeline.

Scope:
- Avoid `unloadAll` during normal debug edit workflows.
- For changed objects, rebuild only affected chunks and nav tiles.
- For chunk-crossing object edits, schedule old/new chunk unload/load or partial refresh through budgeted work.
- Keep explicit full reload/reset as a debug action, but clearly label it as blocking or route it through staged work.

Acceptance:
- Removing, placing, moving, or resetting a persistent object does not trigger an unbounded frame spike.
- Active selections are cleared or remapped safely when edited chunks refresh.

Deferred:
- In-game editor UX, undo/redo, transaction logs, and persistent authored world files.

## Phase P9 - Long-Frame Capture History

Expand the current single long-frame record into a short rolling history.

Scope:
- Store the last N long frames with:
  - frame CPU time;
  - slowest budget item;
  - CPU bucket timings;
  - pending work counts;
  - chunk center;
  - loaded/pending nav/chunk counts;
  - cache hit/miss/write counters.
- Add a Debug UI table and optional text export.

Acceptance:
- Rare hitches can be compared over time without watching the exact moment they occur.
- The history makes it clear whether hitches are recurring from the same system or from different one-off tasks.

Deferred:
- External profiler integration, trace files, ETW, Tracy, Remotery, or Chrome tracing.

## Current Priority Order

1. Async navigation cache reads/writes.
2. Worker-built world navigation graph.
3. Fine-grained connectivity rebuild phases.
4. Worker-generated terrain render LOD meshes.
5. Incremental renderer visibility metadata.
6. Debug geometry caps and worker preparation.
7. More detailed resource destruction pacing if long-frame diagnostics point there.
8. Budgeted save/reload/editor refresh paths.
9. Rolling long-frame history/export.
