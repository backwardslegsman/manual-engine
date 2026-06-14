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
