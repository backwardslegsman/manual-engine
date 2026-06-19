#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/TerrainDataset.hpp"

namespace Engine {
    inline constexpr const char* TerrainChunkSerializationSchemaVersion = "terrain_chunk_serialization_t9_v1";
    inline constexpr const char* TerrainChunkAuthoritativePayloadVersion = "terrain_chunk_authoritative_payload_t9_v1";

    enum class TerrainSerializedChunkPayloadRole {
        SourceSnapshot,
        EditedOverride,
        DerivedCacheReference,
    };

    struct TerrainSerializedChunkPayloadBoundary {
        bool storesAuthoritativeHeights = true;
        bool storesEditedHeightDeltas = false;
        bool storesMaterialOverrides = false;
        bool storesRendererLodMeshes = false;
        bool storesNavigationBuildData = false;
        bool storesPhysicsColliderMeshes = false;
        bool storesLiveRuntimeHandles = false;
    };

    struct TerrainChunkStableIdentity {
        TerrainSourceChunkId chunkId;
        TerrainDatasetSourceType sourceType = TerrainDatasetSourceType::Generated;
        AssetImportSettingsKey importSettings;
        std::string sourceRevision;
        std::string materialRevision;
        uint32_t chunkResolution = 0;
        float chunkSize = 0.0f;
    };

    struct TerrainSerializedChunkFileMetadata {
        std::string schemaVersion = TerrainChunkSerializationSchemaVersion;
        std::string payloadVersion = TerrainChunkAuthoritativePayloadVersion;
        TerrainSerializedChunkPayloadRole role = TerrainSerializedChunkPayloadRole::SourceSnapshot;
        TerrainChunkStableIdentity identity;
        TerrainSerializedChunkPayloadBoundary boundary;
        std::string identityHash;
        std::string payloadFileName;
    };

    struct TerrainSerializationPrepValidation {
        bool valid = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] std::string terrainChunkStableIdentityString(
        const TerrainChunkStableIdentity& identity,
        std::string_view schemaVersion = TerrainChunkSerializationSchemaVersion);
    [[nodiscard]] std::string terrainChunkStableIdentityHash(
        const TerrainChunkStableIdentity& identity,
        std::string_view schemaVersion = TerrainChunkSerializationSchemaVersion);
    [[nodiscard]] std::string terrainSerializedChunkFileName(
        const TerrainChunkStableIdentity& identity,
        std::string_view identityHash);
    [[nodiscard]] TerrainSerializedChunkFileMetadata buildTerrainSerializedChunkFileMetadata(
        TerrainChunkStableIdentity identity,
        TerrainSerializedChunkPayloadBoundary boundary = {},
        TerrainSerializedChunkPayloadRole role = TerrainSerializedChunkPayloadRole::SourceSnapshot,
        std::string payloadVersion = TerrainChunkAuthoritativePayloadVersion,
        std::string schemaVersion = TerrainChunkSerializationSchemaVersion);
    [[nodiscard]] TerrainSerializationPrepValidation validateTerrainSerializedChunkFileMetadata(
        const TerrainSerializedChunkFileMetadata& metadata);
    [[nodiscard]] std::string terrainDerivedCacheSourceHash(
        const TerrainSerializedChunkFileMetadata& metadata);
}
