#include "Engine/AuthoredScene.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <system_error>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "Assets/Assimp/Importer.hpp"

namespace {
    Engine::AuthoredSceneBounds convertBounds(const Assets::Assimp::ImportedSceneBounds& bounds)
    {
        return {bounds.min, bounds.max, bounds.valid};
    }

    Assets::Assimp::ImportedSceneBounds convertBounds(const Engine::AuthoredSceneBounds& bounds)
    {
        return {bounds.min, bounds.max, bounds.valid};
    }

    std::filesystem::path resolveSceneTexturePath(
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath)
    {
        if (texturePath.empty() || texturePath.is_absolute()) {
            return texturePath;
        }
        return scenePath.parent_path() / texturePath;
    }

    bool textureFileExists(const std::filesystem::path& path)
    {
        return !path.empty() && std::filesystem::exists(path);
    }

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

    Renderer::MeshVertex convertVertex(const Assets::Assimp::ImportedSceneVertex& vertex)
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

    Renderer::MaterialDescriptor::AlphaMode convertAlphaMode(Assets::Assimp::ImportedSceneAlphaMode mode)
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

    bool convertLightType(Assets::Assimp::ImportedSceneLightType imported, Renderer::LightType& type)
    {
        switch (imported) {
            case Assets::Assimp::ImportedSceneLightType::Directional:
                type = Renderer::LightType::Directional;
                return true;
            case Assets::Assimp::ImportedSceneLightType::Point:
                type = Renderer::LightType::Point;
                return true;
            case Assets::Assimp::ImportedSceneLightType::Spot:
                type = Renderer::LightType::Spot;
                return true;
            case Assets::Assimp::ImportedSceneLightType::Ambient:
            case Assets::Assimp::ImportedSceneLightType::Area:
            case Assets::Assimp::ImportedSceneLightType::Unknown:
            default:
                break;
        }
        return false;
    }

    bool isFiniteVec3(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    glm::vec3 normalizedOr(const glm::vec3& value, const glm::vec3& fallback)
    {
        if (!isFiniteVec3(value) || glm::length(value) <= 0.0f) {
            return fallback;
        }
        return glm::normalize(value);
    }

    Renderer::LightDescriptor makeLightDescriptor(
        const Assets::Assimp::ImportedScene& scene,
        const Assets::Assimp::ImportedSceneLight& light,
        Engine::AuthoredSceneDiagnostics& diagnostics)
    {
        Renderer::LightDescriptor descriptor;
        descriptor.name = light.name;
        descriptor.color = light.color;
        descriptor.intensity = std::max(light.intensity, 0.0f);
        descriptor.position = light.position;
        descriptor.direction = normalizedOr(light.direction, {0.0f, -1.0f, 0.0f});
        descriptor.range = light.range > 0.0f ? light.range : 25.0f;
        descriptor.innerConeAngle = light.innerConeAngle;
        descriptor.outerConeAngle = light.outerConeAngle > 0.0f ? light.outerConeAngle : 0.7853982f;
        descriptor.enabled = descriptor.intensity > 0.0f;

        if (light.nodeIndex) {
            if (*light.nodeIndex < scene.nodes.size()) {
                const glm::mat4& transform = scene.nodes[*light.nodeIndex].worldTransform;
                descriptor.position = glm::vec3{transform[3]};
                descriptor.direction = normalizedOr(glm::vec3{transform * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}}, descriptor.direction);
            } else {
                ++diagnostics.missingLightTransformCount;
            }
        }

        return descriptor;
    }

    Renderer::MaterialDescriptor::TextureSlotHints convertTextureHints(
        const Assets::Assimp::ImportedSceneTextureHints& imported)
    {
        Renderer::MaterialDescriptor::TextureSlotHints hints;
        hints.colorSpace = imported.colorSpace == Assets::Assimp::ImportedSceneTextureColorSpace::Srgb
            ? Renderer::MaterialDescriptor::TextureColorSpace::Srgb
            : Renderer::MaterialDescriptor::TextureColorSpace::Linear;
        return hints;
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

    Renderer::TextureDescriptor makeTextureDescriptor(
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

    void appendDeferredDiagnostics(
        const Assets::Assimp::ImportedScene& imported,
        Engine::AuthoredSceneDiagnostics& diagnostics)
    {
        diagnostics.deferredAlphaMaterialCount = imported.diagnostics.alphaMaterialCount;
        diagnostics.deferredDoubleSidedMaterialCount = imported.diagnostics.doubleSidedMaterialCount;
        diagnostics.retainedTexcoord1PrimitiveCount = imported.diagnostics.texcoord1PrimitiveCount;
        diagnostics.retainedVertexColorPrimitiveCount = imported.diagnostics.vertexColorPrimitiveCount;

        for (const Assets::Assimp::ImportedSceneMaterial& material : imported.materials) {
            if (material.hasPackedMetallicRoughnessTexture) {
                ++diagnostics.mappedPackedMetallicRoughnessCount;
            }
            if (!material.occlusionTexture.empty()) {
                ++diagnostics.deferredOcclusionTextureCount;
            }
            if (!material.emissiveTexture.empty()) {
                ++diagnostics.deferredEmissiveTextureCount;
            }
        }

    }

    void appendImportDiagnostics(
        const Assets::Assimp::ImportedScene& imported,
        Engine::AuthoredSceneDiagnostics& diagnostics)
    {
        diagnostics.sourceFormat = imported.sourceFormat;
        diagnostics.importedNodeCount = imported.diagnostics.nodeCount;
        diagnostics.importedMeshCount = imported.diagnostics.meshCount;
        diagnostics.importedPrimitiveCount = imported.diagnostics.primitiveCount;
        diagnostics.importedMaterialCount = imported.diagnostics.materialCount;
        diagnostics.importedTextureCount = imported.diagnostics.textureCount;
        diagnostics.importedLightCount = imported.diagnostics.lightCount;
        diagnostics.importedSkinCount = imported.diagnostics.skinCount;
        diagnostics.importedJointCount = imported.diagnostics.jointCount;
        diagnostics.importedAnimationCount = imported.diagnostics.animationCount;
        diagnostics.importedAnimationChannelCount = imported.diagnostics.animationChannelCount;
        diagnostics.boundsValid = imported.bounds.valid;
        diagnostics.warnings = imported.diagnostics.warnings;
        appendDeferredDiagnostics(imported, diagnostics);
    }

    std::string animatedModelRuntimeRequiredMessage()
    {
        return "Authored scene imported successfully but contains skeletal or animation data. Load it through the animated model runtime instead of the static authored scene path.";
    }

    void expandBounds(Assets::Assimp::ImportedSceneBounds& bounds, const glm::vec3& point)
    {
        if (!bounds.valid) {
            bounds.min = point;
            bounds.max = point;
            bounds.valid = true;
            return;
        }

        bounds.min = glm::min(bounds.min, point);
        bounds.max = glm::max(bounds.max, point);
    }

    void expandBounds(Assets::Assimp::ImportedSceneBounds& bounds, const Assets::Assimp::ImportedSceneBounds& other)
    {
        if (!other.valid) {
            return;
        }
        expandBounds(bounds, other.min);
        expandBounds(bounds, other.max);
    }

    Assets::Assimp::ImportedSceneBounds transformBounds(
        const Assets::Assimp::ImportedSceneBounds& bounds,
        const glm::mat4& transform)
    {
        Assets::Assimp::ImportedSceneBounds transformed;
        if (!bounds.valid) {
            return transformed;
        }

        for (uint32_t corner = 0; corner < 8; ++corner) {
            const glm::vec3 point{
                (corner & 1u) ? bounds.max.x : bounds.min.x,
                (corner & 2u) ? bounds.max.y : bounds.min.y,
                (corner & 4u) ? bounds.max.z : bounds.min.z,
            };
            expandBounds(transformed, glm::vec3{transform * glm::vec4{point, 1.0f}});
        }
        return transformed;
    }

    glm::vec3 boundsCenter(const Engine::AuthoredSceneBounds& bounds)
    {
        return (bounds.min + bounds.max) * 0.5f;
    }

    float boundsDistance(const Engine::AuthoredSceneBounds& bounds, const glm::vec3& point)
    {
        const glm::vec3 center = boundsCenter(bounds);
        const glm::vec2 delta{center.x - point.x, center.z - point.z};
        return glm::length(delta);
    }

    template <typename T>
    void appendUnique(std::vector<T>& values, T value)
    {
        if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(value);
        }
    }

    Renderer::MaterialDescriptor makeMaterialDescriptor(
        const Assets::Assimp::ImportedSceneMaterial& material,
        const std::vector<Renderer::TextureHandle>& baseColorTextures,
        const std::vector<Renderer::TextureHandle>& normalTextures,
        const std::vector<Renderer::TextureHandle>& metallicTextures,
        const std::vector<Renderer::TextureHandle>& roughnessTextures,
        const std::vector<Renderer::TextureHandle>& metallicRoughnessTextures,
        const std::vector<Renderer::TextureHandle>& occlusionTextures,
        const std::vector<Renderer::TextureHandle>& emissiveTextures,
        uint32_t materialIndex)
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = material.name.empty()
            ? "authored.material." + std::to_string(materialIndex)
            : material.name;
        descriptor.baseColorFactor = material.baseColorFactor;
        descriptor.baseColorTexture = baseColorTextures[materialIndex];
        descriptor.normalTexture = normalTextures[materialIndex];
        descriptor.normalScale = material.normalScale;
        descriptor.metallicFactor = material.metallicFactor;
        descriptor.roughnessFactor = material.roughnessFactor;
        descriptor.metallicTexture = metallicTextures[materialIndex];
        descriptor.roughnessTexture = roughnessTextures[materialIndex];
        descriptor.metallicRoughnessTexture = metallicRoughnessTextures[materialIndex];
        descriptor.occlusionTexture = occlusionTextures[materialIndex];
        descriptor.occlusionStrength = material.occlusionStrength;
        descriptor.emissiveTexture = emissiveTextures[materialIndex];
        descriptor.emissiveFactor = material.emissiveFactor;
        descriptor.alphaMode = convertAlphaMode(material.alphaMode);
        descriptor.alphaCutoff = material.alphaCutoff;
        descriptor.doubleSided = material.doubleSided;
        descriptor.baseColorTextureHints = convertTextureHints(material.baseColorTextureHints);
        descriptor.normalTextureHints = convertTextureHints(material.normalTextureHints);
        descriptor.metallicTextureHints = convertTextureHints(material.metallicTextureHints);
        descriptor.roughnessTextureHints = convertTextureHints(material.roughnessTextureHints);
        descriptor.metallicRoughnessTextureHints = convertTextureHints(material.metallicRoughnessTextureHints);
        descriptor.occlusionTextureHints = convertTextureHints(material.occlusionTextureHints);
        descriptor.emissiveTextureHints = convertTextureHints(material.emissiveTextureHints);
        return descriptor;
    }

    Renderer::MaterialDescriptor makeMaterialDescriptor(
        const Assets::Assimp::ImportedSceneMaterial& material,
        uint32_t materialIndex)
    {
        static const std::vector<Renderer::TextureHandle> emptyHandles(1);
        std::vector<Renderer::TextureHandle> handles(materialIndex + 1);
        return makeMaterialDescriptor(material, handles, handles, handles, handles, handles, handles, handles, materialIndex);
    }

    Renderer::StaticMeshDescriptor makeMeshDescriptor(
        const Assets::Assimp::ImportedSceneMesh& importedMesh,
        const std::vector<Renderer::MaterialHandle>& materials,
        Engine::AuthoredSceneDiagnostics& diagnostics)
    {
        Renderer::StaticMeshDescriptor descriptor;
        descriptor.name = importedMesh.name;
        descriptor.submeshes.reserve(importedMesh.primitives.size());

        for (const Assets::Assimp::ImportedScenePrimitive& primitive : importedMesh.primitives) {
            Renderer::StaticSubmeshDescriptor submesh;
            submesh.vertices.reserve(primitive.vertices.size());
            submesh.indices = primitive.indices;
            if (primitive.materialIndex < materials.size()) {
                submesh.material = materials[primitive.materialIndex];
            } else {
                ++diagnostics.invalidMaterialReferenceCount;
            }

            for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                submesh.vertices.push_back(convertVertex(vertex));
            }
            descriptor.submeshes.push_back(std::move(submesh));
        }

        return descriptor;
    }

    void countPrimitivePayload(
        const Assets::Assimp::ImportedSceneMesh& mesh,
        Engine::AuthoredSceneSectorManifest& sector)
    {
        for (const Assets::Assimp::ImportedScenePrimitive& primitive : mesh.primitives) {
            ++sector.primitiveCount;
            sector.vertexCount += static_cast<uint32_t>(primitive.vertices.size());
            sector.indexCount += static_cast<uint32_t>(primitive.indices.size());
        }
    }

    void collectMaterialReferences(
        const Assets::Assimp::ImportedSceneMesh& mesh,
        Engine::AuthoredSceneSectorManifest& sector)
    {
        for (const Assets::Assimp::ImportedScenePrimitive& primitive : mesh.primitives) {
            appendUnique(sector.materialIndices, primitive.materialIndex);
        }
    }

    void collectTextureReferences(
        const Assets::Assimp::ImportedScene& scene,
        Engine::AuthoredSceneSectorManifest& sector)
    {
        for (uint32_t textureIndex = 0; textureIndex < scene.textures.size(); ++textureIndex) {
            const Assets::Assimp::ImportedSceneTexture& texture = scene.textures[textureIndex];
            if (std::find(sector.materialIndices.begin(), sector.materialIndices.end(), texture.materialIndex) != sector.materialIndices.end()) {
                appendUnique(sector.textureIndices, textureIndex);
            }
        }
    }

    Engine::AuthoredScenePartition buildPartition(
        const Assets::Assimp::ImportedScene& scene,
        const Engine::AuthoredScenePartitionSettings& settings)
    {
        Engine::AuthoredScenePartition partition;
        partition.bounds = convertBounds(scene.bounds);

        if (!settings.enabled || !scene.bounds.valid || settings.sectorSize <= 0.0f) {
            Engine::AuthoredSceneSectorManifest sector;
            sector.id = {0};
            sector.name = "authored.root";
            sector.bounds = convertBounds(scene.bounds);
            for (uint32_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
                const Assets::Assimp::ImportedSceneNode& node = scene.nodes[nodeIndex];
                if (node.meshIndices.empty()) {
                    continue;
                }
                sector.nodeIndices.push_back(nodeIndex);
                for (uint32_t meshIndex : node.meshIndices) {
                    if (meshIndex < scene.meshes.size()) {
                        appendUnique(sector.meshIndices, meshIndex);
                        collectMaterialReferences(scene.meshes[meshIndex], sector);
                        countPrimitivePayload(scene.meshes[meshIndex], sector);
                    }
                }
            }
            for (uint32_t lightIndex = 0; lightIndex < scene.lights.size(); ++lightIndex) {
                sector.lightIndices.push_back(lightIndex);
            }
            collectTextureReferences(scene, sector);
            partition.sectors.push_back(std::move(sector));
            partition.usedFallbackRootSector = true;
            partition.warnings.push_back("Authored scene partition used a single root sector because partition settings or bounds were invalid.");
            return partition;
        }

        struct SectorKey {
            int32_t x = 0;
            int32_t z = 0;

            bool operator<(const SectorKey& other) const
            {
                return x == other.x ? z < other.z : x < other.x;
            }
        };

        std::map<SectorKey, Engine::AuthoredSceneSectorManifest> sectorsByKey;
        const glm::vec3 origin = scene.bounds.min;

        for (uint32_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
            const Assets::Assimp::ImportedSceneNode& node = scene.nodes[nodeIndex];
            for (uint32_t meshIndex : node.meshIndices) {
                if (meshIndex >= scene.meshes.size()) {
                    continue;
                }

                const Assets::Assimp::ImportedSceneBounds worldBounds =
                    transformBounds(scene.meshes[meshIndex].bounds, node.worldTransform);
                if (!worldBounds.valid) {
                    continue;
                }

                const glm::vec3 center = (worldBounds.min + worldBounds.max) * 0.5f;
                const SectorKey key{
                    static_cast<int32_t>(std::floor((center.x - origin.x) / settings.sectorSize)),
                    static_cast<int32_t>(std::floor((center.z - origin.z) / settings.sectorSize)),
                };
                Engine::AuthoredSceneSectorManifest& sector = sectorsByKey[key];
                sector.name = "authored.sector." + std::to_string(key.x) + "." + std::to_string(key.z);
                appendUnique(sector.nodeIndices, nodeIndex);
                appendUnique(sector.meshIndices, meshIndex);
                Assets::Assimp::ImportedSceneBounds sectorBounds = convertBounds(sector.bounds);
                expandBounds(sectorBounds, worldBounds);
                sector.bounds = convertBounds(sectorBounds);
                collectMaterialReferences(scene.meshes[meshIndex], sector);
                countPrimitivePayload(scene.meshes[meshIndex], sector);
            }
        }

        uint32_t sectorIndex = 0;
        for (auto& [key, sector] : sectorsByKey) {
            (void)key;
            sector.id = {sectorIndex++};
            collectTextureReferences(scene, sector);
            partition.sectors.push_back(std::move(sector));
        }

        for (uint32_t lightIndex = 0; lightIndex < scene.lights.size(); ++lightIndex) {
            const Assets::Assimp::ImportedSceneLight& light = scene.lights[lightIndex];
            uint32_t targetSector = UINT32_MAX;
            if (light.nodeIndex) {
                for (const Engine::AuthoredSceneSectorManifest& sector : partition.sectors) {
                    if (std::find(sector.nodeIndices.begin(), sector.nodeIndices.end(), *light.nodeIndex) != sector.nodeIndices.end()) {
                        targetSector = sector.id.value;
                        break;
                    }
                }
            }
            if (targetSector == UINT32_MAX && !partition.sectors.empty()) {
                float bestDistance = std::numeric_limits<float>::max();
                for (const Engine::AuthoredSceneSectorManifest& sector : partition.sectors) {
                    if (!sector.bounds.valid) {
                        continue;
                    }
                    const float distance = boundsDistance(sector.bounds, light.position);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        targetSector = sector.id.value;
                    }
                }
            }
            if (targetSector < partition.sectors.size()) {
                partition.sectors[targetSector].lightIndices.push_back(lightIndex);
            }
        }

        if (partition.sectors.empty()) {
            Engine::AuthoredScenePartitionSettings fallbackSettings = settings;
            fallbackSettings.enabled = false;
            partition = buildPartition(scene, fallbackSettings);
            partition.warnings.push_back("Authored scene partition produced no sectors and fell back to the root sector.");
        }

        return partition;
    }

    uint64_t fnv1a(const std::string& text)
    {
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char value : text) {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string hexHash(uint64_t hash)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << hash;
        return stream.str();
    }

    std::filesystem::path canonicalForIdentity(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
        if (!error) {
            return canonical;
        }
        canonical = std::filesystem::absolute(path, error);
        return error ? path : canonical;
    }

    YAML::Node vec2Node(const glm::vec2& value)
    {
        YAML::Node node;
        node.push_back(value.x);
        node.push_back(value.y);
        return node;
    }

    YAML::Node vec3Node(const glm::vec3& value)
    {
        YAML::Node node;
        node.push_back(value.x);
        node.push_back(value.y);
        node.push_back(value.z);
        return node;
    }

    YAML::Node vec4Node(const glm::vec4& value)
    {
        YAML::Node node;
        node.push_back(value.x);
        node.push_back(value.y);
        node.push_back(value.z);
        node.push_back(value.w);
        return node;
    }

    glm::vec2 readVec2(const YAML::Node& node, glm::vec2 fallback = {})
    {
        if (!node || !node.IsSequence() || node.size() < 2) {
            return fallback;
        }
        return {node[0].as<float>(fallback.x), node[1].as<float>(fallback.y)};
    }

    glm::vec3 readVec3(const YAML::Node& node, glm::vec3 fallback = {})
    {
        if (!node || !node.IsSequence() || node.size() < 3) {
            return fallback;
        }
        return {node[0].as<float>(fallback.x), node[1].as<float>(fallback.y), node[2].as<float>(fallback.z)};
    }

    glm::vec4 readVec4(const YAML::Node& node, glm::vec4 fallback = {})
    {
        if (!node || !node.IsSequence() || node.size() < 4) {
            return fallback;
        }
        return {
            node[0].as<float>(fallback.x),
            node[1].as<float>(fallback.y),
            node[2].as<float>(fallback.z),
            node[3].as<float>(fallback.w),
        };
    }

    YAML::Node mat4Node(const glm::mat4& value)
    {
        YAML::Node node;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                node.push_back(value[column][row]);
            }
        }
        return node;
    }

    glm::mat4 readMat4(const YAML::Node& node)
    {
        glm::mat4 value{1.0f};
        if (!node || !node.IsSequence() || node.size() < 16) {
            return value;
        }
        uint32_t index = 0;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                value[column][row] = node[index++].as<float>(value[column][row]);
            }
        }
        return value;
    }

    YAML::Node boundsNode(const Assets::Assimp::ImportedSceneBounds& bounds)
    {
        YAML::Node node;
        node["valid"] = bounds.valid;
        node["min"] = vec3Node(bounds.min);
        node["max"] = vec3Node(bounds.max);
        return node;
    }

    Assets::Assimp::ImportedSceneBounds readImportedBounds(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneBounds bounds;
        bounds.valid = node["valid"].as<bool>(false);
        bounds.min = readVec3(node["min"]);
        bounds.max = readVec3(node["max"]);
        return bounds;
    }

    YAML::Node boundsNode(const Engine::AuthoredSceneBounds& bounds)
    {
        return boundsNode(convertBounds(bounds));
    }

    Engine::AuthoredSceneBounds readAuthoredBounds(const YAML::Node& node)
    {
        return convertBounds(readImportedBounds(node));
    }

    YAML::Node uintVectorNode(const std::vector<uint32_t>& values)
    {
        YAML::Node node;
        for (uint32_t value : values) {
            node.push_back(value);
        }
        return node;
    }

    std::vector<uint32_t> readUintVector(const YAML::Node& node)
    {
        std::vector<uint32_t> values;
        if (!node || !node.IsSequence()) {
            return values;
        }
        values.reserve(node.size());
        for (const YAML::Node& item : node) {
            values.push_back(item.as<uint32_t>(UINT32_MAX));
        }
        return values;
    }

    uint32_t unpackChannel(uint32_t color, uint32_t shift)
    {
        return (color >> shift) & 0xffu;
    }

    glm::vec4 unpackColorAbgr(uint32_t color)
    {
        return {
            static_cast<float>(unpackChannel(color, 0)) / 255.0f,
            static_cast<float>(unpackChannel(color, 8)) / 255.0f,
            static_cast<float>(unpackChannel(color, 16)) / 255.0f,
            static_cast<float>(unpackChannel(color, 24)) / 255.0f,
        };
    }

    Assets::Assimp::ImportedSceneVertex importedVertexFromMeshVertex(const Renderer::MeshVertex& vertex)
    {
        Assets::Assimp::ImportedSceneVertex imported;
        imported.position = {vertex.px, vertex.py, vertex.pz};
        imported.normal = {vertex.nx, vertex.ny, vertex.nz};
        imported.tangent = {vertex.tx, vertex.ty, vertex.tz, vertex.tw};
        imported.texcoord0 = {vertex.u, vertex.v};
        imported.texcoord1 = {vertex.u1, vertex.v1};
        imported.color0 = unpackColorAbgr(vertex.abgr);
        imported.hasNormal = true;
        imported.hasTangent = true;
        imported.hasTexcoord0 = true;
        imported.hasTexcoord1 = true;
        imported.hasColor0 = true;
        return imported;
    }

    template <typename T>
    void writeBinary(std::ostream& output, const T& value)
    {
        output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename T>
    bool readBinary(std::istream& input, T& value)
    {
        input.read(reinterpret_cast<char*>(&value), sizeof(T));
        return static_cast<bool>(input);
    }

    std::filesystem::path meshPayloadName(uint32_t meshIndex)
    {
        return "mesh_" + std::to_string(meshIndex) + ".bin";
    }

    bool writeMeshPayload(
        const std::filesystem::path& path,
        const Assets::Assimp::ImportedSceneMesh& mesh,
        std::string& message)
    {
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            message = "Failed to open authored scene mesh cache for writing.";
            return false;
        }

        const uint32_t magic = 0x4345534du; // MSEC
        const uint32_t version = 1;
        const uint32_t primitiveCount = static_cast<uint32_t>(mesh.primitives.size());
        writeBinary(output, magic);
        writeBinary(output, version);
        writeBinary(output, primitiveCount);

        for (const Assets::Assimp::ImportedScenePrimitive& primitive : mesh.primitives) {
            const uint32_t vertexCount = static_cast<uint32_t>(primitive.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(primitive.indices.size());
            writeBinary(output, primitive.materialIndex);
            writeBinary(output, primitive.bounds.min);
            writeBinary(output, primitive.bounds.max);
            writeBinary(output, primitive.bounds.valid);
            writeBinary(output, primitive.hasTexcoord1);
            writeBinary(output, primitive.hasColor0);
            writeBinary(output, primitive.missingNormals);
            writeBinary(output, primitive.missingTangents);
            writeBinary(output, primitive.missingTexcoord0);
            writeBinary(output, vertexCount);
            writeBinary(output, indexCount);

            for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                const Renderer::MeshVertex meshVertex = convertVertex(vertex);
                writeBinary(output, meshVertex);
            }
            for (uint32_t index : primitive.indices) {
                writeBinary(output, index);
            }
        }

        if (!output) {
            message = "Failed to write authored scene mesh cache payload.";
            return false;
        }
        return true;
    }

    bool readMeshPayload(
        const std::filesystem::path& path,
        Assets::Assimp::ImportedSceneMesh& mesh,
        std::string& message)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            message = "Authored scene mesh cache payload is missing.";
            return false;
        }

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t primitiveCount = 0;
        if (!readBinary(input, magic) || !readBinary(input, version) || !readBinary(input, primitiveCount) ||
            magic != 0x4345534du || version != 1) {
            message = "Authored scene mesh cache payload header is invalid.";
            return false;
        }

        mesh.primitives.clear();
        mesh.primitives.reserve(primitiveCount);
        for (uint32_t primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex) {
            Assets::Assimp::ImportedScenePrimitive primitive;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            if (!readBinary(input, primitive.materialIndex) ||
                !readBinary(input, primitive.bounds.min) ||
                !readBinary(input, primitive.bounds.max) ||
                !readBinary(input, primitive.bounds.valid) ||
                !readBinary(input, primitive.hasTexcoord1) ||
                !readBinary(input, primitive.hasColor0) ||
                !readBinary(input, primitive.missingNormals) ||
                !readBinary(input, primitive.missingTangents) ||
                !readBinary(input, primitive.missingTexcoord0) ||
                !readBinary(input, vertexCount) ||
                !readBinary(input, indexCount)) {
                message = "Authored scene mesh cache payload is truncated.";
                return false;
            }

            primitive.vertices.reserve(vertexCount);
            for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                Renderer::MeshVertex meshVertex;
                if (!readBinary(input, meshVertex)) {
                    message = "Authored scene mesh vertex cache payload is truncated.";
                    return false;
                }
                primitive.vertices.push_back(importedVertexFromMeshVertex(meshVertex));
            }

            primitive.indices.reserve(indexCount);
            for (uint32_t index = 0; index < indexCount; ++index) {
                uint32_t value = 0;
                if (!readBinary(input, value)) {
                    message = "Authored scene mesh index cache payload is truncated.";
                    return false;
                }
                primitive.indices.push_back(value);
            }
            mesh.primitives.push_back(std::move(primitive));
        }
        return true;
    }

    YAML::Node textureHintsNode(const Assets::Assimp::ImportedSceneTextureHints& hints)
    {
        YAML::Node node;
        node["color_space"] = static_cast<uint32_t>(hints.colorSpace);
        node["wrap_u"] = static_cast<uint32_t>(hints.wrapU);
        node["wrap_v"] = static_cast<uint32_t>(hints.wrapV);
        node["min_filter"] = static_cast<uint32_t>(hints.minFilter);
        node["mag_filter"] = static_cast<uint32_t>(hints.magFilter);
        return node;
    }

    Assets::Assimp::ImportedSceneTextureHints readTextureHints(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneTextureHints hints;
        hints.colorSpace = static_cast<Assets::Assimp::ImportedSceneTextureColorSpace>(node["color_space"].as<uint32_t>(0));
        hints.wrapU = static_cast<Assets::Assimp::ImportedSceneTextureWrap>(node["wrap_u"].as<uint32_t>(1));
        hints.wrapV = static_cast<Assets::Assimp::ImportedSceneTextureWrap>(node["wrap_v"].as<uint32_t>(1));
        hints.minFilter = static_cast<Assets::Assimp::ImportedSceneTextureFilter>(node["min_filter"].as<uint32_t>(2));
        hints.magFilter = static_cast<Assets::Assimp::ImportedSceneTextureFilter>(node["mag_filter"].as<uint32_t>(2));
        return hints;
    }

    YAML::Node materialNode(const Assets::Assimp::ImportedSceneMaterial& material)
    {
        YAML::Node node;
        node["name"] = material.name;
        node["base_color_factor"] = vec4Node(material.baseColorFactor);
        node["metallic_factor"] = material.metallicFactor;
        node["roughness_factor"] = material.roughnessFactor;
        node["base_color_texture"] = material.baseColorTexture.generic_string();
        node["normal_texture"] = material.normalTexture.generic_string();
        node["normal_scale"] = material.normalScale;
        node["metallic_texture"] = material.metallicTexture.generic_string();
        node["roughness_texture"] = material.roughnessTexture.generic_string();
        node["metallic_roughness_texture"] = material.metallicRoughnessTexture.generic_string();
        node["has_packed_metallic_roughness_texture"] = material.hasPackedMetallicRoughnessTexture;
        node["occlusion_texture"] = material.occlusionTexture.generic_string();
        node["occlusion_strength"] = material.occlusionStrength;
        node["emissive_texture"] = material.emissiveTexture.generic_string();
        node["emissive_factor"] = vec3Node(material.emissiveFactor);
        node["base_color_hints"] = textureHintsNode(material.baseColorTextureHints);
        node["normal_hints"] = textureHintsNode(material.normalTextureHints);
        node["metallic_hints"] = textureHintsNode(material.metallicTextureHints);
        node["roughness_hints"] = textureHintsNode(material.roughnessTextureHints);
        node["metallic_roughness_hints"] = textureHintsNode(material.metallicRoughnessTextureHints);
        node["occlusion_hints"] = textureHintsNode(material.occlusionTextureHints);
        node["emissive_hints"] = textureHintsNode(material.emissiveTextureHints);
        node["alpha_mode"] = static_cast<uint32_t>(material.alphaMode);
        node["alpha_cutoff"] = material.alphaCutoff;
        node["double_sided"] = material.doubleSided;
        return node;
    }

    Assets::Assimp::ImportedSceneMaterial readMaterial(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneMaterial material;
        material.name = node["name"].as<std::string>(std::string{});
        material.baseColorFactor = readVec4(node["base_color_factor"], glm::vec4{1.0f});
        material.metallicFactor = node["metallic_factor"].as<float>(0.0f);
        material.roughnessFactor = node["roughness_factor"].as<float>(1.0f);
        material.baseColorTexture = node["base_color_texture"].as<std::string>(std::string{});
        material.normalTexture = node["normal_texture"].as<std::string>(std::string{});
        material.normalScale = node["normal_scale"].as<float>(1.0f);
        material.metallicTexture = node["metallic_texture"].as<std::string>(std::string{});
        material.roughnessTexture = node["roughness_texture"].as<std::string>(std::string{});
        material.metallicRoughnessTexture = node["metallic_roughness_texture"].as<std::string>(std::string{});
        material.hasPackedMetallicRoughnessTexture = node["has_packed_metallic_roughness_texture"].as<bool>(false);
        material.occlusionTexture = node["occlusion_texture"].as<std::string>(std::string{});
        material.occlusionStrength = node["occlusion_strength"].as<float>(1.0f);
        material.emissiveTexture = node["emissive_texture"].as<std::string>(std::string{});
        material.emissiveFactor = readVec3(node["emissive_factor"]);
        material.baseColorTextureHints = readTextureHints(node["base_color_hints"]);
        material.normalTextureHints = readTextureHints(node["normal_hints"]);
        material.metallicTextureHints = readTextureHints(node["metallic_hints"]);
        material.roughnessTextureHints = readTextureHints(node["roughness_hints"]);
        material.metallicRoughnessTextureHints = readTextureHints(node["metallic_roughness_hints"]);
        material.occlusionTextureHints = readTextureHints(node["occlusion_hints"]);
        material.emissiveTextureHints = readTextureHints(node["emissive_hints"]);
        material.alphaMode = static_cast<Assets::Assimp::ImportedSceneAlphaMode>(node["alpha_mode"].as<uint32_t>(0));
        material.alphaCutoff = node["alpha_cutoff"].as<float>(0.5f);
        material.doubleSided = node["double_sided"].as<bool>(false);
        return material;
    }

    YAML::Node textureNode(const Assets::Assimp::ImportedSceneTexture& texture)
    {
        YAML::Node node;
        node["path"] = texture.path.generic_string();
        node["semantic"] = static_cast<uint32_t>(texture.semantic);
        node["material_index"] = texture.materialIndex;
        node["texcoord_index"] = texture.texcoordIndex;
        node["may_be_packed_metallic_roughness"] = texture.mayBePackedMetallicRoughness;
        node["hints"] = textureHintsNode(texture.hints);
        return node;
    }

    Assets::Assimp::ImportedSceneTexture readTexture(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneTexture texture;
        texture.path = node["path"].as<std::string>(std::string{});
        texture.semantic = static_cast<Assets::Assimp::ImportedSceneTextureSemantic>(node["semantic"].as<uint32_t>(0));
        texture.materialIndex = node["material_index"].as<uint32_t>(UINT32_MAX);
        texture.texcoordIndex = node["texcoord_index"].as<int>(0);
        texture.mayBePackedMetallicRoughness = node["may_be_packed_metallic_roughness"].as<bool>(false);
        texture.hints = readTextureHints(node["hints"]);
        return texture;
    }

    YAML::Node lightNode(const Assets::Assimp::ImportedSceneLight& light)
    {
        YAML::Node node;
        node["name"] = light.name;
        node["type"] = static_cast<uint32_t>(light.type);
        node["color"] = vec3Node(light.color);
        node["intensity"] = light.intensity;
        node["position"] = vec3Node(light.position);
        node["direction"] = vec3Node(light.direction);
        node["range"] = light.range;
        node["inner_cone_angle"] = light.innerConeAngle;
        node["outer_cone_angle"] = light.outerConeAngle;
        node["has_node"] = light.nodeIndex.has_value();
        node["node_index"] = light.nodeIndex.value_or(UINT32_MAX);
        return node;
    }

    Assets::Assimp::ImportedSceneLight readLight(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneLight light;
        light.name = node["name"].as<std::string>(std::string{});
        light.type = static_cast<Assets::Assimp::ImportedSceneLightType>(node["type"].as<uint32_t>(0));
        light.color = readVec3(node["color"], glm::vec3{1.0f});
        light.intensity = node["intensity"].as<float>(1.0f);
        light.position = readVec3(node["position"]);
        light.direction = readVec3(node["direction"], {0.0f, -1.0f, 0.0f});
        light.range = node["range"].as<float>(0.0f);
        light.innerConeAngle = node["inner_cone_angle"].as<float>(0.0f);
        light.outerConeAngle = node["outer_cone_angle"].as<float>(0.0f);
        if (node["has_node"].as<bool>(false)) {
            light.nodeIndex = node["node_index"].as<uint32_t>(UINT32_MAX);
        }
        return light;
    }

    YAML::Node sectorNode(const Engine::AuthoredSceneSectorManifest& sector)
    {
        YAML::Node node;
        node["id"] = sector.id.value;
        node["name"] = sector.name;
        node["bounds"] = boundsNode(sector.bounds);
        node["nodes"] = uintVectorNode(sector.nodeIndices);
        node["meshes"] = uintVectorNode(sector.meshIndices);
        node["materials"] = uintVectorNode(sector.materialIndices);
        node["textures"] = uintVectorNode(sector.textureIndices);
        node["lights"] = uintVectorNode(sector.lightIndices);
        node["primitive_count"] = sector.primitiveCount;
        node["vertex_count"] = sector.vertexCount;
        node["index_count"] = sector.indexCount;
        return node;
    }

    Engine::AuthoredSceneSectorManifest readSector(const YAML::Node& node)
    {
        Engine::AuthoredSceneSectorManifest sector;
        sector.id = {node["id"].as<uint32_t>(UINT32_MAX)};
        sector.name = node["name"].as<std::string>(std::string{});
        sector.bounds = readAuthoredBounds(node["bounds"]);
        sector.nodeIndices = readUintVector(node["nodes"]);
        sector.meshIndices = readUintVector(node["meshes"]);
        sector.materialIndices = readUintVector(node["materials"]);
        sector.textureIndices = readUintVector(node["textures"]);
        sector.lightIndices = readUintVector(node["lights"]);
        sector.primitiveCount = node["primitive_count"].as<uint32_t>(0);
        sector.vertexCount = node["vertex_count"].as<uint32_t>(0);
        sector.indexCount = node["index_count"].as<uint32_t>(0);
        return sector;
    }

    YAML::Node diagnosticsNode(const Assets::Assimp::ImportedSceneDiagnostics& diagnostics)
    {
        YAML::Node node;
        node["source_format"] = Assets::Assimp::sourceFormatName(diagnostics.sourceFormat);
        node["node_count"] = diagnostics.nodeCount;
        node["mesh_node_count"] = diagnostics.meshNodeCount;
        node["mesh_count"] = diagnostics.meshCount;
        node["primitive_count"] = diagnostics.primitiveCount;
        node["material_count"] = diagnostics.materialCount;
        node["texture_count"] = diagnostics.textureCount;
        node["light_count"] = diagnostics.lightCount;
        node["alpha_material_count"] = diagnostics.alphaMaterialCount;
        node["double_sided_material_count"] = diagnostics.doubleSidedMaterialCount;
        node["texcoord1_primitive_count"] = diagnostics.texcoord1PrimitiveCount;
        node["vertex_color_primitive_count"] = diagnostics.vertexColorPrimitiveCount;
        node["missing_normal_primitive_count"] = diagnostics.missingNormalPrimitiveCount;
        node["missing_tangent_primitive_count"] = diagnostics.missingTangentPrimitiveCount;
        node["missing_texcoord0_primitive_count"] = diagnostics.missingTexcoord0PrimitiveCount;
        node["skin_count"] = diagnostics.skinCount;
        node["joint_count"] = diagnostics.jointCount;
        node["skinned_mesh_count"] = diagnostics.skinnedMeshCount;
        node["influenced_vertex_count"] = diagnostics.influencedVertexCount;
        node["max_influences_per_vertex"] = diagnostics.maxInfluencesPerVertex;
        node["zero_weight_vertex_count"] = diagnostics.zeroWeightVertexCount;
        node["non_normalized_weight_vertex_count"] = diagnostics.nonNormalizedWeightVertexCount;
        node["over_four_influence_vertex_count"] = diagnostics.overFourInfluenceVertexCount;
        node["animation_count"] = diagnostics.animationCount;
        node["animation_channel_count"] = diagnostics.animationChannelCount;
        node["translation_key_count"] = diagnostics.translationKeyCount;
        node["rotation_key_count"] = diagnostics.rotationKeyCount;
        node["scale_key_count"] = diagnostics.scaleKeyCount;
        node["missing_animation_target_count"] = diagnostics.missingAnimationTargetCount;
        node["embedded_texture_count"] = diagnostics.embeddedTextureCount;
        node["missing_pbr_material_count"] = diagnostics.missingPbrMaterialCount;
        node["fbx_unit_scale_factor"] = diagnostics.fbxUnitScaleFactor;
        node["fbx_up_axis"] = diagnostics.fbxUpAxis;
        node["fbx_up_axis_sign"] = diagnostics.fbxUpAxisSign;
        node["fbx_animation_stack_count"] = diagnostics.fbxAnimationStackCount;
        node["fbx_unnamed_animation_stack_count"] = diagnostics.fbxUnnamedAnimationStackCount;
        node["fbx_animation_semantic_warning_count"] = diagnostics.fbxAnimationSemanticWarningCount;
        YAML::Node warnings;
        for (const std::string& warning : diagnostics.warnings) {
            warnings.push_back(warning);
        }
        node["warnings"] = warnings;
        return node;
    }

    Assets::Assimp::ImportedSceneSourceFormat readSourceFormat(const YAML::Node& node)
    {
        const std::string value = node.as<std::string>(std::string{});
        if (value == "gltf") {
            return Assets::Assimp::ImportedSceneSourceFormat::Gltf;
        }
        if (value == "glb") {
            return Assets::Assimp::ImportedSceneSourceFormat::Glb;
        }
        if (value == "fbx") {
            return Assets::Assimp::ImportedSceneSourceFormat::Fbx;
        }
        return Assets::Assimp::ImportedSceneSourceFormat::Unknown;
    }

    Assets::Assimp::ImportedSceneDiagnostics readDiagnostics(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneDiagnostics diagnostics;
        diagnostics.sourceFormat = readSourceFormat(node["source_format"]);
        diagnostics.nodeCount = node["node_count"].as<uint32_t>(0);
        diagnostics.meshNodeCount = node["mesh_node_count"].as<uint32_t>(0);
        diagnostics.meshCount = node["mesh_count"].as<uint32_t>(0);
        diagnostics.primitiveCount = node["primitive_count"].as<uint32_t>(0);
        diagnostics.materialCount = node["material_count"].as<uint32_t>(0);
        diagnostics.textureCount = node["texture_count"].as<uint32_t>(0);
        diagnostics.lightCount = node["light_count"].as<uint32_t>(0);
        diagnostics.alphaMaterialCount = node["alpha_material_count"].as<uint32_t>(0);
        diagnostics.doubleSidedMaterialCount = node["double_sided_material_count"].as<uint32_t>(0);
        diagnostics.texcoord1PrimitiveCount = node["texcoord1_primitive_count"].as<uint32_t>(0);
        diagnostics.vertexColorPrimitiveCount = node["vertex_color_primitive_count"].as<uint32_t>(0);
        diagnostics.missingNormalPrimitiveCount = node["missing_normal_primitive_count"].as<uint32_t>(0);
        diagnostics.missingTangentPrimitiveCount = node["missing_tangent_primitive_count"].as<uint32_t>(0);
        diagnostics.missingTexcoord0PrimitiveCount = node["missing_texcoord0_primitive_count"].as<uint32_t>(0);
        diagnostics.skinCount = node["skin_count"].as<uint32_t>(0);
        diagnostics.jointCount = node["joint_count"].as<uint32_t>(0);
        diagnostics.skinnedMeshCount = node["skinned_mesh_count"].as<uint32_t>(0);
        diagnostics.influencedVertexCount = node["influenced_vertex_count"].as<uint32_t>(0);
        diagnostics.maxInfluencesPerVertex = node["max_influences_per_vertex"].as<uint32_t>(0);
        diagnostics.zeroWeightVertexCount = node["zero_weight_vertex_count"].as<uint32_t>(0);
        diagnostics.nonNormalizedWeightVertexCount = node["non_normalized_weight_vertex_count"].as<uint32_t>(0);
        diagnostics.overFourInfluenceVertexCount = node["over_four_influence_vertex_count"].as<uint32_t>(0);
        diagnostics.animationCount = node["animation_count"].as<uint32_t>(0);
        diagnostics.animationChannelCount = node["animation_channel_count"].as<uint32_t>(0);
        diagnostics.translationKeyCount = node["translation_key_count"].as<uint32_t>(0);
        diagnostics.rotationKeyCount = node["rotation_key_count"].as<uint32_t>(0);
        diagnostics.scaleKeyCount = node["scale_key_count"].as<uint32_t>(0);
        diagnostics.missingAnimationTargetCount = node["missing_animation_target_count"].as<uint32_t>(0);
        diagnostics.embeddedTextureCount = node["embedded_texture_count"].as<uint32_t>(0);
        diagnostics.missingPbrMaterialCount = node["missing_pbr_material_count"].as<uint32_t>(0);
        diagnostics.fbxUnitScaleFactor = node["fbx_unit_scale_factor"].as<float>(1.0f);
        diagnostics.fbxUpAxis = node["fbx_up_axis"].as<int32_t>(1);
        diagnostics.fbxUpAxisSign = node["fbx_up_axis_sign"].as<int32_t>(1);
        diagnostics.fbxAnimationStackCount = node["fbx_animation_stack_count"].as<uint32_t>(0);
        diagnostics.fbxUnnamedAnimationStackCount = node["fbx_unnamed_animation_stack_count"].as<uint32_t>(0);
        diagnostics.fbxAnimationSemanticWarningCount = node["fbx_animation_semantic_warning_count"].as<uint32_t>(0);
        if (const YAML::Node warnings = node["warnings"]; warnings && warnings.IsSequence()) {
            for (const YAML::Node& warning : warnings) {
                diagnostics.warnings.push_back(warning.as<std::string>(std::string{}));
            }
        }
        return diagnostics;
    }
}

namespace Engine {
    struct PartitionedAuthoredScene::ImportedStorage {
        Assets::Assimp::ImportedScene scene;
    };

    const char* cacheStatusName(AuthoredSceneCacheStatus status)
    {
        switch (status) {
            case AuthoredSceneCacheStatus::Hit:
                return "hit";
            case AuthoredSceneCacheStatus::Stale:
                return "stale";
            case AuthoredSceneCacheStatus::Corrupt:
                return "corrupt";
            case AuthoredSceneCacheStatus::WriteSuccess:
                return "write-success";
            case AuthoredSceneCacheStatus::WriteFailed:
                return "write-failed";
            case AuthoredSceneCacheStatus::Cancelled:
                return "cancelled";
            case AuthoredSceneCacheStatus::Miss:
            default:
                return "miss";
        }
    }

    std::string quotedYamlString(const std::string& value)
    {
        std::string output = "\"";
        for (char character : value) {
            if (character == '\\' || character == '"') {
                output.push_back('\\');
            }
            output.push_back(character);
        }
        output.push_back('"');
        return output;
    }

    AuthoredSceneDiagnosticsSummary summarizeAuthoredSceneDiagnostics(const AuthoredSceneDiagnostics& diagnostics)
    {
        AuthoredSceneDiagnosticsSummary summary;
        summary.sourceFormat = diagnostics.sourceFormat;
        summary.sourceFormatName = Assets::Assimp::sourceFormatName(diagnostics.sourceFormat);
        summary.importedNodeCount = diagnostics.importedNodeCount;
        summary.importedMeshCount = diagnostics.importedMeshCount;
        summary.importedPrimitiveCount = diagnostics.importedPrimitiveCount;
        summary.importedMaterialCount = diagnostics.importedMaterialCount;
        summary.importedTextureCount = diagnostics.importedTextureCount;
        summary.importedLightCount = diagnostics.importedLightCount;
        summary.importedSkinCount = diagnostics.importedSkinCount;
        summary.importedJointCount = diagnostics.importedJointCount;
        summary.importedAnimationCount = diagnostics.importedAnimationCount;
        summary.importedAnimationChannelCount = diagnostics.importedAnimationChannelCount;
        summary.createdMeshCount = diagnostics.createdMeshCount;
        summary.createdMaterialCount = diagnostics.createdMaterialCount;
        summary.createdInstanceCount = diagnostics.createdInstanceCount;
        summary.createdLightCount = diagnostics.createdLightCount;
        summary.textureLoadSuccessCount = diagnostics.textureLoadSuccessCount;
        summary.textureLoadFailureCount = diagnostics.textureLoadFailureCount;
        summary.fallbackTextureCount = diagnostics.fallbackTextureCount;
        summary.textureEstimatedBytes = diagnostics.textureEstimatedBytes;
        summary.disabledZeroIntensityLightCount = diagnostics.disabledZeroIntensityLightCount;
        summary.skippedOverBudgetLightCount = diagnostics.skippedOverBudgetLightCount;
        summary.activeAuthoredLightCount = diagnostics.activeAuthoredLightCount;
        summary.totalSectorCount = diagnostics.totalSectorCount;
        summary.loadedSectorCount = diagnostics.loadedSectorCount;
        summary.pendingLoadSectorCount = diagnostics.pendingLoadSectorCount;
        summary.pendingUnloadSectorCount = diagnostics.pendingUnloadSectorCount;
        summary.failedSectorCount = diagnostics.failedSectorCount;
        summary.sectorEstimatedBytes = diagnostics.sectorEstimatedBytes;
        summary.cacheStatus = diagnostics.cacheStatus;
        summary.loadedFromCache = diagnostics.loadedFromCache;
        summary.cacheReadCount = diagnostics.cacheReadCount;
        summary.cacheWriteCount = diagnostics.cacheWriteCount;
        summary.cacheMissCount = diagnostics.cacheMissCount;
        summary.cacheStaleCount = diagnostics.cacheStaleCount;
        summary.cacheCorruptCount = diagnostics.cacheCorruptCount;
        summary.cacheIdentityHash = diagnostics.cacheIdentityHash;
        summary.cachePath = diagnostics.cachePath;
        summary.cacheMessage = diagnostics.cacheMessage;
        summary.asyncPhase = diagnostics.asyncPhase;
        summary.asyncJobsQueued = diagnostics.asyncJobsQueued;
        summary.asyncJobsCompleted = diagnostics.asyncJobsCompleted;
        summary.asyncJobsFailed = diagnostics.asyncJobsFailed;
        summary.asyncPendingJobs = diagnostics.asyncPendingJobs;
        summary.asyncCacheReadMs = diagnostics.asyncCacheReadMs;
        summary.asyncImportMs = diagnostics.asyncImportMs;
        summary.asyncCacheWriteMs = diagnostics.asyncCacheWriteMs;
        summary.asyncMessage = diagnostics.asyncMessage;
        summary.boundsValid = diagnostics.boundsValid;
        summary.warningCount = static_cast<uint32_t>(diagnostics.warnings.size());
        if (!diagnostics.warnings.empty()) {
            summary.lastWarning = diagnostics.warnings.back();
        }
        return summary;
    }

    std::string authoredSceneDiagnosticsSummaryText(const AuthoredSceneDiagnostics& diagnostics)
    {
        const AuthoredSceneDiagnosticsSummary summary = summarizeAuthoredSceneDiagnostics(diagnostics);
        std::ostringstream output;
        output
            << "sourceFormat=" << summary.sourceFormatName
            << "; imported nodes=" << summary.importedNodeCount
            << " meshes=" << summary.importedMeshCount
            << " primitives=" << summary.importedPrimitiveCount
            << " materials=" << summary.importedMaterialCount
            << " textures=" << summary.importedTextureCount
            << " lights=" << summary.importedLightCount
            << " skins=" << summary.importedSkinCount
            << " joints=" << summary.importedJointCount
            << " animations=" << summary.importedAnimationCount
            << "; created meshes=" << summary.createdMeshCount
            << " materials=" << summary.createdMaterialCount
            << " instances=" << summary.createdInstanceCount
            << " lights=" << summary.createdLightCount
            << "; textures ok=" << summary.textureLoadSuccessCount
            << " failed=" << summary.textureLoadFailureCount
            << " fallback=" << summary.fallbackTextureCount
            << " bytes=" << summary.textureEstimatedBytes
            << "; sectors " << summary.loadedSectorCount << "/" << summary.totalSectorCount
            << " pending=" << (summary.pendingLoadSectorCount + summary.pendingUnloadSectorCount)
            << " failed=" << summary.failedSectorCount
            << "; cache=" << cacheStatusName(summary.cacheStatus)
            << " loadedFromCache=" << (summary.loadedFromCache ? "true" : "false")
            << " reads=" << summary.cacheReadCount
            << " writes=" << summary.cacheWriteCount
            << "; async=" << (summary.asyncPhase.empty() ? "n/a" : summary.asyncPhase)
            << " queued=" << summary.asyncJobsQueued
            << " completed=" << summary.asyncJobsCompleted
            << " failed=" << summary.asyncJobsFailed
            << " pending=" << summary.asyncPendingJobs
            << " importMs=" << std::fixed << std::setprecision(3) << summary.asyncImportMs
            << " cacheReadMs=" << summary.asyncCacheReadMs
            << " cacheWriteMs=" << summary.asyncCacheWriteMs
            << "; boundsValid=" << (summary.boundsValid ? "true" : "false")
            << "; warnings=" << summary.warningCount;
        if (!summary.lastWarning.empty()) {
            output << "; lastWarning=" << summary.lastWarning;
        }
        return output.str();
    }

    std::string authoredSceneDiagnosticsSummaryYaml(const AuthoredSceneDiagnostics& diagnostics)
    {
        const AuthoredSceneDiagnosticsSummary summary = summarizeAuthoredSceneDiagnostics(diagnostics);
        std::ostringstream output;
        output << "authored_scene:\n";
        output << "  source_format: " << quotedYamlString(summary.sourceFormatName) << '\n';
        output << "  imported:\n";
        output << "    nodes: " << summary.importedNodeCount << '\n';
        output << "    meshes: " << summary.importedMeshCount << '\n';
        output << "    primitives: " << summary.importedPrimitiveCount << '\n';
        output << "    materials: " << summary.importedMaterialCount << '\n';
        output << "    textures: " << summary.importedTextureCount << '\n';
        output << "    lights: " << summary.importedLightCount << '\n';
        output << "    skins: " << summary.importedSkinCount << '\n';
        output << "    joints: " << summary.importedJointCount << '\n';
        output << "    animations: " << summary.importedAnimationCount << '\n';
        output << "    animation_channels: " << summary.importedAnimationChannelCount << '\n';
        output << "  created:\n";
        output << "    meshes: " << summary.createdMeshCount << '\n';
        output << "    materials: " << summary.createdMaterialCount << '\n';
        output << "    instances: " << summary.createdInstanceCount << '\n';
        output << "    lights: " << summary.createdLightCount << '\n';
        output << "  textures:\n";
        output << "    loaded: " << summary.textureLoadSuccessCount << '\n';
        output << "    failed: " << summary.textureLoadFailureCount << '\n';
        output << "    fallback: " << summary.fallbackTextureCount << '\n';
        output << "    estimated_bytes: " << summary.textureEstimatedBytes << '\n';
        output << "  lights:\n";
        output << "    active: " << summary.activeAuthoredLightCount << '\n';
        output << "    disabled_zero_intensity: " << summary.disabledZeroIntensityLightCount << '\n';
        output << "    skipped_over_budget: " << summary.skippedOverBudgetLightCount << '\n';
        output << "  sectors:\n";
        output << "    total: " << summary.totalSectorCount << '\n';
        output << "    loaded: " << summary.loadedSectorCount << '\n';
        output << "    pending_load: " << summary.pendingLoadSectorCount << '\n';
        output << "    pending_unload: " << summary.pendingUnloadSectorCount << '\n';
        output << "    failed: " << summary.failedSectorCount << '\n';
        output << "    estimated_bytes: " << summary.sectorEstimatedBytes << '\n';
        output << "  cache:\n";
        output << "    status: " << cacheStatusName(summary.cacheStatus) << '\n';
        output << "    loaded_from_cache: " << (summary.loadedFromCache ? "true" : "false") << '\n';
        output << "    reads: " << summary.cacheReadCount << '\n';
        output << "    writes: " << summary.cacheWriteCount << '\n';
        output << "    misses: " << summary.cacheMissCount << '\n';
        output << "    stale: " << summary.cacheStaleCount << '\n';
        output << "    corrupt: " << summary.cacheCorruptCount << '\n';
        output << "    identity: " << quotedYamlString(summary.cacheIdentityHash) << '\n';
        output << "    path: " << quotedYamlString(summary.cachePath.generic_string()) << '\n';
        output << "    message: " << quotedYamlString(summary.cacheMessage) << '\n';
        output << "  async:\n";
        output << "    phase: " << quotedYamlString(summary.asyncPhase) << '\n';
        output << "    queued: " << summary.asyncJobsQueued << '\n';
        output << "    completed: " << summary.asyncJobsCompleted << '\n';
        output << "    failed: " << summary.asyncJobsFailed << '\n';
        output << "    pending: " << summary.asyncPendingJobs << '\n';
        output << "    cache_read_ms: " << summary.asyncCacheReadMs << '\n';
        output << "    import_ms: " << summary.asyncImportMs << '\n';
        output << "    cache_write_ms: " << summary.asyncCacheWriteMs << '\n';
        output << "    message: " << quotedYamlString(summary.asyncMessage) << '\n';
        output << "  bounds_valid: " << (summary.boundsValid ? "true" : "false") << '\n';
        output << "  warnings: " << summary.warningCount << '\n';
        output << "  last_warning: " << quotedYamlString(summary.lastWarning) << '\n';
        return output.str();
    }

    AuthoredSceneCache::AuthoredSceneCache(AuthoredSceneCacheManifest manifest)
        : manifest_(std::move(manifest))
    {
    }

    AuthoredSceneCacheManifest AuthoredSceneCache::buildManifest(
        AuthoredSceneCacheSettings settings,
        const std::filesystem::path& sourcePath,
        const AuthoredScenePartitionSettings& partition)
    {
        AuthoredSceneCacheManifest manifest;
        manifest.settings = std::move(settings);
        manifest.partition = partition;
        manifest.sourcePath = canonicalForIdentity(sourcePath);
        manifest.sourceHash = hashFile(manifest.sourcePath);

        std::ostringstream identity;
        identity << manifest.sourcePath.generic_string() << '|'
                 << manifest.sourceHash << '|'
                 << manifest.settings.formatVersion << '|'
                 << manifest.settings.importerVersion << '|'
                 << manifest.settings.materialPipelineVersion << '|'
                 << manifest.settings.texturePolicyVersion << '|'
                 << manifest.settings.vertexFormatVersion << '|'
                 << manifest.settings.partitionVersion << '|'
                 << manifest.partition.enabled << '|'
                 << manifest.partition.sectorSize;
        manifest.identityHash = hexHash(fnv1a(identity.str()));
        return manifest;
    }

    std::string AuthoredSceneCache::hashFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream stream;
        stream << file.rdbuf();
        return hexHash(fnv1a(stream.str()));
    }

    std::filesystem::path AuthoredSceneCache::cacheRoot(const AuthoredSceneCacheManifest& manifest)
    {
        return manifest.settings.rootPath / manifest.identityHash;
    }

    const AuthoredSceneCacheManifest& AuthoredSceneCache::manifest() const
    {
        return manifest_;
    }

    const AuthoredSceneCacheStats& AuthoredSceneCache::stats() const
    {
        return stats_;
    }

    void AuthoredSceneCache::clearStats()
    {
        stats_ = {};
    }

    void AuthoredSceneCache::recordReadResult(const AuthoredSceneCacheOperationResult& result)
    {
        stats_.lastPath = result.path;
        stats_.lastMessage = result.message;
        switch (result.status) {
            case AuthoredSceneCacheStatus::Hit:
                ++stats_.hits;
                break;
            case AuthoredSceneCacheStatus::Stale:
                ++stats_.stale;
                break;
            case AuthoredSceneCacheStatus::Corrupt:
                ++stats_.corrupt;
                break;
            case AuthoredSceneCacheStatus::Miss:
            default:
                ++stats_.misses;
                break;
        }
    }

    void AuthoredSceneCache::recordWriteResult(const AuthoredSceneCacheOperationResult& result)
    {
        stats_.lastPath = result.path;
        stats_.lastMessage = result.message;
        if (result.status == AuthoredSceneCacheStatus::WriteSuccess) {
            ++stats_.writes;
        }
    }

    AuthoredSceneCacheReadResult AuthoredSceneCache::read()
    {
        AuthoredSceneCacheReadResult result = read(manifest_);
        recordReadResult(result);
        return result;
    }

    AuthoredSceneCacheWriteResult AuthoredSceneCache::write(const AuthoredSceneCachePayload& payload)
    {
        AuthoredSceneCacheWriteResult result = write(manifest_, payload);
        recordWriteResult(result);
        return result;
    }

    AuthoredSceneCacheReadResult AuthoredSceneCache::read(const AuthoredSceneCacheManifest& manifest)
    {
        AuthoredSceneCacheReadResult result;
        const std::filesystem::path rootPath = cacheRoot(manifest);
        const std::filesystem::path manifestPath = rootPath / "manifest.yaml";
        const std::filesystem::path scenePath = rootPath / "scene.yaml";
        result.path = manifestPath;

        if (!std::filesystem::exists(manifestPath) || !std::filesystem::exists(scenePath)) {
            result.status = AuthoredSceneCacheStatus::Miss;
            result.message = "Authored scene cache miss.";
            return result;
        }

        try {
            const YAML::Node manifestRoot = YAML::LoadFile(manifestPath.string());
            if (manifestRoot["identity_hash"].as<std::string>(std::string{}) != manifest.identityHash ||
                manifestRoot["source_hash"].as<std::string>(std::string{}) != manifest.sourceHash ||
                manifestRoot["format_version"].as<uint32_t>(0) != manifest.settings.formatVersion) {
                result.status = AuthoredSceneCacheStatus::Stale;
                result.message = "Authored scene cache identity mismatch.";
                return result;
            }

            const YAML::Node root = YAML::LoadFile(scenePath.string());
            AuthoredSceneCachePayload payload;
            payload.scene.success = true;
            payload.scene.sourceFormat = readSourceFormat(root["source_format"]);
            payload.scene.rootNodeIndex = root["root_node_index"].as<uint32_t>(UINT32_MAX);
            payload.scene.bounds = readImportedBounds(root["bounds"]);
            payload.scene.diagnostics = readDiagnostics(root["diagnostics"]);
            if (payload.scene.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Unknown) {
                payload.scene.sourceFormat = payload.scene.diagnostics.sourceFormat;
            }
            if (payload.scene.diagnostics.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Unknown) {
                payload.scene.diagnostics.sourceFormat = payload.scene.sourceFormat;
            }

            if (const YAML::Node nodes = root["nodes"]; nodes && nodes.IsSequence()) {
                payload.scene.nodes.reserve(nodes.size());
                for (const YAML::Node& item : nodes) {
                    Assets::Assimp::ImportedSceneNode node;
                    node.name = item["name"].as<std::string>(std::string{});
                    node.parentIndex = item["parent"].as<uint32_t>(UINT32_MAX);
                    node.childIndices = readUintVector(item["children"]);
                    node.meshIndices = readUintVector(item["meshes"]);
                    node.localTransform = readMat4(item["local_transform"]);
                    node.worldTransform = readMat4(item["world_transform"]);
                    payload.scene.nodes.push_back(std::move(node));
                }
            }

            if (const YAML::Node materials = root["materials"]; materials && materials.IsSequence()) {
                payload.scene.materials.reserve(materials.size());
                for (const YAML::Node& item : materials) {
                    payload.scene.materials.push_back(readMaterial(item));
                }
            }

            if (const YAML::Node textures = root["textures"]; textures && textures.IsSequence()) {
                payload.scene.textures.reserve(textures.size());
                for (const YAML::Node& item : textures) {
                    payload.scene.textures.push_back(readTexture(item));
                }
            }

            if (const YAML::Node lights = root["lights"]; lights && lights.IsSequence()) {
                payload.scene.lights.reserve(lights.size());
                for (const YAML::Node& item : lights) {
                    payload.scene.lights.push_back(readLight(item));
                }
            }

            if (const YAML::Node meshes = root["meshes"]; meshes && meshes.IsSequence()) {
                payload.scene.meshes.reserve(meshes.size());
                for (const YAML::Node& item : meshes) {
                    Assets::Assimp::ImportedSceneMesh mesh;
                    mesh.name = item["name"].as<std::string>(std::string{});
                    mesh.bounds = readImportedBounds(item["bounds"]);
                    const std::filesystem::path meshPath = rootPath / item["payload"].as<std::string>(std::string{});
                    if (!readMeshPayload(meshPath, mesh, result.message)) {
                        result.status = AuthoredSceneCacheStatus::Corrupt;
                        result.path = meshPath;
                        return result;
                    }
                    payload.scene.meshes.push_back(std::move(mesh));
                }
            }

            payload.partition.bounds = readAuthoredBounds(root["partition"]["bounds"]);
            payload.partition.usedFallbackRootSector = root["partition"]["used_fallback_root_sector"].as<bool>(false);
            if (const YAML::Node warnings = root["partition"]["warnings"]; warnings && warnings.IsSequence()) {
                for (const YAML::Node& warning : warnings) {
                    payload.partition.warnings.push_back(warning.as<std::string>(std::string{}));
                }
            }
            if (const YAML::Node sectors = root["partition"]["sectors"]; sectors && sectors.IsSequence()) {
                payload.partition.sectors.reserve(sectors.size());
                for (const YAML::Node& item : sectors) {
                    payload.partition.sectors.push_back(readSector(item));
                }
            }

            result.status = AuthoredSceneCacheStatus::Hit;
            result.message = "Loaded authored scene cache.";
            result.payload = std::move(payload);
            return result;
        } catch (const YAML::BadFile&) {
            result.status = AuthoredSceneCacheStatus::Miss;
            result.message = "Authored scene cache file is missing.";
        } catch (const std::exception& exception) {
            result.status = AuthoredSceneCacheStatus::Corrupt;
            result.message = exception.what();
        }
        return result;
    }

    AuthoredSceneCacheWriteResult AuthoredSceneCache::write(
        const AuthoredSceneCacheManifest& manifest,
        const AuthoredSceneCachePayload& payload)
    {
        AuthoredSceneCacheWriteResult result;
        const std::filesystem::path rootPath = cacheRoot(manifest);
        result.path = rootPath / "manifest.yaml";

        if (Assets::Assimp::containsSkeletalOrAnimationData(payload.scene)) {
            result.status = AuthoredSceneCacheStatus::WriteFailed;
            result.message = animatedModelRuntimeRequiredMessage();
            return result;
        }

        try {
            std::filesystem::create_directories(rootPath);

            YAML::Node manifestRoot;
            manifestRoot["format_version"] = manifest.settings.formatVersion;
            manifestRoot["identity_hash"] = manifest.identityHash;
            manifestRoot["source_path"] = manifest.sourcePath.generic_string();
            manifestRoot["source_hash"] = manifest.sourceHash;
            manifestRoot["importer_version"] = manifest.settings.importerVersion;
            manifestRoot["material_pipeline_version"] = manifest.settings.materialPipelineVersion;
            manifestRoot["texture_policy_version"] = manifest.settings.texturePolicyVersion;
            manifestRoot["vertex_format_version"] = manifest.settings.vertexFormatVersion;
            manifestRoot["partition_version"] = manifest.settings.partitionVersion;
            manifestRoot["partition_sector_size"] = manifest.partition.sectorSize;
            manifestRoot["partition_enabled"] = manifest.partition.enabled;
            manifestRoot["node_count"] = static_cast<uint32_t>(payload.scene.nodes.size());
            manifestRoot["mesh_count"] = static_cast<uint32_t>(payload.scene.meshes.size());
            manifestRoot["material_count"] = static_cast<uint32_t>(payload.scene.materials.size());
            manifestRoot["texture_count"] = static_cast<uint32_t>(payload.scene.textures.size());
            manifestRoot["light_count"] = static_cast<uint32_t>(payload.scene.lights.size());
            manifestRoot["sector_count"] = static_cast<uint32_t>(payload.partition.sectors.size());
            manifestRoot["scene_payload"] = "scene.yaml";

            YAML::Node root;
            root["source_format"] = Assets::Assimp::sourceFormatName(payload.scene.sourceFormat);
            root["root_node_index"] = payload.scene.rootNodeIndex;
            root["bounds"] = boundsNode(payload.scene.bounds);
            root["diagnostics"] = diagnosticsNode(payload.scene.diagnostics);

            YAML::Node nodes;
            for (const Assets::Assimp::ImportedSceneNode& node : payload.scene.nodes) {
                YAML::Node item;
                item["name"] = node.name;
                item["parent"] = node.parentIndex;
                item["children"] = uintVectorNode(node.childIndices);
                item["meshes"] = uintVectorNode(node.meshIndices);
                item["local_transform"] = mat4Node(node.localTransform);
                item["world_transform"] = mat4Node(node.worldTransform);
                nodes.push_back(item);
            }
            root["nodes"] = nodes;

            YAML::Node meshes;
            for (uint32_t meshIndex = 0; meshIndex < payload.scene.meshes.size(); ++meshIndex) {
                const std::filesystem::path payloadName = meshPayloadName(meshIndex);
                std::string message;
                if (!writeMeshPayload(rootPath / payloadName, payload.scene.meshes[meshIndex], message)) {
                    result.status = AuthoredSceneCacheStatus::WriteFailed;
                    result.message = message;
                    return result;
                }
                YAML::Node item;
                item["name"] = payload.scene.meshes[meshIndex].name;
                item["bounds"] = boundsNode(payload.scene.meshes[meshIndex].bounds);
                item["payload"] = payloadName.generic_string();
                meshes.push_back(item);
            }
            root["meshes"] = meshes;

            YAML::Node materials;
            for (const Assets::Assimp::ImportedSceneMaterial& material : payload.scene.materials) {
                materials.push_back(materialNode(material));
            }
            root["materials"] = materials;

            YAML::Node textures;
            for (const Assets::Assimp::ImportedSceneTexture& texture : payload.scene.textures) {
                textures.push_back(textureNode(texture));
            }
            root["textures"] = textures;

            YAML::Node lights;
            for (const Assets::Assimp::ImportedSceneLight& light : payload.scene.lights) {
                lights.push_back(lightNode(light));
            }
            root["lights"] = lights;

            root["partition"]["bounds"] = boundsNode(payload.partition.bounds);
            root["partition"]["used_fallback_root_sector"] = payload.partition.usedFallbackRootSector;
            YAML::Node partitionWarnings;
            for (const std::string& warning : payload.partition.warnings) {
                partitionWarnings.push_back(warning);
            }
            root["partition"]["warnings"] = partitionWarnings;
            YAML::Node sectors;
            for (const AuthoredSceneSectorManifest& sector : payload.partition.sectors) {
                sectors.push_back(sectorNode(sector));
            }
            root["partition"]["sectors"] = sectors;

            {
                std::ofstream file(rootPath / "scene.yaml");
                if (!file) {
                    result.status = AuthoredSceneCacheStatus::WriteFailed;
                    result.message = "Failed to open authored scene cache scene payload for writing.";
                    return result;
                }
                file << root;
            }
            {
                std::ofstream file(result.path);
                if (!file) {
                    result.status = AuthoredSceneCacheStatus::WriteFailed;
                    result.message = "Failed to open authored scene cache manifest for writing.";
                    return result;
                }
                file << manifestRoot;
            }

            result.status = AuthoredSceneCacheStatus::WriteSuccess;
            result.message = "Wrote authored scene cache.";
        } catch (const std::exception& exception) {
            result.status = AuthoredSceneCacheStatus::WriteFailed;
            result.message = exception.what();
        }
        return result;
    }

    AuthoredScene::~AuthoredScene()
    {
        shutdown();
    }

    AuthoredScene::AuthoredScene(AuthoredScene&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    AuthoredScene& AuthoredScene::operator=(AuthoredScene&& other) noexcept
    {
        if (this != &other) {
            shutdown();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void AuthoredScene::moveFrom(AuthoredScene&& other) noexcept
    {
        assetCache_ = other.assetCache_;
        loaded_ = other.loaded_;
        bounds_ = other.bounds_;
        diagnostics_ = std::move(other.diagnostics_);
        meshes_ = std::move(other.meshes_);
        materials_ = std::move(other.materials_);
        lights_ = std::move(other.lights_);
        instances_ = std::move(other.instances_);
        textures_ = std::move(other.textures_);

        other.assetCache_ = nullptr;
        other.loaded_ = false;
        other.bounds_ = {};
    }

    void AuthoredScene::shutdown()
    {
        for (AuthoredSceneInstance& instance : instances_) {
            Renderer::destroyInstance(instance.handle);
        }
        instances_.clear();

        for (Renderer::LightHandle light : lights_) {
            Renderer::destroyLight(light);
        }
        lights_.clear();

        for (Renderer::StaticMeshHandle mesh : meshes_) {
            Renderer::destroyStaticMesh(mesh);
        }
        meshes_.clear();

        for (Renderer::MaterialHandle material : materials_) {
            Renderer::destroyMaterial(material);
        }
        materials_.clear();

        if (assetCache_) {
            for (CachedTexture texture : textures_) {
                assetCache_->release(texture);
            }
        }
        textures_.clear();

        loaded_ = false;
        assetCache_ = nullptr;
    }

    bool AuthoredScene::loaded() const
    {
        return loaded_;
    }

    const AuthoredSceneBounds& AuthoredScene::bounds() const
    {
        return bounds_;
    }

    const AuthoredSceneDiagnostics& AuthoredScene::diagnostics() const
    {
        return diagnostics_;
    }

    const std::vector<AuthoredSceneInstance>& AuthoredScene::instances() const
    {
        return instances_;
    }

    PartitionedAuthoredScene::~PartitionedAuthoredScene()
    {
        shutdown();
    }

    PartitionedAuthoredScene::PartitionedAuthoredScene(PartitionedAuthoredScene&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    PartitionedAuthoredScene& PartitionedAuthoredScene::operator=(PartitionedAuthoredScene&& other) noexcept
    {
        if (this != &other) {
            shutdown();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void PartitionedAuthoredScene::moveFrom(PartitionedAuthoredScene&& other) noexcept
    {
        assetCache_ = other.assetCache_;
        settings_ = other.settings_;
        loaded_ = other.loaded_;
        sourcePath_ = std::move(other.sourcePath_);
        bounds_ = other.bounds_;
        diagnostics_ = std::move(other.diagnostics_);
        partition_ = std::move(other.partition_);
        sectorRuntime_ = std::move(other.sectorRuntime_);
        materials_ = std::move(other.materials_);
        queuedLoadSectors_ = std::move(other.queuedLoadSectors_);
        queuedUnloadSectors_ = std::move(other.queuedUnloadSectors_);
        imported_ = std::move(other.imported_);

        other.assetCache_ = nullptr;
        other.loaded_ = false;
        other.bounds_ = {};
    }

    bool PartitionedAuthoredScene::loaded() const
    {
        return loaded_;
    }

    const AuthoredSceneBounds& PartitionedAuthoredScene::bounds() const
    {
        return bounds_;
    }

    const AuthoredSceneDiagnostics& PartitionedAuthoredScene::diagnostics() const
    {
        return diagnostics_;
    }

    const AuthoredScenePartition& PartitionedAuthoredScene::partition() const
    {
        return partition_;
    }

    void PartitionedAuthoredScene::setAsyncDiagnostics(
        std::string phase,
        uint32_t queued,
        uint32_t completed,
        uint32_t failed,
        uint32_t pending,
        float cacheReadMs,
        float importMs,
        float cacheWriteMs,
        std::string message)
    {
        diagnostics_.asyncPhase = std::move(phase);
        diagnostics_.asyncJobsQueued = queued;
        diagnostics_.asyncJobsCompleted = completed;
        diagnostics_.asyncJobsFailed = failed;
        diagnostics_.asyncPendingJobs = pending;
        diagnostics_.asyncCacheReadMs = cacheReadMs;
        diagnostics_.asyncImportMs = importMs;
        diagnostics_.asyncCacheWriteMs = cacheWriteMs;
        diagnostics_.asyncMessage = std::move(message);
    }

    void PartitionedAuthoredScene::setStreamingWarning(std::string warning)
    {
        diagnostics_.lastStreamingWarning = std::move(warning);
        if (!diagnostics_.lastStreamingWarning.empty()) {
            diagnostics_.warnings.push_back(diagnostics_.lastStreamingWarning);
        }
    }

    void PartitionedAuthoredScene::refreshDiagnostics()
    {
        diagnostics_.totalSectorCount = static_cast<uint32_t>(partition_.sectors.size());
        diagnostics_.loadedSectorCount = 0;
        diagnostics_.pendingLoadSectorCount = 0;
        diagnostics_.pendingUnloadSectorCount = 0;
        diagnostics_.failedSectorCount = 0;
        diagnostics_.sharedMaterialReferenceCount = 0;
        diagnostics_.sharedTextureReferenceCount = 0;

        for (const SectorRuntime& sector : sectorRuntime_) {
            switch (sector.state) {
                case SectorState::Loaded:
                    ++diagnostics_.loadedSectorCount;
                    break;
                case SectorState::PendingLoad:
                    ++diagnostics_.pendingLoadSectorCount;
                    break;
                case SectorState::PendingUnload:
                    ++diagnostics_.pendingUnloadSectorCount;
                    break;
                case SectorState::Failed:
                    ++diagnostics_.failedSectorCount;
                    break;
                case SectorState::Unloaded:
                default:
                    break;
            }
        }

        for (const MaterialRecord& material : materials_) {
            if (material.refCount > 1) {
                diagnostics_.sharedMaterialReferenceCount += material.refCount - 1;
            }
            for (CachedTexture texture : material.textures) {
                (void)texture;
                if (material.refCount > 1) {
                    diagnostics_.sharedTextureReferenceCount += material.refCount - 1;
                }
            }
        }
    }

    void PartitionedAuthoredScene::enqueueSectorLoad(AuthoredSceneSectorId sector, MainThreadWorkQueue& mainThreadWork)
    {
        if (sector.value >= sectorRuntime_.size() || queuedLoadSectors_.contains(sector.value)) {
            return;
        }
        SectorRuntime& runtime = sectorRuntime_[sector.value];
        if (runtime.state != SectorState::Unloaded) {
            return;
        }

        runtime.state = SectorState::PendingLoad;
        queuedLoadSectors_.insert(sector.value);
        mainThreadWork.enqueue({
            BudgetCategory::StreamingCommit,
            BudgetPriority::Normal,
            "authored sector load",
            [this, sector]() {
                commitSectorLoad(sector);
            },
        });
        refreshDiagnostics();
    }

    void PartitionedAuthoredScene::enqueueSectorUnload(AuthoredSceneSectorId sector, MainThreadWorkQueue& mainThreadWork)
    {
        if (sector.value >= sectorRuntime_.size() || queuedUnloadSectors_.contains(sector.value)) {
            return;
        }
        SectorRuntime& runtime = sectorRuntime_[sector.value];
        if (runtime.state != SectorState::Loaded) {
            return;
        }

        runtime.state = SectorState::PendingUnload;
        queuedUnloadSectors_.insert(sector.value);
        mainThreadWork.enqueue({
            BudgetCategory::StreamingCommit,
            BudgetPriority::Normal,
            "authored sector unload",
            [this, sector]() {
                commitSectorUnload(sector);
            },
        });
        refreshDiagnostics();
    }

    void PartitionedAuthoredScene::commitSectorLoad(AuthoredSceneSectorId sector)
    {
        queuedLoadSectors_.erase(sector.value);
        if (!loaded_ || !imported_ || sector.value >= partition_.sectors.size() || sector.value >= sectorRuntime_.size()) {
            return;
        }

        SectorRuntime& runtime = sectorRuntime_[sector.value];
        if (runtime.state != SectorState::PendingLoad && runtime.state != SectorState::Unloaded) {
            return;
        }
        runtime.state = SectorState::PendingLoad;

        const Assets::Assimp::ImportedScene& imported = imported_->scene;
        const AuthoredSceneSectorManifest& manifest = partition_.sectors[sector.value];

        Renderer::RenderGroupDescriptor groupDescriptor;
        groupDescriptor.name = manifest.name;
        runtime.renderGroup = Renderer::createRenderGroup(groupDescriptor);

        auto failSector = [&](std::string message) {
            setStreamingWarning(std::move(message));
            commitSectorUnload(sector);
            runtime.state = SectorState::Failed;
            refreshDiagnostics();
        };

        auto acquireTexture = [&](const std::filesystem::path& texturePath,
                                  const char* slotName,
                                  const Renderer::TextureDescriptor& descriptor,
                                  MaterialRecord& materialRecord) -> Renderer::TextureHandle {
            if (!settings_.load.loadTextures) {
                if (!texturePath.empty()) {
                    ++diagnostics_.fallbackTextureCount;
                }
                return {};
            }

            const std::filesystem::path resolvedPath = resolveSceneTexturePath(sourcePath_, texturePath);
            if (!textureFileExists(resolvedPath)) {
                if (!texturePath.empty()) {
                    ++diagnostics_.textureLoadFailureCount;
                    ++diagnostics_.fallbackTextureCount;
                    setStreamingWarning(std::string{"Missing authored scene texture for "} + slotName + ": " + texturePath.generic_string());
                }
                return {};
            }

            CachedTexture texture = assetCache_->acquireTexture(resolvedPath, descriptor);
            if (!Renderer::isValid(texture.handle)) {
                ++diagnostics_.textureLoadFailureCount;
                ++diagnostics_.fallbackTextureCount;
                setStreamingWarning(std::string{"Failed to load authored scene texture for "} + slotName + ": " + texturePath.generic_string());
                return {};
            }

            ++diagnostics_.textureLoadSuccessCount;
            const Renderer::TextureInfo info = Renderer::textureInfo(texture.handle);
            diagnostics_.textureEstimatedBytes += info.estimatedBytes;
            diagnostics_.sectorEstimatedBytes += info.estimatedBytes;
            if (info.srgbFallback) {
                ++diagnostics_.textureSrgbFallbackCount;
            }
            materialRecord.textures.push_back(texture);
            return texture.handle;
        };

        auto acquireMaterial = [&](uint32_t materialIndex) -> Renderer::MaterialHandle {
            if (materialIndex >= imported.materials.size()) {
                ++diagnostics_.invalidMaterialReferenceCount;
                return {};
            }
            if (materialIndex >= materials_.size()) {
                materials_.resize(imported.materials.size());
            }

            MaterialRecord& record = materials_[materialIndex];
            if (record.refCount == 0) {
                const Assets::Assimp::ImportedSceneMaterial& material = imported.materials[materialIndex];
                std::vector<Renderer::TextureHandle> baseColorTextures(imported.materials.size());
                std::vector<Renderer::TextureHandle> normalTextures(imported.materials.size());
                std::vector<Renderer::TextureHandle> metallicTextures(imported.materials.size());
                std::vector<Renderer::TextureHandle> roughnessTextures(imported.materials.size());
                std::vector<Renderer::TextureHandle> metallicRoughnessTextures(imported.materials.size());
                std::vector<Renderer::TextureHandle> occlusionTextures(imported.materials.size());
                std::vector<Renderer::TextureHandle> emissiveTextures(imported.materials.size());

                baseColorTextures[materialIndex] = acquireTexture(
                    material.baseColorTexture,
                    "baseColor",
                    makeTextureDescriptor(Renderer::TextureSlot::BaseColor, Renderer::TextureColorSpace::Srgb, material.baseColorTextureHints, "baseColor"),
                    record);
                normalTextures[materialIndex] = acquireTexture(
                    material.normalTexture,
                    "normal",
                    makeTextureDescriptor(Renderer::TextureSlot::Normal, Renderer::TextureColorSpace::Linear, material.normalTextureHints, "normal"),
                    record);
                if (!material.hasPackedMetallicRoughnessTexture) {
                    metallicTextures[materialIndex] = acquireTexture(
                        material.metallicTexture,
                        "metallic",
                        makeTextureDescriptor(Renderer::TextureSlot::Metallic, Renderer::TextureColorSpace::Linear, material.metallicTextureHints, "metallic"),
                        record);
                    roughnessTextures[materialIndex] = acquireTexture(
                        material.roughnessTexture,
                        "roughness",
                        makeTextureDescriptor(Renderer::TextureSlot::Roughness, Renderer::TextureColorSpace::Linear, material.roughnessTextureHints, "roughness"),
                        record);
                }
                metallicRoughnessTextures[materialIndex] = acquireTexture(
                    material.metallicRoughnessTexture,
                    "metallicRoughness",
                    makeTextureDescriptor(Renderer::TextureSlot::MetallicRoughness, Renderer::TextureColorSpace::Linear, material.metallicRoughnessTextureHints, "metallicRoughness"),
                    record);
                occlusionTextures[materialIndex] = acquireTexture(
                    material.occlusionTexture,
                    "occlusion",
                    makeTextureDescriptor(Renderer::TextureSlot::Occlusion, Renderer::TextureColorSpace::Linear, material.occlusionTextureHints, "occlusion"),
                    record);
                emissiveTextures[materialIndex] = acquireTexture(
                    material.emissiveTexture,
                    "emissive",
                    makeTextureDescriptor(Renderer::TextureSlot::Emissive, Renderer::TextureColorSpace::Srgb, material.emissiveTextureHints, "emissive"),
                    record);

                record.handle = Renderer::createMaterial(makeMaterialDescriptor(
                    material,
                    baseColorTextures,
                    normalTextures,
                    metallicTextures,
                    roughnessTextures,
                    metallicRoughnessTextures,
                    occlusionTextures,
                    emissiveTextures,
                    materialIndex));
                if (record.handle.id == UINT32_MAX) {
                    ++diagnostics_.fallbackMaterialCount;
                    if (assetCache_) {
                        for (CachedTexture texture : record.textures) {
                            assetCache_->release(texture);
                        }
                    }
                    record.textures.clear();
                    return {};
                }
                ++diagnostics_.createdMaterialCount;
            }

            ++record.refCount;
            appendUnique(runtime.materialRefs, materialIndex);
            return record.handle;
        };

        std::vector<Renderer::MaterialHandle> materialHandles(imported.materials.size());
        for (uint32_t materialIndex : manifest.materialIndices) {
            materialHandles[materialIndex] = acquireMaterial(materialIndex);
            if (materialHandles[materialIndex].id == UINT32_MAX) {
                failSector("Failed to create renderer material for authored sector.");
                return;
            }
        }

        for (uint32_t lightIndex : manifest.lightIndices) {
            if (lightIndex >= imported.lights.size()) {
                continue;
            }
            Renderer::LightType type;
            if (!convertLightType(imported.lights[lightIndex].type, type)) {
                ++diagnostics_.skippedUnsupportedLightCount;
                continue;
            }
            Renderer::LightDescriptor descriptor = makeLightDescriptor(imported, imported.lights[lightIndex], diagnostics_);
            descriptor.type = type;
            if (!descriptor.enabled) {
                ++diagnostics_.disabledZeroIntensityLightCount;
            } else {
                ++diagnostics_.activeAuthoredLightCount;
            }
            Renderer::LightHandle light = Renderer::createLight(descriptor);
            if (light.id == UINT32_MAX) {
                failSector("Failed to create renderer light for authored sector.");
                return;
            }
            runtime.lights.push_back(light);
            ++diagnostics_.createdLightCount;
        }

        for (uint32_t nodeIndex : manifest.nodeIndices) {
            if (nodeIndex >= imported.nodes.size()) {
                continue;
            }
            const Assets::Assimp::ImportedSceneNode& node = imported.nodes[nodeIndex];
            for (uint32_t meshIndex : node.meshIndices) {
                if (meshIndex >= imported.meshes.size()) {
                    ++diagnostics_.invalidMeshReferenceCount;
                    continue;
                }
                for (const Assets::Assimp::ImportedScenePrimitive& primitive : imported.meshes[meshIndex].primitives) {
                    if (primitive.materialIndex < materialHandles.size() && materialHandles[primitive.materialIndex].id == UINT32_MAX) {
                        materialHandles[primitive.materialIndex] = acquireMaterial(primitive.materialIndex);
                    }
                }
                Renderer::StaticMeshDescriptor descriptor = makeMeshDescriptor(imported.meshes[meshIndex], materialHandles, diagnostics_);
                Renderer::StaticMeshHandle mesh = Renderer::createStaticMesh(descriptor);
                if (mesh.id == UINT32_MAX) {
                    failSector("Failed to create renderer static mesh for authored sector.");
                    return;
                }
                runtime.meshes.push_back(mesh);
                ++diagnostics_.createdMeshCount;

                Renderer::MeshInstanceHandle instance = Renderer::createInstance(mesh);
                if (instance.id == UINT32_MAX) {
                    failSector("Failed to create renderer mesh instance for authored sector.");
                    return;
                }
                Renderer::setInstanceTransform(instance, node.worldTransform);
                Renderer::setInstanceRenderLayer(instance, settings_.load.renderLayer);
                Renderer::setInstanceMaxDrawDistance(instance, settings_.load.maxDrawDistance);
                Renderer::setInstanceRenderGroup(instance, runtime.renderGroup);
                runtime.instances.push_back(instance);
                ++diagnostics_.createdInstanceCount;
            }
        }

        runtime.state = SectorState::Loaded;
        refreshDiagnostics();
    }

    void PartitionedAuthoredScene::commitSectorUnload(AuthoredSceneSectorId sector)
    {
        queuedUnloadSectors_.erase(sector.value);
        if (sector.value >= sectorRuntime_.size()) {
            return;
        }

        SectorRuntime& runtime = sectorRuntime_[sector.value];
        for (Renderer::MeshInstanceHandle instance : runtime.instances) {
            Renderer::destroyInstance(instance);
        }
        runtime.instances.clear();

        for (Renderer::LightHandle light : runtime.lights) {
            Renderer::destroyLight(light);
        }
        runtime.lights.clear();

        for (Renderer::StaticMeshHandle mesh : runtime.meshes) {
            Renderer::destroyStaticMesh(mesh);
        }
        runtime.meshes.clear();

        if (runtime.renderGroup.id != UINT32_MAX) {
            Renderer::destroyRenderGroup(runtime.renderGroup);
            runtime.renderGroup = {};
        }

        for (uint32_t materialIndex : runtime.materialRefs) {
            if (materialIndex >= materials_.size() || materials_[materialIndex].refCount == 0) {
                continue;
            }
            MaterialRecord& material = materials_[materialIndex];
            --material.refCount;
            if (material.refCount == 0) {
                Renderer::destroyMaterial(material.handle);
                material.handle = {};
                if (assetCache_) {
                    for (CachedTexture texture : material.textures) {
                        assetCache_->release(texture);
                    }
                }
                material.textures.clear();
            }
        }
        runtime.materialRefs.clear();
        runtime.textureRefs.clear();
        if (runtime.state != SectorState::Failed) {
            runtime.state = SectorState::Unloaded;
        }
        refreshDiagnostics();
    }

    void PartitionedAuthoredScene::updateStreaming(const glm::vec3& cameraPosition, MainThreadWorkQueue& mainThreadWork)
    {
        if (!loaded_) {
            return;
        }

        std::vector<std::pair<float, uint32_t>> candidates;
        candidates.reserve(partition_.sectors.size());
        for (const AuthoredSceneSectorManifest& sector : partition_.sectors) {
            const float distance = sector.bounds.valid ? boundsDistance(sector.bounds, cameraPosition) : 0.0f;
            candidates.push_back({distance, sector.id.value});
        }
        std::sort(candidates.begin(), candidates.end());

        std::unordered_set<uint32_t> desired;
        for (const auto& [distance, sectorIndex] : candidates) {
            if (distance > settings_.loadRadius) {
                continue;
            }
            if (settings_.maxLoadedSectorCount > 0 && desired.size() >= settings_.maxLoadedSectorCount) {
                break;
            }
            desired.insert(sectorIndex);
        }

        uint32_t unloadsQueued = 0;
        for (const auto& [distance, sectorIndex] : candidates) {
            if (sectorIndex >= sectorRuntime_.size()) {
                continue;
            }
            if (sectorRuntime_[sectorIndex].state == SectorState::Loaded &&
                !desired.contains(sectorIndex) &&
                distance > settings_.unloadRadius &&
                unloadsQueued < settings_.maxSectorUnloadCommitsPerFrame) {
                enqueueSectorUnload({sectorIndex}, mainThreadWork);
                ++unloadsQueued;
            }
        }

        uint32_t loadsQueued = 0;
        for (const auto& [distance, sectorIndex] : candidates) {
            (void)distance;
            if (!desired.contains(sectorIndex) || sectorIndex >= sectorRuntime_.size()) {
                continue;
            }
            if (sectorRuntime_[sectorIndex].state == SectorState::Unloaded &&
                loadsQueued < settings_.maxSectorLoadCommitsPerFrame) {
                enqueueSectorLoad({sectorIndex}, mainThreadWork);
                ++loadsQueued;
            }
        }

        refreshDiagnostics();
    }

    void PartitionedAuthoredScene::shutdown()
    {
        for (uint32_t sectorIndex = 0; sectorIndex < sectorRuntime_.size(); ++sectorIndex) {
            commitSectorUnload({sectorIndex});
        }
        sectorRuntime_.clear();

        for (MaterialRecord& material : materials_) {
            if (material.refCount > 0) {
                Renderer::destroyMaterial(material.handle);
                if (assetCache_) {
                    for (CachedTexture texture : material.textures) {
                        assetCache_->release(texture);
                    }
                }
            }
            material = {};
        }
        materials_.clear();
        queuedLoadSectors_.clear();
        queuedUnloadSectors_.clear();
        imported_.reset();
        loaded_ = false;
        assetCache_ = nullptr;
        refreshDiagnostics();
    }

    AuthoredSceneLoadResult loadAuthoredScene(
        const std::filesystem::path& path,
        AssetCache& assetCache,
        const AuthoredSceneLoadSettings& settings)
    {
        AuthoredSceneLoadResult result;
        result.scene.assetCache_ = &assetCache;

        const Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(path);
        if (!imported.success) {
            result.message = imported.error;
            return result;
        }

        AuthoredSceneDiagnostics& diagnostics = result.scene.diagnostics_;
        diagnostics.importedNodeCount = imported.diagnostics.nodeCount;
        diagnostics.importedMeshCount = imported.diagnostics.meshCount;
        diagnostics.importedPrimitiveCount = imported.diagnostics.primitiveCount;
        diagnostics.importedMaterialCount = imported.diagnostics.materialCount;
        diagnostics.importedTextureCount = imported.diagnostics.textureCount;
        diagnostics.importedLightCount = imported.diagnostics.lightCount;
        diagnostics.importedSkinCount = imported.diagnostics.skinCount;
        diagnostics.importedJointCount = imported.diagnostics.jointCount;
        diagnostics.importedAnimationCount = imported.diagnostics.animationCount;
        diagnostics.importedAnimationChannelCount = imported.diagnostics.animationChannelCount;
        diagnostics.boundsValid = imported.bounds.valid;
        diagnostics.warnings = imported.diagnostics.warnings;
        appendDeferredDiagnostics(imported, diagnostics);

        result.scene.bounds_ = convertBounds(imported.bounds);

        if (Assets::Assimp::containsSkeletalOrAnimationData(imported)) {
            result.message = animatedModelRuntimeRequiredMessage();
            diagnostics.warnings.push_back(result.message);
            return result;
        }

        std::vector<Renderer::TextureHandle> baseColorTextures(imported.materials.size());
        std::vector<Renderer::TextureHandle> normalTextures(imported.materials.size());
        std::vector<Renderer::TextureHandle> metallicTextures(imported.materials.size());
        std::vector<Renderer::TextureHandle> roughnessTextures(imported.materials.size());
        std::vector<Renderer::TextureHandle> metallicRoughnessTextures(imported.materials.size());
        std::vector<Renderer::TextureHandle> occlusionTextures(imported.materials.size());
        std::vector<Renderer::TextureHandle> emissiveTextures(imported.materials.size());
        if (settings.loadTextures) {
            for (uint32_t materialIndex = 0; materialIndex < imported.materials.size(); ++materialIndex) {
                const Assets::Assimp::ImportedSceneMaterial& material = imported.materials[materialIndex];
                const auto acquireTexture = [&](
                    const std::filesystem::path& texturePath,
                    const char* slotName,
                    const Renderer::TextureDescriptor& descriptor) -> Renderer::TextureHandle {
                    const std::filesystem::path resolvedPath = resolveSceneTexturePath(path, texturePath);
                    if (!textureFileExists(resolvedPath)) {
                        if (!texturePath.empty()) {
                            ++diagnostics.textureLoadFailureCount;
                            ++diagnostics.fallbackTextureCount;
                            diagnostics.warnings.push_back(
                                std::string{"Missing authored scene texture for "} + slotName + ": " + texturePath.generic_string());
                        }
                        return {};
                    }

                    CachedTexture texture = assetCache.acquireTexture(resolvedPath, descriptor);
                    if (!Renderer::isValid(texture.handle)) {
                        ++diagnostics.textureLoadFailureCount;
                        ++diagnostics.fallbackTextureCount;
                        diagnostics.warnings.push_back(
                            std::string{"Failed to load authored scene texture for "} + slotName + ": " + texturePath.generic_string());
                        return {};
                    }

                    ++diagnostics.textureLoadSuccessCount;
                    const Renderer::TextureInfo info = Renderer::textureInfo(texture.handle);
                    diagnostics.textureEstimatedBytes += info.estimatedBytes;
                    if (info.srgbFallback) {
                        ++diagnostics.textureSrgbFallbackCount;
                    }
                    result.scene.textures_.push_back(texture);
                    return texture.handle;
                };

                baseColorTextures[materialIndex] = acquireTexture(
                    material.baseColorTexture,
                    "baseColor",
                    makeTextureDescriptor(Renderer::TextureSlot::BaseColor, Renderer::TextureColorSpace::Srgb, material.baseColorTextureHints, "baseColor"));
                normalTextures[materialIndex] = acquireTexture(
                    material.normalTexture,
                    "normal",
                    makeTextureDescriptor(Renderer::TextureSlot::Normal, Renderer::TextureColorSpace::Linear, material.normalTextureHints, "normal"));
                if (!material.hasPackedMetallicRoughnessTexture) {
                    metallicTextures[materialIndex] = acquireTexture(
                        material.metallicTexture,
                        "metallic",
                        makeTextureDescriptor(Renderer::TextureSlot::Metallic, Renderer::TextureColorSpace::Linear, material.metallicTextureHints, "metallic"));
                    roughnessTextures[materialIndex] = acquireTexture(
                        material.roughnessTexture,
                        "roughness",
                        makeTextureDescriptor(Renderer::TextureSlot::Roughness, Renderer::TextureColorSpace::Linear, material.roughnessTextureHints, "roughness"));
                }
                metallicRoughnessTextures[materialIndex] = acquireTexture(
                    material.metallicRoughnessTexture,
                    "metallicRoughness",
                    makeTextureDescriptor(Renderer::TextureSlot::MetallicRoughness, Renderer::TextureColorSpace::Linear, material.metallicRoughnessTextureHints, "metallicRoughness"));
                occlusionTextures[materialIndex] = acquireTexture(
                    material.occlusionTexture,
                    "occlusion",
                    makeTextureDescriptor(Renderer::TextureSlot::Occlusion, Renderer::TextureColorSpace::Linear, material.occlusionTextureHints, "occlusion"));
                emissiveTextures[materialIndex] = acquireTexture(
                    material.emissiveTexture,
                    "emissive",
                    makeTextureDescriptor(Renderer::TextureSlot::Emissive, Renderer::TextureColorSpace::Srgb, material.emissiveTextureHints, "emissive"));
            }
        } else {
            for (const Assets::Assimp::ImportedSceneMaterial& material : imported.materials) {
                if (!material.baseColorTexture.empty() ||
                    !material.normalTexture.empty() ||
                    !material.metallicTexture.empty() ||
                    !material.roughnessTexture.empty() ||
                    !material.metallicRoughnessTexture.empty() ||
                    !material.occlusionTexture.empty() ||
                    !material.emissiveTexture.empty()) {
                    ++diagnostics.fallbackTextureCount;
                }
            }
        }

        result.scene.materials_.reserve(imported.materials.size());
        for (uint32_t materialIndex = 0; materialIndex < imported.materials.size(); ++materialIndex) {
            const Assets::Assimp::ImportedSceneMaterial& material = imported.materials[materialIndex];
            Renderer::MaterialDescriptor descriptor;
            descriptor.name = material.name.empty()
                ? "authored.material." + std::to_string(materialIndex)
                : material.name;
            descriptor.baseColorFactor = material.baseColorFactor;
            descriptor.baseColorTexture = baseColorTextures[materialIndex];
            descriptor.normalTexture = normalTextures[materialIndex];
            descriptor.normalScale = material.normalScale;
            descriptor.metallicFactor = material.metallicFactor;
            descriptor.roughnessFactor = material.roughnessFactor;
            descriptor.metallicTexture = metallicTextures[materialIndex];
            descriptor.roughnessTexture = roughnessTextures[materialIndex];
            descriptor.metallicRoughnessTexture = metallicRoughnessTextures[materialIndex];
            descriptor.occlusionTexture = occlusionTextures[materialIndex];
            descriptor.occlusionStrength = material.occlusionStrength;
            descriptor.emissiveTexture = emissiveTextures[materialIndex];
            descriptor.emissiveFactor = material.emissiveFactor;
            descriptor.alphaMode = convertAlphaMode(material.alphaMode);
            descriptor.alphaCutoff = material.alphaCutoff;
            descriptor.doubleSided = material.doubleSided;
            descriptor.baseColorTextureHints = convertTextureHints(material.baseColorTextureHints);
            descriptor.normalTextureHints = convertTextureHints(material.normalTextureHints);
            descriptor.metallicTextureHints = convertTextureHints(material.metallicTextureHints);
            descriptor.roughnessTextureHints = convertTextureHints(material.roughnessTextureHints);
            descriptor.metallicRoughnessTextureHints = convertTextureHints(material.metallicRoughnessTextureHints);
            descriptor.occlusionTextureHints = convertTextureHints(material.occlusionTextureHints);
            descriptor.emissiveTextureHints = convertTextureHints(material.emissiveTextureHints);

            Renderer::MaterialHandle handle = Renderer::createMaterial(descriptor);
            if (handle.id == UINT32_MAX) {
                ++diagnostics.fallbackMaterialCount;
                result.message = "Failed to create renderer material for authored scene.";
                result.scene.shutdown();
                return result;
            }

            result.scene.materials_.push_back(handle);
            ++diagnostics.createdMaterialCount;
        }

        uint32_t activeAuthoredLights = 0;
        result.scene.lights_.reserve(imported.lights.size());
        for (const Assets::Assimp::ImportedSceneLight& importedLight : imported.lights) {
            Renderer::LightType type;
            if (!convertLightType(importedLight.type, type)) {
                ++diagnostics.skippedUnsupportedLightCount;
                continue;
            }

            Renderer::LightDescriptor descriptor = makeLightDescriptor(imported, importedLight, diagnostics);
            descriptor.type = type;
            if (!descriptor.enabled) {
                ++diagnostics.disabledZeroIntensityLightCount;
            } else {
                if (activeAuthoredLights >= Renderer::MaxForwardLights) {
                    ++diagnostics.skippedOverBudgetLightCount;
                    continue;
                }
                ++activeAuthoredLights;
                ++diagnostics.activeAuthoredLightCount;
            }

            Renderer::LightHandle handle = Renderer::createLight(descriptor);
            if (handle.id == UINT32_MAX) {
                result.message = "Failed to create renderer light for authored scene.";
                result.scene.shutdown();
                return result;
            }

            result.scene.lights_.push_back(handle);
            ++diagnostics.createdLightCount;
        }

        result.scene.meshes_.reserve(imported.meshes.size());
        for (uint32_t meshIndex = 0; meshIndex < imported.meshes.size(); ++meshIndex) {
            const Assets::Assimp::ImportedSceneMesh& importedMesh = imported.meshes[meshIndex];
            Renderer::StaticMeshDescriptor descriptor;
            descriptor.name = importedMesh.name;
            descriptor.submeshes.reserve(importedMesh.primitives.size());

            for (const Assets::Assimp::ImportedScenePrimitive& primitive : importedMesh.primitives) {
                Renderer::StaticSubmeshDescriptor submesh;
                submesh.vertices.reserve(primitive.vertices.size());
                submesh.indices = primitive.indices;
                if (primitive.materialIndex < result.scene.materials_.size()) {
                    submesh.material = result.scene.materials_[primitive.materialIndex];
                } else {
                    ++diagnostics.invalidMaterialReferenceCount;
                }

                for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                    submesh.vertices.push_back(convertVertex(vertex));
                }
                descriptor.submeshes.push_back(std::move(submesh));
            }

            Renderer::StaticMeshHandle mesh = Renderer::createStaticMesh(descriptor);
            if (mesh.id == UINT32_MAX) {
                result.message = "Failed to create renderer static mesh for authored scene.";
                result.scene.shutdown();
                return result;
            }

            result.scene.meshes_.push_back(mesh);
            ++diagnostics.createdMeshCount;
        }

        for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
            const Assets::Assimp::ImportedSceneNode& node = imported.nodes[nodeIndex];
            for (uint32_t meshIndex : node.meshIndices) {
                if (meshIndex >= result.scene.meshes_.size()) {
                    ++diagnostics.invalidMeshReferenceCount;
                    continue;
                }

                Renderer::MeshInstanceHandle instance = Renderer::createInstance(result.scene.meshes_[meshIndex]);
                if (instance.id == UINT32_MAX) {
                    result.message = "Failed to create renderer mesh instance for authored scene.";
                    result.scene.shutdown();
                    return result;
                }

                Renderer::setInstanceTransform(instance, node.worldTransform);
                Renderer::setInstanceRenderLayer(instance, settings.renderLayer);
                Renderer::setInstanceMaxDrawDistance(instance, settings.maxDrawDistance);
                result.scene.instances_.push_back({nodeIndex, meshIndex, instance, node.worldTransform});
                ++diagnostics.createdInstanceCount;
            }
        }

        result.scene.loaded_ = true;
        result.success = true;
        result.message = "Authored scene loaded.";
        return result;
    }

    AuthoredScenePartition partitionAuthoredScene(
        const std::filesystem::path& path,
        const AuthoredScenePartitionSettings& settings)
    {
        const Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(path);
        if (!imported.success) {
            AuthoredScenePartition partition;
            partition.usedFallbackRootSector = true;
            partition.warnings.push_back(imported.error);
            return partition;
        }
        if (Assets::Assimp::containsSkeletalOrAnimationData(imported)) {
            AuthoredScenePartition partition;
            partition.usedFallbackRootSector = true;
            partition.warnings.push_back(animatedModelRuntimeRequiredMessage());
            return partition;
        }
        return buildPartition(imported, settings);
    }

    AuthoredScenePartition partitionImportedAuthoredScene(
        const Assets::Assimp::ImportedScene& imported,
        const AuthoredScenePartitionSettings& settings)
    {
        if (!imported.success) {
            AuthoredScenePartition partition;
            partition.usedFallbackRootSector = true;
            partition.warnings.push_back(imported.error);
            return partition;
        }
        if (Assets::Assimp::containsSkeletalOrAnimationData(imported)) {
            AuthoredScenePartition partition;
            partition.usedFallbackRootSector = true;
            partition.warnings.push_back(animatedModelRuntimeRequiredMessage());
            return partition;
        }
        return buildPartition(imported, settings);
    }

    PartitionedAuthoredSceneLoadResult createPartitionedAuthoredSceneFromPayload(
        const std::filesystem::path& path,
        AuthoredSceneCachePayload payload,
        AssetCache& assetCache,
        const AuthoredSceneStreamingSettings& settings)
    {
        PartitionedAuthoredSceneLoadResult result;
        result.scene.assetCache_ = &assetCache;
        result.scene.settings_ = settings;
        result.scene.sourcePath_ = path;
        result.scene.imported_ = std::make_shared<PartitionedAuthoredScene::ImportedStorage>();
        result.scene.imported_->scene = std::move(payload.scene);

        if (!result.scene.imported_->scene.success) {
            result.message = result.scene.imported_->scene.error.empty()
                ? "Authored scene payload was not successful."
                : result.scene.imported_->scene.error;
            result.scene.imported_.reset();
            return result;
        }

        const Assets::Assimp::ImportedScene& imported = result.scene.imported_->scene;
        appendImportDiagnostics(imported, result.scene.diagnostics_);
        result.scene.bounds_ = convertBounds(imported.bounds);
        if (Assets::Assimp::containsSkeletalOrAnimationData(imported)) {
            result.message = animatedModelRuntimeRequiredMessage();
            result.scene.diagnostics_.warnings.push_back(result.message);
            result.scene.imported_.reset();
            return result;
        }
        result.scene.partition_ = std::move(payload.partition);
        result.scene.diagnostics_.totalSectorCount = static_cast<uint32_t>(result.scene.partition_.sectors.size());
        result.scene.diagnostics_.sectorEstimatedBytes = 0;
        for (const AuthoredSceneSectorManifest& sector : result.scene.partition_.sectors) {
            result.scene.diagnostics_.sectorEstimatedBytes +=
                static_cast<uint64_t>(sector.vertexCount) * sizeof(Renderer::MeshVertex) +
                static_cast<uint64_t>(sector.indexCount) * sizeof(uint32_t);
        }
        for (const std::string& warning : result.scene.partition_.warnings) {
            result.scene.diagnostics_.warnings.push_back(warning);
            result.scene.diagnostics_.lastStreamingWarning = warning;
        }

        result.scene.sectorRuntime_.resize(result.scene.partition_.sectors.size());
        result.scene.materials_.resize(imported.materials.size());
        result.scene.loaded_ = true;

        if (settings.loadInitialSectorsImmediately) {
            std::vector<std::pair<float, uint32_t>> candidates;
            candidates.reserve(result.scene.partition_.sectors.size());
            for (const AuthoredSceneSectorManifest& sector : result.scene.partition_.sectors) {
                const float distance = sector.bounds.valid ? boundsDistance(sector.bounds, settings.initialCameraPosition) : 0.0f;
                if (distance <= settings.loadRadius) {
                    candidates.push_back({distance, sector.id.value});
                }
            }
            std::sort(candidates.begin(), candidates.end());
            uint32_t loadedCount = 0;
            for (const auto& [distance, sectorIndex] : candidates) {
                (void)distance;
                if (settings.maxLoadedSectorCount > 0 && loadedCount >= settings.maxLoadedSectorCount) {
                    break;
                }
                if (sectorIndex < result.scene.sectorRuntime_.size()) {
                    result.scene.sectorRuntime_[sectorIndex].state = PartitionedAuthoredScene::SectorState::PendingLoad;
                    result.scene.commitSectorLoad({sectorIndex});
                    ++loadedCount;
                }
            }
        }

        result.scene.refreshDiagnostics();
        result.success = true;
        result.message = "Partitioned authored scene loaded.";
        return result;
    }

    PartitionedAuthoredSceneLoadResult loadPartitionedAuthoredScene(
        const std::filesystem::path& path,
        AssetCache& assetCache,
        const AuthoredSceneStreamingSettings& settings)
    {
        const AuthoredSceneCacheManifest cacheManifest =
            AuthoredSceneCache::buildManifest(settings.cache, path, settings.partition);

        bool loadedFromCache = false;
        AuthoredSceneCachePayload payload;
        AuthoredSceneDiagnostics cacheDiagnostics;
        cacheDiagnostics.cachePath = AuthoredSceneCache::cacheRoot(cacheManifest);
        cacheDiagnostics.cacheIdentityHash = cacheManifest.identityHash;
        if (settings.cache.policy != AuthoredSceneCachePolicy::Disabled &&
            settings.cache.policy != AuthoredSceneCachePolicy::Refresh) {
            AuthoredSceneCacheReadResult cacheRead = AuthoredSceneCache::read(cacheManifest);
            cacheDiagnostics.cacheStatus = cacheRead.status;
            cacheDiagnostics.cacheMessage = cacheRead.message;
            ++cacheDiagnostics.cacheReadCount;
            if (cacheRead.status == AuthoredSceneCacheStatus::Hit && cacheRead.payload) {
                payload = std::move(*cacheRead.payload);
                loadedFromCache = true;
                cacheDiagnostics.loadedFromCache = true;
            } else {
                switch (cacheRead.status) {
                    case AuthoredSceneCacheStatus::Stale:
                        ++cacheDiagnostics.cacheStaleCount;
                        break;
                    case AuthoredSceneCacheStatus::Corrupt:
                        ++cacheDiagnostics.cacheCorruptCount;
                        break;
                    case AuthoredSceneCacheStatus::Miss:
                    default:
                        ++cacheDiagnostics.cacheMissCount;
                        break;
                }
            }
        } else if (settings.cache.policy == AuthoredSceneCachePolicy::Refresh) {
            cacheDiagnostics.cacheStatus = AuthoredSceneCacheStatus::Stale;
            cacheDiagnostics.cacheMessage = "Authored scene cache refresh requested.";
        }

        if (!loadedFromCache) {
            payload.scene = Assets::Assimp::importScene(path);
            if (!payload.scene.success) {
                PartitionedAuthoredSceneLoadResult failed;
                failed.message = payload.scene.error;
                return failed;
            }
            if (Assets::Assimp::containsSkeletalOrAnimationData(payload.scene)) {
                PartitionedAuthoredSceneLoadResult failed;
                failed.message = animatedModelRuntimeRequiredMessage();
                appendImportDiagnostics(payload.scene, failed.scene.diagnostics_);
                failed.scene.diagnostics_.warnings.push_back(failed.message);
                return failed;
            }
            payload.partition = buildPartition(payload.scene, settings.partition);
        }

        const bool shouldWriteCache =
            settings.cache.policy == AuthoredSceneCachePolicy::Refresh ||
            (settings.cache.policy == AuthoredSceneCachePolicy::GenerateOnMiss && !loadedFromCache);
        if (shouldWriteCache) {
            AuthoredSceneCacheWriteResult cacheWrite = AuthoredSceneCache::write(cacheManifest, payload);
            cacheDiagnostics.cacheStatus = cacheWrite.status;
            cacheDiagnostics.cacheMessage = cacheWrite.message;
            cacheDiagnostics.cachePath = cacheWrite.path.empty() ? cacheDiagnostics.cachePath : cacheWrite.path;
            if (cacheWrite.status == AuthoredSceneCacheStatus::WriteSuccess) {
                ++cacheDiagnostics.cacheWriteCount;
            } else {
                cacheDiagnostics.warnings.push_back(cacheWrite.message);
            }
        } else if (loadedFromCache) {
            cacheDiagnostics.cacheStatus = AuthoredSceneCacheStatus::Hit;
        }

        PartitionedAuthoredSceneLoadResult result =
            createPartitionedAuthoredSceneFromPayload(path, std::move(payload), assetCache, settings);
        result.scene.diagnostics_.cacheStatus = cacheDiagnostics.cacheStatus;
        result.scene.diagnostics_.cachePath = cacheDiagnostics.cachePath;
        result.scene.diagnostics_.cacheIdentityHash = cacheDiagnostics.cacheIdentityHash;
        result.scene.diagnostics_.cacheReadCount = cacheDiagnostics.cacheReadCount;
        result.scene.diagnostics_.cacheWriteCount = cacheDiagnostics.cacheWriteCount;
        result.scene.diagnostics_.cacheMissCount = cacheDiagnostics.cacheMissCount;
        result.scene.diagnostics_.cacheStaleCount = cacheDiagnostics.cacheStaleCount;
        result.scene.diagnostics_.cacheCorruptCount = cacheDiagnostics.cacheCorruptCount;
        result.scene.diagnostics_.loadedFromCache = cacheDiagnostics.loadedFromCache;
        result.scene.diagnostics_.cacheMessage = cacheDiagnostics.cacheMessage;
        for (const std::string& warning : cacheDiagnostics.warnings) {
            result.scene.diagnostics_.warnings.push_back(warning);
        }
        return result;
    }
}
