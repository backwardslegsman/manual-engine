#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Assets::Assimp {
    struct ImportedScene;
}

namespace Engine {
    struct AssetHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct AssetId {
        uint64_t value = 0;
    };

    enum class AssetType {
        Unknown,
        StaticMesh,
        Texture,
        Material,
        AuthoredScene,
        SkinnedMesh,
        Skeleton,
        AnimationClip,
        NavigationSource,
        Heightmap,
        TerrainSource,
        TerrainMaterialSet,
        TerrainChunk,
    };

    enum class AssetSourceFormat {
        Unknown,
        Gltf,
        Glb,
        Fbx,
        Image,
        Yaml,
        Generated,
    };

    enum class AssetStatus {
        Registered,
        Missing,
        Stale,
        Invalid,
    };

    struct AssetImportSettingsKey {
        std::string pipeline;
        std::string version;
        std::string optionsHash;
    };

    struct AssetDescriptor {
        std::filesystem::path sourcePath;
        AssetType type = AssetType::Unknown;
        AssetImportSettingsKey settings;
        AssetId explicitId;
    };

    struct AssetMetadata {
        AssetHandle handle;
        AssetId id;
        AssetType type = AssetType::Unknown;
        AssetStatus status = AssetStatus::Invalid;
        std::filesystem::path sourcePath;
        std::filesystem::path canonicalPath;
        AssetSourceFormat sourceFormat = AssetSourceFormat::Unknown;
        std::string contentHash;
        AssetImportSettingsKey settings;
        std::vector<AssetId> dependencies;
        std::vector<std::string> warnings;
    };

    struct AssetRegistryDiagnostics {
        uint32_t totalRegisteredAssets = 0;
        std::vector<uint32_t> liveAssetCountByType;
        uint32_t missingCount = 0;
        uint32_t staleCount = 0;
        uint32_t duplicateRegistrationCount = 0;
        uint32_t dependencyEdgeCount = 0;
        std::string lastMessage;
    };

    [[nodiscard]] constexpr bool isValid(AssetHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(AssetId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool operator==(AssetHandle lhs, AssetHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(AssetHandle lhs, AssetHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(AssetId lhs, AssetId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(AssetId lhs, AssetId rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] inline bool operator==(const AssetImportSettingsKey& lhs, const AssetImportSettingsKey& rhs)
    {
        return lhs.pipeline == rhs.pipeline && lhs.version == rhs.version && lhs.optionsHash == rhs.optionsHash;
    }

    [[nodiscard]] inline bool operator!=(const AssetImportSettingsKey& lhs, const AssetImportSettingsKey& rhs)
    {
        return !(lhs == rhs);
    }

    class AssetRegistry {
    public:
        [[nodiscard]] AssetHandle registerAsset(const AssetDescriptor& descriptor);
        bool unregisterAsset(AssetHandle asset);

        [[nodiscard]] bool contains(AssetHandle asset) const;
        [[nodiscard]] std::optional<AssetHandle> findById(AssetId id) const;
        [[nodiscard]] std::optional<AssetHandle> findByCanonicalPath(
            const std::filesystem::path& path,
            AssetType type,
            const AssetImportSettingsKey& settings = {}) const;

        [[nodiscard]] std::optional<AssetMetadata> metadata(AssetHandle asset) const;
        [[nodiscard]] std::optional<AssetMetadata> metadata(AssetId id) const;
        [[nodiscard]] std::vector<AssetHandle> assets() const;

        bool addDependency(AssetHandle asset, AssetHandle dependency);
        bool setDependencies(AssetHandle asset, const std::vector<AssetHandle>& dependencies);
        [[nodiscard]] std::vector<AssetHandle> dependencies(AssetHandle asset) const;
        [[nodiscard]] std::vector<AssetHandle> dependents(AssetHandle asset) const;

        bool refresh(AssetHandle asset);
        void refreshAll();
        [[nodiscard]] AssetRegistryDiagnostics diagnostics() const;

    private:
        struct AssetRecord {
            uint32_t generation = 0;
            bool alive = false;
            AssetMetadata metadata;
        };

        [[nodiscard]] AssetRecord* record(AssetHandle asset);
        [[nodiscard]] const AssetRecord* record(AssetHandle asset) const;
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        [[nodiscard]] AssetHandle handleForIndex(uint32_t index) const;
        [[nodiscard]] std::filesystem::path canonicalize(const std::filesystem::path& path) const;
        [[nodiscard]] AssetId makeAssetId(
            const std::filesystem::path& canonicalPath,
            AssetType type,
            const AssetImportSettingsKey& settings) const;
        [[nodiscard]] AssetSourceFormat detectSourceFormat(const std::filesystem::path& path, AssetType type) const;
        [[nodiscard]] std::string hashFile(const std::filesystem::path& path) const;
        [[nodiscard]] AssetMetadata makeMetadata(const AssetDescriptor& descriptor, AssetHandle handle) const;
        void refreshDiagnostics();
        void setLastMessage(std::string message);

        std::vector<AssetRecord> assets_;
        AssetRegistryDiagnostics diagnostics_;
    };

    struct ImportedSceneAssetDependencyResult {
        uint32_t registeredTextureCount = 0;
        uint32_t dependencyCount = 0;
        std::vector<std::string> warnings;
    };

    ImportedSceneAssetDependencyResult registerImportedSceneTextureDependencies(
        AssetRegistry& registry,
        AssetHandle sceneAsset,
        const std::filesystem::path& scenePath,
        const Assets::Assimp::ImportedScene& importedScene);
}
