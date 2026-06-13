# Recast + Detour Near-Movement Roadmap

This roadmap targets Kenshi-like near movement first: select or command an actor, click a nearby terrain destination, follow a navmesh path over procedural terrain, and expose enough debug tools to tune movement. Long-distance world travel, hierarchical planning, and async streaming come later.

## Goal

- Use Recast + Detour for terrain-aware local pathfinding.
- Keep Recast/Detour hidden behind Engine-owned navigation interfaces.
- Keep actor vertical placement snapped to `TerrainSystem::sampleHeight`.
- Integrate with existing chunk streaming, terrain CPU data, object identity, debug draw, and interaction systems.
- Prioritize a working near-command loop over full open-world navigation.

## Phase 1 - Navigation Backend Boundary

Add `Engine::NavigationSystem` as the only public owner of navigation runtime state.

Initial public concepts:
- `NavigationTileHandle`
- `NavAgentSettings`
- `NavPath`
- `NavQueryResult`

Initial API:
- `buildTerrainTile(ChunkCoord coord, const TerrainTile& terrain)`
- `destroyTile(ChunkCoord coord)`
- `nearestNavigablePoint(glm::vec3 point, const NavAgentSettings& agent)`
- `findPath(glm::vec3 start, glm::vec3 end, const NavAgentSettings& agent)`
- `isNavigable(glm::vec3 point, const NavAgentSettings& agent)`

Implementation notes:
- Recast and Detour types stay private to `NavigationSystem`.
- App, actors, terrain, and persistence must not include Recast headers.
- Start with one agent configuration: player-sized humanoid.

## Phase 2 - Terrain-Only Navmesh Tiles

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

Acceptance:
- Loaded chunks produce valid nav tiles.
- Unloaded chunks remove their nav tiles.
- A nearby terrain point can be snapped to the nearest navigable polygon.

## Phase 3 - Click-To-Move Player

Add one semantic input action:

```yaml
player.set_destination: RightMouse
player.cancel_destination: Escape
```

Flow:
- Existing terrain picking provides the clicked world position.
- App publishes or handles a destination command for the player actor.
- Actor requests `NavigationSystem::findPath`.
- Actor follows path corners at its configured movement speed.
- Actor facing rotates toward movement direction.
- Existing kinematic collision remains active as a safety layer.

Acceptance:
- Right-clicking nearby terrain moves the player marker along a visible path.
- Unreachable destinations report a debug status instead of moving directly.
- Escape cancels the active destination.

## Phase 4 - Path-Following Actor State

Extend actor movement with path-following state:
- current path
- current corner index
- destination
- arrival radius
- movement status: idle, pathing, moving, blocked, arrived, failed

Behavior:
- Move toward the current path corner in XZ.
- Advance corners when within arrival radius.
- Stop at final destination.
- Repath when a new destination is issued.
- Keep direct WASD movement as debug/manual steering for now.

Acceptance:
- Actor arrives at nearby clicked destinations.
- Actor stops cleanly at the end of the path.
- Actor reports failed pathing without corrupting movement state.

## Phase 5 - Navigation Debug Draw

Use renderer debug primitives for:
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

Acceptance:
- Debug draw makes path, tile, and failure state visible.
- Agent settings can be tuned live and visible tiles rebuilt.

## Phase 6 - Static Prop Blocking

After terrain-only pathing works, include blocking props in nav generation.

V1 approach:
- Blocking archetypes contribute conservative AABB geometry to the owning chunk nav build.
- Removing, placing, or moving a blocking persistent object rebuilds the affected chunk nav tile.
- If an edited object crosses chunks, rebuild both old and new owning chunks.

Avoid in V1:
- DetourTileCache obstacle carving.
- Dynamic obstacle avoidance.
- Crowd simulation.

Acceptance:
- Removed trees/rocks no longer block regenerated nav.
- Placed markers can block nav after chunk rebuild.
- Moving blocking objects updates the owning chunk nav tile.

## Phase 7 - Multi-Selection Movement

Add group movement only after one actor pathing is stable.

V1 behavior:
- Selected actors receive one command.
- Each actor gets a deterministic formation offset around the clicked point.
- Each actor requests an independent nearby destination/path.
- Existing kinematic collision handles basic overlap prevention.

Later:
- Add DetourCrowd if local avoidance becomes necessary.

## Phase 8 - Near-Movement Polish

Add the small behaviors that make local movement feel usable:
- destination marker
- stop command
- unreachable feedback
- stuck detection and limited repath
- click object to interact, click terrain to move
- movement status in Dear ImGui

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
