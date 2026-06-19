#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"
#include "Engine/SceneCharacterMovement.hpp"
#include "Engine/SceneReflection.hpp"
#include "Engine/TerrainDataset.hpp"

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

    bool near(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool near(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.0001f)
    {
        return near(lhs.x, rhs.x, epsilon) && near(lhs.y, rhs.y, epsilon) && near(lhs.z, rhs.z, epsilon);
    }

    Engine::ScenePhysicsShapeDescriptor box(const glm::vec3& halfExtents)
    {
        Engine::ScenePhysicsShapeDescriptor shape;
        shape.type = Engine::ScenePhysicsShapeType::Box;
        shape.box.halfExtents = halfExtents;
        return shape;
    }

    void RegistryDescriptorsAreDeterministic(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry;
        Engine::registerSceneReflectionDescriptors(registry);

        const auto objects = registry.objects();
        ctx.expect(!objects.empty(), "scene descriptors were registered");
        ctx.expect(objects.front().id == static_cast<uint32_t>(Engine::SceneReflectedObjectId::SceneActor), "objects are sorted by id");
        ctx.expect(registry.property(
            static_cast<uint32_t>(Engine::SceneReflectedObjectId::SceneActor),
            static_cast<uint32_t>(Engine::SceneReflectedPropertyId::LocalTranslation)) != nullptr,
            "actor local translation property registered");

        Engine::ReflectedObjectDescriptor duplicate;
        duplicate.id = static_cast<uint32_t>(Engine::SceneReflectedObjectId::SceneActor);
        duplicate.name = "SceneActor";
        duplicate.properties.push_back({
            static_cast<uint32_t>(Engine::SceneReflectedPropertyId::Enabled),
            "enabled",
            "enabled",
            {},
            Engine::ReflectedValueType::Bool,
            true});
        ctx.expect(registry.registerObject(duplicate) == Engine::ReflectionStatus::DuplicateObject, "duplicate object rejected");

        Engine::ReflectedObjectDescriptor bad;
        bad.id = 9000;
        bad.name = "Bad";
        bad.properties.push_back({1, "same", "same", {}, Engine::ReflectedValueType::Bool, false});
        bad.properties.push_back({1, "sameAgain", "sameAgain", {}, Engine::ReflectedValueType::Bool, false});
        ctx.expect(registry.registerObject(bad) == Engine::ReflectionStatus::DuplicateProperty, "duplicate property rejected");
    }

    void OpaqueHandleValidation(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        Engine::SceneReflectionContext context;
        context.scene = &scene;

        const Engine::OpaqueHandle opaque = Engine::toOpaque(actor);
        const auto roundTrip = Engine::sceneActorFromOpaque(context, opaque);
        ctx.expect(roundTrip.has_value() && *roundTrip == actor, "valid scene actor opaque handle converts back");

        Engine::OpaqueHandle wrongOwner = opaque;
        wrongOwner.owner = 55;
        ctx.expect(!Engine::sceneActorFromOpaque(context, wrongOwner).has_value(), "wrong owner rejects conversion");

        scene.destroyActor(actor);
        ctx.expect(!Engine::sceneActorFromOpaque(context, opaque).has_value(), "pending destroy actor rejects conversion");
        scene.flushDestroyedActors();
        const Engine::SceneActorHandle reused = scene.createActor();
        ctx.expect(reused.index == actor.index, "actor slot reused for stale-handle test");
        ctx.expect(!Engine::sceneActorFromOpaque(context, opaque).has_value(), "stale actor opaque handle rejects conversion");
    }

    void SceneTransformReflectionWritesThroughScene(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor(Engine::SceneObjectId{123});
        scene.attachChild(child, parent, false);

        Engine::SceneReflectionContext context;
        context.scene = &scene;

        const Engine::OpaqueHandle childHandle = Engine::toOpaque(child);
        const Engine::ReflectionResult setResult = Engine::setReflectedProperty(
            context,
            childHandle,
            Engine::SceneReflectedPropertyId::LocalTranslation,
            glm::vec3{3.0f, 4.0f, 5.0f});
        ctx.expect(setResult.status == Engine::ReflectionStatus::Success && setResult.changed, "local translation write succeeded");

        const Engine::ReflectionResult translation = Engine::getReflectedProperty(
            context,
            childHandle,
            Engine::SceneReflectedPropertyId::LocalTranslation);
        ctx.expect(translation.status == Engine::ReflectionStatus::Success, "local translation read succeeded");
        ctx.expect(near(std::get<glm::vec3>(translation.value), glm::vec3{3.0f, 4.0f, 5.0f}), "local translation updated");

        const Engine::ReflectionResult stable = Engine::getReflectedProperty(
            context,
            childHandle,
            Engine::SceneReflectedPropertyId::StableId);
        ctx.expect(std::get<Engine::SceneObjectId>(stable.value).value == 123, "stable scene id is reflected");

        const Engine::ReflectionResult readOnly = Engine::setReflectedProperty(
            context,
            childHandle,
            Engine::SceneReflectedPropertyId::WorldMatrix,
            glm::mat4{1.0f});
        ctx.expect(readOnly.status == Engine::ReflectionStatus::ReadOnly, "world matrix is read-only");
    }

    void RenderBridgeReflectionWritesDescriptor(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        const Engine::SceneActorHandle actor = scene.createActor();

        Engine::SceneMeshComponentDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.mesh = Renderer::StaticMeshHandle{7};
        descriptor.maxDrawDistance = 10.0f;
        const Engine::SceneMeshComponentHandle mesh = bridge.attachMesh(descriptor);

        Engine::SceneReflectionContext context;
        context.scene = &scene;
        context.renderBridge = &bridge;

        const Engine::ReflectionResult setResult = Engine::setReflectedProperty(
            context,
            Engine::toOpaque(mesh),
            Engine::SceneReflectedPropertyId::MaxDrawDistance,
            42.0f);
        ctx.expect(setResult.status == Engine::ReflectionStatus::Success, "mesh max distance write succeeded");
        ctx.expect(near(bridge.meshDescriptor(mesh)->maxDrawDistance, 42.0f), "mesh descriptor was updated through bridge API");

        const Engine::ReflectionResult meshId = Engine::getReflectedProperty(
            context,
            Engine::toOpaque(mesh),
            Engine::SceneReflectedPropertyId::MeshResourceId);
        ctx.expect(std::get<uint64_t>(meshId.value) == 7, "mesh resource id reflected");

        const Engine::ReflectionResult badType = Engine::setReflectedProperty(
            context,
            Engine::toOpaque(mesh),
            Engine::SceneReflectedPropertyId::Enabled,
            1.0f);
        ctx.expect(badType.status == Engine::ReflectionStatus::TypeMismatch, "wrong value type rejected");
    }

    void PhysicsAndCharacterReflectionUsePublicApis(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle floorActor = scene.createActor();
        Engine::ScenePhysicsBodyDescriptor floorDescriptor;
        floorDescriptor.actor = floorActor;
        floorDescriptor.motionType = Engine::ScenePhysicsMotionType::Static;
        const Engine::ScenePhysicsBodyHandle floorBody = physics.createBody(floorDescriptor);
        const Engine::SceneColliderHandle floorCollider = physics.attachCollider(floorBody, box({2.0f, 0.25f, 2.0f}));

        Engine::SceneReflectionContext context;
        context.scene = &scene;
        context.physics = &physics;

        const Engine::ReflectionResult enabled = Engine::setReflectedProperty(
            context,
            Engine::toOpaque(floorBody),
            Engine::SceneReflectedPropertyId::Enabled,
            false);
        ctx.expect(enabled.status == Engine::ReflectionStatus::Success, "physics enabled write succeeded");
        ctx.expect(physics.body(floorBody)->enabled == false, "physics body descriptor updated");

        const Engine::ReflectionResult colliderShape = Engine::getReflectedProperty(
            context,
            Engine::toOpaque(floorCollider),
            Engine::SceneReflectedPropertyId::ColliderShapeType);
        ctx.expect(std::get<int64_t>(colliderShape.value) == static_cast<int64_t>(Engine::ScenePhysicsShapeType::Box), "collider shape reflected");

        const Engine::SceneActorHandle characterActor = scene.createActor();
        Engine::SceneCharacterMovementSystem characters(scene, physics);
        Engine::SceneCharacterDescriptor characterDescriptor;
        characterDescriptor.actor = characterActor;
        characterDescriptor.enabled = true;
        const Engine::SceneCharacterHandle character = characters.createCharacter(characterDescriptor);
        context.characters = &characters;

        const Engine::ReflectionResult characterEnabled = Engine::setReflectedProperty(
            context,
            Engine::toOpaque(character),
            Engine::SceneReflectedPropertyId::Enabled,
            false);
        ctx.expect(characterEnabled.status == Engine::ReflectionStatus::Success, "character enabled write succeeded");
        ctx.expect(characters.descriptor(character)->enabled == false, "character descriptor updated");

        const Engine::ReflectionResult speedScale = Engine::setReflectedProperty(
            context,
            Engine::toOpaque(character),
            Engine::SceneReflectedPropertyId::CharacterSpeedScale,
            2.0f);
        ctx.expect(speedScale.status == Engine::ReflectionStatus::Success, "character input speed scale write succeeded");
    }

    void AssetAndTerrainReflectionExposeStableIds(TestContext& ctx)
    {
        Engine::AssetRegistry assets;
        Engine::AssetDescriptor assetDescriptor;
        assetDescriptor.sourcePath = std::filesystem::path{"missing/reflection_asset.png"};
        assetDescriptor.type = Engine::AssetType::Texture;
        assetDescriptor.explicitId = Engine::AssetId{777};
        const Engine::AssetHandle asset = assets.registerAsset(assetDescriptor);

        Engine::TerrainDataset terrain;
        Engine::TerrainSourceDescriptor sourceDescriptor;
        sourceDescriptor.sourceId = Engine::AssetId{900};
        sourceDescriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        sourceDescriptor.debugName = "reflection terrain";
        sourceDescriptor.defaultChunkSize = 8.0f;
        sourceDescriptor.defaultResolution = 3;
        const Engine::TerrainSourceHandle source = terrain.registerSource(sourceDescriptor);

        Engine::TerrainImportedChunk imported;
        imported.id = {sourceDescriptor.sourceId, {2, -1}};
        imported.coord = {2, -1};
        imported.origin = {16.0f, 0.0f, -8.0f};
        imported.size = 8.0f;
        imported.resolution = 3;
        imported.heights = {0.0f, 1.0f, 0.0f, 1.0f, 2.0f, 1.0f, 0.0f, 1.0f, 0.0f};
        const Engine::TerrainChunkHandle chunk = terrain.loadImportedChunk(source, imported);

        Engine::SceneReflectionContext context;
        context.assets = &assets;
        context.terrain = &terrain;

        const Engine::ReflectionResult assetId = Engine::getReflectedProperty(
            context,
            Engine::toOpaque(asset),
            Engine::SceneReflectedPropertyId::AssetId);
        ctx.expect(std::get<Engine::AssetId>(assetId.value).value == 777, "asset stable id reflected");

        const Engine::ReflectionResult terrainId = Engine::getReflectedProperty(
            context,
            Engine::toOpaque(chunk),
            Engine::SceneReflectedPropertyId::TerrainChunkId);
        ctx.expect(std::get<Engine::TerrainSourceChunkId>(terrainId.value) == imported.id, "terrain durable chunk id reflected");

        const Engine::ReflectionResult readOnly = Engine::setReflectedProperty(
            context,
            Engine::toOpaque(asset),
            Engine::SceneReflectedPropertyId::AssetStatus,
            int64_t{0});
        ctx.expect(readOnly.status == Engine::ReflectionStatus::ReadOnly, "asset metadata is read-only");

        const Engine::OpaqueHandle stableChunkOpaque = Engine::toOpaque(imported.id);
        ctx.expect(stableChunkOpaque.kind == Engine::OpaqueHandleKind::TerrainStableChunk, "stable terrain chunk can be wrapped as non-runtime opaque token");
    }
}

int main()
{
    std::vector<TestFailure> failures;

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"RegistryDescriptorsAreDeterministic", RegistryDescriptorsAreDeterministic},
        {"OpaqueHandleValidation", OpaqueHandleValidation},
        {"SceneTransformReflectionWritesThroughScene", SceneTransformReflectionWritesThroughScene},
        {"RenderBridgeReflectionWritesDescriptor", RenderBridgeReflectionWritesDescriptor},
        {"PhysicsAndCharacterReflectionUsePublicApis", PhysicsAndCharacterReflectionUsePublicApis},
        {"AssetAndTerrainReflectionExposeStableIds", AssetAndTerrainReflectionExposeStableIds},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Scene reflection tests passed (" << tests.size() << " tests)\n";
    return 0;
}
