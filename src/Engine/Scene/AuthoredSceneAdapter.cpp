#include "Engine/Scene/AuthoredSceneAdapter.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "Engine/ImportedSceneResources.hpp"

namespace {
    constexpr uint32_t InvalidIndex = UINT32_MAX;

    bool rendererHandleValid(Renderer::StaticMeshHandle handle)
    {
        return handle.id != UINT32_MAX;
    }

    bool rendererHandleValid(Renderer::MaterialHandle handle)
    {
        return handle.id != UINT32_MAX;
    }

    bool isFinite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool isFinite(const glm::quat& value)
    {
        return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    std::optional<Engine::SceneTransform> decomposeTransform(const glm::mat4& matrix)
    {
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 translation{0.0f};
        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
            return std::nullopt;
        }

        rotation = glm::normalize(rotation);
        if (!isFinite(translation) || !isFinite(scale) || !isFinite(rotation)) {
            return std::nullopt;
        }

        return Engine::SceneTransform{translation, rotation, scale};
    }

    Renderer::LightType convertLightType(Assets::Assimp::ImportedSceneLightType type)
    {
        switch (type) {
            case Assets::Assimp::ImportedSceneLightType::Directional:
                return Renderer::LightType::Directional;
            case Assets::Assimp::ImportedSceneLightType::Spot:
                return Renderer::LightType::Spot;
            case Assets::Assimp::ImportedSceneLightType::Point:
            default:
                return Renderer::LightType::Point;
        }
    }

    bool supportedLightType(Assets::Assimp::ImportedSceneLightType type)
    {
        switch (type) {
            case Assets::Assimp::ImportedSceneLightType::Directional:
            case Assets::Assimp::ImportedSceneLightType::Point:
            case Assets::Assimp::ImportedSceneLightType::Spot:
                return true;
            case Assets::Assimp::ImportedSceneLightType::Unknown:
            case Assets::Assimp::ImportedSceneLightType::Ambient:
            case Assets::Assimp::ImportedSceneLightType::Area:
            default:
                return false;
        }
    }

    Renderer::MaterialDescriptor fallbackMaterialDescriptor(const Engine::SceneAuthoredAdapterSettings& settings)
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = settings.materialNamePrefix.empty()
            ? "SceneAuthoredFallback"
            : settings.materialNamePrefix + ".Fallback";
        descriptor.baseColorFactor = {1.0f, 0.0f, 1.0f, 1.0f};
        descriptor.metallicFactor = 0.0f;
        descriptor.roughnessFactor = 1.0f;
        return descriptor;
    }

    Renderer::MaterialHandle ensureFallbackMaterial(
        std::optional<Renderer::MaterialHandle>& fallbackMaterial,
        Engine::SceneAuthoredResourceBinding& resources,
        Engine::SceneAuthoredAdapterDiagnostics& diagnostics,
        const Engine::SceneAuthoredAdapterSettings& settings)
    {
        if (fallbackMaterial.has_value()) {
            return *fallbackMaterial;
        }

        const Renderer::MaterialHandle fallback = Renderer::createMaterial(fallbackMaterialDescriptor(settings));
        resources.materials.push_back(fallback);
        fallbackMaterial = fallback;
        if (rendererHandleValid(fallback)) {
            ++diagnostics.createdMaterialCount;
        } else {
            diagnostics.warnings.push_back("Renderer rejected authored scene fallback material");
        }
        return fallback;
    }
}

namespace Engine {
    SceneAuthoredAdapterResult adaptImportedSceneToScene(
        const Assets::Assimp::ImportedScene& importedScene,
        const std::filesystem::path& scenePath,
        AssetCache& assetCache,
        Scene& scene,
        SceneRenderBridge& renderBridge,
        const SceneAuthoredAdapterSettings& settings)
    {
        SceneAuthoredAdapterResult result;
        result.diagnostics.importedNodeCount = static_cast<uint32_t>(importedScene.nodes.size());
        result.diagnostics.importedMeshCount = static_cast<uint32_t>(importedScene.meshes.size());
        result.diagnostics.importedMaterialCount = static_cast<uint32_t>(importedScene.materials.size());
        result.diagnostics.importedTextureCount = static_cast<uint32_t>(importedScene.textures.size());
        result.diagnostics.importedLightCount = static_cast<uint32_t>(importedScene.lights.size());
        result.diagnostics.skippedSkinCount = static_cast<uint32_t>(importedScene.skins.size());
        result.diagnostics.skippedJointCount = static_cast<uint32_t>(importedScene.joints.size());
        result.diagnostics.skippedSkinnedMeshCount = importedScene.diagnostics.skinnedMeshCount;
        result.diagnostics.skippedAnimationCount = static_cast<uint32_t>(importedScene.animations.size());
        result.diagnostics.warnings = importedScene.diagnostics.warnings;

        if (!importedScene.success) {
            result.message = importedScene.error.empty()
                ? "Imported scene was not successful"
                : importedScene.error;
            return result;
        }

        ImportedSceneTextureSet textureSet;
        ImportedSceneMaterialMappingSettings materialSettings;
        materialSettings.materialNamePrefix = settings.materialNamePrefix;
        materialSettings.textureDebugNamePrefix = settings.textureDebugNamePrefix;
        materialSettings.loadTextures = settings.loadTextures;
        ImportedSceneTextureLoadStats textureStats = acquireImportedSceneMaterialTextures(
            scenePath,
            importedScene.materials,
            assetCache,
            materialSettings,
            textureSet);
        result.diagnostics.textureLoadSuccessCount = textureStats.successCount;
        result.diagnostics.textureLoadFailureCount = textureStats.failureCount;
        result.diagnostics.textureFallbackCount = textureStats.fallbackCount;
        result.diagnostics.textureSrgbFallbackCount = textureStats.srgbFallbackCount;
        result.diagnostics.textureEstimatedBytes = textureStats.estimatedBytes;
        result.diagnostics.warnings.insert(
            result.diagnostics.warnings.end(),
            textureStats.warnings.begin(),
            textureStats.warnings.end());
        result.resources.textures = std::move(textureStats.acquiredTextures);

        result.resources.materials.reserve(importedScene.materials.size());
        for (uint32_t materialIndex = 0; materialIndex < importedScene.materials.size(); ++materialIndex) {
            const Renderer::MaterialDescriptor descriptor = importedSceneMaterialDescriptor(
                importedScene.materials[materialIndex],
                textureSet,
                materialIndex,
                settings.materialNamePrefix);
            const Renderer::MaterialHandle material = Renderer::createMaterial(descriptor);
            result.resources.materials.push_back(material);
            if (rendererHandleValid(material)) {
                ++result.diagnostics.createdMaterialCount;
            } else {
                result.diagnostics.warnings.push_back(
                    "Renderer rejected authored scene material " + std::to_string(materialIndex));
            }
        }

        result.resources.staticMeshes.reserve(importedScene.meshes.size());
        std::optional<Renderer::MaterialHandle> fallbackMaterial;
        for (uint32_t meshIndex = 0; meshIndex < importedScene.meshes.size(); ++meshIndex) {
            const Assets::Assimp::ImportedSceneMesh& importedMesh = importedScene.meshes[meshIndex];
            Renderer::StaticMeshDescriptor descriptor;
            descriptor.name = importedMesh.name.empty()
                ? "SceneAuthoredMesh." + std::to_string(meshIndex)
                : importedMesh.name;

            for (const Assets::Assimp::ImportedScenePrimitive& primitive : importedMesh.primitives) {
                Renderer::StaticSubmeshDescriptor submesh;
                submesh.vertices.reserve(primitive.vertices.size());
                for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                    submesh.vertices.push_back(importedSceneVertexToMeshVertex(vertex));
                }
                submesh.indices = primitive.indices;

                if (primitive.materialIndex < result.resources.materials.size() &&
                    rendererHandleValid(result.resources.materials[primitive.materialIndex])) {
                    submesh.material = result.resources.materials[primitive.materialIndex];
                } else {
                    ++result.diagnostics.invalidMaterialReferenceCount;
                    submesh.material = ensureFallbackMaterial(
                        fallbackMaterial,
                        result.resources,
                        result.diagnostics,
                        settings);
                }

                descriptor.submeshes.push_back(std::move(submesh));
            }

            const Renderer::StaticMeshHandle mesh = Renderer::createStaticMesh(descriptor);
            result.resources.staticMeshes.push_back(mesh);
            if (rendererHandleValid(mesh)) {
                ++result.diagnostics.createdRendererMeshCount;
            } else {
                result.diagnostics.warnings.push_back(
                    "Renderer rejected authored scene static mesh " + std::to_string(meshIndex));
            }
        }

        result.nodes.reserve(importedScene.nodes.size());
        std::vector<SceneActorHandle> actors(importedScene.nodes.size());
        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            SceneAuthoredNodeBinding binding;
            binding.importedNodeIndex = nodeIndex;
            binding.actor = scene.createActor();
            actors[nodeIndex] = binding.actor;
            ++result.diagnostics.createdActorCount;

            if (const std::optional<SceneTransform> transform = decomposeTransform(importedScene.nodes[nodeIndex].localTransform)) {
                scene.setLocalTransform(binding.actor, *transform);
            } else {
                result.diagnostics.warnings.push_back(
                    "Imported scene node " + std::to_string(nodeIndex) + " had a non-decomposable local transform");
            }

            result.nodes.push_back(std::move(binding));
        }

        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            const uint32_t parentIndex = importedScene.nodes[nodeIndex].parentIndex;
            if (parentIndex == InvalidIndex) {
                continue;
            }
            if (parentIndex >= actors.size()) {
                ++result.diagnostics.invalidNodeReferenceCount;
                result.diagnostics.warnings.push_back(
                    "Imported scene node " + std::to_string(nodeIndex) + " references invalid parent " + std::to_string(parentIndex));
                continue;
            }
            if (scene.attachChild(actors[nodeIndex], actors[parentIndex], false) != SceneTransformUpdateResult::Success) {
                ++result.diagnostics.invalidNodeReferenceCount;
                result.diagnostics.warnings.push_back(
                    "Failed to attach imported scene node " + std::to_string(nodeIndex) + " to parent " + std::to_string(parentIndex));
            }
        }

        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            SceneAuthoredNodeBinding& binding = result.nodes[nodeIndex];
            for (uint32_t meshIndex : importedScene.nodes[nodeIndex].meshIndices) {
                if (meshIndex >= result.resources.staticMeshes.size() || !rendererHandleValid(result.resources.staticMeshes[meshIndex])) {
                    ++result.diagnostics.invalidMeshReferenceCount;
                    result.diagnostics.warnings.push_back(
                        "Imported scene node " + std::to_string(nodeIndex) + " references invalid mesh " + std::to_string(meshIndex));
                    continue;
                }

                SceneMeshComponentDescriptor descriptor;
                descriptor.actor = binding.actor;
                descriptor.mesh = result.resources.staticMeshes[meshIndex];
                descriptor.layer = settings.renderLayer;
                descriptor.visibility = Renderer::VisibilityFlags::Visible;
                descriptor.maxDrawDistance = settings.maxDrawDistance;
                const SceneMeshComponentHandle component = renderBridge.attachMesh(descriptor);
                if (isValid(component)) {
                    binding.meshComponents.push_back(component);
                    ++result.diagnostics.createdMeshComponentCount;
                } else {
                    ++result.diagnostics.invalidNodeReferenceCount;
                    result.diagnostics.warnings.push_back(
                        "Failed to attach mesh component for imported scene node " + std::to_string(nodeIndex));
                }
            }
        }

        for (uint32_t lightIndex = 0; lightIndex < importedScene.lights.size(); ++lightIndex) {
            const Assets::Assimp::ImportedSceneLight& importedLight = importedScene.lights[lightIndex];
            if (!supportedLightType(importedLight.type)) {
                ++result.diagnostics.skippedUnsupportedLightCount;
                result.diagnostics.warnings.push_back(
                    "Skipped unsupported imported scene light " + std::to_string(lightIndex));
                continue;
            }
            if (!importedLight.nodeIndex.has_value() || *importedLight.nodeIndex >= result.nodes.size()) {
                ++result.diagnostics.invalidNodeReferenceCount;
                result.diagnostics.warnings.push_back(
                    "Imported scene light " + std::to_string(lightIndex) + " references an invalid node");
                continue;
            }

            SceneLightComponentDescriptor descriptor;
            descriptor.actor = result.nodes[*importedLight.nodeIndex].actor;
            descriptor.type = convertLightType(importedLight.type);
            descriptor.name = importedLight.name;
            descriptor.color = importedLight.color;
            descriptor.intensity = importedLight.intensity;
            descriptor.range = importedLight.range;
            descriptor.innerConeAngle = importedLight.innerConeAngle;
            descriptor.outerConeAngle = importedLight.outerConeAngle;
            descriptor.enabled = importedLight.intensity > 0.0f;

            const SceneLightComponentHandle component = renderBridge.attachLight(descriptor);
            if (isValid(component)) {
                result.nodes[*importedLight.nodeIndex].lightComponents.push_back(component);
                ++result.diagnostics.createdLightComponentCount;
            } else {
                ++result.diagnostics.invalidNodeReferenceCount;
                result.diagnostics.warnings.push_back(
                    "Failed to attach light component for imported scene light " + std::to_string(lightIndex));
            }
        }

        result.success = true;
        result.message = "Imported scene adapted to scene runtime";
        return result;
    }

    void releaseSceneAuthoredAdapterResources(
        SceneAuthoredResourceBinding& resources,
        AssetCache& assetCache)
    {
        for (Renderer::StaticMeshHandle& mesh : resources.staticMeshes) {
            if (rendererHandleValid(mesh)) {
                Renderer::destroyStaticMesh(mesh);
                mesh = {};
            }
        }

        for (Renderer::MaterialHandle& material : resources.materials) {
            if (rendererHandleValid(material)) {
                Renderer::destroyMaterial(material);
                material = {};
            }
        }

        for (const CachedTexture& texture : resources.textures) {
            assetCache.release(texture);
        }
        resources.textures.clear();
    }
}
