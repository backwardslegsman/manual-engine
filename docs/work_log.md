# ManualEngine Work Log

Append a short entry after each meaningful feature, refactor, or documentation pass. Each entry should include:

- Date in `YYYY-MM-DD` format.
- One short title.
- `Changed:` one or two bullets summarizing what was done.
- `Rationale:` one or two bullets explaining why the work was done or what boundary it clarified.

Keep entries concise. This is a persistent project memory, not a changelog for every line edited.

## 2026-06-13 - Architecture Documentation And Update Rule

Changed:
- Added `docs/engine_overview.md` summarizing current subsystem ownership, data flow, feature surface, save-facing contracts, and encapsulation guidance.
- Added this work log and updated AGENTS guidance so future passes append a short entry with work done and rationale.

Rationale:
- The prototype now has enough engine, renderer, world, persistence, editor, and debug systems that their boundaries need to be documented outside the conversation history.
- Future changes should preserve intent by recording not only what changed, but why it belongs in a given subsystem.

## 2026-06-13 - Open-World Runtime Foundation Snapshot

Changed:
- Established a small open-world runtime with fixed-step updates, world-owned transforms, explicit renderer bindings, camera modes, actor movement, terrain grounding, blocking collision, chunk streaming, biome-driven terrain/props, stable object IDs, save-backed overrides, and debug picking/editing.
- Built renderer-side scene submission around handles, materials, terrain tiles, render groups, culling, batching stats, atmosphere settings, and Dear ImGui diagnostics.

Rationale:
- The engine is intentionally simple but now has the minimum structure needed to exercise open-world lifetime, persistence, rendering, and debug workflows.
- Stable identity plus regenerated deterministic baseline content keeps saves small while still allowing object removal, placement, and transform edits.

## 2026-06-13 - Persistent Object Editor Extraction

Changed:
- Moved save-backed selected-object editing, placement, removal, and override reset behavior out of `src/App/main.cpp` into `Engine::PersistentObjectEditor`.
- Centralized custom/procedural `ObjectId` parsing helpers in the Engine identity API so App no longer owns save-facing ID interpretation.

Rationale:
- App should translate debug UI and interaction intent into service calls, not own reusable world mutation rules.
- Persistent object editing must consistently go through `WorldObjectOverrides` so live edits, chunk regeneration, and save/load share the same stable-ID behavior.

## 2026-06-13 - Per-Biome Terrain Materials

Changed:
- Added App-owned runtime terrain materials keyed by biome ID, using each biome descriptor's configured terrain color.
- Assigned terrain tiles and LOD-rebuilt renderer terrain handles from the primary chunk biome, with a cyan fallback for unknown biome IDs.

Rationale:
- Biomes need visible terrain identity before heavier material blending or splat maps are justified.
- Keeping assignment one material per tile preserves deterministic chunk regeneration and keeps renderer/material lifetime explicit.

## 2026-06-13 - Debug Draw Primitives

Changed:
- Added a renderer-owned transient debug line queue with wire boxes, XZ rectangles, camera frustum lines, and Dear ImGui toggles.
- Enqueued selected bounds, collision bounds, chunk borders, terrain tile bounds, camera frustum, and actor movement diagnostics from App debug state.

Rationale:
- Terrain, picking, collision, and streaming need immediate visual diagnostics without changing simulation or renderer resource ownership.
- Keeping primitives transient preserves the existing frame-driven debug model and avoids persistent debug object lifetime.

## 2026-06-13 - Navigation Roadmap And Recast Dependency

Changed:
- Added `docs/navigation_roadmap.md` outlining the Recast + Detour path to Kenshi-like near movement.
- Added the `recastnavigation` vcpkg dependency as the planned navigation backend.

Rationale:
- Near movement needs a real navmesh backend, but the integration should stay hidden behind Engine navigation APIs.
- Capturing the phased plan keeps long-distance navigation, crowd behavior, and dynamic obstacle work intentionally deferred.

## 2026-06-13 - Navigation Backend Boundary

Changed:
- Added `Engine::NavigationSystem` public types and safe query/build stubs for the Recast/Detour backend.
- Confined Recast/Detour includes and allocation to the navigation implementation file.

Rationale:
- Navigation needs a stable Engine-facing API before terrain tile generation and actor path following are added.
- Keeping third-party nav types private protects App, terrain, actors, and persistence from backend coupling.

## 2026-06-13 - Terrain-Only Navigation Tiles

Changed:
- Added CPU terrain build-data extraction and synchronous Recast/Detour nav tile generation for loaded terrain chunks.
- Mirrored loaded chunks into App-owned navigation tiles and exposed basic navigation build/query debug stats.

Rationale:
- Terrain-only navmesh tiles prove the backend and chunk synchronization before actor path following or dynamic obstacle work.
- Keeping navigation synced from App avoids making `ChunkStreamer` own another subsystem before the lifecycle pressures are clear.

## 2026-06-13 - Click-To-Move Player Navigation

Changed:
- Added semantic right-click destination and Escape cancel input actions, with click-vs-drag filtering so RMB camera pan remains usable.
- Extended the player actor controller with terrain-only nav path requests, corner following, cancellation, arrival/failure status, and debug path drawing/status.

Rationale:
- The first playable navigation loop should move one actor over loaded terrain before adding long-distance planning, prop-baked navmesh, or crowd behavior.
- Keeping path state in the actor controller reuses existing movement speed, facing, terrain grounding, and collision safety behavior.

## 2026-06-13 - Robust Actor Path State

Changed:
- Hardened actor path-following with explicit path settings, blocked/repathing status, blocked tick tracking, limited synchronous repath, and reusable actor path APIs.
- Expanded player navigation debug output and path drawing so completed corners, active corners, and blocked/failed destinations are visible.

Rationale:
- Path following should be a reusable Engine actor capability before adding multi-selection, NPC movement, or prop-baked navmesh.
- Limited repath keeps the terrain-only movement loop robust enough for debugging without introducing crowd simulation or dynamic obstacle systems.

## 2026-06-13 - Navigation Debug Draw And Tuning

Changed:
- Added plain Engine navigation debug geometry for navmesh polygon edges while keeping Detour/Recast private.
- Added Dear ImGui navigation toggles, nearest-point/path stats, manual visible-tile rebuild, and agent/build tuning controls.

Rationale:
- Navigation needs visual inspection before prop-baked navmesh or group movement can be tuned reliably.
- Manual rebuild keeps expensive Recast work explicit while still allowing quick iteration on agent and cell-size settings.

## 2026-06-13 - Static Prop Blocking In Navmesh

Changed:
- Added conservative collision AABB blocker geometry to navigation tile build data and rasterized blockers as unwalkable Recast areas.
- Rebuilt loaded nav tiles from terrain plus current blocking props, with debug counts, selected blocker status, and blocker bounds visualization.

Rationale:
- Static blockers make near movement respect trees, rocks, and markers without introducing DetourTileCache or crowd avoidance yet.
- Reusing world CPU bounds and collision flags keeps nav blocking aligned with existing gameplay/debug collision behavior.

## 2026-06-13 - Multi-Selection Actor Movement

Changed:
- Added Engine actor selection state and screen-rect actor selection helpers.
- Spawned a small demo squad and routed selected actors to deterministic formation offsets through independent navigation path requests.
- Expanded navigation debug output and debug draw to show selected actors, formation destinations, and selected actor paths.

Rationale:
- Kenshi-like near movement needs group commands, but V1 can stay simple by keeping one path per actor and deferring shared corridors, slot reservation, and crowd avoidance.
- Keeping selection and path commands in Engine-facing APIs avoids coupling App debug picking or Renderer code to actor internals.

## 2026-06-13 - Near-Movement Polish

Changed:
- Added a semantic `player.stop` action bound to Space for selected actors or the player fallback.
- Routed right-click object targets through the interaction handler while preserving right-click terrain movement.
- Added persistent command feedback, unreachable formation markers, and per-command failure summaries to navigation debug display.

Rationale:
- Local movement needs clear stop, interact, and failure feedback before expanding into queued commands or richer selection UI.
- Keeping the polish on semantic input actions and existing interaction/path systems avoids adding a separate command architecture too early.

## 2026-06-13 - Navigation Cleanup And Encapsulation Pass

Changed:
- Moved deterministic group formation offset calculation from `src/App/main.cpp` into a small Engine actor-command helper.
- Added public-header comments for actor selection and navigation tile/query ownership contracts.
- Updated App and Engine folder guidance to keep reusable command, formation, and navigation behavior out of sample composition code.

Rationale:
- The App loop should compose services and debug controls, not accumulate reusable movement rules.
- Navigation and actor command APIs are now easier to extend without leaking Recast/Detour details or renderer dependencies.

## 2026-06-13 - Open-World Navigation Roadmap Expansion

Changed:
- Expanded the navigation roadmap beyond near movement with chunk connectivity, coarse region graph, and hierarchical move command phases.
- Clarified that local Recast/Detour tiles remain the precise movement layer while long-distance travel uses a strategic graph above them.
- Expanded the deferred list for async builds, nav caches, dynamic obstacle systems, roads, off-mesh links, crowd avoidance, and long-distance unloaded simulation.

Rationale:
- Open-world-scale movement needs a hierarchy instead of one giant navmesh query.
- Writing the next phases down now keeps upcoming work focused on connectivity and graph routing before introducing heavier runtime systems.

## 2026-06-13 - Chunk Navigation Connectivity

Changed:
- Added an Engine-owned navigation connectivity system that samples loaded nav tile borders and records coarse portal metadata per chunk edge.
- Added debug draw and Dear ImGui stats for portals, connected portals, partial chunks, blocked chunks, and camera/selected chunk connectivity summaries.
- Rebuilt connectivity after loaded nav tile sync/rebuild while keeping chunk and nav tile lifetime unchanged.

Rationale:
- Open-world pathing needs stable chunk-to-chunk connectivity before building a coarse graph or hierarchical move command.
- Keeping connectivity derived from public navigation queries preserves the Recast/Detour encapsulation boundary.

## 2026-06-13 - Coarse Region Graph And Large Test World

Changed:
- Added `WorldNavigationGraph` with deterministic chunk nodes, weighted edges, A* route queries, and coarse route debug draw/stats.
- Expanded the sample world profile to 96m chunks, a 7x7 loaded chunk set, a 33x33 generated graph area, larger camera bounds, and larger terrain LOD distances.
- Kept local Recast/Detour nav tiles loaded-only while graph connectivity can be generated beyond loaded chunks.

Rationale:
- Open-world-scale navigation needs a coarse strategic layer above local Detour pathing.
- The larger sample profile makes graph routing and chunk-scale debug behavior visible before hierarchical actor movement is added.

## 2026-06-13 - Systems And Contracts Guide

Changed:
- Added `docs/system_contracts.md` listing current gameplay systems, rendering features, initialization order, and cross-system contracts.
- Updated agent guidance to require consulting and maintaining the guide when adding features, public interfaces, or initialization dependencies.

Rationale:
- The prototype now has enough interacting systems that feature work needs one explicit contract checklist, not only roadmap notes.
- Keeping initialization and ownership rules documented reduces accidental coupling between App, Engine, Renderer, and Assets.

## 2026-06-13 - Hierarchical Move Command

Changed:
- Added actor-owned hierarchical route state above local Detour path state.
- Routed terrain move commands through `WorldNavigationGraph` so selected actors can follow coarse route waypoints with local Detour segments.
- Added route status, waypoint progress, final destination, and waiting/failure messages to debug display and debug draw.

Rationale:
- Open-world movement needs long commands to be decomposed into chunk-scale guidance plus precise loaded-tile pathing.
- Keeping chunk streaming camera/App-owned makes unloaded local nav tiles an explicit waiting/failure state instead of hidden teleporting or forced streaming.

## 2026-06-13 - Navigation Cache Generation Phase

Changed:
- Added Phase 12 to the navigation roadmap for generated local nav tile, chunk connectivity, and coarse graph cache artifacts.
- Defined cache manifest/version inputs so terrain, biome, archetype, Recast, and agent setting changes invalidate derived navigation data.
- Clarified that cache files are derived data and runtime must fall back to synchronous generation on cache misses.

Rationale:
- Open-world navigation needs a cache path before larger regions can be practical without frame-time rebuild spikes.
- Treating cache data as derived and versioned keeps save files, authored config, and runtime persistence separate.

## 2026-06-13 - Navigation Cache Implementation

Changed:
- Added `NavigationCache` for versioned local nav tile, connectivity, and world graph cache files under `generated/navigation_cache`.
- Added plain tile byte import/export APIs to `NavigationSystem` while keeping Recast/Detour private to `Navigation.cpp`.
- Wired App nav sync to load cache records when safe, write successful baseline builds, and expose cache controls/stats in Dear ImGui.

Rationale:
- Cached baseline nav data reduces repeat synchronous Recast work without changing chunk ownership or save files.
- Override-aware cache bypass keeps save-backed object edits authoritative over derived baseline cache artifacts.

## 2026-06-13 - Release Build Without Debug Hooks

Changed:
- Added `MANUAL_ENGINE_ENABLE_DEBUG_TOOLS`, enabled for Debug and disabled for Release.
- Stubbed Dear ImGui debug UI calls and skipped debug primitive gathering/submission in Release builds.
- Removed the ImGui link dependency from Release builds while preserving Debug tooling.

Rationale:
- Release builds should exercise the game/runtime path without debug UI capture, debug draw submission, or editor/debug knobs.
- Keeping the same App call sites with no-op debug implementations avoids a separate release-only main loop.

## 2026-06-13 - Dirty Runtime Updates And Release Picking

Changed:
- Added App-owned dirty flags so nav tile sync, navigation connectivity, world graph rebuilds, renderer visibility metadata, and picking run only when their inputs change.
- Changed chunk streaming and terrain LOD updates to report whether they changed runtime state, giving App clear triggers for dependent rebuilds.
- Gated Release picking to command-time target acquisition while preserving continuous Debug hover picking and adding debug-only frame timing stats.

Rationale:
- The larger open-world profile made repeated derived-data rebuilds the likely frame-time floor.
- Dirty orchestration keeps the simple synchronous architecture while avoiding expensive work on stationary frames.

## 2026-06-13 - Chunk Seam Route Bridging

Changed:
- Added neighbor-ingress waypoints to coarse world routes so each chunk transition has an egress point and an ingress point.
- Added a short validated actor seam bridge fallback when Detour cannot produce a direct path across adjacent loaded chunk tiles.
- Kept unloaded neighbor chunks as waiting/failure route states rather than forcing streaming or teleporting actors.

Rationale:
- Independently built Recast tiles can fail exact edge stitching even when the coarse graph correctly identifies a traversable chunk border.
- A small explicit seam bridge keeps the current synchronous chunk-nav architecture usable while preserving local Detour pathing inside each chunk.

## 2026-06-13 - Navigation Portal Cache Invalidation

Changed:
- Bumped the navigation cache generator version after portal/seam behavior changed so stale zero-portal connectivity records are not reused.
- Loosened loaded-chunk portal sampling defaults for the larger 96m test chunks.

Rationale:
- Cached connectivity is derived data; algorithm changes must invalidate it or the debug UI can keep showing old portal results.
- Larger rolling terrain benefits from more edge samples and less brittle edge-band/link tolerances while navigation remains loaded-tile-only.

## 2026-06-13 - Paired Portal Route Waypoints

Changed:
- Extended coarse graph edges with both source portal and neighbor ingress portal waypoints.
- Serialized the new ingress waypoint in graph cache records and bumped the navigation cache generator version.
- Increased the validated actor seam bridge distance to match the wider connected-portal tolerance.

Rationale:
- A single portal point plus a blind offset can miss the actual matched portal on the neighbor chunk.
- Routing through paired portal positions gives the actor a concrete local target on both sides of a chunk transition.

## 2026-06-13 - Tile-Specific Portal Snapping

Changed:
- Added a tile-specific nearest-point query to `NavigationSystem` without exposing Recast/Detour types.
- Added chunk metadata for route waypoints so actor route following can snap neighbor portal targets against the intended loaded nav tile.
- Rejected local paths that report success but end on the wrong chunk-side target.

Rationale:
- A global nearest-poly query near a chunk border can snap a neighbor portal target back onto the actor's current tile.
- Chunk-tagged route waypoints make seam transitions explicit and prevent actors from walking to a same-chunk substitute point.

## 2026-06-13 - Explicit Portal Pair Links

Changed:
- Added the exact connected neighbor position to each loaded chunk portal.
- Changed portal debug draw and world graph route waypoints to use explicit portal pairs instead of searching for any connected portal on the neighbor edge.
- Bumped the navigation cache generator version so old connectivity/graph records with ambiguous portal links are ignored.

Rationale:
- A boolean "connected" flag was not enough; debug draw and route generation could pair a portal with the wrong neighbor portal and create long diagonal edge links.
- Explicit portal pairs make the visible connection and the route transition use the same data.

## 2026-06-13 - Multi-Portal Graph Edges

Changed:
- Changed the coarse world graph to keep one edge per connected portal pair between loaded neighboring chunks.
- Updated A* route planning to remember the exact edge selected for each chunk transition.
- Added start/end proximity cost so the first and last chunk transition prefer nearby portal pairs.
- Bumped the navigation cache generator version to invalidate graph records with only one edge per chunk pair.

Rationale:
- A single graph edge per neighboring chunk pair forced actors to use one portal no matter which connected portal was closest.
- Multiple portal-pair edges make route selection reflect the visible portal network.

## 2026-06-13 - Same-Chunk Portal Detours

Changed:
- Actor route commands now try a direct local Detour path before requesting a coarse graph route.
- Added a same-chunk detour route path that can leave the chunk through one connected portal pair and re-enter through another when the direct local path is blocked.
- Kept this as an adjacent-chunk fallback instead of a general cycle search.

Rationale:
- Same-chunk endpoints can still require a portal detour when static blockers split the local navmesh.
- Direct-first behavior preserves simple same-chunk movement while allowing blocked cases to use the chunk connectivity layer.

## 2026-06-14 - Navigation CTest Fixes

Changed:
- Added a narrow adjacent-tile seam bridge fallback inside `NavigationSystem::findPath` for partial Detour corridors between adjacent loaded tiles.
- Tightened actor path command acceptance so `Success` results with `NavPath::complete == false` are rejected and can fall back to hierarchical routing.

Rationale:
- The navigation tests exposed that cross-tile Detour queries could return partial paths while still reporting a successful query status.
- Actor movement should only treat complete paths as usable movement commands; partial paths are route-planning failures, not destinations.

## 2026-06-14 - Navigation Climb And Slope Test Fixes

Changed:
- Made adjacent-tile seam bridge completion respect `NavAgentSettings::maxClimb` so height jumps above the agent climb limit stay incomplete.
- Added a small slope-threshold epsilon for Recast tile builds so exact-at-limit slopes behave as configured.
- Removed Recast low-hanging obstacle promotion from static-blocker tile builds so high `maxClimb` values do not make blocker geometry walkable.
- Marked same-tile paths that leave their tile as incomplete so higher-level same-chunk portal detours handle those cases explicitly.

Rationale:
- Expanded navigation tests covered the distinction between climbable terrain steps and static blockers.
- Static blocker geometry should remain blocking even when an agent can climb large terrain steps or slopes.

## 2026-06-14 - Navigation Diagnostics Roadmap

Changed:
- Added Phase 13 to `docs/navigation_roadmap.md` for navigation diagnostics and tuning.
- Scoped tile build stats, portal candidate/rejection stats, path failure analysis, and YAML nav profiles.
- Marked the phase as debug-only and explicitly deferred new pathfinding behavior.

Rationale:
- The current navigation stack needs better explanations for missing hilltop navmesh, portal density, and failed movement commands before more behavior is added.
- Debug diagnostics should make Recast and graph decisions tunable without turning debug knobs into gameplay state.

## 2026-06-14 - Headless Navigation Test Harness

Changed:
- Added a CTest navigation executable with deterministic C++ map fixtures, direct nav queries, actor fixed-step simulation, route checks, manual-input cancellation, and collision reaction coverage.
- Added CPU-only terrain fixture support so tests can create explicit heightfield tiles without initializing renderer resources.
- Added renderer stubs for the test target so navigation and actor tests stay headless.

Rationale:
- Navigation regressions need repeatable command-and-reaction coverage outside the SDL/bgfx sample app.
- Explicit fixture maps make chunk seams, blockers, slopes, missing tiles, and route handoff failures easy to reproduce.

## 2026-06-14 - Navigation Elevation Threshold Tests

Changed:
- Expanded the headless navigation suite with max-climb step tests below, at, and above the configured climb height.
- Added slope tests below, at, and above multiple max-slope settings.
- Covered threshold cases inside one chunk, across chunk seams, and same-chunk endpoint detours that should route through neighboring chunks.

Rationale:
- Elevation limits are a core agent contract and need deterministic regression coverage separate from visual debugging.
- Testing the same thresholds across local, seam, and coarse-route contexts makes it easier to isolate whether failures come from Recast build settings, Detour tile stitching, or actor route handoff.

## 2026-06-14 - Navigation Diagnostics And Tuning

Changed:
- Added plain Engine tile diagnostics for live/cache nav tile sources, Recast build counts, walkable triangle counts, polygon/detail counts, bounds, and settings.
- Added portal diagnostics for accepted, connected, rejected, and merged samples per chunk edge, plus Dear ImGui controls to tune portal sampling and rebuild connectivity/graph without rebuilding nav tiles.
- Added actor command diagnostics for direct local query status, route fallback status, current waypoint chunk availability, and final command reason.
- Added YAML navigation profile loading from `assets/config/navigation_profiles.yaml` and included the active profile ID in the navigation cache manifest identity.
- Added focused navigation CTests for tile diagnostics, portal diagnostics, and actor command diagnostics.

Rationale:
- Open-world navigation tuning needs concrete explanations for missing navmesh, sparse portals, and failed movement commands before adding more movement behavior.
- Diagnostics stay read-only Engine data so debug UI can explain decisions without changing pathfinding outcomes.

## 2026-06-14 - Debug UI Readability Pass

Changed:
- Grouped the Dear ImGui debug window into Render, World, Navigation, Camera/Biome, Picking, and Groups tabs.
- Set a first-use debug window size so dense controls have enough room to read.
- Disabled renderer fog by default while keeping the fog controls available in the Render tab.

Rationale:
- The single long debug panel had become hard to scan as navigation and world-edit diagnostics grew.
- Fog should be an opt-in atmosphere effect while terrain/navigation visibility is still being tuned.

## 2026-06-14 - Terrain Generation Tuning

Changed:
- Added biome terrain-shape fields for base height, rolling/detail amplitudes, frequencies, and nav slope hints.
- Replaced hardcoded terrain height constants with biome-configured generation while keeping old scale fields as compatibility multipliers.
- Added CPU terrain tile diagnostics for height range, average height, max/average slope, and estimated nav-walkable triangle percent.
- Added a Terrain debug tab and optional slope warning debug draw markers.
- Expanded navigation tests with deterministic terrain, edge continuity, terrain diagnostics, and gentle-vs-steep profile coverage.

Rationale:
- Terrain shape is now easier to tune without recompiling, and gentler defaults should reduce isolated navmesh islands.
- Diagnostics should make steep chunks visible before trying to solve navigation issues in Recast or route code.

## 2026-06-14 - Async Terrain And Navigation Generation

Changed:
- Added `Engine::AsyncWorkQueue` for CPU-only background jobs.
- Added generated terrain tile data and main-thread tile creation from generated data.
- Split navigation tile generation into worker-safe Detour byte builds and main-thread live navmesh insertion.
- Added generated chunk prop descriptors and staged App chunk streaming with pending jobs, completed load queue, stale cancellation, and per-frame load/unload commit budgets.
- Added async streaming diagnostics and controls to the navigation debug UI.

Rationale:
- Camera movement across chunk boundaries should not synchronously generate terrain, props, Recast tiles, and destroy/create every changed chunk in one frame.
- Keeping workers limited to immutable snapshots and plain generated data preserves the existing ownership contracts for world, renderer, terrain, spatial registry, and live navigation state.

## 2026-06-14 - Global Main-Thread Frame Budget

Changed:
- Added `Engine::FrameBudget` and `Engine::MainThreadWorkQueue` for fixed-ms cooperative main-thread work scheduling.
- Split generated chunk commit into budgeted phases for render group creation, CPU terrain storage, renderer terrain creation, prop batches, nav tile insertion, and final metadata.
- Moved chunk unloads and dirty navigation/connectivity/graph/renderer-visibility rebuilds behind budgeted work items.
- Added frame budget debug stats and controls for budget enablement, budget ms, pending work count, overrun, category timings, and prop batch size.
- Added focused tests for budget deferral, critical overrun behavior, and queue preservation of deferred work.

Rationale:
- Async generation removed worker-side cost from the frame, but live renderer/world/nav commits still needed a shared main-thread budget to avoid one hitch per committed chunk.
- A small cooperative budget is enough for the current engine and leaves room for future systems to share main-thread time without introducing a full task graph.

## 2026-06-14 - Explicit Chunk Commit Phase Scheduling

Changed:
- Added explicit names, budget categories, and priorities for each generated chunk commit phase.
- Chunk commit work queue labels now identify the exact phase being run or deferred, such as CPU terrain creation, renderer terrain creation, prop batch spawning, nav tile insertion, and final metadata.

Rationale:
- The frame budget is only useful if expensive main-thread work is split into visible cooperative units.
- Named phases make it easier to tune the commit pipeline and identify which phase still overruns the frame budget.

## 2026-06-14 - Smaller Sample Chunks

Changed:
- Reduced the sample chunk size from `96m` to `24m`.
- Scaled the sample loaded chunk radius from `3` to `12` and the coarse graph radius from `16` to `64` so the prototype keeps roughly the same world-space coverage.
- Updated navigation cache and world graph defaults to match the new sample chunk scale.

Rationale:
- Smaller chunks reduce the amount of terrain, prop, renderer, and navigation work tied to one chunk commit.
- Keeping the physical coverage similar preserves the large-world test profile while making each individual streaming/nav unit easier to budget.

## 2026-06-14 - CPU Timing Probes

Changed:
- Added frame CPU timing probes for event polling, input mapping, camera update, chunk streaming, terrain LOD, budget queue drain, nav tile sync, connectivity, world graph rebuilds, fixed updates, world sync, picking, nearest-nav debug queries, interaction/command handling, debug primitive enqueue/draw, scene draw, and Dear ImGui build/render.
- Replaced the compact performance text in the navigation debug tab with a readable CPU probe table.

Rationale:
- Hitch reduction needs concrete timing attribution now that chunk generation, chunk commits, navigation, graph rebuilds, rendering, and debug tooling can all contribute to frame spikes.
- Keeping probes lightweight and local to the App frame loop avoids adding a profiler dependency while preserving enough detail to guide the next optimization pass.

## 2026-06-14 - Navigation CPU Profiling Test

Changed:
- Added a headless navigation profiling scenario to the CTest runner.
- The test times terrain CPU tile creation, navigation build data extraction, blocker geometry append, Recast tile builds, connectivity rebuild, world graph rebuild, nearest/path queries, coarse route queries, actor route command setup, and actor fixed updates.
- The ranked profile is printed during verbose test runs and written to `build/windows-vs-vcpkg/navigation_cpu_profile.txt`.

Rationale:
- Frame hitches need a repeatable test artifact that shows which CPU buckets dominate without relying on the SDL/bgfx runtime.
- The profiling test validates outputs but avoids machine-specific timing thresholds, so it can guide optimization without becoming flaky.

## 2026-06-14 - Runtime Navigation Hitch Reduction

Changed:
- Added runtime nav tile records for missing, pending, ready, failed, and dirty loaded chunks.
- Changed normal nav tile sync to prefer cache hits and enqueue worker Recast byte builds on misses instead of synchronously building Recast tiles on the main thread.
- Split visual chunk commit from nav tile availability so terrain/props can appear before local navigation is ready.
- Added `TerrainSettings::navigationResolution`, YAML navigation profile loading for `navigation_resolution`, cache manifest identity support, and nav tile diagnostics for source resolution.
- Added incremental connectivity APIs and switched normal tile insert/destroy updates to rebuild changed chunks and neighbors.
- Added debug counters for runtime nav cache hits/misses, worker build queue/completion/failure, and ready/pending/failed nav chunk counts.

Rationale:
- Profiling showed Recast tile generation was the dominant CPU cost, so the runtime path now keeps that work off the main thread and reduces source geometry before building.
- Connectivity was the second-largest headless bucket, so normal tile updates no longer require a full loaded-set portal rebuild.

## 2026-06-14 - Expanded CPU Profiling Probes

Changed:
- Added runtime CPU probes for `bgfx::frame()` and post-frame debug/save/rebuild request handling.
- Expanded the headless navigation CPU profile with Detour tile cache export/insert, incremental connectivity rebuild, full `33x33` nav-source Recast build, and reduced `17x17` nav-source Recast build buckets.

Rationale:
- The previous profile identified broad hotspots, but cache insertion, graphics frame wait, post-frame debug actions, and reduced-source Recast costs needed separate measurements.
- The latest Release profile shows cache export/insert is negligible, incremental connectivity is substantially cheaper than a full rebuild, and reduced nav source geometry lowers Recast build time.

## 2026-06-14 - Gameplay-Scale Render Distance Defaults

Changed:
- Changed default renderer distance culling from unlimited to finite draw ranges: props at `160m` and terrain at `280m`.
- Reduced the default camera far plane to `900m` and max orbit distance to `320m`.

Rationale:
- Unlimited terrain and prop submission made the large generated world more expensive than a typical gameplay view needs.
- Keeping runtime debug sliders preserves tuning flexibility while making the default profile more practical for normal play.

## 2026-06-15 - Budgeted Terrain LOD Rebuilds

Changed:
- Added a budgeted terrain LOD update path that limits renderer terrain buffer rebuilds per frame while reporting rebuilt and pending counts.
- Switched App runtime, save/load, and chunk reload paths away from all-at-once terrain LOD updates.
- Added a Dear ImGui performance control for terrain LOD rebuilds per frame.
- Added phased runtime chunk unloads so nav removal, prop destruction, terrain destruction, render-group destruction, and final dirty marking are separate budgeted work items.

Rationale:
- Camera movement can change desired LOD for many loaded terrain tiles at once. Recreating every affected renderer terrain buffer in a single frame produces visible hitches.
- CPU terrain data remains authoritative for gameplay, so stale renderer LOD for a few frames is an acceptable visual tradeoff for stable frame pacing.
- Moving across chunk boundaries can unload an entire row/column. Splitting unloads prevents one chunk teardown from destroying every world object and renderer resource in a single callback.

## 2026-06-15 - Chunk-Capped Navigation Runtime Work

Changed:
- Capped runtime nav tile sync so one queued work item only probes/schedules a fixed number of chunks and requeues remaining work.
- Capped dirty navigation connectivity rebuilds so portal sampling is spread across frames when many nav tiles change.
- Delayed coarse world graph rebuilds until connectivity is fully refreshed, and recentered the large graph only after the camera moves a configurable number of chunks.
- Added Dear ImGui controls and counters for nav sync chunks, connectivity chunks, and graph recenter threshold.

Rationale:
- Navigation hitching was still possible because cache probing, connectivity portal sampling, and graph rebuilds were budgeted as large atomic callbacks.
- Spreading tile sync/connectivity work over multiple frames and coalescing graph rebuilds keeps local pathing data progressive without doing all navigation-derived work on a single camera chunk crossing.

## 2026-06-15 - Dear ImGui Index Offset Fix

Changed:
- Fixed the bgfx Dear ImGui renderer to use `ImDrawCmd::IdxOffset` directly instead of adding a second manual running index offset.

Rationale:
- Modern Dear ImGui draw commands already provide command-list-relative index offsets. Double-offsetting later commands corrupted tab and text geometry, making debug UI labels appear glitchy or partially missing.

## 2026-06-15 - Long-Frame Diagnostics And Cache Write Policy

Changed:
- Added slowest-work-item attribution to `Engine::FrameBudget`.
- Added Debug UI long-frame diagnostics showing the last frame above the hitch threshold with CPU bucket timings, slowest budget item, and pending work count.
- Disabled navigation cache write-through by default while keeping cache reads enabled and manual cache generation controls available.

Rationale:
- Rare hitches every 20-30 seconds need event-style diagnostics because steady CPU probes can miss the responsible frame.
- Main-thread disk writes from automatic navigation cache write-through can stall unpredictably, so cache generation should be explicit unless the user is intentionally warming cache data.

## 2026-06-15 - Performance Roadmap

Changed:
- Added `docs/performance_roadmap.md` to track worker offload, frame budgeting, streaming, and hitch-reduction phases separately from the navigation feature roadmap.
- Cross-referenced the new roadmap from `docs/engine_overview.md`.

Rationale:
- The engine now has enough runtime systems that performance work needs its own planning document with explicit worker/main-thread boundaries and phased commit priorities.

## 2026-06-15 - Async Navigation Cache I/O

Changed:
- Added worker-safe navigation cache read/write helpers and result structs for tile, connectivity, and world graph cache records.
- Moved runtime nav tile cache reads, connectivity/graph cache reads, write-through writes, and manual cache generation writes onto `AsyncWorkQueue`.
- Added debug counters for async cache read/write jobs and cache stale/corrupt results.
- Added cache helper tests for missing files, tile round-trips, corrupt tile files, identity mismatch, and negative chunk coordinates.

Rationale:
- Navigation cache file I/O can stall unpredictably on the main thread. Workers now handle file reads/writes, while the main thread only merges results into stats and live systems.

## 2026-06-15 - Encapsulation Pass

Changed:
- Extracted navigation cache async job wrappers from `src/App/main.cpp` into `Engine::NavigationCacheAsync`.
- Tightened public comments around navigation cache manifest validity, worker-safe cache helpers, and cooperative main-thread budgeting.
- Updated App/Engine guidance and system contracts to describe the current async cache and budgeted main-thread ownership rules.

Rationale:
- App should compose services and choose scheduling policy, while reusable cache I/O job construction belongs in Engine.
- The performance work added new worker/main-thread contracts that need to be explicit in public headers and persistent guidance.

## 2026-06-15 - Worker-Built World Navigation Graph

Changed:
- Added worker-safe world navigation graph build snapshots and `WorldNavigationGraph::buildCacheData`.
- Switched runtime graph dirty processing to async cache read first, then worker graph build on cache miss or cache-disabled paths.
- Added graph worker build counters and last-build diagnostics to the navigation debug UI.
- Added tests covering worker graph parity with synchronous rebuilds and radius-zero graph data.

Rationale:
- Coarse graph rebuilds can cover a large generated region and should not run inside a main-thread frame callback.
- Keeping the output as `WorldNavigationGraphCacheData` preserves the live graph interface while moving construction to immutable worker snapshots.

## 2026-06-16 - Fine-Grained Connectivity Rebuild

Changed:
- Added phased `NavigationConnectivitySystem` rebuild handles that step through chunk setup, each edge, neighbor relinking, and finalization.
- Switched App dirty connectivity processing to run one budgeted connectivity step at a time and delay world graph refresh until connectivity finishes.
- Added Dear ImGui counters for connectivity samples, active chunk, and current step label.
- Added navigation tests covering phased/synchronous parity, one-sample stepping, cancellation, and clear-existing behavior.

Rationale:
- Loaded nav tiles can arrive in bursts, and rebuilding all affected portals in one callback can still hitch even when nav generation is asynchronous.
- Keeping connectivity on the main thread preserves the live Detour query boundary, while phased stepping gives the frame budget enough control to spread portal sampling across frames.

## 2026-06-16 - Worker-Generated Terrain Render LOD Meshes

Changed:
- Added terrain render mesh build inputs/results so LOD vertex/index generation can run on worker threads from immutable CPU tile snapshots.
- Split terrain LOD replacement into worker mesh generation and budgeted main-thread renderer terrain handle commits.
- Added debug stats for terrain LOD jobs, commits, stale results, pending work, and last worker build time.
- Added tests for deterministic terrain render mesh generation, stale generation rejection, and unchanged CPU height queries.

Rationale:
- Terrain LOD changes are visual work; CPU terrain remains authoritative for gameplay, picking, nav build data, and height queries.
- Moving vertex/index generation off the frame thread leaves only bgfx upload/destruction as main-thread work, making terrain LOD spikes easier to budget and diagnose.

## 2026-06-16 - Incremental Renderer Visibility Metadata

Changed:
- Replaced broad renderer metadata reapply with App-owned dirty chunk tracking and a capped full-reapply queue.
- Added direct `ChunkStreamer::visitLoadedChunkContent` lookup so local visibility metadata changes do not scan every loaded chunk.
- Added debug UI counters and controls for visibility chunks processed/deferred, terrain updates, instance updates, and full-reapply state.
- Added a focused test for direct loaded chunk content lookup.

Rationale:
- Terrain LOD commits, object edits, and chunk loads usually affect one chunk, so applying renderer material/layer/group/distance metadata across the whole loaded world is unnecessary.
- Keeping metadata orchestration in App preserves the current renderer handle API while making the expensive path budgetable and visible in the Performance panel.

## 2026-06-16 - Debug Geometry Budgeting

Changed:
- Added renderer debug draw caps, primitive batch types, and per-frame debug draw stats.
- Routed expensive debug visualization groups through App-side category caps for navmesh edges, world graph edges, terrain slope warnings, collision bounds, and chunk/bounds lines.
- Added Dear ImGui controls for debug draw caps and counters for generated, submitted, and clipped lines.

Rationale:
- Debug visualization can cover thousands of navigation and graph lines in the large-world profile.
- Capping debug geometry keeps diagnostic tools useful without allowing them to become the next frame hitch source.

## 2026-06-17 - Destruction Profiling Buckets

Changed:
- Extended the navigation CPU profile report with teardown buckets for Detour tile destruction, nav-system clear, connectivity chunk removal, graph clear, world object destruction with spatial unregister, chunk detach bookkeeping, and CPU terrain tile destruction.

Rationale:
- P7 should only split resource destruction further if teardown is actually visible in frame-time data.
- The headless profiler now distinguishes CPU teardown costs from generation/build/query costs, while keeping real renderer/bgfx destruction as a runtime-only measurement concern.

## 2026-06-17 - System Inbox Architecture Note

Changed:
- Added `docs/system_inboxes.md` to formalize receiver-owned typed inboxes, publish-only sinks, explicit flush order, and constraints around lifetime, threading, debugging, and tests.
- Cross-linked the inbox guidance from the overview, system contracts, and agent guide.

Rationale:
- Future event-style communication should keep ownership and dependencies visible instead of introducing a global dynamic event bus registry.
- The pattern gives systems flexible input surfaces while preserving deterministic frame ordering, testability, and main-thread mutation contracts.

## 2026-06-17 - Authored Scene Roadmap

Changed:
- Added `docs/authored_scene_roadmap.md` covering the path from source glTF import through material/texture/PBR rendering, Release Sponza mode, authored scene streaming, runtime caches, and diagnostics.
- Cross-linked the roadmap from the overview and agent guide.

Rationale:
- The Sponza asset exposes requirements beyond the current procedural sample renderer, including scene graph transforms, packed PBR textures, alpha/double-sided materials, large texture handling, and authored-scene lifetime.
- The roadmap keeps the target on a stable general scene pipeline instead of a one-off minimal Sponza load.

## 2026-06-17 - Authored Scene CPU Import Boundary

Changed:
- Added a CPU-only Assimp authored scene import API with scene nodes, transforms, mesh primitives, material/texture references, light records, bounds, and diagnostics.
- Added a headless asset import CTest target with a small embedded glTF fixture and optional Sponza validation.

Rationale:
- Authored scene loading needs a renderer-independent data boundary before GPU resource ownership, PBR material mapping, and Release scene mode can be built.
- Keeping the existing static mesh importer intact preserves current renderer behavior while giving later phases richer scene data.

## 2026-06-17 - Authored Scene Runtime Ownership

Changed:
- Added an Engine-owned authored scene runtime loader that creates renderer materials, static meshes, and mesh instances from imported CPU scene data, applies node world transforms, records diagnostics, and tears resources down in renderer-safe order.
- Added a renderer CPU static mesh creation API plus stubbed ownership and hidden-window renderer smoke tests.

Rationale:
- Authored scenes need explicit runtime resource ownership before Release scene mode, PBR material expansion, and streaming can be added.
- The smoke path verifies the new ownership layer can submit through the real renderer without changing the procedural sample app startup.

## 2026-06-17 - Authored Scene Sector Streaming

Changed:
- Added deterministic authored scene partition metadata with sector bounds, node/mesh/material/texture/light references, and payload estimates.
- Added a partitioned authored scene owner that loads and unloads sector renderer resources through budgeted main-thread work while sharing materials and cached textures across loaded sectors.
- Switched authored app mode to the partitioned loader and exposed sector counts through authored diagnostics/status.
- Added authored scene tests for partition determinism, sector streaming, render group cleanup, shared material references, and renderer smoke coverage.

Rationale:
- Large authored scenes should not remain an all-or-nothing renderer resource set.
- Sector manifests and budgeted commits create the ownership boundary needed for streaming now, while leaving derived scene caches and compressed texture pipelines to later phases.

## 2026-06-17 - Authored Scene Runtime Cache

Changed:
- Added an authored scene derived-data cache with manifest identity based on source hash, importer/material/texture/vertex/partition versions, and partition settings.
- Stored scene metadata and sector manifests in YAML plus renderer-ready mesh vertex/index payloads in binary files under `generated/authored_scene_cache`.
- Wired partitioned authored scene loading to use cache hits when enabled and to fall back to source import on miss, stale, or corrupt cache data.
- Added tests for cache round trips, cache policy behavior, stale/corrupt fallback, partition identity changes, and cached partitioned scene loading.

Rationale:
- Release authored scene startup should be able to skip source glTF parsing when a matching derived cache exists.
- Keeping cache files derived and optional preserves robust source fallback while preparing for later texture/cache processing phases.
## 2026-06-18 - Scene Component Runtime Roadmap

Changed:
- Added `docs/scene_component_roadmap.md` with a high-level scene/component runtime roadmap and granular per-phase plans.
- Added `docs/scene_runtime/README.md` as the index for future implementable scene-runtime phase plans.
- Added documentation-only skeleton guidance for future scene, component, physics, scripting, debug visualization, and asset registry source areas.
- Updated asset guidance to reflect that general authored import can preserve skeletal/animated CPU data while static runtime loaders reject unsupported animated payloads.

Rationale:
- The next architecture pass should consolidate existing procedural, authored scene, animated model, renderer, navigation, cache, and async systems instead of rebuilding them independently.
- Adding guidance files now makes future implementation work easier to scope without introducing build-affecting code.
## 2026-06-18 - Authored And Animation Interface Cleanup

Changed:
- Removed obsolete imported-scene diagnostics that labeled animation and skinned mesh data as unsupported even though the CPU importer now preserves those records.
- Added a shared `Assets::Assimp::containsSkeletalOrAnimationData()` predicate and routed static authored scene, animated model, and async loading checks through it.
- Updated static authored scene rejection text to point users at the existing animated model runtime.
- Bumped the authored scene cache format version after the diagnostics schema cleanup.
- Refreshed authored scene and animation roadmap current-state snapshots so they describe the implemented pipeline rather than old gap analysis.

Rationale:
- The static authored scene and animated model paths should share the same import-boundary rule instead of duplicating skeletal/animation checks in multiple runtime owners.
- Diagnostics should describe preserved imported data accurately and reserve “unsupported” wording for actual unsupported behavior.
