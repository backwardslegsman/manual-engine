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
