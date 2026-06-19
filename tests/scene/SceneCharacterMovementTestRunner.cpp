#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Navigation.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneCharacterMovement.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainNavigationAdapter.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"

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

    bool near(float lhs, float rhs, float epsilon = 0.05f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    Engine::SceneTransform transformAt(const glm::vec3& position)
    {
        Engine::SceneTransform transform;
        transform.translation = position;
        return transform;
    }

    Engine::ScenePhysicsShapeDescriptor box(const glm::vec3& halfExtents)
    {
        Engine::ScenePhysicsShapeDescriptor shape;
        shape.type = Engine::ScenePhysicsShapeType::Box;
        shape.box.halfExtents = halfExtents;
        return shape;
    }

    Engine::ScenePhysicsBodyHandle createStaticBox(
        Engine::Scene& scene,
        Engine::ScenePhysicsWorld& physics,
        const glm::vec3& position,
        const glm::vec3& halfExtents)
    {
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt(position));
        Engine::ScenePhysicsBodyDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.motionType = Engine::ScenePhysicsMotionType::Static;
        const Engine::ScenePhysicsBodyHandle body = physics.createBody(descriptor);
        if (Engine::isValid(body)) {
            [[maybe_unused]] const Engine::SceneColliderHandle collider = physics.attachCollider(body, box(halfExtents));
        }
        return body;
    }

    Engine::SceneCharacterDescriptor characterDescriptor(Engine::SceneActorHandle actor)
    {
        Engine::SceneCharacterDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.radius = 0.35f;
        descriptor.height = 1.8f;
        descriptor.maxSpeed = 4.0f;
        descriptor.acceleration = 40.0f;
        descriptor.braking = 40.0f;
        descriptor.gravity = 24.0f;
        descriptor.slopeLimitDegrees = 45.0f;
        descriptor.stepHeight = 0.45f;
        descriptor.snapDistance = 0.35f;
        descriptor.physicsLayer = {2};
        descriptor.physicsFilter = {UINT32_MAX, 2, true};
        return descriptor;
    }

    void fixedStep(
        Engine::Scene& scene,
        Engine::ScenePhysicsWorld& physics,
        Engine::SceneCharacterMovementSystem& movement,
        float dt)
    {
        movement.updateFixed(dt);
        physics.syncFromScene();
        physics.stepFixed(dt);
        physics.syncToScene();
        scene.updateWorldTransforms();
    }

    constexpr Engine::AssetId TestTerrainSourceId{0x1100};
    constexpr Engine::TerrainSourceChunkCoord TestChunkCoord{0, 0};
    constexpr float TestChunkSize = 16.0f;
    constexpr uint32_t TestResolution = 5;

    Engine::TerrainSourceDescriptor terrainSourceDescriptor()
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = TestTerrainSourceId;
        descriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        descriptor.defaultChunkSize = TestChunkSize;
        descriptor.defaultResolution = TestResolution;
        descriptor.settings = {"heightmap_terrain", "character", "flat"};
        descriptor.debugName = "scene.character.heightmap";
        return descriptor;
    }

    Engine::TerrainImportedChunk flatTerrainChunk()
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {TestTerrainSourceId, TestChunkCoord};
        chunk.coord = TestChunkCoord;
        chunk.origin = {0.0f, 0.0f, 0.0f};
        chunk.size = TestChunkSize;
        chunk.resolution = TestResolution;
        chunk.heights.assign(static_cast<size_t>(TestResolution) * TestResolution, 0.0f);
        return chunk;
    }

    struct TerrainFixture {
        Engine::TerrainDataset dataset;
        Engine::TerrainSourceHandle source;
        Engine::TerrainChunkHandle chunk;
    };

    TerrainFixture makeTerrain(TestContext& ctx)
    {
        TerrainFixture fixture;
        fixture.source = fixture.dataset.registerSource(terrainSourceDescriptor());
        fixture.chunk = fixture.dataset.loadImportedChunk(fixture.source, flatTerrainChunk());
        ctx.expect(Engine::isValid(fixture.source), "terrain source was invalid");
        ctx.expect(Engine::isValid(fixture.chunk), "terrain chunk was invalid");
        return fixture;
    }

    Engine::TerrainPhysicsSourceIdentity physicsIdentity()
    {
        Engine::TerrainPhysicsSourceIdentity identity;
        identity.sourceId = TestTerrainSourceId;
        identity.sourceHash = "character-flat-terrain";
        identity.importSettings = {"heightmap_terrain", "character", "flat"};
        identity.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return identity;
    }

    Engine::TerrainNavigationSourceIdentity navigationIdentity()
    {
        Engine::TerrainNavigationSourceIdentity identity;
        identity.sourceId = TestTerrainSourceId;
        identity.sourceHash = "character-flat-terrain";
        identity.importSettings = {"heightmap_terrain", "character", "flat"};
        identity.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return identity;
    }

    void lifecycleAndInvalidation(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 1.0f, 0.0f}));

        const Engine::SceneCharacterHandle first = movement.createCharacter(characterDescriptor(actor));
        ctx.expect(Engine::isValid(first), "character handle was invalid");
        ctx.expect(movement.contains(first), "movement system did not contain character");
        ctx.expect(movement.characterForActor(actor) == first, "characterForActor returned wrong handle");
        ctx.expect(movement.destroyCharacter(first), "destroyCharacter failed");
        ctx.expect(!movement.contains(first), "destroyed character still validates");

        const Engine::SceneCharacterHandle second = movement.createCharacter(characterDescriptor(actor));
        ctx.expect(Engine::isValid(second), "second character handle was invalid");
        ctx.expect(first.index == second.index, "character slot was not reused");
        ctx.expect(first.generation != second.generation, "character generation did not change");
        scene.destroyActor(actor);
        scene.flushDestroyedActors();
        movement.updateFixed(1.0f / 60.0f);
        ctx.expect(!movement.contains(second), "destroyed owner did not clean character");
        ctx.expect(movement.diagnostics().invalidOwnerCleanupCount == 1, "invalid owner cleanup was not counted");
    }

    void creationRejectsInvalidInputs(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        ctx.expect(!Engine::isValid(movement.createCharacter(characterDescriptor({}))), "default actor was accepted");

        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.attachChild(child, parent, false);
        ctx.expect(!Engine::isValid(movement.createCharacter(characterDescriptor(child))), "parented actor was accepted");

        const Engine::SceneActorHandle malformedActor = scene.createActor();
        Engine::SceneCharacterDescriptor malformed = characterDescriptor(malformedActor);
        malformed.radius = -1.0f;
        ctx.expect(!Engine::isValid(movement.createCharacter(malformed)), "malformed capsule was accepted");

        const Engine::SceneActorHandle bodyActor = scene.createActor();
        Engine::ScenePhysicsBodyDescriptor bodyDescriptor;
        bodyDescriptor.actor = bodyActor;
        [[maybe_unused]] const Engine::ScenePhysicsBodyHandle body = physics.createBody(bodyDescriptor);
        ctx.expect(!Engine::isValid(movement.createCharacter(characterDescriptor(bodyActor))), "pre-existing physics body was accepted");
    }

    void creationOwnsKinematicCapsule(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 1.0f, 0.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
        const std::optional<Engine::SceneCharacterDescriptor> descriptor = movement.descriptor(character);
        const std::optional<Engine::ScenePhysicsBodyHandle> body = physics.bodyForActor(actor);
        ctx.expect(descriptor.has_value(), "character descriptor was missing");
        ctx.expect(body.has_value(), "character body was missing");
        if (body) {
            const std::optional<Engine::ScenePhysicsBodyDescriptor> bodyDescriptor = physics.body(*body);
            ctx.expect(bodyDescriptor.has_value() &&
                    bodyDescriptor->motionType == Engine::ScenePhysicsMotionType::Kinematic,
                "character body was not kinematic");
            ctx.expect(physics.colliders(*body).size() == 1, "character did not own exactly one collider");
        }
    }

    void flatFloorGroundingAndDisabledNoOp(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        createStaticBox(scene, physics, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 1.15f, 0.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));

        fixedStep(scene, physics, movement, 1.0f / 60.0f);
        std::optional<Engine::SceneCharacterState> state = movement.state(character);
        ctx.expect(state.has_value() && state->grounded, "character did not ground on flat floor");
        std::optional<Engine::SceneTransform> before = scene.localTransform(actor);
        movement.setEnabled(character, false);
        movement.setMoveInput(character, {{1.0f, 0.0f, 0.0f}, 1.0f});
        fixedStep(scene, physics, movement, 1.0f / 60.0f);
        std::optional<Engine::SceneTransform> after = scene.localTransform(actor);
        ctx.expect(before && after && near(before->translation.x, after->translation.x), "disabled character moved");
    }

    void terrainColliderGrounding(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        TerrainFixture terrain = makeTerrain(ctx);
        const auto request = Engine::terrainPhysicsColliderRequestFromDatasetChunk(
            terrain.dataset,
            terrain.chunk,
            TestResolution,
            physicsIdentity());
        ctx.expect(request.has_value(), "terrain physics request missing");
        if (!request) {
            return;
        }
        const Engine::TerrainPhysicsColliderBuildResult build = Engine::buildTerrainPhysicsCollider(*request);
        Engine::TerrainPhysicsColliderAdapter adapter;
        ctx.expect(build.success && build.payload.has_value(), "terrain physics payload failed");
        if (!build.payload) {
            return;
        }
        [[maybe_unused]] const Engine::TerrainPhysicsColliderHandle terrainCollider =
            adapter.createStaticCollider(scene, physics, *build.payload);

        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({8.0f, 1.15f, 8.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
        fixedStep(scene, physics, movement, 1.0f / 60.0f);
        const std::optional<Engine::SceneCharacterState> state = movement.state(character);
        ctx.expect(state.has_value() && state->grounded, "character did not ground on explicit terrain collider");
    }

    void steepSurfaceRejectedAndFalling(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        createStaticBox(scene, physics, {0.0f, 2.0f, 0.0f}, {0.1f, 2.0f, 4.0f});
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({-0.45f, 2.0f, 0.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
        fixedStep(scene, physics, movement, 1.0f / 60.0f);
        const std::optional<Engine::SceneCharacterState> state = movement.state(character);
        ctx.expect(state.has_value() && !state->grounded, "vertical surface was treated as walkable ground");
    }

    void accelerationBrakingAndGravity(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 4.0f, 0.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
        movement.setMoveInput(character, {{1.0f, 0.0f, 0.0f}, 1.0f});
        fixedStep(scene, physics, movement, 0.1f);
        std::optional<Engine::SceneCharacterState> state = movement.state(character);
        ctx.expect(state.has_value() && state->velocity.x > 0.0f, "character did not accelerate");
        ctx.expect(state.has_value() && state->velocity.x <= characterDescriptor(actor).maxSpeed + 0.01f, "character exceeded max speed");
        ctx.expect(state.has_value() && state->velocity.y < 0.0f, "falling character did not apply gravity");
        movement.setMoveInput(character, {});
        fixedStep(scene, physics, movement, 0.1f);
        state = movement.state(character);
        ctx.expect(state.has_value() && state->velocity.x < characterDescriptor(actor).maxSpeed, "character did not brake");
    }

    void wallSlideAndStepHandling(TestContext& ctx)
    {
        {
            Engine::Scene scene;
            Engine::ScenePhysicsWorld physics(scene);
            Engine::SceneCharacterMovementSystem movement(scene, physics);
            createStaticBox(scene, physics, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
            createStaticBox(scene, physics, {1.6f, 1.0f, 0.0f}, {0.2f, 1.0f, 8.0f});
            const Engine::SceneActorHandle actor = scene.createActor();
            scene.setLocalTransform(actor, transformAt({0.0f, 1.0f, 0.0f}));
            const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
            movement.setMoveInput(character, {{1.0f, 0.0f, 1.0f}, 1.0f});
            for (int i = 0; i < 12; ++i) {
                fixedStep(scene, physics, movement, 1.0f / 30.0f);
            }
            const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
            ctx.expect(transform.has_value() && transform->translation.x < 1.25f, "character tunneled through wall");
            ctx.expect(transform.has_value() && transform->translation.z > 0.1f, "character did not slide along wall");
        }

        {
            Engine::Scene scene;
            Engine::ScenePhysicsWorld physics(scene);
            Engine::SceneCharacterMovementSystem movement(scene, physics);
            createStaticBox(scene, physics, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
            createStaticBox(scene, physics, {1.0f, 0.1f, 0.0f}, {0.25f, 0.1f, 1.0f});
            const Engine::SceneActorHandle actor = scene.createActor();
            scene.setLocalTransform(actor, transformAt({0.0f, 1.0f, 0.0f}));
            const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
            movement.setMoveInput(character, {{1.0f, 0.0f, 0.0f}, 1.0f});
            for (int i = 0; i < 10; ++i) {
                fixedStep(scene, physics, movement, 1.0f / 30.0f);
            }
            const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
            ctx.expect(transform.has_value() && transform->translation.x > 1.0f, "character did not step over low obstacle");
        }

        {
            Engine::Scene scene;
            Engine::ScenePhysicsWorld physics(scene);
            Engine::SceneCharacterMovementSystem movement(scene, physics);
            createStaticBox(scene, physics, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
            createStaticBox(scene, physics, {1.0f, 1.0f, 0.0f}, {0.25f, 1.0f, 1.0f});
            const Engine::SceneActorHandle actor = scene.createActor();
            scene.setLocalTransform(actor, transformAt({0.0f, 1.0f, 0.0f}));
            const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
            movement.setMoveInput(character, {{1.0f, 0.0f, 0.0f}, 1.0f});
            for (int i = 0; i < 10; ++i) {
                fixedStep(scene, physics, movement, 1.0f / 30.0f);
            }
            const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
            ctx.expect(transform.has_value() && transform->translation.x < 1.0f, "character stepped over high obstacle");
        }
    }

    void pathFollowingAndMissingNavigation(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        TerrainFixture terrain = makeTerrain(ctx);
        const auto request = Engine::terrainNavigationRequestFromDatasetChunk(
            terrain.dataset,
            terrain.chunk,
            TestResolution,
            navigationIdentity());
        ctx.expect(request.has_value(), "terrain navigation request missing");
        if (!request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult build = Engine::buildTerrainNavigationData(*request);
        Engine::NavigationSystem navigation;
        if (build.buildData) {
            [[maybe_unused]] const Engine::NavigationTileHandle tile = navigation.buildTerrainTile(*build.buildData, {});
        }
        Engine::SceneNavigationService service(navigation);
        movement.setNavigationService(&service);

        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({2.0f, 1.0f, 2.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
        Engine::SceneCharacterPathRequest pathRequest;
        pathRequest.goal = {14.0f, 0.0f, 14.0f};
        ctx.expect(movement.requestPathTo(character, pathRequest), "path request failed");
        for (int i = 0; i < 20; ++i) {
            fixedStep(scene, physics, movement, 1.0f / 30.0f);
        }
        const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
        ctx.expect(transform.has_value() && transform->translation.x > 2.5f && transform->translation.z > 2.5f,
            "character did not move along nav path");
        ctx.expect(movement.clearPath(character), "clearPath failed");

        Engine::NavigationSystem emptyNavigation;
        Engine::SceneNavigationService emptyService(emptyNavigation);
        movement.setNavigationService(&emptyService);
        ctx.expect(!movement.requestPathTo(character, pathRequest), "missing navigation tile unexpectedly produced path");
        const std::optional<Engine::SceneCharacterState> state = movement.state(character);
        ctx.expect(state.has_value() && state->lastStatus == Engine::SceneCharacterMovementStatus::NoPath,
            "missing navigation did not report NoPath");
    }

    void schedulerAndDiagnostics(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::SceneCharacterMovementSystem movement(scene, physics);
        createStaticBox(scene, physics, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 1.0f, 0.0f}));
        const Engine::SceneCharacterHandle character = movement.createCharacter(characterDescriptor(actor));
        movement.setMoveInput(character, {{1.0f, 0.0f, 0.0f}, 1.0f});

        const Engine::SceneSystemHandle physicsSystem = physics.registerPhysicsSystems();
        const Engine::SceneSystemHandle movementSystem = movement.registerMovementSystem();
        ctx.expect(Engine::isValid(physicsSystem) && Engine::isValid(movementSystem), "scheduler systems were invalid");
        ctx.expect(scene.load() && scene.start(), "scene lifecycle failed");
        scene.tickFixed(1.0f / 30.0f);
        const std::optional<Engine::SceneTransform> moved = scene.localTransform(actor);
        ctx.expect(moved.has_value() && moved->translation.x > 0.0f, "scheduled movement did not run");

        scene.setPaused(true);
        const float pausedX = moved ? moved->translation.x : 0.0f;
        scene.tickFixed(1.0f / 30.0f);
        const std::optional<Engine::SceneTransform> paused = scene.localTransform(actor);
        ctx.expect(paused.has_value() && near(paused->translation.x, pausedX), "paused fixed phase moved character");
        ctx.expect(movement.diagnostics().characterCount == 1, "diagnostics character count was wrong");
        ctx.expect(!movement.debugRequests().empty(), "debug requests were not recorded");
        movement.clearDebugRequests();
        ctx.expect(movement.debugRequests().empty(), "debug requests did not clear");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"LifecycleAndInvalidation", lifecycleAndInvalidation},
        {"CreationRejectsInvalidInputs", creationRejectsInvalidInputs},
        {"CreationOwnsKinematicCapsule", creationOwnsKinematicCapsule},
        {"FlatFloorGroundingAndDisabledNoOp", flatFloorGroundingAndDisabledNoOp},
        {"TerrainColliderGrounding", terrainColliderGrounding},
        {"SteepSurfaceRejectedAndFalling", steepSurfaceRejectedAndFalling},
        {"AccelerationBrakingAndGravity", accelerationBrakingAndGravity},
        {"WallSlideAndStepHandling", wallSlideAndStepHandling},
        {"PathFollowingAndMissingNavigation", pathFollowingAndMissingNavigation},
        {"SchedulerAndDiagnostics", schedulerAndDiagnostics},
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

    std::cout << "Scene character movement tests passed (" << tests.size() << ")\n";
    return 0;
}
