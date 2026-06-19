# Scene Serialization Conventions

This document is the shared contract for scene-oriented serialization work. It applies to the Phase 13 binary scene container and to later subsystem records that want to participate in scene save/load.

Use this document when adding serialized scene data, durable component descriptors, terrain chunk references, authored or animated scene bindings, scripting-visible variables, or migration code. The short rule is: serialize durable identity and plain data, never live runtime ownership.

## Current Scope

Phase 13 implements the foundation and core scene round trip:

- binary magic `MESCENE\0`;
- little-endian fixed-width numeric fields;
- format string `scene_binary_v13_1`;
- numeric format version `1`;
- fixed header plus chunk directory;
- 64-bit FNV-1a checksums for header and chunk payloads;
- header-only inspection for future streaming decisions;
- whole-file read/write in the first implementation;
- validation before mutation;
- restore of core `Engine::Scene` actors, local transforms, hierarchy links, and metadata-only scene components.

The current loader does not reconstruct renderer bridge resources, physics bodies or colliders, character movement records, navigation tiles, terrain runtime chunks, authored or animated adapter resources, App state, asset-cache acquisitions, scripting state, or editor data.

## Identity Rules

Runtime handles are transient and must not be serialized. This includes generation-counted handles, renderer handles, backend IDs, and opaque access tokens.

Durable serialized identity uses:

- `SceneObjectId` for scene actors;
- `SceneComponentTypeId` for metadata-only scene component type identity;
- future stable component IDs only after a component system explicitly introduces them;
- `AssetId` plus import settings for asset references;
- `TerrainChunkStableIdentity` and `TerrainSerializedChunkFileMetadata` for terrain references;
- procedural `ObjectId` only for the existing procedural `World` save path, not for scene actors.

Do not infer durable identity from runtime handle index, generation, creation address, vector slot, renderer handle value, Jolt body ID, Detour reference, bgfx handle, cache path, or debug label.

Actors without valid `SceneObjectId` fail validation by default. Generated temporary serialization IDs are allowed only when a caller explicitly enables that policy for tools/tests, and those IDs should not be treated as authoring identity.

## Runtime Data That Must Stay Out

The following are invalid serialized payload data:

- `SceneActorHandle`, `SceneComponentHandle`, `SceneSystemHandle`;
- scene render bridge component handles and live renderer instance/light mappings;
- `Renderer::*Handle` values, bgfx resources, draw stats, render groups as live handles;
- `ScenePhysicsBodyHandle`, `SceneColliderHandle`, Jolt IDs, broadphase state, contact caches;
- `SceneCharacterHandle`, current sweep/debug caches, transient path-following internals unless a future durable descriptor is added;
- `NavigationSystem` tile handles, Detour refs, path corridor internals, connectivity rebuild handles;
- `TerrainSourceHandle`, `TerrainChunkHandle`, terrain renderer handles, terrain physics collider handles;
- `AssetHandle`, `AssetCache` acquisitions, loaded texture/mesh ownership;
- `OpaqueHandle`;
- ImGui state, debug-only counters, frame indices, dirty flags, scheduler callback handles, pending async jobs, and App-local sample state.

If a live subsystem needs to be rebuilt after loading, serialize the durable descriptor or reference it needs, then recreate live resources through explicit composition code after the core scene has loaded.

## Binary Container Conventions

All binary fields are written explicitly. Do not dump C++ structs directly.

Container rules:

- fields are little-endian;
- integers are fixed width;
- floats are 32-bit IEEE values written field-by-field;
- strings are length-prefixed UTF-8 bytes;
- vectors, quaternions, matrices, and transforms are serialized as named numeric fields in deterministic order;
- chunks are independently addressable by directory offset and byte size;
- chunks carry uncompressed size even when compression is not used yet;
- chunk checksums validate payload bytes independently;
- unknown optional chunks may be skipped with diagnostics;
- unknown required chunks fail validation;
- unsupported format versions fail before runtime mutation.

The first implementation writes whole files. Future streaming should use the same header and directory to read selected chunks without changing the identity or validation rules.

## Determinism

Deterministic output is the default expectation for tests and generated fixtures.

Use deterministic ordering:

- metadata first;
- reflection schema before records that depend on it;
- asset references by stable asset identity;
- actors by `SceneObjectId`, then explicit order index as tie-breaker when allowed;
- hierarchy links by child actor record order;
- components by owner `SceneObjectId`, then component type ID, then explicit order index;
- terrain references by durable terrain chunk identity;
- extension chunks last.

Avoid wall-clock timestamps, machine-local paths, temporary directories, pointer values, hash-table iteration order, unordered diagnostics, and non-deterministic generated IDs unless the caller explicitly requests non-deterministic metadata.

## Validation Before Mutation

Readers and loaders must validate the complete snapshot before mutating a live `Scene` or any live subsystem.

Validation should reject:

- bad magic;
- wrong endian marker;
- unsupported format version;
- invalid header size;
- invalid file size;
- out-of-bounds or overlapping chunks;
- checksum mismatch;
- missing required chunks;
- unknown required chunks;
- duplicate actor IDs;
- invalid parent IDs;
- component records with missing owners;
- invalid component type IDs;
- serializable `OpaqueHandle` values;
- terrain metadata whose payload boundary claims live runtime handles.

Validation may warn about:

- unknown optional chunks;
- skipped transient or runtime-only reflected properties;
- missing or stale asset references;
- derived terrain payload references that are not authoritative source data;
- optional records ignored by the current engine version.

Apply operations must return a failure status without partial mutation when validation fails. If a future load path needs to replace an existing scene, it should stage into a temporary scene or use an explicit clear-and-apply transaction.

## Reflection Schema Use

Reflection metadata is a compatibility and tool contract, not permission to serialize every reflected property.

Only properties with durable serialization flags should enter serialized payloads. Runtime-only, transient, read-only derived, diagnostic, debug, or `OpaqueHandle` properties must be skipped or rejected according to the owning subsystem contract.

When property IDs change, add explicit migration code and tests. Do not silently reinterpret old property IDs or rely on property names as the only compatibility key.

## Subsystem Extension Hints

When adding a serialized record for a subsystem:

1. Define the durable identity and descriptor in the owning Engine module.
2. Document which live runtime handles are excluded.
3. Store only plain values, stable IDs, and asset/terrain references.
4. Add a chunk type or extension record with a version.
5. Validate references before any live resource creation.
6. Recreate live resources only through the subsystem's public APIs after core scene restore.
7. Add byte-stability and validation-failure tests.

Render bridge records should serialize durable mesh/material/light/camera descriptors and `AssetId` references, not renderer handles or live instances.

Physics records should serialize shape descriptors, material/filter settings, motion type, and stable actor ownership, not Jolt IDs or current broadphase/contact state.

Character movement records should serialize descriptor settings and durable path intent only when needed, not transient sweep caches or nav query internals.

Navigation records should serialize durable requests, agent config, or authored links only when a future phase defines them. Runtime Detour tiles and path refs remain derived/live resources.

Terrain references should use `TerrainSerializationPrep` metadata. Authoritative terrain payloads and edited overrides need their own chunk-file contract; renderer LOD, navigation build geometry, physics collider meshes, and material weights remain derived data unless a future phase explicitly says otherwise.

Authored and animated adapters should serialize imported scene asset identity, node binding identity, and durable adapter descriptors, not adapter-created renderer handles, cached texture acquisitions, sampled pose buffers, or current animation evaluation caches.

## Testing Expectations

Serialization tests should cover:

- header-only read without payload reads;
- malformed header and directory failures;
- checksum failures;
- unknown optional and required chunks;
- deterministic byte output from identical input;
- stable-ID round trip;
- validation-before-mutation;
- explicit absence of runtime handles;
- subsystem reference records using durable identity only.

Use tiny deterministic fixtures for normal CTest. Large authored scenes, KayKit assets, and real heightmaps should remain opt-in validation assets unless a test specifically documents why they are required.
