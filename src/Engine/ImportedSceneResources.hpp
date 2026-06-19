#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct ImportedSceneTextureSet {
        std::vector<Renderer::TextureHandle> baseColor;
        std::vector<Renderer::TextureHandle> normal;
        std::vector<Renderer::TextureHandle> metallic;
        std::vector<Renderer::TextureHandle> roughness;
        std::vector<Renderer::TextureHandle> metallicRoughness;
        std::vector<Renderer::TextureHandle> occlusion;
        std::vector<Renderer::TextureHandle> emissive;
    };

    struct ImportedSceneTextureLoadStats {
        uint32_t successCount = 0;
        uint32_t failureCount = 0;
        uint32_t fallbackCount = 0;
        uint32_t srgbFallbackCount = 0;
        uint64_t estimatedBytes = 0;
        std::vector<std::string> warnings;
        std::vector<CachedTexture> acquiredTextures;
    };

    struct ImportedSceneMaterialMappingSettings {
        std::string materialNamePrefix;
        std::string textureDebugNamePrefix;
        bool loadTextures = true;
    };

    std::filesystem::path resolveImportedSceneTexturePath(
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath);

    Renderer::MeshVertex importedSceneVertexToMeshVertex(
        const Assets::Assimp::ImportedSceneVertex& vertex);

    Renderer::MaterialDescriptor::AlphaMode importedSceneAlphaMode(
        Assets::Assimp::ImportedSceneAlphaMode mode);

    Renderer::MaterialDescriptor::TextureSlotHints importedSceneTextureSlotHints(
        const Assets::Assimp::ImportedSceneTextureHints& imported);

    Renderer::TextureDescriptor importedSceneTextureDescriptor(
        Renderer::TextureSlot slot,
        Renderer::TextureColorSpace colorSpace,
        const Assets::Assimp::ImportedSceneTextureHints& hints,
        std::string debugName);

    Renderer::MaterialDescriptor importedSceneMaterialDescriptor(
        const Assets::Assimp::ImportedSceneMaterial& material,
        const ImportedSceneTextureSet& textures,
        uint32_t materialIndex,
        std::string_view fallbackNamePrefix);

    ImportedSceneTextureLoadStats acquireImportedSceneMaterialTextures(
        const std::filesystem::path& scenePath,
        const std::vector<Assets::Assimp::ImportedSceneMaterial>& materials,
        AssetCache& assetCache,
        const ImportedSceneMaterialMappingSettings& settings,
        ImportedSceneTextureSet& outTextures);
}
