#include "Engine/AssetRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "Assets/Assimp/Importer.hpp"

namespace Engine {
    namespace {
        constexpr uint64_t FnvOffset = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;

        std::string lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        std::string normalizedPathString(const std::filesystem::path& path)
        {
            return lowercase(path.lexically_normal().generic_string());
        }

        uint64_t hashText(std::string_view text, uint64_t seed = FnvOffset)
        {
            uint64_t hash = seed;
            for (unsigned char character : text) {
                hash ^= character;
                hash *= FnvPrime;
            }
            return hash;
        }

        std::string hexString(uint64_t value)
        {
            std::ostringstream stream;
            stream << std::hex << value;
            return stream.str();
        }

        uint32_t typeIndex(AssetType type)
        {
            return static_cast<uint32_t>(type);
        }

        std::filesystem::path resolveTexturePath(
            const std::filesystem::path& scenePath,
            const std::filesystem::path& texturePath)
        {
            if (texturePath.empty()) {
                return {};
            }

            const std::string textureText = texturePath.generic_string();
            if (textureText.rfind("data:", 0) == 0) {
                return {};
            }

            if (texturePath.is_absolute()) {
                return texturePath;
            }

            return scenePath.parent_path() / texturePath;
        }

        void appendUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
        {
            if (path.empty()) {
                return;
            }

            const std::string normalized = normalizedPathString(path);
            const auto found = std::find_if(paths.begin(), paths.end(), [&](const std::filesystem::path& existing) {
                return normalizedPathString(existing) == normalized;
            });
            if (found == paths.end()) {
                paths.push_back(path);
            }
        }
    }

    AssetHandle AssetRegistry::registerAsset(const AssetDescriptor& descriptor)
    {
        const AssetMetadata candidate = makeMetadata(descriptor, {});
        for (uint32_t index = 0; index < assets_.size(); ++index) {
            const AssetRecord& record = assets_[index];
            if (!record.alive) {
                continue;
            }

            if (record.metadata.canonicalPath == candidate.canonicalPath
                && record.metadata.type == candidate.type
                && record.metadata.settings == candidate.settings) {
                ++diagnostics_.duplicateRegistrationCount;
                setLastMessage("duplicate asset registration returned existing handle");
                return {index, record.generation};
            }
        }

        for (uint32_t index = 0; index < assets_.size(); ++index) {
            AssetRecord& record = assets_[index];
            if (!record.alive) {
                record.alive = true;
                record.generation = nextGeneration(record.generation);
                record.metadata = makeMetadata(descriptor, {index, record.generation});
                refreshDiagnostics();
                return {index, record.generation};
            }
        }

        AssetRecord record;
        record.alive = true;
        record.generation = nextGeneration(record.generation);
        assets_.push_back(std::move(record));
        const AssetHandle handle{static_cast<uint32_t>(assets_.size() - 1), assets_.back().generation};
        assets_.back().metadata = makeMetadata(descriptor, handle);
        refreshDiagnostics();
        return handle;
    }

    bool AssetRegistry::unregisterAsset(AssetHandle asset)
    {
        AssetRecord* assetRecord = record(asset);
        if (!assetRecord) {
            return false;
        }

        const AssetId removedId = assetRecord->metadata.id;
        assetRecord->alive = false;
        assetRecord->metadata = {};

        for (AssetRecord& candidate : assets_) {
            if (!candidate.alive) {
                continue;
            }
            auto& dependencies = candidate.metadata.dependencies;
            dependencies.erase(
                std::remove(dependencies.begin(), dependencies.end(), removedId),
                dependencies.end());
        }

        refreshDiagnostics();
        return true;
    }

    bool AssetRegistry::contains(AssetHandle asset) const
    {
        return record(asset) != nullptr;
    }

    std::optional<AssetHandle> AssetRegistry::findById(AssetId id) const
    {
        if (!isValid(id)) {
            return std::nullopt;
        }

        for (uint32_t index = 0; index < assets_.size(); ++index) {
            const AssetRecord& asset = assets_[index];
            if (asset.alive && asset.metadata.id == id) {
                return AssetHandle{index, asset.generation};
            }
        }
        return std::nullopt;
    }

    std::optional<AssetHandle> AssetRegistry::findByCanonicalPath(
        const std::filesystem::path& path,
        AssetType type,
        const AssetImportSettingsKey& settings) const
    {
        const std::filesystem::path canonicalPath = canonicalize(path);
        for (uint32_t index = 0; index < assets_.size(); ++index) {
            const AssetRecord& asset = assets_[index];
            if (asset.alive
                && asset.metadata.canonicalPath == canonicalPath
                && asset.metadata.type == type
                && asset.metadata.settings == settings) {
                return AssetHandle{index, asset.generation};
            }
        }
        return std::nullopt;
    }

    std::optional<AssetMetadata> AssetRegistry::metadata(AssetHandle asset) const
    {
        const AssetRecord* assetRecord = record(asset);
        if (!assetRecord) {
            return std::nullopt;
        }
        return assetRecord->metadata;
    }

    std::optional<AssetMetadata> AssetRegistry::metadata(AssetId id) const
    {
        const std::optional<AssetHandle> handle = findById(id);
        if (!handle.has_value()) {
            return std::nullopt;
        }
        return metadata(*handle);
    }

    std::vector<AssetHandle> AssetRegistry::assets() const
    {
        std::vector<AssetHandle> result;
        for (uint32_t index = 0; index < assets_.size(); ++index) {
            const AssetRecord& asset = assets_[index];
            if (asset.alive) {
                result.push_back({index, asset.generation});
            }
        }
        return result;
    }

    bool AssetRegistry::addDependency(AssetHandle asset, AssetHandle dependency)
    {
        AssetRecord* assetRecord = record(asset);
        const AssetRecord* dependencyRecord = record(dependency);
        if (!assetRecord || !dependencyRecord || assetRecord->metadata.id == dependencyRecord->metadata.id) {
            return false;
        }

        auto& dependencies = assetRecord->metadata.dependencies;
        if (std::find(dependencies.begin(), dependencies.end(), dependencyRecord->metadata.id) != dependencies.end()) {
            return false;
        }

        dependencies.push_back(dependencyRecord->metadata.id);
        refreshDiagnostics();
        return true;
    }

    bool AssetRegistry::setDependencies(AssetHandle asset, const std::vector<AssetHandle>& dependencies)
    {
        AssetRecord* assetRecord = record(asset);
        if (!assetRecord) {
            return false;
        }

        std::vector<AssetId> ids;
        for (AssetHandle dependency : dependencies) {
            const AssetRecord* dependencyRecord = record(dependency);
            if (!dependencyRecord || dependencyRecord->metadata.id == assetRecord->metadata.id) {
                return false;
            }
            if (std::find(ids.begin(), ids.end(), dependencyRecord->metadata.id) != ids.end()) {
                return false;
            }
            ids.push_back(dependencyRecord->metadata.id);
        }

        assetRecord->metadata.dependencies = std::move(ids);
        refreshDiagnostics();
        return true;
    }

    std::vector<AssetHandle> AssetRegistry::dependencies(AssetHandle asset) const
    {
        const AssetRecord* assetRecord = record(asset);
        if (!assetRecord) {
            return {};
        }

        std::vector<AssetHandle> result;
        for (AssetId id : assetRecord->metadata.dependencies) {
            if (const std::optional<AssetHandle> handle = findById(id)) {
                result.push_back(*handle);
            }
        }
        return result;
    }

    std::vector<AssetHandle> AssetRegistry::dependents(AssetHandle asset) const
    {
        const AssetRecord* assetRecord = record(asset);
        if (!assetRecord) {
            return {};
        }

        std::vector<AssetHandle> result;
        for (uint32_t index = 0; index < assets_.size(); ++index) {
            const AssetRecord& candidate = assets_[index];
            if (!candidate.alive) {
                continue;
            }
            if (std::find(
                candidate.metadata.dependencies.begin(),
                candidate.metadata.dependencies.end(),
                assetRecord->metadata.id) != candidate.metadata.dependencies.end()) {
                result.push_back({index, candidate.generation});
            }
        }
        return result;
    }

    bool AssetRegistry::refresh(AssetHandle asset)
    {
        AssetRecord* assetRecord = record(asset);
        if (!assetRecord) {
            return false;
        }

        const bool exists = std::filesystem::is_regular_file(assetRecord->metadata.canonicalPath);
        assetRecord->metadata.sourceFormat = detectSourceFormat(assetRecord->metadata.canonicalPath, assetRecord->metadata.type);
        if (!exists) {
            assetRecord->metadata.status = AssetStatus::Missing;
            assetRecord->metadata.warnings.push_back("asset source file is missing");
            refreshDiagnostics();
            return true;
        }

        const std::string newHash = hashFile(assetRecord->metadata.canonicalPath);
        if (!assetRecord->metadata.contentHash.empty() && newHash != assetRecord->metadata.contentHash) {
            assetRecord->metadata.status = AssetStatus::Stale;
        } else {
            assetRecord->metadata.status = AssetStatus::Registered;
        }
        assetRecord->metadata.contentHash = newHash;
        refreshDiagnostics();
        return true;
    }

    void AssetRegistry::refreshAll()
    {
        const std::vector<AssetHandle> handles = assets();
        for (AssetHandle handle : handles) {
            refresh(handle);
        }
        refreshDiagnostics();
    }

    AssetRegistryDiagnostics AssetRegistry::diagnostics() const
    {
        return diagnostics_;
    }

    AssetRegistry::AssetRecord* AssetRegistry::record(AssetHandle asset)
    {
        if (!isValid(asset) || asset.index >= assets_.size()) {
            return nullptr;
        }

        AssetRecord& assetRecord = assets_[asset.index];
        if (!assetRecord.alive || assetRecord.generation != asset.generation) {
            return nullptr;
        }
        return &assetRecord;
    }

    const AssetRegistry::AssetRecord* AssetRegistry::record(AssetHandle asset) const
    {
        if (!isValid(asset) || asset.index >= assets_.size()) {
            return nullptr;
        }

        const AssetRecord& assetRecord = assets_[asset.index];
        if (!assetRecord.alive || assetRecord.generation != asset.generation) {
            return nullptr;
        }
        return &assetRecord;
    }

    uint32_t AssetRegistry::nextGeneration(uint32_t generation) const
    {
        if (generation == std::numeric_limits<uint32_t>::max()) {
            return 1;
        }
        return generation + 1;
    }

    AssetHandle AssetRegistry::handleForIndex(uint32_t index) const
    {
        if (index >= assets_.size() || !assets_[index].alive) {
            return {};
        }
        return {index, assets_[index].generation};
    }

    std::filesystem::path AssetRegistry::canonicalize(const std::filesystem::path& path) const
    {
        std::error_code error;
        std::filesystem::path absolutePath = path.is_absolute() ? path : std::filesystem::absolute(path, error);
        if (error) {
            absolutePath = path;
        }

        std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, error);
        if (!error) {
            return canonicalPath.lexically_normal();
        }
        return absolutePath.lexically_normal();
    }

    AssetId AssetRegistry::makeAssetId(
        const std::filesystem::path& canonicalPath,
        AssetType type,
        const AssetImportSettingsKey& settings) const
    {
        uint64_t hash = hashText(normalizedPathString(canonicalPath));
        hash = hashText(std::to_string(static_cast<uint32_t>(type)), hash);
        hash = hashText(settings.pipeline, hash);
        hash = hashText(settings.version, hash);
        hash = hashText(settings.optionsHash, hash);
        if (hash == 0) {
            hash = 1;
        }
        return {hash};
    }

    AssetSourceFormat AssetRegistry::detectSourceFormat(const std::filesystem::path& path, AssetType type) const
    {
        if (type == AssetType::Material ||
            type == AssetType::Skeleton ||
            type == AssetType::AnimationClip ||
            type == AssetType::TerrainChunk) {
            return AssetSourceFormat::Generated;
        }

        const std::string extension = lowercase(path.extension().string());
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga" || extension == ".hdr" || extension == ".dds") {
            return AssetSourceFormat::Image;
        }
        if (extension == ".yaml" || extension == ".yml") {
            return AssetSourceFormat::Yaml;
        }

        if (type == AssetType::TerrainSource || type == AssetType::TerrainMaterialSet) {
            return AssetSourceFormat::Generated;
        }

        switch (Assets::Assimp::detectSceneSourceFormat(path)) {
            case Assets::Assimp::ImportedSceneSourceFormat::Gltf:
                return AssetSourceFormat::Gltf;
            case Assets::Assimp::ImportedSceneSourceFormat::Glb:
                return AssetSourceFormat::Glb;
            case Assets::Assimp::ImportedSceneSourceFormat::Fbx:
                return AssetSourceFormat::Fbx;
            case Assets::Assimp::ImportedSceneSourceFormat::Unknown:
            default:
                break;
        }
        return AssetSourceFormat::Unknown;
    }

    std::string AssetRegistry::hashFile(const std::filesystem::path& path) const
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return {};
        }

        uint64_t hash = FnvOffset;
        std::array<char, 4096> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            for (std::streamsize index = 0; index < count; ++index) {
                hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(index)]);
                hash *= FnvPrime;
            }
        }
        return hexString(hash);
    }

    AssetMetadata AssetRegistry::makeMetadata(const AssetDescriptor& descriptor, AssetHandle handle) const
    {
        AssetMetadata metadata;
        metadata.handle = handle;
        metadata.type = descriptor.type;
        metadata.sourcePath = descriptor.sourcePath;
        metadata.canonicalPath = canonicalize(descriptor.sourcePath);
        metadata.settings = descriptor.settings;
        metadata.sourceFormat = detectSourceFormat(metadata.canonicalPath, descriptor.type);
        metadata.id = isValid(descriptor.explicitId)
            ? descriptor.explicitId
            : makeAssetId(metadata.canonicalPath, descriptor.type, descriptor.settings);

        if (metadata.sourceFormat == AssetSourceFormat::Generated) {
            metadata.status = AssetStatus::Registered;
            return metadata;
        }

        if (std::filesystem::is_regular_file(metadata.canonicalPath)) {
            metadata.status = AssetStatus::Registered;
            metadata.contentHash = hashFile(metadata.canonicalPath);
        } else {
            metadata.status = AssetStatus::Missing;
            metadata.warnings.push_back("asset source file is missing");
        }
        return metadata;
    }

    void AssetRegistry::refreshDiagnostics()
    {
        const uint32_t typeCount = static_cast<uint32_t>(AssetType::TerrainChunk) + 1;
        diagnostics_.liveAssetCountByType.assign(typeCount, 0);
        diagnostics_.totalRegisteredAssets = 0;
        diagnostics_.missingCount = 0;
        diagnostics_.staleCount = 0;
        diagnostics_.dependencyEdgeCount = 0;

        for (const AssetRecord& asset : assets_) {
            if (!asset.alive) {
                continue;
            }
            ++diagnostics_.totalRegisteredAssets;
            const uint32_t index = typeIndex(asset.metadata.type);
            if (index < diagnostics_.liveAssetCountByType.size()) {
                ++diagnostics_.liveAssetCountByType[index];
            }
            if (asset.metadata.status == AssetStatus::Missing) {
                ++diagnostics_.missingCount;
            }
            if (asset.metadata.status == AssetStatus::Stale) {
                ++diagnostics_.staleCount;
            }
            diagnostics_.dependencyEdgeCount += static_cast<uint32_t>(asset.metadata.dependencies.size());
        }
    }

    void AssetRegistry::setLastMessage(std::string message)
    {
        diagnostics_.lastMessage = std::move(message);
    }

    ImportedSceneAssetDependencyResult registerImportedSceneTextureDependencies(
        AssetRegistry& registry,
        AssetHandle sceneAsset,
        const std::filesystem::path& scenePath,
        const Assets::Assimp::ImportedScene& importedScene)
    {
        ImportedSceneAssetDependencyResult result;
        if (!registry.contains(sceneAsset)) {
            result.warnings.push_back("scene asset handle is invalid");
            return result;
        }

        std::vector<std::filesystem::path> texturePaths;
        for (const Assets::Assimp::ImportedSceneTexture& texture : importedScene.textures) {
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, texture.path));
        }
        for (const Assets::Assimp::ImportedSceneMaterial& material : importedScene.materials) {
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.baseColorTexture));
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.normalTexture));
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.metallicTexture));
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.roughnessTexture));
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.metallicRoughnessTexture));
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.occlusionTexture));
            appendUniquePath(texturePaths, resolveTexturePath(scenePath, material.emissiveTexture));
        }

        for (const std::filesystem::path& texturePath : texturePaths) {
            AssetDescriptor descriptor;
            descriptor.sourcePath = texturePath;
            descriptor.type = AssetType::Texture;
            descriptor.settings = {"imported_scene_texture", "phase_04", ""};
            const AssetHandle textureAsset = registry.registerAsset(descriptor);
            if (registry.addDependency(sceneAsset, textureAsset)) {
                ++result.dependencyCount;
            }
            ++result.registeredTextureCount;
        }

        return result;
    }
}
