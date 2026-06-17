#include <cmath>
#include <algorithm>
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

    void optionalSponzaValidation(TestContext& ctx)
    {
        const std::filesystem::path path = sponzaPath();
        if (!std::filesystem::exists(path)) {
            std::cout << "[assets] Sponza fixture not present; skipping optional validation\n";
            return;
        }

        const Assets::Assimp::ImportedScene scene = Assets::Assimp::importScene(path);
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
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"AuthoredSceneImportsGraphAndGeometry", authoredSceneImportsGraphAndGeometry},
        {"AuthoredSceneImportsMaterialDiagnostics", authoredSceneImportsMaterialDiagnostics},
        {"AuthoredSceneImportsLights", authoredSceneImportsLights},
        {"StaticMeshImporterStillWorks", staticMeshImporterStillWorks},
        {"OptionalSponzaValidation", optionalSponzaValidation},
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
