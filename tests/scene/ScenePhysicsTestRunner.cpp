#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Physics/ScenePhysics.hpp"

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

    bool nearlyEqual(float lhs, float rhs, float epsilon = 0.01f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.01f)
    {
        return nearlyEqual(lhs.x, rhs.x, epsilon)
            && nearlyEqual(lhs.y, rhs.y, epsilon)
            && nearlyEqual(lhs.z, rhs.z, epsilon);
    }

    Engine::SceneTransform transformAt(const glm::vec3& translation)
    {
        Engine::SceneTransform transform;
        transform.translation = translation;
        return transform;
    }

    Engine::ScenePhysicsShapeDescriptor box(const glm::vec3& halfExtents)
    {
        Engine::ScenePhysicsShapeDescriptor shape;
        shape.type = Engine::ScenePhysicsShapeType::Box;
        shape.box.halfExtents = halfExtents;
        return shape;
    }

    Engine::ScenePhysicsShapeDescriptor box(float halfExtent)
    {
        return box(glm::vec3{halfExtent});
    }

    Engine::ScenePhysicsShapeDescriptor sphere(float radius)
    {
        Engine::ScenePhysicsShapeDescriptor shape;
        shape.type = Engine::ScenePhysicsShapeType::Sphere;
        shape.sphere.radius = radius;
        return shape;
    }

    Engine::ScenePhysicsShapeDescriptor capsule(float radius, float halfHeight)
    {
        Engine::ScenePhysicsShapeDescriptor shape;
        shape.type = Engine::ScenePhysicsShapeType::Capsule;
        shape.capsule.radius = radius;
        shape.capsule.halfHeight = halfHeight;
        return shape;
    }

    Engine::ScenePhysicsBodyDescriptor bodyDescriptor(
        Engine::SceneActorHandle actor,
        Engine::ScenePhysicsMotionType motionType)
    {
        Engine::ScenePhysicsBodyDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.motionType = motionType;
        return descriptor;
    }

    Engine::ScenePhysicsBodyHandle createBodyWithCollider(
        Engine::ScenePhysicsWorld& physics,
        Engine::SceneActorHandle actor,
        Engine::ScenePhysicsMotionType motionType,
        const Engine::ScenePhysicsShapeDescriptor& shape)
    {
        const Engine::ScenePhysicsBodyHandle body = physics.createBody(bodyDescriptor(actor, motionType));
        if (Engine::isValid(body)) {
            [[maybe_unused]] const Engine::SceneColliderHandle collider = physics.attachCollider(body, shape);
        }
        return body;
    }

    void stepPhysics(Engine::ScenePhysicsWorld& physics, int steps, float dt = 1.0f / 60.0f)
    {
        for (int index = 0; index < steps; ++index) {
            physics.syncFromScene();
            physics.stepFixed(dt);
            physics.syncToScene();
        }
    }

    void bodyLifecycleInvalidatesStaleHandles(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::ScenePhysicsBodyHandle first =
            physics.createBody(bodyDescriptor(actor, Engine::ScenePhysicsMotionType::Static));

        ctx.expect(Engine::isValid(first), "created physics body handle was invalid");
        ctx.expect(physics.contains(first), "physics world did not contain created body");
        ctx.expect(physics.destroyBody(first), "destroyBody failed");
        ctx.expect(!physics.contains(first), "destroyed body still validates");

        const Engine::ScenePhysicsBodyHandle second =
            physics.createBody(bodyDescriptor(actor, Engine::ScenePhysicsMotionType::Static));
        ctx.expect(Engine::isValid(second), "second body handle was invalid");
        ctx.expect(first.index == second.index, "body slot was not reused deterministically");
        ctx.expect(first.generation != second.generation, "body generation did not change after reuse");
        ctx.expect(!physics.destroyBody(first), "stale body destroy should be a no-op failure");
    }

    void colliderLifecycleAndMalformedShapes(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::ScenePhysicsBodyHandle body =
            physics.createBody(bodyDescriptor(actor, Engine::ScenePhysicsMotionType::Static));

        Engine::ScenePhysicsShapeDescriptor invalid = sphere(-1.0f);
        ctx.expect(!Engine::isValid(physics.attachCollider(body, invalid)), "invalid sphere collider was accepted");

        const Engine::SceneColliderHandle collider = physics.attachCollider(body, sphere(0.5f));
        ctx.expect(Engine::isValid(collider), "valid collider handle was invalid");
        ctx.expect(physics.contains(collider), "physics world did not contain collider");
        ctx.expect(physics.colliders(body).size() == 1, "body collider list size was wrong");
        ctx.expect(physics.detachCollider(collider), "detachCollider failed");
        ctx.expect(!physics.contains(collider), "detached collider still validates");
    }

    void staticBodyReadsSceneTransform(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({3.0f, 0.0f, 0.0f}));
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Static, box({0.5f, 0.5f, 0.5f}));

        const Engine::ScenePhysicsRaycastResult hit = physics.raycast({3.0f, 3.0f, 0.0f}, {3.0f, -3.0f, 0.0f});
        ctx.expect(hit.status == Engine::ScenePhysicsQueryStatus::Success, "raycast did not hit translated static body");
        ctx.expect(hit.hit.has_value() && hit.hit->actor == actor, "raycast hit wrong actor");
    }

    void kinematicTargetMovesBody(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::ScenePhysicsBodyHandle body =
            createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Kinematic, sphere(0.5f));

        ctx.expect(physics.setKinematicTarget(body, {2.0f, 0.0f, 0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}), "kinematic target failed");
        physics.syncFromScene();
        physics.stepFixed(1.0f / 60.0f);

        const Engine::ScenePhysicsRaycastResult hit = physics.raycast({2.0f, 3.0f, 0.0f}, {2.0f, -3.0f, 0.0f});
        ctx.expect(hit.status == Engine::ScenePhysicsQueryStatus::Success, "raycast did not hit moved kinematic body");
        ctx.expect(hit.hit.has_value() && hit.hit->actor == actor, "kinematic target moved wrong body");
    }

    void dynamicBodyFallsOntoStaticFloor(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle floor = scene.createActor();
        scene.setLocalTransform(floor, transformAt({0.0f, -0.5f, 0.0f}));
        createBodyWithCollider(physics, floor, Engine::ScenePhysicsMotionType::Static, box({5.0f, 0.5f, 5.0f}));

        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 5.0f, 0.0f}));
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Dynamic, sphere(0.5f));
        stepPhysics(physics, 180);

        const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
        ctx.expect(transform.has_value(), "dynamic actor transform missing");
        if (transform.has_value()) {
            ctx.expect(transform->translation.y < 5.0f, "dynamic body did not fall");
            ctx.expect(transform->translation.y > 0.35f, "dynamic body fell through floor");
        }
    }

    void dynamicBodyWritesBackSceneTransform(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 2.0f, 0.0f}));
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Dynamic, sphere(0.5f));

        stepPhysics(physics, 10);
        const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
        ctx.expect(transform.has_value() && transform->translation.y < 2.0f, "dynamic body did not write back to scene");
    }

    void parentedDynamicBodyRejected(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.attachChild(child, parent, false);

        const Engine::ScenePhysicsBodyHandle body =
            physics.createBody(bodyDescriptor(child, Engine::ScenePhysicsMotionType::Dynamic));
        ctx.expect(!Engine::isValid(body), "parented dynamic body was accepted");
    }

    void actorDestroyFlushCleansPhysicsBodies(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::ScenePhysicsBodyHandle body =
            createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Static, box(0.5f));

        scene.destroyActor(actor);
        scene.flushDestroyedActors();
        physics.syncFromScene();
        ctx.expect(!physics.contains(body), "physics body survived owner actor flush");
        ctx.expect(physics.diagnostics().invalidOwnerCleanupCount > 0, "invalid owner cleanup was not counted");
    }

    void raycastHitAndMiss(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Static, box(1.0f));

        ctx.expect(physics.raycast({0.0f, 3.0f, 0.0f}, {0.0f, -3.0f, 0.0f}).status == Engine::ScenePhysicsQueryStatus::Success,
            "raycast hit failed");
        ctx.expect(physics.raycast({3.0f, 3.0f, 0.0f}, {3.0f, -3.0f, 0.0f}).status == Engine::ScenePhysicsQueryStatus::NoHit,
            "raycast miss failed");
    }

    void overlapReturnsDeterministicHits(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle first = scene.createActor();
        const Engine::SceneActorHandle second = scene.createActor();
        scene.setLocalTransform(second, transformAt({1.5f, 0.0f, 0.0f}));
        const Engine::ScenePhysicsBodyHandle firstBody =
            createBodyWithCollider(physics, first, Engine::ScenePhysicsMotionType::Static, box(0.5f));
        const Engine::ScenePhysicsBodyHandle secondBody =
            createBodyWithCollider(physics, second, Engine::ScenePhysicsMotionType::Static, box(0.5f));

        const Engine::ScenePhysicsOverlapResult result = physics.overlap(box({2.0f, 1.0f, 1.0f}), {0.5f, 0.0f, 0.0f});
        ctx.expect(result.status == Engine::ScenePhysicsQueryStatus::Success, "overlap did not hit bodies");
        ctx.expect(result.hits.size() == 2, "overlap hit count was wrong");
        if (result.hits.size() == 2) {
            ctx.expect(result.hits[0].body == firstBody && result.hits[1].body == secondBody, "overlap order was not deterministic");
        }
    }

    void capsuleSweepDetectsBlockingShape(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle wall = scene.createActor();
        scene.setLocalTransform(wall, transformAt({2.0f, 0.0f, 0.0f}));
        createBodyWithCollider(physics, wall, Engine::ScenePhysicsMotionType::Static, box({0.25f, 1.0f, 1.0f}));

        const Engine::ScenePhysicsSweepResult result =
            physics.sweepCapsule({0.35f, 0.5f}, {0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f});
        ctx.expect(result.status == Engine::ScenePhysicsQueryStatus::Success, "capsule sweep did not hit wall");
        ctx.expect(result.hit.has_value() && result.hit->actor == wall, "capsule sweep hit wrong actor");
    }

    void closestPointReportsExpectedResult(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Static, box(1.0f));

        const Engine::ScenePhysicsClosestPointResult result = physics.closestPoint({3.0f, 0.0f, 0.0f}, 10.0f);
        ctx.expect(result.status == Engine::ScenePhysicsQueryStatus::Success, "closest point failed");
        if (result.hit.has_value()) {
            ctx.expect(nearlyEqual(result.hit->position, {1.0f, 0.0f, 0.0f}), "closest point position was wrong");
            ctx.expect(nearlyEqual(result.hit->distance, 2.0f), "closest point distance was wrong");
        }
    }

    void layerFilterIncludesAndExcludesBodies(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle first = scene.createActor();
        const Engine::SceneActorHandle second = scene.createActor();
        scene.setLocalTransform(second, transformAt({0.0f, 3.0f, 0.0f}));

        Engine::ScenePhysicsBodyDescriptor firstDesc = bodyDescriptor(first, Engine::ScenePhysicsMotionType::Static);
        firstDesc.layer = {1u};
        Engine::ScenePhysicsBodyDescriptor secondDesc = bodyDescriptor(second, Engine::ScenePhysicsMotionType::Static);
        secondDesc.layer = {2u};
        const Engine::ScenePhysicsBodyHandle firstBody = physics.createBody(firstDesc);
        const Engine::ScenePhysicsBodyHandle secondBody = physics.createBody(secondDesc);
        [[maybe_unused]] const Engine::SceneColliderHandle firstCollider = physics.attachCollider(firstBody, box(0.5f));
        [[maybe_unused]] const Engine::SceneColliderHandle secondCollider = physics.attachCollider(secondBody, box(0.5f));

        Engine::ScenePhysicsFilter filter;
        filter.includeLayerMask = 2u;
        const Engine::ScenePhysicsRaycastResult result = physics.raycast({0.0f, 5.0f, 0.0f}, {0.0f, -5.0f, 0.0f}, filter);
        ctx.expect(result.status == Engine::ScenePhysicsQueryStatus::Success, "filtered raycast missed included layer");
        ctx.expect(result.hit.has_value() && result.hit->body == secondBody, "filtered raycast hit wrong layer");
    }

    void schedulerRunsFixedPhysicsOrder(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 2.0f, 0.0f}));
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Dynamic, sphere(0.5f));

        ctx.expect(Engine::isValid(physics.registerPhysicsSystems()), "physics scheduler system did not register");
        ctx.expect(scene.load(), "scene load failed");
        ctx.expect(scene.start(), "scene start failed");
        scene.tickFixed(1.0f / 60.0f);
        ctx.expect(scene.stop(), "scene stop failed");

        const std::optional<Engine::SceneTransform> transform = scene.localTransform(actor);
        ctx.expect(transform.has_value() && transform->translation.y < 2.0f, "scheduler fixed phases did not step and write back physics");
    }

    void preRenderCanObservePhysicsWriteback(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, transformAt({0.0f, 2.0f, 0.0f}));
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Dynamic, sphere(0.5f));

        float observedY = 2.0f;
        ctx.expect(Engine::isValid(physics.registerPhysicsSystems()), "physics scheduler system did not register");
        Engine::SceneSystemDescriptor observer;
        observer.name = "PhysicsPreRenderObserver";
        observer.phases = {Engine::SceneTickPhase::PreRender};
        observer.onTick = [&](Engine::Scene& observedScene, const Engine::SceneTickContext&) {
            const std::optional<glm::mat4> world = observedScene.worldMatrix(actor);
            if (world.has_value()) {
                observedY = (*world)[3].y;
            }
        };
        ctx.expect(Engine::isValid(scene.registerSystem(std::move(observer))), "pre-render observer did not register");
        ctx.expect(scene.load(), "scene load failed");
        ctx.expect(scene.start(), "scene start failed");
        scene.tickFixed(1.0f / 60.0f);
        scene.tickFrame(1.0f / 60.0f);
        ctx.expect(scene.stop(), "scene stop failed");
        ctx.expect(observedY < 2.0f, "pre-render observer did not see physics writeback");
    }

    void diagnosticsAndDebugRequestsUpdate(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        const Engine::SceneActorHandle actor = scene.createActor();
        createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Static, box(0.5f));

        [[maybe_unused]] const Engine::ScenePhysicsRaycastResult ray = physics.raycast({0.0f, 2.0f, 0.0f}, {0.0f, -2.0f, 0.0f});
        [[maybe_unused]] const Engine::ScenePhysicsClosestPointResult closest = physics.closestPoint({2.0f, 0.0f, 0.0f}, 10.0f);
        const Engine::ScenePhysicsDiagnostics diagnostics = physics.diagnostics();
        ctx.expect(diagnostics.bodyCount == 1 && diagnostics.colliderCount == 1, "diagnostic counts were wrong");
        ctx.expect(diagnostics.raycastCount == 1 && diagnostics.closestPointCount == 1, "query diagnostics were wrong");
        ctx.expect(!physics.debugRequests().empty(), "debug requests were not recorded");
        physics.clearDebugRequests();
        ctx.expect(physics.debugRequests().empty(), "debug requests did not clear");
    }

    void shutdownIsIdempotent(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsBodyHandle body;
        {
            Engine::ScenePhysicsWorld physics(scene);
            const Engine::SceneActorHandle actor = scene.createActor();
            body = createBodyWithCollider(physics, actor, Engine::ScenePhysicsMotionType::Static, box(0.5f));
            ctx.expect(physics.destroyBody(body), "first explicit destroy failed");
            ctx.expect(!physics.destroyBody(body), "second explicit destroy should fail cleanly");
        }
        ctx.expect(true, "physics world shutdown completed");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"BodyLifecycleInvalidatesStaleHandles", bodyLifecycleInvalidatesStaleHandles},
        {"ColliderLifecycleAndMalformedShapes", colliderLifecycleAndMalformedShapes},
        {"StaticBodyReadsSceneTransform", staticBodyReadsSceneTransform},
        {"KinematicTargetMovesBody", kinematicTargetMovesBody},
        {"DynamicBodyFallsOntoStaticFloor", dynamicBodyFallsOntoStaticFloor},
        {"DynamicBodyWritesBackSceneTransform", dynamicBodyWritesBackSceneTransform},
        {"ParentedDynamicBodyRejected", parentedDynamicBodyRejected},
        {"ActorDestroyFlushCleansPhysicsBodies", actorDestroyFlushCleansPhysicsBodies},
        {"RaycastHitAndMiss", raycastHitAndMiss},
        {"OverlapReturnsDeterministicHits", overlapReturnsDeterministicHits},
        {"CapsuleSweepDetectsBlockingShape", capsuleSweepDetectsBlockingShape},
        {"ClosestPointReportsExpectedResult", closestPointReportsExpectedResult},
        {"LayerFilterIncludesAndExcludesBodies", layerFilterIncludesAndExcludesBodies},
        {"SchedulerRunsFixedPhysicsOrder", schedulerRunsFixedPhysicsOrder},
        {"PreRenderCanObservePhysicsWriteback", preRenderCanObservePhysicsWriteback},
        {"DiagnosticsAndDebugRequestsUpdate", diagnosticsAndDebugRequestsUpdate},
        {"ShutdownIsIdempotent", shutdownIsIdempotent},
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

    std::cout << "Scene physics tests passed (" << tests.size() << ")\n";
    return 0;
}
