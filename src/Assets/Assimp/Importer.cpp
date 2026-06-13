#include "Assets/Assimp/Importer.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {
    std::filesystem::path readTexturePath(const aiMaterial& material, aiTextureType type)
    {
        aiString texturePath;
        if (material.GetTexture(type, 0, &texturePath) != AI_SUCCESS || texturePath.length == 0) {
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
}
