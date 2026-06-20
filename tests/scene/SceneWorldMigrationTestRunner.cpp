#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneCharacterMovement.hpp"
#include "Engine/SceneSerialization.hpp"
#include "Engine/SceneWorldMigrationBridge.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"
#include "Engine/World.hpp"

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

    bool near(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.001f)
    {
        return std::abs(lhs.x - rhs.x) <= epsilon &&
            std::abs(lhs.y - rhs.y) <= epsilon &&
            std::abs(lhs.z - rhs.z) <= epsilon;
    }

    Engine::WorldObjectHandle createWorldObject(Engine::World& world, const glm::vec3& position)
    {
        const Engine::WorldObjectHandle object = world.createObject(Engine::ObjectId::fromString("test/object/" + std::to_string(position.x)));
        world.setPosition(object, position);
        world.setScale(object, {1.0f, 1.0f, 1.0f});
        world.setLocalBounds(object, {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
        world.setCollisionEnabled(object, true);
        return object;
    }

    void WorldObjectMapsAndSyncsTransforms(TestContext& ctx)
    {
        Engine::World world;
        Engine::Scene scene;
        Engine::SceneWorldMigrationBridge bridge;
        const Engine::WorldObjectHandle object = createWorldObject(world, {1.0f, 2.0f, 3.0f});

        const Engine::SceneActorHandle actor = bridge.mapObject(scene, world, object);
        ctx.expect(Engine::isValid(actor), "world object did not map to a scene actor");
        ctx.expect(scene.contains(actor), "mapped scene actor is not active");
        ctx.expect(bridge.actorForObject(object) == actor, "object-to-actor lookup failed");

        const std::optional<Engine::SceneTransform> initial = scene.localTransform(actor);
        ctx.expect(initial && near(initial->translation, {1.0f, 2.0f, 3.0f}), "world-to-scene initial transform mismatch");

        world.setPosition(object, {4.0f, 5.0f, 6.0f});
        bridge.syncWorldToScene(scene, world);
        const std::optional<Engine::SceneTransform> syncedScene = scene.localTransform(actor);
        ctx.expect(syncedScene && near(syncedScene->translation, {4.0f, 5.0f, 6.0f}), "world-to-scene sync mismatch");

        Engine::SceneTransform moved = *syncedScene;
        moved.translation = {-2.0f, 1.0f, 8.0f};
        (void)scene.setLocalTransform(actor, moved);
        bridge.syncSceneToWorld(scene, world);
        ctx.expect(near(world.position(object).value_or(glm::vec3{}), {-2.0f, 1.0f, 8.0f}), "scene-to-world sync mismatch");
    }

    void CharacterBindingCreatesOwnedKinematicBody(TestContext& ctx)
    {
        Engine::World world;
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics{scene};
        Engine::SceneCharacterMovementSystem characters{scene, physics};
        Engine::SceneWorldMigrationBridge bridge;
        const Engine::WorldObjectHandle object = createWorldObject(world, {0.0f, 2.0f, 0.0f});
        (void)bridge.mapObject(scene, world, object);

        Engine::SceneCharacterDescriptor descriptor;
        descriptor.radius = 0.3f;
        descriptor.height = 1.8f;
        const Engine::SceneCharacterHandle character = bridge.bindCharacter(characters, object, descriptor);

        ctx.expect(Engine::isValid(character), "character binding failed");
        ctx.expect(characters.diagnostics().characterCount == 1, "character diagnostics did not count binding");
        ctx.expect(physics.diagnostics().bodyCount == 1, "character did not create a physics body");
        ctx.expect(physics.diagnostics().colliderCount == 1, "character did not create a physics collider");
        ctx.expect(bridge.diagnostics().characterBindingCount == 1, "bridge did not report character binding");
    }

    void StaticColliderLifecycleIsIdempotent(TestContext& ctx)
    {
        Engine::World world;
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics{scene};
        Engine::SceneWorldMigrationBridge bridge;
        const Engine::WorldObjectHandle object = createWorldObject(world, {0.0f, 0.0f, 0.0f});
        (void)bridge.mapObject(scene, world, object);

        ctx.expect(bridge.createStaticBoxColliderForObject(physics, world, object), "static collider creation failed");
        ctx.expect(physics.diagnostics().bodyCount == 1, "static collider did not create body");
        ctx.expect(physics.diagnostics().colliderCount == 1, "static collider did not create collider");
        ctx.expect(bridge.diagnostics().staticColliderBindingCount == 1, "bridge did not report static collider");

        ctx.expect(bridge.destroyStaticColliderForObject(physics, object), "static collider destroy failed");
        ctx.expect(!bridge.destroyStaticColliderForObject(physics, object), "double static collider destroy should no-op");
        ctx.expect(physics.diagnostics().bodyCount == 0, "static body leaked after destroy");
    }

    void TerrainColliderHandoffIsExplicitAndReleasable(TestContext& ctx)
    {
        Engine::TerrainSettings settings;
        settings.chunkSize = 8.0f;
        settings.resolution = 5;
        settings.navigationResolution = 5;
        Engine::TerrainSystem terrain{settings};
        const Engine::TerrainTileHandle tile = terrain.createTile({0, 0}, {});

        const auto request = Engine::terrainPhysicsColliderRequestFromTerrainSystemTile(
            terrain,
            tile,
            settings.navigationResolution,
            Engine::legacyProceduralTerrainPhysicsIdentity(settings));
        ctx.expect(request.has_value(), "terrain physics request was not created");
        if (!request) {
            return;
        }
        const Engine::TerrainPhysicsColliderBuildResult build = Engine::buildTerrainPhysicsCollider(*request);
        ctx.expect(build.success && build.payload.has_value(), "terrain physics payload was not built");
        if (!build.payload) {
            return;
        }

        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics{scene};
        Engine::TerrainPhysicsColliderAdapter adapter;
        const Engine::TerrainPhysicsColliderHandle handle =
            adapter.createStaticCollider(scene, physics, *build.payload);
        ctx.expect(Engine::isValid(handle), "terrain collider binding was not created");
        ctx.expect(physics.diagnostics().bodyCount == 1, "terrain collider did not create physics body");
        ctx.expect(adapter.destroyColliderForChunk(scene, physics, build.payload->chunkId), "terrain collider was not destroyed by chunk id");
        ctx.expect(physics.diagnostics().bodyCount == 0, "terrain collider body leaked after destroy");
    }

    void InvalidWorldAndSceneCleanupIsDeterministic(TestContext& ctx)
    {
        Engine::World world;
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics{scene};
        Engine::SceneCharacterMovementSystem characters{scene, physics};
        Engine::SceneWorldMigrationBridge bridge;
        const Engine::WorldObjectHandle object = createWorldObject(world, {2.0f, 0.0f, 0.0f});
        const Engine::SceneActorHandle actor = bridge.mapObject(scene, world, object);
        ctx.expect(bridge.createStaticBoxColliderForObject(physics, world, object), "static collider creation failed");

        world.destroyObject(object);
        bridge.cleanupInvalidMappings(scene, world, &physics, &characters);
        ctx.expect(bridge.mappings().empty(), "invalid world object mapping was not removed");
        ctx.expect(!scene.contains(actor), "scene actor was not destroyed during cleanup");
        ctx.expect(physics.diagnostics().bodyCount == 0, "physics body leaked during cleanup");
        ctx.expect(bridge.diagnostics().invalidWorldObjectCleanupCount == 1, "invalid world cleanup count mismatch");
    }

    void SerializationSnapshotContainsOnlySceneRecords(TestContext& ctx)
    {
        Engine::World world;
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics{scene};
        Engine::SceneWorldMigrationBridge bridge;
        const Engine::WorldObjectHandle object = createWorldObject(world, {3.0f, 0.0f, 0.0f});
        (void)bridge.mapObject(scene, world, object);
        ctx.expect(bridge.createStaticBoxColliderForObject(physics, world, object), "static collider creation failed");

        Engine::ReflectionRegistry reflection;
        const Engine::SceneSerializedScene serialized = Engine::buildSerializedScene(scene, reflection);
        const Engine::SceneSerializationDiagnostics validation = Engine::validateSerializedScene(serialized, reflection);

        ctx.expect(validation.errors.empty(), "serialized mapped scene was invalid");
        ctx.expect(serialized.actors.size() == 1, "serialized scene should contain one actor");
        ctx.expect(serialized.components.empty(), "bridge should not serialize runtime bridge or physics components");
        ctx.expect(serialized.terrain.empty(), "bridge should not serialize runtime terrain handles");
    }

    using TestFn = void (*)(TestContext&);

    struct TestCase {
        const char* name;
        TestFn fn;
    };
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<TestCase> tests{
        {"WorldObjectMapsAndSyncsTransforms", WorldObjectMapsAndSyncsTransforms},
        {"CharacterBindingCreatesOwnedKinematicBody", CharacterBindingCreatesOwnedKinematicBody},
        {"StaticColliderLifecycleIsIdempotent", StaticColliderLifecycleIsIdempotent},
        {"TerrainColliderHandoffIsExplicitAndReleasable", TerrainColliderHandoffIsExplicitAndReleasable},
        {"InvalidWorldAndSceneCleanupIsDeterministic", InvalidWorldAndSceneCleanupIsDeterministic},
        {"SerializationSnapshotContainsOnlySceneRecords", SerializationSnapshotContainsOnlySceneRecords},
    };

    for (const TestCase& test : tests) {
        TestContext context{test.name, failures};
        test.fn(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "SceneWorldMigration tests passed (" << tests.size() << " cases)\n";
    return 0;
}
