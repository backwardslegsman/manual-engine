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
