#include "Engine/AnimatedModelCache.hpp"

#include <fstream>
#include <sstream>

#include <glm/gtc/quaternion.hpp>
#include <yaml-cpp/yaml.h>

namespace {
    uint64_t fnv1a(const std::string& text)
    {
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char character : text) {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string hexHash(uint64_t value)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string output(16, '0');
        for (int index = 15; index >= 0; --index) {
            output[index] = digits[value & 0x0f];
            value >>= 4;
        }
        return output;
    }

    std::filesystem::path canonicalForIdentity(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
        return error ? path.lexically_normal() : canonical;
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

    YAML::Node quatNode(const glm::quat& value)
    {
        YAML::Node node;
        node.push_back(value.w);
        node.push_back(value.x);
        node.push_back(value.y);
        node.push_back(value.z);
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

    glm::quat readQuat(const YAML::Node& node, glm::quat fallback = {1.0f, 0.0f, 0.0f, 0.0f})
    {
        if (!node || !node.IsSequence() || node.size() < 4) {
            return fallback;
        }
        return {
            node[0].as<float>(fallback.w),
            node[1].as<float>(fallback.x),
            node[2].as<float>(fallback.y),
            node[3].as<float>(fallback.z),
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
        node["min"] = vec3Node(bounds.min);
        node["max"] = vec3Node(bounds.max);
        node["valid"] = bounds.valid;
        return node;
    }

    Assets::Assimp::ImportedSceneBounds readBounds(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneBounds bounds;
        bounds.min = readVec3(node["min"]);
        bounds.max = readVec3(node["max"]);
        bounds.valid = node["valid"].as<bool>(false);
        return bounds;
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

    YAML::Node optionalUintNode(const std::optional<uint32_t>& value)
    {
        YAML::Node node;
        if (value) {
            node = *value;
        }
        return node;
    }

    std::optional<uint32_t> readOptionalUint(const YAML::Node& node)
    {
        if (!node || node.IsNull()) {
            return std::nullopt;
        }
        return node.as<uint32_t>(UINT32_MAX);
    }

    template <typename Enum>
    uint32_t enumValue(Enum value)
    {
        return static_cast<uint32_t>(value);
    }

    Assets::Assimp::ImportedSceneSourceFormat readSourceFormat(const YAML::Node& node)
    {
        const std::string value = node.as<std::string>(std::string{});
        if (value == "Gltf" || value == "gltf") {
            return Assets::Assimp::ImportedSceneSourceFormat::Gltf;
        }
        if (value == "Glb" || value == "glb") {
            return Assets::Assimp::ImportedSceneSourceFormat::Glb;
        }
        if (value == "Fbx" || value == "fbx") {
            return Assets::Assimp::ImportedSceneSourceFormat::Fbx;
        }
        return Assets::Assimp::ImportedSceneSourceFormat::Unknown;
    }

    YAML::Node textureHintsNode(const Assets::Assimp::ImportedSceneTextureHints& hints)
    {
        YAML::Node node;
        node["color_space"] = enumValue(hints.colorSpace);
        node["wrap_u"] = enumValue(hints.wrapU);
        node["wrap_v"] = enumValue(hints.wrapV);
        node["min_filter"] = enumValue(hints.minFilter);
        node["mag_filter"] = enumValue(hints.magFilter);
        return node;
    }

    Assets::Assimp::ImportedSceneTextureHints readTextureHints(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneTextureHints hints;
        hints.colorSpace = static_cast<Assets::Assimp::ImportedSceneTextureColorSpace>(
            node["color_space"].as<uint32_t>(enumValue(hints.colorSpace)));
        hints.wrapU = static_cast<Assets::Assimp::ImportedSceneTextureWrap>(
            node["wrap_u"].as<uint32_t>(enumValue(hints.wrapU)));
        hints.wrapV = static_cast<Assets::Assimp::ImportedSceneTextureWrap>(
            node["wrap_v"].as<uint32_t>(enumValue(hints.wrapV)));
        hints.minFilter = static_cast<Assets::Assimp::ImportedSceneTextureFilter>(
            node["min_filter"].as<uint32_t>(enumValue(hints.minFilter)));
        hints.magFilter = static_cast<Assets::Assimp::ImportedSceneTextureFilter>(
            node["mag_filter"].as<uint32_t>(enumValue(hints.magFilter)));
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
        node["packed_metallic_roughness"] = material.hasPackedMetallicRoughnessTexture;
        node["occlusion_texture"] = material.occlusionTexture.generic_string();
        node["occlusion_strength"] = material.occlusionStrength;
        node["emissive_texture"] = material.emissiveTexture.generic_string();
        node["emissive_factor"] = vec3Node(material.emissiveFactor);
        node["alpha_mode"] = enumValue(material.alphaMode);
        node["alpha_cutoff"] = material.alphaCutoff;
        node["double_sided"] = material.doubleSided;
        node["base_color_hints"] = textureHintsNode(material.baseColorTextureHints);
        node["normal_hints"] = textureHintsNode(material.normalTextureHints);
        node["metallic_hints"] = textureHintsNode(material.metallicTextureHints);
        node["roughness_hints"] = textureHintsNode(material.roughnessTextureHints);
        node["metallic_roughness_hints"] = textureHintsNode(material.metallicRoughnessTextureHints);
        node["occlusion_hints"] = textureHintsNode(material.occlusionTextureHints);
        node["emissive_hints"] = textureHintsNode(material.emissiveTextureHints);
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
        material.hasPackedMetallicRoughnessTexture = node["packed_metallic_roughness"].as<bool>(false);
        material.occlusionTexture = node["occlusion_texture"].as<std::string>(std::string{});
        material.occlusionStrength = node["occlusion_strength"].as<float>(1.0f);
        material.emissiveTexture = node["emissive_texture"].as<std::string>(std::string{});
        material.emissiveFactor = readVec3(node["emissive_factor"]);
        material.alphaMode = static_cast<Assets::Assimp::ImportedSceneAlphaMode>(node["alpha_mode"].as<uint32_t>(0));
        material.alphaCutoff = node["alpha_cutoff"].as<float>(0.5f);
        material.doubleSided = node["double_sided"].as<bool>(false);
        material.baseColorTextureHints = readTextureHints(node["base_color_hints"]);
        material.normalTextureHints = readTextureHints(node["normal_hints"]);
        material.metallicTextureHints = readTextureHints(node["metallic_hints"]);
        material.roughnessTextureHints = readTextureHints(node["roughness_hints"]);
        material.metallicRoughnessTextureHints = readTextureHints(node["metallic_roughness_hints"]);
        material.occlusionTextureHints = readTextureHints(node["occlusion_hints"]);
        material.emissiveTextureHints = readTextureHints(node["emissive_hints"]);
        return material;
    }

    YAML::Node textureNode(const Assets::Assimp::ImportedSceneTexture& texture)
    {
        YAML::Node node;
        node["path"] = texture.path.generic_string();
        node["semantic"] = enumValue(texture.semantic);
        node["material_index"] = texture.materialIndex;
        node["texcoord_index"] = texture.texcoordIndex;
        node["packed_mr"] = texture.mayBePackedMetallicRoughness;
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
        texture.mayBePackedMetallicRoughness = node["packed_mr"].as<bool>(false);
        texture.hints = readTextureHints(node["hints"]);
        return texture;
    }

    YAML::Node diagnosticsNode(const Assets::Assimp::ImportedSceneDiagnostics& diagnostics)
    {
        YAML::Node node;
        node["source_format"] = Assets::Assimp::sourceFormatName(diagnostics.sourceFormat);
        node["nodes"] = diagnostics.nodeCount;
        node["mesh_nodes"] = diagnostics.meshNodeCount;
        node["meshes"] = diagnostics.meshCount;
        node["primitives"] = diagnostics.primitiveCount;
        node["materials"] = diagnostics.materialCount;
        node["textures"] = diagnostics.textureCount;
        node["lights"] = diagnostics.lightCount;
        node["skins"] = diagnostics.skinCount;
        node["joints"] = diagnostics.jointCount;
        node["skinned_meshes"] = diagnostics.skinnedMeshCount;
        node["influenced_vertices"] = diagnostics.influencedVertexCount;
        node["max_influences"] = diagnostics.maxInfluencesPerVertex;
        node["zero_weight_vertices"] = diagnostics.zeroWeightVertexCount;
        node["non_normalized_weights"] = diagnostics.nonNormalizedWeightVertexCount;
        node["over_four_influences"] = diagnostics.overFourInfluenceVertexCount;
        node["animations"] = diagnostics.animationCount;
        node["animation_channels"] = diagnostics.animationChannelCount;
        node["translation_keys"] = diagnostics.translationKeyCount;
        node["rotation_keys"] = diagnostics.rotationKeyCount;
        node["scale_keys"] = diagnostics.scaleKeyCount;
        node["missing_animation_targets"] = diagnostics.missingAnimationTargetCount;
        node["embedded_textures"] = diagnostics.embeddedTextureCount;
        node["missing_pbr_materials"] = diagnostics.missingPbrMaterialCount;
        node["fbx_unit_scale"] = diagnostics.fbxUnitScaleFactor;
        node["fbx_up_axis"] = diagnostics.fbxUpAxis;
        node["fbx_up_axis_sign"] = diagnostics.fbxUpAxisSign;
        node["fbx_animation_stacks"] = diagnostics.fbxAnimationStackCount;
        node["fbx_unnamed_animation_stacks"] = diagnostics.fbxUnnamedAnimationStackCount;
        node["fbx_animation_semantic_warnings"] = diagnostics.fbxAnimationSemanticWarningCount;
        YAML::Node warnings;
        for (const std::string& warning : diagnostics.warnings) {
            warnings.push_back(warning);
        }
        node["warnings"] = warnings;
        return node;
    }

    Assets::Assimp::ImportedSceneDiagnostics readDiagnostics(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneDiagnostics diagnostics;
        diagnostics.sourceFormat = readSourceFormat(node["source_format"]);
        diagnostics.nodeCount = node["nodes"].as<uint32_t>(0);
        diagnostics.meshNodeCount = node["mesh_nodes"].as<uint32_t>(0);
        diagnostics.meshCount = node["meshes"].as<uint32_t>(0);
        diagnostics.primitiveCount = node["primitives"].as<uint32_t>(0);
        diagnostics.materialCount = node["materials"].as<uint32_t>(0);
        diagnostics.textureCount = node["textures"].as<uint32_t>(0);
        diagnostics.lightCount = node["lights"].as<uint32_t>(0);
        diagnostics.skinCount = node["skins"].as<uint32_t>(0);
        diagnostics.jointCount = node["joints"].as<uint32_t>(0);
        diagnostics.skinnedMeshCount = node["skinned_meshes"].as<uint32_t>(0);
        diagnostics.influencedVertexCount = node["influenced_vertices"].as<uint32_t>(0);
        diagnostics.maxInfluencesPerVertex = node["max_influences"].as<uint32_t>(0);
        diagnostics.zeroWeightVertexCount = node["zero_weight_vertices"].as<uint32_t>(0);
        diagnostics.nonNormalizedWeightVertexCount = node["non_normalized_weights"].as<uint32_t>(0);
        diagnostics.overFourInfluenceVertexCount = node["over_four_influences"].as<uint32_t>(0);
        diagnostics.animationCount = node["animations"].as<uint32_t>(0);
        diagnostics.animationChannelCount = node["animation_channels"].as<uint32_t>(0);
        diagnostics.translationKeyCount = node["translation_keys"].as<uint32_t>(0);
        diagnostics.rotationKeyCount = node["rotation_keys"].as<uint32_t>(0);
        diagnostics.scaleKeyCount = node["scale_keys"].as<uint32_t>(0);
        diagnostics.missingAnimationTargetCount = node["missing_animation_targets"].as<uint32_t>(0);
        diagnostics.embeddedTextureCount = node["embedded_textures"].as<uint32_t>(0);
        diagnostics.missingPbrMaterialCount = node["missing_pbr_materials"].as<uint32_t>(0);
        diagnostics.fbxUnitScaleFactor = node["fbx_unit_scale"].as<float>(1.0f);
        diagnostics.fbxUpAxis = node["fbx_up_axis"].as<int32_t>(1);
        diagnostics.fbxUpAxisSign = node["fbx_up_axis_sign"].as<int32_t>(1);
        diagnostics.fbxAnimationStackCount = node["fbx_animation_stacks"].as<uint32_t>(0);
        diagnostics.fbxUnnamedAnimationStackCount = node["fbx_unnamed_animation_stacks"].as<uint32_t>(0);
        diagnostics.fbxAnimationSemanticWarningCount = node["fbx_animation_semantic_warnings"].as<uint32_t>(0);
        if (const YAML::Node warnings = node["warnings"]; warnings && warnings.IsSequence()) {
            for (const YAML::Node& warning : warnings) {
                diagnostics.warnings.push_back(warning.as<std::string>(std::string{}));
            }
        }
        return diagnostics;
    }

    YAML::Node vertexNode(const Assets::Assimp::ImportedSceneVertex& vertex)
    {
        YAML::Node node;
        node["position"] = vec3Node(vertex.position);
        node["normal"] = vec3Node(vertex.normal);
        node["tangent"] = vec4Node(vertex.tangent);
        node["texcoord0"] = vec2Node(vertex.texcoord0);
        node["texcoord1"] = vec2Node(vertex.texcoord1);
        node["color0"] = vec4Node(vertex.color0);
        node["has_normal"] = vertex.hasNormal;
        node["has_tangent"] = vertex.hasTangent;
        node["has_texcoord0"] = vertex.hasTexcoord0;
        node["has_texcoord1"] = vertex.hasTexcoord1;
        node["has_color0"] = vertex.hasColor0;
        YAML::Node influences;
        for (const Assets::Assimp::ImportedSceneVertexInfluence& influence : vertex.influences) {
            YAML::Node item;
            item["joint"] = influence.jointIndex;
            item["weight"] = influence.weight;
            influences.push_back(item);
        }
        node["influences"] = influences;
        return node;
    }

    Assets::Assimp::ImportedSceneVertex readVertex(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneVertex vertex;
        vertex.position = readVec3(node["position"]);
        vertex.normal = readVec3(node["normal"], {0.0f, 1.0f, 0.0f});
        vertex.tangent = readVec4(node["tangent"], {1.0f, 0.0f, 0.0f, 1.0f});
        vertex.texcoord0 = readVec2(node["texcoord0"]);
        vertex.texcoord1 = readVec2(node["texcoord1"]);
        vertex.color0 = readVec4(node["color0"], glm::vec4{1.0f});
        vertex.hasNormal = node["has_normal"].as<bool>(false);
        vertex.hasTangent = node["has_tangent"].as<bool>(false);
        vertex.hasTexcoord0 = node["has_texcoord0"].as<bool>(false);
        vertex.hasTexcoord1 = node["has_texcoord1"].as<bool>(false);
        vertex.hasColor0 = node["has_color0"].as<bool>(false);
        if (const YAML::Node influences = node["influences"]; influences && influences.IsSequence()) {
            vertex.influences.reserve(influences.size());
            for (const YAML::Node& item : influences) {
                vertex.influences.push_back({
                    item["joint"].as<uint32_t>(UINT32_MAX),
                    item["weight"].as<float>(0.0f),
                });
            }
        }
        return vertex;
    }

    YAML::Node primitiveNode(const Assets::Assimp::ImportedScenePrimitive& primitive)
    {
        YAML::Node node;
        node["material"] = primitive.materialIndex;
        node["bounds"] = boundsNode(primitive.bounds);
        node["has_texcoord1"] = primitive.hasTexcoord1;
        node["has_color0"] = primitive.hasColor0;
        node["missing_normals"] = primitive.missingNormals;
        node["missing_tangents"] = primitive.missingTangents;
        node["missing_texcoord0"] = primitive.missingTexcoord0;
        YAML::Node vertices;
        for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
            vertices.push_back(vertexNode(vertex));
        }
        node["vertices"] = vertices;
        node["indices"] = uintVectorNode(primitive.indices);
        return node;
    }

    Assets::Assimp::ImportedScenePrimitive readPrimitive(const YAML::Node& node)
    {
        Assets::Assimp::ImportedScenePrimitive primitive;
        primitive.materialIndex = node["material"].as<uint32_t>(0);
        primitive.bounds = readBounds(node["bounds"]);
        primitive.hasTexcoord1 = node["has_texcoord1"].as<bool>(false);
        primitive.hasColor0 = node["has_color0"].as<bool>(false);
        primitive.missingNormals = node["missing_normals"].as<bool>(false);
        primitive.missingTangents = node["missing_tangents"].as<bool>(false);
        primitive.missingTexcoord0 = node["missing_texcoord0"].as<bool>(false);
        if (const YAML::Node vertices = node["vertices"]; vertices && vertices.IsSequence()) {
            primitive.vertices.reserve(vertices.size());
            for (const YAML::Node& item : vertices) {
                primitive.vertices.push_back(readVertex(item));
            }
        }
        primitive.indices = readUintVector(node["indices"]);
        return primitive;
    }

    YAML::Node nodeNode(const Assets::Assimp::ImportedSceneNode& sceneNode)
    {
        YAML::Node node;
        node["name"] = sceneNode.name;
        node["parent"] = sceneNode.parentIndex;
        node["children"] = uintVectorNode(sceneNode.childIndices);
        node["meshes"] = uintVectorNode(sceneNode.meshIndices);
        node["local_transform"] = mat4Node(sceneNode.localTransform);
        node["world_transform"] = mat4Node(sceneNode.worldTransform);
        return node;
    }

    Assets::Assimp::ImportedSceneNode readNode(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneNode sceneNode;
        sceneNode.name = node["name"].as<std::string>(std::string{});
        sceneNode.parentIndex = node["parent"].as<uint32_t>(UINT32_MAX);
        sceneNode.childIndices = readUintVector(node["children"]);
        sceneNode.meshIndices = readUintVector(node["meshes"]);
        sceneNode.localTransform = readMat4(node["local_transform"]);
        sceneNode.worldTransform = readMat4(node["world_transform"]);
        return sceneNode;
    }

    YAML::Node jointNode(const Assets::Assimp::ImportedSceneJoint& joint)
    {
        YAML::Node node;
        node["name"] = joint.name;
        node["node"] = optionalUintNode(joint.nodeIndex);
        node["parent_joint"] = joint.parentJointIndex;
        node["inverse_bind"] = mat4Node(joint.inverseBindMatrix);
        node["has_inverse_bind"] = joint.hasInverseBindMatrix;
        node["local_bind"] = mat4Node(joint.localBindTransform);
        node["world_bind"] = mat4Node(joint.worldBindTransform);
        return node;
    }

    Assets::Assimp::ImportedSceneJoint readJoint(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneJoint joint;
        joint.name = node["name"].as<std::string>(std::string{});
        joint.nodeIndex = readOptionalUint(node["node"]);
        joint.parentJointIndex = node["parent_joint"].as<uint32_t>(UINT32_MAX);
        joint.inverseBindMatrix = readMat4(node["inverse_bind"]);
        joint.hasInverseBindMatrix = node["has_inverse_bind"].as<bool>(false);
        joint.localBindTransform = readMat4(node["local_bind"]);
        joint.worldBindTransform = readMat4(node["world_bind"]);
        return joint;
    }

    YAML::Node skinNode(const Assets::Assimp::ImportedSceneSkin& skin)
    {
        YAML::Node node;
        node["name"] = skin.name;
        node["skeleton_root"] = optionalUintNode(skin.skeletonRootNodeIndex);
        node["joints"] = uintVectorNode(skin.jointIndices);
        node["meshes"] = uintVectorNode(skin.meshIndices);
        node["nodes"] = uintVectorNode(skin.nodeIndices);
        node["inverse_bind_count"] = skin.inverseBindMatrixCount;
        return node;
    }

    Assets::Assimp::ImportedSceneSkin readSkin(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneSkin skin;
        skin.name = node["name"].as<std::string>(std::string{});
        skin.skeletonRootNodeIndex = readOptionalUint(node["skeleton_root"]);
        skin.jointIndices = readUintVector(node["joints"]);
        skin.meshIndices = readUintVector(node["meshes"]);
        skin.nodeIndices = readUintVector(node["nodes"]);
        skin.inverseBindMatrixCount = node["inverse_bind_count"].as<uint32_t>(0);
        return skin;
    }

    YAML::Node vec3KeyNode(const Assets::Assimp::ImportedSceneVec3Key& key)
    {
        YAML::Node node;
        node["time"] = key.timeSeconds;
        node["value"] = vec3Node(key.value);
        node["interpolation"] = enumValue(key.interpolation);
        return node;
    }

    Assets::Assimp::ImportedSceneVec3Key readVec3Key(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneVec3Key key;
        key.timeSeconds = node["time"].as<float>(0.0f);
        key.value = readVec3(node["value"]);
        key.interpolation = static_cast<Assets::Assimp::ImportedSceneAnimationInterpolation>(
            node["interpolation"].as<uint32_t>(enumValue(key.interpolation)));
        return key;
    }

    YAML::Node quatKeyNode(const Assets::Assimp::ImportedSceneQuatKey& key)
    {
        YAML::Node node;
        node["time"] = key.timeSeconds;
        node["value"] = quatNode(key.value);
        node["interpolation"] = enumValue(key.interpolation);
        return node;
    }

    Assets::Assimp::ImportedSceneQuatKey readQuatKey(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneQuatKey key;
        key.timeSeconds = node["time"].as<float>(0.0f);
        key.value = readQuat(node["value"]);
        key.interpolation = static_cast<Assets::Assimp::ImportedSceneAnimationInterpolation>(
            node["interpolation"].as<uint32_t>(enumValue(key.interpolation)));
        return key;
    }

    YAML::Node channelNode(const Assets::Assimp::ImportedSceneAnimationChannel& channel)
    {
        YAML::Node node;
        node["target_name"] = channel.targetName;
        node["target_node"] = optionalUintNode(channel.targetNodeIndex);
        for (const auto& key : channel.translationKeys) {
            node["translations"].push_back(vec3KeyNode(key));
        }
        for (const auto& key : channel.rotationKeys) {
            node["rotations"].push_back(quatKeyNode(key));
        }
        for (const auto& key : channel.scaleKeys) {
            node["scales"].push_back(vec3KeyNode(key));
        }
        return node;
    }

    Assets::Assimp::ImportedSceneAnimationChannel readChannel(const YAML::Node& node)
    {
        Assets::Assimp::ImportedSceneAnimationChannel channel;
        channel.targetName = node["target_name"].as<std::string>(std::string{});
        channel.targetNodeIndex = readOptionalUint(node["target_node"]);
        if (const YAML::Node keys = node["translations"]; keys && keys.IsSequence()) {
            for (const YAML::Node& key : keys) {
                channel.translationKeys.push_back(readVec3Key(key));
            }
        }
        if (const YAML::Node keys = node["rotations"]; keys && keys.IsSequence()) {
            for (const YAML::Node& key : keys) {
                channel.rotationKeys.push_back(readQuatKey(key));
            }
        }
        if (const YAML::Node keys = node["scales"]; keys && keys.IsSequence()) {
            for (const YAML::Node& key : keys) {
                channel.scaleKeys.push_back(readVec3Key(key));
            }
        }
        return channel;
    }
}

namespace Engine {
    const char* animatedModelCacheStatusName(AnimatedModelCacheStatus status)
    {
        switch (status) {
            case AnimatedModelCacheStatus::Hit:
                return "hit";
            case AnimatedModelCacheStatus::Stale:
                return "stale";
            case AnimatedModelCacheStatus::Corrupt:
                return "corrupt";
            case AnimatedModelCacheStatus::WriteSuccess:
                return "write_success";
            case AnimatedModelCacheStatus::WriteFailed:
                return "write_failed";
            case AnimatedModelCacheStatus::Cancelled:
                return "cancelled";
            case AnimatedModelCacheStatus::Miss:
            default:
                return "miss";
        }
    }

    AnimatedModelCacheManifest AnimatedModelCache::buildManifest(
        AnimatedModelCacheSettings settings,
        const std::filesystem::path& sourcePath)
    {
        AnimatedModelCacheManifest manifest;
        manifest.settings = std::move(settings);
        manifest.sourcePath = canonicalForIdentity(sourcePath);
        manifest.sourceHash = hashFile(manifest.sourcePath);
        manifest.sourceFormat = Assets::Assimp::detectSceneSourceFormat(manifest.sourcePath);

        std::ostringstream identity;
        identity << manifest.sourcePath.generic_string() << '|'
                 << manifest.sourceHash << '|'
                 << Assets::Assimp::sourceFormatName(manifest.sourceFormat) << '|'
                 << manifest.settings.formatVersion << '|'
                 << manifest.settings.importerVersion << '|'
                 << manifest.settings.materialPipelineVersion << '|'
                 << manifest.settings.texturePolicyVersion << '|'
                 << manifest.settings.staticVertexFormatVersion << '|'
                 << manifest.settings.skinnedVertexFormatVersion << '|'
                 << manifest.settings.animationPipelineVersion;
        manifest.identityHash = hexHash(fnv1a(identity.str()));
        return manifest;
    }

    std::string AnimatedModelCache::hashFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream stream;
        stream << file.rdbuf();
        return hexHash(fnv1a(stream.str()));
    }

    std::filesystem::path AnimatedModelCache::cacheRoot(const AnimatedModelCacheManifest& manifest)
    {
        return manifest.settings.rootPath / manifest.identityHash;
    }

    AnimatedModelCacheReadResult AnimatedModelCache::read(const AnimatedModelCacheManifest& manifest)
    {
        AnimatedModelCacheReadResult result;
        const std::filesystem::path rootPath = cacheRoot(manifest);
        const std::filesystem::path manifestPath = rootPath / "manifest.yaml";
        const std::filesystem::path payloadPath = rootPath / "model.yaml";
        result.path = manifestPath;
        if (!std::filesystem::exists(manifestPath) || !std::filesystem::exists(payloadPath)) {
            result.status = AnimatedModelCacheStatus::Miss;
            result.message = "Animated model cache miss.";
            return result;
        }

        try {
            const YAML::Node manifestRoot = YAML::LoadFile(manifestPath.string());
            if (manifestRoot["identity_hash"].as<std::string>(std::string{}) != manifest.identityHash ||
                manifestRoot["source_hash"].as<std::string>(std::string{}) != manifest.sourceHash ||
                manifestRoot["format_version"].as<uint32_t>(0) != manifest.settings.formatVersion) {
                result.status = AnimatedModelCacheStatus::Stale;
                result.message = "Animated model cache identity mismatch.";
                return result;
            }

            const YAML::Node root = YAML::LoadFile(payloadPath.string());
            AnimatedModelCachePayload payload;
            payload.scene.success = true;
            payload.scene.sourceFormat = readSourceFormat(root["source_format"]);
            payload.scene.rootNodeIndex = root["root_node_index"].as<uint32_t>(UINT32_MAX);
            payload.scene.bounds = readBounds(root["bounds"]);
            payload.scene.diagnostics = readDiagnostics(root["diagnostics"]);
            if (payload.scene.diagnostics.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Unknown) {
                payload.scene.diagnostics.sourceFormat = payload.scene.sourceFormat;
            }

            if (const YAML::Node nodes = root["nodes"]; nodes && nodes.IsSequence()) {
                for (const YAML::Node& item : nodes) {
                    payload.scene.nodes.push_back(readNode(item));
                }
            }
            if (const YAML::Node meshes = root["meshes"]; meshes && meshes.IsSequence()) {
                for (const YAML::Node& item : meshes) {
                    Assets::Assimp::ImportedSceneMesh mesh;
                    mesh.name = item["name"].as<std::string>(std::string{});
                    mesh.bounds = readBounds(item["bounds"]);
                    if (const YAML::Node primitives = item["primitives"]; primitives && primitives.IsSequence()) {
                        for (const YAML::Node& primitive : primitives) {
                            mesh.primitives.push_back(readPrimitive(primitive));
                        }
                    }
                    payload.scene.meshes.push_back(std::move(mesh));
                }
            }
            if (const YAML::Node materials = root["materials"]; materials && materials.IsSequence()) {
                for (const YAML::Node& item : materials) {
                    payload.scene.materials.push_back(readMaterial(item));
                }
            }
            if (const YAML::Node textures = root["textures"]; textures && textures.IsSequence()) {
                for (const YAML::Node& item : textures) {
                    payload.scene.textures.push_back(readTexture(item));
                }
            }
            if (const YAML::Node joints = root["joints"]; joints && joints.IsSequence()) {
                for (const YAML::Node& item : joints) {
                    payload.scene.joints.push_back(readJoint(item));
                }
            }
            if (const YAML::Node skins = root["skins"]; skins && skins.IsSequence()) {
                for (const YAML::Node& item : skins) {
                    payload.scene.skins.push_back(readSkin(item));
                }
            }
            if (const YAML::Node clips = root["animations"]; clips && clips.IsSequence()) {
                for (const YAML::Node& item : clips) {
                    Assets::Assimp::ImportedSceneAnimationClip clip;
                    clip.name = item["name"].as<std::string>(std::string{});
                    clip.durationSeconds = item["duration"].as<float>(0.0f);
                    clip.ticksPerSecond = item["ticks_per_second"].as<double>(1.0);
                    if (const YAML::Node channels = item["channels"]; channels && channels.IsSequence()) {
                        for (const YAML::Node& channel : channels) {
                            clip.channels.push_back(readChannel(channel));
                        }
                    }
                    payload.scene.animations.push_back(std::move(clip));
                }
            }

            result.status = AnimatedModelCacheStatus::Hit;
            result.message = "Loaded animated model cache.";
            result.payload = std::move(payload);
        } catch (const YAML::BadFile&) {
            result.status = AnimatedModelCacheStatus::Miss;
            result.message = "Animated model cache file is missing.";
        } catch (const std::exception& exception) {
            result.status = AnimatedModelCacheStatus::Corrupt;
            result.message = exception.what();
        }
        return result;
    }

    AnimatedModelCacheWriteResult AnimatedModelCache::write(
        const AnimatedModelCacheManifest& manifest,
        const AnimatedModelCachePayload& payload)
    {
        AnimatedModelCacheWriteResult result;
        const std::filesystem::path rootPath = cacheRoot(manifest);
        result.path = rootPath / "manifest.yaml";
        if (payload.scene.joints.empty() && payload.scene.skins.empty() && payload.scene.animations.empty()) {
            result.status = AnimatedModelCacheStatus::WriteFailed;
            result.message = "Animated model cache write skipped for non-animated payload.";
            return result;
        }

        try {
            std::filesystem::create_directories(rootPath);

            YAML::Node manifestRoot;
            manifestRoot["format_version"] = manifest.settings.formatVersion;
            manifestRoot["identity_hash"] = manifest.identityHash;
            manifestRoot["source_path"] = manifest.sourcePath.generic_string();
            manifestRoot["source_hash"] = manifest.sourceHash;
            manifestRoot["source_format"] = Assets::Assimp::sourceFormatName(manifest.sourceFormat);
            manifestRoot["importer_version"] = manifest.settings.importerVersion;
            manifestRoot["material_pipeline_version"] = manifest.settings.materialPipelineVersion;
            manifestRoot["texture_policy_version"] = manifest.settings.texturePolicyVersion;
            manifestRoot["static_vertex_format_version"] = manifest.settings.staticVertexFormatVersion;
            manifestRoot["skinned_vertex_format_version"] = manifest.settings.skinnedVertexFormatVersion;
            manifestRoot["animation_pipeline_version"] = manifest.settings.animationPipelineVersion;
            manifestRoot["node_count"] = static_cast<uint32_t>(payload.scene.nodes.size());
            manifestRoot["mesh_count"] = static_cast<uint32_t>(payload.scene.meshes.size());
            manifestRoot["material_count"] = static_cast<uint32_t>(payload.scene.materials.size());
            manifestRoot["texture_count"] = static_cast<uint32_t>(payload.scene.textures.size());
            manifestRoot["joint_count"] = static_cast<uint32_t>(payload.scene.joints.size());
            manifestRoot["skin_count"] = static_cast<uint32_t>(payload.scene.skins.size());
            manifestRoot["animation_count"] = static_cast<uint32_t>(payload.scene.animations.size());
            manifestRoot["payload"] = "model.yaml";

            YAML::Node root;
            root["source_format"] = Assets::Assimp::sourceFormatName(payload.scene.sourceFormat);
            root["root_node_index"] = payload.scene.rootNodeIndex;
            root["bounds"] = boundsNode(payload.scene.bounds);
            root["diagnostics"] = diagnosticsNode(payload.scene.diagnostics);
            for (const Assets::Assimp::ImportedSceneNode& sceneNode : payload.scene.nodes) {
                root["nodes"].push_back(nodeNode(sceneNode));
            }
            for (const Assets::Assimp::ImportedSceneMesh& mesh : payload.scene.meshes) {
                YAML::Node item;
                item["name"] = mesh.name;
                item["bounds"] = boundsNode(mesh.bounds);
                for (const Assets::Assimp::ImportedScenePrimitive& primitive : mesh.primitives) {
                    item["primitives"].push_back(primitiveNode(primitive));
                }
                root["meshes"].push_back(item);
            }
            for (const Assets::Assimp::ImportedSceneMaterial& material : payload.scene.materials) {
                root["materials"].push_back(materialNode(material));
            }
            for (const Assets::Assimp::ImportedSceneTexture& texture : payload.scene.textures) {
                root["textures"].push_back(textureNode(texture));
            }
            for (const Assets::Assimp::ImportedSceneJoint& joint : payload.scene.joints) {
                root["joints"].push_back(jointNode(joint));
            }
            for (const Assets::Assimp::ImportedSceneSkin& skin : payload.scene.skins) {
                root["skins"].push_back(skinNode(skin));
            }
            for (const Assets::Assimp::ImportedSceneAnimationClip& clip : payload.scene.animations) {
                YAML::Node item;
                item["name"] = clip.name;
                item["duration"] = clip.durationSeconds;
                item["ticks_per_second"] = clip.ticksPerSecond;
                for (const Assets::Assimp::ImportedSceneAnimationChannel& channel : clip.channels) {
                    item["channels"].push_back(channelNode(channel));
                }
                root["animations"].push_back(item);
            }

            {
                std::ofstream output(rootPath / "model.yaml");
                if (!output) {
                    result.status = AnimatedModelCacheStatus::WriteFailed;
                    result.message = "Failed to open animated model cache payload for writing.";
                    return result;
                }
                output << root;
            }
            {
                std::ofstream output(result.path);
                if (!output) {
                    result.status = AnimatedModelCacheStatus::WriteFailed;
                    result.message = "Failed to open animated model cache manifest for writing.";
                    return result;
                }
                output << manifestRoot;
            }
            result.status = AnimatedModelCacheStatus::WriteSuccess;
            result.message = "Wrote animated model cache.";
        } catch (const std::exception& exception) {
            result.status = AnimatedModelCacheStatus::WriteFailed;
            result.message = exception.what();
        }
        return result;
    }
}
