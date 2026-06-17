#include "Assets/Assimp/Importer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

    Assets::Assimp::ImportedSceneMaterial importSceneMaterial(const aiMaterial& aiMat)
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
        material.normalScale = readFloatProperty(aiMat, "$mat.gltf.normalTexture.scale", 0, 0, 1.0f);
        material.metallicTexture = readTexturePath(aiMat, aiTextureType_METALNESS);
        material.metallicTextureHints = readTextureHints(aiMat, aiTextureType_METALNESS, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        material.roughnessTexture = readTexturePath(aiMat, aiTextureType_DIFFUSE_ROUGHNESS);
        material.roughnessTextureHints = readTextureHints(aiMat, aiTextureType_DIFFUSE_ROUGHNESS, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        material.metallicRoughnessTexture = readTexturePath(aiMat, aiTextureType_UNKNOWN);
        material.metallicRoughnessTextureHints = readTextureHints(aiMat, aiTextureType_UNKNOWN, Assets::Assimp::ImportedSceneTextureColorSpace::Linear);
        if (material.metallicRoughnessTexture.empty() && !material.metallicTexture.empty() &&
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
            return {false, importer.GetErrorString()};
        }

        ImportedScene result;
        result.success = true;
        result.bounds = emptyBounds();
        result.diagnostics.unsupportedAnimationCount = scene->mNumAnimations;
        if (scene->mNumAnimations > 0) {
            result.diagnostics.warnings.push_back("Animated scene data is not supported by the authored static scene importer.");
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

            result.materials.push_back(importSceneMaterial(*aiMat));
            ImportedSceneMaterial& material = result.materials.back();
            if (material.alphaMode != ImportedSceneAlphaMode::Opaque) {
                ++result.diagnostics.alphaMaterialCount;
            }
            if (material.doubleSided) {
                ++result.diagnostics.doubleSidedMaterialCount;
            }
            addTextureRecord(result, importedMaterialIndex, aiTextureType_BASE_COLOR, ImportedSceneTextureSemantic::BaseColor);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_NORMALS, ImportedSceneTextureSemantic::Normal);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_METALNESS, ImportedSceneTextureSemantic::Metallic);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_DIFFUSE_ROUGHNESS, ImportedSceneTextureSemantic::Roughness);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_UNKNOWN, ImportedSceneTextureSemantic::MetallicRoughness, true);
            addTextureRecord(result, importedMaterialIndex, aiTextureType_AMBIENT_OCCLUSION, ImportedSceneTextureSemantic::Occlusion);
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

            if (aiMesh->HasBones()) {
                ++result.diagnostics.unsupportedSkinnedMeshCount;
                result.diagnostics.warnings.push_back("Skinned mesh data is not supported by the authored static scene importer.");
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

        if (result.diagnostics.unsupportedSkinnedMeshCount > 0) {
            result.success = false;
            result.error = "Skinned meshes are not supported by the authored static scene importer.";
        }
        if (result.meshes.empty()) {
            result.success = false;
            result.error = "No static triangle meshes were found in the authored scene.";
        }

        return result;
    }
}
