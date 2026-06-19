#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct SceneAuthoredNodeBinding {
        uint32_t importedNodeIndex = UINT32_MAX;
        SceneActorHandle actor;
        std::vector<SceneMeshComponentHandle> meshComponents;
        std::vector<SceneLightComponentHandle> lightComponents;
    };

    struct SceneAuthoredResourceBinding {
        std::vector<Renderer::StaticMeshHandle> staticMeshes;
        std::vector<Renderer::MaterialHandle> materials;
        std::vector<CachedTexture> textures;
    };

    struct SceneAuthoredAdapterSettings {
        bool loadTextures = true;
        Renderer::RenderLayer renderLayer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
        std::string materialNamePrefix = "SceneAuthoredMaterial";
        std::string textureDebugNamePrefix = "SceneAuthored";
    };

    struct SceneAuthoredAdapterDiagnostics {
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t importedTextureCount = 0;
        uint32_t importedLightCount = 0;
        uint32_t createdActorCount = 0;
        uint32_t createdRendererMeshCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t createdMeshComponentCount = 0;
        uint32_t createdLightComponentCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t textureFallbackCount = 0;
        uint32_t textureSrgbFallbackCount = 0;
        uint64_t textureEstimatedBytes = 0;
        uint32_t skippedSkinCount = 0;
        uint32_t skippedJointCount = 0;
        uint32_t skippedSkinnedMeshCount = 0;
        uint32_t skippedAnimationCount = 0;
        uint32_t skippedUnsupportedLightCount = 0;
        uint32_t invalidNodeReferenceCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        std::vector<std::string> warnings;
    };

    struct SceneAuthoredAdapterResult {
        bool success = false;
        std::string message;
        std::vector<SceneAuthoredNodeBinding> nodes;
        SceneAuthoredResourceBinding resources;
        SceneAuthoredAdapterDiagnostics diagnostics;
    };

    [[nodiscard]] SceneAuthoredAdapterResult adaptImportedSceneToScene(
        const Assets::Assimp::ImportedScene& importedScene,
        const std::filesystem::path& scenePath,
        AssetCache& assetCache,
        Scene& scene,
        SceneRenderBridge& renderBridge,
        const SceneAuthoredAdapterSettings& settings = {});

    void releaseSceneAuthoredAdapterResources(
        SceneAuthoredResourceBinding& resources,
        AssetCache& assetCache);
}
