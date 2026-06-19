#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Scene/AuthoredSceneAdapter.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"

namespace TestRenderer {
    void reset();
    uint32_t liveMeshCount();
    uint32_t liveMaterialCount();
    uint32_t liveTextureCount();
    uint32_t liveInstanceCount();
    uint32_t liveLightCount();
    Renderer::StaticMeshDescriptor firstMeshDescriptor();
    Renderer::MaterialDescriptor firstMaterialDescriptor();
}

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

    class FakeRenderBackend final : public Engine::SceneRenderBackend {
    public:
        struct MeshInstanceRecord {
            bool alive = false;
            Renderer::StaticMeshHandle mesh;
            glm::mat4 transform{1.0f};
        };

        struct LightRecord {
            bool alive = false;
            Renderer::LightDescriptor descriptor;
        };

        Renderer::MeshInstanceHandle createMeshInstance(Renderer::StaticMeshHandle mesh) override
        {
            if (mesh.id == UINT32_MAX) {
                return {};
            }
            for (uint32_t index = 0; index < meshInstances.size(); ++index) {
                if (!meshInstances[index].alive) {
                    meshInstances[index] = {true, mesh, glm::mat4{1.0f}};
                    return {index};
                }
            }
            meshInstances.push_back({true, mesh, glm::mat4{1.0f}});
            return {static_cast<uint32_t>(meshInstances.size() - 1)};
        }

        void destroyMeshInstance(Renderer::MeshInstanceHandle instance) override
        {
            if (instance.id < meshInstances.size()) {
                meshInstances[instance.id] = {};
            }
        }

        void setMeshInstanceTransform(Renderer::MeshInstanceHandle instance, const glm::mat4& transform) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].transform = transform;
            }
        }

        void setMeshInstanceMaterial(Renderer::MeshInstanceHandle, Renderer::MaterialHandle) override {}
        void clearMeshInstanceMaterial(Renderer::MeshInstanceHandle) override {}
        void setMeshInstanceRenderLayer(Renderer::MeshInstanceHandle, Renderer::RenderLayer) override {}
        void setMeshInstanceVisibilityFlags(Renderer::MeshInstanceHandle, Renderer::VisibilityFlags) override {}
        void setMeshInstanceMaxDrawDistance(Renderer::MeshInstanceHandle, float) override {}
        void setMeshInstanceRenderGroup(Renderer::MeshInstanceHandle, Renderer::RenderGroupHandle) override {}
        void clearMeshInstanceRenderGroup(Renderer::MeshInstanceHandle) override {}

        Renderer::SkinnedMeshInstanceHandle createSkinnedMeshInstance(Renderer::SkinnedMeshHandle) override { return {}; }
        void destroySkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle) override {}
        void setSkinnedMeshInstanceTransform(Renderer::SkinnedMeshInstanceHandle, const glm::mat4&) override {}
        void setSkinnedMeshInstanceRenderLayer(Renderer::SkinnedMeshInstanceHandle, Renderer::RenderLayer) override {}
        void setSkinnedMeshInstanceMaxDrawDistance(Renderer::SkinnedMeshInstanceHandle, float) override {}
        void setSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle, Renderer::RenderGroupHandle) override {}
        void clearSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle) override {}
        void setSkinnedMeshInstanceJointMatrices(Renderer::SkinnedMeshInstanceHandle, const std::vector<glm::mat4>&) override {}

        Renderer::LightHandle createLight(const Renderer::LightDescriptor& descriptor) override
        {
            for (uint32_t index = 0; index < lights.size(); ++index) {
                if (!lights[index].alive) {
                    lights[index] = {true, descriptor};
                    return {index};
                }
            }
            lights.push_back({true, descriptor});
            return {static_cast<uint32_t>(lights.size() - 1)};
        }

        void destroyLight(Renderer::LightHandle light) override
        {
            if (light.id < lights.size()) {
                lights[light.id] = {};
            }
        }

        void setLightDescriptor(Renderer::LightHandle light, const Renderer::LightDescriptor& descriptor) override
        {
            if (light.id < lights.size() && lights[light.id].alive) {
                lights[light.id].descriptor = descriptor;
            }
        }

        uint32_t liveMeshInstanceCount() const
        {
            uint32_t count = 0;
            for (const MeshInstanceRecord& instance : meshInstances) {
                count += instance.alive ? 1u : 0u;
            }
            return count;
        }

        uint32_t liveLightCount() const
        {
            uint32_t count = 0;
            for (const LightRecord& light : lights) {
                count += light.alive ? 1u : 0u;
            }
            return count;
        }

        std::vector<MeshInstanceRecord> meshInstances;
        std::vector<LightRecord> lights;
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

    bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0002f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool nearlyEqual(const glm::mat4& lhs, const glm::mat4& rhs)
    {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!nearlyEqual(lhs[column][row], rhs[column][row])) {
                    return false;
                }
            }
        }
        return true;
    }

    uint32_t meshReferenceCount(const Assets::Assimp::ImportedScene& imported)
    {
        uint32_t count = 0;
        for (const Assets::Assimp::ImportedSceneNode& node : imported.nodes) {
            count += static_cast<uint32_t>(node.meshIndices.size());
        }
        return count;
    }

    uint32_t supportedNodeLightCount(const Assets::Assimp::ImportedScene& imported)
    {
        uint32_t count = 0;
        for (const Assets::Assimp::ImportedSceneLight& light : imported.lights) {
            const bool supported =
                light.type == Assets::Assimp::ImportedSceneLightType::Directional ||
                light.type == Assets::Assimp::ImportedSceneLightType::Point ||
                light.type == Assets::Assimp::ImportedSceneLightType::Spot;
            if (supported && light.nodeIndex.has_value() && *light.nodeIndex < imported.nodes.size()) {
                ++count;
            }
        }
        return count;
    }

    Engine::SceneAuthoredAdapterResult adaptFixture(
        Engine::Scene& scene,
        Engine::SceneRenderBridge& bridge,
        Engine::AssetCache& assetCache,
        Assets::Assimp::ImportedScene& imported,
        Engine::SceneAuthoredAdapterSettings settings = {})
    {
        imported = Assets::Assimp::importScene(fixturePath());
        settings.loadTextures = false;
        return Engine::adaptImportedSceneToScene(imported, fixturePath(), assetCache, scene, bridge, settings);
    }

    Assets::Assimp::ImportedSceneVertex vertex(float x, float y, float z)
    {
        Assets::Assimp::ImportedSceneVertex result;
        result.position = {x, y, z};
        result.normal = {0.0f, 1.0f, 0.0f};
        result.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        result.texcoord0 = {0.0f, 0.0f};
        result.hasNormal = true;
        result.hasTangent = true;
        result.hasTexcoord0 = true;
        return result;
    }

    Assets::Assimp::ImportedScene syntheticInvalidScene()
    {
        Assets::Assimp::ImportedScene scene;
        scene.success = true;
        scene.nodes.resize(2);
        scene.nodes[0].name = "Root";
        scene.nodes[0].meshIndices = {99};
        scene.nodes[1].name = "Child";
        scene.nodes[1].parentIndex = 42;
        scene.nodes[1].meshIndices = {0};

        Assets::Assimp::ImportedScenePrimitive primitive;
        primitive.vertices = {
            vertex(0.0f, 0.0f, 0.0f),
            vertex(1.0f, 0.0f, 0.0f),
            vertex(0.0f, 1.0f, 0.0f),
        };
        primitive.indices = {0, 1, 2};
        primitive.materialIndex = 7;
        Assets::Assimp::ImportedSceneMesh mesh;
        mesh.name = "InvalidMaterialMesh";
        mesh.primitives.push_back(std::move(primitive));
        scene.meshes.push_back(std::move(mesh));

        Assets::Assimp::ImportedSceneLight invalidNodeLight;
        invalidNodeLight.type = Assets::Assimp::ImportedSceneLightType::Point;
        invalidNodeLight.nodeIndex = 99;
        scene.lights.push_back(invalidNodeLight);

        Assets::Assimp::ImportedSceneLight unsupportedLight;
        unsupportedLight.type = Assets::Assimp::ImportedSceneLightType::Ambient;
        unsupportedLight.nodeIndex = 0;
        scene.lights.push_back(unsupportedLight);
        return scene;
    }

    void fixtureCreatesActorsForAllImportedNodes(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);

        ctx.expect(imported.success, "fixture import failed: " + imported.error);
        ctx.expect(result.success, "adapter failed: " + result.message);
        ctx.expect(result.nodes.size() == imported.nodes.size(), "node binding count did not match imported nodes");
        ctx.expect(result.diagnostics.createdActorCount == imported.nodes.size(), "created actor count was wrong");
        for (uint32_t index = 0; index < result.nodes.size(); ++index) {
            ctx.expect(result.nodes[index].importedNodeIndex == index, "node binding order was not preserved");
            ctx.expect(scene.contains(result.nodes[index].actor), "node actor was not live");
        }

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void fixturePreservesHierarchyAndWorldTransforms(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);

        for (uint32_t index = 0; index < imported.nodes.size(); ++index) {
            const uint32_t parentIndex = imported.nodes[index].parentIndex;
            if (parentIndex != UINT32_MAX && parentIndex < result.nodes.size()) {
                const std::optional<Engine::SceneActorHandle> parent = scene.parent(result.nodes[index].actor);
                ctx.expect(parent.has_value() && *parent == result.nodes[parentIndex].actor, "node parent link was not preserved");
            }
            const std::optional<glm::mat4> world = scene.worldMatrix(result.nodes[index].actor);
            ctx.expect(world.has_value() && nearlyEqual(*world, imported.nodes[index].worldTransform),
                "node world transform did not match imported transform");
        }

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void fixtureCreatesMeshComponentsForMeshNodes(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);

        const uint32_t expectedMeshRefs = meshReferenceCount(imported);
        ctx.expect(result.diagnostics.createdMeshComponentCount == expectedMeshRefs, "created mesh component count was wrong");
        uint32_t boundMeshRefs = 0;
        for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
            boundMeshRefs += static_cast<uint32_t>(result.nodes[nodeIndex].meshComponents.size());
            ctx.expect(
                result.nodes[nodeIndex].meshComponents.size() == imported.nodes[nodeIndex].meshIndices.size(),
                "mesh component binding count did not match node mesh references");
        }
        ctx.expect(boundMeshRefs == expectedMeshRefs, "mesh component handles were not stored on node bindings");

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void fixtureCreatesRendererMeshesAndMaterials(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);

        ctx.expect(result.diagnostics.createdRendererMeshCount == imported.meshes.size(), "created renderer mesh count was wrong");
        ctx.expect(result.diagnostics.createdMaterialCount == imported.materials.size(), "created material count was wrong");
        ctx.expect(TestRenderer::liveMeshCount() == imported.meshes.size(), "renderer live mesh count was wrong");
        ctx.expect(TestRenderer::liveMaterialCount() == imported.materials.size(), "renderer live material count was wrong");
        if (!imported.meshes.empty()) {
            ctx.expect(
                TestRenderer::firstMeshDescriptor().submeshes.size() == imported.meshes.front().primitives.size(),
                "renderer mesh submesh count did not match imported primitive count");
        }
        if (!imported.materials.empty()) {
            ctx.expect(!TestRenderer::firstMaterialDescriptor().name.empty(), "renderer material descriptor name was empty");
        }

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        ctx.expect(TestRenderer::liveMeshCount() == 0, "adapter release leaked renderer meshes");
        ctx.expect(TestRenderer::liveMaterialCount() == 0, "adapter release leaked renderer materials");
        assetCache.shutdown();
    }

    void fixtureRegistersTextureStats(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(fixturePath());
        Engine::SceneAuthoredAdapterSettings settings;
        settings.loadTextures = true;
        Engine::SceneAuthoredAdapterResult result = Engine::adaptImportedSceneToScene(
            imported,
            fixturePath(),
            assetCache,
            scene,
            bridge,
            settings);

        ctx.expect(result.success, "adapter failed with texture loading enabled");
        ctx.expect(result.diagnostics.importedTextureCount == imported.textures.size(), "imported texture count was not copied");
        ctx.expect(result.diagnostics.textureFallbackCount > 0 || result.diagnostics.textureLoadSuccessCount > 0,
            "texture diagnostics did not record loads or fallbacks");
        ctx.expect(result.resources.textures.size() == result.diagnostics.textureLoadSuccessCount,
            "texture resource binding count did not match successful texture loads");

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void fixtureCreatesLightComponents(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);

        const uint32_t expectedLights = supportedNodeLightCount(imported);
        ctx.expect(result.diagnostics.createdLightComponentCount == expectedLights, "created light component count was wrong");
        uint32_t boundLights = 0;
        for (const Engine::SceneAuthoredNodeBinding& binding : result.nodes) {
            boundLights += static_cast<uint32_t>(binding.lightComponents.size());
        }
        ctx.expect(boundLights == expectedLights, "light component handles were not stored on node bindings");

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void bridgeSyncCreatesInstancesFromAdaptedScene(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);
        FakeRenderBackend backend;

        bridge.sync(backend);
        ctx.expect(backend.liveMeshInstanceCount() == result.diagnostics.createdMeshComponentCount,
            "bridge did not create one instance per adapted mesh component");
        ctx.expect(backend.liveLightCount() == result.diagnostics.createdLightComponentCount,
            "bridge did not create one light per adapted light component");

        for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
            if (!imported.nodes[nodeIndex].meshIndices.empty() && !backend.meshInstances.empty()) {
                ctx.expect(nearlyEqual(backend.meshInstances.front().transform, imported.nodes[nodeIndex].worldTransform),
                    "bridge instance transform did not match adapted node world transform");
                break;
            }
        }

        bridge.releaseRendererResources(backend);
        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void invalidReferencesAreDiagnosed(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported = syntheticInvalidScene();
        Engine::SceneAuthoredAdapterSettings settings;
        settings.loadTextures = false;
        Engine::SceneAuthoredAdapterResult result = Engine::adaptImportedSceneToScene(
            imported,
            "synthetic_invalid.gltf",
            assetCache,
            scene,
            bridge,
            settings);

        ctx.expect(result.success, "synthetic invalid scene should adapt non-fatally");
        ctx.expect(result.diagnostics.invalidNodeReferenceCount >= 2, "invalid node references were not diagnosed");
        ctx.expect(result.diagnostics.invalidMeshReferenceCount >= 1, "invalid mesh references were not diagnosed");
        ctx.expect(result.diagnostics.invalidMaterialReferenceCount >= 1, "invalid material references were not diagnosed");
        ctx.expect(result.diagnostics.skippedUnsupportedLightCount >= 1, "unsupported lights were not diagnosed");
        ctx.expect(!result.diagnostics.warnings.empty(), "invalid references did not produce warnings");

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void skinnedAndAnimatedDataSkipped(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(skinnedFixturePath());
        Engine::SceneAuthoredAdapterSettings settings;
        settings.loadTextures = false;
        Engine::SceneAuthoredAdapterResult result = Engine::adaptImportedSceneToScene(
            imported,
            skinnedFixturePath(),
            assetCache,
            scene,
            bridge,
            settings);

        ctx.expect(imported.success, "skinned fixture import failed: " + imported.error);
        ctx.expect(result.success, "adapter should still adapt static node hierarchy from skinned fixture");
        ctx.expect(result.diagnostics.skippedSkinCount == imported.skins.size(), "skipped skin count was wrong");
        ctx.expect(result.diagnostics.skippedJointCount == imported.joints.size(), "skipped joint count was wrong");
        ctx.expect(result.diagnostics.skippedAnimationCount == imported.animations.size(), "skipped animation count was wrong");
        ctx.expect(bridge.diagnostics().skinnedMeshComponentCount == 0, "adapter created skinned bridge components in phase 6");

        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();
    }

    void resourceReleaseIsDeterministic(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Assets::Assimp::ImportedScene imported;
        Engine::SceneAuthoredAdapterResult result = adaptFixture(scene, bridge, assetCache, imported);
        FakeRenderBackend backend;
        bridge.sync(backend);

        bridge.releaseRendererResources(backend);
        bridge.releaseRendererResources(backend);
        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        Engine::releaseSceneAuthoredAdapterResources(result.resources, assetCache);
        assetCache.shutdown();

        ctx.expect(backend.liveMeshInstanceCount() == 0, "bridge release leaked fake mesh instances");
        ctx.expect(backend.liveLightCount() == 0, "bridge release leaked fake lights");
        ctx.expect(TestRenderer::liveMeshCount() == 0, "adapter release leaked renderer meshes");
        ctx.expect(TestRenderer::liveMaterialCount() == 0, "adapter release leaked renderer materials");
        ctx.expect(TestRenderer::liveTextureCount() == 0, "adapter release leaked renderer textures");
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "renderer instance stubs should not be used by adapter tests");
        ctx.expect(TestRenderer::liveLightCount() == 0, "renderer light stubs should not be used by adapter tests");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"FixtureCreatesActorsForAllImportedNodes", fixtureCreatesActorsForAllImportedNodes},
        {"FixturePreservesHierarchyAndWorldTransforms", fixturePreservesHierarchyAndWorldTransforms},
        {"FixtureCreatesMeshComponentsForMeshNodes", fixtureCreatesMeshComponentsForMeshNodes},
        {"FixtureCreatesRendererMeshesAndMaterials", fixtureCreatesRendererMeshesAndMaterials},
        {"FixtureRegistersTextureStats", fixtureRegistersTextureStats},
        {"FixtureCreatesLightComponents", fixtureCreatesLightComponents},
        {"BridgeSyncCreatesInstancesFromAdaptedScene", bridgeSyncCreatesInstancesFromAdaptedScene},
        {"InvalidReferencesAreDiagnosed", invalidReferencesAreDiagnosed},
        {"SkinnedAndAnimatedDataSkipped", skinnedAndAnimatedDataSkipped},
        {"ResourceReleaseIsDeterministic", resourceReleaseIsDeterministic},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
        std::cout << "[authored_scene_adapter] " << name << '\n';
    }

    if (failures.empty()) {
        std::cout << "Authored scene adapter tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Authored scene adapter tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  [" << failure.testName << "] " << failure.message << '\n';
    }
    return 1;
}
