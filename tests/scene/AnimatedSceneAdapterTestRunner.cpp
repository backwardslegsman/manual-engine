#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AnimationPose.hpp"
#include "Engine/AnimatedModelPose.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Scene/AnimatedSceneAdapter.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"

namespace TestRenderer {
    void reset();
    uint32_t liveSkinnedMeshCount();
    uint32_t liveMaterialCount();
    uint32_t liveTextureCount();
    Renderer::SkinnedMeshDescriptor firstSkinnedMeshDescriptor();
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

    bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool nearlyEqual(const glm::mat4& lhs, const glm::mat4& rhs, float epsilon = 0.0001f)
    {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!nearlyEqual(lhs[column][row], rhs[column][row], epsilon)) {
                    return false;
                }
            }
        }
        return true;
    }

    class FakeRenderBackend final : public Engine::SceneRenderBackend {
    public:
        struct SkinnedInstanceRecord {
            bool alive = false;
            Renderer::SkinnedMeshHandle mesh;
            glm::mat4 transform{1.0f};
            Renderer::RenderLayer layer = Renderer::RenderLayer::Props;
            float maxDrawDistance = 0.0f;
            std::optional<Renderer::RenderGroupHandle> renderGroup;
            std::vector<glm::mat4> jointMatrices;
        };

        Renderer::MeshInstanceHandle createMeshInstance(Renderer::StaticMeshHandle) override { return {}; }
        void destroyMeshInstance(Renderer::MeshInstanceHandle) override {}
        void setMeshInstanceTransform(Renderer::MeshInstanceHandle, const glm::mat4&) override {}
        void setMeshInstanceMaterial(Renderer::MeshInstanceHandle, Renderer::MaterialHandle) override {}
        void clearMeshInstanceMaterial(Renderer::MeshInstanceHandle) override {}
        void setMeshInstanceRenderLayer(Renderer::MeshInstanceHandle, Renderer::RenderLayer) override {}
        void setMeshInstanceVisibilityFlags(Renderer::MeshInstanceHandle, Renderer::VisibilityFlags) override {}
        void setMeshInstanceMaxDrawDistance(Renderer::MeshInstanceHandle, float) override {}
        void setMeshInstanceRenderGroup(Renderer::MeshInstanceHandle, Renderer::RenderGroupHandle) override {}
        void clearMeshInstanceRenderGroup(Renderer::MeshInstanceHandle) override {}

        Renderer::SkinnedMeshInstanceHandle createSkinnedMeshInstance(Renderer::SkinnedMeshHandle mesh) override
        {
            if (mesh.id == UINT32_MAX) {
                return {};
            }
            for (uint32_t index = 0; index < skinnedInstances.size(); ++index) {
                if (!skinnedInstances[index].alive) {
                    skinnedInstances[index] = {};
                    skinnedInstances[index].alive = true;
                    skinnedInstances[index].mesh = mesh;
                    return {index};
                }
            }
            SkinnedInstanceRecord record;
            record.alive = true;
            record.mesh = mesh;
            skinnedInstances.push_back(record);
            return {static_cast<uint32_t>(skinnedInstances.size() - 1)};
        }

        void destroySkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance) override
        {
            if (instance.id < skinnedInstances.size()) {
                skinnedInstances[instance.id] = {};
            }
        }

        void setSkinnedMeshInstanceTransform(Renderer::SkinnedMeshInstanceHandle instance, const glm::mat4& transform) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id].transform = transform;
            }
        }

        void setSkinnedMeshInstanceRenderLayer(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderLayer layer) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id].layer = layer;
            }
        }

        void setSkinnedMeshInstanceMaxDrawDistance(Renderer::SkinnedMeshInstanceHandle instance, float maxDrawDistance) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id].maxDrawDistance = maxDrawDistance;
            }
        }

        void setSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderGroupHandle group) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id].renderGroup = group;
            }
        }

        void clearSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id].renderGroup.reset();
            }
        }

        void setSkinnedMeshInstanceJointMatrices(
            Renderer::SkinnedMeshInstanceHandle instance,
            const std::vector<glm::mat4>& matrices) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id].jointMatrices = matrices;
            }
        }

        Renderer::LightHandle createLight(const Renderer::LightDescriptor&) override { return {}; }
        void destroyLight(Renderer::LightHandle) override {}
        void setLightDescriptor(Renderer::LightHandle, const Renderer::LightDescriptor&) override {}

        uint32_t liveSkinnedInstanceCount() const
        {
            uint32_t count = 0;
            for (const SkinnedInstanceRecord& instance : skinnedInstances) {
                count += instance.alive ? 1u : 0u;
            }
            return count;
        }

        std::vector<SkinnedInstanceRecord> skinnedInstances;
    };

    std::filesystem::path skinnedFixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/skinned_animation_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }
        const std::filesystem::path parentRelative = "../tests/assets/fixtures/skinned_animation_fixture.gltf";
        if (std::filesystem::exists(parentRelative)) {
            return parentRelative;
        }
        return std::filesystem::path{"../../tests/assets/fixtures/skinned_animation_fixture.gltf"};
    }

    std::filesystem::path kayKitReleasePath()
    {
        const std::filesystem::path sourceRelative =
            "assets/KayKit_Adventurers_2.0_FREE/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }
        const std::filesystem::path parentRelative =
            "../assets/KayKit_Adventurers_2.0_FREE/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb";
        if (std::filesystem::exists(parentRelative)) {
            return parentRelative;
        }
        return std::filesystem::path{
            "../../assets/KayKit_Adventurers_2.0_FREE/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb"};
    }

    std::filesystem::path staticFixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }
        const std::filesystem::path parentRelative = "../tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(parentRelative)) {
            return parentRelative;
        }
        return std::filesystem::path{"../../tests/assets/fixtures/authored_scene_fixture.gltf"};
    }

    Assets::Assimp::ImportedScene importSkinnedFixture()
    {
        return Assets::Assimp::importScene(skinnedFixturePath());
    }

    Engine::SceneAnimatedAdapterSettings testSettings()
    {
        Engine::SceneAnimatedAdapterSettings settings;
        settings.loadTextures = false;
        settings.playOnStart = true;
        settings.loop = true;
        return settings;
    }

    struct AdapterFixture {
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge{scene};
        Engine::SceneAnimatedModelAdapter adapter{scene, bridge};
        Engine::AssetCache cache;
    };

    Engine::SceneAnimatedAdapterResult adaptFixture(AdapterFixture& fixture)
    {
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        return fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());
    }

    void fixtureCreatesActorsForAllImportedNodes(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        context.expect(result.success, "animated fixture adapts successfully");
        context.expect(result.nodes.size() == imported.nodes.size(), "one actor binding is created for each imported node");
        for (const Engine::SceneAnimatedNodeBinding& binding : result.nodes) {
            context.expect(fixture.scene.contains(binding.actor), "bound actor is live");
        }

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void fixturePreservesSkeletonHierarchy(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
            const std::optional<glm::mat4> world = fixture.scene.worldMatrix(result.nodes[nodeIndex].actor);
            context.expect(world.has_value(), "adapted node has a world matrix");
            if (world.has_value()) {
                context.expect(nearlyEqual(*world, imported.nodes[nodeIndex].worldTransform, 0.001f), "adapted node world matches import");
            }
        }

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void fixtureCreatesSkeletonBindings(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        context.expect(result.skeletons.size() == imported.skins.size(), "one skeleton binding is created per imported skin");
        for (const Engine::SceneSkeletonBinding& binding : result.skeletons) {
            context.expect(Engine::isValid(binding.skeleton), "skeleton handle is valid");
            context.expect(!binding.jointActors.empty(), "skeleton binding carries joint actors");
        }

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void fixtureCreatesSkinnedMeshComponents(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        context.expect(!result.skinnedMeshes.empty(), "skinned mesh component bindings are created");
        for (const Engine::SceneAnimatedMeshBinding& binding : result.skinnedMeshes) {
            context.expect(Engine::isValid(binding.component), "skinned mesh component handle is valid");
        }
        context.expect(result.diagnostics.createdSkinnedComponentCount == result.skinnedMeshes.size(), "component diagnostics match bindings");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void rootFallbackDisabledRejectsUnboundSkinnedResources(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        for (Assets::Assimp::ImportedSceneNode& node : imported.nodes) {
            node.meshIndices.clear();
        }

        Engine::SceneAnimatedAdapterSettings settings = testSettings();
        settings.allowRootFallbackSkinnedBindings = false;
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, settings);

        context.expect(!result.success, "root fallback disabled rejects unbound skinned resources");
        context.expect(result.diagnostics.createdSkinnedMeshCount > 0, "skinned resources were created before binding failed");
        context.expect(result.diagnostics.createdSkinnedComponentCount == 0, "no fallback skinned components were created");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void rootFallbackEnabledCreatesComponents(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        for (Assets::Assimp::ImportedSceneNode& node : imported.nodes) {
            node.meshIndices.clear();
        }

        Engine::SceneAnimatedAdapterSettings settings = testSettings();
        settings.allowRootFallbackSkinnedBindings = true;
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, settings);

        context.expect(result.success, "root fallback enabled adapts unbound skinned resources");
        context.expect(result.diagnostics.createdSkinnedComponentCount == result.diagnostics.createdSkinnedMeshCount,
            "fallback creates one skinned component per valid skinned mesh");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void fixtureCreatesRendererSkinnedMeshesAndMaterials(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        context.expect(TestRenderer::liveSkinnedMeshCount() == result.diagnostics.createdSkinnedMeshCount, "renderer skinned meshes are owned by adapter resources");
        context.expect(TestRenderer::liveMaterialCount() == result.diagnostics.createdMaterialCount, "renderer materials are owned by adapter resources");
        const Renderer::SkinnedMeshDescriptor descriptor = TestRenderer::firstSkinnedMeshDescriptor();
        context.expect(!descriptor.submeshes.empty(), "renderer stub received skinned submesh descriptors");
        context.expect(descriptor.jointCount == result.diagnostics.importedJointCount, "renderer skinned mesh joint count matches import");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void bindPoseMatchesAnimatedModelRuntime(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        const Engine::AnimatedSkeletonPose helperPose = Engine::sampleImportedSceneBindPose(imported, true);
        const std::optional<Engine::AnimatedSkeletonPose> adapterPose = fixture.adapter.lastPose(result.animator);
        context.expect(adapterPose.has_value(), "adapter exposes last sampled pose");
        context.expect(helperPose.joints.size() == adapterPose->joints.size(), "adapter pose joint count matches helper");
        const std::vector<glm::mat4> bindPalette = Engine::animatedBindPosePalette(imported);
        context.expect(bindPalette.size() == helperPose.joints.size(), "bind palette size matches helper pose");
        for (uint32_t jointIndex = 0; jointIndex < helperPose.joints.size(); ++jointIndex) {
            context.expect(
                nearlyEqual(bindPalette[jointIndex], helperPose.joints[jointIndex].finalSkinningMatrix, 0.001f),
                "bind palette matches helper final skinning matrix");
        }

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void sampledClipMatchesAnimatedModelRuntime(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());
        fixture.adapter.updateAnimator(result.animator, 0.25f);

        const Engine::AnimatedSkeletonPose helperPose = Engine::sampleImportedSceneClip(imported, true, 0, 0.25f);
        const std::optional<Engine::AnimatedSkeletonPose> adapterPose = fixture.adapter.lastPose(result.animator);
        context.expect(adapterPose.has_value(), "adapter exposes sampled pose");
        context.expect(helperPose.joints.size() == adapterPose->joints.size(), "sampled adapter pose joint count matches helper");
        if (!helperPose.joints.empty() && adapterPose.has_value()) {
            context.expect(
                nearlyEqual(helperPose.joints.front().finalSkinningMatrix, adapterPose->joints.front().finalSkinningMatrix, 0.001f),
                "sampled adapter pose matrix matches helper");
        }

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void animatorAdvancesPlayback(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Engine::SceneAnimatedAdapterResult result = adaptFixture(fixture);
        const std::optional<Engine::SceneAnimatorState> before = fixture.adapter.animatorState(result.animator);
        fixture.adapter.updateAnimator(result.animator, 0.1f);
        const std::optional<Engine::SceneAnimatorState> after = fixture.adapter.animatorState(result.animator);

        context.expect(before.has_value() && after.has_value(), "animator state can be queried");
        context.expect(after->timeSeconds > before->timeSeconds, "animator update advances playback time");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void animatorCrossfadeUpdatesPalette(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Engine::SceneAnimatedAdapterResult result = adaptFixture(fixture);
        const bool began = fixture.adapter.beginCrossfade(result.animator, 0, 0.25f);
        const bool updated = fixture.adapter.updateAnimator(result.animator, 0.1f);
        const std::optional<Engine::SceneAnimatorState> state = fixture.adapter.animatorState(result.animator);
        const std::optional<Engine::AnimatedSkeletonPose> pose = fixture.adapter.lastPose(result.animator);

        context.expect(began, "crossfade can begin against valid clip");
        context.expect(updated, "crossfade update samples a pose");
        context.expect(state.has_value(), "crossfade update keeps animator state queryable");
        context.expect(pose.has_value() && pose->diagnostics.valid, "crossfade update keeps a valid palette");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void bridgeSyncCreatesSkinnedInstances(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        FakeRenderBackend backend;
        Engine::SceneAnimatedAdapterResult result = adaptFixture(fixture);
        fixture.bridge.sync(backend);

        context.expect(backend.liveSkinnedInstanceCount() == result.skinnedMeshes.size(), "bridge sync creates skinned instances");
        context.expect(!backend.skinnedInstances.empty() && !backend.skinnedInstances.front().jointMatrices.empty(), "bridge receives joint palette");

        fixture.bridge.releaseRendererResources(backend);
        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void skinnedPaletteIsRelativeToComponentActor(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        FakeRenderBackend backend;
        Engine::SceneAnimatedAdapterResult result = adaptFixture(fixture);
        context.expect(!result.skinnedMeshes.empty(), "fixture creates skinned mesh bindings");
        if (result.skinnedMeshes.empty()) {
            fixture.adapter.releaseResources(result.resources, fixture.cache);
            return;
        }

        const uint32_t ownerNodeIndex = result.skinnedMeshes.front().importedNodeIndex;
        context.expect(ownerNodeIndex < result.nodes.size(), "skinned binding has a valid owner node");
        if (ownerNodeIndex >= result.nodes.size()) {
            fixture.adapter.releaseResources(result.resources, fixture.cache);
            return;
        }
        const Engine::SceneActorHandle ownerActor = result.nodes[ownerNodeIndex].actor;

        Engine::SceneTransform transform;
        transform.translation = {3.0f, 4.0f, 5.0f};
        transform.scale = {2.0f, 2.0f, 2.0f};
        context.expect(fixture.scene.setLocalTransform(ownerActor, transform), "owner transform can be changed");
        fixture.scene.updateWorldTransforms();
        context.expect(fixture.adapter.updateAnimator(result.animator, 0.25f), "animator updates after owner transform");
        fixture.bridge.sync(backend);

        const std::optional<Engine::AnimatedSkeletonPose> pose = fixture.adapter.lastPose(result.animator);
        const std::optional<glm::mat4> ownerWorld = fixture.scene.worldMatrix(ownerActor);
        context.expect(pose.has_value() && ownerWorld.has_value(), "pose and owner world are available");
        context.expect(!backend.skinnedInstances.empty(), "backend received skinned instance");
        if (pose.has_value() && ownerWorld.has_value() && !pose->joints.empty() && !backend.skinnedInstances.empty()) {
            const glm::mat4 expected = glm::inverse(*ownerWorld) * pose->joints.front().finalSkinningMatrix;
            context.expect(!backend.skinnedInstances.front().jointMatrices.empty(), "backend received joint matrices");
            if (!backend.skinnedInstances.front().jointMatrices.empty()) {
                context.expect(
                    nearlyEqual(backend.skinnedInstances.front().jointMatrices.front(), expected, 0.001f),
                    "joint palette is relative to skinned component actor");
            }
        }

        fixture.bridge.releaseRendererResources(backend);
        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void schedulerVariableAnimationUpdatesBeforePreRender(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        FakeRenderBackend backend;
        Engine::SceneAnimatedAdapterResult result = adaptFixture(fixture);
        context.expect(Engine::isValid(fixture.adapter.registerVariableAnimationSystem()), "animation scheduler system registers");
        context.expect(Engine::isValid(fixture.bridge.registerPreRenderSystem(backend)), "bridge pre-render system registers");
        context.expect(fixture.scene.load(), "scene loads");
        context.expect(fixture.scene.start(), "scene starts");
        fixture.scene.tickFrame(0.2f);

        context.expect(backend.liveSkinnedInstanceCount() == result.skinnedMeshes.size(), "pre-render bridge sync creates instances after animation tick");
        const std::optional<Engine::SceneAnimatorState> state = fixture.adapter.animatorState(result.animator);
        context.expect(state.has_value() && state->timeSeconds > 0.0f, "variable animation system advanced animator");

        fixture.scene.stop();
        fixture.bridge.releaseRendererResources(backend);
        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void invalidReferencesAreDiagnosed(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = importSkinnedFixture();
        if (!imported.nodes.empty()) {
            imported.nodes.front().meshIndices.push_back(9999);
        }
        if (!imported.skins.empty()) {
            imported.skins.front().jointIndices.push_back(9999);
        }
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, skinnedFixturePath(), fixture.cache, testSettings());

        context.expect(result.success, "adapter survives invalid references when the remaining asset is usable");
        context.expect(result.diagnostics.invalidMeshReferenceCount > 0, "invalid mesh references are diagnosed");
        context.expect(result.diagnostics.invalidJointReferenceCount > 0, "invalid joint references are diagnosed");

        fixture.adapter.releaseResources(result.resources, fixture.cache);
    }

    void staticSceneRejectedCleanly(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(staticFixturePath());
        Engine::SceneAnimatedAdapterResult result =
            fixture.adapter.adaptImportedScene(imported, staticFixturePath(), fixture.cache, testSettings());

        context.expect(!result.success, "static authored fixture is rejected by animated adapter");
        context.expect(result.resources.skinnedMeshes.empty(), "rejected static scene creates no skinned resources");
    }

    void optionalKayKitClipMovesJoints(TestContext& context)
    {
        const std::filesystem::path path = kayKitReleasePath();
        if (!std::filesystem::exists(path)) {
            return;
        }

        const Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(path);
        context.expect(imported.success, "KayKit release sample imports");
        if (!imported.success || imported.animations.empty()) {
            return;
        }

        const Engine::AnimatedSkeletonPose bindPose = Engine::sampleImportedSceneBindPose(imported, true);
        const Engine::AnimatedSkeletonPose clipPose = Engine::sampleImportedSceneClip(imported, true, 0, 0.25f);
        context.expect(bindPose.diagnostics.valid, "KayKit bind pose is valid");
        context.expect(clipPose.diagnostics.valid, "KayKit clip pose is valid");
        context.expect(bindPose.joints.size() == clipPose.joints.size(), "KayKit pose joint counts match");

        uint32_t movedJointCount = 0;
        for (uint32_t jointIndex = 0; jointIndex < bindPose.joints.size() && jointIndex < clipPose.joints.size(); ++jointIndex) {
            if (!nearlyEqual(bindPose.joints[jointIndex].modelTransform, clipPose.joints[jointIndex].modelTransform, 0.0001f)) {
                ++movedJointCount;
            }
        }
        std::cout << "[scene animated] KayKit clip0 moved joints: " << movedJointCount
                  << " / " << bindPose.joints.size()
                  << " duration " << imported.animations.front().durationSeconds
                  << " channels " << imported.animations.front().channels.size()
                  << " warnings " << imported.diagnostics.warnings.size()
                  << '\n';
        context.expect(movedJointCount > 0, "KayKit clip 0 moves imported joints");
    }

    void resourceReleaseIsDeterministic(TestContext& context)
    {
        TestRenderer::reset();
        AdapterFixture fixture;
        FakeRenderBackend backend;
        Engine::SceneAnimatedAdapterResult result = adaptFixture(fixture);
        fixture.bridge.sync(backend);
        fixture.bridge.releaseRendererResources(backend);
        fixture.bridge.releaseRendererResources(backend);
        fixture.adapter.releaseResources(result.resources, fixture.cache);
        fixture.adapter.releaseResources(result.resources, fixture.cache);

        context.expect(backend.liveSkinnedInstanceCount() == 0, "bridge releases skinned instances once");
        context.expect(TestRenderer::liveSkinnedMeshCount() == 0, "adapter releases skinned meshes");
        context.expect(TestRenderer::liveMaterialCount() == 0, "adapter releases materials");
        context.expect(TestRenderer::liveTextureCount() == 0, "adapter releases cached textures");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"FixtureCreatesActorsForAllImportedNodes", fixtureCreatesActorsForAllImportedNodes},
        {"FixturePreservesSkeletonHierarchy", fixturePreservesSkeletonHierarchy},
        {"FixtureCreatesSkeletonBindings", fixtureCreatesSkeletonBindings},
        {"FixtureCreatesSkinnedMeshComponents", fixtureCreatesSkinnedMeshComponents},
        {"RootFallbackDisabledRejectsUnboundSkinnedResources", rootFallbackDisabledRejectsUnboundSkinnedResources},
        {"RootFallbackEnabledCreatesComponents", rootFallbackEnabledCreatesComponents},
        {"FixtureCreatesRendererSkinnedMeshesAndMaterials", fixtureCreatesRendererSkinnedMeshesAndMaterials},
        {"BindPoseMatchesAnimatedModelRuntime", bindPoseMatchesAnimatedModelRuntime},
        {"SampledClipMatchesAnimatedModelRuntime", sampledClipMatchesAnimatedModelRuntime},
        {"AnimatorAdvancesPlayback", animatorAdvancesPlayback},
        {"AnimatorCrossfadeUpdatesPalette", animatorCrossfadeUpdatesPalette},
        {"BridgeSyncCreatesSkinnedInstances", bridgeSyncCreatesSkinnedInstances},
        {"SkinnedPaletteIsRelativeToComponentActor", skinnedPaletteIsRelativeToComponentActor},
        {"SchedulerVariableAnimationUpdatesBeforePreRender", schedulerVariableAnimationUpdatesBeforePreRender},
        {"InvalidReferencesAreDiagnosed", invalidReferencesAreDiagnosed},
        {"StaticSceneRejectedCleanly", staticSceneRejectedCleanly},
        {"OptionalKayKitClipMovesJoints", optionalKayKitClipMovesJoints},
        {"ResourceReleaseIsDeterministic", resourceReleaseIsDeterministic},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{std::string{name}, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << "[FAIL] " << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Animated scene adapter tests passed (" << tests.size() << ")\n";
    return 0;
}
