#include "Engine/ImportedSceneResources.hpp"

#include <algorithm>

namespace {
    uint32_t packColorAbgr(const glm::vec4& color)
    {
        const auto channel = [](float value) {
            return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        const uint32_t r = channel(color.r);
        const uint32_t g = channel(color.g);
        const uint32_t b = channel(color.b);
        const uint32_t a = channel(color.a);
        return (a << 24u) | (b << 16u) | (g << 8u) | r;
    }

    Renderer::TextureWrap convertTextureWrap(Assets::Assimp::ImportedSceneTextureWrap wrap)
    {
        switch (wrap) {
            case Assets::Assimp::ImportedSceneTextureWrap::ClampToEdge:
                return Renderer::TextureWrap::ClampToEdge;
            case Assets::Assimp::ImportedSceneTextureWrap::MirroredRepeat:
                return Renderer::TextureWrap::MirroredRepeat;
            case Assets::Assimp::ImportedSceneTextureWrap::Repeat:
            case Assets::Assimp::ImportedSceneTextureWrap::Unknown:
            default:
                break;
        }
        return Renderer::TextureWrap::Repeat;
    }

    bool textureFileExists(const std::filesystem::path& path)
    {
        return !path.empty() && std::filesystem::exists(path);
    }

    bool materialHasAnyTexture(const Assets::Assimp::ImportedSceneMaterial& material)
    {
        return !material.baseColorTexture.empty() ||
            !material.normalTexture.empty() ||
            !material.metallicTexture.empty() ||
            !material.roughnessTexture.empty() ||
            !material.metallicRoughnessTexture.empty() ||
            !material.occlusionTexture.empty() ||
            !material.emissiveTexture.empty();
    }
}

namespace Engine {
    std::filesystem::path resolveImportedSceneTexturePath(
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath)
    {
        if (texturePath.empty() || texturePath.is_absolute()) {
            return texturePath;
        }
        return scenePath.parent_path() / texturePath;
    }

    Renderer::MeshVertex importedSceneVertexToMeshVertex(
        const Assets::Assimp::ImportedSceneVertex& vertex)
    {
        const glm::vec2 texcoord1 = vertex.hasTexcoord1 ? vertex.texcoord1 : vertex.texcoord0;
        return {
            vertex.position.x,
            vertex.position.y,
            vertex.position.z,
            vertex.normal.x,
            vertex.normal.y,
            vertex.normal.z,
            vertex.tangent.x,
            vertex.tangent.y,
            vertex.tangent.z,
            vertex.tangent.w,
            vertex.texcoord0.x,
            vertex.texcoord0.y,
            texcoord1.x,
            texcoord1.y,
            packColorAbgr(vertex.hasColor0 ? vertex.color0 : glm::vec4{1.0f}),
        };
    }

    Renderer::MaterialDescriptor::AlphaMode importedSceneAlphaMode(
        Assets::Assimp::ImportedSceneAlphaMode mode)
    {
        switch (mode) {
            case Assets::Assimp::ImportedSceneAlphaMode::Mask:
                return Renderer::MaterialDescriptor::AlphaMode::Mask;
            case Assets::Assimp::ImportedSceneAlphaMode::Blend:
                return Renderer::MaterialDescriptor::AlphaMode::Blend;
            case Assets::Assimp::ImportedSceneAlphaMode::Opaque:
            default:
                break;
        }
        return Renderer::MaterialDescriptor::AlphaMode::Opaque;
    }

    Renderer::MaterialDescriptor::TextureSlotHints importedSceneTextureSlotHints(
        const Assets::Assimp::ImportedSceneTextureHints& imported)
    {
        Renderer::MaterialDescriptor::TextureSlotHints hints;
        hints.colorSpace = imported.colorSpace == Assets::Assimp::ImportedSceneTextureColorSpace::Srgb
            ? Renderer::MaterialDescriptor::TextureColorSpace::Srgb
            : Renderer::MaterialDescriptor::TextureColorSpace::Linear;
        return hints;
    }

    Renderer::TextureDescriptor importedSceneTextureDescriptor(
        Renderer::TextureSlot slot,
        Renderer::TextureColorSpace colorSpace,
        const Assets::Assimp::ImportedSceneTextureHints& hints,
        std::string debugName)
    {
        Renderer::TextureDescriptor descriptor;
        descriptor.slot = slot;
        descriptor.colorSpace = colorSpace;
        descriptor.wrapU = convertTextureWrap(hints.wrapU);
        descriptor.wrapV = convertTextureWrap(hints.wrapV);
        descriptor.minFilter = Renderer::TextureFilter::Linear;
        descriptor.magFilter = Renderer::TextureFilter::Linear;
        descriptor.mipFilter = Renderer::TextureFilter::Linear;
        descriptor.generateMips = true;
        descriptor.debugName = std::move(debugName);
        return descriptor;
    }

    Renderer::MaterialDescriptor importedSceneMaterialDescriptor(
        const Assets::Assimp::ImportedSceneMaterial& material,
        const ImportedSceneTextureSet& textures,
        uint32_t materialIndex,
        std::string_view fallbackNamePrefix)
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = material.name.empty()
            ? std::string{fallbackNamePrefix} + "." + std::to_string(materialIndex)
            : material.name;
        descriptor.baseColorFactor = material.baseColorFactor;
        descriptor.baseColorTexture = materialIndex < textures.baseColor.size() ? textures.baseColor[materialIndex] : Renderer::TextureHandle{};
        descriptor.normalTexture = materialIndex < textures.normal.size() ? textures.normal[materialIndex] : Renderer::TextureHandle{};
        descriptor.normalScale = material.normalScale;
        descriptor.metallicFactor = material.metallicFactor;
        descriptor.roughnessFactor = material.roughnessFactor;
        descriptor.metallicTexture = materialIndex < textures.metallic.size() ? textures.metallic[materialIndex] : Renderer::TextureHandle{};
        descriptor.roughnessTexture = materialIndex < textures.roughness.size() ? textures.roughness[materialIndex] : Renderer::TextureHandle{};
        descriptor.metallicRoughnessTexture = materialIndex < textures.metallicRoughness.size() ? textures.metallicRoughness[materialIndex] : Renderer::TextureHandle{};
        descriptor.occlusionTexture = materialIndex < textures.occlusion.size() ? textures.occlusion[materialIndex] : Renderer::TextureHandle{};
        descriptor.occlusionStrength = material.occlusionStrength;
        descriptor.emissiveTexture = materialIndex < textures.emissive.size() ? textures.emissive[materialIndex] : Renderer::TextureHandle{};
        descriptor.emissiveFactor = material.emissiveFactor;
        descriptor.alphaMode = importedSceneAlphaMode(material.alphaMode);
        descriptor.alphaCutoff = material.alphaCutoff;
        descriptor.doubleSided = material.doubleSided;
        descriptor.baseColorTextureHints = importedSceneTextureSlotHints(material.baseColorTextureHints);
        descriptor.normalTextureHints = importedSceneTextureSlotHints(material.normalTextureHints);
        descriptor.metallicTextureHints = importedSceneTextureSlotHints(material.metallicTextureHints);
        descriptor.roughnessTextureHints = importedSceneTextureSlotHints(material.roughnessTextureHints);
        descriptor.metallicRoughnessTextureHints = importedSceneTextureSlotHints(material.metallicRoughnessTextureHints);
        descriptor.occlusionTextureHints = importedSceneTextureSlotHints(material.occlusionTextureHints);
        descriptor.emissiveTextureHints = importedSceneTextureSlotHints(material.emissiveTextureHints);
        return descriptor;
    }

    ImportedSceneTextureLoadStats acquireImportedSceneMaterialTextures(
        const std::filesystem::path& scenePath,
        const std::vector<Assets::Assimp::ImportedSceneMaterial>& materials,
        AssetCache& assetCache,
        const ImportedSceneMaterialMappingSettings& settings,
        ImportedSceneTextureSet& outTextures)
    {
        ImportedSceneTextureLoadStats stats;
        const size_t materialCount = materials.size();
        outTextures.baseColor.assign(materialCount, {});
        outTextures.normal.assign(materialCount, {});
        outTextures.metallic.assign(materialCount, {});
        outTextures.roughness.assign(materialCount, {});
        outTextures.metallicRoughness.assign(materialCount, {});
        outTextures.occlusion.assign(materialCount, {});
        outTextures.emissive.assign(materialCount, {});

        if (!settings.loadTextures) {
            for (const Assets::Assimp::ImportedSceneMaterial& material : materials) {
                if (materialHasAnyTexture(material)) {
                    ++stats.fallbackCount;
                }
            }
            return stats;
        }

        const auto debugName = [&](std::string_view slotName) {
            return settings.textureDebugNamePrefix.empty()
                ? std::string{slotName}
                : settings.textureDebugNamePrefix + std::string{slotName};
        };

        const auto acquireTexture = [&](
            const std::filesystem::path& texturePath,
            std::string_view slotName,
            const Renderer::TextureDescriptor& descriptor) -> Renderer::TextureHandle {
            const std::filesystem::path resolvedPath = resolveImportedSceneTexturePath(scenePath, texturePath);
            if (!textureFileExists(resolvedPath)) {
                if (!texturePath.empty()) {
                    ++stats.failureCount;
                    ++stats.fallbackCount;
                    stats.warnings.push_back(
                        "Missing imported scene texture for " + std::string{slotName} + ": " + texturePath.generic_string());
                }
                return {};
            }

            CachedTexture texture = assetCache.acquireTexture(resolvedPath, descriptor);
            if (!Renderer::isValid(texture.handle)) {
                ++stats.failureCount;
                ++stats.fallbackCount;
                stats.warnings.push_back(
                    "Failed to load imported scene texture for " + std::string{slotName} + ": " + texturePath.generic_string());
                return {};
            }

            ++stats.successCount;
            const Renderer::TextureInfo info = Renderer::textureInfo(texture.handle);
            stats.estimatedBytes += info.estimatedBytes;
            if (info.srgbFallback) {
                ++stats.srgbFallbackCount;
            }
            stats.acquiredTextures.push_back(texture);
            return texture.handle;
        };

        for (uint32_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
            const Assets::Assimp::ImportedSceneMaterial& material = materials[materialIndex];
            outTextures.baseColor[materialIndex] = acquireTexture(
                material.baseColorTexture,
                "baseColor",
                importedSceneTextureDescriptor(Renderer::TextureSlot::BaseColor, Renderer::TextureColorSpace::Srgb, material.baseColorTextureHints, debugName("BaseColor")));
            outTextures.normal[materialIndex] = acquireTexture(
                material.normalTexture,
                "normal",
                importedSceneTextureDescriptor(Renderer::TextureSlot::Normal, Renderer::TextureColorSpace::Linear, material.normalTextureHints, debugName("Normal")));
            if (!material.hasPackedMetallicRoughnessTexture) {
                outTextures.metallic[materialIndex] = acquireTexture(
                    material.metallicTexture,
                    "metallic",
                    importedSceneTextureDescriptor(Renderer::TextureSlot::Metallic, Renderer::TextureColorSpace::Linear, material.metallicTextureHints, debugName("Metallic")));
                outTextures.roughness[materialIndex] = acquireTexture(
                    material.roughnessTexture,
                    "roughness",
                    importedSceneTextureDescriptor(Renderer::TextureSlot::Roughness, Renderer::TextureColorSpace::Linear, material.roughnessTextureHints, debugName("Roughness")));
            }
            outTextures.metallicRoughness[materialIndex] = acquireTexture(
                material.metallicRoughnessTexture,
                "metallicRoughness",
                importedSceneTextureDescriptor(Renderer::TextureSlot::MetallicRoughness, Renderer::TextureColorSpace::Linear, material.metallicRoughnessTextureHints, debugName("MetallicRoughness")));
            outTextures.occlusion[materialIndex] = acquireTexture(
                material.occlusionTexture,
                "occlusion",
                importedSceneTextureDescriptor(Renderer::TextureSlot::Occlusion, Renderer::TextureColorSpace::Linear, material.occlusionTextureHints, debugName("Occlusion")));
            outTextures.emissive[materialIndex] = acquireTexture(
                material.emissiveTexture,
                "emissive",
                importedSceneTextureDescriptor(Renderer::TextureSlot::Emissive, Renderer::TextureColorSpace::Srgb, material.emissiveTextureHints, debugName("Emissive")));
        }

        return stats;
    }
}
