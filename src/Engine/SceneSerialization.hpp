#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/TerrainSerializationPrep.hpp"

namespace Engine {
    inline constexpr const char* SceneBinaryFormatVersion = "scene_binary_v13_1";
    inline constexpr uint32_t SceneBinaryNumericVersion = 1;
    inline constexpr uint32_t SceneBinaryEndianMarker = 0x01020304u;

    enum class SceneSerializationStatus {
        Success,
        InvalidInput,
        UnsupportedVersion,
        CorruptHeader,
        CorruptDirectory,
        MissingRequiredChunk,
        UnknownRequiredChunk,
        ChecksumMismatch,
        UnknownType,
        MissingAsset,
        InvalidReference,
        IoError,
    };

    enum class SceneBinaryChunkType : uint32_t {
        SceneMetadata = 1,
        ReflectionSchema = 2,
        ActorTable = 3,
        ComponentTable = 4,
        AssetReferenceTable = 5,
        TerrainReferenceTable = 6,
        StringTable = 7,
        UserExtension = 8,
    };

    enum class SceneBinaryChunkFlags : uint32_t {
        None = 0,
        Required = 1u << 0,
        Optional = 1u << 1,
    };

    [[nodiscard]] constexpr SceneBinaryChunkFlags operator|(SceneBinaryChunkFlags lhs, SceneBinaryChunkFlags rhs)
    {
        return static_cast<SceneBinaryChunkFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr SceneBinaryChunkFlags operator&(SceneBinaryChunkFlags lhs, SceneBinaryChunkFlags rhs)
    {
        return static_cast<SceneBinaryChunkFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr bool hasFlag(SceneBinaryChunkFlags flags, SceneBinaryChunkFlags flag)
    {
        return static_cast<uint32_t>(flags & flag) != 0u;
    }

    struct SceneSerializationSettings {
        uint32_t formatVersion = SceneBinaryNumericVersion;
        bool enableChecksums = true;
        bool deterministicOutput = true;
        bool skipUnknownOptionalChunks = true;
        bool allowGeneratedSceneObjectIds = false;
        uint64_t maxFileBytes = 64ull * 1024ull * 1024ull;
        uint64_t maxChunkBytes = 16ull * 1024ull * 1024ull;
        uint32_t maxStringBytes = 64u * 1024u;
    };

    struct SceneBinaryHeader {
        char magic[8] = {'M', 'E', 'S', 'C', 'E', 'N', 'E', '\0'};
        uint32_t headerSize = 72;
        uint32_t endianMarker = SceneBinaryEndianMarker;
        uint32_t formatVersion = SceneBinaryNumericVersion;
        uint32_t flags = 0;
        uint64_t fileSize = 0;
        uint64_t directoryOffset = 0;
        uint32_t directoryCount = 0;
        uint64_t schemaOffset = 0;
        uint32_t schemaCount = 0;
        uint64_t rootMetadataOffset = 0;
        uint64_t headerChecksum = 0;
    };

    struct SceneBinaryChunkEntry {
        SceneBinaryChunkType type = SceneBinaryChunkType::UserExtension;
        uint32_t version = 1;
        SceneBinaryChunkFlags flags = SceneBinaryChunkFlags::None;
        uint64_t offset = 0;
        uint64_t byteSize = 0;
        uint64_t uncompressedByteSize = 0;
        uint32_t recordCount = 0;
        uint64_t checksum = 0;
        uint32_t dependencyStart = 0;
        uint32_t dependencyCount = 0;
    };

    struct SceneSerializedActorRecord {
        SceneObjectId id;
        std::optional<SceneObjectId> parent;
        SceneTransform localTransform;
        uint32_t order = 0;
    };

    struct SceneSerializedComponentRecord {
        SceneObjectId owner;
        SceneComponentTypeId type;
        uint32_t order = 0;
    };

    struct SceneSerializedPropertySchema {
        uint32_t objectId = 0;
        uint32_t propertyId = 0;
        std::string name;
        ReflectedValueType type = ReflectedValueType::None;
        ReflectedPropertyFlag flags = ReflectedPropertyFlag::None;
    };

    struct SceneSerializedAssetReference {
        AssetId id;
        AssetType type = AssetType::Unknown;
        AssetImportSettingsKey importSettings;
        std::string sourcePath;
        std::string canonicalPath;
    };

    struct SceneSerializedTerrainReference {
        TerrainSerializedChunkFileMetadata metadata;
    };

    struct SceneSerializedScene {
        std::string formatVersion = SceneBinaryFormatVersion;
        std::vector<SceneSerializedActorRecord> actors;
        std::vector<SceneSerializedComponentRecord> components;
        std::vector<SceneSerializedPropertySchema> schema;
        std::vector<SceneSerializedAssetReference> assets;
        std::vector<SceneSerializedTerrainReference> terrain;
    };

    struct SceneSerializationDiagnostics {
        uint32_t actorCount = 0;
        uint32_t componentCount = 0;
        uint32_t schemaPropertyCount = 0;
        uint32_t assetReferenceCount = 0;
        uint32_t terrainReferenceCount = 0;
        uint32_t skippedReflectionPropertyCount = 0;
        uint32_t unknownOptionalChunkCount = 0;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    struct SceneSerializationWriteResult {
        SceneSerializationStatus status = SceneSerializationStatus::Success;
        std::string message;
        std::filesystem::path path;
        SceneBinaryHeader header;
        std::vector<SceneBinaryChunkEntry> directory;
        SceneSerializationDiagnostics diagnostics;
    };

    struct SceneSerializationReadResult {
        SceneSerializationStatus status = SceneSerializationStatus::Success;
        std::string message;
        std::filesystem::path path;
        SceneBinaryHeader header;
        std::vector<SceneBinaryChunkEntry> directory;
        SceneSerializedScene scene;
        SceneSerializationDiagnostics diagnostics;
    };

    struct SceneSerializationHeaderReadResult {
        SceneSerializationStatus status = SceneSerializationStatus::Success;
        std::string message;
        std::filesystem::path path;
        SceneBinaryHeader header;
        std::vector<SceneBinaryChunkEntry> directory;
        SceneSerializationDiagnostics diagnostics;
    };

    struct SceneSerializationLoadContext {
        bool clearExistingScene = false;
    };

    [[nodiscard]] SceneSerializedScene buildSerializedScene(
        Scene& scene,
        const ReflectionRegistry& registry,
        const SceneSerializationSettings& settings = {});
    [[nodiscard]] SceneSerializationDiagnostics validateSerializedScene(
        const SceneSerializedScene& scene,
        const ReflectionRegistry& registry,
        const SceneSerializationSettings& settings = {});
    [[nodiscard]] SceneSerializationWriteResult writeSceneBinary(
        const std::filesystem::path& path,
        const SceneSerializedScene& scene,
        const SceneSerializationSettings& settings = {});
    [[nodiscard]] SceneSerializationHeaderReadResult readSceneBinaryHeader(
        const std::filesystem::path& path,
        const SceneSerializationSettings& settings = {});
    [[nodiscard]] SceneSerializationReadResult readSceneBinary(
        const std::filesystem::path& path,
        const SceneSerializationSettings& settings = {});
    [[nodiscard]] SceneSerializationStatus applySerializedScene(
        Scene& scene,
        const SceneSerializedScene& serialized,
        const SceneSerializationLoadContext& context = {},
        const SceneSerializationSettings& settings = {});
}
