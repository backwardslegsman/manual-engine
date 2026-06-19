#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Scene/SceneRenderBridge.hpp"

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

    bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.0001f)
    {
        return nearlyEqual(lhs.x, rhs.x, epsilon)
            && nearlyEqual(lhs.y, rhs.y, epsilon)
            && nearlyEqual(lhs.z, rhs.z, epsilon);
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

    glm::mat4 translationMatrix(const glm::vec3& translation)
    {
        return glm::translate(glm::mat4{1.0f}, translation);
    }

    class FakeRenderBackend final : public Engine::SceneRenderBackend {
    public:
        struct MeshInstanceRecord {
            bool alive = false;
            Renderer::StaticMeshHandle mesh;
            glm::mat4 transform{1.0f};
            std::optional<Renderer::MaterialHandle> material;
            Renderer::RenderLayer layer = Renderer::RenderLayer::Props;
            Renderer::VisibilityFlags visibility = Renderer::VisibilityFlags::Visible;
            float maxDrawDistance = 0.0f;
            std::optional<Renderer::RenderGroupHandle> renderGroup;
        };

        struct SkinnedInstanceRecord {
            bool alive = false;
            Renderer::SkinnedMeshHandle mesh;
            glm::mat4 transform{1.0f};
            Renderer::RenderLayer layer = Renderer::RenderLayer::Props;
            float maxDrawDistance = 0.0f;
            std::optional<Renderer::RenderGroupHandle> renderGroup;
            std::vector<glm::mat4> jointMatrices;
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
                    meshInstances[index] = {};
                    meshInstances[index].alive = true;
                    meshInstances[index].mesh = mesh;
                    ++createCount;
                    return {index};
                }
            }
            MeshInstanceRecord record;
            record.alive = true;
            record.mesh = mesh;
            meshInstances.push_back(record);
            ++createCount;
            return {static_cast<uint32_t>(meshInstances.size() - 1)};
        }

        void destroyMeshInstance(Renderer::MeshInstanceHandle instance) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id] = {};
                ++destroyCount;
            }
        }

        void setMeshInstanceTransform(Renderer::MeshInstanceHandle instance, const glm::mat4& transform) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].transform = transform;
                ++meshTransformSetCount;
            }
        }

        void setMeshInstanceMaterial(Renderer::MeshInstanceHandle instance, Renderer::MaterialHandle material) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].material = material;
            }
        }

        void clearMeshInstanceMaterial(Renderer::MeshInstanceHandle instance) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].material = std::nullopt;
            }
        }

        void setMeshInstanceRenderLayer(Renderer::MeshInstanceHandle instance, Renderer::RenderLayer layer) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].layer = layer;
            }
        }

        void setMeshInstanceVisibilityFlags(Renderer::MeshInstanceHandle instance, Renderer::VisibilityFlags flags) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].visibility = flags;
            }
        }

        void setMeshInstanceMaxDrawDistance(Renderer::MeshInstanceHandle instance, float maxDrawDistance) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].maxDrawDistance = maxDrawDistance;
            }
        }

        void setMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance, Renderer::RenderGroupHandle group) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].renderGroup = group;
            }
        }

        void clearMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance) override
        {
            if (instance.id < meshInstances.size() && meshInstances[instance.id].alive) {
                meshInstances[instance.id].renderGroup = std::nullopt;
            }
        }

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
                    ++createCount;
                    return {index};
                }
            }
            SkinnedInstanceRecord record;
            record.alive = true;
            record.mesh = mesh;
            skinnedInstances.push_back(record);
            ++createCount;
            return {static_cast<uint32_t>(skinnedInstances.size() - 1)};
        }

        void destroySkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance) override
        {
            if (instance.id < skinnedInstances.size() && skinnedInstances[instance.id].alive) {
                skinnedInstances[instance.id] = {};
                ++destroyCount;
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
                skinnedInstances[instance.id].renderGroup = std::nullopt;
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

        Renderer::LightHandle createLight(const Renderer::LightDescriptor& descriptor) override
        {
            for (uint32_t index = 0; index < lights.size(); ++index) {
                if (!lights[index].alive) {
                    lights[index] = {};
                    lights[index].alive = true;
                    lights[index].descriptor = descriptor;
                    ++createCount;
                    return {index};
                }
            }
            LightRecord record;
            record.alive = true;
            record.descriptor = descriptor;
            lights.push_back(record);
            ++createCount;
            return {static_cast<uint32_t>(lights.size() - 1)};
        }

        void destroyLight(Renderer::LightHandle light) override
        {
            if (light.id < lights.size() && lights[light.id].alive) {
                lights[light.id] = {};
                ++destroyCount;
            }
        }

        void setLightDescriptor(Renderer::LightHandle light, const Renderer::LightDescriptor& descriptor) override
        {
            if (light.id < lights.size() && lights[light.id].alive) {
                lights[light.id].descriptor = descriptor;
                ++lightDescriptorSetCount;
            }
        }

        void resetCounters()
        {
            createCount = 0;
            destroyCount = 0;
            meshTransformSetCount = 0;
            lightDescriptorSetCount = 0;
        }

        uint32_t liveMeshInstances() const
        {
            uint32_t count = 0;
            for (const MeshInstanceRecord& record : meshInstances) {
                count += record.alive ? 1u : 0u;
            }
            return count;
        }

        uint32_t liveSkinnedInstances() const
        {
            uint32_t count = 0;
            for (const SkinnedInstanceRecord& record : skinnedInstances) {
                count += record.alive ? 1u : 0u;
            }
            return count;
        }

        uint32_t liveLights() const
        {
            uint32_t count = 0;
            for (const LightRecord& record : lights) {
                count += record.alive ? 1u : 0u;
            }
            return count;
        }

        std::vector<MeshInstanceRecord> meshInstances;
        std::vector<SkinnedInstanceRecord> skinnedInstances;
        std::vector<LightRecord> lights;
        uint32_t createCount = 0;
        uint32_t destroyCount = 0;
        uint32_t meshTransformSetCount = 0;
        uint32_t lightDescriptorSetCount = 0;
    };

    Engine::SceneMeshComponentDescriptor meshDescriptor(Engine::SceneActorHandle actor, uint32_t meshId = 1)
    {
        Engine::SceneMeshComponentDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.mesh = {meshId};
        descriptor.material = Renderer::MaterialHandle{7};
        descriptor.layer = Renderer::RenderLayer::Transparent;
        descriptor.visibility = Renderer::VisibilityFlags::Visible;
        descriptor.maxDrawDistance = 125.0f;
        descriptor.renderGroup = Renderer::RenderGroupHandle{3};
        return descriptor;
    }

    void attachRejectsInvalidActors(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::SceneActorHandle stale = actor;
        scene.destroyActor(actor);

        ctx.expect(!Engine::isValid(bridge.attachMesh(meshDescriptor({}))), "mesh attach accepted invalid actor");
        ctx.expect(!Engine::isValid(bridge.attachLight({})), "light attach accepted invalid actor");
        ctx.expect(!Engine::isValid(bridge.attachCamera({})), "camera attach accepted invalid actor");
        ctx.expect(!Engine::isValid(bridge.attachSkinnedMesh({})), "skinned attach accepted invalid actor");
        ctx.expect(!Engine::isValid(bridge.attachMesh(meshDescriptor(stale))), "mesh attach accepted pending actor");

        scene.flushDestroyedActors();
        ctx.expect(!Engine::isValid(bridge.attachMesh(meshDescriptor(stale))), "mesh attach accepted stale actor");
        bridge.releaseRendererResources(backend);
    }

    void meshSyncCreatesAndUpdatesInstance(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, {{2.0f, 3.0f, 4.0f}});
        const Engine::SceneMeshComponentHandle mesh = bridge.attachMesh(meshDescriptor(actor));

        bridge.sync(backend);

        ctx.expect(Engine::isValid(mesh), "mesh component handle was invalid");
        ctx.expect(backend.liveMeshInstances() == 1, "mesh sync did not create one instance");
        ctx.expect(backend.meshInstances.size() == 1, "mesh instance storage size was wrong");
        if (!backend.meshInstances.empty()) {
            const FakeRenderBackend::MeshInstanceRecord& instance = backend.meshInstances.front();
            ctx.expect(nearlyEqual(instance.transform, translationMatrix({2.0f, 3.0f, 4.0f})), "mesh transform was wrong");
            ctx.expect(instance.material.has_value() && instance.material->id == 7, "mesh material override was wrong");
            ctx.expect(instance.layer == Renderer::RenderLayer::Transparent, "mesh layer was wrong");
            ctx.expect(instance.visibility == Renderer::VisibilityFlags::Visible, "mesh visibility was wrong");
            ctx.expect(nearlyEqual(instance.maxDrawDistance, 125.0f), "mesh max draw distance was wrong");
            ctx.expect(instance.renderGroup.has_value() && instance.renderGroup->id == 3, "mesh render group was wrong");
        }
        ctx.expect(bridge.diagnostics().liveMeshInstanceCount == 1, "mesh diagnostics live count was wrong");
        bridge.releaseRendererResources(backend);
    }

    void meshTransformUsesHierarchyWorldMatrix(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.setLocalTransform(parent, {{5.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(child, {{1.0f, 2.0f, 3.0f}});
        scene.attachChild(child, parent, false);
        [[maybe_unused]] const Engine::SceneMeshComponentHandle mesh = bridge.attachMesh(meshDescriptor(child));

        bridge.sync(backend);

        ctx.expect(!backend.meshInstances.empty(), "mesh instance was missing");
        if (!backend.meshInstances.empty()) {
            ctx.expect(nearlyEqual(backend.meshInstances.front().transform, translationMatrix({6.0f, 2.0f, 3.0f})),
                "mesh did not receive hierarchy world matrix");
        }
        bridge.releaseRendererResources(backend);
    }

    void meshDescriptorReplacementUpdatesOnlyThatRecord(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle firstActor = scene.createActor();
        const Engine::SceneActorHandle secondActor = scene.createActor();
        const Engine::SceneMeshComponentHandle first = bridge.attachMesh(meshDescriptor(firstActor, 1));
        [[maybe_unused]] const Engine::SceneMeshComponentHandle second = bridge.attachMesh(meshDescriptor(secondActor, 2));
        bridge.sync(backend);
        backend.resetCounters();

        Engine::SceneMeshComponentDescriptor changed = meshDescriptor(firstActor, 1);
        changed.material = Renderer::MaterialHandle{11};
        ctx.expect(bridge.setMeshDescriptor(first, changed), "setMeshDescriptor failed");
        bridge.sync(backend);

        ctx.expect(backend.createCount == 0, "descriptor-only mesh update recreated instances");
        ctx.expect(bridge.diagnostics().updateCount == 1, "descriptor update did not affect exactly one record");
        ctx.expect(backend.meshInstances.size() >= 2, "expected two mesh instances");
        if (backend.meshInstances.size() >= 2) {
            ctx.expect(backend.meshInstances[0].material.has_value() && backend.meshInstances[0].material->id == 11, "changed material was not applied");
            ctx.expect(backend.meshInstances[1].material.has_value() && backend.meshInstances[1].material->id == 7, "unrelated material changed");
        }
        bridge.releaseRendererResources(backend);
    }

    void meshDetachAndActorDestroyReleaseInstance(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle firstActor = scene.createActor();
        const Engine::SceneActorHandle secondActor = scene.createActor();
        const Engine::SceneMeshComponentHandle mesh = bridge.attachMesh(meshDescriptor(firstActor));
        [[maybe_unused]] const Engine::SceneMeshComponentHandle secondMesh = bridge.attachMesh(meshDescriptor(secondActor));
        bridge.sync(backend);

        ctx.expect(bridge.detachMesh(mesh), "detachMesh failed");
        ctx.expect(!bridge.detachMesh(mesh), "stale mesh detach succeeded");
        ctx.expect(backend.liveMeshInstances() == 1, "detach did not release one mesh instance");

        scene.destroyActor(secondActor);
        scene.flushDestroyedActors();
        bridge.sync(backend);
        ctx.expect(backend.liveMeshInstances() == 0, "owner destroy did not release mesh instance");
        bridge.releaseRendererResources(backend);
    }

    void skinnedSyncCreatesInstanceAndPalette(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, {{4.0f, 0.0f, 0.0f}});

        Engine::SceneSkinnedMeshComponentDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.mesh = {9};
        descriptor.layer = Renderer::RenderLayer::Transparent;
        descriptor.maxDrawDistance = 42.0f;
        descriptor.renderGroup = Renderer::RenderGroupHandle{8};
        descriptor.jointMatrices = {glm::mat4{1.0f}, translationMatrix({1.0f, 0.0f, 0.0f})};
        [[maybe_unused]] const Engine::SceneSkinnedMeshComponentHandle skinned = bridge.attachSkinnedMesh(descriptor);
        bridge.sync(backend);

        ctx.expect(backend.liveSkinnedInstances() == 1, "skinned sync did not create one instance");
        ctx.expect(!backend.skinnedInstances.empty(), "skinned instance storage was empty");
        if (!backend.skinnedInstances.empty()) {
            const FakeRenderBackend::SkinnedInstanceRecord& instance = backend.skinnedInstances.front();
            ctx.expect(nearlyEqual(instance.transform, translationMatrix({4.0f, 0.0f, 0.0f})), "skinned transform was wrong");
            ctx.expect(instance.layer == Renderer::RenderLayer::Transparent, "skinned layer was wrong");
            ctx.expect(nearlyEqual(instance.maxDrawDistance, 42.0f), "skinned max draw distance was wrong");
            ctx.expect(instance.renderGroup.has_value() && instance.renderGroup->id == 8, "skinned render group was wrong");
            ctx.expect(instance.jointMatrices.size() == 2, "skinned joint matrix count was wrong");
        }
        bridge.releaseRendererResources(backend);
    }

    void lightSyncUsesActorTransform(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        Engine::SceneTransform transform;
        transform.translation = {1.0f, 2.0f, 3.0f};
        transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
        scene.setLocalTransform(actor, transform);

        Engine::SceneLightComponentDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.type = Renderer::LightType::Spot;
        descriptor.name = "spot";
        descriptor.color = {0.25f, 0.5f, 1.0f};
        descriptor.intensity = 4.0f;
        descriptor.range = 12.0f;
        [[maybe_unused]] const Engine::SceneLightComponentHandle light = bridge.attachLight(descriptor);
        bridge.sync(backend);

        ctx.expect(backend.liveLights() == 1, "light sync did not create one light");
        if (!backend.lights.empty()) {
            const Renderer::LightDescriptor& light = backend.lights.front().descriptor;
            ctx.expect(light.type == Renderer::LightType::Spot, "light type was wrong");
            ctx.expect(nearlyEqual(light.position, {1.0f, 2.0f, 3.0f}), "light position was wrong");
            ctx.expect(nearlyEqual(light.direction, {-1.0f, 0.0f, 0.0f}), "light direction did not use actor -Z");
            ctx.expect(nearlyEqual(light.color, {0.25f, 0.5f, 1.0f}), "light color was wrong");
            ctx.expect(nearlyEqual(light.intensity, 4.0f), "light intensity was wrong");
            ctx.expect(nearlyEqual(light.range, 12.0f), "light range was wrong");
        }
        bridge.releaseRendererResources(backend);
    }

    void lightDisableAndDetach(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        Engine::SceneLightComponentDescriptor descriptor;
        descriptor.actor = actor;
        const Engine::SceneLightComponentHandle light = bridge.attachLight(descriptor);
        bridge.sync(backend);
        backend.resetCounters();

        descriptor.enabled = false;
        ctx.expect(bridge.setLightDescriptor(light, descriptor), "setLightDescriptor failed");
        bridge.sync(backend);
        ctx.expect(backend.liveLights() == 1, "disabled light was destroyed instead of updated");
        ctx.expect(backend.lightDescriptorSetCount == 1, "disabled light descriptor was not updated");
        if (!backend.lights.empty()) {
            ctx.expect(!backend.lights.front().descriptor.enabled, "disabled light descriptor stayed enabled");
        }

        ctx.expect(bridge.detachLight(light), "detachLight failed");
        ctx.expect(backend.liveLights() == 0, "detachLight did not destroy light");
        bridge.releaseRendererResources(backend);
    }

    void cameraBuildRenderView(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, {{0.0f, 2.0f, 5.0f}});

        Engine::SceneCameraComponentDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.verticalFieldOfViewRadians = glm::radians(70.0f);
        descriptor.nearPlane = 0.2f;
        descriptor.farPlane = 500.0f;
        descriptor.layerMask = static_cast<uint32_t>(Renderer::RenderLayer::Props);
        descriptor.enableDistanceCulling = false;
        const Engine::SceneCameraComponentHandle camera = bridge.attachCamera(descriptor);

        const std::optional<Renderer::RenderView> view = bridge.buildRenderView(camera, 3, 1280, 720);
        ctx.expect(view.has_value(), "buildRenderView failed");
        if (view) {
            ctx.expect(view->viewId == 3, "view ID was wrong");
            ctx.expect(view->viewportWidth == 1280 && view->viewportHeight == 720, "viewport was wrong");
            ctx.expect(view->layerMask == static_cast<uint32_t>(Renderer::RenderLayer::Props), "layer mask was wrong");
            ctx.expect(!view->enableDistanceCulling, "distance culling flag was wrong");
            ctx.expect(nearlyEqual(view->cameraPosition, {0.0f, 2.0f, 5.0f}), "camera position was wrong");
            ctx.expect(nearlyEqual(view->viewProjection, view->projection * view->view), "viewProjection was wrong");
        }
        bridge.releaseRendererResources(backend);
    }

    void preRenderSystemSyncsAfterTransformRefresh(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        [[maybe_unused]] const Engine::SceneMeshComponentHandle mesh = bridge.attachMesh(meshDescriptor(actor));

        Engine::SceneSystemDescriptor updater;
        updater.phases = {Engine::SceneTickPhase::VariableUpdate};
        updater.onTick = [actor](Engine::Scene& tickScene, const Engine::SceneTickContext&) {
            tickScene.setLocalTransform(actor, {{9.0f, 0.0f, 0.0f}});
        };
        [[maybe_unused]] const Engine::SceneSystemHandle updaterSystem = scene.registerSystem(std::move(updater));
        const Engine::SceneSystemHandle bridgeSystem = bridge.registerPreRenderSystem(backend);

        ctx.expect(Engine::isValid(bridgeSystem), "bridge pre-render system registration failed");
        scene.load();
        scene.start();
        scene.tickFrame(0.016f);

        ctx.expect(backend.liveMeshInstances() == 1, "pre-render sync did not create mesh instance");
        if (!backend.meshInstances.empty()) {
            ctx.expect(nearlyEqual(backend.meshInstances.front().transform, translationMatrix({9.0f, 0.0f, 0.0f})),
                "pre-render sync did not observe refreshed transform");
        }
        scene.stop();
        bridge.releaseRendererResources(backend);
    }

    void releaseRendererResourcesIsIdempotent(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        [[maybe_unused]] const Engine::SceneMeshComponentHandle mesh = bridge.attachMesh(meshDescriptor(actor));
        Engine::SceneLightComponentDescriptor lightDescriptor;
        lightDescriptor.actor = actor;
        [[maybe_unused]] const Engine::SceneLightComponentHandle light = bridge.attachLight(lightDescriptor);
        bridge.sync(backend);

        bridge.releaseRendererResources(backend);
        bridge.releaseRendererResources(backend);

        ctx.expect(backend.liveMeshInstances() == 0, "idempotent release left live mesh instance");
        ctx.expect(backend.liveLights() == 0, "idempotent release left live light");
    }

    void diagnosticsReportCounts(TestContext& ctx)
    {
        Engine::Scene scene;
        FakeRenderBackend backend;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        [[maybe_unused]] const Engine::SceneMeshComponentHandle validMesh = bridge.attachMesh(meshDescriptor(actor));
        [[maybe_unused]] const Engine::SceneMeshComponentHandle invalidMesh = bridge.attachMesh(meshDescriptor(actor, UINT32_MAX));
        Engine::SceneLightComponentDescriptor lightDescriptor;
        lightDescriptor.actor = actor;
        [[maybe_unused]] const Engine::SceneLightComponentHandle light = bridge.attachLight(lightDescriptor);
        Engine::SceneCameraComponentDescriptor cameraDescriptor;
        cameraDescriptor.actor = actor;
        [[maybe_unused]] const Engine::SceneCameraComponentHandle camera = bridge.attachCamera(cameraDescriptor);

        bridge.sync(backend);
        const Engine::SceneRenderBridgeDiagnostics diagnostics = bridge.diagnostics();
        ctx.expect(diagnostics.meshComponentCount == 2, "diagnostics mesh component count was wrong");
        ctx.expect(diagnostics.liveMeshInstanceCount == 1, "diagnostics live mesh count was wrong");
        ctx.expect(diagnostics.lightComponentCount == 1, "diagnostics light component count was wrong");
        ctx.expect(diagnostics.liveLightCount == 1, "diagnostics live light count was wrong");
        ctx.expect(diagnostics.cameraComponentCount == 1, "diagnostics camera component count was wrong");
        ctx.expect(diagnostics.missingResourceCount == 1, "diagnostics missing resource count was wrong");
        ctx.expect(!diagnostics.warnings.empty(), "diagnostics warnings were missing");
        bridge.releaseRendererResources(backend);
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"AttachRejectsInvalidActors", attachRejectsInvalidActors},
        {"MeshSyncCreatesAndUpdatesInstance", meshSyncCreatesAndUpdatesInstance},
        {"MeshTransformUsesHierarchyWorldMatrix", meshTransformUsesHierarchyWorldMatrix},
        {"MeshDescriptorReplacementUpdatesOnlyThatRecord", meshDescriptorReplacementUpdatesOnlyThatRecord},
        {"MeshDetachAndActorDestroyReleaseInstance", meshDetachAndActorDestroyReleaseInstance},
        {"SkinnedSyncCreatesInstanceAndPalette", skinnedSyncCreatesInstanceAndPalette},
        {"LightSyncUsesActorTransform", lightSyncUsesActorTransform},
        {"LightDisableAndDetach", lightDisableAndDetach},
        {"CameraBuildRenderView", cameraBuildRenderView},
        {"PreRenderSystemSyncsAfterTransformRefresh", preRenderSystemSyncsAfterTransformRefresh},
        {"ReleaseRendererResourcesIsIdempotent", releaseRendererResourcesIsIdempotent},
        {"DiagnosticsReportCounts", diagnosticsReportCounts},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
    }

    if (failures.empty()) {
        std::cout << "Scene render bridge tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Scene render bridge tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  " << failure.testName << ": " << failure.message << '\n';
    }
    return 1;
}
