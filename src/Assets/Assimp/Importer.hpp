#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Assets::Assimp {
    struct ImportedVertex {
        glm::vec3 position{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 tangent{1.0f, 0.0f, 0.0f};
        glm::vec2 uv{};
    };

    struct ImportedMaterial {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        std::filesystem::path baseColorTexture;
        std::filesystem::path normalTexture;
        std::filesystem::path metallicTexture;
        std::filesystem::path roughnessTexture;
    };

    struct ImportedSubmesh {
        std::vector<ImportedVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex = 0;
    };

    struct ImportedStaticMesh {
        std::vector<ImportedSubmesh> submeshes;
        std::vector<ImportedMaterial> materials;
    };

    struct ImportResult {
        bool success = false;
        std::string error;
        ImportedStaticMesh mesh;
    };

    struct ImportedSceneBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        bool valid = false;
    };

    enum class ImportedSceneSourceFormat {
        Unknown,
        Gltf,
        Glb,
        Fbx,
    };

    struct ImportedSceneVertexInfluence {
        uint32_t jointIndex = UINT32_MAX;
        float weight = 0.0f;
    };

    struct ImportedSceneVertex {
        glm::vec3 position{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
        glm::vec2 texcoord0{};
        glm::vec2 texcoord1{};
        glm::vec4 color0{1.0f};
        std::vector<ImportedSceneVertexInfluence> influences;
        bool hasNormal = false;
        bool hasTangent = false;
        bool hasTexcoord0 = false;
        bool hasTexcoord1 = false;
        bool hasColor0 = false;
    };

    struct ImportedScenePrimitive {
        std::vector<ImportedSceneVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex = 0;
        ImportedSceneBounds bounds;
        bool hasTexcoord1 = false;
        bool hasColor0 = false;
        bool missingNormals = false;
        bool missingTangents = false;
        bool missingTexcoord0 = false;
    };

    struct ImportedSceneMesh {
        std::string name;
        std::vector<ImportedScenePrimitive> primitives;
        ImportedSceneBounds bounds;
    };

    enum class ImportedSceneTextureSemantic {
        Unknown,
        BaseColor,
        Normal,
        Metallic,
        Roughness,
        MetallicRoughness,
        Occlusion,
        Emissive,
    };

    enum class ImportedSceneTextureColorSpace {
        Linear,
        Srgb,
    };

    enum class ImportedSceneTextureWrap {
        Unknown,
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    enum class ImportedSceneTextureFilter {
        Unknown,
        Nearest,
        Linear,
    };

    struct ImportedSceneTextureHints {
        ImportedSceneTextureColorSpace colorSpace = ImportedSceneTextureColorSpace::Linear;
        ImportedSceneTextureWrap wrapU = ImportedSceneTextureWrap::Repeat;
        ImportedSceneTextureWrap wrapV = ImportedSceneTextureWrap::Repeat;
        ImportedSceneTextureFilter minFilter = ImportedSceneTextureFilter::Linear;
        ImportedSceneTextureFilter magFilter = ImportedSceneTextureFilter::Linear;
    };

    struct ImportedSceneTexture {
        std::filesystem::path path;
        ImportedSceneTextureSemantic semantic = ImportedSceneTextureSemantic::Unknown;
        uint32_t materialIndex = UINT32_MAX;
        int texcoordIndex = 0;
        bool mayBePackedMetallicRoughness = false;
        ImportedSceneTextureHints hints;
    };

    enum class ImportedSceneAlphaMode {
        Opaque,
        Mask,
        Blend,
    };

    struct ImportedSceneMaterial {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        std::filesystem::path baseColorTexture;
        std::filesystem::path normalTexture;
        float normalScale = 1.0f;
        std::filesystem::path metallicTexture;
        std::filesystem::path roughnessTexture;
        std::filesystem::path metallicRoughnessTexture;
        bool hasPackedMetallicRoughnessTexture = false;
        std::filesystem::path occlusionTexture;
        float occlusionStrength = 1.0f;
        std::filesystem::path emissiveTexture;
        glm::vec3 emissiveFactor{0.0f};
        ImportedSceneTextureHints baseColorTextureHints{ImportedSceneTextureColorSpace::Srgb};
        ImportedSceneTextureHints normalTextureHints;
        ImportedSceneTextureHints metallicTextureHints;
        ImportedSceneTextureHints roughnessTextureHints;
        ImportedSceneTextureHints metallicRoughnessTextureHints;
        ImportedSceneTextureHints occlusionTextureHints;
        ImportedSceneTextureHints emissiveTextureHints{ImportedSceneTextureColorSpace::Srgb};
        ImportedSceneAlphaMode alphaMode = ImportedSceneAlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct ImportedSceneNode {
        std::string name;
        uint32_t parentIndex = UINT32_MAX;
        std::vector<uint32_t> childIndices;
        std::vector<uint32_t> meshIndices;
        glm::mat4 localTransform{1.0f};
        glm::mat4 worldTransform{1.0f};
    };

    enum class ImportedSceneLightType {
        Unknown,
        Directional,
        Point,
        Spot,
        Ambient,
        Area,
    };

    struct ImportedSceneLight {
        std::string name;
        ImportedSceneLightType type = ImportedSceneLightType::Unknown;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        float range = 0.0f;
        float innerConeAngle = 0.0f;
        float outerConeAngle = 0.0f;
        std::optional<uint32_t> nodeIndex;
    };

    struct ImportedSceneJoint {
        std::string name;
        std::optional<uint32_t> nodeIndex;
        uint32_t parentJointIndex = UINT32_MAX;
        glm::mat4 inverseBindMatrix{1.0f};
        bool hasInverseBindMatrix = false;
        glm::mat4 localBindTransform{1.0f};
        glm::mat4 worldBindTransform{1.0f};
    };

    struct ImportedSceneSkin {
        std::string name;
        std::optional<uint32_t> skeletonRootNodeIndex;
        std::vector<uint32_t> jointIndices;
        std::vector<uint32_t> meshIndices;
        std::vector<uint32_t> nodeIndices;
        uint32_t inverseBindMatrixCount = 0;
    };

    enum class ImportedSceneAnimationInterpolation {
        Unknown,
        Step,
        Linear,
        CubicSpline,
    };

    struct ImportedSceneVec3Key {
        float timeSeconds = 0.0f;
        glm::vec3 value{0.0f};
        ImportedSceneAnimationInterpolation interpolation = ImportedSceneAnimationInterpolation::Linear;
    };

    struct ImportedSceneQuatKey {
        float timeSeconds = 0.0f;
        glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
        ImportedSceneAnimationInterpolation interpolation = ImportedSceneAnimationInterpolation::Linear;
    };

    struct ImportedSceneAnimationChannel {
        std::string targetName;
        std::optional<uint32_t> targetNodeIndex;
        std::vector<ImportedSceneVec3Key> translationKeys;
        std::vector<ImportedSceneQuatKey> rotationKeys;
        std::vector<ImportedSceneVec3Key> scaleKeys;
    };

    struct ImportedSceneAnimationClip {
        std::string name;
        float durationSeconds = 0.0f;
        double ticksPerSecond = 1.0;
        std::vector<ImportedSceneAnimationChannel> channels;
    };

    struct ImportedSceneDiagnostics {
        ImportedSceneSourceFormat sourceFormat = ImportedSceneSourceFormat::Unknown;
        uint32_t nodeCount = 0;
        uint32_t meshNodeCount = 0;
        uint32_t meshCount = 0;
        uint32_t primitiveCount = 0;
        uint32_t materialCount = 0;
        uint32_t textureCount = 0;
        uint32_t lightCount = 0;
        uint32_t alphaMaterialCount = 0;
        uint32_t doubleSidedMaterialCount = 0;
        uint32_t texcoord1PrimitiveCount = 0;
        uint32_t vertexColorPrimitiveCount = 0;
        uint32_t missingNormalPrimitiveCount = 0;
        uint32_t missingTangentPrimitiveCount = 0;
        uint32_t missingTexcoord0PrimitiveCount = 0;
        uint32_t skinCount = 0;
        uint32_t jointCount = 0;
        uint32_t skinnedMeshCount = 0;
        uint32_t influencedVertexCount = 0;
        uint32_t maxInfluencesPerVertex = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t nonNormalizedWeightVertexCount = 0;
        uint32_t overFourInfluenceVertexCount = 0;
        uint32_t animationCount = 0;
        uint32_t animationChannelCount = 0;
        uint32_t translationKeyCount = 0;
        uint32_t rotationKeyCount = 0;
        uint32_t scaleKeyCount = 0;
        uint32_t missingAnimationTargetCount = 0;
        uint32_t embeddedTextureCount = 0;
        uint32_t missingPbrMaterialCount = 0;
        float fbxUnitScaleFactor = 1.0f;
        int32_t fbxUpAxis = 1;
        int32_t fbxUpAxisSign = 1;
        uint32_t fbxAnimationStackCount = 0;
        uint32_t fbxUnnamedAnimationStackCount = 0;
        uint32_t fbxAnimationSemanticWarningCount = 0;
        std::vector<std::string> warnings;
    };

    struct ImportedScene {
        bool success = false;
        std::string error;
        ImportedSceneSourceFormat sourceFormat = ImportedSceneSourceFormat::Unknown;
        uint32_t rootNodeIndex = UINT32_MAX;
        std::vector<ImportedSceneNode> nodes;
        std::vector<ImportedSceneMesh> meshes;
        std::vector<ImportedSceneMaterial> materials;
        std::vector<ImportedSceneTexture> textures;
        std::vector<ImportedSceneLight> lights;
        std::vector<ImportedSceneJoint> joints;
        std::vector<ImportedSceneSkin> skins;
        std::vector<ImportedSceneAnimationClip> animations;
        ImportedSceneBounds bounds;
        ImportedSceneDiagnostics diagnostics;
    };

    ImportedSceneSourceFormat detectSceneSourceFormat(const std::filesystem::path& path);
    const char* sourceFormatName(ImportedSceneSourceFormat format);

    // True when imported CPU data requires the animated model runtime instead of
    // the static authored scene path.
    bool containsSkeletalOrAnimationData(const ImportedScene& scene);

    // Legacy simple-mesh import for static renderer assets. This path remains
    // intentionally narrower than importScene and rejects animated/skinned data.
    ImportResult importStaticMesh(const std::filesystem::path& path);

    // Full CPU authored-scene import. This creates no renderer/bgfx resources
    // and preserves static, skeletal, material, texture, light, and animation records.
    ImportedScene importScene(const std::filesystem::path& path);
}
