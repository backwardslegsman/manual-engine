# Recast + Detour Navigation Roadmap

This roadmap targets Kenshi-like near movement first: select or command an actor, click a nearby terrain destination, follow a navmesh path over procedural terrain, and expose enough debug tools to tune movement. After that local loop is stable, navigation expands to open-world scale with a coarse chunk/sector graph layered above loaded Detour tiles.

## Goal

- Use Recast + Detour for terrain-aware local pathfinding.
- Keep Recast/Detour hidden behind Engine-owned navigation interfaces.
- Keep actor vertical placement snapped to `TerrainSystem::sampleHeight`.
- Integrate with existing chunk streaming, terrain CPU data, object identity, debug draw, and interaction systems.
- Prioritize a working near-command loop first, then add hierarchical routing for long-distance travel without replacing local Detour pathing.

## Architecture Direction

Open-world navigation should use two layers:

- **Local layer:** loaded Recast/Detour tiles for nearby terrain, blockers, and precise path following.
- **Strategic layer:** a coarse world graph over chunks, sectors, portals, roads, passes, gates, and other durable travel connections.

Actors should not request one giant Detour path across the world. Long commands should become a route of coarse waypoints/portals, with the actor repeatedly asking the local layer for short paths to the next reachable target.

## Phase 1 - Navigation Backend Boundary

Status: boundary implemented. `Engine::NavigationSystem` is the only public owner of navigation runtime state, and Recast/Detour headers are confined to the implementation file.

Initial public concepts:
- `NavigationTileHandle`
- `NavAgentSettings`
- `NavBuildSettings`
- `NavPath`
- `NavQueryResult`
- `NavigationTerrainBuildData`

Initial API:
- `buildTerrainTile(const NavigationTerrainBuildData& buildData, const NavAgentSettings& agent)`
- `destroyTile(ChunkCoord coord)`
- `nearestNavigablePoint(glm::vec3 point, const NavAgentSettings& agent)`
- `findPath(glm::vec3 start, glm::vec3 end, const NavAgentSettings& agent)`
- `isNavigable(glm::vec3 point, const NavAgentSettings& agent)`
- `clear()`, `hasTile()`, `tileCount()`, and `settings()`

Implementation notes:
- Recast and Detour types stay private to `NavigationSystem`.
- App, actors, terrain, and persistence must not include Recast headers.
- Start with one agent configuration: player-sized humanoid.
- Phase 1 validates inputs and returns safe `Unsupported` or `NoTile` statuses until Phase 2 builds real navmesh tiles.

## Phase 2 - Terrain-Only Navmesh Tiles

Status: implemented as synchronous terrain-only tiles synchronized by App from loaded chunk terrain.

Build one navigation tile per loaded terrain chunk.

Input:
- CPU terrain tile vertices and triangles from `TerrainSystem`.
- Chunk coordinate as tile identity.
- Agent radius, height, max slope, max climb, cell size, and cell height.

V1 behavior:
- Build nav tiles synchronously when chunks load.
- Destroy nav tiles when chunks unload.
- Mark walkability from terrain slope.
- Do not bake props or dynamic obstacles yet.
- Keep actor Y from terrain height sampling, not from Detour polygon height.
- App owns synchronization between loaded chunks and navigation tiles for this phase.
- Nearest-point and path queries are available, but no actor path-following behavior consumes them yet.

Acceptance:
- Loaded chunks produce valid nav tiles.
- Unloaded chunks remove their nav tiles.
- A nearby terrain point can be snapped to the nearest navigable polygon.

## Phase 3 - Click-To-Move Player

Status: implemented for one player actor. Right-click release commands a terrain destination when the pointer did not drag beyond the click threshold; right-drag remains camera pan.

Added semantic input actions:

```yaml
player.set_destination: RightMouse
player.cancel_destination: Escape
```

Implemented flow:
- Existing terrain picking provides the clicked world position.
- App handles destination commands for the player actor after picking.
- Actor requests `NavigationSystem::findPath` and stores a lightweight path-following state.
- Actor follows path corners at its configured movement speed, rotates toward movement direction, and stays grounded from terrain height sampling.
- Existing kinematic collision remains active as a safety layer; repeated blocking fails the active path.
- WASD manual movement and Escape cancel active path following.

Acceptance:
- Right-clicking nearby terrain moves the player marker along a visible path.
- Unreachable destinations report a debug status instead of moving directly.
- Escape cancels the active destination.

## Phase 4 - Path-Following Actor State

Status: implemented as reusable actor path-following state, with the sample App still commanding only the player actor.

Implemented behavior:
- Actor path settings define arrival radius, corner advance radius, blocked tick limit, repath attempts, and repath distance threshold.
- Actor path state tracks current path, corner index, destination, blocked ticks, repath attempts used, last query status/message, and status.
- Path status distinguishes idle, pathing, moving, blocked/repathing, arrived, failed, and cancelled.
- Blocking collision during path movement can trigger one synchronous repath over the currently loaded terrain navmesh.
- Direct WASD movement remains player debug/manual steering and cancels active paths when enabled.

Acceptance:
- Actor arrives at nearby clicked destinations.
- Actor stops cleanly at the end of the path.
- Actor reports failed pathing without corrupting movement state.

## Phase 5 - Navigation Debug Draw

Status: implemented for terrain-only navigation debug visualization and manual tuning.

Renderer debug primitives cover:
- nav tile bounds
- navmesh polygon edges
- current path corridor/corners
- current target corner
- destination marker
- nearest navigable point
- failed query reason in Dear ImGui

Dear ImGui controls:
- show navmesh
- show current path
- rebuild visible nav tiles
- agent radius
- agent height
- max slope
- max climb
- Recast cell size/cell height

Implementation notes:
- `NavigationSystem` exposes plain debug line geometry while keeping Detour/Recast private.
- Agent setting changes affect new queries immediately.
- Recast build setting changes apply only after manually rebuilding visible nav tiles.
- Rebuilding visible nav tiles cancels the active player path.

Acceptance:
- Debug draw makes path, tile, and failure state visible.
- Agent settings can be tuned live and visible tiles rebuilt.

## Phase 6 - Static Prop Blocking

Status: implemented with conservative AABB blockers baked into synchronous terrain nav tile generation.

Implemented V1 approach:
- Blocking archetypes contribute conservative AABB geometry to the owning chunk nav build.
- Terrain triangles are marked walkable by slope; blocker triangles remain unwalkable Recast areas.
- Loaded chunks build nav tiles from CPU terrain plus currently spawned blocking objects.
- Removing, placing, or moving a blocking persistent object rebuilds or reloads affected loaded chunk nav.
- Active player paths are cancelled after nav tile rebuilds that may invalidate polygon data.
- Debug UI reports blocker geometry counts, selected-object blocking state, and last rebuilt nav chunk.

Avoid in V1:
- DetourTileCache obstacle carving.
- Dynamic obstacle avoidance.
- Crowd simulation.

Acceptance:
- Removed trees/rocks no longer block regenerated nav.
- Placed markers can block nav after chunk rebuild.
- Moving blocking objects updates the owning chunk nav tile.

## Phase 7 - Multi-Selection Movement

Status: implemented as RTS-style drag-box selection and independent per-actor path commands for a small demo squad.

V1 behavior:
- The sample App spawns the player plus three demo actor markers.
- Left-drag selects actors in screen space without replacing object debug picking.
- Right-click terrain commands selected actors; when no actors are selected, the player-only command path remains active.
- Each commanded actor gets a deterministic formation offset around the clicked point.
- Formation destinations are snapped to the nearest loaded navmesh point before pathing.
- Each actor requests an independent path through `ActorController` and `NavigationSystem`.
- Existing kinematic collision remains the local safety layer; V1 does not reserve formation slots or run crowd avoidance.
- Dear ImGui reports selected actor handles/object IDs and group command success/failure counts.

Later:
- Add DetourCrowd if local avoidance becomes necessary.

## Phase 8 - Near-Movement Polish

Status: implemented as the first pass of local movement usability polish.

Implemented behavior:
- Accepted formation destinations remain visible through debug draw markers.
- Failed/unreachable formation destinations draw warning markers and report the first failure reason in Dear ImGui.
- `player.stop` is a semantic input action bound to Space and cancels the current selected-actor command, or the player command when no actors are selected.
- Right-clicking a hovered object runs the existing interaction handler; right-clicking terrain issues movement.
- Actor path state already includes stuck detection, limited repath, blocked/failed status, and movement status in Dear ImGui.
- Group command stats show selected actors, path statuses, success/failure counts, and failure messages.

Avoid in V1:
- Gameplay UI prompts.
- Queued command orders.
- Per-object cursor modes.
- Local avoidance or slot reservation.

## Phase 9 - Chunk Navigation Connectivity

Status: implemented for currently loaded nav tiles.

Extract coarse connectivity metadata from loaded/generated chunk nav data.

Goal:
- Determine how each chunk connects to neighboring chunks without needing the full world loaded.
- Produce stable portal candidates for north, south, east, and west chunk borders.
- Keep the data simple enough to debug and eventually cache.

Planned data:
- `ChunkNavConnectivity`
- chunk coord
- primary biome/debug labels
- border portal points
- reachable neighbor directions
- approximate traversal cost
- blocked/partial-connectivity flags

V1 behavior:
- Build connectivity for currently loaded nav tiles.
- Identify border portals by sampling navigable points near chunk edges.
- Accept portals only when they remain inside the edge band and can path from the chunk center.
- Merge nearby representative portal points per edge.
- Mark neighbor connections when adjacent loaded chunks have matching opposite-edge portals.
- Draw portals and chunk-to-chunk links with debug primitives.
- Report connectivity counts and missing/blocked edges in Dear ImGui.

Acceptance:
- Loaded chunks expose visible portal markers on traversable borders.
- Adjacent loaded chunks can report whether they are mutually connected.
- Negative chunk coordinates and mixed biomes remain deterministic.

Avoid in V1:
- Full long-distance route planning.
- Disk persistence of connectivity.
- Road preference or faction gate rules.
- Portal quality scoring beyond basic reachability.

## Phase 10 - Coarse Region Graph

Status: implemented as a deterministic generated chunk graph over the larger sample world profile.

Build a high-level navigation graph over chunk/sector connectivity.

Goal:
- Route between distant chunks using lightweight graph search even when detailed Detour tiles are not all loaded.
- Keep long-distance planning independent from renderer visibility and active terrain mesh LOD.

Planned data:
- `WorldNavigationGraph`
- `WorldNavNode`
- `WorldNavEdge`
- chunk/sector node IDs
- portal/edge costs
- route result with chunk sequence and portal targets

V1 behavior:
- Construct an in-memory `33x33` graph around the active graph center.
- Run A* over chunk nodes from source chunk to destination chunk.
- Use deterministic cost inputs: distance, terrain roughness/biome cost, and loaded connectivity blocked-edge state.
- Return a route made of chunk coords and portal/world-space waypoint targets.
- Add debug draw for graph nodes, graph edges, and the last coarse route separate from the local Detour path.
- Use a larger test profile: `96m` chunks, `7x7` loaded chunks, expanded terrain LOD distances, and wider camera bounds.

Acceptance:
- A route can be planned across generated chunks without building every local nav tile at once.
- Coarse routes produce readable chunk/portal sequences.
- Failed coarse routes report whether the issue was missing connectivity, blocked edges, or invalid endpoints.

Avoid in V1:
- Async graph generation.
- Persistent graph cache.
- Roads as first-class authored splines.
- Door/gate/faction permission rules.
- Multiple movement modes or agent classes.

## Phase 11 - Hierarchical Move Command

Status: implemented as actor-owned hierarchical route state that feeds local Detour path requests.

Goal:
- Allow far terrain commands to become a sequence of local Detour paths through coarse route waypoints.
- Keep local pathing responsible for the precise last leg, blockers, and terrain grounding.

V1 behavior:
- Right-clicking terrain now requests a coarse route through `WorldNavigationGraph` for the commanded actor or selected actor group.
- Actor route state stores a high-level route, final destination, current waypoint index, route status, and last route message.
- Actor requests a local Detour path to the next portal/waypoint.
- As each waypoint is reached, actor advances to the next coarse route target.
- If a required local tile is unavailable, the route enters `WaitingForLocalTile` and retries when fixed updates run against newly available local nav data.
- Stop/cancel/manual movement clears both local path and high-level route state.
- Debug UI shows high-level route status, current waypoint index/count, final destination, route messages, and local path status.
- Coarse route debug draw remains separate from the active local Detour path draw.

Acceptance:
- Nearby commands still use direct local Detour pathing.
- Far commands produce a coarse route and advance through local waypoint paths.
- Streaming/lack of loaded local tiles fails clearly or waits safely.
- Cancelling/stopping an actor clears both local and high-level route state.

Avoid in V1:
- Actor-driven chunk streaming requests.
- Background navmesh generation jobs.
- DetourCrowd/local avoidance.
- Shared group corridors.
- Route reservation or traffic management.

## Phase 12 - Navigation Cache Generation

Status: implemented as an in-app debug/write-through cache for baseline navigation data.

Generate and load deterministic navigation cache artifacts so open-world routing does not need to rebuild every coarse or local navigation product from scratch at runtime.

Goal:
- Move expensive deterministic navigation products out of the frame loop and into explicit cache generation.
- Keep runtime chunk streaming simple: load cache data when available, fall back to synchronous generation only for missing/debug chunks.
- Version cache artifacts against the procedural rules that affect terrain, blockers, biomes, archetypes, and agent/build settings.

Planned cache products:
- **Local nav tile cache:** Detour tile data per chunk and agent profile, built from terrain plus static blocking props.
- **Chunk connectivity cache:** portal metadata and edge connectivity from Phase 9.
- **Coarse graph cache:** generated node/edge data over a configured region from Phase 10.
- **Cache manifest:** world seed, chunk size, generator versions, biome/archetype config hashes, Recast build settings, agent settings, and cache format version.

V1 behavior:
- Add explicit debug-triggered cache generation/refresh commands for the currently visible loaded chunks and graph.
- Write cache files under a generated-data directory, not inside authored config.
- Runtime `NavigationSystem` can load cached Detour tile bytes for a chunk before falling back to building from terrain.
- Runtime connectivity and world graph systems can load cached metadata before falling back to deterministic generation.
- A cache mismatch must be reported clearly and treated as a miss, not silently loaded.
- Persistent object overrides remain runtime modifications; V1 caches only deterministic baseline navigation and requires affected loaded chunks to rebuild when edited blockers change.
- Dear ImGui reports cache hits, misses, stale entries, writes, last cache path/message, and current manifest identity.

Acceptance:
- Generating a cache for the visible/graph test region produces repeatable files for the same seed and config.
- Starting the app with a valid cache avoids rebuilding unchanged loaded nav tiles where possible.
- Changing biome rules, archetype blocking data, Recast settings, or agent settings invalidates the relevant cache data.
- Missing cache data falls back to current synchronous generation without breaking movement.
- Debug rebuild can refresh cache data for selected/visible chunks.

Avoid in V1:
- Background worker build queues.
- Streaming-driven cache generation for arbitrary distant chunks.
- Persistent dynamic obstacle caches.
- Multiple simultaneous agent-class caches beyond the current player-sized profile.
- Compression or archive packaging unless file size becomes a real problem.
- Save-file coupling; cache is derived data and can be deleted/regenerated.

## Deferred

- Async navmesh generation and worker-thread build queues.
- Fully automatic disk-backed navmesh/connectivity cache generation for arbitrary world regions.
- Actor-driven chunk streaming requests.
- Road networks as authored splines with road-following bias.
- Off-mesh links for ladders, jumps, ledges, bridges, doors, elevators, or teleport-style transitions.
- Door/gate navigation, faction permissions, lock state, and destructible blockers.
- Multiple agent classes and movement modes.
- DetourCrowd, reciprocal avoidance, local steering, and formation slot reservation.
- DetourTileCache obstacle carving for dynamic blockers.
- Shared group corridors, traffic lanes, and crowd-scale route reservation.
- Long-distance travel simulation while actors are fully unloaded.
- Save migration/versioning for biome or connectivity rule changes.

## First Implementation Milestone

Implement terrain-only Recast tile build, Detour path query, debug path draw, and right-click player move. This creates the local command loop needed for Kenshi-like near movement while keeping open-world-scale navigation decisions deferred.
