#include "Engine/TerrainSerializationPrep.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace Engine {
    namespace {
        constexpr uint64_t FnvOffset = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;

        uint64_t fnv1a(std::string_view text, uint64_t hash = FnvOffset)
        {
            for (unsigned char value : text) {
                hash ^= value;
                hash *= FnvPrime;
            }
            return hash;
        }

        std::string hexHash(uint64_t hash)
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << hash;
            return stream.str();
        }

        const char* sourceTypeName(TerrainDatasetSourceType type)
        {
            switch (type) {
                case TerrainDatasetSourceType::HeightmapImported:
                    return "heightmap_imported";
                case TerrainDatasetSourceType::Procedural:
                    return "procedural";
                case TerrainDatasetSourceType::Generated:
                default:
                    return "generated";
            }
        }

        const char* roleName(TerrainSerializedChunkPayloadRole role)
        {
            switch (role) {
                case TerrainSerializedChunkPayloadRole::EditedOverride:
                    return "edited_override";
                case TerrainSerializedChunkPayloadRole::DerivedCacheReference:
                    return "derived_cache_reference";
                case TerrainSerializedChunkPayloadRole::SourceSnapshot:
                default:
                    return "source_snapshot";
            }
        }

        void appendBool(std::ostringstream& stream, bool value)
        {
            stream << (value ? '1' : '0') << '|';
        }
    }

    std::string terrainChunkStableIdentityString(
        const TerrainChunkStableIdentity& identity,
        std::string_view schemaVersion)
    {
        std::ostringstream stream;
        stream << schemaVersion << '|'
               << identity.chunkId.source.value << '|'
               << identity.chunkId.coord.x << '|'
               << identity.chunkId.coord.z << '|'
               << sourceTypeName(identity.sourceType) << '|'
               << identity.importSettings.pipeline << '|'
               << identity.importSettings.version << '|'
               << identity.importSettings.optionsHash << '|'
               << identity.sourceRevision << '|'
               << identity.materialRevision << '|'
               << identity.chunkResolution << '|'
               << std::setprecision(std::numeric_limits<float>::max_digits10) << identity.chunkSize;
        return stream.str();
    }

    std::string terrainChunkStableIdentityHash(
        const TerrainChunkStableIdentity& identity,
        std::string_view schemaVersion)
    {
        return hexHash(fnv1a(terrainChunkStableIdentityString(identity, schemaVersion)));
    }

    std::string terrainSerializedChunkFileName(
        const TerrainChunkStableIdentity& identity,
        std::string_view identityHash)
    {
        return "terrain_chunk_" +
            std::to_string(identity.chunkId.source.value) + "_" +
            std::to_string(identity.chunkId.coord.x) + "_" +
            std::to_string(identity.chunkId.coord.z) + "_" +
            std::string{identityHash} + ".bin";
    }

    TerrainSerializedChunkFileMetadata buildTerrainSerializedChunkFileMetadata(
        TerrainChunkStableIdentity identity,
        TerrainSerializedChunkPayloadBoundary boundary,
        TerrainSerializedChunkPayloadRole role,
        std::string payloadVersion,
        std::string schemaVersion)
    {
        TerrainSerializedChunkFileMetadata metadata;
        metadata.schemaVersion = std::move(schemaVersion);
        metadata.payloadVersion = std::move(payloadVersion);
        metadata.role = role;
        metadata.identity = std::move(identity);
        metadata.boundary = boundary;
        metadata.identityHash = terrainChunkStableIdentityHash(metadata.identity, metadata.schemaVersion);
        metadata.payloadFileName = terrainSerializedChunkFileName(metadata.identity, metadata.identityHash);
        return metadata;
    }

    TerrainSerializationPrepValidation validateTerrainSerializedChunkFileMetadata(
        const TerrainSerializedChunkFileMetadata& metadata)
    {
        TerrainSerializationPrepValidation validation;
        if (metadata.schemaVersion.empty()) {
            validation.errors.push_back("Terrain chunk schema version is required.");
        }
        if (metadata.payloadVersion.empty()) {
            validation.errors.push_back("Terrain chunk payload version is required.");
        }
        if (!isValid(metadata.identity.chunkId.source)) {
            validation.errors.push_back("Terrain chunk source asset ID is required.");
        }
        if (metadata.identity.sourceRevision.empty()) {
            validation.errors.push_back("Terrain chunk source revision is required.");
        }
        if (metadata.identity.importSettings.pipeline.empty()) {
            validation.errors.push_back("Terrain chunk import settings pipeline is required.");
        }
        if (metadata.identity.importSettings.version.empty()) {
            validation.errors.push_back("Terrain chunk import settings version is required.");
        }
        if (metadata.identity.importSettings.optionsHash.empty()) {
            validation.errors.push_back("Terrain chunk import settings options hash is required.");
        }
        if (metadata.identity.chunkResolution < 2) {
            validation.errors.push_back("Terrain chunk resolution must be at least 2.");
        }
        if (!std::isfinite(metadata.identity.chunkSize) || metadata.identity.chunkSize <= 0.0f) {
            validation.errors.push_back("Terrain chunk size must be finite and positive.");
        }
        if (metadata.identityHash.empty()) {
            validation.errors.push_back("Terrain chunk identity hash is required.");
        } else {
            const std::string expectedHash =
                terrainChunkStableIdentityHash(metadata.identity, metadata.schemaVersion);
            if (metadata.identityHash != expectedHash) {
                validation.errors.push_back("Terrain chunk identity hash does not match metadata.");
            }
        }
        if (metadata.payloadFileName.empty()) {
            validation.errors.push_back("Terrain chunk payload file name is required.");
        }
        if (metadata.boundary.storesLiveRuntimeHandles) {
            validation.errors.push_back("Terrain chunk payloads must not store live runtime handles.");
        }

        if (metadata.boundary.storesRendererLodMeshes ||
            metadata.boundary.storesNavigationBuildData ||
            metadata.boundary.storesPhysicsColliderMeshes) {
            validation.warnings.push_back("Terrain chunk payload boundary includes derived data; derived payloads must remain disposable.");
        }
        if (metadata.role == TerrainSerializedChunkPayloadRole::DerivedCacheReference &&
            metadata.boundary.storesAuthoritativeHeights) {
            validation.warnings.push_back("Derived cache reference payload should not be the authoritative height source.");
        }
        if (metadata.role == TerrainSerializedChunkPayloadRole::SourceSnapshot &&
            !metadata.boundary.storesAuthoritativeHeights) {
            validation.warnings.push_back("Source snapshot payload does not contain authoritative heights.");
        }

        validation.valid = validation.errors.empty();
        return validation;
    }

    std::string terrainDerivedCacheSourceHash(
        const TerrainSerializedChunkFileMetadata& metadata)
    {
        std::ostringstream stream;
        stream << metadata.schemaVersion << '|'
               << metadata.payloadVersion << '|'
               << roleName(metadata.role) << '|'
               << metadata.identityHash << '|';
        appendBool(stream, metadata.boundary.storesAuthoritativeHeights);
        appendBool(stream, metadata.boundary.storesEditedHeightDeltas);
        appendBool(stream, metadata.boundary.storesMaterialOverrides);
        appendBool(stream, metadata.boundary.storesRendererLodMeshes);
        appendBool(stream, metadata.boundary.storesNavigationBuildData);
        appendBool(stream, metadata.boundary.storesPhysicsColliderMeshes);
        return hexHash(fnv1a(stream.str()));
    }
}
