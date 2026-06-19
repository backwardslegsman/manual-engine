# T9 Terrain Serialization Preparation

Phase T9 freezes the terrain identity and payload boundary needed for future chunked serialize/deserialize. It does not implement file I/O, scene/world serialization, terrain editing, App streaming changes, or automatic cache migration.

## Durable Identity

Future serialized terrain chunk files identify chunks by `TerrainChunkStableIdentity`:

- source asset ID: `TerrainSourceChunkId::source`;
- source-space chunk coordinate: `TerrainSourceChunkId::coord`;
- terrain source type: imported heightmap, procedural, or generated;
- import settings key: pipeline, version, and options hash;
- source revision: source content hash or procedural/settings hash;
- material revision: terrain material-rule or material-set version/hash;
- authoritative chunk resolution;
- authoritative chunk world size.

`TerrainSourceHandle` and `TerrainChunkHandle` remain runtime-only generation-counted handles. They are intentionally absent from the durable identity string and hash.

## Chunk File Metadata

`TerrainSerializedChunkFileMetadata` is the future chunk-file header contract:

- `schemaVersion`: currently `terrain_chunk_serialization_t9_v1`;
- `payloadVersion`: currently `terrain_chunk_authoritative_payload_t9_v1`;
- `role`: source snapshot, edited override, or derived-cache reference;
- `identity`: the durable chunk identity above;
- `boundary`: booleans that describe which payload classes are present;
- `identityHash`: deterministic hash of the durable identity and schema version;
- `payloadFileName`: deterministic file name derived from source ID, chunk coordinate, and identity hash.

This metadata is plain Engine data. T9 does not define a YAML or binary on-disk layout yet; that will happen in the serialization implementation phase.

## Payload Boundary

Future chunk payloads must distinguish authoritative data from derived data:

- authoritative heights may be stored in source snapshots and edited overrides;
- edited height deltas and material overrides belong to serialized override payloads;
- renderer LOD meshes, navigation build data, and physics collider meshes are derived data and must remain disposable;
- live runtime handles are never valid serialized payload data.

Validation rejects missing source IDs, missing source/import revisions, invalid chunk size/resolution, mismatched identity hashes, missing payload filenames, and any payload boundary that stores live runtime handles. Derived-data payload flags are allowed but warn because they must not become authoritative source data.

## Derived Cache Relationship

`terrainDerivedCacheSourceHash(metadata)` produces a deterministic source hash for derived cache manifests from serialized chunk metadata. This lets future serialized chunk support invalidate render/nav/physics/material derived outputs when source revision, import settings, material revision, payload version, or payload boundary changes.

The existing `TerrainDerivedCache` remains a disposable generated-data cache. T9 does not add automatic cache reads/writes to serialization.

## Non-Goals

- No chunk file reader or writer.
- No scene/world serializer.
- No terrain editor or edited chunk payload format.
- No App streaming policy change.
- No renderer, navigation, physics, or material-weight cache format changes.
- No migration of existing procedural saves.
