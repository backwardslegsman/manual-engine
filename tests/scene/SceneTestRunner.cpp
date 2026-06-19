#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Scene/Scene.hpp"

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

    void createDestroyActor(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneObjectId stableId{42};
        const Engine::SceneActorHandle actor = scene.createActor(stableId);

        ctx.expect(Engine::isValid(actor), "created actor handle was invalid");
        ctx.expect(scene.contains(actor), "scene did not contain created actor");
        ctx.expect(scene.stableId(actor) == stableId, "stable actor ID was not preserved");
        ctx.expect(scene.actorState(actor) == Engine::SceneActorState::Active, "created actor was not active");

        ctx.expect(scene.destroyActor(actor), "destroyActor failed for an active actor");
        ctx.expect(!scene.contains(actor), "pending-destroy actor still validated through contains");
        ctx.expect(scene.actorState(actor) == Engine::SceneActorState::PendingDestroy, "destroyed actor was not pending");
        ctx.expect(scene.flushDestroyedActors(), "flushDestroyedActors did not report reclaimed actor");
        ctx.expect(!scene.contains(actor), "flushed actor still validated");
        ctx.expect(!scene.actorState(actor).has_value(), "flushed actor still reported state");
    }

    void actorSlotReuseInvalidatesStaleHandle(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle first = scene.createActor({11});
        ctx.expect(scene.destroyActor(first), "destroyActor failed for first actor");
        ctx.expect(scene.flushDestroyedActors(), "flush did not reclaim first actor");

        const Engine::SceneActorHandle second = scene.createActor({12});
        ctx.expect(scene.contains(second), "scene did not contain reused actor");
        ctx.expect(first.index == second.index, "actor slot was not reused deterministically");
        ctx.expect(first.generation != second.generation, "reused actor slot did not increment generation");
        ctx.expect(!scene.contains(first), "stale actor handle validated after slot reuse");
        ctx.expect(scene.stableId(second) == Engine::SceneObjectId{12}, "reused actor stable ID was wrong");
    }

    void componentAttachDetach(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::SceneComponentTypeId meshType{101};
        const Engine::SceneComponentTypeId lightType{102};

        const Engine::SceneComponentHandle meshA = scene.attachComponent(actor, meshType);
        const Engine::SceneComponentHandle light = scene.attachComponent(actor, lightType);
        const Engine::SceneComponentHandle meshB = scene.attachComponent(actor, meshType);

        ctx.expect(Engine::isValid(meshA), "first component handle was invalid");
        ctx.expect(Engine::isValid(light), "second component handle was invalid");
        ctx.expect(Engine::isValid(meshB), "third component handle was invalid");
        ctx.expect(scene.contains(meshA), "scene did not contain first component");
        ctx.expect(scene.componentOwner(meshA) == actor, "component owner was wrong");
        ctx.expect(scene.componentType(light) == lightType, "component type was wrong");

        const std::vector<Engine::SceneComponentHandle> components = scene.components(actor);
        ctx.expect(components.size() == 3, "actor component list size was wrong");
        ctx.expect(components.size() >= 3 && components[0] == meshA && components[1] == light && components[2] == meshB,
            "actor component order was not attach order");
        ctx.expect(scene.firstComponent(actor, meshType) == meshA, "first component by type did not return first match");

        ctx.expect(scene.detachComponent(light), "detachComponent failed for live component");
        ctx.expect(!scene.contains(light), "detached component still validated");
        ctx.expect(!scene.componentOwner(light).has_value(), "detached component still reported owner");

        const std::vector<Engine::SceneComponentHandle> remaining = scene.components(actor);
        ctx.expect(remaining.size() == 2, "component list size after detach was wrong");
        ctx.expect(
            remaining.size() >= 2 && remaining[0] == meshA && remaining[1] == meshB,
            "component list after detach did not preserve remaining order");
    }

    void destroyActorDetachesComponents(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::SceneComponentHandle first = scene.attachComponent(actor, {201});
        const Engine::SceneComponentHandle second = scene.attachComponent(actor, {202});

        ctx.expect(scene.destroyActor(actor), "destroyActor failed for actor with components");
        ctx.expect(!Engine::isValid(scene.attachComponent(actor, {203})), "pending-destroy actor accepted component");
        ctx.expect(scene.components(actor).empty(), "pending-destroy actor returned active components");
        ctx.expect(scene.flushDestroyedActors(), "flush did not reclaim actor with components");
        ctx.expect(!scene.contains(first), "first component survived owner flush");
        ctx.expect(!scene.contains(second), "second component survived owner flush");
    }

    void iterationSkipsPendingDestroy(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle first = scene.createActor();
        const Engine::SceneActorHandle second = scene.createActor();
        const Engine::SceneActorHandle third = scene.createActor();
        ctx.expect(scene.destroyActor(second), "destroyActor failed for middle actor");

        std::vector<Engine::SceneActorHandle> visited;
        scene.forEachActor([&](Engine::SceneActorHandle actor) {
            visited.push_back(actor);
        });

        ctx.expect(visited.size() == 2, "iteration visited wrong actor count");
        ctx.expect(visited.size() >= 2 && visited[0] == first && visited[1] == third,
            "iteration did not visit active actors in slot order");
    }

    void invalidInputsAreNoOps(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::SceneComponentHandle component = scene.attachComponent(actor, {301});

        ctx.expect(!Engine::isValid(Engine::SceneActorHandle{}), "default actor handle was valid");
        ctx.expect(!Engine::isValid(Engine::SceneComponentHandle{}), "default component handle was valid");
        ctx.expect(!Engine::isValid(Engine::SceneObjectId{}), "default scene object ID was valid");
        ctx.expect(!Engine::isValid(Engine::SceneComponentTypeId{}), "default component type ID was valid");
        ctx.expect(!scene.destroyActor({}), "destroyActor accepted invalid actor");
        ctx.expect(!scene.detachComponent({}), "detachComponent accepted invalid component");
        ctx.expect(!Engine::isValid(scene.attachComponent(actor, {})), "attachComponent accepted invalid type");

        ctx.expect(scene.destroyActor(actor), "destroyActor failed for valid actor");
        ctx.expect(!scene.destroyActor(actor), "destroyActor accepted pending actor twice");
        ctx.expect(scene.flushDestroyedActors(), "flush did not reclaim actor");
        ctx.expect(!scene.destroyActor(actor), "destroyActor accepted stale actor");
        ctx.expect(!scene.detachComponent(component), "detachComponent accepted stale component");
        ctx.expect(!scene.flushDestroyedActors(), "empty flush reported mutation");
        ctx.expect(!scene.stableId(actor).has_value(), "stale actor returned stable ID");
        ctx.expect(!scene.componentType(component).has_value(), "stale component returned type");
    }

    void stableIdIsNotRuntimeHandle(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor({1});

        ctx.expect(actor.index == 0, "first actor did not use expected slot for test");
        ctx.expect(actor.generation == 1, "first actor did not use expected generation for test");
        ctx.expect(scene.stableId(actor) == Engine::SceneObjectId{1}, "stable ID was not returned explicitly");
        ctx.expect(scene.stableId(actor)->value == actor.index + actor.generation,
            "test stable ID did not overlap handle fields as intended");

        const Engine::SceneActorHandle other = scene.createActor({999});
        ctx.expect(scene.stableId(other)->value != other.index, "stable ID unexpectedly mirrored runtime index");
        ctx.expect(scene.stableId(other)->value != other.generation, "stable ID unexpectedly mirrored generation");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"CreateDestroyActor", createDestroyActor},
        {"ActorSlotReuseInvalidatesStaleHandle", actorSlotReuseInvalidatesStaleHandle},
        {"ComponentAttachDetach", componentAttachDetach},
        {"DestroyActorDetachesComponents", destroyActorDetachesComponents},
        {"IterationSkipsPendingDestroy", iterationSkipsPendingDestroy},
        {"InvalidInputsAreNoOps", invalidInputsAreNoOps},
        {"StableIdIsNotRuntimeHandle", stableIdIsNotRuntimeHandle},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
    }

    if (failures.empty()) {
        std::cout << "Scene tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Scene tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  " << failure.testName << ": " << failure.message << '\n';
    }
    return 1;
}
