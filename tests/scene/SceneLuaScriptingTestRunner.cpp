#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneBehaviorHooks.hpp"
#include "Engine/SceneLuaScripting.hpp"
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
        Engine::SceneLuaRuntime lua;

        Fixture()
            : hooks(scene, registry, reflectionContext),
              lua(scene, registry, reflectionContext, hooks)
        {
            Engine::registerSceneReflectionDescriptors(registry);
            reflectionContext.scene = &scene;
            (void)hooks.registerSchedulerSystem();
        }
    };

    Engine::LuaScriptDescriptor script(
        uint32_t id,
        std::string source,
        Engine::OpaqueHandle target = {},
        Engine::SceneBehaviorTargetKind targetKind = Engine::SceneBehaviorTargetKind::Scene)
    {
        Engine::LuaScriptDescriptor descriptor;
        descriptor.id = Engine::LuaScriptId{id};
        descriptor.debugName = "lua_test_" + std::to_string(id);
        descriptor.inlineSource = std::move(source);
        descriptor.target = target;
        descriptor.targetKind = targetKind;
        descriptor.phases = {Engine::SceneTickPhase::VariableUpdate};
        return descriptor;
    }

    Engine::SceneActorHandle makeActor(Fixture& fixture, uint64_t id = 100)
    {
        return fixture.scene.createActor(Engine::SceneObjectId{id});
    }

    void VmCreateShutdownIsIdempotent(TestContext& ctx)
    {
        Fixture fixture;
        fixture.lua.shutdown();
        fixture.lua.shutdown();
        ctx.expect(fixture.lua.diagnostics().loadedScriptCount == 0, "shutdown leaves no loaded scripts");
    }

    void InlineScriptRunsLifecycleAndTickThroughHooks(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        const Engine::LuaScriptInstanceHandle handle = fixture.lua.loadScript(script(1, R"lua(
            count = 0
            function on_load(ctx)
                count = count + 1
            end
            function on_tick(ctx)
                count = count + 1
                engine.set(ctx.target, "localTranslation", {x = count, y = 2, z = 3})
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));

        ctx.expect(Engine::isValid(handle), "inline script loads");
        ctx.expect(fixture.scene.load(), "scene loads");
        ctx.expect(fixture.scene.start(), "scene starts");
        fixture.scene.tickFrame(0.016f);

        const auto transform = fixture.scene.localTransform(actor);
        ctx.expect(transform && near(transform->translation, {2.0f, 2.0f, 3.0f}), "Lua lifecycle and tick mutate reflected transform");
        const auto state = fixture.lua.state(handle);
        ctx.expect(state && state->lifecycleCallCount == 1 && state->tickCallCount == 1, "Lua state counts lifecycle and tick callbacks");
    }

    void LuaReflectionGetSetAndNotify(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        uint32_t propertyNotifications = 0;

        Engine::SceneBehaviorDescriptor observer;
        observer.type = Engine::SceneBehaviorTypeId{900};
        observer.debugName = "native_observer";
        observer.targetKind = Engine::SceneBehaviorTargetKind::Actor;
        observer.target = target;
        observer.onPropertyChanged = [&](Engine::SceneBehaviorContext&) {
            ++propertyNotifications;
            return Engine::SceneBehaviorResult::success();
        };
        (void)fixture.hooks.registerBehavior(std::move(observer));

        (void)fixture.lua.loadScript(script(2, R"lua(
            function on_tick(ctx)
                local previous = engine.get(ctx.target, "localTranslation")
                if previous.x ~= 0 then error("unexpected initial translation") end
                engine.set_and_notify(ctx.target, "localTranslation", {x = 7, y = 8, z = 9})
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));

        ctx.expect(fixture.scene.load(), "scene loads");
        ctx.expect(fixture.scene.start(), "scene starts");
        fixture.scene.tickFrame(0.016f);

        ctx.expect(propertyNotifications == 1, "set_and_notify emits native property notification");
        const auto transform = fixture.scene.localTransform(actor);
        ctx.expect(transform && near(transform->translation, {7.0f, 8.0f, 9.0f}), "set_and_notify writes reflected property");

        (void)fixture.scene.stop();
        (void)fixture.scene.unload();

        Fixture rawFixture;
        const Engine::SceneActorHandle rawActor = makeActor(rawFixture, 101);
        const Engine::OpaqueHandle rawTarget = Engine::toOpaque(rawActor);
        uint32_t rawNotifications = 0;
        Engine::SceneBehaviorDescriptor rawObserver;
        rawObserver.type = Engine::SceneBehaviorTypeId{901};
        rawObserver.targetKind = Engine::SceneBehaviorTargetKind::Actor;
        rawObserver.target = rawTarget;
        rawObserver.onPropertyChanged = [&](Engine::SceneBehaviorContext&) {
            ++rawNotifications;
            return Engine::SceneBehaviorResult::success();
        };
        (void)rawFixture.hooks.registerBehavior(std::move(rawObserver));
        (void)rawFixture.lua.loadScript(script(3, R"lua(
            function on_tick(ctx)
                engine.set(ctx.target, "localTranslation", {x = 1, y = 2, z = 3})
            end
        )lua", rawTarget, Engine::SceneBehaviorTargetKind::Actor));
        (void)rawFixture.scene.load();
        (void)rawFixture.scene.start();
        rawFixture.scene.tickFrame(0.016f);
        ctx.expect(rawNotifications == 0, "raw engine.set does not emit property notification");
    }

    void InvalidOpaqueAndMalformedValuesFailCleanly(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        const Engine::LuaScriptInstanceHandle handle = fixture.lua.loadScript(script(4, R"lua(
            function on_tick(ctx)
                local ok = engine.is_valid(ctx.target)
                if ok ~= true then error("target should be valid") end
                local wrote, status = engine.set(ctx.target, "localTranslation", {bad = 1})
                if wrote or status ~= "TypeMismatch" then error("malformed vector was accepted") end
                local missing = engine.get(ctx.target, "missingProperty")
                if missing ~= nil then error("missing property returned a value") end
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));

        ctx.expect(Engine::isValid(handle), "script loads");
        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        ctx.expect(fixture.lua.diagnostics().typeMismatchCount == 1, "malformed reflected value increments type mismatch diagnostics");
        ctx.expect(fixture.lua.state(handle)->failedCallCount == 0, "script handled invalid inputs without callback failure");
    }

    void LuaConversionAcceptsVectorsQuaternionMatrixAndStableIds(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture, 222);
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        (void)fixture.lua.loadScript(script(5, R"lua(
            function on_tick(ctx)
                local stable = engine.get(ctx.target, "stableId")
                if stable.sceneObjectId ~= 222 then error("stable id conversion failed") end
                local matrix = engine.get(ctx.target, "worldMatrix")
                if matrix[1] ~= 1 or matrix[6] ~= 1 or matrix[11] ~= 1 or matrix[16] ~= 1 then
                    error("matrix conversion failed")
                end
                engine.set(ctx.target, "localRotation", {w = 1, x = 0, y = 0, z = 0})
                engine.set(ctx.target, "localScale", {1, 2, 3})
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));

        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        const auto transform = fixture.scene.localTransform(actor);
        ctx.expect(transform && near(transform->scale, {1.0f, 2.0f, 3.0f}), "Lua array vector writes reflected scale");
    }

    void ScriptErrorIsIsolated(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        (void)fixture.lua.loadScript(script(6, R"lua(
            function on_tick(ctx)
                error("intentional")
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));
        (void)fixture.lua.loadScript(script(7, R"lua(
            function on_tick(ctx)
                engine.set(ctx.target, "localTranslation", {x = 4, y = 5, z = 6})
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));

        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);

        const auto transform = fixture.scene.localTransform(actor);
        ctx.expect(transform && near(transform->translation, {4.0f, 5.0f, 6.0f}), "unrelated Lua hook still runs after another script fails");
        ctx.expect(fixture.lua.diagnostics().failedCallCount == 1, "Lua runtime reports isolated callback failure");
    }

    void InstructionBudgetFailurePolicyDisablesScript(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        Engine::LuaScriptDescriptor descriptor = script(8, R"lua(
            function on_tick(ctx)
                while true do end
            end
        )lua", Engine::toOpaque(actor), Engine::SceneBehaviorTargetKind::Actor);
        descriptor.instructionBudget = 100;
        descriptor.failurePolicy = Engine::LuaScriptFailurePolicy::DisableScript;
        const Engine::LuaScriptInstanceHandle handle = fixture.lua.loadScript(std::move(descriptor));

        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        fixture.scene.tickFrame(0.016f);

        const auto state = fixture.lua.state(handle);
        ctx.expect(state && !state->enabled, "budget failure disables script when requested");
        ctx.expect(state && state->budgetExceededCount == 1, "budget exhaustion counted once");
    }

    void ReloadSuccessAndFailurePreservesPreviousScript(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "manual_engine_scene_lua_reload_test.lua";
        {
            std::ofstream output(path);
            output << "function on_tick(ctx) engine.set(ctx.target, 'localTranslation', {x = 1, y = 0, z = 0}) end";
        }

        Engine::LuaScriptDescriptor descriptor = script(9, {}, Engine::toOpaque(actor), Engine::SceneBehaviorTargetKind::Actor);
        descriptor.sourceKind = Engine::LuaScriptSourceKind::File;
        descriptor.sourcePath = path;
        const Engine::LuaScriptInstanceHandle handle = fixture.lua.loadScript(std::move(descriptor));
        ctx.expect(Engine::isValid(handle), "file script loads");

        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        ctx.expect(near(fixture.scene.localTransform(actor)->translation, {1.0f, 0.0f, 0.0f}), "initial file script ran");

        {
            std::ofstream output(path);
            output << "function on_tick(ctx) engine.set(ctx.target, 'localTranslation', {x = 2, y = 0, z = 0}) end";
        }
        ctx.expect(fixture.lua.reloadScript(handle) == Engine::LuaScriptStatus::Success, "successful reload swaps script environment");
        fixture.scene.tickFrame(0.016f);
        ctx.expect(near(fixture.scene.localTransform(actor)->translation, {2.0f, 0.0f, 0.0f}), "reloaded script ran");

        {
            std::ofstream output(path);
            output << "function on_tick(ctx) this is invalid lua";
        }
        ctx.expect(fixture.lua.reloadScript(handle) == Engine::LuaScriptStatus::CompileError, "failed reload reports compile error");
        fixture.scene.tickFrame(0.016f);
        ctx.expect(near(fixture.scene.localTransform(actor)->translation, {2.0f, 0.0f, 0.0f}), "failed reload preserves previous working script");
    }

    void SandboxBlocksUnsafeLibraries(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture);
        const Engine::OpaqueHandle target = Engine::toOpaque(actor);
        (void)fixture.lua.loadScript(script(10, R"lua(
            function on_tick(ctx)
                if os ~= nil or io ~= nil or debug ~= nil or package ~= nil or dofile ~= nil or loadfile ~= nil or require ~= nil then
                    error("unsafe library exposed")
                end
                engine.set(ctx.target, "localTranslation", {x = 9, y = 0, z = 0})
            end
        )lua", target, Engine::SceneBehaviorTargetKind::Actor));

        (void)fixture.scene.load();
        (void)fixture.scene.start();
        fixture.scene.tickFrame(0.016f);
        ctx.expect(fixture.lua.diagnostics().failedCallCount == 0, "sandboxed script did not fail");
        ctx.expect(near(fixture.scene.localTransform(actor)->translation, {9.0f, 0.0f, 0.0f}), "sandboxed script ran");
    }

    void RuntimeLuaHandlesAreNotSerializedIdentity(TestContext& ctx)
    {
        Fixture fixture;
        const Engine::SceneActorHandle actor = makeActor(fixture, 12345);
        const Engine::LuaScriptInstanceHandle scriptHandle = fixture.lua.loadScript(script(11, "function on_tick(ctx) end", Engine::toOpaque(actor), Engine::SceneBehaviorTargetKind::Actor));
        const Engine::OpaqueHandle opaque = Engine::toOpaque(actor);

        ctx.expect(Engine::isValid(scriptHandle), "Lua script handle is valid runtime token");
        ctx.expect(Engine::isValid(opaque), "opaque handle is valid runtime token");
        ctx.expect(fixture.scene.stableId(actor)->value == 12345, "stable scene id is durable identity");
        ctx.expect(scriptHandle.index != fixture.scene.stableId(actor)->value, "Lua script handle is not serialized actor identity");
        ctx.expect(opaque.index == actor.index && opaque.generation == actor.generation, "opaque handle mirrors runtime handle only");
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
        {"VmCreateShutdownIsIdempotent", VmCreateShutdownIsIdempotent},
        {"InlineScriptRunsLifecycleAndTickThroughHooks", InlineScriptRunsLifecycleAndTickThroughHooks},
        {"LuaReflectionGetSetAndNotify", LuaReflectionGetSetAndNotify},
        {"InvalidOpaqueAndMalformedValuesFailCleanly", InvalidOpaqueAndMalformedValuesFailCleanly},
        {"LuaConversionAcceptsVectorsQuaternionMatrixAndStableIds", LuaConversionAcceptsVectorsQuaternionMatrixAndStableIds},
        {"ScriptErrorIsIsolated", ScriptErrorIsIsolated},
        {"InstructionBudgetFailurePolicyDisablesScript", InstructionBudgetFailurePolicyDisablesScript},
        {"ReloadSuccessAndFailurePreservesPreviousScript", ReloadSuccessAndFailurePreservesPreviousScript},
        {"SandboxBlocksUnsafeLibraries", SandboxBlocksUnsafeLibraries},
        {"RuntimeLuaHandlesAreNotSerializedIdentity", RuntimeLuaHandlesAreNotSerializedIdentity},
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

    std::cout << "SceneLuaScripting tests passed (" << tests.size() << " cases)\n";
    return 0;
}
