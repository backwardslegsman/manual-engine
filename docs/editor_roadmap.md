# ManualEngine Editor Roadmap

This roadmap describes the first editor-style build configuration for ManualEngine. The editor is intended to grow from the current Dear ImGui debug surface into a full editor while preserving the modern scene-backed runtime boundaries.

Status: complete as of 2026-06-21 for the first editor vertical slice. This roadmap now records the completed E1-E7 milestone, not the full future editor program.

## Goal

Create a separate editor executable that reuses the modern scene/open-world runtime but adds an editor-facing settings model, reflection-backed property UI, and explicit rebuild controls.

The first vertical slice focuses on the terrain-derived pipeline:

- heightmap import settings;
- terrain render LOD generation settings;
- navigation build and agent settings;
- scene-geometry navigation filtering settings;
- terrain physics collider generation settings;
- saved-build validation and rebuild controls;
- live promotion of rebuilt outputs;
- renderer, culling, atmosphere, camera, and debug settings that can be applied without expensive rebuilds.

## Completed Capability

The E1-E7 milestone delivers a separate `manual_engine_editor` executable that boots the modern scene stack, loads `projects/default.editor.yaml`, reflects editor project settings into generated Dear ImGui panels, tracks validation and dirty state, rebuilds saved open-world streaming builds, applies lightweight renderer/debug/camera settings on command, reloads rebuilt streaming-owned data, and runs one-shot Lua tool scripts through safe App-side hooks.

`manual_engine` remains the sample-oriented executable and does not invoke editor panels, rebuild coordination, live apply, streaming reload commands, or tool scripting.

Generated outputs, runtime handles, renderer handles, physics handles, navigation handles, Lua state, and behavior handles remain derived/runtime state and are not persisted in the editor profile.

## Closure Acceptance Checklist

- `manual_engine_editor` boots the modern default scene through shared App runtime composition.
- `manual_engine_editor` loads `projects/default.editor.yaml` with validated default-fill behavior.
- The editor window exposes Project Settings, Rebuild, Diagnostics, Runtime/Viewport, and Tool Scripts panels.
- Reflected settings can be inspected and edited in memory without bypassing validation.
- Rebuild commands can regenerate saved open-world streaming builds through public Engine bake APIs.
- Lightweight renderer/debug/camera settings apply only through explicit editor commands.
- Rebuilt streaming outputs can be reloaded through the streaming-only App reload path.
- `scripts/editor/validate_profile.lua` can run as a sample Lua tool script through the editor Tool Scripts panel.
- `manual_engine` stays sample-oriented and does not invoke editor UI, rebuild coordinator, live apply, streaming reload, or tool scripting code.
- Editor profile persistence excludes generated outputs and all runtime subsystem handles/state.

## Future Roadmaps

The following work is explicitly outside this completed roadmap and should be planned in a new roadmap before implementation:

- profile YAML save/apply workflow;
- partial payload rebuilds and saved-manifest merging;
- dockspace, editor layout persistence, and multi-window workspace polish;
- startup autorun or file watching for tool scripts;
- scene/object selection and inspector editing;
- asset browser and content pipeline UI;
- undo/redo, transactions, project packaging, and multi-project profiles;
- broader live resource hot-swap beyond the current streaming-only reload path.

## Architecture Direction

- Add a separate `manual_engine_editor` executable rather than turning the sample runtime into an editor mode.
- Keep `manual_engine` behavior stable and sample-oriented.
- Extract reusable modern-default runtime composition from `src/App/main.cpp` into a narrow shared support layer so both the sample and editor can compose the same `Scene`, terrain, streaming, navigation, physics, renderer, and debug systems.
- Keep reusable behavior under `src/Engine`, renderer/bgfx ownership under `src/Renderer`, source decoding under `src/Assets`, and executable composition under `src/App`.
- Use public Engine/Renderer APIs, reflection metadata, native behavior hooks, and Lua boundaries. Do not expose subsystem storage directly to editor code.

## Phase E1: Editor Target And Runtime Composition

Goal: create an editor entrypoint without destabilizing the current default sample.

Status: implemented as the initial editor executable and shared modern scene launch boundary. `manual_engine_editor` boots the same modern default scene stack as `manual_engine`, uses an editor window title/debug mode label, and provides the launch boundary that later phases use for persistent settings, reflection-backed panels, rebuilds, live apply, and tool scripting.

Implementation direction:

- Add a `manual_engine_editor` target with its own App entrypoint.
- Extract shared modern runtime setup into reusable composition helpers.
- Preserve the current `manual_engine` executable as the runtime/sample path.
- Keep SDL, bgfx, Dear ImGui, fixed-step timing, modern terrain, scene, navigation, physics, character movement, streaming, and debug visualization composition explicit.
- Avoid introducing a generic editor framework or ECS. The editor should compose the existing modern systems.

Exit criteria:

- The editor executable boots the modern default scene stack.
- The sample executable still builds and runs with the same default behavior.
- Shared runtime setup is not hidden inside renderer, debug UI, or subsystem internals.

## Phase E2: Project Settings Profile

Goal: establish durable editor state that drives rebuild identity.

Status: implemented as an App-side editor project profile. `manual_engine_editor` loads `projects/default.editor.yaml`, validates/default-fills it through `EditorProjectSettings`, and uses the profile to seed safe startup defaults for streaming validation, camera, renderer debug, and debug draw settings. Later phases now use this profile for reflection metadata, UI, dirty reporting, rebuild orchestration, live apply, and tool scripting.

Implementation direction:

- Add a durable editor project profile, for example `projects/default.editor.yaml`.
- Treat the profile as the source of truth for editable build/render settings.
- Keep generated streaming manifests, terrain cache manifests, nav cache manifests, and derived payload files as outputs, not editable project state.
- Support default-fill for missing profile fields so future settings can be added without breaking old profiles.

Initial profile groups:

- `TerrainHeightmapImportSettings`
- `TerrainLodMeshBuildSettings[]`
- `TerrainDerivedCacheSettings`
- `NavBuildSettings`
- `NavAgentSettings`
- `TerrainNavigationBuildSettings`
- terrain physics collider resolution/build settings
- scene navigation geometry slope and tile-padding settings
- `OpenWorldStreamingRuntimeSettings` fields relevant to saved-build validation/rebuild
- renderer viewport settings: atmosphere, distance culling, render/debug draw settings
- camera settings useful for editor viewport defaults

Exit criteria:

- Editor settings load from and save to a durable profile.
- The profile can be validated before it mutates runtime state.
- Profile changes can be compared against active runtime/saved-build fingerprints.

## Phase E3: Settings Reflection Layer

Goal: make settings inspectable and editable through metadata instead of hand-coded one-off panels.

Status: implemented as an App-side settings reflection layer over `EditorProjectSettings`. E3 registers editor-visible descriptors for the first-slice project settings, enumerates singleton and indexed render-LOD targets, and provides validated reflected get/set helpers consumed by the editor UI, dirty reporting, rebuild orchestration, live apply, and Lua tool scripting. Profile YAML auto-save remains future work.

Implementation direction:

- Extend the existing reflection metadata pattern with editor-visible reflected descriptors for profile/settings objects.
- Keep this separate from live scene object reflection such as actor/component/physics/character properties.
- Add settings object categories such as:
  - Terrain Import
  - Render LODs
  - Navigation Build
  - Navigation Agent
  - Physics Colliders
  - Scene Geometry Filtering
  - Streaming
  - Renderer
  - Debug Draw
  - Camera
- Use existing reflected value types where possible.
- Mark expensive settings with `RequiresExplicitApply`.
- Use metadata for display name, category, min/max, enum labels, units, documentation, advanced flags, script visibility, and editor visibility.

Exit criteria:

- The editor can enumerate first-slice settings from reflection metadata.
- UI widgets can be generated from type and constraint metadata.
- Expensive settings become dirty without silently rebuilding.

## Phase E4: Editor UI Panels

Goal: expose reflected settings and rebuild status through Dear ImGui.

Status: implemented as App-side editor-only ImGui panels over `EditorProjectSettings`. `manual_engine_editor` now shows a separate editor window that generates project settings controls from E3 reflection metadata, stages edits in memory, reports validation and reflection diagnostics, and distinguishes rebuild/apply-required changes from lightweight pending settings. Later phases now provide rebuild execution, dirty-domain orchestration, live apply, reload, and tool scripting. Profile YAML saving remains future work.

Implementation direction:

- Add editor-facing ImGui panels:
  - Project Settings
  - Rebuild
  - Diagnostics
  - Runtime/Viewport Settings
- Generate most settings controls from reflected descriptors.
- Use type-aware widgets:
  - checkboxes for booleans;
  - numeric inputs/sliders for constrained numbers;
  - combo boxes for enum labels;
  - vector fields for vector values;
  - path fields for source/cache/profile paths;
  - list editors for render LOD profiles.
- Keep debug visualization controls available, but do not persist transient debug state unless it is explicitly part of the editor profile.

Exit criteria:

- Users can inspect and edit first-slice settings through the editor.
- Dirty/rebuild-required state is visible.
- Invalid settings show diagnostics without mutating runtime state.

## Phase E5: Rebuild Orchestrator

Goal: rebuild derived terrain/nav/physics/render outputs explicitly from the editor profile.

Status: implemented as an App-side rebuild coordinator over the in-memory editor profile. `manual_engine_editor` now computes dirty rebuild domains from reflected settings, exposes explicit rebuild buttons, runs the existing full saved-build bake/write backend for all rebuild-domain commands, reports diagnostics and runtime-reload-required status, and advances editor baselines after successful saved-build or lightweight-profile apply commands. E6 now provides live runtime reload. Profile saving, partial payload rebuilds, and manifest merging remain future work.

Implementation direction:

- Add an editor-only rebuild coordinator that validates the current profile and computes dirty domains.
- Route work through existing explicit systems:
  - heightmap import and terrain chunk cache;
  - terrain render LOD adapter/cache;
  - terrain navigation adapter/cache;
  - scene navigation geometry filtering;
  - terrain physics collider adapter/cache;
  - open-world saved-build manifest generation.
- Use existing async/main-thread queues where applicable.
- Promote rebuilt live outputs only through explicit main-thread callbacks.
- Provide rebuild actions:
  - rebuild terrain chunks;
  - rebuild render LODs;
  - rebuild nav tiles;
  - rebuild physics colliders;
  - rebuild full saved build;
  - apply lightweight runtime settings.
- Report cancel, failed, stale, cache hit/miss/corrupt, and promotion diagnostics.

Exit criteria:

- Editing terrain/nav/physics settings marks the correct rebuild domains dirty.
- Rebuild actions produce new cache/fingerprint identities where appropriate.
- Runtime promotion is explicit and diagnosable.

## Phase E6: Live Apply And Validation

Goal: make editor changes predictable and safe.

Status: implemented as explicit editor-only live apply commands. Lightweight renderer, debug draw, and camera profile edits now apply to the running editor viewport only when requested, rebuilt saved streaming builds can be reloaded through a streaming-owned resource teardown/reinitialize path, and validation runs before live apply, rebuild, and reload commands. Profile YAML saving, partial payload rebuilds, full-scene restart, and broader hot-swap behavior are future-roadmap scope.

Implementation direction:

- Apply lightweight renderer/debug/camera settings through an explicit Apply button.
- Require explicit rebuild for terrain import, render LOD, navigation, physics collider, and streaming saved-build settings.
- Validate before apply or rebuild:
  - missing paths;
  - invalid numeric ranges;
  - unsupported LOD resolutions;
  - excessive nav tile limits;
  - invalid collider resolution;
  - stale or incompatible cache roots;
  - missing optional assets.
- Preserve existing cache identity contracts. Changed build settings must invalidate saved-build fingerprints and relevant derived payloads.

Exit criteria:

- Invalid settings never partially mutate the live runtime.
- Rebuild-affecting settings are visible as dirty until rebuilt.
- Lightweight settings can be tested quickly in the editor viewport.
- Rebuilt saved streaming outputs remain marked as reload-required until the editor streaming runtime reload succeeds.

## Phase E7: Hooks, Lua, And Tool Scripting

Goal: let editor functionality grow through the existing behavior/reflection/Lua boundary without exposing engine internals.

Status: implemented as App-side editor hooks and one-shot Lua tool scripting. `manual_engine_editor` now discovers Lua files from `scripts/editor`, shows them in a Tool Scripts panel, exposes reflected editor settings and validated editor commands through a narrow `editor` Lua table, and fires native editor hook events for settings, validation, rebuild, live apply, reload, and script execution. Scene Lua behavior scripting remains separate. Profile YAML saving, startup autorun, and file watching are future-roadmap scope.

Implementation direction:

- Keep editor mutation routed through reflected/public Engine APIs.
- Let native C++ hooks respond to profile changes, rebuild completion, or selected-editor-object changes only through explicit editor-owned events.
- Expose a narrow Lua tool-scripting surface for reading selected reflected settings and invoking allowed editor commands.
- Do not expose Renderer handles, bgfx resources, Jolt objects, Detour refs, streaming runtime storage, Lua VM internals, or App pointers.

Exit criteria:

- Editor scripting can automate safe settings edits and rebuild commands.
- Tool scripts cannot bypass validation or mutate subsystem storage directly.
- Tool scripts run manually from the editor and report diagnostics without saving project YAML.

## Completed Public Interfaces

Completed public-facing types and APIs:

- `EditorProjectSettings`
- editor profile load/save/validate helpers
- settings reflection registration separate from `registerSceneReflectionDescriptors`
- editor settings dirty-domain diagnostics
- editor rebuild coordinator with:
  - dirty-state query;
  - profile validation;
  - selected-domain rebuild;
  - full saved-build rebuild;
  - lightweight runtime apply;
  - result/diagnostic records.
- editor live-apply host callbacks for:
  - lightweight renderer/debug/camera apply;
  - rebuilt saved-build streaming runtime reload;
  - validation diagnostics before runtime mutation.
- editor tool scripting with:
  - native editor hook events;
  - Lua script discovery from `scripts/editor`;
  - reflected settings get/set;
  - validated rebuild/apply/reload commands;
  - script diagnostics and run status.
- shared modern runtime composition helpers used by both sample and editor executables.

## Testing Strategy

Add focused tests as each phase lands:

- editor profile load/save defaults and round-trip stability;
- profile validation failures and default-fill behavior;
- reflected settings metadata coverage for all first-slice groups;
- correct metadata flags for editor visibility, script visibility, advanced fields, and `RequiresExplicitApply`;
- dirty-domain computation for terrain import, render LOD, nav, physics collider, scene-geometry filtering, streaming, renderer, and debug settings;
- small fixture rebuilds proving changed settings produce new cache/fingerprint identities;
- editor target build wiring without optional large assets;
- regression coverage for existing terrain, navigation, physics collider, render LOD, reflection, Lua, and open-world streaming behavior touched by the editor coordinator.

Required verification for implementation passes:

- relevant new editor tests;
- existing touched subsystem tests;
- `cmake --build --preset windows-vs-vcpkg-debug`.

## Closure Verification Record

Verified on 2026-06-21:

- Built editor verification targets:
  - `manual_engine_editor_project_settings_tests`
  - `manual_engine_editor_settings_reflection_tests`
  - `manual_engine_editor_ui_tests`
  - `manual_engine_editor_rebuild_coordinator_tests`
  - `manual_engine_editor_live_apply_tests`
  - `manual_engine_editor_tool_scripting_tests`
  - `manual_engine_scene_lua_scripting_tests`
  - `manual_engine_open_world_streaming_runtime_tests`
- Ran the same named CTest subset with 8/8 passing.
- Built `manual_engine_editor`.
- Built `manual_engine`.
- Built `cmake --build --preset windows-vs-vcpkg-debug`.
- Launched `manual_engine_editor` for a terminal smoke check; the process stayed alive for five seconds and was stopped cleanly.

Terminal automation did not visually inspect Dear ImGui layout overlap. The closure checklist records the intended shipped editor surfaces; final visual sign-off should be done interactively when polishing editor UI layout.

## Assumptions And Constraints

- The first editor milestone is a separate executable: `manual_engine_editor`.
- First persistent state is a project profile file, not generated manifests.
- The first rebuildable slice prioritizes terrain-derived data and lightweight renderer/debug/camera settings.
- Existing `manual_engine` sample behavior remains stable.
- Generated caches, saved builds, runtime handles, renderer handles, physics handles, navigation handles, Lua VM state, and behavior handles remain non-serialized derived/runtime state.
- Editor settings and rebuild commands must use public Engine/Renderer APIs rather than mutating internal storage.
- Heavy optional assets should not become mandatory for default CTest.
