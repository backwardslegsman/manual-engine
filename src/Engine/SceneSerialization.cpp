#include "Engine/SceneSerialization.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace Engine {
    namespace {
        constexpr uint64_t FnvOffset = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;
        constexpr uint32_t HeaderSize = 72;
        constexpr uint32_t DirectoryEntrySize = 56;
        constexpr uint32_t ChunkVersion = 1;

        using Bytes = std::vector<uint8_t>;

        [[nodiscard]] bool magicMatches(const SceneBinaryHeader& header)
        {
            constexpr std::array<char, 8> magic{'M', 'E', 'S', 'C', 'E', 'N', 'E', '\0'};
            return std::equal(std::begin(header.magic), std::end(header.magic), magic.begin(), magic.end());
        }

        [[nodiscard]] uint64_t fnv1a(const uint8_t* data, size_t size)
        {
            uint64_t hash = FnvOffset;
            for (size_t index = 0; index < size; ++index) {
                hash ^= data[index];
                hash *= FnvPrime;
            }
            return hash;
        }

        [[nodiscard]] uint64_t fnv1a(const Bytes& bytes)
        {
            return fnv1a(bytes.data(), bytes.size());
        }

        void writeU8(Bytes& bytes, uint8_t value)
        {
            bytes.push_back(value);
        }

        void writeU32(Bytes& bytes, uint32_t value)
        {
            for (uint32_t shift = 0; shift < 32; shift += 8) {
                bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
            }
        }

        void writeI32(Bytes& bytes, int32_t value)
        {
            writeU32(bytes, static_cast<uint32_t>(value));
        }

        void writeU64(Bytes& bytes, uint64_t value)
        {
            for (uint32_t shift = 0; shift < 64; shift += 8) {
                bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
            }
        }

        void writeF32(Bytes& bytes, float value)
        {
            static_assert(sizeof(float) == sizeof(uint32_t));
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(float));
            writeU32(bytes, bits);
        }

        void writeString(Bytes& bytes, const std::string& value)
        {
            writeU32(bytes, static_cast<uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        void writeVec3(Bytes& bytes, const glm::vec3& value)
        {
            writeF32(bytes, value.x);
            writeF32(bytes, value.y);
            writeF32(bytes, value.z);
        }

        void writeQuat(Bytes& bytes, const glm::quat& value)
        {
            writeF32(bytes, value.w);
            writeF32(bytes, value.x);
            writeF32(bytes, value.y);
            writeF32(bytes, value.z);
        }

        struct Reader {
            const Bytes& bytes;
            size_t offset = 0;
            SceneSerializationStatus status = SceneSerializationStatus::Success;
            std::string message;
            const SceneSerializationSettings& settings;

            [[nodiscard]] bool require(size_t count)
            {
                if (status != SceneSerializationStatus::Success) {
                    return false;
                }
                if (count > bytes.size() || offset > bytes.size() - count) {
                    status = SceneSerializationStatus::CorruptDirectory;
                    message = "Unexpected end of scene binary payload.";
                    return false;
                }
                return true;
            }

            [[nodiscard]] uint8_t u8()
            {
                if (!require(1)) {
                    return 0;
                }
                return bytes[offset++];
            }

            [[nodiscard]] uint32_t u32()
            {
                if (!require(4)) {
                    return 0;
                }
                uint32_t value = 0;
                for (uint32_t shift = 0; shift < 32; shift += 8) {
                    value |= static_cast<uint32_t>(bytes[offset++]) << shift;
                }
                return value;
            }

            [[nodiscard]] int32_t i32()
            {
                return static_cast<int32_t>(u32());
            }

            [[nodiscard]] uint64_t u64()
            {
                if (!require(8)) {
                    return 0;
                }
                uint64_t value = 0;
                for (uint32_t shift = 0; shift < 64; shift += 8) {
                    value |= static_cast<uint64_t>(bytes[offset++]) << shift;
                }
                return value;
            }

            [[nodiscard]] float f32()
            {
                const uint32_t bits = u32();
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(float));
                return value;
            }

            [[nodiscard]] std::string string()
            {
                const uint32_t size = u32();
                if (size > settings.maxStringBytes || !require(size)) {
                    status = SceneSerializationStatus::CorruptDirectory;
                    message = "Invalid string length in scene binary payload.";
                    return {};
                }
                std::string value(reinterpret_cast<const char*>(bytes.data() + offset), size);
                offset += size;
                return value;
            }

            [[nodiscard]] glm::vec3 vec3()
            {
                return {f32(), f32(), f32()};
            }

            [[nodiscard]] glm::quat quat()
            {
                return {f32(), f32(), f32(), f32()};
            }
        };

        [[nodiscard]] Bytes encodeHeader(SceneBinaryHeader header, bool zeroChecksum)
        {
            if (zeroChecksum) {
                header.headerChecksum = 0;
            }
            Bytes bytes;
            bytes.insert(bytes.end(), std::begin(header.magic), std::end(header.magic));
            writeU32(bytes, header.headerSize);
            writeU32(bytes, header.endianMarker);
            writeU32(bytes, header.formatVersion);
            writeU32(bytes, header.flags);
            writeU64(bytes, header.fileSize);
            writeU64(bytes, header.directoryOffset);
            writeU32(bytes, header.directoryCount);
            writeU64(bytes, header.schemaOffset);
            writeU32(bytes, header.schemaCount);
            writeU64(bytes, header.rootMetadataOffset);
            writeU64(bytes, header.headerChecksum);
            return bytes;
        }

        [[nodiscard]] Bytes encodeDirectoryEntry(const SceneBinaryChunkEntry& entry)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(entry.type));
            writeU32(bytes, entry.version);
            writeU32(bytes, static_cast<uint32_t>(entry.flags));
            writeU64(bytes, entry.offset);
            writeU64(bytes, entry.byteSize);
            writeU64(bytes, entry.uncompressedByteSize);
            writeU32(bytes, entry.recordCount);
            writeU64(bytes, entry.checksum);
            writeU32(bytes, entry.dependencyStart);
            writeU32(bytes, entry.dependencyCount);
            return bytes;
        }

        [[nodiscard]] SceneBinaryHeader decodeHeader(const Bytes& bytes, SceneSerializationStatus& status, std::string& message)
        {
            SceneSerializationSettings settings;
            Reader reader{bytes, 0, SceneSerializationStatus::Success, {}, settings};
            SceneBinaryHeader header;
            if (!reader.require(HeaderSize)) {
                status = SceneSerializationStatus::CorruptHeader;
                message = "Scene binary header is truncated.";
                return header;
            }
            for (char& character : header.magic) {
                character = static_cast<char>(reader.u8());
            }
            header.headerSize = reader.u32();
            header.endianMarker = reader.u32();
            header.formatVersion = reader.u32();
            header.flags = reader.u32();
            header.fileSize = reader.u64();
            header.directoryOffset = reader.u64();
            header.directoryCount = reader.u32();
            header.schemaOffset = reader.u64();
            header.schemaCount = reader.u32();
            header.rootMetadataOffset = reader.u64();
            header.headerChecksum = reader.u64();
            status = reader.status;
            message = reader.message;
            return header;
        }

        [[nodiscard]] SceneBinaryChunkEntry decodeDirectoryEntry(Reader& reader)
        {
            SceneBinaryChunkEntry entry;
            entry.type = static_cast<SceneBinaryChunkType>(reader.u32());
            entry.version = reader.u32();
            entry.flags = static_cast<SceneBinaryChunkFlags>(reader.u32());
            entry.offset = reader.u64();
            entry.byteSize = reader.u64();
            entry.uncompressedByteSize = reader.u64();
            entry.recordCount = reader.u32();
            entry.checksum = reader.u64();
            entry.dependencyStart = reader.u32();
            entry.dependencyCount = reader.u32();
            return entry;
        }

        [[nodiscard]] bool isKnownChunk(SceneBinaryChunkType type)
        {
            switch (type) {
                case SceneBinaryChunkType::SceneMetadata:
                case SceneBinaryChunkType::ReflectionSchema:
                case SceneBinaryChunkType::ActorTable:
                case SceneBinaryChunkType::ComponentTable:
                case SceneBinaryChunkType::AssetReferenceTable:
                case SceneBinaryChunkType::TerrainReferenceTable:
                case SceneBinaryChunkType::StringTable:
                case SceneBinaryChunkType::UserExtension:
                case SceneBinaryChunkType::ActorAuthoringMetadata:
                case SceneBinaryChunkType::ActorComponentAuthoringMetadata:
                    return true;
            }
            return false;
        }

        [[nodiscard]] Bytes encodeMetadata(const SceneSerializedScene& scene)
        {
            Bytes bytes;
            writeString(bytes, scene.formatVersion);
            writeU32(bytes, static_cast<uint32_t>(scene.actors.size()));
            writeU32(bytes, static_cast<uint32_t>(scene.components.size()));
            writeU32(bytes, static_cast<uint32_t>(scene.schema.size()));
            writeU32(bytes, static_cast<uint32_t>(scene.assets.size()));
            writeU32(bytes, static_cast<uint32_t>(scene.terrain.size()));
            return bytes;
        }

        [[nodiscard]] Bytes encodeSchema(const std::vector<SceneSerializedPropertySchema>& schema)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(schema.size()));
            for (const SceneSerializedPropertySchema& property : schema) {
                writeU32(bytes, property.objectId);
                writeU32(bytes, property.propertyId);
                writeString(bytes, property.name);
                writeU32(bytes, static_cast<uint32_t>(property.type));
                writeU32(bytes, static_cast<uint32_t>(property.flags));
            }
            return bytes;
        }

        [[nodiscard]] Bytes encodeActors(const std::vector<SceneSerializedActorRecord>& actors)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(actors.size()));
            for (const SceneSerializedActorRecord& actor : actors) {
                writeU64(bytes, actor.id.value);
                writeU8(bytes, actor.parent.has_value() ? 1u : 0u);
                writeU64(bytes, actor.parent ? actor.parent->value : 0u);
                writeVec3(bytes, actor.localTransform.translation);
                writeQuat(bytes, actor.localTransform.rotation);
                writeVec3(bytes, actor.localTransform.scale);
                writeU32(bytes, actor.order);
            }
            return bytes;
        }

        [[nodiscard]] Bytes encodeComponents(const std::vector<SceneSerializedComponentRecord>& components)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(components.size()));
            for (const SceneSerializedComponentRecord& component : components) {
                writeU64(bytes, component.owner.value);
                writeU32(bytes, component.type.value);
                writeU32(bytes, component.order);
            }
            return bytes;
        }

        [[nodiscard]] Bytes encodeAssets(const std::vector<SceneSerializedAssetReference>& assets)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(assets.size()));
            for (const SceneSerializedAssetReference& asset : assets) {
                writeU64(bytes, asset.id.value);
                writeU32(bytes, static_cast<uint32_t>(asset.type));
                writeString(bytes, asset.importSettings.pipeline);
                writeString(bytes, asset.importSettings.version);
                writeString(bytes, asset.importSettings.optionsHash);
                writeString(bytes, asset.sourcePath);
                writeString(bytes, asset.canonicalPath);
            }
            return bytes;
        }

        [[nodiscard]] uint32_t boundaryBits(const TerrainSerializedChunkPayloadBoundary& boundary)
        {
            return (boundary.storesAuthoritativeHeights ? 1u : 0u) |
                (boundary.storesEditedHeightDeltas ? 1u << 1 : 0u) |
                (boundary.storesMaterialOverrides ? 1u << 2 : 0u) |
                (boundary.storesRendererLodMeshes ? 1u << 3 : 0u) |
                (boundary.storesNavigationBuildData ? 1u << 4 : 0u) |
                (boundary.storesPhysicsColliderMeshes ? 1u << 5 : 0u) |
                (boundary.storesLiveRuntimeHandles ? 1u << 6 : 0u);
        }

        [[nodiscard]] TerrainSerializedChunkPayloadBoundary boundaryFromBits(uint32_t bits)
        {
            TerrainSerializedChunkPayloadBoundary boundary;
            boundary.storesAuthoritativeHeights = (bits & 1u) != 0u;
            boundary.storesEditedHeightDeltas = (bits & (1u << 1)) != 0u;
            boundary.storesMaterialOverrides = (bits & (1u << 2)) != 0u;
            boundary.storesRendererLodMeshes = (bits & (1u << 3)) != 0u;
            boundary.storesNavigationBuildData = (bits & (1u << 4)) != 0u;
            boundary.storesPhysicsColliderMeshes = (bits & (1u << 5)) != 0u;
            boundary.storesLiveRuntimeHandles = (bits & (1u << 6)) != 0u;
            return boundary;
        }

        [[nodiscard]] Bytes encodeTerrain(const std::vector<SceneSerializedTerrainReference>& terrain)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(terrain.size()));
            for (const SceneSerializedTerrainReference& reference : terrain) {
                const TerrainSerializedChunkFileMetadata& metadata = reference.metadata;
                writeString(bytes, metadata.schemaVersion);
                writeString(bytes, metadata.payloadVersion);
                writeU32(bytes, static_cast<uint32_t>(metadata.role));
                writeU64(bytes, metadata.identity.chunkId.source.value);
                writeI32(bytes, metadata.identity.chunkId.coord.x);
                writeI32(bytes, metadata.identity.chunkId.coord.z);
                writeU32(bytes, static_cast<uint32_t>(metadata.identity.sourceType));
                writeString(bytes, metadata.identity.importSettings.pipeline);
                writeString(bytes, metadata.identity.importSettings.version);
                writeString(bytes, metadata.identity.importSettings.optionsHash);
                writeString(bytes, metadata.identity.sourceRevision);
                writeString(bytes, metadata.identity.materialRevision);
                writeU32(bytes, metadata.identity.chunkResolution);
                writeF32(bytes, metadata.identity.chunkSize);
                writeU32(bytes, boundaryBits(metadata.boundary));
                writeString(bytes, metadata.identityHash);
                writeString(bytes, metadata.payloadFileName);
            }
            return bytes;
        }

        [[nodiscard]] Bytes encodeActorAuthoring(const std::vector<SceneSerializedActorAuthoringRecord>& metadata)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(metadata.size()));
            for (const SceneSerializedActorAuthoringRecord& record : metadata) {
                writeU64(bytes, record.metadata.actorId.value);
                writeString(bytes, record.metadata.displayName);
                writeString(bytes, record.metadata.layer);
                writeU32(bytes, static_cast<uint32_t>(record.metadata.tags.size()));
                for (const std::string& tag : record.metadata.tags) {
                    writeString(bytes, tag);
                }
            }
            return bytes;
        }

        [[nodiscard]] Bytes encodeActorComponents(const std::vector<SceneSerializedActorComponentRecord>& metadata)
        {
            Bytes bytes;
            writeU32(bytes, static_cast<uint32_t>(metadata.size()));
            for (const SceneSerializedActorComponentRecord& record : metadata) {
                writeU64(bytes, record.metadata.componentId.value);
                writeU64(bytes, record.metadata.ownerActorId.value);
                writeU32(bytes, record.metadata.componentType.value);
                writeString(bytes, record.metadata.displayName);
                writeU8(bytes, record.metadata.enabled ? 1u : 0u);
                writeU32(bytes, record.metadata.order);
            }
            return bytes;
        }

        [[nodiscard]] SceneSerializedScene decodeScene(
            const std::map<SceneBinaryChunkType, Bytes>& payloads,
            const SceneSerializationSettings& settings,
            SceneSerializationStatus& status,
            std::string& message)
        {
            SceneSerializedScene scene;
            if (const auto it = payloads.find(SceneBinaryChunkType::SceneMetadata); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                scene.formatVersion = reader.string();
                (void)reader.u32();
                (void)reader.u32();
                (void)reader.u32();
                (void)reader.u32();
                (void)reader.u32();
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }

            if (const auto it = payloads.find(SceneBinaryChunkType::ReflectionSchema); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedPropertySchema property;
                    property.objectId = reader.u32();
                    property.propertyId = reader.u32();
                    property.name = reader.string();
                    property.type = static_cast<ReflectedValueType>(reader.u32());
                    property.flags = static_cast<ReflectedPropertyFlag>(reader.u32());
                    scene.schema.push_back(std::move(property));
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }

            if (const auto it = payloads.find(SceneBinaryChunkType::ActorTable); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedActorRecord actor;
                    actor.id = {reader.u64()};
                    const bool hasParent = reader.u8() != 0;
                    const SceneObjectId parent{reader.u64()};
                    if (hasParent) {
                        actor.parent = parent;
                    }
                    actor.localTransform.translation = reader.vec3();
                    actor.localTransform.rotation = reader.quat();
                    actor.localTransform.scale = reader.vec3();
                    actor.order = reader.u32();
                    scene.actors.push_back(actor);
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }

            if (const auto it = payloads.find(SceneBinaryChunkType::ComponentTable); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedComponentRecord component;
                    component.owner = {reader.u64()};
                    component.type = {reader.u32()};
                    component.order = reader.u32();
                    scene.components.push_back(component);
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }

            if (const auto it = payloads.find(SceneBinaryChunkType::AssetReferenceTable); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedAssetReference asset;
                    asset.id = {reader.u64()};
                    asset.type = static_cast<AssetType>(reader.u32());
                    asset.importSettings.pipeline = reader.string();
                    asset.importSettings.version = reader.string();
                    asset.importSettings.optionsHash = reader.string();
                    asset.sourcePath = reader.string();
                    asset.canonicalPath = reader.string();
                    scene.assets.push_back(std::move(asset));
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }

            if (const auto it = payloads.find(SceneBinaryChunkType::TerrainReferenceTable); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedTerrainReference reference;
                    TerrainSerializedChunkFileMetadata& metadata = reference.metadata;
                    metadata.schemaVersion = reader.string();
                    metadata.payloadVersion = reader.string();
                    metadata.role = static_cast<TerrainSerializedChunkPayloadRole>(reader.u32());
                    metadata.identity.chunkId.source = {reader.u64()};
                    metadata.identity.chunkId.coord.x = reader.i32();
                    metadata.identity.chunkId.coord.z = reader.i32();
                    metadata.identity.sourceType = static_cast<TerrainDatasetSourceType>(reader.u32());
                    metadata.identity.importSettings.pipeline = reader.string();
                    metadata.identity.importSettings.version = reader.string();
                    metadata.identity.importSettings.optionsHash = reader.string();
                    metadata.identity.sourceRevision = reader.string();
                    metadata.identity.materialRevision = reader.string();
                    metadata.identity.chunkResolution = reader.u32();
                    metadata.identity.chunkSize = reader.f32();
                    metadata.boundary = boundaryFromBits(reader.u32());
                    metadata.identityHash = reader.string();
                    metadata.payloadFileName = reader.string();
                    scene.terrain.push_back(std::move(reference));
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }
            if (const auto it = payloads.find(SceneBinaryChunkType::ActorAuthoringMetadata); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedActorAuthoringRecord record;
                    record.metadata.actorId = {reader.u64()};
                    record.metadata.displayName = reader.string();
                    record.metadata.layer = reader.string();
                    const uint32_t tagCount = reader.u32();
                    for (uint32_t tagIndex = 0; tagIndex < tagCount; ++tagIndex) {
                        record.metadata.tags.push_back(reader.string());
                    }
                    scene.actorAuthoring.push_back(std::move(record));
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }
            if (const auto it = payloads.find(SceneBinaryChunkType::ActorComponentAuthoringMetadata); it != payloads.end()) {
                Reader reader{it->second, 0, SceneSerializationStatus::Success, {}, settings};
                const uint32_t count = reader.u32();
                for (uint32_t index = 0; index < count; ++index) {
                    SceneSerializedActorComponentRecord record;
                    record.metadata.componentId = {reader.u64()};
                    record.metadata.ownerActorId = {reader.u64()};
                    record.metadata.componentType = {reader.u32()};
                    record.metadata.displayName = reader.string();
                    record.metadata.enabled = reader.u8() != 0;
                    record.metadata.order = reader.u32();
                    scene.actorComponents.push_back(std::move(record));
                }
                if (reader.status != SceneSerializationStatus::Success) {
                    status = reader.status;
                    message = reader.message;
                    return {};
                }
            }
            return scene;
        }

        [[nodiscard]] bool serializableProperty(const ReflectedPropertyDescriptor& property)
        {
            return hasFlag(property.flags, ReflectedPropertyFlag::Serializable) &&
                !hasFlag(property.flags, ReflectedPropertyFlag::RuntimeOnly) &&
                !hasFlag(property.flags, ReflectedPropertyFlag::Transient) &&
                property.type != ReflectedValueType::OpaqueHandle;
        }

        [[nodiscard]] SceneSerializationDiagnostics makeDiagnostics(const SceneSerializedScene& scene)
        {
            SceneSerializationDiagnostics diagnostics;
            diagnostics.actorCount = static_cast<uint32_t>(scene.actors.size());
            diagnostics.componentCount = static_cast<uint32_t>(scene.components.size());
            diagnostics.schemaPropertyCount = static_cast<uint32_t>(scene.schema.size());
            diagnostics.assetReferenceCount = static_cast<uint32_t>(scene.assets.size());
            diagnostics.terrainReferenceCount = static_cast<uint32_t>(scene.terrain.size());
            diagnostics.actorAuthoringCount = static_cast<uint32_t>(scene.actorAuthoring.size());
            diagnostics.actorComponentAuthoringCount = static_cast<uint32_t>(scene.actorComponents.size());
            return diagnostics;
        }

        void validateDirectory(
            const SceneBinaryHeader& header,
            const std::vector<SceneBinaryChunkEntry>& directory,
            const SceneSerializationSettings& settings,
            SceneSerializationHeaderReadResult& result)
        {
            if (!magicMatches(header)) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary magic mismatch.";
                return;
            }
            if (header.headerSize != HeaderSize) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary header size mismatch.";
                return;
            }
            if (header.endianMarker != SceneBinaryEndianMarker) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary endian marker mismatch.";
                return;
            }
            if (header.formatVersion != settings.formatVersion) {
                result.status = SceneSerializationStatus::UnsupportedVersion;
                result.message = "Scene binary format version is unsupported.";
                return;
            }
            if (header.fileSize > settings.maxFileBytes) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary file exceeds configured limit.";
                return;
            }
            if (header.directoryOffset != HeaderSize) {
                result.status = SceneSerializationStatus::CorruptDirectory;
                result.message = "Scene binary directory offset is invalid.";
                return;
            }
            if (directory.size() != header.directoryCount) {
                result.status = SceneSerializationStatus::CorruptDirectory;
                result.message = "Scene binary directory count mismatch.";
                return;
            }

            std::vector<std::pair<uint64_t, uint64_t>> ranges;
            bool hasMetadata = false;
            bool hasSchema = false;
            bool hasActors = false;
            bool hasComponents = false;
            for (const SceneBinaryChunkEntry& entry : directory) {
                if (!isKnownChunk(entry.type)) {
                    if (hasFlag(entry.flags, SceneBinaryChunkFlags::Required)) {
                        result.status = SceneSerializationStatus::UnknownRequiredChunk;
                        result.message = "Scene binary contains unknown required chunk.";
                        return;
                    }
                    ++result.diagnostics.unknownOptionalChunkCount;
                    continue;
                }
                if (entry.byteSize > settings.maxChunkBytes ||
                    entry.offset < HeaderSize + static_cast<uint64_t>(header.directoryCount) * DirectoryEntrySize ||
                    entry.offset > header.fileSize ||
                    entry.byteSize > header.fileSize - entry.offset) {
                    result.status = SceneSerializationStatus::CorruptDirectory;
                    result.message = "Scene binary chunk range is invalid.";
                    return;
                }
                ranges.push_back({entry.offset, entry.offset + entry.byteSize});
                hasMetadata = hasMetadata || entry.type == SceneBinaryChunkType::SceneMetadata;
                hasSchema = hasSchema || entry.type == SceneBinaryChunkType::ReflectionSchema;
                hasActors = hasActors || entry.type == SceneBinaryChunkType::ActorTable;
                hasComponents = hasComponents || entry.type == SceneBinaryChunkType::ComponentTable;
            }
            std::ranges::sort(ranges);
            for (size_t index = 1; index < ranges.size(); ++index) {
                if (ranges[index].first < ranges[index - 1].second) {
                    result.status = SceneSerializationStatus::CorruptDirectory;
                    result.message = "Scene binary chunks overlap.";
                    return;
                }
            }
            if (!hasMetadata || !hasSchema || !hasActors || !hasComponents) {
                result.status = SceneSerializationStatus::MissingRequiredChunk;
                result.message = "Scene binary is missing one or more required chunks.";
                return;
            }
        }

        [[nodiscard]] std::vector<SceneBinaryChunkEntry> readDirectory(
            const Bytes& bytes,
            const SceneBinaryHeader& header,
            const SceneSerializationSettings& settings,
            SceneSerializationStatus& status,
            std::string& message)
        {
            std::vector<SceneBinaryChunkEntry> directory;
            Reader reader{bytes, static_cast<size_t>(header.directoryOffset), SceneSerializationStatus::Success, {}, settings};
            for (uint32_t index = 0; index < header.directoryCount; ++index) {
                directory.push_back(decodeDirectoryEntry(reader));
            }
            status = reader.status;
            message = reader.message;
            return directory;
        }

        [[nodiscard]] SceneSerializationHeaderReadResult readHeaderFromBytes(
            const Bytes& bytes,
            const std::filesystem::path& path,
            const SceneSerializationSettings& settings)
        {
            SceneSerializationHeaderReadResult result;
            result.path = path;
            SceneSerializationStatus status = SceneSerializationStatus::Success;
            std::string message;
            if (bytes.size() < HeaderSize) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary header is truncated.";
                return result;
            }
            result.header = decodeHeader(bytes, status, message);
            if (status != SceneSerializationStatus::Success) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = message;
                return result;
            }
            if (!magicMatches(result.header)) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary magic mismatch.";
                return result;
            }
            if (result.header.headerSize != HeaderSize) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary header size mismatch.";
                return result;
            }
            if (result.header.endianMarker != SceneBinaryEndianMarker) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary endian marker mismatch.";
                return result;
            }
            if (result.header.formatVersion != settings.formatVersion) {
                result.status = SceneSerializationStatus::UnsupportedVersion;
                result.message = "Scene binary format version is unsupported.";
                return result;
            }
            if (result.header.fileSize != bytes.size()) {
                result.status = SceneSerializationStatus::CorruptHeader;
                result.message = "Scene binary file size mismatch.";
                return result;
            }
            if (settings.enableChecksums) {
                Bytes headerBytes = encodeHeader(result.header, true);
                if (fnv1a(headerBytes) != result.header.headerChecksum) {
                    result.status = SceneSerializationStatus::ChecksumMismatch;
                    result.message = "Scene binary header checksum mismatch.";
                    return result;
                }
            }
            if (result.header.directoryOffset + static_cast<uint64_t>(result.header.directoryCount) * DirectoryEntrySize > bytes.size()) {
                result.status = SceneSerializationStatus::CorruptDirectory;
                result.message = "Scene binary directory is truncated.";
                return result;
            }
            result.directory = readDirectory(bytes, result.header, settings, status, message);
            if (status != SceneSerializationStatus::Success) {
                result.status = SceneSerializationStatus::CorruptDirectory;
                result.message = message;
                return result;
            }
            validateDirectory(result.header, result.directory, settings, result);
            return result;
        }

        [[nodiscard]] Bytes readFile(const std::filesystem::path& path, SceneSerializationStatus& status, std::string& message)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                status = SceneSerializationStatus::IoError;
                message = "Failed to open scene binary for reading.";
                return {};
            }
            file.seekg(0, std::ios::end);
            const std::streamoff size = file.tellg();
            if (size < 0) {
                status = SceneSerializationStatus::IoError;
                message = "Failed to inspect scene binary size.";
                return {};
            }
            file.seekg(0, std::ios::beg);
            Bytes bytes(static_cast<size_t>(size));
            if (!bytes.empty()) {
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            if (!file && !file.eof()) {
                status = SceneSerializationStatus::IoError;
                message = "Failed to read scene binary.";
                return {};
            }
            return bytes;
        }

        [[nodiscard]] uint64_t generatedIdForOrder(uint32_t order)
        {
            return 0x8000000000000000ull | static_cast<uint64_t>(order + 1u);
        }
    }

    SceneSerializedScene buildSerializedScene(
        Scene& scene,
        const ReflectionRegistry& registry,
        const SceneSerializationSettings& settings)
    {
        SceneSerializedScene serialized;

        uint32_t skipped = 0;
        for (const ReflectedObjectDescriptor& object : registry.objects()) {
            for (const ReflectedPropertyDescriptor& property : object.properties) {
                if (serializableProperty(property)) {
                    serialized.schema.push_back({
                        object.id,
                        property.id,
                        property.name,
                        property.type,
                        property.flags,
                    });
                } else {
                    ++skipped;
                }
            }
        }
        (void)skipped;

        uint32_t order = 0;
        std::map<uint32_t, SceneObjectId> generatedByIndex;
        scene.forEachActor([&](SceneActorHandle actor) {
            SceneObjectId id = scene.stableId(actor).value_or(SceneObjectId{});
            if (!isValid(id) && settings.allowGeneratedSceneObjectIds) {
                id = {generatedIdForOrder(order)};
                generatedByIndex[actor.index] = id;
            }

            SceneSerializedActorRecord record;
            record.id = id;
            record.localTransform = scene.localTransform(actor).value_or(SceneTransform{});
            record.order = order++;
            if (const std::optional<SceneActorHandle> parent = scene.parent(actor)) {
                SceneObjectId parentId = scene.stableId(*parent).value_or(SceneObjectId{});
                if (!isValid(parentId) && settings.allowGeneratedSceneObjectIds) {
                    if (const auto it = generatedByIndex.find(parent->index); it != generatedByIndex.end()) {
                        parentId = it->second;
                    }
                }
                record.parent = parentId;
            }
            serialized.actors.push_back(record);

            uint32_t componentOrder = 0;
            for (SceneComponentHandle component : scene.components(actor)) {
                const std::optional<SceneComponentTypeId> type = scene.componentType(component);
                if (type) {
                    serialized.components.push_back({record.id, *type, componentOrder++});
                }
            }
        });

        std::ranges::sort(serialized.actors, [](const auto& lhs, const auto& rhs) {
            if (lhs.id.value != rhs.id.value) {
                return lhs.id.value < rhs.id.value;
            }
            return lhs.order < rhs.order;
        });
        std::ranges::sort(serialized.components, [](const auto& lhs, const auto& rhs) {
            if (lhs.owner.value != rhs.owner.value) {
                return lhs.owner.value < rhs.owner.value;
            }
            if (lhs.type.value != rhs.type.value) {
                return lhs.type.value < rhs.type.value;
            }
            return lhs.order < rhs.order;
        });
        return serialized;
    }

    SceneSerializedScene buildSerializedScene(
        Scene& scene,
        const ActorAuthoringStore& actorAuthoring,
        const ReflectionRegistry& registry,
        const SceneSerializationSettings& settings)
    {
        SceneSerializedScene serialized = buildSerializedScene(scene, registry, settings);
        std::set<uint64_t> actorIds;
        for (const SceneSerializedActorRecord& actor : serialized.actors) {
            if (isValid(actor.id)) {
                actorIds.insert(actor.id.value);
            }
        }
        for (const ActorAuthoringRecord& record : actorAuthoring.records()) {
            if (actorIds.contains(record.actorId.value)) {
                serialized.actorAuthoring.push_back({record});
            }
        }
        std::ranges::sort(serialized.actorAuthoring, [](const auto& lhs, const auto& rhs) {
            return lhs.metadata.actorId.value < rhs.metadata.actorId.value;
        });
        return serialized;
    }

    SceneSerializedScene buildSerializedScene(
        Scene& scene,
        const ActorAuthoringStore& actorAuthoring,
        const ActorComponentDescriptorStore& actorComponents,
        const ReflectionRegistry& registry,
        const SceneSerializationSettings& settings)
    {
        SceneSerializedScene serialized = buildSerializedScene(scene, actorAuthoring, registry, settings);
        std::set<uint64_t> actorIds;
        for (const SceneSerializedActorRecord& actor : serialized.actors) {
            if (isValid(actor.id)) {
                actorIds.insert(actor.id.value);
            }
        }
        for (const ActorComponentInstanceRecord& record : actorComponents.records()) {
            if (actorIds.contains(record.ownerActorId.value)) {
                serialized.actorComponents.push_back({record});
            }
        }
        std::ranges::sort(serialized.actorComponents, [](const auto& lhs, const auto& rhs) {
            if (lhs.metadata.ownerActorId.value != rhs.metadata.ownerActorId.value) {
                return lhs.metadata.ownerActorId.value < rhs.metadata.ownerActorId.value;
            }
            if (lhs.metadata.order != rhs.metadata.order) {
                return lhs.metadata.order < rhs.metadata.order;
            }
            if (lhs.metadata.componentType.value != rhs.metadata.componentType.value) {
                return lhs.metadata.componentType.value < rhs.metadata.componentType.value;
            }
            return lhs.metadata.componentId.value < rhs.metadata.componentId.value;
        });
        return serialized;
    }

    SceneSerializationDiagnostics validateSerializedScene(
        const SceneSerializedScene& scene,
        const ReflectionRegistry&,
        const SceneSerializationSettings&)
    {
        SceneSerializationDiagnostics diagnostics = makeDiagnostics(scene);
        std::set<uint64_t> actorIds;
        for (const SceneSerializedActorRecord& actor : scene.actors) {
            if (!isValid(actor.id)) {
                diagnostics.errors.push_back("Serialized actor is missing a stable SceneObjectId.");
                continue;
            }
            if (!actorIds.insert(actor.id.value).second) {
                diagnostics.errors.push_back("Serialized scene contains duplicate SceneObjectId.");
            }
        }
        for (const SceneSerializedActorRecord& actor : scene.actors) {
            if (actor.parent && (!isValid(*actor.parent) || !actorIds.contains(actor.parent->value))) {
                diagnostics.errors.push_back("Serialized actor references a missing parent SceneObjectId.");
            }
        }
        for (const SceneSerializedComponentRecord& component : scene.components) {
            if (!isValid(component.owner) || !actorIds.contains(component.owner.value)) {
                diagnostics.errors.push_back("Serialized component references a missing owner SceneObjectId.");
            }
            if (!isValid(component.type)) {
                diagnostics.errors.push_back("Serialized component has an invalid SceneComponentTypeId.");
            }
        }
        for (const SceneSerializedAssetReference& asset : scene.assets) {
            if (!isValid(asset.id)) {
                diagnostics.errors.push_back("Serialized asset reference has an invalid AssetId.");
            }
        }
        for (const SceneSerializedTerrainReference& terrain : scene.terrain) {
            const TerrainSerializationPrepValidation validation =
                validateTerrainSerializedChunkFileMetadata(terrain.metadata);
            if (!validation.valid) {
                diagnostics.errors.insert(diagnostics.errors.end(), validation.errors.begin(), validation.errors.end());
            }
            diagnostics.warnings.insert(diagnostics.warnings.end(), validation.warnings.begin(), validation.warnings.end());
            if (terrain.metadata.boundary.storesLiveRuntimeHandles) {
                diagnostics.errors.push_back("Serialized terrain reference boundary includes live runtime handles.");
            }
        }
        for (const SceneSerializedPropertySchema& property : scene.schema) {
            if (property.type == ReflectedValueType::OpaqueHandle) {
                diagnostics.errors.push_back("Reflection schema contains serializable OpaqueHandle property.");
            }
        }
        std::set<uint64_t> metadataActorIds;
        for (const SceneSerializedActorAuthoringRecord& record : scene.actorAuthoring) {
            if (!isValid(record.metadata.actorId) || !actorIds.contains(record.metadata.actorId.value)) {
                diagnostics.errors.push_back("Actor authoring metadata references a missing SceneObjectId.");
            }
            if (isValid(record.metadata.actorId) && !metadataActorIds.insert(record.metadata.actorId.value).second) {
                diagnostics.errors.push_back("Actor authoring metadata contains duplicate SceneObjectId records.");
            }
            const ActorAuthoringValidationResult validation = validateActorAuthoringRecord(record.metadata);
            diagnostics.errors.insert(diagnostics.errors.end(), validation.errors.begin(), validation.errors.end());
            diagnostics.warnings.insert(diagnostics.warnings.end(), validation.warnings.begin(), validation.warnings.end());
        }
        std::set<uint64_t> authoredComponentIds;
        for (const SceneSerializedActorComponentRecord& record : scene.actorComponents) {
            if (!isValid(record.metadata.ownerActorId) || !actorIds.contains(record.metadata.ownerActorId.value)) {
                diagnostics.errors.push_back("Authored component metadata references a missing SceneObjectId.");
            }
            if (isValid(record.metadata.componentId) &&
                !authoredComponentIds.insert(record.metadata.componentId.value).second) {
                diagnostics.errors.push_back("Authored component metadata contains duplicate ActorComponentId records.");
            }
            const ActorComponentValidationResult validation =
                validateActorComponentInstanceRecord(record.metadata);
            diagnostics.errors.insert(diagnostics.errors.end(), validation.errors.begin(), validation.errors.end());
            diagnostics.warnings.insert(diagnostics.warnings.end(), validation.warnings.begin(), validation.warnings.end());
        }
        return diagnostics;
    }

    SceneSerializationDiagnostics validateSerializedScene(
        const SceneSerializedScene& scene,
        const ReflectionRegistry& registry,
        const ActorComponentDescriptorRegistry& componentRegistry,
        const SceneSerializationSettings& settings)
    {
        SceneSerializationDiagnostics diagnostics = validateSerializedScene(scene, registry, settings);
        for (const SceneSerializedActorComponentRecord& record : scene.actorComponents) {
            const std::optional<ActorComponentTypeDescriptor> descriptor =
                componentRegistry.descriptor(record.metadata.componentType);
            if (!descriptor) {
                diagnostics.errors.push_back("Authored component metadata references an unregistered component type.");
                continue;
            }
            if (descriptor->validateInstance) {
                const ActorComponentValidationResult validation = descriptor->validateInstance(record.metadata);
                diagnostics.errors.insert(diagnostics.errors.end(), validation.errors.begin(), validation.errors.end());
                diagnostics.warnings.insert(
                    diagnostics.warnings.end(),
                    validation.warnings.begin(),
                    validation.warnings.end());
            }
        }
        return diagnostics;
    }

    SceneSerializationWriteResult writeSceneBinary(
        const std::filesystem::path& path,
        const SceneSerializedScene& scene,
        const SceneSerializationSettings& settings)
    {
        SceneSerializationWriteResult result;
        result.path = path;
        result.diagnostics = validateSerializedScene(scene, ReflectionRegistry{}, settings);
        if (!result.diagnostics.errors.empty()) {
            result.status = SceneSerializationStatus::InvalidInput;
            result.message = result.diagnostics.errors.front();
            return result;
        }

        struct ChunkPayload {
            SceneBinaryChunkType type;
            SceneBinaryChunkFlags flags;
            uint32_t recordCount;
            Bytes bytes;
        };
        std::vector<ChunkPayload> payloads{
            {SceneBinaryChunkType::SceneMetadata, SceneBinaryChunkFlags::Required, 1, encodeMetadata(scene)},
            {SceneBinaryChunkType::ReflectionSchema, SceneBinaryChunkFlags::Required, static_cast<uint32_t>(scene.schema.size()), encodeSchema(scene.schema)},
            {SceneBinaryChunkType::ActorTable, SceneBinaryChunkFlags::Required, static_cast<uint32_t>(scene.actors.size()), encodeActors(scene.actors)},
            {SceneBinaryChunkType::ComponentTable, SceneBinaryChunkFlags::Required, static_cast<uint32_t>(scene.components.size()), encodeComponents(scene.components)},
            {SceneBinaryChunkType::AssetReferenceTable, SceneBinaryChunkFlags::Optional, static_cast<uint32_t>(scene.assets.size()), encodeAssets(scene.assets)},
            {SceneBinaryChunkType::TerrainReferenceTable, SceneBinaryChunkFlags::Optional, static_cast<uint32_t>(scene.terrain.size()), encodeTerrain(scene.terrain)},
        };
        if (!scene.actorAuthoring.empty()) {
            payloads.push_back({
                SceneBinaryChunkType::ActorAuthoringMetadata,
                SceneBinaryChunkFlags::Optional,
                static_cast<uint32_t>(scene.actorAuthoring.size()),
                encodeActorAuthoring(scene.actorAuthoring),
            });
        }
        if (!scene.actorComponents.empty()) {
            payloads.push_back({
                SceneBinaryChunkType::ActorComponentAuthoringMetadata,
                SceneBinaryChunkFlags::Optional,
                static_cast<uint32_t>(scene.actorComponents.size()),
                encodeActorComponents(scene.actorComponents),
            });
        }

        SceneBinaryHeader header;
        header.headerSize = HeaderSize;
        header.formatVersion = settings.formatVersion;
        header.flags = settings.enableChecksums ? 1u : 0u;
        header.directoryOffset = HeaderSize;
        header.directoryCount = static_cast<uint32_t>(payloads.size());
        header.schemaCount = static_cast<uint32_t>(scene.schema.size());
        uint64_t offset = HeaderSize + static_cast<uint64_t>(payloads.size()) * DirectoryEntrySize;
        for (const ChunkPayload& payload : payloads) {
            SceneBinaryChunkEntry entry;
            entry.type = payload.type;
            entry.version = ChunkVersion;
            entry.flags = payload.flags;
            entry.offset = offset;
            entry.byteSize = payload.bytes.size();
            entry.uncompressedByteSize = payload.bytes.size();
            entry.recordCount = payload.recordCount;
            entry.checksum = settings.enableChecksums ? fnv1a(payload.bytes) : 0;
            result.directory.push_back(entry);
            if (payload.type == SceneBinaryChunkType::ReflectionSchema) {
                header.schemaOffset = offset;
            } else if (payload.type == SceneBinaryChunkType::SceneMetadata) {
                header.rootMetadataOffset = offset;
            }
            offset += payload.bytes.size();
        }
        header.fileSize = offset;
        header.headerChecksum = settings.enableChecksums ? fnv1a(encodeHeader(header, true)) : 0;
        result.header = header;

        Bytes fileBytes = encodeHeader(header, false);
        for (const SceneBinaryChunkEntry& entry : result.directory) {
            Bytes encoded = encodeDirectoryEntry(entry);
            fileBytes.insert(fileBytes.end(), encoded.begin(), encoded.end());
        }
        for (const ChunkPayload& payload : payloads) {
            fileBytes.insert(fileBytes.end(), payload.bytes.begin(), payload.bytes.end());
        }

        const auto parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath);
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.status = SceneSerializationStatus::IoError;
            result.message = "Failed to open scene binary for writing.";
            return result;
        }
        output.write(reinterpret_cast<const char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
        if (!output) {
            result.status = SceneSerializationStatus::IoError;
            result.message = "Failed to write scene binary.";
            return result;
        }
        return result;
    }

    SceneSerializationHeaderReadResult readSceneBinaryHeader(
        const std::filesystem::path& path,
        const SceneSerializationSettings& settings)
    {
        SceneSerializationHeaderReadResult result;
        result.path = path;
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            result.status = SceneSerializationStatus::IoError;
            result.message = "Failed to open scene binary for reading.";
            return result;
        }

        std::error_code error;
        const uint64_t actualFileSize = std::filesystem::file_size(path, error);
        if (error) {
            result.status = SceneSerializationStatus::IoError;
            result.message = "Failed to inspect scene binary size.";
            return result;
        }
        if (actualFileSize < HeaderSize) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = "Scene binary header is truncated.";
            return result;
        }

        Bytes headerBytes(HeaderSize);
        file.read(reinterpret_cast<char*>(headerBytes.data()), HeaderSize);
        if (!file) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = "Failed to read scene binary header.";
            return result;
        }

        SceneSerializationStatus status = SceneSerializationStatus::Success;
        std::string message;
        result.header = decodeHeader(headerBytes, status, message);
        if (status != SceneSerializationStatus::Success) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = message;
            return result;
        }
        if (!magicMatches(result.header)) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = "Scene binary magic mismatch.";
            return result;
        }
        if (result.header.headerSize != HeaderSize) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = "Scene binary header size mismatch.";
            return result;
        }
        if (result.header.endianMarker != SceneBinaryEndianMarker) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = "Scene binary endian marker mismatch.";
            return result;
        }
        if (result.header.formatVersion != settings.formatVersion) {
            result.status = SceneSerializationStatus::UnsupportedVersion;
            result.message = "Scene binary format version is unsupported.";
            return result;
        }
        if (result.header.fileSize != actualFileSize) {
            result.status = SceneSerializationStatus::CorruptHeader;
            result.message = "Scene binary file size mismatch.";
            return result;
        }
        if (settings.enableChecksums && fnv1a(encodeHeader(result.header, true)) != result.header.headerChecksum) {
            result.status = SceneSerializationStatus::ChecksumMismatch;
            result.message = "Scene binary header checksum mismatch.";
            return result;
        }
        const uint64_t directoryBytes = static_cast<uint64_t>(result.header.directoryCount) * DirectoryEntrySize;
        if (result.header.directoryOffset + directoryBytes > actualFileSize) {
            result.status = SceneSerializationStatus::CorruptDirectory;
            result.message = "Scene binary directory is truncated.";
            return result;
        }
        Bytes directoryPayload(static_cast<size_t>(directoryBytes));
        file.seekg(static_cast<std::streamoff>(result.header.directoryOffset), std::ios::beg);
        if (!directoryPayload.empty()) {
            file.read(reinterpret_cast<char*>(directoryPayload.data()), static_cast<std::streamsize>(directoryPayload.size()));
        }
        if (!file) {
            result.status = SceneSerializationStatus::CorruptDirectory;
            result.message = "Failed to read scene binary directory.";
            return result;
        }
        Reader reader{directoryPayload, 0, SceneSerializationStatus::Success, {}, settings};
        for (uint32_t index = 0; index < result.header.directoryCount; ++index) {
            result.directory.push_back(decodeDirectoryEntry(reader));
        }
        if (reader.status != SceneSerializationStatus::Success) {
            result.status = SceneSerializationStatus::CorruptDirectory;
            result.message = reader.message;
            return result;
        }
        validateDirectory(result.header, result.directory, settings, result);
        return result;
    }

    SceneSerializationReadResult readSceneBinary(
        const std::filesystem::path& path,
        const SceneSerializationSettings& settings)
    {
        SceneSerializationReadResult result;
        result.path = path;
        SceneSerializationStatus status = SceneSerializationStatus::Success;
        std::string message;
        const Bytes bytes = readFile(path, status, message);
        if (status != SceneSerializationStatus::Success) {
            result.status = status;
            result.message = message;
            return result;
        }
        SceneSerializationHeaderReadResult header = readHeaderFromBytes(bytes, path, settings);
        result.header = header.header;
        result.directory = header.directory;
        result.diagnostics = header.diagnostics;
        if (header.status != SceneSerializationStatus::Success) {
            result.status = header.status;
            result.message = header.message;
            return result;
        }

        std::map<SceneBinaryChunkType, Bytes> payloads;
        for (const SceneBinaryChunkEntry& entry : result.directory) {
            if (!isKnownChunk(entry.type)) {
                continue;
            }
            Bytes payload(
                bytes.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.byteSize));
            if (settings.enableChecksums && fnv1a(payload) != entry.checksum) {
                result.status = SceneSerializationStatus::ChecksumMismatch;
                result.message = "Scene binary chunk checksum mismatch.";
                return result;
            }
            payloads[entry.type] = std::move(payload);
        }
        result.scene = decodeScene(payloads, settings, status, message);
        if (status != SceneSerializationStatus::Success) {
            result.status = status;
            result.message = message;
            return result;
        }
        result.diagnostics = validateSerializedScene(result.scene, ReflectionRegistry{}, settings);
        if (!result.diagnostics.errors.empty()) {
            result.status = SceneSerializationStatus::InvalidReference;
            result.message = result.diagnostics.errors.front();
        }
        return result;
    }

    SceneSerializationStatus applySerializedScene(
        Scene& scene,
        const SceneSerializedScene& serialized,
        const SceneSerializationLoadContext& context,
        const SceneSerializationSettings& settings)
    {
        if (context.clearExistingScene) {
            return SceneSerializationStatus::InvalidInput;
        }
        const SceneSerializationDiagnostics diagnostics = validateSerializedScene(serialized, ReflectionRegistry{}, settings);
        if (!diagnostics.errors.empty()) {
            return SceneSerializationStatus::InvalidReference;
        }

        std::map<uint64_t, SceneActorHandle> actorById;
        std::vector<SceneSerializedActorRecord> actors = serialized.actors;
        std::ranges::sort(actors, [](const auto& lhs, const auto& rhs) {
            return lhs.order < rhs.order;
        });
        for (const SceneSerializedActorRecord& actor : actors) {
            SceneActorHandle handle = scene.createActor(actor.id);
            if (!scene.setLocalTransform(handle, actor.localTransform)) {
                return SceneSerializationStatus::InvalidReference;
            }
            actorById[actor.id.value] = handle;
        }
        for (const SceneSerializedActorRecord& actor : actors) {
            if (!actor.parent) {
                continue;
            }
            const auto childIt = actorById.find(actor.id.value);
            const auto parentIt = actorById.find(actor.parent->value);
            if (childIt == actorById.end() || parentIt == actorById.end()) {
                return SceneSerializationStatus::InvalidReference;
            }
            if (scene.attachChild(childIt->second, parentIt->second, false) != SceneTransformUpdateResult::Success) {
                return SceneSerializationStatus::InvalidReference;
            }
        }
        for (const SceneSerializedComponentRecord& component : serialized.components) {
            const auto ownerIt = actorById.find(component.owner.value);
            if (ownerIt == actorById.end() || !isValid(component.type)) {
                return SceneSerializationStatus::InvalidReference;
            }
            if (!isValid(scene.attachComponent(ownerIt->second, component.type))) {
                return SceneSerializationStatus::InvalidReference;
            }
        }
        return SceneSerializationStatus::Success;
    }

    SceneSerializationStatus applySerializedScene(
        Scene& scene,
        ActorAuthoringStore& actorAuthoring,
        const SceneSerializedScene& serialized,
        const SceneSerializationLoadContext& context,
        const SceneSerializationSettings& settings)
    {
        const SceneSerializationDiagnostics diagnostics = validateSerializedScene(serialized, ReflectionRegistry{}, settings);
        if (!diagnostics.errors.empty()) {
            return SceneSerializationStatus::InvalidReference;
        }

        const SceneSerializationStatus sceneStatus = applySerializedScene(scene, serialized, context, settings);
        if (sceneStatus != SceneSerializationStatus::Success) {
            return sceneStatus;
        }

        actorAuthoring.clear();
        for (const SceneSerializedActorAuthoringRecord& record : serialized.actorAuthoring) {
            if (actorAuthoring.upsert(record.metadata) != ActorAuthoringStatus::Success) {
                return SceneSerializationStatus::InvalidReference;
            }
        }
        return SceneSerializationStatus::Success;
    }

    SceneSerializationStatus applySerializedScene(
        Scene& scene,
        ActorAuthoringStore& actorAuthoring,
        ActorComponentDescriptorStore& actorComponents,
        const ActorComponentDescriptorRegistry& componentRegistry,
        const SceneSerializedScene& serialized,
        const SceneSerializationLoadContext& context,
        const SceneSerializationSettings& settings)
    {
        const SceneSerializationDiagnostics diagnostics =
            validateSerializedScene(serialized, ReflectionRegistry{}, componentRegistry, settings);
        if (!diagnostics.errors.empty()) {
            return SceneSerializationStatus::InvalidReference;
        }

        ActorComponentDescriptorStore stagedComponents;
        for (const SceneSerializedActorComponentRecord& record : serialized.actorComponents) {
            std::string message;
            if (stagedComponents.upsert(record.metadata, &message) != ActorComponentStatus::Success) {
                return SceneSerializationStatus::InvalidReference;
            }
        }

        const SceneSerializationStatus sceneStatus = applySerializedScene(scene, actorAuthoring, serialized, context, settings);
        if (sceneStatus != SceneSerializationStatus::Success) {
            return sceneStatus;
        }

        actorComponents = stagedComponents;
        return SceneSerializationStatus::Success;
    }
}
