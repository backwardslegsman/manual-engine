#include "Assets/Assimp/Importer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <assimp/light.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {
    constexpr uint32_t InvalidIndex = UINT32_MAX;

    std::string lowercaseExtension(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return extension;
    }

    bool isGltfLike(Assets::Assimp::ImportedSceneSourceFormat format)
    {
        return format == Assets::Assimp::ImportedSceneSourceFormat::Gltf ||
            format == Assets::Assimp::ImportedSceneSourceFormat::Glb;
    }

    std::filesystem::path readTexturePath(const aiMaterial& material, aiTextureType type)
    {
        aiString texturePath;
        if (material.GetTexture(type, 0, &texturePath) != AI_SUCCESS || texturePath.length == 0) {
            return {};
        }

        return std::filesystem::path(texturePath.C_Str());
    }

    std::filesystem::path readTexturePath(const aiMaterial& material, aiTextureType type, unsigned int index)
    {
        aiString texturePath;
        if (material.GetTexture(type, index, &texturePath) != AI_SUCCESS || texturePath.length == 0) {
            return {};
        }

        return std::filesystem::path(texturePath.C_Str());
    }

    float readFloatProperty(const aiMaterial& material, const char* key, unsigned int type, unsigned int index, float fallback)
    {
        float value = fallback;
        if (material.Get(key, type, index, value) == AI_SUCCESS) {
            return value;
        }

        return fallback;
    }

    bool readBoolProperty(const aiMaterial& material, const char* key, unsigned int type, unsigned int index, bool fallback)
    {
        int value = fallback ? 1 : 0;
        if (material.Get(key, type, index, value) == AI_SUCCESS) {
            return value != 0;
        }

        return fallback;
    }

    Assets::Assimp::ImportedSceneTextureWrap toImportedWrap(aiTextureMapMode mode)
    {
        switch (mode) {
            case aiTextureMapMode_Clamp:
            case aiTextureMapMode_Decal:
                return Assets::Assimp::ImportedSceneTextureWrap::ClampToEdge;
            case aiTextureMapMode_Mirror:
                return Assets::Assimp::ImportedSceneTextureWrap::MirroredRepeat;
            case aiTextureMapMode_Wrap:
            default:
                break;
        }
        return Assets::Assimp::ImportedSceneTextureWrap::Repeat;
    }

    Assets::Assimp::ImportedSceneTextureHints readTextureHints(
        const aiMaterial& material,
        aiTextureType type,
        Assets::Assimp::ImportedSceneTextureColorSpace colorSpace)
    {
        Assets::Assimp::ImportedSceneTextureHints hints;
        hints.colorSpace = colorSpace;

        aiTextureMapMode mapMode = aiTextureMapMode_Wrap;
        if (material.Get(AI_MATKEY_MAPPINGMODE_U(type, 0), mapMode) == AI_SUCCESS) {
            hints.wrapU = toImportedWrap(mapMode);
        }
        mapMode = aiTextureMapMode_Wrap;
        if (material.Get(AI_MATKEY_MAPPINGMODE_V(type, 0), mapMode) == AI_SUCCESS) {
            hints.wrapV = toImportedWrap(mapMode);
        }

        return hints;
    }

    std::string readStringProperty(const aiMaterial& material, const char* key, unsigned int type, unsigned int index)
    {
        aiString value;
        if (material.Get(key, type, index, value) == AI_SUCCESS) {
            return value.C_Str();
        }
        return {};
    }

    glm::vec3 toGlm(const aiVector3D& value)
    {
        return {value.x, value.y, value.z};
    }

    glm::vec4 toGlm(const aiColor4D& value)
    {
        return {value.r, value.g, value.b, value.a};
    }

    glm::mat4 toGlm(const aiMatrix4x4& value)
    {
        glm::mat4 result{1.0f};
        result[0][0] = value.a1;
        result[1][0] = value.a2;
        result[2][0] = value.a3;
        result[3][0] = value.a4;
        result[0][1] = value.b1;
        result[1][1] = value.b2;
        result[2][1] = value.b3;
        result[3][1] = value.b4;
        result[0][2] = value.c1;
        result[1][2] = value.c2;
        result[2][2] = value.c3;
        result[3][2] = value.c4;
        result[0][3] = value.d1;
        result[1][3] = value.d2;
        result[2][3] = value.d3;
        result[3][3] = value.d4;
        return result;
    }

    Assets::Assimp::ImportedSceneBounds emptyBounds()
    {
        const float maxFloat = std::numeric_limits<float>::max();
        return {{maxFloat, maxFloat, maxFloat}, {-maxFloat, -maxFloat, -maxFloat}, false};
    }

    void includePoint(Assets::Assimp::ImportedSceneBounds& bounds, const glm::vec3& point)
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

    void includeBounds(Assets::Assimp::ImportedSceneBounds& bounds, const Assets::Assimp::ImportedSceneBounds& other)
    {
        if (!other.valid) {
            return;
        }
        includePoint(bounds, other.min);
        includePoint(bounds, other.max);
    }

    void includeTransformedBounds(
        Assets::Assimp::ImportedSceneBounds& bounds,
        const Assets::Assimp::ImportedSceneBounds& localBounds,
        const glm::mat4& transform)
    {
        if (!localBounds.valid) {
            return;
        }

        for (uint32_t corner = 0; corner < 8; ++corner) {
            const glm::vec3 point{
                (corner & 1) ? localBounds.max.x : localBounds.min.x,
                (corner & 2) ? localBounds.max.y : localBounds.min.y,
                (corner & 4) ? localBounds.max.z : localBounds.min.z,
            };
            includePoint(bounds, glm::vec3{transform * glm::vec4{point, 1.0f}});
        }
    }

    Assets::Assimp::ImportedSceneAlphaMode readAlphaMode(const aiMaterial& material, float opacity)
    {
        const std::string gltfAlphaMode = readStringProperty(material, "$mat.gltf.alphaMode", 0, 0);
        if (gltfAlphaMode == "MASK") {
            return Assets::Assimp::ImportedSceneAlphaMode::Mask;
        }
        if (gltfAlphaMode == "BLEND") {
            return Assets::Assimp::ImportedSceneAlphaMode::Blend;
        }
        if (opacity < 0.999f) {
            return Assets::Assimp::ImportedSceneAlphaMode::Blend;
        }
        return Assets::Assimp::ImportedSceneAlphaMode::Opaque;
    }

    Assets::Assimp::ImportedSceneTextureSemantic semanticForType(aiTextureType type)
    {
        switch (type) {
            case aiTextureType_BASE_COLOR:
            case aiTextureType_DIFFUSE:
                return Assets::Assimp::ImportedSceneTextureSemantic::BaseColor;
            case aiTextureType_NORMALS:
            case aiTextureType_NORMAL_CAMERA:
                return Assets::Assimp::ImportedSceneTextureSemantic::Normal;
            case aiTextureType_METALNESS:
                return Assets::Assimp::ImportedSceneTextureSemantic::Metallic;
            case aiTextureType_DIFFUSE_ROUGHNESS:
                return Assets::Assimp::ImportedSceneTextureSemantic::Roughness;
            case aiTextureType_AMBIENT_OCCLUSION:
            case aiTextureType_LIGHTMAP:
                return Assets::Assimp::ImportedSceneTextureSemantic::Occlusion;
            case aiTextureType_EMISSIVE:
                return Assets::Assimp::ImportedSceneTextureSemantic::Emissive;
            case aiTextureType_UNKNOWN:
                return Assets::Assimp::ImportedSceneTextureSemantic::MetallicRoughness;
            default:
                break;
        }
        return Assets::Assimp::ImportedSceneTextureSemantic::Unknown;
    }

    std::filesystem::path firstTexturePath(
        const aiMaterial& material,
        std::initializer_list<aiTextureType> types)
    {
        for (aiTextureType type : types) {
            std::filesystem::path path = readTexturePath(material, type);
            if (!path.empty()) {
                return path;
            }
        }
        return {};
    }

    void addTextureRecord(
        Assets::Assimp::ImportedScene& result,
        uint32_t materialIndex,
        aiTextureType type,
        Assets::Assimp::ImportedSceneTextureSemantic semantic,
        bool packedMetallicRoughness = false)
    {
        const Assets::Assimp::ImportedSceneMaterial& material = result.materials[materialIndex];
        std::filesystem::path path;
        switch (semantic) {
            case Assets::Assimp::ImportedSceneTextureSemantic::BaseColor:
                path = material.baseColorTexture;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Normal:
                path = material.normalTexture;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Metallic:
                path = material.metallicTexture;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Roughness:
                path = material.roughnessTexture;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::MetallicRoughness:
                path = material.metallicRoughnessTexture;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Occlusion:
                path = material.occlusionTexture;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Emissive:
                path = material.emissiveTexture;
                break;
            default:
                return;
        }

        if (path.empty()) {
            return;
        }

        const auto duplicate = std::find_if(
            result.textures.begin(),
            result.textures.end(),
            [&](const Assets::Assimp::ImportedSceneTexture& existing) {
                return existing.materialIndex == materialIndex &&
                    existing.semantic == semantic &&
                    existing.path == path;
            });
        if (duplicate != result.textures.end()) {
            return;
        }

        Assets::Assimp::ImportedSceneTexture texture;
        texture.path = path;
        texture.semantic = semanticForType(type) == Assets::Assimp::ImportedSceneTextureSemantic::Unknown
            ? semantic
            : semanticForType(type);
        texture.materialIndex = materialIndex;
        texture.mayBePackedMetallicRoughness = packedMetallicRoughness;
        switch (semantic) {
            case Assets::Assimp::ImportedSceneTextureSemantic::BaseColor:
                texture.hints = material.baseColorTextureHints;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Normal:
                texture.hints = material.normalTextureHints;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Metallic:
                texture.hints = material.metallicTextureHints;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Roughness:
                texture.hints = material.roughnessTextureHints;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::MetallicRoughness:
                texture.hints = material.metallicRoughnessTextureHints;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Occlusion:
                texture.hints = material.occlusionTextureHints;
                break;
            case Assets::Assimp::ImportedSceneTextureSemantic::Emissive:
                texture.hints = material.emissiveTextureHints;
                break;
            default:
                break;
        }
        result.textures.push_back(std::move(texture));
    }

    Assets::Assimp::ImportedSceneMaterial importSceneMaterial(
        const aiMaterial& aiMat,
        Assets::Assimp::ImportedSceneSourceFormat sourceFormat,
        Assets::Assimp::ImportedSceneDiagnostics& diagnostics)
    {
        Assets::Assimp::ImportedSceneMaterial material;

        aiString name;
        if (aiMat.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            material.name = name.C_Str();
        }

        aiColor4D baseColor;
        if (aiMat.Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
            aiMat.Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
            material.baseColorFactor = toGlm(baseColor);
        }

        material.metallicFactor = readFloatProperty(aiMat, AI_MATKEY_METALLIC_FACTOR, 0.0f);
        material.roughnessFactor = readFloatProperty(aiMat, AI_MATKEY_ROUGHNESS_FACTOR, 1.0f);
        material.baseColorTexture = readTexturePath(aiMat, aiTextureType_BASE_COLOR);
        material.baseColorTextureHints = readTextureHints(aiMat, aiTextureType_BASE_COLOR, Assets::Assimp::ImportedSceneTextureColorSpace::Srgb);
        if (material.baseColorTexture.empty()) {
            material.baseColorTexture = readTexturePath(aiMat, aiTextureType_DIFFUSE);
            material.baseColorTextureHints = readTextureHints(aiMat, aiTextureType_DIFFUSE, Assets::Assimp::ImportedSceneTextureColorSpace::Srgb);
        }
        material.normalTexture = readTexturePath(aiMat, aiTextureType_NORMALS);
        material.normalTextureHints = readTextureHints(aiMat, aiTextureType_NORMALS, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        if (material.normalTexture.empty()) {
            material.normalTexture = readTexturePath(aiMat, aiTextureType_NORMAL_CAMERA);
            material.normalTextureHints = readTextureHints(aiMat, aiTextureType_NORMAL_CAMERA, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        }
        if (sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx && material.normalTexture.empty()) {
            material.normalTexture = firstTexturePath(aiMat, {aiTextureType_HEIGHT, aiTextureType_DISPLACEMENT});
            material.normalTextureHints = readTextureHints(aiMat, aiTextureType_HEIGHT, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        }
        material.normalScale = readFloatProperty(aiMat, "$mat.gltf.normalTexture.scale", 0, 0, 1.0f);
        material.metallicTexture = readTexturePath(aiMat, aiTextureType_METALNESS);
        material.metallicTextureHints = readTextureHints(aiMat, aiTextureType_METALNESS, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        material.roughnessTexture = readTexturePath(aiMat, aiTextureType_DIFFUSE_ROUGHNESS);
        material.roughnessTextureHints = readTextureHints(aiMat, aiTextureType_DIFFUSE_ROUGHNESS, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        if (isGltfLike(sourceFormat)) {
            material.metallicRoughnessTexture = readTexturePath(aiMat, aiTextureType_UNKNOWN);
            material.metallicRoughnessTextureHints = readTextureHints(aiMat, aiTextureType_UNKNOWN, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        }
        if (isGltfLike(sourceFormat) && material.metallicRoughnessTexture.empty() && !material.metallicTexture.empty() &&
            material.metallicTexture == material.roughnessTexture) {
            material.metallicRoughnessTexture = material.metallicTexture;
            material.metallicRoughnessTextureHints = material.metallicTextureHints;
        }
        material.hasPackedMetallicRoughnessTexture = !material.metallicRoughnessTexture.empty();
        material.occlusionTexture = readTexturePath(aiMat, aiTextureType_AMBIENT_OCCLUSION);
        material.occlusionTextureHints = readTextureHints(aiMat, aiTextureType_AMBIENT_OCCLUSION, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        if (material.occlusionTexture.empty()) {
            material.occlusionTexture = readTexturePath(aiMat, aiTextureType_LIGHTMAP);
            material.occlusionTextureHints = readTextureHints(aiMat, aiTextureType_LIGHTMAP, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        }
        material.occlusionStrength = readFloatProperty(aiMat, "$mat.gltf.occlusionTexture.strength", 0, 0, 1.0f);
        material.emissiveTexture = readTexturePath(aiMat, aiTextureType_EMISSIVE);
        material.emissiveTextureHints = readTextureHints(aiMat, aiTextureType_EMISSIVE, Assets::Assimp::ImportedSceneTextureColorSpace::Srgb);
        aiColor4D emissiveColor;
        if (aiMat.Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS) {
            material.emissiveFactor = {emissiveColor.r, emissiveColor.g, emissiveColor.b};
        }

        const float opacity = readFloatProperty(aiMat, AI_MATKEY_OPACITY, 1.0f);
        material.alphaMode = readAlphaMode(aiMat, opacity);
        material.alphaCutoff = readFloatProperty(aiMat, "$mat.gltf.alphaCutoff", 0, 0, 0.5f);
        material.doubleSided = readBoolProperty(aiMat, AI_MATKEY_TWOSIDED, false);

        if (sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx) {
            material.metallicFactor = readFloatProperty(aiMat, AI_MATKEY_METALLIC_FACTOR, 0.0f);
            material.roughnessFactor = readFloatProperty(aiMat, AI_MATKEY_ROUGHNESS_FACTOR, 1.0f);
            const bool missingPbr =
                material.metallicTexture.empty() &&
                material.roughnessTexture.empty() &&
                material.metallicRoughnessTexture.empty();
            if (missingPbr) {
                ++diagnostics.missingPbrMaterialCount;
            }
        }

        return material;
    }

    Assets::Assimp::ImportedScenePrimitive importScenePrimitive(
        const aiMesh& aiMesh,
        Assets::Assimp::ImportedSceneDiagnostics& diagnostics)
    {
        Assets::Assimp::ImportedScenePrimitive primitive;
        primitive.materialIndex = aiMesh.mMaterialIndex;
        primitive.bounds = emptyBounds();
        primitive.vertices.reserve(aiMesh.mNumVertices);
        primitive.indices.reserve(static_cast<size_t>(aiMesh.mNumFaces) * 3);

        primitive.missingNormals = !aiMesh.HasNormals();
        primitive.missingTangents = !aiMesh.HasTangentsAndBitangents();
        primitive.missingTexcoord0 = !aiMesh.HasTextureCoords(0);
        primitive.hasTexcoord1 = aiMesh.HasTextureCoords(1);
        primitive.hasColor0 = aiMesh.HasVertexColors(0);

        if (primitive.missingNormals) {
            ++diagnostics.missingNormalPrimitiveCount;
        }
        if (primitive.missingTangents) {
            ++diagnostics.missingTangentPrimitiveCount;
        }
        if (primitive.missingTexcoord0) {
            ++diagnostics.missingTexcoord0PrimitiveCount;
        }
        if (primitive.hasTexcoord1) {
            ++diagnostics.texcoord1PrimitiveCount;
        }
        if (primitive.hasColor0) {
            ++diagnostics.vertexColorPrimitiveCount;
        }

        for (unsigned int vertexIndex = 0; vertexIndex < aiMesh.mNumVertices; ++vertexIndex) {
            Assets::Assimp::ImportedSceneVertex vertex;
            vertex.position = toGlm(aiMesh.mVertices[vertexIndex]);
            includePoint(primitive.bounds, vertex.position);

            if (aiMesh.HasNormals()) {
                vertex.normal = toGlm(aiMesh.mNormals[vertexIndex]);
                vertex.hasNormal = true;
            }

            if (aiMesh.HasTangentsAndBitangents()) {
                const glm::vec3 tangent = toGlm(aiMesh.mTangents[vertexIndex]);
                const glm::vec3 bitangent = toGlm(aiMesh.mBitangents[vertexIndex]);
                const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                vertex.tangent = {tangent.x, tangent.y, tangent.z, handedness};
                vertex.hasTangent = true;
            }

            if (aiMesh.HasTextureCoords(0)) {
                const aiVector3D& uv = aiMesh.mTextureCoords[0][vertexIndex];
                vertex.texcoord0 = {uv.x, uv.y};
                vertex.hasTexcoord0 = true;
            }

            if (aiMesh.HasTextureCoords(1)) {
                const aiVector3D& uv = aiMesh.mTextureCoords[1][vertexIndex];
                vertex.texcoord1 = {uv.x, uv.y};
                vertex.hasTexcoord1 = true;
            }

            if (aiMesh.HasVertexColors(0)) {
                vertex.color0 = toGlm(aiMesh.mColors[0][vertexIndex]);
                vertex.hasColor0 = true;
            }

            primitive.vertices.push_back(vertex);
        }

        for (unsigned int faceIndex = 0; faceIndex < aiMesh.mNumFaces; ++faceIndex) {
            const aiFace& face = aiMesh.mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }
            primitive.indices.push_back(face.mIndices[0]);
            primitive.indices.push_back(face.mIndices[1]);
            primitive.indices.push_back(face.mIndices[2]);
        }

        return primitive;
    }

    Assets::Assimp::ImportedSceneLightType lightType(aiLightSourceType type)
    {
        switch (type) {
            case aiLightSource_DIRECTIONAL:
                return Assets::Assimp::ImportedSceneLightType::Directional;
            case aiLightSource_POINT:
                return Assets::Assimp::ImportedSceneLightType::Point;
            case aiLightSource_SPOT:
                return Assets::Assimp::ImportedSceneLightType::Spot;
            case aiLightSource_AMBIENT:
                return Assets::Assimp::ImportedSceneLightType::Ambient;
            case aiLightSource_AREA:
                return Assets::Assimp::ImportedSceneLightType::Area;
            default:
                break;
        }
        return Assets::Assimp::ImportedSceneLightType::Unknown;
    }

    void traverseNode(
        const aiNode& node,
        uint32_t parentIndex,
        const glm::mat4& parentTransform,
        Assets::Assimp::ImportedScene& result,
        std::unordered_map<std::string, uint32_t>& nodeByName)
    {
        const uint32_t nodeIndex = static_cast<uint32_t>(result.nodes.size());
        Assets::Assimp::ImportedSceneNode importedNode;
        importedNode.name = node.mName.C_Str();
        importedNode.parentIndex = parentIndex;
        importedNode.localTransform = toGlm(node.mTransformation);
        importedNode.worldTransform = parentTransform * importedNode.localTransform;
        importedNode.meshIndices.reserve(node.mNumMeshes);
        for (unsigned int meshIndex = 0; meshIndex < node.mNumMeshes; ++meshIndex) {
            importedNode.meshIndices.push_back(node.mMeshes[meshIndex]);
            if (meshIndex == 0) {
                ++result.diagnostics.meshNodeCount;
            }
            if (node.mMeshes[meshIndex] < result.meshes.size()) {
                includeTransformedBounds(result.bounds, result.meshes[node.mMeshes[meshIndex]].bounds, importedNode.worldTransform);
            }
        }

        result.nodes.push_back(std::move(importedNode));
        if (!result.nodes[nodeIndex].name.empty()) {
            nodeByName[result.nodes[nodeIndex].name] = nodeIndex;
        }
        if (parentIndex != InvalidIndex) {
            result.nodes[parentIndex].childIndices.push_back(nodeIndex);
        }

        for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex) {
            if (node.mChildren[childIndex]) {
                traverseNode(*node.mChildren[childIndex], nodeIndex, result.nodes[nodeIndex].worldTransform, result, nodeByName);
            }
        }
    }

    void addUnique(std::vector<uint32_t>& values, uint32_t value)
    {
        if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(value);
        }
    }

    std::vector<uint32_t> nodesReferencingMesh(
        const Assets::Assimp::ImportedScene& result,
        uint32_t meshIndex)
    {
        std::vector<uint32_t> nodes;
        for (uint32_t nodeIndex = 0; nodeIndex < result.nodes.size(); ++nodeIndex) {
            const std::vector<uint32_t>& meshIndices = result.nodes[nodeIndex].meshIndices;
            if (std::find(meshIndices.begin(), meshIndices.end(), meshIndex) != meshIndices.end()) {
                nodes.push_back(nodeIndex);
            }
        }
        return nodes;
    }

    uint32_t findJointParent(
        uint32_t nodeIndex,
        const Assets::Assimp::ImportedScene& result,
        const std::unordered_map<uint32_t, uint32_t>& jointByNode)
    {
        uint32_t parentNode = result.nodes[nodeIndex].parentIndex;
        while (parentNode != InvalidIndex && parentNode < result.nodes.size()) {
            if (const auto joint = jointByNode.find(parentNode); joint != jointByNode.end()) {
                return joint->second;
            }
            parentNode = result.nodes[parentNode].parentIndex;
        }
        return InvalidIndex;
    }

    uint32_t findSkeletonRoot(
        const Assets::Assimp::ImportedScene& result,
        const std::vector<uint32_t>& jointIndices)
    {
        for (uint32_t jointIndex : jointIndices) {
            if (jointIndex >= result.joints.size() || !result.joints[jointIndex].nodeIndex) {
                continue;
            }
            const uint32_t nodeIndex = *result.joints[jointIndex].nodeIndex;
            const uint32_t parentNode = result.nodes[nodeIndex].parentIndex;
            bool parentIsJoint = false;
            for (uint32_t otherJointIndex : jointIndices) {
                if (otherJointIndex < result.joints.size() &&
                    result.joints[otherJointIndex].nodeIndex &&
                    *result.joints[otherJointIndex].nodeIndex == parentNode) {
                    parentIsJoint = true;
                    break;
                }
            }
            if (!parentIsJoint) {
                return nodeIndex;
            }
        }
        return InvalidIndex;
    }

    void importSkinsAndVertexInfluences(
        const aiScene& scene,
        Assets::Assimp::ImportedScene& result,
        const std::unordered_map<std::string, uint32_t>& nodeByName)
    {
        std::unordered_map<std::string, uint32_t> jointByName;
        std::unordered_map<uint32_t, uint32_t> jointByNode;

        const auto getOrCreateJoint = [&](const aiBone& bone) -> uint32_t {
            const std::string name = bone.mName.C_Str();
            if (const auto found = jointByName.find(name); found != jointByName.end()) {
                const uint32_t jointIndex = found->second;
                Assets::Assimp::ImportedSceneJoint& joint = result.joints[jointIndex];
                joint.inverseBindMatrix = toGlm(bone.mOffsetMatrix);
                joint.hasInverseBindMatrix = true;
                return jointIndex;
            }

            Assets::Assimp::ImportedSceneJoint joint;
            joint.name = name;
            joint.inverseBindMatrix = toGlm(bone.mOffsetMatrix);
            joint.hasInverseBindMatrix = true;
            if (const auto node = nodeByName.find(name); node != nodeByName.end()) {
                joint.nodeIndex = node->second;
                joint.localBindTransform = result.nodes[node->second].localTransform;
                joint.worldBindTransform = result.nodes[node->second].worldTransform;
            } else {
                result.diagnostics.warnings.push_back("Imported skin joint did not resolve to a scene node: " + name);
            }

            const uint32_t jointIndex = static_cast<uint32_t>(result.joints.size());
            result.joints.push_back(std::move(joint));
            jointByName[name] = jointIndex;
            if (result.joints[jointIndex].nodeIndex) {
                jointByNode[*result.joints[jointIndex].nodeIndex] = jointIndex;
            }
            return jointIndex;
        };

        for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
            const aiMesh* aiMesh = scene.mMeshes[meshIndex];
            if (!aiMesh || !aiMesh->HasBones() || meshIndex >= result.meshes.size()) {
                continue;
            }
            if (result.meshes[meshIndex].primitives.empty()) {
                result.diagnostics.warnings.push_back("Skinned mesh had no imported primitive to receive joint weights.");
                continue;
            }

            ++result.diagnostics.skinnedMeshCount;
            Assets::Assimp::ImportedSceneSkin skin;
            skin.name = std::string{aiMesh->mName.C_Str()} + ".skin";
            skin.meshIndices.push_back(meshIndex);
            skin.nodeIndices = nodesReferencingMesh(result, meshIndex);
            skin.jointIndices.reserve(aiMesh->mNumBones);

            for (unsigned int boneIndex = 0; boneIndex < aiMesh->mNumBones; ++boneIndex) {
                const aiBone* aiBone = aiMesh->mBones[boneIndex];
                if (!aiBone) {
                    result.diagnostics.warnings.push_back("Encountered null bone while importing skinned mesh.");
                    continue;
                }

                const uint32_t jointIndex = getOrCreateJoint(*aiBone);
                addUnique(skin.jointIndices, jointIndex);
                if (result.joints[jointIndex].hasInverseBindMatrix) {
                    ++skin.inverseBindMatrixCount;
                }

                for (unsigned int weightIndex = 0; weightIndex < aiBone->mNumWeights; ++weightIndex) {
                    const aiVertexWeight& weight = aiBone->mWeights[weightIndex];
                    if (weight.mVertexId >= result.meshes[meshIndex].primitives.front().vertices.size()) {
                        result.diagnostics.warnings.push_back("Bone weight referenced a vertex outside the imported mesh.");
                        continue;
                    }
                    Assets::Assimp::ImportedSceneVertexInfluence influence;
                    influence.jointIndex = jointIndex;
                    influence.weight = weight.mWeight;
                    result.meshes[meshIndex].primitives.front().vertices[weight.mVertexId].influences.push_back(influence);
                }
            }

            const uint32_t skeletonRoot = findSkeletonRoot(result, skin.jointIndices);
            if (skeletonRoot != InvalidIndex) {
                skin.skeletonRootNodeIndex = skeletonRoot;
            }
            result.skins.push_back(std::move(skin));
        }

        for (uint32_t jointIndex = 0; jointIndex < result.joints.size(); ++jointIndex) {
            Assets::Assimp::ImportedSceneJoint& joint = result.joints[jointIndex];
            if (joint.nodeIndex) {
                joint.parentJointIndex = findJointParent(*joint.nodeIndex, result, jointByNode);
            }
        }

        for (Assets::Assimp::ImportedSceneMesh& mesh : result.meshes) {
            for (Assets::Assimp::ImportedScenePrimitive& primitive : mesh.primitives) {
                for (Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                    std::sort(vertex.influences.begin(), vertex.influences.end(), [](const auto& lhs, const auto& rhs) {
                        return lhs.weight > rhs.weight;
                    });
                    const uint32_t influenceCount = static_cast<uint32_t>(vertex.influences.size());
                    result.diagnostics.maxInfluencesPerVertex = std::max(result.diagnostics.maxInfluencesPerVertex, influenceCount);
                    if (influenceCount == 0) {
                        if (!result.skins.empty()) {
                            ++result.diagnostics.zeroWeightVertexCount;
                        }
                        continue;
                    }
                    ++result.diagnostics.influencedVertexCount;
                    if (influenceCount > 4) {
                        ++result.diagnostics.overFourInfluenceVertexCount;
                    }
                    float weightSum = 0.0f;
                    for (const Assets::Assimp::ImportedSceneVertexInfluence& influence : vertex.influences) {
                        weightSum += influence.weight;
                    }
                    if (std::abs(weightSum - 1.0f) > 0.01f) {
                        ++result.diagnostics.nonNormalizedWeightVertexCount;
                    }
                }
            }
        }

        if (result.diagnostics.overFourInfluenceVertexCount > 0) {
            result.diagnostics.warnings.push_back("One or more vertices have more than four joint influences; renderer packing is deferred.");
        }
        if (result.diagnostics.nonNormalizedWeightVertexCount > 0) {
            result.diagnostics.warnings.push_back("One or more skinned vertices have non-normalized joint weights.");
        }
        if (result.diagnostics.zeroWeightVertexCount > 0) {
            result.diagnostics.warnings.push_back("One or more vertices in a skinned scene have no joint weights.");
        }
        result.diagnostics.skinCount = static_cast<uint32_t>(result.skins.size());
        result.diagnostics.jointCount = static_cast<uint32_t>(result.joints.size());
    }

    void importAnimations(
        const aiScene& scene,
        Assets::Assimp::ImportedScene& result,
        const std::unordered_map<std::string, uint32_t>& nodeByName)
    {
        result.animations.reserve(scene.mNumAnimations);
        for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
            const aiAnimation* aiAnimation = scene.mAnimations[animationIndex];
            if (!aiAnimation) {
                result.diagnostics.warnings.push_back("Encountered null animation record.");
                continue;
            }

            const double ticksPerSecond = aiAnimation->mTicksPerSecond > 0.0 ? aiAnimation->mTicksPerSecond : 1.0;
            Assets::Assimp::ImportedSceneAnimationClip clip;
            clip.name = aiAnimation->mName.length > 0
                ? aiAnimation->mName.C_Str()
                : "animation." + std::to_string(animationIndex);
            if (aiAnimation->mName.length == 0 && result.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx) {
                ++result.diagnostics.fbxUnnamedAnimationStackCount;
            }
            clip.ticksPerSecond = ticksPerSecond;
            clip.durationSeconds = static_cast<float>(aiAnimation->mDuration / ticksPerSecond);
            clip.channels.reserve(aiAnimation->mNumChannels);

            for (unsigned int channelIndex = 0; channelIndex < aiAnimation->mNumChannels; ++channelIndex) {
                const aiNodeAnim* aiChannel = aiAnimation->mChannels[channelIndex];
                if (!aiChannel) {
                    result.diagnostics.warnings.push_back("Encountered null animation channel.");
                    continue;
                }

                Assets::Assimp::ImportedSceneAnimationChannel channel;
                channel.targetName = aiChannel->mNodeName.C_Str();
                if (const auto node = nodeByName.find(channel.targetName); node != nodeByName.end()) {
                    channel.targetNodeIndex = node->second;
                } else {
                    ++result.diagnostics.missingAnimationTargetCount;
                    result.diagnostics.warnings.push_back("Animation channel target did not resolve to a scene node: " + channel.targetName);
                }

                channel.translationKeys.reserve(aiChannel->mNumPositionKeys);
                for (unsigned int keyIndex = 0; keyIndex < aiChannel->mNumPositionKeys; ++keyIndex) {
                    const aiVectorKey& key = aiChannel->mPositionKeys[keyIndex];
                    channel.translationKeys.push_back({
                        static_cast<float>(key.mTime / ticksPerSecond),
                        toGlm(key.mValue),
                        Assets::Assimp::ImportedSceneAnimationInterpolation::Linear,
                    });
                }

                channel.rotationKeys.reserve(aiChannel->mNumRotationKeys);
                for (unsigned int keyIndex = 0; keyIndex < aiChannel->mNumRotationKeys; ++keyIndex) {
                    const aiQuatKey& key = aiChannel->mRotationKeys[keyIndex];
                    channel.rotationKeys.push_back({
                        static_cast<float>(key.mTime / ticksPerSecond),
                        glm::normalize(glm::quat{key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z}),
                        Assets::Assimp::ImportedSceneAnimationInterpolation::Linear,
                    });
                }

                channel.scaleKeys.reserve(aiChannel->mNumScalingKeys);
                for (unsigned int keyIndex = 0; keyIndex < aiChannel->mNumScalingKeys; ++keyIndex) {
                    const aiVectorKey& key = aiChannel->mScalingKeys[keyIndex];
                    channel.scaleKeys.push_back({
                        static_cast<float>(key.mTime / ticksPerSecond),
                        toGlm(key.mValue),
                        Assets::Assimp::ImportedSceneAnimationInterpolation::Linear,
                    });
                }

                result.diagnostics.translationKeyCount += static_cast<uint32_t>(channel.translationKeys.size());
                result.diagnostics.rotationKeyCount += static_cast<uint32_t>(channel.rotationKeys.size());
                result.diagnostics.scaleKeyCount += static_cast<uint32_t>(channel.scaleKeys.size());
                clip.channels.push_back(std::move(channel));
                ++result.diagnostics.animationChannelCount;
            }

            result.animations.push_back(std::move(clip));
        }

        result.diagnostics.animationCount = static_cast<uint32_t>(result.animations.size());
        if (result.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx) {
            result.diagnostics.fbxAnimationStackCount = result.diagnostics.animationCount;
        }
    }

    bool isIdentity(const glm::mat4& matrix)
    {
        const glm::mat4 identity{1.0f};
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (std::abs(matrix[column][row] - identity[column][row]) > 0.00001f) {
                    return false;
                }
            }
        }
        return true;
    }

    bool isArtificialSceneRoot(const aiNode& node)
    {
        return node.mNumMeshes == 0 &&
            node.mNumChildren > 0 &&
            isIdentity(toGlm(node.mTransformation));
    }
}

namespace Assets::Assimp {
    ImportedSceneSourceFormat detectSceneSourceFormat(const std::filesystem::path& path)
    {
        const std::string extension = lowercaseExtension(path);
        if (extension == ".gltf") {
            return ImportedSceneSourceFormat::Gltf;
        }
        if (extension == ".glb") {
            return ImportedSceneSourceFormat::Glb;
        }
        if (extension == ".fbx") {
            return ImportedSceneSourceFormat::Fbx;
        }
        return ImportedSceneSourceFormat::Unknown;
    }

    const char* sourceFormatName(ImportedSceneSourceFormat format)
    {
        switch (format) {
            case ImportedSceneSourceFormat::Gltf:
                return "gltf";
            case ImportedSceneSourceFormat::Glb:
                return "glb";
            case ImportedSceneSourceFormat::Fbx:
                return "fbx";
            case ImportedSceneSourceFormat::Unknown:
            default:
                return "unknown";
        }
    }

    ImportResult importStaticMesh(const std::filesystem::path& path)
    {
        ::Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            path.string(),
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType
        );

        if (!scene) {
            return {false, importer.GetErrorString(), {}};
        }

        if (scene->mNumAnimations > 0) {
            return {false, "Animated meshes are not supported by the static mesh importer.", {}};
        }

        ImportResult result;
        result.success = true;

        result.mesh.materials.reserve(scene->mNumMaterials > 0 ? scene->mNumMaterials : 1);
        for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
            const aiMaterial* aiMat = scene->mMaterials[materialIndex];
            ImportedMaterial material;

            aiString name;
            if (aiMat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
                material.name = name.C_Str();
            }

            aiColor4D baseColor;
            if (aiMat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
                aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
                material.baseColorFactor = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};
            }

            material.metallicFactor = readFloatProperty(*aiMat, AI_MATKEY_METALLIC_FACTOR, 0.0f);
            material.roughnessFactor = readFloatProperty(*aiMat, AI_MATKEY_ROUGHNESS_FACTOR, 1.0f);
            material.baseColorTexture = readTexturePath(*aiMat, aiTextureType_BASE_COLOR);
            if (material.baseColorTexture.empty()) {
                material.baseColorTexture = readTexturePath(*aiMat, aiTextureType_DIFFUSE);
            }
            material.normalTexture = readTexturePath(*aiMat, aiTextureType_NORMALS);
            if (material.normalTexture.empty()) {
                material.normalTexture = readTexturePath(*aiMat, aiTextureType_NORMAL_CAMERA);
            }
            material.metallicTexture = readTexturePath(*aiMat, aiTextureType_METALNESS);
            material.roughnessTexture = readTexturePath(*aiMat, aiTextureType_DIFFUSE_ROUGHNESS);

            result.mesh.materials.push_back(std::move(material));
        }

        if (result.mesh.materials.empty()) {
            result.mesh.materials.emplace_back();
        }

        result.mesh.submeshes.reserve(scene->mNumMeshes);
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            const aiMesh* aiMesh = scene->mMeshes[meshIndex];
            if (!aiMesh || aiMesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
                continue;
            }

            if (aiMesh->HasBones()) {
                return {false, "Skinned meshes are not supported by the static mesh importer.", {}};
            }

            ImportedSubmesh submesh;
            submesh.materialIndex = aiMesh->mMaterialIndex;
            submesh.vertices.reserve(aiMesh->mNumVertices);
            submesh.indices.reserve(static_cast<size_t>(aiMesh->mNumFaces) * 3);

            for (unsigned int vertexIndex = 0; vertexIndex < aiMesh->mNumVertices; ++vertexIndex) {
                ImportedVertex vertex;
                const aiVector3D& position = aiMesh->mVertices[vertexIndex];
                vertex.position = {position.x, position.y, position.z};

                if (aiMesh->HasNormals()) {
                    const aiVector3D& normal = aiMesh->mNormals[vertexIndex];
                    vertex.normal = {normal.x, normal.y, normal.z};
                }

                if (aiMesh->HasTangentsAndBitangents()) {
                    const aiVector3D& tangent = aiMesh->mTangents[vertexIndex];
                    vertex.tangent = {tangent.x, tangent.y, tangent.z};
                }

                if (aiMesh->HasTextureCoords(0)) {
                    const aiVector3D& uv = aiMesh->mTextureCoords[0][vertexIndex];
                    vertex.uv = {uv.x, uv.y};
                }

                submesh.vertices.push_back(vertex);
            }

            for (unsigned int faceIndex = 0; faceIndex < aiMesh->mNumFaces; ++faceIndex) {
                const aiFace& face = aiMesh->mFaces[faceIndex];
                if (face.mNumIndices != 3) {
                    continue;
                }

                submesh.indices.push_back(face.mIndices[0]);
                submesh.indices.push_back(face.mIndices[1]);
                submesh.indices.push_back(face.mIndices[2]);
            }

            if (!submesh.vertices.empty() && !submesh.indices.empty()) {
                result.mesh.submeshes.push_back(std::move(submesh));
            }
        }

        if (result.mesh.submeshes.empty()) {
            return {false, "No static triangle meshes were found in the asset.", {}};
        }

        return result;
    }

    ImportedScene importScene(const std::filesystem::path& path)
    {
        const ImportedSceneSourceFormat sourceFormat = detectSceneSourceFormat(path);
        ::Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            path.string(),
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType
        );

        if (!scene) {
            ImportedScene failed;
            failed.success = false;
            failed.error = importer.GetErrorString();
            failed.sourceFormat = sourceFormat;
            failed.diagnostics.sourceFormat = sourceFormat;
            return failed;
        }

        ImportedScene result;
        result.success = true;
        result.sourceFormat = sourceFormat;
        result.diagnostics.sourceFormat = sourceFormat;
        result.bounds = emptyBounds();
        result.diagnostics.embeddedTextureCount = scene->mNumTextures;
        if (scene->mMetaData) {
            int upAxis = result.diagnostics.fbxUpAxis;
            int upAxisSign = result.diagnostics.fbxUpAxisSign;
            float unitScaleFactor = result.diagnostics.fbxUnitScaleFactor;
            if (scene->mMetaData->Get("UpAxis", upAxis)) {
                result.diagnostics.fbxUpAxis = upAxis;
            }
            if (scene->mMetaData->Get("UpAxisSign", upAxisSign)) {
                result.diagnostics.fbxUpAxisSign = upAxisSign;
            }
            if (scene->mMetaData->Get("UnitScaleFactor", unitScaleFactor)) {
                result.diagnostics.fbxUnitScaleFactor = unitScaleFactor;
            }
        }
        if (sourceFormat == ImportedSceneSourceFormat::Fbx) {
            result.diagnostics.warnings.push_back(
                "FBX authored scene import preserves static and exposed skeletal data, but full FBX animation semantics are best-effort.");
            ++result.diagnostics.fbxAnimationSemanticWarningCount;
            if (scene->mNumTextures > 0) {
                result.diagnostics.warnings.push_back(
                    "FBX embedded textures are recorded as diagnostics but are not extracted by the runtime texture loader.");
            }
        }
        std::unordered_set<uint32_t> usedMaterialIndices;
        std::vector<uint32_t> materialIndexRemap(scene->mNumMaterials, 0);
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            const aiMesh* aiMesh = scene->mMeshes[meshIndex];
            if (aiMesh && aiMesh->mMaterialIndex < scene->mNumMaterials) {
                usedMaterialIndices.insert(aiMesh->mMaterialIndex);
            }
        }

        result.materials.reserve(!usedMaterialIndices.empty() ? usedMaterialIndices.size() : 1);
        for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
            if (!usedMaterialIndices.contains(materialIndex)) {
                continue;
            }

            const aiMaterial* aiMat = scene->mMaterials[materialIndex];
            const uint32_t importedMaterialIndex = static_cast<uint32_t>(result.materials.size());
            materialIndexRemap[materialIndex] = importedMaterialIndex;
            if (!aiMat) {
                result.materials.emplace_back();
                result.diagnostics.warnings.push_back("Encountered null material; inserted a fallback material record.");
                continue;
            }

            result.materials.push_back(importSceneMaterial(*aiMat, sourceFormat, result.diagnostics));
            ImportedSceneMaterial& material = result.materials.back();
            if (material.alphaMode != ImportedSceneAlphaMode::Opaque) {
                ++result.diagnostics.alphaMaterialCount;
            }
            if (material.doubleSided) {
                ++result.diagnostics.doubleSidedMaterialCount;
            }
            addTextureRecord(result, importedMaterialIndex, aiTextureType_BASE_COLOR, ImportedSceneTextureSemantic::BaseColor);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_DIFFUSE, ImportedSceneTextureSemantic::BaseColor);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_NORMALS, ImportedSceneTextureSemantic::Normal);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_NORMAL_CAMERA, ImportedSceneTextureSemantic::Normal);
            if (sourceFormat == ImportedSceneSourceFormat::Fbx) {
                addTextureRecord(result, importedMaterialIndex, aiTextureType_HEIGHT, ImportedSceneTextureSemantic::Normal);
                addTextureRecord(result, importedMaterialIndex, aiTextureType_DISPLACEMENT, ImportedSceneTextureSemantic::Normal);
            }
            addTextureRecord(result, importedMaterialIndex, aiTextureType_METALNESS, ImportedSceneTextureSemantic::Metallic);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_DIFFUSE_ROUGHNESS, ImportedSceneTextureSemantic::Roughness);
            if (isGltfLike(sourceFormat)) {
                addTextureRecord(result, importedMaterialIndex, aiTextureType_UNKNOWN, ImportedSceneTextureSemantic::MetallicRoughness, true);
            }
            addTextureRecord(result, importedMaterialIndex, aiTextureType_AMBIENT_OCCLUSION, ImportedSceneTextureSemantic::Occlusion);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_LIGHTMAP, ImportedSceneTextureSemantic::Occlusion);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_EMISSIVE, ImportedSceneTextureSemantic::Emissive);
        }

        if (result.materials.empty()) {
            result.materials.emplace_back();
        }

        result.meshes.reserve(scene->mNumMeshes);
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            const aiMesh* aiMesh = scene->mMeshes[meshIndex];
            if (!aiMesh || aiMesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
                result.meshes.emplace_back();
                result.diagnostics.warnings.push_back("Skipped a null or non-triangle mesh.");
                continue;
            }

            ImportedSceneMesh mesh;
            mesh.name = aiMesh->mName.C_Str();
            mesh.bounds = emptyBounds();
            ImportedScenePrimitive primitive = importScenePrimitive(*aiMesh, result.diagnostics);
            if (aiMesh->mMaterialIndex < materialIndexRemap.size()) {
                primitive.materialIndex = materialIndexRemap[aiMesh->mMaterialIndex];
            }
            includeBounds(mesh.bounds, primitive.bounds);
            if (!primitive.vertices.empty() && !primitive.indices.empty()) {
                mesh.primitives.push_back(std::move(primitive));
                ++result.diagnostics.primitiveCount;
            }
            result.meshes.push_back(std::move(mesh));
        }

        std::unordered_map<std::string, uint32_t> nodeByName;
        if (scene->mRootNode) {
            if (isArtificialSceneRoot(*scene->mRootNode)) {
                for (unsigned int childIndex = 0; childIndex < scene->mRootNode->mNumChildren; ++childIndex) {
                    if (scene->mRootNode->mChildren[childIndex]) {
                        if (result.rootNodeIndex == InvalidIndex) {
                            result.rootNodeIndex = static_cast<uint32_t>(result.nodes.size());
                        }
                        traverseNode(*scene->mRootNode->mChildren[childIndex], InvalidIndex, glm::mat4{1.0f}, result, nodeByName);
                    }
                }
            } else {
                result.rootNodeIndex = 0;
                traverseNode(*scene->mRootNode, InvalidIndex, glm::mat4{1.0f}, result, nodeByName);
            }
        } else {
            result.diagnostics.warnings.push_back("Imported scene has no root node.");
        }

        importSkinsAndVertexInfluences(*scene, result, nodeByName);
        importAnimations(*scene, result, nodeByName);
        if (sourceFormat == ImportedSceneSourceFormat::Fbx && !result.animations.empty()) {
            result.diagnostics.warnings.push_back(
                "FBX animation stacks were imported from Assimp node channels; constraints, layers, and pre/post rotations are not represented explicitly.");
            ++result.diagnostics.fbxAnimationSemanticWarningCount;
            if (result.diagnostics.fbxUnnamedAnimationStackCount > 0) {
                result.diagnostics.warnings.push_back("One or more FBX animation stacks had no source name and were assigned generated names.");
            }
            if (result.diagnostics.missingAnimationTargetCount > 0) {
                result.diagnostics.warnings.push_back("One or more FBX animation channels target nodes that Assimp did not expose in the scene graph.");
            }
        }
        if (containsSkeletalOrAnimationData(result)) {
            result.diagnostics.warnings.push_back(
                "Imported skeletal or animation data is CPU-only for the static authored scene path; use the animated model runtime to load it.");
        }

        result.lights.reserve(scene->mNumLights);
        for (unsigned int lightIndex = 0; lightIndex < scene->mNumLights; ++lightIndex) {
            const aiLight* aiLight = scene->mLights[lightIndex];
            if (!aiLight) {
                continue;
            }

            ImportedSceneLight light;
            light.name = aiLight->mName.C_Str();
            light.type = lightType(aiLight->mType);
            light.color = {aiLight->mColorDiffuse.r, aiLight->mColorDiffuse.g, aiLight->mColorDiffuse.b};
            light.intensity = std::max({aiLight->mColorDiffuse.r, aiLight->mColorDiffuse.g, aiLight->mColorDiffuse.b});
            light.position = {aiLight->mPosition.x, aiLight->mPosition.y, aiLight->mPosition.z};
            light.direction = {aiLight->mDirection.x, aiLight->mDirection.y, aiLight->mDirection.z};
            light.range = aiLight->mAttenuationLinear > 0.0f ? 1.0f / aiLight->mAttenuationLinear : 0.0f;
            light.innerConeAngle = aiLight->mAngleInnerCone;
            light.outerConeAngle = aiLight->mAngleOuterCone;
            const auto node = nodeByName.find(light.name);
            if (node != nodeByName.end()) {
                light.nodeIndex = node->second;
            }
            result.lights.push_back(std::move(light));
        }

        result.diagnostics.nodeCount = static_cast<uint32_t>(result.nodes.size());
        result.diagnostics.meshCount = static_cast<uint32_t>(result.meshes.size());
        result.diagnostics.materialCount = static_cast<uint32_t>(result.materials.size());
        result.diagnostics.textureCount = static_cast<uint32_t>(result.textures.size());
        result.diagnostics.lightCount = static_cast<uint32_t>(result.lights.size());

        if (sourceFormat == ImportedSceneSourceFormat::Fbx && result.diagnostics.missingPbrMaterialCount > 0) {
            result.diagnostics.warnings.push_back(
                "One or more FBX materials did not expose PBR metallic/roughness data; default dielectric material values were used.");
        }
        if (result.meshes.empty() && result.skins.empty() && result.animations.empty()) {
            result.success = false;
            result.error = "No static triangle meshes were found in the authored scene.";
        } else if (result.meshes.empty()) {
            result.diagnostics.warnings.push_back(
                "Imported animated scene has no renderable triangle meshes; CPU skeleton/clip data was preserved only.");
        }

        return result;
    }

    bool containsSkeletalOrAnimationData(const ImportedScene& scene)
    {
        return !scene.skins.empty() || !scene.joints.empty() || !scene.animations.empty();
    }
}
