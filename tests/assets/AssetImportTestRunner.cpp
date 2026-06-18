#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Assets/Assimp/Importer.hpp"

namespace {
    struct TestFailure {
        std::string testName;
        std::string message;
    };

    struct TestContext {
        std::string name;
        std::vector<TestFailure>& failures;

        void expect(bool condition, std::string message)
        {
            if (!condition) {
                failures.push_back({name, std::move(message)});
            }
        }
    };

    std::filesystem::path fixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::filesystem::path skinnedFixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/skinned_animation_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../tests/assets/fixtures/skinned_animation_fixture.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::filesystem::path sponzaPath()
    {
        const std::filesystem::path sourceRelative = "assets/main_sponza/NewSponza_Main_glTF_003.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../assets/main_sponza/NewSponza_Main_glTF_003.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::filesystem::path sponzaFbxPath()
    {
        const std::filesystem::path sourceRelative = "assets/main_sponza/NewSponza_Main_Yup_003.fbx";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../assets/main_sponza/NewSponza_Main_Yup_003.fbx";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::vector<std::filesystem::path> kayKitFbxAnimationPaths()
    {
        return {
            "assets/KayKit_Adventurers_2.0_FREE/Animations/fbx/Rig_Medium/Rig_Medium_General.fbx",
            "assets/KayKit_Adventurers_2.0_FREE/Animations/fbx/Rig_Medium/Rig_Medium_MovementBasic.fbx",
        };
    }

    std::filesystem::path authoredReportPath(std::string_view filename)
    {
        const std::filesystem::path directory = "generated/authored_scene_reports";
        std::filesystem::create_directories(directory);
        return directory / filename;
    }

    bool heavyOptionalAssetTestsEnabled()
    {
        const char* value = std::getenv("MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS");
        if (!value) {
            return false;
        }
        const std::string_view text{value};
        return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
    }

    template <typename Function>
    auto measureMilliseconds(Function&& function, float& milliseconds)
    {
        const auto start = std::chrono::steady_clock::now();
        auto result = function();
        const auto end = std::chrono::steady_clock::now();
        milliseconds = std::chrono::duration<float, std::milli>(end - start).count();
        return result;
    }

    bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.0001f)
    {
        return nearlyEqual(lhs.x, rhs.x, epsilon) &&
            nearlyEqual(lhs.y, rhs.y, epsilon) &&
            nearlyEqual(lhs.z, rhs.z, epsilon);
    }

    glm::vec3 translationOf(const glm::mat4& transform)
    {
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    std::optional<uint32_t> findNode(const Assets::Assimp::ImportedScene& scene, std::string_view name)
    {
        for (uint32_t index = 0; index < scene.nodes.size(); ++index) {
            if (scene.nodes[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    void authoredSceneImportsGraphAndGeometry(TestContext& ctx)
    {
        const Assets::Assimp::ImportedScene scene = Assets::Assimp::importScene(fixturePath());
        ctx.expect(scene.success, "fixture scene import failed: " + scene.error);
        if (!scene.success) {
            return;
        }

        ctx.expect(scene.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Gltf, "fixture source format should be glTF");
        ctx.expect(scene.diagnostics.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Gltf, "diagnostic source format should be glTF");
        ctx.expect(scene.rootNodeIndex != UINT32_MAX, "scene root node was not set");
        ctx.expect(scene.meshes.size() == 1, "fixture should import one mesh");
        ctx.expect(scene.diagnostics.primitiveCount == 1, "fixture should import one primitive");
        ctx.expect(scene.materials.size() == 1, "fixture should import one material");

        const std::optional<uint32_t> rootOffsetIndex = findNode(scene, "RootOffset");
        const std::optional<uint32_t> meshChildIndex = findNode(scene, "MeshChild");
        ctx.expect(rootOffsetIndex.has_value(), "missing RootOffset node");
        ctx.expect(meshChildIndex.has_value(), "missing MeshChild node");
        if (rootOffsetIndex && meshChildIndex) {
            const Assets::Assimp::ImportedSceneNode& rootOffset = scene.nodes[*rootOffsetIndex];
            const Assets::Assimp::ImportedSceneNode& meshChild = scene.nodes[*meshChildIndex];
            ctx.expect(meshChild.parentIndex == *rootOffsetIndex, "MeshChild parent did not point to RootOffset");
            ctx.expect(!meshChild.meshIndices.empty() && meshChild.meshIndices.front() == 0, "MeshChild did not reference mesh 0");
            ctx.expect(nearlyEqual(translationOf(rootOffset.worldTransform), {10.0f, 0.0f, 0.0f}),
                "RootOffset world transform did not preserve translation");
            ctx.expect(nearlyEqual(translationOf(meshChild.worldTransform), {10.0f, 2.0f, 0.0f}),
                "MeshChild world transform did not combine parent and local translation");
        }

        const Assets::Assimp::ImportedSceneMesh& mesh = scene.meshes.front();
        ctx.expect(mesh.primitives.size() == 1, "mesh should contain one primitive");
        if (!mesh.primitives.empty()) {
            const Assets::Assimp::ImportedScenePrimitive& primitive = mesh.primitives.front();
            ctx.expect(primitive.vertices.size() == 4, "primitive should contain four vertices");
            ctx.expect(primitive.indices.size() == 6, "primitive should contain six indices");
            ctx.expect(primitive.materialIndex == 0, "primitive material index should be zero");
            ctx.expect(primitive.bounds.valid, "primitive bounds were not computed");
            ctx.expect(primitive.hasTexcoord1, "primitive did not preserve TEXCOORD_1");
            ctx.expect(primitive.hasColor0, "primitive did not preserve COLOR_0");
            if (!primitive.vertices.empty()) {
                const Assets::Assimp::ImportedSceneVertex& vertex = primitive.vertices.front();
                ctx.expect(vertex.hasNormal, "vertex normal stream missing");
                ctx.expect(vertex.hasTangent, "vertex tangent stream missing");
                ctx.expect(vertex.hasTexcoord0, "vertex texcoord0 stream missing");
                ctx.expect(vertex.hasTexcoord1, "vertex texcoord1 stream missing");
                ctx.expect(vertex.hasColor0, "vertex color0 stream missing");
            }
        }
    }

    void authoredSceneImportsMaterialDiagnostics(TestContext& ctx)
    {
        const Assets::Assimp::ImportedScene scene = Assets::Assimp::importScene(fixturePath());
        ctx.expect(scene.success, "fixture scene import failed: " + scene.error);
        if (!scene.success || scene.materials.empty()) {
            return;
        }

        const Assets::Assimp::ImportedSceneMaterial& material = scene.materials.front();
        ctx.expect(material.name == "FixtureBlendDoubleSided", "material name did not round-trip");
        ctx.expect(nearlyEqual(material.baseColorFactor.x, 0.25f), "base color factor R did not import");
        ctx.expect(material.alphaMode == Assets::Assimp::ImportedSceneAlphaMode::Blend,
            "alpha mode was not imported as blend");
        ctx.expect(material.doubleSided, "double-sided material flag was not imported");
        ctx.expect(!material.baseColorTexture.empty(), "base color texture URI was not imported");
        ctx.expect(!material.normalTexture.empty(), "normal texture URI was not imported");
        ctx.expect(nearlyEqual(material.normalScale, 0.75f) || nearlyEqual(material.normalScale, 1.0f),
            "normal texture scale was neither imported nor defaulted");
        ctx.expect(material.hasPackedMetallicRoughnessTexture || !material.roughnessTexture.empty() || !material.metallicTexture.empty(),
            "metallic/roughness texture metadata was not imported");
        ctx.expect(!material.occlusionTexture.empty(), "occlusion texture URI was not imported");
        ctx.expect(nearlyEqual(material.occlusionStrength, 0.6f) || nearlyEqual(material.occlusionStrength, 1.0f),
            "occlusion strength was neither imported nor defaulted");
        ctx.expect(!material.emissiveTexture.empty(), "emissive texture URI was not imported");
        ctx.expect(nearlyEqual(material.emissiveFactor, {0.1f, 0.2f, 0.3f}), "emissive factor was not imported");
        ctx.expect(material.baseColorTextureHints.colorSpace == Assets::Assimp::ImportedSceneTextureColorSpace::Srgb,
            "base color texture did not keep sRGB hint");
        ctx.expect(material.metallicRoughnessTextureHints.colorSpace == Assets::Assimp::ImportedSceneTextureColorSpace::Linear,
            "metallic-roughness texture did not keep linear hint");
        ctx.expect(material.baseColorTextureHints.wrapU == Assets::Assimp::ImportedSceneTextureWrap::Repeat &&
            material.baseColorTextureHints.wrapV == Assets::Assimp::ImportedSceneTextureWrap::Repeat,
            "base color texture did not keep repeat wrap hint");
        ctx.expect(material.normalTextureHints.wrapU == Assets::Assimp::ImportedSceneTextureWrap::Repeat &&
            material.normalTextureHints.wrapV == Assets::Assimp::ImportedSceneTextureWrap::Repeat,
            "normal texture did not keep repeat wrap hint");

        ctx.expect(scene.diagnostics.nodeCount >= 2, "diagnostics missing node count");
        ctx.expect(scene.diagnostics.meshNodeCount == 1, "diagnostics mesh node count was wrong");
        ctx.expect(scene.diagnostics.materialCount == 1, "diagnostics material count was wrong");
        ctx.expect(scene.diagnostics.alphaMaterialCount == 1, "diagnostics alpha material count was wrong");
        ctx.expect(scene.diagnostics.doubleSidedMaterialCount == 1, "diagnostics double-sided material count was wrong");
        ctx.expect(scene.diagnostics.texcoord1PrimitiveCount == 1, "diagnostics texcoord1 primitive count was wrong");
        ctx.expect(scene.diagnostics.vertexColorPrimitiveCount == 1, "diagnostics vertex color primitive count was wrong");
        ctx.expect(scene.diagnostics.textureCount > 0, "diagnostics texture count should be nonzero");
        const auto packedTexture = std::find_if(
            scene.textures.begin(),
            scene.textures.end(),
            [](const Assets::Assimp::ImportedSceneTexture& texture) {
                return texture.semantic == Assets::Assimp::ImportedSceneTextureSemantic::MetallicRoughness;
            });
        ctx.expect(packedTexture != scene.textures.end(), "packed metallic-roughness texture record was missing");
        if (packedTexture != scene.textures.end()) {
            ctx.expect(packedTexture->mayBePackedMetallicRoughness, "packed metallic-roughness texture record was not marked packed");
        }
    }

    void authoredSceneImportsLights(TestContext& ctx)
    {
        const Assets::Assimp::ImportedScene scene = Assets::Assimp::importScene(fixturePath());
        ctx.expect(scene.success, "fixture scene import failed: " + scene.error);
        if (!scene.success) {
            return;
        }

        ctx.expect(scene.diagnostics.lightCount == 4, "fixture light count was not imported");
        ctx.expect(scene.lights.size() == 4, "fixture light vector size was wrong");
        if (scene.lights.size() < 4) {
            return;
        }

        const auto findLight = [&](std::string_view name) -> const Assets::Assimp::ImportedSceneLight* {
            const auto found = std::find_if(
                scene.lights.begin(),
                scene.lights.end(),
                [name](const Assets::Assimp::ImportedSceneLight& light) {
                    return light.name == name;
                });
            return found == scene.lights.end() ? nullptr : &(*found);
        };

        const Assets::Assimp::ImportedSceneLight* point = findLight("FixturePointLight");
        const Assets::Assimp::ImportedSceneLight* spot = findLight("FixtureSpotLight");
        const Assets::Assimp::ImportedSceneLight* zero = findLight("FixtureZeroLight");
        const Assets::Assimp::ImportedSceneLight* directional = findLight("FixtureDirectionalLight");

        ctx.expect(point != nullptr, "point light was not imported by name");
        ctx.expect(spot != nullptr, "spot light was not imported by name");
        ctx.expect(zero != nullptr, "zero-intensity light was not imported by name");
        ctx.expect(directional != nullptr, "directional light was not imported by name");
        if (point) {
            ctx.expect(point->type == Assets::Assimp::ImportedSceneLightType::Point, "point light imported with wrong type");
            ctx.expect(nearlyEqual(point->intensity, 5.0f), "point light intensity was wrong");
            ctx.expect(nearlyEqual(point->range, 12.0f) || point->range == 0.0f, "point light range was wrong");
            ctx.expect(point->nodeIndex.has_value(), "point light node association was missing");
        }
        if (spot) {
            ctx.expect(spot->type == Assets::Assimp::ImportedSceneLightType::Spot, "spot light imported with wrong type");
            ctx.expect(nearlyEqual(spot->innerConeAngle, 0.2f) || spot->innerConeAngle == 0.0f, "spot inner cone was wrong");
            ctx.expect(nearlyEqual(spot->outerConeAngle, 0.7f) || spot->outerConeAngle == 0.0f, "spot outer cone was wrong");
            ctx.expect(spot->nodeIndex.has_value(), "spot light node association was missing");
        }
        if (zero) {
            ctx.expect(nearlyEqual(zero->intensity, 0.0f), "zero light intensity should stay zero");
            ctx.expect(zero->nodeIndex.has_value(), "zero light node association was missing");
        }
        if (directional) {
            ctx.expect(
                directional->type == Assets::Assimp::ImportedSceneLightType::Directional,
                "directional light imported with wrong type");
            ctx.expect(directional->nodeIndex.has_value(), "directional light node association was missing");
        }
    }

    void authoredSceneImportsSkinsAndAnimations(TestContext& ctx)
    {
        const Assets::Assimp::ImportedScene scene = Assets::Assimp::importScene(skinnedFixturePath());
        ctx.expect(scene.success, "skinned fixture import failed: " + scene.error);
        if (!scene.success) {
            return;
        }

        ctx.expect(scene.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Gltf, "skinned fixture source format should be glTF");
        ctx.expect(scene.diagnostics.skinCount == 1, "skinned fixture should import one skin");
        ctx.expect(scene.diagnostics.jointCount == 2, "skinned fixture should import two joints");
        ctx.expect(scene.diagnostics.skinnedMeshCount == 1, "skinned fixture should import one skinned mesh");
        ctx.expect(scene.diagnostics.animationCount == 2, "skinned fixture should import two animation clips");
        ctx.expect(scene.diagnostics.animationChannelCount == 4, "skinned fixture should import four animation channels");
        ctx.expect(scene.diagnostics.translationKeyCount >= 2, "skinned fixture translation keys were not preserved");
        ctx.expect(scene.diagnostics.rotationKeyCount >= 2, "skinned fixture rotation keys were not preserved");
        ctx.expect(scene.diagnostics.influencedVertexCount == 3, "skinned fixture influenced vertex count was wrong");
        ctx.expect(scene.diagnostics.maxInfluencesPerVertex == 1, "skinned fixture max influences per vertex was wrong");
        ctx.expect(scene.diagnostics.zeroWeightVertexCount == 0, "skinned fixture should not report zero-weight vertices");
        ctx.expect(scene.diagnostics.nonNormalizedWeightVertexCount == 0, "skinned fixture should not report non-normalized weights");

        ctx.expect(scene.skins.size() == 1, "skin vector size was wrong");
        ctx.expect(scene.joints.size() == 2, "joint vector size was wrong");
        ctx.expect(scene.animations.size() == 2, "animation vector size was wrong");
        if (scene.skins.size() == 1) {
            const Assets::Assimp::ImportedSceneSkin& skin = scene.skins.front();
            ctx.expect(skin.jointIndices.size() == 2, "skin joint reference count was wrong");
            ctx.expect(skin.inverseBindMatrixCount == 2, "skin inverse bind count was wrong");
            ctx.expect(!skin.meshIndices.empty() && skin.meshIndices.front() == 0, "skin mesh association was wrong");
            ctx.expect(!skin.nodeIndices.empty(), "skin node association was missing");
            ctx.expect(skin.skeletonRootNodeIndex.has_value(), "skin skeleton root was missing");
        }
        if (scene.joints.size() == 2) {
            const Assets::Assimp::ImportedSceneJoint& root = scene.joints[0];
            const Assets::Assimp::ImportedSceneJoint& tip = scene.joints[1];
            ctx.expect(root.name == "JointRoot", "root joint name was wrong");
            ctx.expect(tip.name == "JointTip", "tip joint name was wrong");
            ctx.expect(root.nodeIndex.has_value(), "root joint node association missing");
            ctx.expect(tip.nodeIndex.has_value(), "tip joint node association missing");
            ctx.expect(root.parentJointIndex == UINT32_MAX, "root joint should not have parent joint");
            ctx.expect(tip.parentJointIndex == 0, "tip joint parent was wrong");
            ctx.expect(root.hasInverseBindMatrix, "root inverse bind matrix missing");
            ctx.expect(tip.hasInverseBindMatrix, "tip inverse bind matrix missing");
        }
        if (!scene.meshes.empty() && !scene.meshes.front().primitives.empty()) {
            const Assets::Assimp::ImportedScenePrimitive& primitive = scene.meshes.front().primitives.front();
            ctx.expect(primitive.vertices.size() == 3, "skinned primitive vertex count was wrong");
            if (primitive.vertices.size() == 3) {
                ctx.expect(primitive.vertices[0].influences.size() == 1, "vertex 0 influence count was wrong");
                ctx.expect(primitive.vertices[0].influences[0].jointIndex == 0, "vertex 0 joint index was wrong");
                ctx.expect(nearlyEqual(primitive.vertices[0].influences[0].weight, 1.0f), "vertex 0 joint weight was wrong");
                ctx.expect(primitive.vertices[2].influences.size() == 1, "vertex 2 influence count was wrong");
                ctx.expect(primitive.vertices[2].influences[0].jointIndex == 1, "vertex 2 joint index was wrong");
            }
        }
        if (scene.animations.size() >= 2) {
            const Assets::Assimp::ImportedSceneAnimationClip& clip = scene.animations.front();
            ctx.expect(clip.name == "JointWave", "animation clip name was wrong");
            ctx.expect(nearlyEqual(clip.durationSeconds, 1.0f), "animation clip duration was wrong");
            ctx.expect(clip.channels.size() == 2, "animation channel vector size was wrong");
            if (clip.channels.size() == 2) {
                ctx.expect(clip.channels[0].targetName == "JointTip", "rotation channel target name was wrong");
                ctx.expect(clip.channels[0].targetNodeIndex.has_value(), "rotation channel node association missing");
                ctx.expect(clip.channels[0].rotationKeys.size() == 2, "rotation key count was wrong");
                ctx.expect(clip.channels[1].targetName == "JointRoot", "translation channel target name was wrong");
                ctx.expect(clip.channels[1].targetNodeIndex.has_value(), "translation channel node association missing");
                ctx.expect(clip.channels[1].translationKeys.size() == 2, "translation key count was wrong");
                if (clip.channels[1].translationKeys.size() == 2) {
                    ctx.expect(nearlyEqual(clip.channels[1].translationKeys[1].value.y, 0.5f), "translation key value was wrong");
                }
            }
            const Assets::Assimp::ImportedSceneAnimationClip& secondClip = scene.animations[1];
            ctx.expect(secondClip.name == "JointNod", "second animation clip name was wrong");
            ctx.expect(nearlyEqual(secondClip.durationSeconds, 1.0f), "second animation clip duration was wrong");
            ctx.expect(secondClip.channels.size() == 2, "second animation channel vector size was wrong");
            if (secondClip.channels.size() == 2) {
                ctx.expect(secondClip.channels[0].targetName == "JointRoot", "second clip rotation channel target name was wrong");
                ctx.expect(secondClip.channels[0].rotationKeys.size() == 2, "second clip rotation key count was wrong");
                ctx.expect(secondClip.channels[1].targetName == "JointTip", "second clip translation channel target name was wrong");
                ctx.expect(secondClip.channels[1].translationKeys.size() == 2, "second clip translation key count was wrong");
            }
        }
    }

    void staticMeshImporterStillWorks(TestContext& ctx)
    {
        const Assets::Assimp::ImportResult result = Assets::Assimp::importStaticMesh(fixturePath());
        ctx.expect(result.success, "static mesh import regression failed: " + result.error);
        if (!result.success) {
            return;
        }

        ctx.expect(result.mesh.submeshes.size() == 1, "static mesh import should keep one submesh");
        ctx.expect(!result.mesh.materials.empty(), "static mesh import should keep at least one material");
        if (!result.mesh.submeshes.empty()) {
            ctx.expect(result.mesh.submeshes.front().vertices.size() == 4, "static mesh import vertex count changed");
            ctx.expect(result.mesh.submeshes.front().indices.size() == 6, "static mesh import index count changed");
        }
    }

    void authoredSceneDetectsSourceFormats(TestContext& ctx)
    {
        ctx.expect(
            Assets::Assimp::detectSceneSourceFormat("scene.gltf") == Assets::Assimp::ImportedSceneSourceFormat::Gltf,
            "gltf extension was not detected");
        ctx.expect(
            Assets::Assimp::detectSceneSourceFormat("scene.glb") == Assets::Assimp::ImportedSceneSourceFormat::Glb,
            "glb extension was not detected");
        ctx.expect(
            Assets::Assimp::detectSceneSourceFormat("scene.fbx") == Assets::Assimp::ImportedSceneSourceFormat::Fbx,
            "fbx extension was not detected");
        ctx.expect(
            std::string{Assets::Assimp::sourceFormatName(Assets::Assimp::ImportedSceneSourceFormat::Fbx)} == "fbx",
            "fbx source format name was wrong");
    }

    void optionalSponzaValidation(TestContext& ctx)
    {
        if (!heavyOptionalAssetTestsEnabled()) {
            std::cout << "[assets] Heavy optional asset tests disabled; set MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1 to run Sponza validation\n";
            return;
        }

        const std::filesystem::path path = sponzaPath();
        if (!std::filesystem::exists(path)) {
            std::cout << "[assets] Sponza fixture not present; skipping optional validation\n";
            return;
        }

        float importMs = 0.0f;
        const Assets::Assimp::ImportedScene scene = measureMilliseconds([&]() {
            return Assets::Assimp::importScene(path);
        }, importMs);
        ctx.expect(scene.success, "Sponza scene import failed: " + scene.error);
        if (!scene.success) {
            return;
        }

        ctx.expect(scene.diagnostics.nodeCount == 155, "Sponza node count differed from expected 155");
        ctx.expect(scene.diagnostics.meshNodeCount == 115, "Sponza mesh node count differed from expected 115");
        ctx.expect(scene.diagnostics.primitiveCount == 405, "Sponza primitive count differed from expected 405");
        ctx.expect(scene.diagnostics.materialCount == 28, "Sponza material count differed from expected 28");
        ctx.expect(scene.diagnostics.textureCount > 0, "Sponza texture references were not imported");
        ctx.expect(scene.diagnostics.lightCount > 0, "Sponza light records were not imported");
        ctx.expect(scene.diagnostics.alphaMaterialCount > 0, "Sponza alpha material usage was not detected");
        ctx.expect(scene.diagnostics.doubleSidedMaterialCount > 0, "Sponza double-sided material usage was not detected");
        ctx.expect(scene.diagnostics.texcoord1PrimitiveCount > 0, "Sponza TEXCOORD_1 usage was not detected");
        ctx.expect(scene.diagnostics.vertexColorPrimitiveCount > 0, "Sponza COLOR_0 usage was not detected");

        for (const std::string& warning : scene.diagnostics.warnings) {
            std::cout << "[assets][sponza warning] " << warning << '\n';
        }

        std::ofstream report(authoredReportPath("authored_scene_sponza_import_report.txt"));
        report << "Sponza Import Report\n";
        report << "import_ms: " << importMs << '\n';
        report << "nodes: " << scene.diagnostics.nodeCount << '\n';
        report << "mesh_nodes: " << scene.diagnostics.meshNodeCount << '\n';
        report << "primitives: " << scene.diagnostics.primitiveCount << '\n';
        report << "materials: " << scene.diagnostics.materialCount << '\n';
        report << "textures: " << scene.diagnostics.textureCount << '\n';
        report << "lights: " << scene.diagnostics.lightCount << '\n';
        report << "alpha_materials: " << scene.diagnostics.alphaMaterialCount << '\n';
        report << "double_sided_materials: " << scene.diagnostics.doubleSidedMaterialCount << '\n';
        report << "texcoord1_primitives: " << scene.diagnostics.texcoord1PrimitiveCount << '\n';
        report << "vertex_color_primitives: " << scene.diagnostics.vertexColorPrimitiveCount << '\n';
        report << "warnings: " << scene.diagnostics.warnings.size() << '\n';
    }

    void optionalSponzaFbxValidation(TestContext& ctx)
    {
        if (!heavyOptionalAssetTestsEnabled()) {
            std::cout << "[assets] Heavy optional asset tests disabled; set MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1 to run Sponza FBX validation\n";
            return;
        }

        const std::filesystem::path path = sponzaFbxPath();
        if (!std::filesystem::exists(path)) {
            std::cout << "[assets] Sponza FBX fixture not present; skipping optional validation\n";
            return;
        }

        float importMs = 0.0f;
        const Assets::Assimp::ImportedScene scene = measureMilliseconds([&]() {
            return Assets::Assimp::importScene(path);
        }, importMs);
        ctx.expect(scene.success, "Sponza FBX scene import failed: " + scene.error);
        if (!scene.success) {
            return;
        }

        ctx.expect(scene.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx, "Sponza FBX source format was not FBX");
        ctx.expect(scene.diagnostics.nodeCount > 0, "Sponza FBX node count was zero");
        ctx.expect(scene.diagnostics.meshCount > 0, "Sponza FBX mesh count was zero");
        ctx.expect(scene.diagnostics.primitiveCount > 0, "Sponza FBX primitive count was zero");
        ctx.expect(scene.diagnostics.materialCount > 0, "Sponza FBX material count was zero");
        ctx.expect(scene.bounds.valid, "Sponza FBX bounds were invalid");

        std::ofstream report(authoredReportPath("authored_scene_sponza_fbx_import_report.txt"));
        report << "Sponza FBX Import Report\n";
        report << "import_ms: " << importMs << '\n';
        report << "source_format: " << Assets::Assimp::sourceFormatName(scene.sourceFormat) << '\n';
        report << "nodes: " << scene.diagnostics.nodeCount << '\n';
        report << "mesh_nodes: " << scene.diagnostics.meshNodeCount << '\n';
        report << "meshes: " << scene.diagnostics.meshCount << '\n';
        report << "primitives: " << scene.diagnostics.primitiveCount << '\n';
        report << "materials: " << scene.diagnostics.materialCount << '\n';
        report << "textures: " << scene.diagnostics.textureCount << '\n';
        report << "embedded_textures: " << scene.diagnostics.embeddedTextureCount << '\n';
        report << "missing_pbr_materials: " << scene.diagnostics.missingPbrMaterialCount << '\n';
        report << "fbx_unit_scale_factor: " << scene.diagnostics.fbxUnitScaleFactor << '\n';
        report << "fbx_up_axis: " << scene.diagnostics.fbxUpAxis << '\n';
        report << "fbx_up_axis_sign: " << scene.diagnostics.fbxUpAxisSign << '\n';
        report << "warnings: " << scene.diagnostics.warnings.size() << '\n';
    }

    void optionalKayKitFbxAnimationValidation(TestContext& ctx)
    {
        if (!heavyOptionalAssetTestsEnabled()) {
            std::cout << "[assets] Heavy optional asset tests disabled; set MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1 to run KayKit FBX validation\n";
            return;
        }

        uint32_t validatedCount = 0;
        for (const std::filesystem::path& path : kayKitFbxAnimationPaths()) {
            if (!std::filesystem::exists(path)) {
                std::cout << "[assets] KayKit FBX fixture not present; skipping " << path.generic_string() << '\n';
                continue;
            }

            ++validatedCount;
            float importMs = 0.0f;
            const Assets::Assimp::ImportedScene scene = measureMilliseconds([&]() {
                return Assets::Assimp::importScene(path);
            }, importMs);
            ctx.expect(scene.success, "KayKit FBX animation import failed: " + path.generic_string() + " " + scene.error);
            if (!scene.success) {
                continue;
            }

            ctx.expect(scene.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx, "KayKit source format was not FBX");
            ctx.expect(scene.diagnostics.nodeCount > 0, "KayKit FBX node count was zero");
            ctx.expect(scene.diagnostics.jointCount > 0 || scene.diagnostics.animationCount > 0,
                "KayKit FBX did not expose joints or animations");
            if (scene.diagnostics.animationCount > 0) {
                ctx.expect(scene.diagnostics.animationChannelCount > 0, "KayKit FBX animation had no channels");
                ctx.expect(
                    scene.diagnostics.translationKeyCount + scene.diagnostics.rotationKeyCount + scene.diagnostics.scaleKeyCount > 0,
                    "KayKit FBX animation had no imported keys");
            }
            if (scene.diagnostics.meshCount > 0 && scene.diagnostics.primitiveCount > 0) {
                ctx.expect(scene.bounds.valid, "KayKit FBX renderable scene bounds were invalid");
            }
            ctx.expect(scene.diagnostics.fbxAnimationStackCount == scene.diagnostics.animationCount,
                "KayKit FBX stack count did not match imported animation count");
            ctx.expect(!scene.diagnostics.warnings.empty(), "KayKit FBX should report best-effort FBX diagnostics");

            std::ofstream report(authoredReportPath("authored_scene_kaykit_fbx_" + path.stem().string() + "_import_report.txt"));
            report << "KayKit FBX Import Report\n";
            report << "path: " << path.generic_string() << '\n';
            report << "import_ms: " << importMs << '\n';
            report << "source_format: " << Assets::Assimp::sourceFormatName(scene.sourceFormat) << '\n';
            report << "nodes: " << scene.diagnostics.nodeCount << '\n';
            report << "meshes: " << scene.diagnostics.meshCount << '\n';
            report << "primitives: " << scene.diagnostics.primitiveCount << '\n';
            report << "materials: " << scene.diagnostics.materialCount << '\n';
            report << "skins: " << scene.diagnostics.skinCount << '\n';
            report << "joints: " << scene.diagnostics.jointCount << '\n';
            report << "skinned_meshes: " << scene.diagnostics.skinnedMeshCount << '\n';
            report << "influenced_vertices: " << scene.diagnostics.influencedVertexCount << '\n';
            report << "max_influences_per_vertex: " << scene.diagnostics.maxInfluencesPerVertex << '\n';
            report << "over_four_influence_vertices: " << scene.diagnostics.overFourInfluenceVertexCount << '\n';
            report << "animations: " << scene.diagnostics.animationCount << '\n';
            report << "fbx_animation_stacks: " << scene.diagnostics.fbxAnimationStackCount << '\n';
            report << "fbx_unnamed_animation_stacks: " << scene.diagnostics.fbxUnnamedAnimationStackCount << '\n';
            report << "animation_channels: " << scene.diagnostics.animationChannelCount << '\n';
            report << "translation_keys: " << scene.diagnostics.translationKeyCount << '\n';
            report << "rotation_keys: " << scene.diagnostics.rotationKeyCount << '\n';
            report << "scale_keys: " << scene.diagnostics.scaleKeyCount << '\n';
            report << "missing_animation_targets: " << scene.diagnostics.missingAnimationTargetCount << '\n';
            report << "embedded_textures: " << scene.diagnostics.embeddedTextureCount << '\n';
            report << "missing_pbr_materials: " << scene.diagnostics.missingPbrMaterialCount << '\n';
            report << "fbx_unit_scale_factor: " << scene.diagnostics.fbxUnitScaleFactor << '\n';
            report << "fbx_up_axis: " << scene.diagnostics.fbxUpAxis << '\n';
            report << "fbx_up_axis_sign: " << scene.diagnostics.fbxUpAxisSign << '\n';
            report << "fbx_animation_semantic_warnings: " << scene.diagnostics.fbxAnimationSemanticWarningCount << '\n';
            report << "warnings: " << scene.diagnostics.warnings.size() << '\n';
        }

        if (validatedCount == 0) {
            std::cout << "[assets] KayKit FBX animation fixtures not present; optional validation skipped\n";
        }
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"AuthoredSceneImportsGraphAndGeometry", authoredSceneImportsGraphAndGeometry},
        {"AuthoredSceneImportsMaterialDiagnostics", authoredSceneImportsMaterialDiagnostics},
        {"AuthoredSceneImportsLights", authoredSceneImportsLights},
        {"AuthoredSceneImportsSkinsAndAnimations", authoredSceneImportsSkinsAndAnimations},
        {"StaticMeshImporterStillWorks", staticMeshImporterStillWorks},
        {"AuthoredSceneDetectsSourceFormats", authoredSceneDetectsSourceFormats},
        {"OptionalSponzaValidation", optionalSponzaValidation},
        {"OptionalSponzaFbxValidation", optionalSponzaFbxValidation},
        {"OptionalKayKitFbxAnimationValidation", optionalKayKitFbxAnimationValidation},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
        std::cout << "[assets] " << name << '\n';
    }

    if (failures.empty()) {
        std::cout << "Asset import tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Asset import tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  [" << failure.testName << "] " << failure.message << '\n';
    }
    return 1;
}
