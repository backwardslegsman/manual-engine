# Recast + Detour Near-Movement Roadmap

This roadmap targets Kenshi-like near movement first: select or command an actor, click a nearby terrain destination, follow a navmesh path over procedural terrain, and expose enough debug tools to tune movement. Long-distance world travel, hierarchical planning, and async streaming come later.

## Goal

- Use Recast + Detour for terrain-aware local pathfinding.
- Keep Recast/Detour hidden behind Engine-owned navigation interfaces.
- Keep actor vertical placement snapped to `TerrainSystem::sampleHeight`.
- Integrate with existing chunk streaming, terrain CPU data, object identity, debug draw, and interaction systems.
- Prioritize a working near-command loop over full open-world navigation.

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

## Deferred

- Long-distance travel.
- Hierarchical pathfinding.
- Async navmesh generation.
- Navmesh persistence cache.
- Off-mesh links.
- Door/gate navigation.
- Multiple agent classes.
- DetourCrowd.
- DetourTileCache obstacle carving.

## First Implementation Milestone

Implement terrain-only Recast tile build, Detour path query, debug path draw, and right-click player move. This creates the local command loop needed for Kenshi-like near movement while keeping open-world-scale navigation decisions deferred.
