#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

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

    glm::mat4 compose(const Engine::SceneTransform& transform)
    {
        glm::mat4 matrix{1.0f};
        matrix = glm::translate(matrix, transform.translation);
        matrix *= glm::mat4_cast(glm::normalize(transform.rotation));
        matrix = glm::scale(matrix, transform.scale);
        return matrix;
    }

    void expectMat4(TestContext& ctx, const std::optional<glm::mat4>& actual, const glm::mat4& expected, std::string message)
    {
        ctx.expect(actual.has_value(), message + " was missing");
        if (actual.has_value()) {
            ctx.expect(nearlyEqual(*actual, expected), std::move(message));
        }
    }

    void expectHandles(TestContext& ctx, const std::vector<Engine::SceneActorHandle>& actual, const std::vector<Engine::SceneActorHandle>& expected, std::string message)
    {
        ctx.expect(actual.size() == expected.size(), message + " size was wrong");
        if (actual.size() == expected.size()) {
            ctx.expect(actual == expected, std::move(message));
        }
    }

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

    void defaultActorTransformIsIdentity(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();

        ctx.expect(scene.hasTransform(actor), "created actor did not report transform");
        ctx.expect(scene.parent(actor) == std::nullopt, "created actor unexpectedly had parent");
        expectHandles(ctx, scene.roots(), {actor}, "created actor roots");
        expectMat4(ctx, scene.localMatrix(actor), glm::mat4{1.0f}, "default local matrix");
        expectMat4(ctx, scene.worldMatrix(actor), glm::mat4{1.0f}, "default world matrix");
    }

    void setLocalTransformUpdatesWorld(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::SceneTransform transform{
            {2.0f, 3.0f, 4.0f},
            glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
            {1.0f, 2.0f, 3.0f},
        };

        ctx.expect(scene.setLocalTransform(actor, transform), "setLocalTransform failed");
        expectMat4(ctx, scene.localMatrix(actor), compose(transform), "local transform matrix");
        expectMat4(ctx, scene.worldMatrix(actor), compose(transform), "world transform matrix");
    }

    void attachChildKeepLocal(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        const Engine::SceneTransform parentTransform{{10.0f, 0.0f, 0.0f}};
        const Engine::SceneTransform childTransform{{1.0f, 2.0f, 3.0f}};
        scene.setLocalTransform(parent, parentTransform);
        scene.setLocalTransform(child, childTransform);

        ctx.expect(
            scene.attachChild(child, parent, false) == Engine::SceneTransformUpdateResult::Success,
            "attach keep-local failed");
        ctx.expect(scene.parent(child) == parent, "child parent was wrong after attach");
        expectHandles(ctx, scene.children(parent), {child}, "parent children after attach");
        expectMat4(ctx, scene.localMatrix(child), compose(childTransform), "child local after keep-local attach");
        expectMat4(ctx, scene.worldMatrix(child), compose(parentTransform) * compose(childTransform), "child world after keep-local attach");
    }

    void attachChildPreserveWorld(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        const Engine::SceneTransform parentTransform{{10.0f, 0.0f, 0.0f}};
        const Engine::SceneTransform childTransform{{1.0f, 2.0f, 3.0f}};
        scene.setLocalTransform(parent, parentTransform);
        scene.setLocalTransform(child, childTransform);
        const glm::mat4 originalWorld = *scene.worldMatrix(child);

        ctx.expect(
            scene.attachChild(child, parent, true) == Engine::SceneTransformUpdateResult::Success,
            "attach preserve-world failed");
        expectMat4(ctx, scene.worldMatrix(child), originalWorld, "child world after preserve-world attach");
        ctx.expect(nearlyEqual(scene.localTransform(child)->translation, {-9.0f, 2.0f, 3.0f}), "child local translation after preserve-world attach was wrong");
    }

    void detachChildPreserveWorld(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.setLocalTransform(parent, {{5.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(child, {{2.0f, 0.0f, 0.0f}});
        scene.attachChild(child, parent, false);
        const glm::mat4 originalWorld = *scene.worldMatrix(child);

        ctx.expect(
            scene.detachChild(child, true) == Engine::SceneTransformUpdateResult::Success,
            "detach preserve-world failed");
        ctx.expect(scene.parent(child) == std::nullopt, "detached child still had parent");
        expectMat4(ctx, scene.worldMatrix(child), originalWorld, "child world after preserve-world detach");
        ctx.expect(nearlyEqual(scene.localTransform(child)->translation, {7.0f, 0.0f, 0.0f}), "child local translation after detach was wrong");
    }

    void reparentPreserveWorldAndKeepLocal(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle firstParent = scene.createActor();
        const Engine::SceneActorHandle secondParent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.setLocalTransform(firstParent, {{5.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(secondParent, {{20.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(child, {{2.0f, 0.0f, 0.0f}});
        scene.attachChild(child, firstParent, false);
        const glm::mat4 firstWorld = *scene.worldMatrix(child);

        ctx.expect(
            scene.reparent(child, secondParent, true) == Engine::SceneTransformUpdateResult::Success,
            "reparent preserve-world failed");
        expectMat4(ctx, scene.worldMatrix(child), firstWorld, "child world after preserve-world reparent");
        ctx.expect(nearlyEqual(scene.localTransform(child)->translation, {-13.0f, 0.0f, 0.0f}), "child local after preserve-world reparent was wrong");

        ctx.expect(
            scene.reparent(child, firstParent, false) == Engine::SceneTransformUpdateResult::Success,
            "reparent keep-local failed");
        expectMat4(ctx, scene.worldMatrix(child), compose({{5.0f, 0.0f, 0.0f}}) * compose({{-13.0f, 0.0f, 0.0f}}), "child world after keep-local reparent");
    }

    void dirtyPropagation(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        const Engine::SceneActorHandle grandchild = scene.createActor();
        scene.setLocalTransform(parent, {{1.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(child, {{2.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(grandchild, {{3.0f, 0.0f, 0.0f}});
        scene.attachChild(child, parent, false);
        scene.attachChild(grandchild, child, false);
        expectMat4(ctx, scene.worldMatrix(grandchild), compose({{6.0f, 0.0f, 0.0f}}), "initial grandchild world");

        scene.setLocalTransform(parent, {{10.0f, 0.0f, 0.0f}});
        scene.updateWorldTransforms();
        expectMat4(ctx, scene.worldMatrix(grandchild), compose({{15.0f, 0.0f, 0.0f}}), "updated grandchild world");
    }

    void rootsAndChildrenAreDeterministic(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle first = scene.createActor();
        const Engine::SceneActorHandle second = scene.createActor();
        const Engine::SceneActorHandle third = scene.createActor();
        const Engine::SceneActorHandle fourth = scene.createActor();
        scene.attachChild(third, first, false);
        scene.attachChild(second, first, false);

        expectHandles(ctx, scene.roots(), {first, fourth}, "root order");
        expectHandles(ctx, scene.children(first), {third, second}, "child insertion order");
    }

    void cycleAndInvalidParentRejected(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        const Engine::SceneActorHandle grandchild = scene.createActor();
        scene.attachChild(child, parent, false);
        scene.attachChild(grandchild, child, false);

        ctx.expect(scene.attachChild({}, parent, false) == Engine::SceneTransformUpdateResult::InvalidActor, "invalid child was not rejected");
        ctx.expect(scene.attachChild(child, {}, false) == Engine::SceneTransformUpdateResult::InvalidParent, "invalid parent was not rejected");
        ctx.expect(scene.attachChild(parent, parent, false) == Engine::SceneTransformUpdateResult::SelfParent, "self parent was not rejected");
        ctx.expect(scene.attachChild(parent, grandchild, false) == Engine::SceneTransformUpdateResult::Cycle, "cycle was not rejected");
        expectHandles(ctx, scene.children(parent), {child}, "hierarchy changed after rejected operations");
    }

    void destroyParentDetachesChildren(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.setLocalTransform(parent, {{5.0f, 0.0f, 0.0f}});
        scene.setLocalTransform(child, {{2.0f, 0.0f, 0.0f}});
        scene.attachChild(child, parent, false);
        const glm::mat4 childWorld = *scene.worldMatrix(child);

        scene.destroyActor(parent);
        scene.flushDestroyedActors();
        ctx.expect(scene.contains(child), "child was destroyed with parent");
        ctx.expect(scene.parent(child) == std::nullopt, "child still had destroyed parent");
        expectHandles(ctx, scene.roots(), {child}, "roots after parent destroy");
        expectMat4(ctx, scene.worldMatrix(child), childWorld, "child world after parent destroy");
    }

    void destroyChildRemovesFromParent(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.attachChild(child, parent, false);

        scene.destroyActor(child);
        scene.flushDestroyedActors();
        expectHandles(ctx, scene.children(parent), {}, "parent children after child destroy");
        expectHandles(ctx, scene.roots(), {parent}, "roots after child destroy");
    }

    void pendingDestroyExcludedFromHierarchyQueries(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.attachChild(child, parent, false);

        scene.destroyActor(parent);
        ctx.expect(!scene.hasTransform(parent), "pending actor still reported transform");
        ctx.expect(!scene.localTransform(parent).has_value(), "pending actor returned local transform");
        ctx.expect(!scene.worldMatrix(parent).has_value(), "pending actor returned world matrix");
        ctx.expect(scene.children(parent).empty(), "pending actor returned children");
        ctx.expect(scene.attachChild(child, parent, false) == Engine::SceneTransformUpdateResult::InvalidParent, "pending actor accepted as parent");
    }

    void reparentFromPendingParentSurvivesFlush(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle oldParent = scene.createActor();
        const Engine::SceneActorHandle newParent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.attachChild(child, oldParent, false);

        scene.destroyActor(oldParent);
        ctx.expect(
            scene.attachChild(child, newParent, false) == Engine::SceneTransformUpdateResult::Success,
            "reparent from pending old parent failed");
        scene.flushDestroyedActors();

        ctx.expect(scene.parent(child) == newParent, "child did not keep new parent after old parent flush");
        expectHandles(ctx, scene.children(newParent), {child}, "new parent children after old parent flush");
    }

    void actorSlotReuseClearsHierarchyState(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        scene.setLocalTransform(parent, {{5.0f, 0.0f, 0.0f}});
        scene.attachChild(child, parent, false);
        scene.destroyActor(parent);
        scene.flushDestroyedActors();

        const Engine::SceneActorHandle reused = scene.createActor();
        ctx.expect(reused.index == parent.index, "actor slot was not reused for hierarchy cleanup test");
        ctx.expect(scene.parent(reused) == std::nullopt, "reused actor kept old parent");
        ctx.expect(scene.children(reused).empty(), "reused actor kept old children");
        expectMat4(ctx, scene.worldMatrix(reused), glm::mat4{1.0f}, "reused actor world matrix");
    }

    void nonDecomposablePreserveWorldFails(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        Engine::SceneTransform zeroScale;
        zeroScale.scale = {0.0f, 1.0f, 1.0f};
        scene.setLocalTransform(parent, zeroScale);

        ctx.expect(
            scene.attachChild(child, parent, true) == Engine::SceneTransformUpdateResult::NonDecomposableTransform,
            "non-decomposable preserve-world attach did not fail");
        ctx.expect(scene.parent(child) == std::nullopt, "failed preserve-world attach mutated parent");
        ctx.expect(scene.children(parent).empty(), "failed preserve-world attach mutated children");
    }

    size_t phaseIndex(Engine::SceneTickPhase phase)
    {
        return static_cast<size_t>(phase);
    }

    void lifecycleTransitions(TestContext& ctx)
    {
        Engine::Scene scene;
        std::vector<std::string> calls;

        Engine::SceneSystemDescriptor first;
        first.name = "first";
        first.onLoad = [&](Engine::Scene&) { calls.push_back("first.load"); };
        first.onStart = [&](Engine::Scene&) { calls.push_back("first.start"); };
        first.onStop = [&](Engine::Scene&) { calls.push_back("first.stop"); };
        first.onUnload = [&](Engine::Scene&) { calls.push_back("first.unload"); };
        [[maybe_unused]] const Engine::SceneSystemHandle firstHandle = scene.registerSystem(std::move(first));

        Engine::SceneSystemDescriptor second;
        second.name = "second";
        second.onLoad = [&](Engine::Scene&) { calls.push_back("second.load"); };
        second.onStart = [&](Engine::Scene&) { calls.push_back("second.start"); };
        second.onStop = [&](Engine::Scene&) { calls.push_back("second.stop"); };
        second.onUnload = [&](Engine::Scene&) { calls.push_back("second.unload"); };
        [[maybe_unused]] const Engine::SceneSystemHandle secondHandle = scene.registerSystem(std::move(second));

        ctx.expect(scene.load(), "load failed");
        ctx.expect(scene.start(), "start failed");
        ctx.expect(scene.stop(), "stop failed");
        ctx.expect(scene.unload(), "unload failed");
        ctx.expect(scene.lifecycleState() == Engine::SceneLifecycleState::Unloaded, "final lifecycle state was wrong");
        ctx.expect(calls == std::vector<std::string>{
            "first.load",
            "second.load",
            "first.start",
            "second.start",
            "second.stop",
            "first.stop",
            "second.unload",
            "first.unload",
        }, "lifecycle callback order was wrong");
    }

    void invalidLifecycleTransitionsAreNoOps(TestContext& ctx)
    {
        Engine::Scene scene;
        uint32_t calls = 0;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.onStart = [&](Engine::Scene&) { ++calls; };
        descriptor.onStop = [&](Engine::Scene&) { ++calls; };
        [[maybe_unused]] const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));

        ctx.expect(!scene.start(), "start from unloaded unexpectedly succeeded");
        ctx.expect(!scene.stop(), "stop from unloaded unexpectedly succeeded");
        ctx.expect(!scene.unload(), "unload from unloaded unexpectedly succeeded");
        ctx.expect(calls == 0, "invalid lifecycle transition ran callbacks");
        ctx.expect(scene.lifecycleState() == Engine::SceneLifecycleState::Unloaded, "invalid lifecycle changed state");
    }

    void framePhaseOrder(TestContext& ctx)
    {
        Engine::Scene scene;
        std::vector<Engine::SceneTickPhase> phases;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.phases = {
            Engine::SceneTickPhase::BeginFrame,
            Engine::SceneTickPhase::VariableAnimation,
            Engine::SceneTickPhase::VariableUpdate,
            Engine::SceneTickPhase::PreRender,
            Engine::SceneTickPhase::EndFrame,
        };
        descriptor.onTick = [&](Engine::Scene&, const Engine::SceneTickContext& tick) {
            phases.push_back(tick.phase);
            ctx.expect(tick.frameIndex == 1, "frame index was not incremented before tickFrame phases");
            ctx.expect(!tick.fixedStep, "variable frame phase was marked fixed");
        };
        [[maybe_unused]] const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));
        scene.load();
        scene.start();
        scene.tickFrame(0.016f);

        ctx.expect(phases == std::vector<Engine::SceneTickPhase>{
            Engine::SceneTickPhase::BeginFrame,
            Engine::SceneTickPhase::VariableAnimation,
            Engine::SceneTickPhase::VariableUpdate,
            Engine::SceneTickPhase::PreRender,
            Engine::SceneTickPhase::EndFrame,
        }, "tickFrame phase order was wrong");
    }

    void fixedPhaseOrder(TestContext& ctx)
    {
        Engine::Scene scene;
        std::vector<Engine::SceneTickPhase> phases;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.phases = {
            Engine::SceneTickPhase::FixedPrePhysics,
            Engine::SceneTickPhase::FixedPhysics,
            Engine::SceneTickPhase::FixedPostPhysics,
        };
        descriptor.onTick = [&](Engine::Scene&, const Engine::SceneTickContext& tick) {
            phases.push_back(tick.phase);
            ctx.expect(tick.fixedStepIndex == 1, "fixed step index was not incremented before fixed phases");
            ctx.expect(tick.fixedStep, "fixed phase was not marked fixed");
        };
        [[maybe_unused]] const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));
        scene.load();
        scene.start();
        scene.tickFixed(1.0f / 60.0f);

        ctx.expect(phases == std::vector<Engine::SceneTickPhase>{
            Engine::SceneTickPhase::FixedPrePhysics,
            Engine::SceneTickPhase::FixedPhysics,
            Engine::SceneTickPhase::FixedPostPhysics,
        }, "tickFixed phase order was wrong");
        ctx.expect(scene.schedulerDiagnostics().fixedStepIndex == 1, "fixed diagnostics index was wrong");
    }

    void registeredSystemOrderIsDeterministic(TestContext& ctx)
    {
        Engine::Scene scene;
        std::vector<std::string> calls;
        for (std::string name : {"first", "second", "third"}) {
            Engine::SceneSystemDescriptor descriptor;
            descriptor.name = name;
            descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
            descriptor.onTick = [&, name](Engine::Scene&, const Engine::SceneTickContext&) {
                calls.push_back(name);
            };
            [[maybe_unused]] const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));
        }
        scene.load();
        scene.start();
        scene.tickPhase(Engine::SceneTickPhase::VariableUpdate, 0.1f);
        ctx.expect(calls == std::vector<std::string>{"first", "second", "third"}, "system tick order was not registration order");
    }

    void systemEnableDisable(TestContext& ctx)
    {
        Engine::Scene scene;
        uint32_t calls = 0;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.enabled = false;
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        descriptor.onLoad = [&](Engine::Scene&) { ++calls; };
        descriptor.onTick = [&](Engine::Scene&, const Engine::SceneTickContext&) { ++calls; };
        const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));

        scene.load();
        scene.start();
        scene.tickPhase(Engine::SceneTickPhase::VariableUpdate, 0.1f);
        ctx.expect(calls == 0, "disabled system ran callbacks");
        ctx.expect(scene.schedulerDiagnostics().skippedCallbackCount >= 2, "disabled callbacks were not counted as skipped");

        scene.stop();
        ctx.expect(scene.setSystemEnabled(system, true), "setSystemEnabled failed");
        scene.start();
        scene.tickPhase(Engine::SceneTickPhase::VariableUpdate, 0.1f);
        ctx.expect(calls == 1, "enabled system did not tick");
    }

    void pausedSceneSkipsSimulation(TestContext& ctx)
    {
        Engine::Scene scene;
        std::vector<Engine::SceneTickPhase> phases;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.phases = {
            Engine::SceneTickPhase::BeginFrame,
            Engine::SceneTickPhase::FixedPrePhysics,
            Engine::SceneTickPhase::FixedPhysics,
            Engine::SceneTickPhase::FixedPostPhysics,
            Engine::SceneTickPhase::VariableAnimation,
            Engine::SceneTickPhase::VariableUpdate,
            Engine::SceneTickPhase::PreRender,
            Engine::SceneTickPhase::EndFrame,
        };
        descriptor.onTick = [&](Engine::Scene&, const Engine::SceneTickContext& tick) {
            phases.push_back(tick.phase);
            ctx.expect(tick.paused, "paused context flag was false");
        };
        [[maybe_unused]] const Engine::SceneSystemHandle registeredSystem = scene.registerSystem(std::move(descriptor));
        scene.load();
        scene.start();
        scene.setPaused(true);
        scene.tickFixed(1.0f / 60.0f);
        scene.tickFrame(0.016f);

        ctx.expect(phases == std::vector<Engine::SceneTickPhase>{
            Engine::SceneTickPhase::BeginFrame,
            Engine::SceneTickPhase::VariableAnimation,
            Engine::SceneTickPhase::PreRender,
            Engine::SceneTickPhase::EndFrame,
        }, "paused scene ran wrong phases");
        ctx.expect(scene.schedulerDiagnostics().skippedPhaseCount >= 1, "paused skipped variable update was not counted");
    }

    void tickIgnoredWhenNotStarted(TestContext& ctx)
    {
        Engine::Scene scene;
        uint32_t calls = 0;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        descriptor.onTick = [&](Engine::Scene&, const Engine::SceneTickContext&) { ++calls; };
        [[maybe_unused]] const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));
        scene.tickPhase(Engine::SceneTickPhase::VariableUpdate, 0.1f);
        ctx.expect(calls == 0, "tick before start ran callbacks");
        ctx.expect(scene.schedulerDiagnostics().skippedPhaseCount == 1, "tick before start did not count skipped phase");

        scene.load();
        scene.start();
        scene.stop();
        scene.tickPhase(Engine::SceneTickPhase::VariableUpdate, 0.1f);
        ctx.expect(calls == 0, "tick after stop ran callbacks");
    }

    void unregisterInvalidatesHandle(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneSystemDescriptor descriptor;
        const Engine::SceneSystemHandle first = scene.registerSystem(std::move(descriptor));
        ctx.expect(Engine::isValid(first), "registered system handle was invalid");
        ctx.expect(scene.contains(first), "scene did not contain registered system");
        ctx.expect(scene.unregisterSystem(first), "unregisterSystem failed");
        ctx.expect(!scene.contains(first), "stale system handle still validated");

        Engine::SceneSystemDescriptor secondDescriptor;
        const Engine::SceneSystemHandle second = scene.registerSystem(std::move(secondDescriptor));
        ctx.expect(first.index == second.index, "system slot was not reused");
        ctx.expect(first.generation != second.generation, "system generation did not increment on reuse");
    }

    void registerWhileStartedRejected(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneSystemDescriptor descriptor;
        const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));
        scene.load();
        scene.start();
        ctx.expect(!Engine::isValid(scene.registerSystem({})), "register while started unexpectedly succeeded");
        ctx.expect(!scene.unregisterSystem(system), "unregister while started unexpectedly succeeded");
    }

    void transformUpdateBeforePreRender(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        scene.setLocalTransform(actor, {{1.0f, 0.0f, 0.0f}});
        [[maybe_unused]] const std::optional<glm::mat4> initialWorld = scene.worldMatrix(actor);

        Engine::SceneSystemDescriptor updater;
        updater.phases = {Engine::SceneTickPhase::VariableUpdate};
        updater.onTick = [actor](Engine::Scene& tickScene, const Engine::SceneTickContext&) {
            tickScene.setLocalTransform(actor, {{5.0f, 0.0f, 0.0f}});
        };
        [[maybe_unused]] const Engine::SceneSystemHandle updaterSystem = scene.registerSystem(std::move(updater));

        Engine::SceneSystemDescriptor observer;
        observer.phases = {Engine::SceneTickPhase::PreRender};
        observer.onTick = [actor, &ctx](Engine::Scene& tickScene, const Engine::SceneTickContext&) {
            expectMat4(ctx, tickScene.worldMatrix(actor), compose({{5.0f, 0.0f, 0.0f}}), "pre-render world matrix");
        };
        [[maybe_unused]] const Engine::SceneSystemHandle observerSystem = scene.registerSystem(std::move(observer));

        scene.load();
        scene.start();
        scene.tickFrame(0.016f);
    }

    void diagnosticsReportPhaseCounts(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneSystemDescriptor descriptor;
        descriptor.phases = {Engine::SceneTickPhase::BeginFrame, Engine::SceneTickPhase::EndFrame};
        descriptor.onTick = [](Engine::Scene&, const Engine::SceneTickContext&) {};
        [[maybe_unused]] const Engine::SceneSystemHandle system = scene.registerSystem(std::move(descriptor));
        scene.load();
        scene.start();
        scene.tickFrame(0.016f);

        const Engine::SceneSchedulerDiagnostics diagnostics = scene.schedulerDiagnostics();
        ctx.expect(diagnostics.lifecycleState == Engine::SceneLifecycleState::Started, "diagnostics lifecycle state was wrong");
        ctx.expect(diagnostics.frameIndex == 1, "diagnostics frame index was wrong");
        ctx.expect(diagnostics.registeredSystemCount == 1, "diagnostics registered count was wrong");
        ctx.expect(diagnostics.enabledSystemCount == 1, "diagnostics enabled count was wrong");
        ctx.expect(diagnostics.lastPhase == Engine::SceneTickPhase::EndFrame, "diagnostics last phase was wrong");
        ctx.expect(diagnostics.phases.size() == phaseIndex(Engine::SceneTickPhase::Count), "diagnostics phase vector size was wrong");
        ctx.expect(diagnostics.phases[phaseIndex(Engine::SceneTickPhase::BeginFrame)].callbackCount == 1, "begin frame callback count was wrong");
        ctx.expect(diagnostics.phases[phaseIndex(Engine::SceneTickPhase::EndFrame)].callbackCount == 1, "end frame callback count was wrong");
    }

    void destroyActorDuringTick(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        uint32_t calls = 0;
        Engine::SceneSystemDescriptor destroyer;
        destroyer.phases = {Engine::SceneTickPhase::VariableUpdate};
        destroyer.onTick = [actor, &calls](Engine::Scene& tickScene, const Engine::SceneTickContext&) {
            ++calls;
            tickScene.destroyActor(actor);
            tickScene.flushDestroyedActors();
        };
        [[maybe_unused]] const Engine::SceneSystemHandle destroyerSystem = scene.registerSystem(std::move(destroyer));

        Engine::SceneSystemDescriptor follower;
        follower.phases = {Engine::SceneTickPhase::VariableUpdate};
        follower.onTick = [&calls](Engine::Scene&, const Engine::SceneTickContext&) {
            ++calls;
        };
        [[maybe_unused]] const Engine::SceneSystemHandle followerSystem = scene.registerSystem(std::move(follower));

        scene.load();
        scene.start();
        scene.tickPhase(Engine::SceneTickPhase::VariableUpdate, 0.1f);
        ctx.expect(calls == 2, "destroying actor during tick broke scheduler iteration");
        ctx.expect(!scene.contains(actor), "actor survived destroy/flush during tick");
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
        {"DefaultActorTransformIsIdentity", defaultActorTransformIsIdentity},
        {"SetLocalTransformUpdatesWorld", setLocalTransformUpdatesWorld},
        {"AttachChildKeepLocal", attachChildKeepLocal},
        {"AttachChildPreserveWorld", attachChildPreserveWorld},
        {"DetachChildPreserveWorld", detachChildPreserveWorld},
        {"ReparentPreserveWorldAndKeepLocal", reparentPreserveWorldAndKeepLocal},
        {"DirtyPropagation", dirtyPropagation},
        {"RootsAndChildrenAreDeterministic", rootsAndChildrenAreDeterministic},
        {"CycleAndInvalidParentRejected", cycleAndInvalidParentRejected},
        {"DestroyParentDetachesChildren", destroyParentDetachesChildren},
        {"DestroyChildRemovesFromParent", destroyChildRemovesFromParent},
        {"PendingDestroyExcludedFromHierarchyQueries", pendingDestroyExcludedFromHierarchyQueries},
        {"ReparentFromPendingParentSurvivesFlush", reparentFromPendingParentSurvivesFlush},
        {"ActorSlotReuseClearsHierarchyState", actorSlotReuseClearsHierarchyState},
        {"NonDecomposablePreserveWorldFails", nonDecomposablePreserveWorldFails},
        {"LifecycleTransitions", lifecycleTransitions},
        {"InvalidLifecycleTransitionsAreNoOps", invalidLifecycleTransitionsAreNoOps},
        {"FramePhaseOrder", framePhaseOrder},
        {"FixedPhaseOrder", fixedPhaseOrder},
        {"RegisteredSystemOrderIsDeterministic", registeredSystemOrderIsDeterministic},
        {"SystemEnableDisable", systemEnableDisable},
        {"PausedSceneSkipsSimulation", pausedSceneSkipsSimulation},
        {"TickIgnoredWhenNotStarted", tickIgnoredWhenNotStarted},
        {"UnregisterInvalidatesHandle", unregisterInvalidatesHandle},
        {"RegisterWhileStartedRejected", registerWhileStartedRejected},
        {"TransformUpdateBeforePreRender", transformUpdateBeforePreRender},
        {"DiagnosticsReportPhaseCounts", diagnosticsReportPhaseCounts},
        {"DestroyActorDuringTick", destroyActorDuringTick},
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
