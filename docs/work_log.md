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
## 2026-06-18 - Scene Runtime Pre-Contracts

Changed:
- Added future scene runtime identity rules to `docs/system_contracts.md`.
- Added the focused Phase 1 scene-kernel plan under `docs/scene_runtime`.
- Linked the scene-component roadmap's first phase to the new handle and identity contract.

Rationale:
- Scene/component implementation needs clear runtime handle, stable ID, asset handle, renderer handle, and future physics/navigation handle boundaries before code is introduced.
## 2026-06-18 - Imported Scene Resource Mapping Helper

Changed:
- Added a shared Engine helper for imported-scene vertex conversion, texture descriptor creation, material descriptor creation, and all-material texture acquisition.
- Updated authored scene and animated model runtime loading to consume the shared helper instead of maintaining duplicate mapping code.

Rationale:
- Static authored scenes and animated models share material, texture, and vertex conversion policy; keeping that policy in one Engine helper reduces drift before scene/component integration begins.
## 2026-06-18 - App Authored Runtime Extraction

Changed:
- Moved authored-scene and animated-sample startup/runtime helper state out of `src/App/main.cpp` into an app-local runtime include.
- Kept the top-level authored frame loop in `main.cpp` while reducing inline startup, async, fallback, and debug-population helper code.

Rationale:
- The scene/component roadmap should start with `main.cpp` acting as composition and frame-loop code rather than owning large authored/animated state machines inline.

## 2026-06-18 - Scene Kernel Phase 1 Plan Refinement

Changed:
- Expanded the Phase 1 scene-kernel plan with concrete handle names, handle layout, lifecycle semantics, metadata-only component storage rules, API shape, and test target expectations.

Rationale:
- The scene runtime implementation needs to avoid the existing procedural `ActorHandle` name and lock destruction/iteration semantics before code is introduced.

## 2026-06-19 - Scene Kernel Phase 1 Implementation

Changed:
- Added the renderer-independent `Engine::Scene` kernel with generation-counted scene actor/component handles, stable scene ID metadata, and metadata-only component attachment.
- Added isolated scene kernel tests and documented the new runtime identity contract.

Rationale:
- The scene/component runtime needs a small handle-safe ownership shell before transforms, renderer bridges, serialization, physics, scripting, navigation, or authored-scene migration are introduced.

## 2026-06-19 - Transform Hierarchy Phase 2 Plan

Changed:
- Added a dedicated Phase 2 transform hierarchy plan covering scene-owned local/world transforms, hierarchy mutation, preserve-world behavior, dirty propagation, destroy cleanup, and CPU-only tests.
- Updated the scene/component roadmap to link the detailed phase plan and lock the transform hierarchy scope boundaries.

Rationale:
- The transform hierarchy needs precise ownership, invalid-handle, cycle-prevention, and update-order semantics before implementation starts.

## 2026-06-19 - Transform Hierarchy Phase 2 Implementation

Changed:
- Added scene-owned local/world transforms, parent/child hierarchy state, dirty propagation, preserve-world hierarchy mutation, and destroy/reuse cleanup to `Engine::Scene`.
- Extended isolated scene tests to cover transform composition, hierarchy mutation, cycle rejection, pending-destroy exclusion, and slot cleanup.

Rationale:
- The scene runtime needs deterministic transform hierarchy behavior before renderer, authored-scene, physics, navigation, serialization, or scripting adapters consume scene actors.

## 2026-06-19 - Tick Scheduler Phase 3 Plan

Changed:
- Added a dedicated Phase 3 tick scheduler plan covering scene lifecycle, fixed and variable phases, system registration, pause behavior, transform refresh timing, diagnostics, and CPU-only tests.
- Updated the scene/component roadmap milestone to link the detailed plan and lock the scheduler scope boundaries.

Rationale:
- Future renderer, physics, navigation, animation, scripting, and behavior phases need a deterministic scene phase contract before they register runtime systems.

## 2026-06-19 - Tick Scheduler Phase 3 Implementation

Changed:
- Added `Engine::Scene` system registration, lifecycle transitions, fixed/variable tick phases, pause handling, transform refresh before pre-render, and scheduler diagnostics.
- Extended isolated scene tests to cover lifecycle order, phase order, enable/disable behavior, pause behavior, stale system handles, diagnostics, and actor destruction during ticks.

Rationale:
- Scene-owned systems need deterministic runtime ordering before renderer, physics, navigation, animation, scripting, or native behavior adapters plug into the scene runtime.

## 2026-06-19 - Asset Registry Phase 4 Plan

Changed:
- Added a dedicated Phase 4 asset registry plan covering stable asset IDs, runtime asset handles, source metadata, import settings identity, dependency tracking, stale/missing diagnostics, and CPU-only tests.
- Updated the scene/component roadmap milestone to link the detailed plan and keep `AssetCache`, authored-scene cache files, and current path-based runtime owners intact.

Rationale:
- Later scene components need stable asset references without coupling component storage to renderer handles, source importer details, or current path-based cache ownership.

## 2026-06-19 - Asset Registry Phase 4 Implementation

Changed:
- Added CPU-only `Engine::AssetRegistry` metadata ownership for stable asset IDs, transient asset handles, source format/status metadata, import settings identity, dependency edges, refresh diagnostics, and imported-scene texture dependency registration.
- Added isolated asset registry tests and documented the registry/cache ownership boundary.

Rationale:
- Scene components need stable asset identity and dependency metadata before renderer-resource components, serialization, or authored-scene conversion start consuming asset references.

## 2026-06-19 - Render Component Bridge Phase 5 Plan

Changed:
- Added a dedicated Phase 5 render component bridge plan covering scene mesh/light/camera component descriptors, renderer facade boundaries, asset/cache compatibility, pre-render sync, dirty tracking, diagnostics, and renderer-independent tests.
- Updated the scene/component roadmap milestone to link the detailed plan and keep authored-scene, animated-model, procedural world, and renderer ownership unchanged.

Rationale:
- Render components need a precise bridge contract before scene actors start creating renderer instances or later authored/animated adapters migrate onto the scene runtime.

## 2026-06-19 - Render Component Bridge Phase 5 Implementation

Changed:
- Added `Engine::SceneRenderBridge` with typed mesh, skinned mesh, light, and camera component handles plus backend-driven sync to renderer-facing resources.
- Added a production renderer backend adapter and isolated fake-backend tests for transform sync, resource lifetime, scheduler pre-render sync, camera view construction, and diagnostics.

Rationale:
- Scene actors need an explicit renderer bridge boundary before authored-scene and animated-model adapters can move onto the scene runtime without making Renderer depend on scene storage.

## 2026-06-19 - Authored Scene Adapter Phase 6 Plan

Changed:
- Added a dedicated Phase 6 authored scene adapter plan covering imported node-to-actor conversion, hierarchy preservation, static mesh and light render bridge components, resource ownership, diagnostics, and fixture tests.
- Updated the scene/component roadmap milestone to keep existing eager/partitioned authored-scene loaders, cache, async flow, and App runtime paths unchanged during the adapter pass.

Rationale:
- Static authored scenes need a controlled path into scene actors/components before authored runtime ownership can migrate away from bespoke renderer instance management.

## 2026-06-19 - Authored Scene Adapter Phase 6 Implementation

Changed:
- Added `Engine::adaptImportedSceneToScene` and release helpers for converting already-imported static authored scene CPU data into scene actors, hierarchy transforms, adapter-owned renderer resources, and scene render bridge mesh/light components.
- Added isolated authored scene adapter tests covering node bindings, hierarchy/world transforms, resource/component creation, texture diagnostics, light components, bridge sync, invalid references, skipped skinned/animation data, and deterministic release.

Rationale:
- Static authored content now has a parallel scene-runtime adapter path without changing the existing authored scene, partitioned streaming, cache, async, animated model, or App runtime owners.

## 2026-06-19 - Release Authored Sample Scene Adapter Path

Changed:
- Updated the release authored sample path to import static authored scene CPU data, adapt it into `Engine::Scene`, and sync renderer instances/lights through `SceneRenderBridge` and `RendererSceneRenderBackend`.
- Kept debug authored mode on the existing async partitioned authored scene path for streaming diagnostics, cache validation, and tooling coverage.

Rationale:
- The release sample should exercise the new scene actor/component adapter path while preserving the mature debug authored runtime for inspection and regression coverage.

## 2026-06-19 - Animated Model Adapter Phase 7 Plan

Changed:
- Added a dedicated Phase 7 animated model adapter plan covering scene actor conversion, skeleton/joint bindings, animator state, skinned mesh bridge components, scheduler integration, diagnostics, resource ownership, and fixture tests.
- Expanded the scene/component roadmap milestone to link the detailed plan and preserve existing `AnimatedModel`, cache, async, renderer skinned mesh, and App animated sample ownership during migration.

Rationale:
- Animated assets need a scene-component adapter path that reuses proven animation runtime behavior before root motion, gameplay movement, serialization, scripting, or editor-facing animation systems are introduced.

## 2026-06-19 - Animated Model Adapter Phase 7 Implementation

Changed:
- Added shared animated pose helpers used by the existing `AnimatedModel` runtime and the new scene animated adapter for bind poses, clip sampling, playback advancement, joint palettes, and skinned vertex packing.
- Added `Engine::SceneAnimatedModelAdapter` for converting already-imported animated scene CPU data into scene actors, skeleton bindings, animator state, adapter-owned renderer resources, and scene render bridge skinned mesh components.
- Added isolated animated scene adapter tests covering actor/hierarchy/skeleton/component/resource creation, pose parity with shared animation helpers, playback/crossfade updates, bridge sync, scheduler ordering, invalid references, cache payload adaptation, and deterministic release.

Rationale:
- Animated content now has a parallel scene-runtime adapter path without changing the existing `AnimatedModel`, cache, async, renderer skinned mesh, or App animated sample owners.

## 2026-06-19 - Release KayKit Animated Scene Adapter Sample

Changed:
- Updated the release authored sample path to load the KayKit Adventurers `Knight.glb` character through `SceneAnimatedModelAdapter`, attach it to the release scene runtime, and sync skinned instances through `SceneRenderBridge`.
- Kept the debug animated sample on the existing async `AnimatedModel` runtime so cache and diagnostic coverage remains unchanged.

Rationale:
- Release builds should exercise both the static authored scene adapter and the animated model scene adapter in the visible sample without migrating the debug animation tool path yet.

## 2026-06-19 - Release KayKit Sample Visibility

Changed:
- Moved the release KayKit scene-adapter animated sample above the authored scene bounds and applied a bounded scale factor based on the authored scene radius.
- Switched the default release KayKit sample from the character-only `Knight.glb` to `Rig_Medium_MovementBasic.glb`, which contains renderable skinned meshes and animation clips.

Rationale:
- The release animated sample should be plainly visible when Sponza is the authored scene instead of appearing at floor height beside a much larger environment.

## 2026-06-19 - Skinned Vertex Joint Index Layout Fix

Changed:
- Updated the renderer skinned vertex layout so joint indices are supplied as float-converted `Uint8` attributes that match the current skinned mesh shader input.

Rationale:
- The scene bridge could create, cull, and submit skinned draw calls while the shader still interpreted joint indices incorrectly, leaving the animated sample effectively invisible.

## 2026-06-19 - Release KayKit Visibility Material Override

Changed:
- Made the release KayKit animated sample material override opaque, double-sided, and strongly emissive.
- Removed that forced material override after real skinned rendering became visible so imported material and texture data can drive the diagnostic sample again.
- Bound the KayKit knight texture atlas to the release animated rig materials because the movement animation GLB imports as a gray rig material without the character texture.

Rationale:
- The sample should remain visible while diagnosing skinned rendering even if the imported asset winding or scene lighting differs from the static authored scene.

## 2026-06-19 - Skinned Joint Indices Use Float Vertex Attributes

Changed:
- Switched `Renderer::SkinnedMeshVertex` joint indices from packed `Uint8` attributes to four float attributes and updated animated vertex packing to write float joint indices.

Rationale:
- The skinned shader consumes joint indices as `vec4`; matching the CPU vertex format to that shader contract removes driver/backend ambiguity that could produce submitted but invisible skinned meshes.

## 2026-06-19 - Release Animated-Only Diagnostic Runtime

Changed:
- Temporarily changed the release authored-mode sample to skip Sponza/static authored scene startup and initialize only the scene render bridge plus KayKit animated adapter sample.
- Kept debug authored scene behavior unchanged.

Rationale:
- Isolating the animated mesh removes static scene occlusion, scale, and draw-order noise while diagnosing why submitted KayKit skinned draws are not visible.

## 2026-06-19 - Animated Skin Mesh Binding Fallback

Changed:
- Added an animated adapter fallback that creates skinned scene components from `ImportedSceneSkin` mesh/node references when imported nodes do not expose mesh references directly.

Rationale:
- Some KayKit animated GLB layouts import valid skinned mesh resources and skins while leaving node mesh reference lists empty, which previously produced zero scene skinned components.

## 2026-06-19 - Animated Root Mesh Binding Diagnostic Fallback

Changed:
- Added a final animated adapter diagnostic fallback that attaches every valid skinned mesh resource to a root scene actor when no node or skin binding path creates skinned components.
- Expanded release KayKit adapter logging with invalid node, mesh, skin, joint, and material reference counters.

Rationale:
- The release KayKit diagnostic sample imported renderer skinned meshes but still produced zero scene skinned components, so the runtime needs a forced binding path to exercise the bridge and renderer while import binding data is investigated.

## 2026-06-19 - Release Animated-Only Bridge Scene Reference Fix

Changed:
- Changed release animated-only startup to initialize `SceneRenderBridge` after `AuthoredRuntime` is in its final storage instead of returning a runtime that already contains a bridge referencing its internal scene.

Rationale:
- Moving a runtime after creating the bridge left the bridge referencing the moved-from scene, so the animated adapter saw valid actors while the bridge rejected those same actor handles.

## 2026-06-19 - Release KayKit Root Skinned Attachment Diagnostic

Changed:
- Added an animated adapter setting to attach skinned mesh components to the imported root actor instead of each mesh node.
- Enabled that setting only for the release KayKit diagnostic sample.

Rationale:
- The KayKit skinned draw path was live and submitted but invisible, consistent with a transform-space mismatch between glTF skinning palettes and per-mesh-node instance transforms.

## 2026-06-19 - Skinned Shader Raw Position Diagnostic

Changed:
- Temporarily changed the skinned mesh vertex shader to render raw vertex positions and normals without applying the joint palette.
- Regenerated the DX11 skinned vertex shader binary through the release build.

Rationale:
- The release KayKit sample now has live, visible, submitted skinned draw calls, so bypassing palette skinning isolates whether the invisibility is caused by skinning matrices or by lower-level vertex/material/draw state.

## 2026-06-19 - Release KayKit Bind-Relative Skin Palette

Changed:
- Restored the skinned vertex shader to apply joint palettes.
- Added a bind-relative animated pose palette helper and used it for the root-attached release KayKit diagnostic path.
- Corrected the bind-relative palette to use joint model-space deltas, so bind pose evaluates to identity in raw mesh space.

Rationale:
- The raw skinned mesh was visible when palette skinning was bypassed, proving the full joint-world palette was moving vertices out of the visible mesh space; the bind-relative palette keeps bind pose in raw mesh space while still allowing animation deltas.

## 2026-06-19 - Release KayKit Identity Skin Palette Diagnostic

Changed:
- Added an animated adapter setting that forces all skinned joint matrices to identity.
- Enabled the identity palette only for the release KayKit diagnostic sample.

Rationale:
- The raw-position shader test showed the model is visible, but sampled skinning palettes still move it out of view; forcing identity matrices through the real skinned shader isolates shader application from animation palette generation.

## 2026-06-19 - Skinned Shader Attribute Sanitization

Changed:
- Clamped skinned shader joint indices to the palette range and clamped weights before normalizing the skinning matrix blend.
- Kept identity fallback behavior when sanitized weights produce an empty influence set.

Rationale:
- The model was visible when the shader ignored joint attributes, but disappeared when the skinned shader read them, indicating unsafe or unexpected index/weight attributes could drive invalid palette access or NaN skinning results.

## 2026-06-19 - Skinned Shader Forced Identity Diagnostic

Changed:
- Temporarily changed `skinMatrix` to return identity before reading joint indices, weights, or palette uniforms.
- Regenerated the DX11 skinned vertex shader binary.

Rationale:
- The model still disappeared with identity palette data, so this isolates whether the shader vanishes because it reads joint attributes/palette state or because of another part of the skinned draw path.

## 2026-06-19 - Skinned Shader No-Index Diagnostic

Changed:
- Temporarily removed the skinned joint-index vertex attribute from the shader input and vertex layout.
- Changed the skinned shader to read weights only and use palette slot zero for all influenced vertices.
- Corrected the diagnostic to keep the index attribute in the vertex declaration so the GPU vertex stride still matches `Renderer::SkinnedMeshVertex`, while the shader ignores index values.

Rationale:
- The forced-identity shader made the model visible, so this isolates whether the `a_indices` stream binding is destabilizing the skinned shader path.

## 2026-06-19 - Reduced Skinned Palette Uniform Budget

Changed:
- Reduced the renderer skinned joint palette budget from 256 to 64 matrices and updated the skinned vertex shader uniform array to match.
- Restored real skinned shader joint-index blending against the reduced palette and disabled the release identity-palette diagnostic override.

Rationale:
- Literal identity skinning rendered, but reading `u_jointPalette[0]` did not; shrinking the matrix uniform payload avoids an oversized vertex uniform array while keeping enough joints for the KayKit diagnostic asset.

## 2026-06-19 - Per-Skin Skinned Palette Binding

Changed:
- Remapped imported vertex joint indices into each mesh skin's local joint palette slots during skinned vertex packing.
- Updated scene animated adapter skinned mesh descriptors and runtime pose updates to send per-skin palette subsets instead of the full imported joint palette.
- Updated animated adapter tests to assert renderer skinned mesh joint counts match the skin palette.

Rationale:
- The KayKit diagnostic rig contains multiple skinned mesh pieces; using one global palette for every component can make only part of the mesh deform correctly.

## 2026-06-19 - Restored Global Skinned Palette Binding

Changed:
- Reverted the per-skin joint remap diagnostic after it produced a T-pose and head deformation on the KayKit release sample.
- Restored imported vertex joint indices and full imported joint palette binding for scene animated adapter skinned components.
- Kept the confirmed 64-matrix skinned palette uniform budget and release texture/material path.

Rationale:
- The deformation showed the per-skin remap did not match the imported vertex joint-index space for this asset; the remaining animation issue needs to be debugged from the global palette path.

## 2026-06-19 - Restored Release Animated Node Attachment

Changed:
- Switched the release KayKit animated adapter sample back from root-attached skinned components to normal imported-node attachment.
- Kept the reduced skinned palette uniform budget, real joint palette updates, and release texture override.

Rationale:
- Root attachment was a visibility diagnostic from before the uniform-budget fix; using the imported node transforms again avoids mixing bind-relative palettes with the release scene hierarchy.

## 2026-06-19 - Removed Animated Visibility Diagnostics From Adapter Path

Changed:
- Removed the root-attachment and forced-identity palette diagnostic settings from `SceneAnimatedModelAdapter`.
- Removed the bind-relative palette helper that only existed for the root-attachment diagnostic path.
- Stopped binding unreferenced skinned meshes to `skin.nodeIndices`; unbound skinned resources now fall back to the imported scene root instead of a joint node.

Rationale:
- The KayKit release sample still showed T-pose/local deformation after visibility was restored, so the remaining debug-only binding paths needed to be removed from live adapter behavior before further pose debugging.

## 2026-06-19 - Scene Animated Palette Space Fix

Changed:
- Updated `SceneAnimatedModelAdapter` to send each skinned component a joint palette relative to that component actor's world transform.
- Added adapter coverage that moves the skinned component owner and verifies the backend receives `inverse(ownerWorld) * finalSkinningMatrix`.

Rationale:
- Scene render bridge applies an actor/world transform to skinned components, so the palette must be relative to that actor instead of using global joint matrices directly.

## 2026-06-19 - Skinned Attribute Semantic Alignment

Changed:
- Changed skinned shader varying semantics for joint indices and weights from spare texcoords to `BLENDINDICES` and `BLENDWEIGHT`.
- Rebuilt the DX11 shader binaries through the release `manual_engine` target.

Rationale:
- The renderer vertex declaration feeds bgfx `Indices` and `Weight` attributes, so the shader inputs should use matching semantics to avoid malformed joint data on the GPU.

## 2026-06-19 - Restored Release Sponza Plus KayKit Sample

Changed:
- Restored release authored mode to load the normal Sponza authored scene path.
- Kept the KayKit animated scene adapter sample running in the same scene runtime/render bridge after Sponza startup.
- Removed the temporary animated-only release runtime bypass and updated the release window title text.

Rationale:
- The animated-only mode was only needed to isolate skinned visibility; with animation rendering fixed, release should again show the authored Sponza sample plus the animated mesh.

## 2026-06-19 - Expanded Navigation Runtime API Roadmap

Changed:
- Expanded Milestone 8 in `docs/scene_component_roadmap.md` into a full Navigation Runtime API plan.
- Covered scene-friendly query APIs, agent/filter data, service ownership, compatibility with existing chunk navigation, diagnostics/debug requests, threading rules, tests, and deferred work.

Rationale:
- Navigation needs a stable public query surface before scene geometry, physics, character movement, scripting, or serialization start depending on it.

## 2026-06-19 - Navigation Runtime API Facade

Changed:
- Added `Engine::SceneNavigationService` as a scene-friendly facade over existing loaded-tile navigation queries.
- Added runtime query result, agent/filter, diagnostics, and debug request records for projection, local paths, reachability, conservative raycasts, and scene actor path starts.
- Added focused navigation runtime tests and documented the facade ownership contract.

Rationale:
- Scene, behavior, and future movement systems need a stable query API that does not expose Detour types or trigger synchronous tile generation.

## 2026-06-19 - Expanded Navigation Scene Geometry Roadmap

Changed:
- Expanded Milestone 9 in `docs/scene_component_roadmap.md` into a full Navigation Scene Geometry plan.
- Covered CPU scene nav source ownership, build snapshot flow, dirty tracking, diagnostics, tests, deferred work, and exit criteria.

Rationale:
- Scene-managed static and authored geometry needs a deterministic navigation build-input contract before navmesh generation can move beyond procedural terrain chunks.

## 2026-06-19 - Navigation Scene Geometry Registry

Changed:
- Added `Engine::SceneNavigationGeometryRegistry` for CPU-only static scene navigation sources, deterministic build snapshots, dirty chunk reporting, diagnostics, and debug request records.
- Added opt-in authored scene adapter navigation source registration from imported CPU mesh primitives.
- Added focused navigation scene geometry tests and documented the new ownership contract.

Rationale:
- Scene-authored static geometry needs a renderer-independent path into existing navigation tile build inputs before App streaming or automatic rebuild orchestration can consume it.

## 2026-06-19 - Phase 10 Physics Readiness Pass

Changed:
- Moved release authored scene adapter updates onto the `Scene` scheduler so animated adapter playback runs in `VariableAnimation` and render bridge sync runs in `PreRender`.
- Made animated adapter root-node skinned binding fallback an explicit setting and enabled it only for the release KayKit compatibility sample.
- Added scene-aware navigation source unregister dirty tracking for sources removed before their first snapshot.
- Documented that upcoming physics should be scene-owned and separate from the legacy procedural `BlockingCollisionSystem`.

Rationale:
- Physics needs explicit scene lifecycle and tick ordering before static colliders, bodies, queries, and transform sync are added.

## 2026-06-19 - Expanded Physics Integration Roadmap

Changed:
- Expanded Milestone 10 in `docs/scene_component_roadmap.md` into a full scene-owned physics implementation plan.
- Added `docs/scene_runtime/phase_10_physics_integration.md` covering public API shape, scheduler order, transform sync, collision geometry, queries, diagnostics, build integration, tests, deferred work, and acceptance checks.

Rationale:
- Physics needs a precise boundary before adding Jolt so scene-owned bodies, colliders, queries, and future character movement do not inherit procedural-world collision assumptions.

## 2026-06-19 - Scene Physics Phase 10 Implementation

Changed:
- Added Jolt through vcpkg and wired `Jolt::Jolt` into `manual_engine` and the new scene physics test target.
- Added `Engine::ScenePhysicsWorld` with generation-counted body/collider handles, static/kinematic/dynamic bodies, direct shape descriptors, fixed scheduler sync, queries, diagnostics, and debug request records.
- Added headless scene physics tests covering lifecycle, transforms, dynamic writeback, queries, filters, scheduler order, diagnostics, and shutdown.
- Updated contracts and overview docs to keep scene physics separate from procedural `World`, `ActorController`, and `BlockingCollisionSystem`.

Rationale:
- Scene-owned physics now has a stable Engine-facing surface for the upcoming character movement milestone without migrating existing procedural world behavior.

## 2026-06-19 - Terrain Rework Roadmap

Changed:
- Added `docs/terrain_rework_roadmap.md` as a dedicated plan for first-class heightmap import, chunked terrain ownership, generated asset caching, terrain material metadata, navigation build data, and physics collider integration.
- Linked the new terrain roadmap from the engine overview planning guidance.

Rationale:
- Heightmap terrain crosses assets, renderer LODs, navigation, physics, materials, caching, and future serialization, so it needs a dedicated roadmap before implementation starts.

## 2026-06-19 - T0 Heightmap Terrain Preflight

Changed:
- Added `docs/terrain_runtime/t0_heightmap_preflight.md` to lock the coordinate convention, validation asset role, fixture policy, T1 import inputs, chunking expectations, and source-versus-derived cache boundary before heightmap import implementation planning.
- Linked the T0 preflight from the terrain rework roadmap as the required preparation step before Phase T1.

Rationale:
- T1 heightmap import needs stable coordinate, chunk identity, fixture, and cache-boundary decisions before adding importer APIs or tests.

## 2026-06-19 - T1 Heightmap Terrain Import

Changed:
- Added a CPU-only heightmap importer for normalized 8-bit and 16-bit source samples.
- Added terrain heightmap import settings and chunk descriptor generation using the T0 north-up coordinate contract and world-size chunking.
- Added terrain-specific asset registry types for heightmaps, terrain sources, terrain material sets, and terrain chunks.
- Added focused heightmap import tests with generated tiny PNG fixtures.

Rationale:
- Heightmap terrain needs a renderer-independent source import and chunk descriptor layer before runtime terrain ownership, generated caches, renderer LODs, navigation data, physics colliders, or material rules are added.

## 2026-06-19 - T2 Runtime Terrain Dataset

Changed:
- Added `Engine::TerrainDataset` as a renderer-independent owner for terrain source records and loaded CPU chunks.
- Added generation-counted terrain source/chunk handles, imported and procedural chunk loading, shared height sampling/raycast/bounds/diagnostics queries, and generated-tile compatibility conversion.
- Added focused dataset tests covering handle invalidation, imported/procedural query paths, invalid no-ops, compatibility sampling, and renderer-stubbed legacy `TerrainSystem` comparison.

Rationale:
- Heightmap and procedural terrain need a shared CPU runtime ownership layer before generated caches, renderer LOD adapters, navigation data, physics colliders, material rules, or serialization are added.

## 2026-06-19 - T3 Terrain Derived Cache

Changed:
- Added `Engine::TerrainDerivedCache` for deterministic generated terrain chunk height payloads and renderer-independent LOD mesh payloads.
- Added YAML manifest plus binary payload storage, hash/magic/count validation, stale/corrupt status reporting, stats, and async queue wrappers.
- Added focused terrain cache tests for manifest identity, chunk and LOD round trips, stale/corrupt handling, async wrappers, and renderer-independent LOD payload generation.
- Updated terrain contracts and roadmap docs to describe the derived CPU cache boundary.

Rationale:
- Heightmap terrain needs reusable generated CPU payloads before render LOD, navigation, physics, material-weight, and future serialization phases consume terrain chunks at scale.
- Keeping cache records disposable and free of live renderer/navigation/physics handles preserves explicit runtime ownership.

## 2026-06-19 - T4 Terrain Render LOD Adapter

Changed:
- Added `Engine::TerrainRenderLodAdapter` as the worker-safe boundary for building renderer terrain LOD mesh data from immutable terrain chunk snapshots.
- Added support for both `TerrainDataset` chunks and legacy `TerrainSystem` tile inputs, plus conversion between T3 renderer-independent LOD cache payloads and current renderer mesh data.
- Migrated the App async terrain LOD worker path to the adapter with read-only derived-cache probes and new cache/generation debug counters.
- Added focused adapter tests covering dataset builds, parity with the old build path, cache hit/miss/stale/corrupt fallback, LOD counts, bounds, and stale commit rejection.

Rationale:
- Terrain render LOD generation now has a narrow bridge between the new dataset/cache model and the existing renderer commit path, allowing future heightmap runtime integration without replacing `TerrainSystem` storage in one step.
- Runtime cache reads can reduce worker mesh generation cost without adding surprise frame-time disk writes.

## 2026-06-19 - T5 Terrain Navigation Adapter

Changed:
- Added `Engine::TerrainNavigationAdapter` for deterministic `NavigationTerrainBuildData` generation from `TerrainDataset` chunks, generated terrain tiles, and legacy `TerrainSystem` tiles.
- Migrated App terrain navigation build-data creation through the adapter while preserving blocker append, nav cache async flow, live tile insertion, connectivity, graph routing, and chunk streaming ownership.
- Extended navigation cache manifest identity with terrain source/import identity and terrain navigation adapter version.
- Added focused terrain navigation adapter tests covering dataset/procedural/legacy paths, parity with existing terrain helpers, Detour tile build compatibility, invalid inputs, blocker boundaries, and cache identity changes.

Rationale:
- Navigation needs a terrain-source-aware adapter before heightmap chunks can feed navmesh generation without coupling Recast builds to renderer terrain storage.
- Including terrain identity in cache manifests prevents heightmap or terrain import changes from reusing stale procedural nav tile bytes.

## 2026-06-19 - T6 Terrain Physics Collider Adapter

Changed:
- Added `Engine::TerrainPhysicsColliderAdapter` for deterministic static triangle mesh collider payload generation from `TerrainDataset` chunks, generated terrain tiles, and legacy `TerrainSystem` tiles.
- Added explicit adapter-owned terrain collider bindings that create and release one static `ScenePhysicsWorld` body/collider per terrain chunk through caller-owned calls.
- Added separate dirty collider chunk tracking for geometry, settings, enable-state, and removal reasons.
- Added focused terrain physics collider adapter tests for dataset/procedural/legacy paths, invalid inputs, scene physics query hits, idempotent cleanup, and chunk-scoped dirty tracking.

Rationale:
- Terrain physics needs an explicit CPU bridge from authoritative height chunks to scene physics without deriving gameplay collision from renderer LOD meshes.
- Keeping App gameplay and `BlockingCollisionSystem` unchanged lets terrain collider work proceed without migrating the procedural world loop.

## 2026-06-19 - T7 Terrain Material Metadata

Changed:
- Added `Engine::TerrainMaterialMetadata` for in-memory terrain material sets, PBR layer texture metadata, stable layer/rule IDs, validation, and CPU rule evaluation.
- Added `AssetRegistry` dependency registration for terrain material texture references without renderer texture allocation or `AssetCache` involvement.
- Added deterministic terrain chunk coverage evaluation from authoritative CPU heights, CPU-derived slope, height ranges, and world-position bands.
- Added focused terrain material metadata tests covering validation, texture dependency deduplication, fallback/height/slope/world-position rules, falloff normalization, and invalid dataset handles.

Rationale:
- Terrain needs stable material metadata and diagnostic CPU rule evaluation before shader-layer blending or material resource creation can be designed safely.
- Keeping T7 renderer-free prevents material rules from depending on render LOD normals or live renderer handles.

## 2026-06-19 - T8 Layered Terrain Rendering

Changed:
- Added renderer-owned terrain material-set handles, descriptors, diagnostics, tile assignment APIs, draw stats, and a dedicated first-pass terrain shader path.
- Added `Engine::TerrainMaterialRenderAdapter` to convert T7 material metadata into renderer terrain material-set resources through `AssetCache`, with idempotent release.
- Updated the procedural App terrain sample to create one shared layered material set for biome fallback, slope rock, highland, and world-position soil rules while preserving per-tile biome materials as fallback.
- Added focused layered terrain rendering tests using renderer stubs for material-set lifecycle, fallback/layered terrain stats, adapter mapping/release, and shader source coverage.

Rationale:
- Terrain material rules now have a live renderer bridge without making T7 metadata depend on renderer handles.
- Keeping per-tile material handles as fallback lets the App sample migrate visually while preserving existing terrain ownership, LOD, navigation, physics, cache, and serialization boundaries.

## 2026-06-19 - T9 Terrain Serialization Preparation

Changed:
- Added `Engine::TerrainSerializationPrep` with stable terrain chunk identity strings/hashes, future chunk-file metadata, payload boundary flags, validation, deterministic payload file names, and derived-cache source hash bridging.
- Added `docs/terrain_runtime/t9_serialization_preparation.md` documenting durable identity, chunk metadata, payload boundaries, cache relationship, and non-goals.
- Added focused tests covering deterministic identity, runtime-handle exclusion, invalidation inputs, invalid metadata rejection, source-vs-derived payload boundaries, and derived cache identity behavior.

Rationale:
- Future terrain serialize/deserialize needs durable chunk identity and payload boundaries before file formats or App streaming can be designed.
- Keeping T9 metadata-only prevents serialization prep from changing live terrain ownership, renderer, navigation, physics, cache payload formats, or procedural saves.

## 2026-06-19 - Terrain Rework Roadmap Wrap-Up

Changed:
- Extracted the procedural sample layered terrain material preset into `Engine::TerrainSampleMaterials`, leaving App code to provide biome colors and own live renderer resource lifetime.
- Added focused test coverage for the sample material preset shape and validation.
- Updated the terrain roadmap with an initial-pass wrap-up and clarified current App terrain boundaries in the overview/contracts docs.

Rationale:
- Terrain material rule construction is reusable Engine metadata, not App orchestration.
- Marking T0 through T9 as the completed foundation makes the remaining heightmap streaming, editing, serialization, and ownership migration work easier to plan as separate follow-up scopes.

## 2026-06-19 - Scene Roadmap Terrain Alignment

Changed:
- Updated the scene component roadmap to account for the terrain rework adapters now available to scene navigation, scene physics, and future serialization.
- Added a 10.5 terrain runtime alignment checkpoint before character movement so terrain collider/navmesh/material/serialization boundaries are explicit.
- Clarified that scene systems should use `TerrainNavigationAdapter`, `TerrainPhysicsColliderAdapter`, and `TerrainSerializationPrep` instead of reaching into renderer terrain handles or legacy procedural terrain internals.

Rationale:
- Character movement and serialization planning need to know that terrain integration is explicit and adapter-driven, not automatic side effects of terrain rendering or legacy `TerrainSystem` ownership.

## 2026-06-19 - Phase 10.5 Scene Terrain Alignment Tests

Changed:
- Added `manual_engine_scene_terrain_alignment_tests` to prove terrain dataset chunks explicitly compose with `TerrainNavigationAdapter`, `SceneNavigationService`, `TerrainPhysicsColliderAdapter`, `ScenePhysicsWorld`, and `TerrainSerializationPrep`.
- Added coverage that terrain dataset/request creation alone does not create nav tiles, scene physics bodies, renderer resources, or serialized chunk files.
- Updated scene roadmap and system contracts to mark the terrain alignment checkpoint implemented and clarify adapter-only terrain handoff rules.

Rationale:
- Character movement can now be planned against tested scene navigation and scene physics terrain handoffs without assuming automatic terrain side effects or legacy `TerrainSystem` internals.

## 2026-06-19 - Phase 11 Character Movement Roadmap Expansion

Changed:
- Expanded the scene component roadmap's Phase 11 section into a full character movement implementation plan.
- Documented the planned scene character movement API shape, kinematic capsule physics behavior, path-following integration, terrain alignment boundaries, and required headless tests.
- Clarified that Phase 11 should not replace procedural `ActorController`, `BlockingCollisionSystem`, App command handling, or automatic terrain/nav generation.

Rationale:
- Character movement is the first gameplay system that depends on scene physics, scene navigation, and explicit terrain handoffs, so its roadmap needs concrete ownership and test boundaries before implementation.

## 2026-06-19 - Phase 11 Scene Character Movement

Changed:
- Added `Engine::SceneCharacterMovementSystem` with generation-counted character handles, kinematic capsule physics ownership, fixed-step movement, grounding, collision sweeps, step probes, path following, diagnostics, and debug records.
- Added `manual_engine_scene_character_movement_tests` covering lifecycle, invalid descriptors, physics ownership, flat and terrain grounding, blocker movement, path following, scheduler behavior, and diagnostics.
- Updated scene roadmap and system contracts to document the kinematic-only character movement boundary.

Rationale:
- Scene gameplay now has a headless character controller built on the scene physics and navigation services without migrating App commands or procedural actor movement.

## 2026-06-19 - Phase 12 Reflection And Opaque APIs Roadmap Expansion

Changed:
- Expanded the scene component roadmap's Phase 12 section into a full reflection and opaque API plan.
- Documented runtime opaque handle boundaries, reflected metadata/value access, subsystem adapter scope, validation rules, implementation subphases, and required tests.
- Clarified that Phase 12 prepares editor, serialization, native hook, and Lua access without adding those systems or exposing subsystem storage.

Rationale:
- Serialization and scripting need a typed access layer that preserves scene, render bridge, physics, navigation, asset, character, and terrain invariants while keeping transient runtime handles separate from durable IDs.

## 2026-06-19 - Phase 12 Reflection And Opaque APIs

Changed:
- Added `Engine::ReflectionRegistry`, typed reflected values/descriptors, reflection statuses, and generation-counted `OpaqueHandle` runtime access tokens.
- Added `Engine::SceneReflection` adapters for approved scene actor, component, render bridge, physics, character movement, asset, terrain, and limited navigation metadata access.
- Added narrow render bridge descriptor getters and scene physics collider descriptor reads needed for validated reflection without exposing internal storage.
- Added `manual_engine_scene_reflection_tests` covering deterministic metadata, opaque handle validation, reflected scene transform writes, render/physics/character writes through public APIs, and stable asset/terrain identity exposure.
- Updated system contracts, overview, and the scene roadmap to document the reflection boundary and runtime-handle serialization rule.

Rationale:
- Future serialization, native hooks, and scripting need a typed inspection/mutation layer that preserves subsystem invariants and keeps transient handles separate from durable scene, asset, and terrain IDs.

## 2026-06-19 - Phase 13 Serialization Roadmap Expansion

Changed:
- Expanded Phase 13 into a full binary scene serialization plan with a fixed header, chunk directory, schema/version metadata, durable identity rules, validation, streaming preparation, staged implementation phases, and tests.
- Documented the fallback path where Phase 13 can first land header/directory validation and in-memory snapshot round trips before full disk payload streaming.
- Clarified that serialization must use `SceneObjectId`, `AssetId`, and terrain durable chunk metadata while rejecting runtime handles and `OpaqueHandle` values.

Rationale:
- Scene persistence needs a binary format shaped for future streaming, but the first implementation can remain incremental if full custom binary save/load is too large for one pass.

## 2026-06-19 - Phase 13 Scene Serialization

Changed:
- Added `Engine::SceneSerialization` with a little-endian binary scene header, chunk directory, FNV-1a checksums, header-only inspection, whole-file write/read, snapshot validation, and core scene restore.
- Serialized core `Scene` actors by `SceneObjectId`, local transform, hierarchy parent ID, and metadata-only component type/owner records while keeping runtime handles out of payloads.
- Recorded reflection schema metadata, `AssetId` references, and `TerrainSerializationPrep` durable terrain references without creating renderer, physics, navigation, terrain, authored/animated, App, or asset-cache resources on load.
- Added `manual_engine_scene_serialization_tests` for header/directory handling, corruption checks, deterministic bytes, core scene roundtrip, asset/terrain durable identity, and validation-before-mutation behavior.
- Updated system contracts, engine overview, and the scene roadmap to document the Phase 13 binary serialization boundary.

Rationale:
- Scene persistence now has a tested binary foundation and core actor/component roundtrip that is streaming-ready by layout while leaving live subsystem reconstruction for later explicit phases.

## 2026-06-19 - Scene Serialization Conventions Documentation

Changed:
- Added `docs/scene_runtime/serialization_conventions.md` with binary container rules, durable identity choices, runtime-handle exclusions, deterministic output expectations, validation-before-mutation rules, reflection schema guidance, and subsystem extension hints.
- Linked the conventions from the scene runtime index, scene component roadmap, system contracts, engine overview, terrain serialization prep, terrain rework roadmap, authored scene roadmap, and animation roadmap.

Rationale:
- Future serialization work needs one concise checklist so render, physics, navigation, terrain, authored, animated, scripting, and editor records extend the binary scene format without leaking transient runtime handles or live subsystem ownership.

## 2026-06-19 - Phase 14 Native Behavior Hooks Roadmap Expansion

Changed:
- Expanded Phase 14 in `docs/scene_component_roadmap.md` into a full native behavior hook plan covering behavior type IDs, runtime handles, descriptors, hook contexts, scheduler integration, actor/component bindings, reflection/opaque access, diagnostics, serialization boundaries, implementation subphases, and tests.
- Clarified that native hooks are synchronous scene-scheduler callbacks and must mutate runtime state only through public/reflection APIs.

Rationale:
- Native C++ gameplay behavior needs a constrained hook layer before Lua or broader App gameplay migration can safely build on scene actors and components.

## 2026-06-19 - Phase 14 Native Behavior Hooks

Changed:
- Added `Engine::SceneBehaviorHooks` with stable behavior type IDs, generation-counted runtime behavior handles, explicit scene/actor/component opaque target bindings, lifecycle/tick/property callbacks, reflected get/set helpers, diagnostics, and debug records.
- Integrated native behavior dispatch through one scene scheduler system so lifecycle and tick callbacks follow existing scene ordering, pause behavior, and mutation-safe handle snapshots.
- Added target validation, callback failure isolation, optional disable-on-failure, explicit property-change notification helpers, and deferred unregister behavior.
- Added `manual_engine_scene_behavior_hooks_tests` covering handle invalidation, scheduler ordering, disabled/paused behavior, actor/component target validation, reflection writes, property notifications, mutation during callbacks, failure diagnostics, and runtime-handle serialization boundaries.
- Updated system contracts, engine overview, and the scene roadmap to document the native behavior hook boundary.

Rationale:
- Scene gameplay now has a native C++ callback layer that can drive behavior through scheduler/reflection contracts before Lua or broader App gameplay migration is introduced.

## 2026-06-19 - Phase 15 Lua Scripting Roadmap Expansion

Changed:
- Expanded Phase 15 in `docs/scene_component_roadmap.md` into a full Lua scripting plan covering VM ownership, script IDs/handles/descriptors, sandboxing, reflection and opaque-handle bindings, native hook integration, execution budgets, reload behavior, diagnostics, serialization boundaries, implementation subphases, and tests.
- Clarified that Lua scripts run synchronously through native hook ordering and may mutate scene state only through reflected/public Engine APIs.

Rationale:
- Lua scripting should build on the proven reflection, serialization, and native hook boundaries rather than exposing subsystem storage or App-local gameplay state.

## 2026-06-19 - Phase 15 Lua Scripting

Changed:
- Added `Engine::SceneLuaRuntime` with generation-counted Lua script instance handles, inline/file script descriptors, sandboxed Lua VM ownership, per-script environments, reload preserve-on-failure behavior, diagnostics, and debug records.
- Routed Lua lifecycle/tick/property callbacks through `SceneBehaviorHooks`, with reflected `engine.get`, `engine.set`, `engine.set_and_notify`, target validation, self-disable, self-unregister, and instruction-budget failure handling.
- Added Lua value conversion for approved reflected primitives, vectors, quaternions, matrices, stable IDs, terrain chunk IDs, and opaque handles while keeping Lua registry refs, closures, VM state, and runtime handles unserialized.
- Added `manual_engine_scene_lua_scripting_tests` covering VM cleanup, scheduler integration, reflected actor transform writes, property notifications, malformed values, error isolation, instruction budgets, reload behavior, sandboxing, and runtime identity boundaries.
- Added the vcpkg `lua` dependency, pinned to Lua 5.4.8 because the current Lua 5.5 vcpkg port requires a newer internal CMake than this Visual Studio vcpkg bootstrap provides.
- Updated system contracts, engine overview, and the scene roadmap to document the Lua scripting boundary.

Rationale:
- Scene gameplay now has a constrained Lua layer that reuses the native hook/reflection/opaque-handle contract without exposing subsystem storage, renderer/bgfx, Jolt, Detour, App state, or serialization payloads.

## 2026-06-19 - Phase 16 Debug Visualization Roadmap Expansion

Changed:
- Expanded Phase 16 in `docs/scene_component_roadmap.md` into a full debug visualization plan covering collector ownership, category/severity models, primitive/report request types, budgets, clipping, renderer/App consumption, subsystem producer targets, release/no-op behavior, serialization boundaries, implementation phases, and tests.
- Clarified that debug visualization should be renderer-independent Engine diagnostic data consumed by App/Renderer composition, not a simulation dependency or a serialized payload.

Rationale:
- Scene, terrain, navigation, physics, animation, Lua, and asset diagnostics now need one bounded debug request contract so future visualization does not keep growing as ad hoc per-subsystem vectors and App-only wiring.

## 2026-06-19 - Phase 16 Debug Visualization

Changed:
- Added `Engine::DebugVisualizationCollector` with renderer-independent debug categories, severities, primitive/report request records, per-category/global budgets, distance clipping, deterministic snapshots, and diagnostics.
- Added line, polyline, AABB, sphere, capsule, transform-axis, label, and report-row request helpers plus a renderer-line conversion helper for line and AABB output.
- Added `manual_engine_debug_visualization_tests` covering disabled/no-op behavior, category filtering, budgets, clipping, deterministic ordering, report rows, plain-data request records, renderer-line conversion, and clear/reset behavior.
- Added the debug visualization module to `manual_engine` and updated system contracts, engine overview, and the scene roadmap.

Rationale:
- Debug visualization now has a bounded Engine request boundary that future scene, terrain, navigation, physics, animation, asset, behavior, and Lua producers can use without depending on ImGui, bgfx, App-local state, or serialized payloads.

## 2026-06-19 - Final Scene Roadmap Migration Cleanup

Changed:
- Removed the Renderer type dependency from the public `Engine::DebugVisualization` header and replaced renderer-line conversion with plain Engine expanded debug line output for line, AABB, sphere, capsule, and transform-axis requests.
- Routed procedural App debug primitive generation through `DebugVisualizationCollector` before final Renderer submission, leaving Renderer as the draw consumer rather than the category/budget owner.
- Added `Engine::SceneWorldMigrationBridge` as a transitional runtime-only bridge from `WorldObjectHandle` to `SceneActorHandle`, optional scene character bindings, optional static scene-physics box colliders, and bidirectional transform sync.
- Added an opt-in experimental procedural scene-character movement mode in the App debug panel while preserving legacy `ActorController`/`BlockingCollisionSystem` as the default runtime.
- Added `manual_engine_scene_world_migration_tests` and expanded debug visualization tests for renderer-independent headers and geometric line expansion.

Rationale:
- The scene roadmap needed a final closure pass that unified debug visualization ownership and created a safe parallel path for procedural gameplay migration without changing current default behavior.

## 2026-06-19 - Modern Default Scene Startup

Changed:
- Switched Debug and Release startup defaults to a modern scene-backed runtime using `Engine::Scene`, scene scheduler systems, scene render bridge, scene authored/animated adapters, `TerrainDataset`, terrain render/navigation/physics adapters, `ScenePhysicsWorld`, `SceneNavigationService`, and `SceneCharacterMovementSystem`.
- Kept the legacy procedural and legacy authored App paths available only through explicit compatibility scene selections.
- Added a strict modern terrain startup path that imports the real heightmap asset into a bounded visible `TerrainDataset` patch, falls back to procedural dataset chunks when needed, renders terrain through `TerrainRenderLodAdapter` plus direct renderer terrain handles, and explicitly creates nav tiles and scene physics terrain colliders.
- Added modern default glTF/FBX coverage with Sponza/KayKit static assets and KayKit skinned animated samples through the scene adapters.
- Updated system contracts, engine overview, and the scene roadmap to document the new default ownership boundary.

Rationale:
- The default samples now exercise the modern scene, terrain, physics, navigation, render bridge, and animation composition path directly instead of validating it only through opt-in or release-only paths.

## 2026-06-20 - Cached Modern Scene Navigation Tiles

Changed:
- Added per-tile scene navigation geometry filtering by padded world-space triangle bounds and configurable walkable slope while preserving terrain base build data.
- Extended navigation cache manifests with scene-geometry source identity, slope threshold, tile padding, and generator version fields.
- Updated modern default startup navigation to read/write cached Detour tile bytes for terrain plus filtered static scene geometry and report cache hit/miss/stale/write counts.

Rationale:
- Modern startup should not rebuild all Sponza/static scene navigation geometry every launch, and Recast should only see triangles that can affect the requested tile and pass the configured walkable-slope policy.

## 2026-06-20 - Modern Debug UI Cleanup

Changed:
- Replaced the public Renderer debug UI data surface with modern runtime aggregates for performance, render controls, scene scheduler/render bridge diagnostics, `TerrainDataset`, modern navigation/cache/filtering, scene physics, scene character movement, and debug visualization.
- Removed legacy-only debug UI structs and visible tabs for `World`, `TerrainSystem`, `ChunkStreamer`, `ActorController`, `BlockingCollisionSystem`, biome editing, picking, groups, world graph routing, and legacy save/edit controls.
- Routed modern debug draw enqueue through modern scene/terrain/navigation/physics/character producers, kept debug draw out of release builds, and wired modern navigation cache clear/rebuild actions.
- Added header-boundary coverage ensuring public Debug UI headers expose modern state and no legacy-only UI names.

Rationale:
- The default runtime is now modern scene-backed, so debug UI should target modern diagnostics and public service APIs instead of keeping stale legacy control surfaces that no longer affect the active scene.

## 2026-06-20 - Open World Streaming Roadmap

Changed:
- Added `docs/open_world_streaming_roadmap.md` covering halo-based residency, async read/generate/promote/demote queues, full-heightmap bake expectations, terrain/nav/physics/scene/asset streaming responsibilities, debug counters, and phased implementation.
- Linked the streaming roadmap from the engine overview, scene component roadmap, and system contracts.

Rationale:
- Full-heightmap worlds need baked navmesh/collision data, asset dependency residency, and budgeted async queue transitions before the modern default scene can scale beyond a small startup patch without blocking the main thread.

## 2026-06-20 - Phase S0 Streaming Contract And Instrumentation

Changed:
- Added `Engine::OpenWorldStreaming` with stable residency states, dirty flags, transition lanes, payload kinds, display-name helpers, diagnostics counters, live-resource counters, cache counters, and empty diagnostics snapshots.
- Added a modern Debug UI Streaming tab with inert queue/timing/cache/resource placeholders fed by no-op diagnostics.
- Added focused streaming contract tests and extended Debug UI header coverage for the streaming placeholder.
- Updated the open-world streaming roadmap, system contracts, and engine overview to document that S0 is instrumentation only.

Rationale:
- Later streaming phases need stable terminology, counters, and presentation surfaces before adding halo planning, async IO, cache generation, or live-system promotion.

## 2026-06-20 - Phase S1 Chunk Manifest And Halo Planner

Changed:
- Extended `Engine::OpenWorldStreaming` with durable in-memory chunk manifest records, stable terrain/scene/asset chunk keys, per-payload active/cache halo policies, and deterministic halo planning.
- Added XZ bounds-distance residency decisions, hysteresis retention, per-payload transition caps, stable manifest record hashing, and planner diagnostics for manifest scan counts, desired residency, skipped records, transition candidates, and limited transitions.
- Expanded the modern Debug UI Streaming tab to show S1 manifest and planner counters while keeping App runtime behavior inert.
- Extended streaming tests for durable identity, active/cache/cold selection, hysteresis, transition caps, independent payload radii, invalid inputs, large metadata scans, and header dependency boundaries.

Rationale:
- Later async streaming phases need a deterministic desired-residency plan before they enqueue disk reads, generation jobs, live-system promotion, demotion, or cache writes.

## 2026-06-20 - Phase S2 Async Read/Decode Cache Halo

Changed:
- Added `Engine::OpenWorldStreamingCacheHalo` as a transient cache-halo coordinator over `AsyncWorkQueue`, S1 halo decisions, and durable streaming keys.
- Added streaming read descriptors, read requests, read results, cached CPU payload variants, descriptor sidecar tables, and snapshot/debug records for terrain chunk cache payloads, navigation tile cache bytes, metadata-only assets, and unsupported future payloads.
- Reused `TerrainDerivedCache::readChunk` and `NavigationCache::readTileCache` through worker jobs without introducing new cache formats or live-system promotion.
- Extended streaming diagnostics and Debug UI stats with pending reads, cached CPU payloads, stale completions, unsupported reads, read queue counters, bytes read, and last read failures.
- Expanded streaming tests for queued reads, duplicate desired plans, cache hits, failure statuses, stale/cancelled completions, queue/merge caps, descriptor helpers, and header dependency boundaries.

Rationale:
- Open-world streaming needs a cache-halo stage that can warm CPU payloads asynchronously before later phases decide whether to generate missing data or promote cached data into renderer, navigation, physics, scene, or asset systems.

## 2026-06-20 - Phase S3 Budgeted Main-Thread Promotion/Demotion

Changed:
- Added `Engine::OpenWorldStreamingLiveHalo` as a transient live-halo coordinator over S1 halo decisions, S2 cached CPU payloads, and budgeted `MainThreadWorkQueue` callbacks.
- Added promotion/demotion request, result, status, callback, runtime-token, snapshot, and diagnostics types while keeping the streaming core independent from live renderer, navigation, physics, scene, App, and serialization handles.
- Extended the modern Debug UI streaming stats with pending promote/demote, live payload, stale live completion, and promotion/demotion failure counters.
- Extended streaming tests for live promotion, demotion, duplicate suppression, missing payloads, callback failures, queue caps, stale queued work, and debug header coverage.

Rationale:
- Active-halo streaming needs a main-thread handoff layer before cached terrain, nav, physics, scene, or asset payloads can safely become live resources under a frame budget.

## 2026-06-20 - Phase S4 Full Heightmap Bake And Runtime Cache Use

Changed:
- Added `Engine::OpenWorldStreamingBake` to import a full heightmap, partition durable terrain chunks, write terrain chunk, render LOD, navigation tile, and terrain physics collider derived payload caches, and return S1 manifest records plus S2 read descriptors.
- Extended `TerrainDerivedCache` with renderer-independent terrain physics collider payload manifests, binary read/write helpers, and S2 read support for baked render LOD and physics collider payloads.
- Added `OpenWorldStreamingDerivedGenerationHalo` so runtime cache miss/refresh generation is explicit policy-driven worker work that returns plain cached payloads without touching live renderer, navigation, physics, scene, or App state.
- Extended streaming diagnostics and Debug UI stats with bake and derived-generation counters.
- Expanded open-world streaming tests for full-heightmap bake, baked payload readback, cache identity invalidation, and explicit generation-on-miss policy.

Rationale:
- Open-world streaming needs reusable baked terrain/nav/physics/render payloads and a non-blocking generation fallback before those payloads can be promoted into live systems around camera halos.

## 2026-06-20 - Phase S5 Asset Dependency Streaming

Changed:
- Added `Engine::OpenWorldStreamingAssets` with asset dependency descriptors, durable manifest/read-descriptor helpers, registry descriptor conversion, and metadata-only cache-halo records.
- Added `OpenWorldStreamingAssetResidency` as the S3 promotion/demotion adapter for `AssetCache`-owned static mesh and texture dependencies.
- Added shared live-reference tracking, deterministic release, missing required/optional asset diagnostics, unsupported type diagnostics, and asset streaming counters for the Debug UI Streaming tab.
- Extended open-world streaming tests for durable asset identity, metadata warming, live promotion/demotion, shared references, missing assets, unsupported asset types, and header boundary checks.

Rationale:
- Open-world streaming needs asset dependencies to move through the same cache/live halo model as terrain, nav, and physics data while keeping renderer resource acquisition explicit and main-thread-owned.

## 2026-06-20 - Phase S6 Scene Chunk Serialization Integration

Changed:
- Added scene chunk streaming payload support to the open-world cache halo using the existing Phase 13 scene binary format.
- Added Engine::OpenWorldStreamingSceneChunks for durable scene chunk manifest records, scene binary read descriptors, and main-thread scene actor/component promotion and demotion callbacks.
- Added explicit chunk-owned, global, and migratory actor ownership policy diagnostics without automatic bounds-based actor migration.
- Extended Streaming debug stats with scene chunk cache, promotion, demotion, actor/component, duplicate ID, invalid reference, and ownership counters.
- Added focused scene chunk streaming tests for binary readback, activation/demotion, duplicate stable IDs, and ownership policy behavior.

Rationale:
- Open-world streaming needs scene actor/component chunks to use the same cache/live halo model as terrain, nav, physics, and assets while preserving stable SceneObjectId identity and keeping transient scene handles out of durable payloads.


## 2026-06-20 - Phase S7 LOD And Multi-Halo Refinement

Changed:
- Added streaming key variant IDs so one durable chunk can carry independent records for LODs, metadata, texture-mip variants, and future behavior/animation/audio payloads.
- Added streaming halo profiles, per-profile policies, detail levels, priority bias, and profile-aware transition caps while preserving default S1-S6 behavior.
- Added predictive focus planning from velocity and goal/path hints; predictive records prefetch cache residency by default and do not request live promotion unless explicitly allowed.
- Extended Streaming diagnostics and Debug UI stats with profile, variant, high-detail, active-focus, predictive-focus, prefetch, and profile-limited transition counters.
- Extended open-world streaming tests for variant identity, terrain LOD profile decisions, far metadata/cache-only behavior, profile caps, and predictive prefetch priority.

Rationale:
- Open-world streaming needs multiple detail records per chunk before terrain LOD, far metadata, high-resolution assets, animation, behavior, and future audio/particle residency can be tuned independently without replacing the S1-S6 cache/live transition model.

## 2026-06-20 - Open World Streaming S8 Roadmap Completion Milestone

Changed:
- Added Phase S8 to `docs/open_world_streaming_roadmap.md` as the planned modern runtime streaming integration and roadmap closure milestone.
- Defined saved streaming build validation for the default scene, including source hashes, import settings, render LOD settings, nav settings, slope/tile filtering, physics collider settings, scene chunk schema, asset dependencies, and version strings.
- Documented the required rebuild behavior when no current heightmap-derived streaming build exists on disk or when a relevant version/source/settings identity changes.
- Extended first-usable streaming acceptance to require Debug and Release modern defaults to use the streaming runtime and to validate/rebuild saved build data before use.

Rationale:
- The streaming roadmap should not be considered complete until the modern default scene uses the S1-S7 streaming stack and can bootstrap missing or stale heightmap-derived streaming data without falling back to eager fixed-patch loading.

## 2026-06-20 - Phase S8 Modern Runtime Streaming Integration

Changed:
- Added `Engine::OpenWorldStreamingRuntime` as the S8 coordinator over saved-build validation, halo planning, async cache reads, optional generation, and budgeted live promotion/demotion.
- Added saved modern-default streaming build manifests under `generated/open_world_streaming/modern_default/manifest.yaml`, with fingerprint validation over heightmap source identity, import settings, render LOD settings, nav settings, slope/tile filtering, physics collider settings, and streaming version strings.
- Added a focused open-world streaming runtime test target covering missing-build rebuilds, saved-build reuse, setting invalidation, async cache reads, live callback promotion/demotion, and handle-free saved manifests.
- Wired the modern default scene to initialize the S8 runtime, rebuild/reuse heightmap-derived streaming data at startup, update streaming residency from camera focus each frame, and feed real streaming diagnostics into the Debug UI.

Rationale:
- The open-world streaming roadmap needs a concrete runtime closure point where baked heightmap-derived payloads become the default scene's residency source instead of remaining Engine-only planning/cache APIs.

## 2026-06-20 - Modern Adjacent Nav Tile Connectivity

Changed:
- Added terrain-independent `NavigationConnectivitySystem` rebuild/step paths that sample live `NavigationSystem` tile bounds and tile-local nearest-poly queries instead of requiring legacy `TerrainSystem`.
- Added portal height-delta validation, modern scene navigation portal route stitching through `SceneNavigationService::findPathAcrossLoadedTiles`, and modern Debug UI connectivity counters/portal debug lines.
- Wired the modern streaming nav tile promotion/demotion callbacks to refresh connectivity for the promoted tile and cardinal neighbors, or remove/relink connectivity when a tile leaves the live halo.
- Extended navigation and navigation runtime tests for terrain-free connectivity, missing neighbors, steep seam rejection, and cross-loaded-tile portal route stitching.

Rationale:
- Streamed open-world navigation needs explicit adjacent-tile seam connectivity that works independently of the legacy procedural terrain path and can degrade cleanly when neighbor tiles are missing.

## 2026-06-20 - Seamless Chunk Navigation Geometry

Changed:
- Added border-aware terrain navigation build settings and request fields so nav tiles can rasterize an expanded source sample apron while keeping the original durable tile coord and output tile bounds.
- Added dataset/imported-neighborhood terrain nav request helpers that sample adjacent chunks, clamp heightmap-edge apron samples deterministically, and report border padding/sample diagnostics.
- Updated full-heightmap bake, modern runtime navigation rebuilds, navigation cache identity, and saved streaming build fingerprints to include the border generator version and apron settings so old seam-gap tile bytes are stale.
- Preserved async/cache-first streaming behavior: S2 still reads cached tile bytes, S3/S8 still promote tile bytes on the main thread, and generation-on-miss uses the same border-aware request path.
- Extended terrain navigation and navigation runtime tests for border geometry, zero-padding compatibility, cache identity invalidation, and seam projection across adjacent tiles.

Rationale:
- Open-world terrain chunks should not leave narrow navmesh gaps at tile seams. Border-apron nav baking gives Recast neighbor context without changing terrain chunk ownership, renderer/physics payloads, nav tile identity, or streaming promotion flow.

## 2026-06-20 - Legacy Runtime Removal Pass

Changed:
- Removed the legacy procedural/runtime compatibility stack from active code, CMake targets, and tests, including procedural world ownership, actor controllers/commands/selection, blocking collision, chunk streaming, spatial registry, object archetypes, world save/override/editor paths, old terrain owner compatibility, legacy authored scene owners, legacy animated model/cache/async owners, and scene-world migration bridge code.
- Made App startup modern-only around the scene-backed default runtime, open-world streaming validation/rebuild, modern terrain dataset/adapters, scene navigation, scene physics, scene character movement, scene authored/animated adapters, and modern debug UI.
- Split shared animation pose/playback helpers into modern animation pose headers so `SceneAnimatedModelAdapter` no longer depends on the removed legacy animated owner.
- Removed graph-cache APIs tied to the deleted world graph path and kept modern navigation connectivity/path stitching as the adjacent-tile contract.
- Added an active-code static guard in debug visualization tests to reject removed legacy symbol names from `src`, `tests`, and `CMakeLists.txt`.
- Rewrote active engine overview and system contracts to describe the modern-only architecture.

Rationale:
- Future roadmap work should not have to distinguish modern systems from vestigial compatibility paths. Removing the old stack makes `Scene`, `TerrainDataset`, streaming, scene physics, scene character movement, and scene navigation the single active architecture.

## 2026-06-20 - Cursor Ray Navigation Targeting

Changed:
- Added `Engine::CursorTrace` to convert cursor screen coordinates plus the active view-projection matrix into renderer-independent world rays.
- Added a navigation projection helper that samples a cursor ray against loaded `SceneNavigationService` tiles without building or streaming navigation data.
- Updated the modern debug navigation action to arm a cursor path test, then use the next viewport click to path the character toward the cursor-projected navmesh point instead of projecting the camera position or the debug UI button position.
- Added debug draw for the exact cursor projection ray and the projected nav hit marker used by the request.
- Added navigation runtime tests for cursor ray construction, cursor-to-nav projection, and invalid cursor input handling.

Rationale:
- Click-to-move, actor selection, and future in-engine GUI hit testing need a proper screen-to-world cursor boundary. Treating the camera position as the click target made navigation tests fragile and unrelated to the actual cursor location.

## 2026-06-21 - Grounded Character Terrain Sweep Retry

Changed:
- Added transient physics query exclusions for one body/collider and used them in scene character movement follow-up sweeps.
- Grounded characters now track their current walkable triangle-mesh ground hit and retry horizontal blocking sweeps with that ground collider excluded when the first sweep starts against it.
- Added a regression test proving terrain mesh bounds no longer block grounded horizontal movement while a separate static wall still blocks the character.

Rationale:
- Accepted navigation paths could be drawn while character movement stayed still because coarse terrain triangle-mesh bounds were treated as horizontal blockers. Retrying only against the current walkable ground preserves normal blocking while allowing terrain path following.

## 2026-06-21 - Character Movement Terminal Diagnostics

Changed:
- Added Debug-build terminal logs for scene character path acceptance/rejection, active movement summaries, grounded/falling state, movement sweep hits, terrain-ground retry decisions, and final blockers.
- Added App-side click-to-navigation logs for cursor ray projection, projected nav target, player start position, accepted point count, and character status.

Rationale:
- The default scene can show a valid path trace while the character remains stationary. Terminal diagnostics make it possible to identify whether the stop happens at path request, grounding, desired direction, terrain retry, or a specific blocking collider.

## 2026-06-21 - Static Mesh Bounds Movement Retry

Changed:
- Extended grounded horizontal character movement to retry through a bounded list of zero-distance static triangle-mesh bounds hits, not only the tracked walkable ground mesh or first authored mesh.
- Added a focused regression where a grounded character starts inside overlapping non-ground static triangle mesh broad bounds and still moves, while box blockers remain blocking.

Rationale:
- Runtime diagnostics showed the default character was pinned by alternating authored static triangle mesh bounds hits with side normals before any real movement occurred.

## 2026-06-21 - Capsule Sweep Debug Bounds Follow Misses

Changed:
- Updated physics capsule sweep debug requests to place missed sweep bounds at the sweep end position instead of the default hit position.
- Added a scene physics regression covering missed sweep debug bounds placement.

Rationale:
- Character movement could advance while the visible debug AABB appeared stuck because successful, non-blocking sweeps recorded no hit position and drew their bounds at the origin.

## 2026-06-21 - Physics Collider Shape Debug Draw

Changed:
- Added transient scene-physics collider shape snapshots for live enabled bodies.
- Added a separate modern debug UI toggle and line cap for drawing live collider shapes as wireframes.
- Drew boxes, spheres, capsules, and static triangle mesh colliders from their actual shape data instead of generic AABBs.

Rationale:
- Physics setup and character collision debugging need to show the actual collider geometry, including capsule-shaped character bodies and terrain/static mesh triangle edges.

## 2026-06-21 - Editor Roadmap

Changed:
- Added `docs/editor_roadmap.md` covering a separate editor executable, durable editor project profiles, settings reflection, ImGui editor panels, rebuild orchestration, live apply, and future hook/Lua tool scripting.

Rationale:
- Editor-style workflows need a durable roadmap before implementation so build settings, reflection metadata, derived-data rebuilds, and runtime/editor boundaries stay explicit.

## 2026-06-21 - Editor Target Launch Boundary

Changed:
- Added a `manual_engine_editor` target with its own App entrypoint.
- Added a shared modern scene launch API so the sample and editor boot the same modern default scene stack.
- Labeled the editor window/debug scene mode separately without adding project profiles, reflected settings panels, or rebuild orchestration.

Rationale:
- E1 needs a separate editor executable that reuses the current runtime composition before later editor settings and rebuild workflows are layered on top.

## 2026-06-21 - Editor Project Settings Profile

Changed:
- Added App-side `EditorProjectSettings` load/save/validate helpers backed by yaml-cpp.
- Added `projects/default.editor.yaml` with modern-default terrain, cache, nav, physics, streaming, renderer debug, debug draw, and editor camera defaults.
- Wired `manual_engine_editor` to load the profile at startup and fall back to built-in defaults when the file is missing or invalid.

Rationale:
- E2 needs durable editor state that can drive saved-build identity and later reflected settings UI without serializing runtime handles or introducing rebuild orchestration yet.

## 2026-06-21 - Editor Settings Reflection

Changed:
- Added App-side `EditorSettingsReflection` descriptors and reflected get/set helpers for `EditorProjectSettings`.
- Reflected first-slice terrain, cache, render LOD, navigation, physics, streaming, renderer, debug draw, and camera settings with editor-visible metadata and explicit-apply flags.
- Added focused tests for descriptor coverage, indexed render LOD targets, representative reads/writes, validation rejection, and default metadata parity.

Rationale:
- E3 needs a metadata/access boundary that future editor panels can enumerate without hand-coded field wiring, while keeping profile mutation validated and separate from live runtime systems.

## 2026-06-21 - Editor UI Panels

Changed:
- Added App-side editor UI state, generated property rows, dirty summaries, and validated reflected edit helpers for `EditorProjectSettings`.
- Wired `manual_engine_editor` to show a separate Dear ImGui editor window with Project Settings, Rebuild, Diagnostics, and Runtime/Viewport tabs.
- Added focused headless tests for editor UI model initialization, reflected row generation, advanced filtering, dirty classification, invalid edit rejection, and indexed render LOD rows.

Rationale:
- E4 needs editable in-memory project settings and visible rebuild-required diagnostics before adding profile persistence, live apply, or rebuild orchestration.

## 2026-06-21 - Editor Rebuild Orchestrator

Changed:
- Added a public Engine API to explicitly rebuild saved open-world streaming builds through the existing full heightmap bake and saved-manifest write path.
- Added an App-side editor rebuild coordinator that computes dirty domains from reflected editor settings, runs saved-build rebuild commands, records diagnostics, and marks successful rebuilds as requiring runtime reload.
- Wired the editor Rebuild tab to terrain, render LOD, navigation, physics, full saved-build, and lightweight-profile apply commands without saving YAML or hot-swapping live runtime resources.
- Added focused coordinator tests covering dirty-domain classification, invalid/failed rebuild handling, successful saved-build writes, baseline advancement, and domain buttons using the full backend.

Rationale:
- E5 needs explicit rebuild controls and saved-build diagnostics before live runtime reload/apply behavior is added in the next phase.

## 2026-06-21 - Editor Live Apply And Streaming Reload

Changed:
- Added explicit editor live-apply commands for lightweight renderer/debug/camera settings, with validation before mutating the running editor viewport and baseline advancement only after success.
- Added a streaming-only reload path in App composition that preflights rebuilt saved manifests, releases streaming-owned terrain render/nav/physics/chunk resources, restarts pending streaming work, and reinitializes `OpenWorldStreamingRuntime` without restarting authored scene state.
- Added live-apply regression tests for lightweight apply, invalid validation failures, reload-required state, reload failure diagnostics, saved manifest validation, and manual sample target wiring.

Rationale:
- E6 needs editor changes to apply predictably through explicit validated commands while keeping profile YAML saving, partial rebuilds, and full-scene restart behavior out of scope.

## 2026-06-21 - Editor Hooks And Lua Tool Scripting

Changed:
- Added App-side editor tool hooks and a one-shot Lua tool scripting runtime that discovers scripts from `scripts/editor` and exposes only reflected editor settings plus validated editor commands.
- Added a Tool Scripts ImGui panel to rescan and run scripts, show status, logs, command diagnostics, and recent tool events without startup autorun or profile YAML persistence.
- Marked editor settings as script-visible while preserving read-only and explicit-apply metadata, and added a sample validation script plus focused tool scripting tests.

Rationale:
- E7 needs automation hooks for editor settings and rebuild workflows without reusing scene behavior Lua or exposing live Engine/Renderer/App storage to scripts.

## 2026-06-21 - Editor Roadmap Closure

Changed:
- Marked `docs/editor_roadmap.md` complete for the E1-E7 first editor vertical slice.
- Added completed capability, closure acceptance, and future-roadmap notes so remaining editor expansion is not implied as unfinished E1-E7 work.
- Ignored generated editor project settings test output so closure artifacts stay limited to committed profile, script, App source, and test files.

Rationale:
- The current editor roadmap has reached its intended first-slice scope; future editor expansion should be planned separately instead of leaving completed phases open-ended.

## 2026-06-21 - Actor Authoring Roadmap

Changed:
- Added `docs/actor_authoring_roadmap.md` covering editor-authored scene actors, typed component descriptors, behavior bindings, serialization/rebind, editor placement, streaming spawn/despawn semantics, and a future FLECS bridge.

Rationale:
- Actor placement and component-driven gameplay need their own roadmap on top of the completed editor foundation, with `Engine::Scene` remaining the actor authority and FLECS deferred as a later game-state mirror.

## 2026-06-21 - Actor Authoring Metadata

Changed:
- Added `Engine::ActorAuthoringStore` for durable actor display names, tags, and layer metadata keyed by `SceneObjectId`.
- Added actor-authoring reflection descriptors and get/set helpers for metadata inspection and validated mutation.
- Extended scene binary serialization with optional actor authoring metadata records and tests for round-trip, validation, determinism, and runtime-handle exclusion.

Rationale:
- Editor-authored actors need stable metadata before placement, component descriptors, behavior bindings, or future FLECS mirroring can safely build on top of `Engine::Scene`.

## 2026-06-21 - Typed Component Descriptor Registry

Changed:
- Added `Engine::ActorComponentDescriptorRegistry` and `Engine::ActorComponentDescriptorStore` for typed authored component metadata keyed by stable `ActorComponentId`.
- Added generic authored component reflection, deterministic store enumeration, and bind/unbind/apply callback contracts tested with fake component descriptors.
- Extended scene binary serialization with optional generic authored component metadata records and registry-aware validation.

Rationale:
- Actor authoring needs stable component instance identity and descriptor registration before concrete `Stats`, `Movement`, and `Sensory` component schemas can be safely added.
