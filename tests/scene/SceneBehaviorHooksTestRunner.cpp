#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneBehaviorHooks.hpp"
#include "Engine/SceneReflection.hpp"

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

    bool near(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs.x - rhs.x) <= epsilon &&
            std::abs(lhs.y - rhs.y) <= epsilon &&
            std::abs(lhs.z - rhs.z) <= epsilon;
    }

    struct Fixture {
        Engine::Scene scene;
        Engine::ReflectionRegistry registry;
        Engine::SceneReflectionContext reflectionContext;
        Engine::SceneBehaviorHooks hooks;

        Fixture()
            : hooks(scene, registry, reflectionContext)
        {
            Engine::registerSceneReflectionDescriptors(registry);
            reflectionContext.scene = &scene;
        }
    };

    Engine::SceneBehaviorDescriptor behavior(
        uint32_t type,
        std::string name = {},
        Engine::SceneBehaviorTargetKind targetKind = Engine::SceneBehaviorTargetKind::Scene,
        Engine::OpaqueHandle target = {})
    {
        Engine::SceneBehaviorDescriptor descriptor;
        descriptor.type = Engine::SceneBehaviorTypeId{type};
        descriptor.debugName = std::move(name);
        descriptor.targetKind = targetKind;
        descriptor.target = target;
        return descriptor;
    }

    void BehaviorHandleLifecycleInvalidatesStaleHandles(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneBehaviorHandle first = fixture.hooks.registerBehavior(behavior(1));
        ctx.expect(Engine::isValid(first), "first behavior handle is valid");
        ctx.expect(fixture.hooks.contains(first), "first behavior is contained");
        ctx.expect(fixture.hooks.unregisterBehavior(first), "unregister succeeds while unloaded");
        ctx.expect(!fixture.hooks.contains(first), "unregistered behavior is absent");

        const Engine::SceneBehaviorHandle second = fixture.hooks.registerBehavior(behavior(2));
        ctx.expect(second.index == first.index, "slot is reused deterministically");
        ctx.expect(second.generation != first.generation, "reused behavior increments generation");
        ctx.expect(!fixture.hooks.contains(first), "stale behavior handle is invalidated");
    }

    void RegisterAndUnregisterRejectedWhileStarted(TestContext& ctx)
    {
        Fixture fixture;
        ctx.expect(fixture.hooks.registerSchedulerSystem() == Engine::SceneBehaviorStatus::Success, "scheduler registered");
        const Engine::SceneBehaviorHandle handle = fixture.hooks.registerBehavior(behavior(1));
        ctx.expect(fixture.scene.load(), "scene loaded");
        ctx.expect(fixture.scene.start(), "scene started");
        ctx.expect(!Engine::isValid(fixture.hooks.registerBehavior(behavior(2))), "register rejected while started");
        ctx.expect(!fixture.hooks.unregisterBehavior(handle), "unregister rejected while started");
        ctx.expect(fixture.hooks.contains(handle), "behavior remains registered after rejected unregister");
    }

    void LifecycleCallbacksUseDeterministicOrder(TestContext& ctx)
    {
        Fixture fixture;
        std::vector<int> order;
        auto make = [&](int id) {
            Engine::SceneBehaviorDescriptor descriptor = behavior(static_cast<uint32_t>(id));
            descriptor.onLoad = [&, id](Engine::SceneBehaviorContext&) {
                order.push_back(id * 10 + 1);
                return Engine::SceneBehaviorResult::success();
            };
            descriptor.onStart = [&, id](Engine::SceneBehaviorContext&) {
                order.push_back(id * 10 + 2);
                return Engine::SceneBehaviorResult::success();
            };
            descriptor.onStop = [&, id](Engine::SceneBehaviorContext&) {
                order.push_back(id * 10 + 3);
                return Engine::SceneBehaviorResult::success();
            };
            descriptor.onUnload = [&, id](Engine::SceneBehaviorContext&) {
                order.push_back(id * 10 + 4);
                return Engine::SceneBehaviorResult::success();
            };
            return descriptor;
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(make(1));
        (void)fixture.hooks.registerBehavior(make(2));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        (void)fixture.scene.stop();
        (void)fixture.scene.unload();

        const std::vector<int> expected{11, 21, 12, 22, 23, 13, 24, 14};
        ctx.expect(order == expected, "load/start run forward and stop/unload run reverse");
    }

    void TickCallbacksUsePhasePriorityAndRegistrationOrder(TestContext& ctx)
    {
        Fixture fixture;
        std::vector<int> order;
        auto make = [&](int id, int priority) {
            Engine::SceneBehaviorDescriptor descriptor = behavior(static_cast<uint32_t>(id));
            descriptor.priority = priority;
            descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
            descriptor.onTick = [&, id](Engine::SceneBehaviorContext&) {
                order.push_back(id);
                return Engine::SceneBehaviorResult::success();
            };
            return descriptor;
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(make(1, 10));
        (void)fixture.hooks.registerBehavior(make(2, 0));
        (void)fixture.hooks.registerBehavior(make(3, 10));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        const std::vector<int> expected{2, 1, 3};
        ctx.expect(order == expected, "tick order uses priority then registration order");
    }

    void DisabledBehaviorsSkipCallbacksAndReportDiagnostics(TestContext& ctx)
    {
        Fixture fixture;
        uint32_t ticks = 0;
        Engine::SceneBehaviorDescriptor descriptor = behavior(1);
        descriptor.enabled = false;
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        descriptor.onTick = [&](Engine::SceneBehaviorContext&) {
            ++ticks;
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(descriptor));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        const auto diagnostics = fixture.hooks.diagnostics();
        ctx.expect(ticks == 0, "disabled behavior did not tick");
        ctx.expect(diagnostics.skippedCallbackCount > 0, "disabled behavior increments skipped callbacks");
        ctx.expect(diagnostics.disabledCount == 1, "disabled behavior counted");
    }

    void PausedSceneSkipsFixedPhaseHooksThroughScheduler(TestContext& ctx)
    {
        Fixture fixture;
        uint32_t fixedTicks = 0;
        uint32_t beginTicks = 0;
        Engine::SceneBehaviorDescriptor fixed = behavior(1);
        fixed.phases = {Engine::SceneTickPhase::FixedPrePhysics};
        fixed.onTick = [&](Engine::SceneBehaviorContext&) {
            ++fixedTicks;
            return Engine::SceneBehaviorResult::success();
        };
        Engine::SceneBehaviorDescriptor begin = behavior(2);
        begin.phases = {Engine::SceneTickPhase::BeginFrame};
        begin.onTick = [&](Engine::SceneBehaviorContext&) {
            ++beginTicks;
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(fixed));
        (void)fixture.hooks.registerBehavior(std::move(begin));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.setPaused(true);
        fixture.scene.tickFixed(1.0f / 60.0f);
        fixture.scene.tickFrame(0.016f);

        ctx.expect(fixedTicks == 0, "paused scene skipped fixed behavior");
        ctx.expect(beginTicks == 1, "paused scene still ran begin-frame behavior");
    }

    void ActorTargetOpaqueHandleValidationRejectsStaleWrongOwnerAndWrongKind(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = fixture.scene.createActor();
        const Engine::SceneComponentHandle component = fixture.scene.attachComponent(actor, Engine::SceneComponentTypeId{7});

        Engine::SceneBehaviorDescriptor valid = behavior(
            1,
            "valid actor",
            Engine::SceneBehaviorTargetKind::Actor,
            Engine::toOpaque(actor));
        valid.phases = {Engine::SceneTickPhase::VariableUpdate};
        uint32_t validTicks = 0;
        valid.onTick = [&](Engine::SceneBehaviorContext&) {
            ++validTicks;
            return Engine::SceneBehaviorResult::success();
        };

        Engine::SceneBehaviorDescriptor wrongOwner = behavior(
            2,
            "wrong owner",
            Engine::SceneBehaviorTargetKind::Actor,
            Engine::toOpaque(actor, 99));
        wrongOwner.phases = {Engine::SceneTickPhase::VariableUpdate};
        wrongOwner.onTick = [](Engine::SceneBehaviorContext&) {
            return Engine::SceneBehaviorResult::success();
        };

        Engine::SceneBehaviorDescriptor wrongKind = behavior(
            3,
            "wrong kind",
            Engine::SceneBehaviorTargetKind::Actor,
            Engine::toOpaque(component));
        wrongKind.phases = {Engine::SceneTickPhase::VariableUpdate};
        wrongKind.onTick = [](Engine::SceneBehaviorContext&) {
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(valid));
        (void)fixture.hooks.registerBehavior(std::move(wrongOwner));
        (void)fixture.hooks.registerBehavior(std::move(wrongKind));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        ctx.expect(validTicks == 1, "valid actor behavior ran");
        ctx.expect(fixture.hooks.diagnostics().invalidTargetCount >= 2, "wrong owner and wrong kind rejected");

        (void)fixture.scene.destroyActor(actor);
        fixture.scene.tickFrame(0.016f);
        ctx.expect(fixture.hooks.diagnostics().invalidTargetCount >= 3, "pending-destroy actor rejected as stale target");
    }

    void ComponentTargetCleanupAfterDetachOrOwnerDestroy(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = fixture.scene.createActor();
        const Engine::SceneComponentHandle component = fixture.scene.attachComponent(actor, Engine::SceneComponentTypeId{7});
        uint32_t ticks = 0;
        uint32_t invalidated = 0;

        Engine::SceneBehaviorDescriptor descriptor = behavior(
            1,
            "component",
            Engine::SceneBehaviorTargetKind::Component,
            Engine::toOpaque(component));
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        descriptor.onTick = [&](Engine::SceneBehaviorContext&) {
            ++ticks;
            return Engine::SceneBehaviorResult::success();
        };
        descriptor.onTargetInvalidated = [&](Engine::SceneBehaviorContext&) {
            ++invalidated;
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(descriptor));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        (void)fixture.scene.detachComponent(component);
        fixture.scene.tickFrame(0.016f);

        ctx.expect(ticks == 1, "component behavior ticked before detach");
        ctx.expect(invalidated == 1, "component behavior reported invalidated after detach");
    }

    void HookReflectionWriteUpdatesSceneTransformAndHierarchy(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle parent = fixture.scene.createActor();
        const Engine::SceneActorHandle child = fixture.scene.createActor();
        (void)fixture.scene.attachChild(child, parent, false);

        Engine::SceneBehaviorDescriptor descriptor = behavior(
            1,
            "actor writer",
            Engine::SceneBehaviorTargetKind::Actor,
            Engine::toOpaque(child));
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        descriptor.onTick = [](Engine::SceneBehaviorContext& context) {
            const Engine::ReflectionResult result = context.set(
                Engine::SceneReflectedPropertyId::LocalTranslation,
                glm::vec3{4.0f, 5.0f, 6.0f});
            return Engine::SceneBehaviorResult{
                result.status == Engine::ReflectionStatus::Success
                    ? Engine::SceneBehaviorStatus::Success
                    : Engine::SceneBehaviorStatus::ReflectionFailed,
                result.message};
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(descriptor));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        const auto local = fixture.scene.localTransform(child);
        const auto world = fixture.scene.worldMatrix(child);
        ctx.expect(local.has_value() && near(local->translation, glm::vec3{4.0f, 5.0f, 6.0f}), "hook updated local transform through reflection");
        ctx.expect(world.has_value() && near(glm::vec3((*world)[3]), glm::vec3{4.0f, 5.0f, 6.0f}), "hierarchy world matrix updated");
    }

    void PropertyChangedNotificationRunsOnlyThroughExplicitNotifyApi(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = fixture.scene.createActor();
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        uint32_t notifications = 0;

        Engine::SceneBehaviorDescriptor descriptor = behavior(
            1,
            "property",
            Engine::SceneBehaviorTargetKind::Actor,
            target);
        descriptor.onPropertyChanged = [&](Engine::SceneBehaviorContext& context) {
            if (context.propertyChange && context.propertyChange->property == Engine::SceneReflectedPropertyId::LocalTranslation) {
                ++notifications;
            }
            return Engine::SceneBehaviorResult::success();
        };
        (void)fixture.hooks.registerBehavior(std::move(descriptor));

        (void)Engine::setReflectedProperty(
            fixture.reflectionContext,
            target,
            Engine::SceneReflectedPropertyId::LocalTranslation,
            glm::vec3{1.0f, 0.0f, 0.0f});
        ctx.expect(notifications == 0, "raw reflection write does not notify behavior hooks");

        (void)fixture.hooks.setReflectedPropertyAndNotify(
            target,
            Engine::SceneReflectedPropertyId::LocalTranslation,
            glm::vec3{2.0f, 0.0f, 0.0f});
        ctx.expect(notifications == 1, "helper reflection write notifies behavior hooks");
    }

    void DestroyActorDuringCallbackDoesNotCorruptIteration(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle victim = fixture.scene.createActor();
        uint32_t firstTicks = 0;
        uint32_t secondTicks = 0;

        Engine::SceneBehaviorDescriptor first = behavior(1);
        first.phases = {Engine::SceneTickPhase::VariableUpdate};
        first.onTick = [&](Engine::SceneBehaviorContext& context) {
            ++firstTicks;
            (void)context.scene->destroyActor(victim);
            return Engine::SceneBehaviorResult::success();
        };
        Engine::SceneBehaviorDescriptor second = behavior(2);
        second.phases = {Engine::SceneTickPhase::VariableUpdate};
        second.onTick = [&](Engine::SceneBehaviorContext&) {
            ++secondTicks;
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(first));
        (void)fixture.hooks.registerBehavior(std::move(second));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        ctx.expect(firstTicks == 1 && secondTicks == 1, "actor destruction during callback did not stop later callback");
        ctx.expect(!fixture.scene.contains(victim), "victim actor is pending destroy");
    }

    void RequestUnregisterDuringCallbackIsDeferredAndDeterministic(TestContext& ctx)
    {
        Fixture fixture;
        Engine::SceneBehaviorHandle handle;
        uint32_t ticks = 0;
        Engine::SceneBehaviorDescriptor descriptor = behavior(1);
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        descriptor.onTick = [&](Engine::SceneBehaviorContext& context) {
            ++ticks;
            context.requestUnregisterSelf();
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        handle = fixture.hooks.registerBehavior(std::move(descriptor));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        fixture.scene.tickFrame(0.016f);
        ctx.expect(ticks == 1, "requested unregister disables behavior before next tick");
        ctx.expect(fixture.hooks.contains(handle), "deferred unregister remains occupied while started");
        (void)fixture.scene.stop();
        ctx.expect(!fixture.hooks.contains(handle), "deferred unregister purges during stop cleanup");
    }

    void CallbackFailureIsIsolatedReportedAndCanDisableHook(TestContext& ctx)
    {
        Fixture fixture;
        uint32_t failingTicks = 0;
        uint32_t healthyTicks = 0;
        Engine::SceneBehaviorDescriptor failing = behavior(1);
        failing.disableOnFailure = true;
        failing.phases = {Engine::SceneTickPhase::VariableUpdate};
        failing.onTick = [&](Engine::SceneBehaviorContext&) {
            ++failingTicks;
            return Engine::SceneBehaviorResult{Engine::SceneBehaviorStatus::CallbackFailed, "expected failure"};
        };
        Engine::SceneBehaviorDescriptor healthy = behavior(2);
        healthy.phases = {Engine::SceneTickPhase::VariableUpdate};
        healthy.onTick = [&](Engine::SceneBehaviorContext&) {
            ++healthyTicks;
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(failing));
        (void)fixture.hooks.registerBehavior(std::move(healthy));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        fixture.scene.tickFrame(0.016f);

        const auto diagnostics = fixture.hooks.diagnostics();
        ctx.expect(failingTicks == 1, "failing behavior disabled after first failure");
        ctx.expect(healthyTicks == 2, "healthy behavior continues after another callback fails");
        ctx.expect(diagnostics.failedCallbackCount == 1, "failure is reported once");
        ctx.expect(diagnostics.lastStatus == Engine::SceneBehaviorStatus::Success, "later healthy callback can update last status");
    }

    void DiagnosticsAndDebugRecordsAreDeterministic(TestContext& ctx)
    {
        Fixture fixture;
        Engine::SceneBehaviorDescriptor descriptor = behavior(1);
        descriptor.phases = {Engine::SceneTickPhase::BeginFrame};
        descriptor.onLoad = [](Engine::SceneBehaviorContext&) {
            return Engine::SceneBehaviorResult::success();
        };
        descriptor.onTick = [](Engine::SceneBehaviorContext&) {
            return Engine::SceneBehaviorResult::success();
        };

        (void)fixture.hooks.registerSchedulerSystem();
        (void)fixture.hooks.registerBehavior(std::move(descriptor));
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        const auto diagnostics = fixture.hooks.diagnostics();
        const auto records = fixture.hooks.debugRecords();
        ctx.expect(diagnostics.registeredCount == 1, "registered count reported");
        ctx.expect(diagnostics.lifecycleCallbackCount == 1, "lifecycle callback count reported");
        ctx.expect(diagnostics.tickCallbackCount == 1, "tick callback count reported");
        ctx.expect(records.size() == 2, "debug records contain load and tick callbacks");
        ctx.expect(records[0].event == "load" && records[1].event == "tick", "debug records are deterministic");
        fixture.hooks.clearDebugRecords();
        ctx.expect(fixture.hooks.debugRecords().empty(), "debug records can be cleared");
    }

    void RuntimeHookAndOpaqueHandlesAreNotSerializablePayloads(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneBehaviorHandle behaviorHandle = fixture.hooks.registerBehavior(behavior(1));
        const Engine::SceneActorHandle actor = fixture.scene.createActor(Engine::SceneObjectId{42});
        const Engine::OpaqueHandle actorOpaque = Engine::toOpaque(actor);

        ctx.expect(Engine::isValid(behaviorHandle), "runtime behavior handle is valid at runtime");
        ctx.expect(Engine::isValid(actorOpaque), "opaque handle is valid at runtime");
        ctx.expect(fixture.scene.stableId(actor)->value == 42, "stable scene id is separate from runtime hook and opaque handles");
        ctx.expect(behaviorHandle.index != fixture.scene.stableId(actor)->value, "behavior handle index is not stable serialized identity");
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
        {"BehaviorHandleLifecycleInvalidatesStaleHandles", BehaviorHandleLifecycleInvalidatesStaleHandles},
        {"RegisterAndUnregisterRejectedWhileStarted", RegisterAndUnregisterRejectedWhileStarted},
        {"LifecycleCallbacksUseDeterministicOrder", LifecycleCallbacksUseDeterministicOrder},
        {"TickCallbacksUsePhasePriorityAndRegistrationOrder", TickCallbacksUsePhasePriorityAndRegistrationOrder},
        {"DisabledBehaviorsSkipCallbacksAndReportDiagnostics", DisabledBehaviorsSkipCallbacksAndReportDiagnostics},
        {"PausedSceneSkipsFixedPhaseHooksThroughScheduler", PausedSceneSkipsFixedPhaseHooksThroughScheduler},
        {"ActorTargetOpaqueHandleValidationRejectsStaleWrongOwnerAndWrongKind", ActorTargetOpaqueHandleValidationRejectsStaleWrongOwnerAndWrongKind},
        {"ComponentTargetCleanupAfterDetachOrOwnerDestroy", ComponentTargetCleanupAfterDetachOrOwnerDestroy},
        {"HookReflectionWriteUpdatesSceneTransformAndHierarchy", HookReflectionWriteUpdatesSceneTransformAndHierarchy},
        {"PropertyChangedNotificationRunsOnlyThroughExplicitNotifyApi", PropertyChangedNotificationRunsOnlyThroughExplicitNotifyApi},
        {"DestroyActorDuringCallbackDoesNotCorruptIteration", DestroyActorDuringCallbackDoesNotCorruptIteration},
        {"RequestUnregisterDuringCallbackIsDeferredAndDeterministic", RequestUnregisterDuringCallbackIsDeferredAndDeterministic},
        {"CallbackFailureIsIsolatedReportedAndCanDisableHook", CallbackFailureIsIsolatedReportedAndCanDisableHook},
        {"DiagnosticsAndDebugRecordsAreDeterministic", DiagnosticsAndDebugRecordsAreDeterministic},
        {"RuntimeHookAndOpaqueHandlesAreNotSerializablePayloads", RuntimeHookAndOpaqueHandlesAreNotSerializablePayloads},
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

    std::cout << "SceneBehaviorHooks tests passed (" << tests.size() << " cases)\n";
    return 0;
}
