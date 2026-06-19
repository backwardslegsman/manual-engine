#include "Engine/SceneLuaScripting.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {
    namespace {
        constexpr int LuaNoRef = LUA_NOREF;
        constexpr const char* OpaqueMetatableName = "ManualEngine.OpaqueHandle";

        [[nodiscard]] std::string statusString(LuaScriptStatus status)
        {
            switch (status) {
                case LuaScriptStatus::Success:
                    return "Success";
                case LuaScriptStatus::InvalidInput:
                    return "InvalidInput";
                case LuaScriptStatus::InvalidScript:
                    return "InvalidScript";
                case LuaScriptStatus::CompileError:
                    return "CompileError";
                case LuaScriptStatus::RuntimeError:
                    return "RuntimeError";
                case LuaScriptStatus::BudgetExceeded:
                    return "BudgetExceeded";
                case LuaScriptStatus::TypeMismatch:
                    return "TypeMismatch";
                case LuaScriptStatus::ReflectionFailed:
                    return "ReflectionFailed";
                case LuaScriptStatus::IoError:
                    return "IoError";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string opaqueKindName(OpaqueHandleKind kind)
        {
            switch (kind) {
                case OpaqueHandleKind::None:
                    return "None";
                case OpaqueHandleKind::SceneActor:
                    return "SceneActor";
                case OpaqueHandleKind::SceneComponent:
                    return "SceneComponent";
                case OpaqueHandleKind::SceneSystem:
                    return "SceneSystem";
                case OpaqueHandleKind::SceneMeshComponent:
                    return "SceneMeshComponent";
                case OpaqueHandleKind::SceneSkinnedMeshComponent:
                    return "SceneSkinnedMeshComponent";
                case OpaqueHandleKind::SceneLightComponent:
                    return "SceneLightComponent";
                case OpaqueHandleKind::SceneCameraComponent:
                    return "SceneCameraComponent";
                case OpaqueHandleKind::ScenePhysicsBody:
                    return "ScenePhysicsBody";
                case OpaqueHandleKind::ScenePhysicsCollider:
                    return "ScenePhysicsCollider";
                case OpaqueHandleKind::SceneCharacter:
                    return "SceneCharacter";
                case OpaqueHandleKind::Asset:
                    return "Asset";
                case OpaqueHandleKind::TerrainSource:
                    return "TerrainSource";
                case OpaqueHandleKind::TerrainChunk:
                    return "TerrainChunk";
                case OpaqueHandleKind::TerrainStableChunk:
                    return "TerrainStableChunk";
            }
            return "Unknown";
        }

        [[nodiscard]] std::optional<uint64_t> tableUIntField(lua_State* state, int index, const char* field)
        {
            lua_getfield(state, index, field);
            if (!lua_isinteger(state, -1)) {
                lua_pop(state, 1);
                return std::nullopt;
            }
            const uint64_t value = static_cast<uint64_t>(lua_tointeger(state, -1));
            lua_pop(state, 1);
            return value;
        }

        [[nodiscard]] bool numericField(lua_State* state, int index, const char* field, float& value)
        {
            lua_getfield(state, index, field);
            if (lua_isnumber(state, -1)) {
                value = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                return true;
            }
            lua_pop(state, 1);
            return false;
        }

        [[nodiscard]] bool numericIndex(lua_State* state, int index, int element, float& value)
        {
            lua_geti(state, index, element);
            if (lua_isnumber(state, -1)) {
                value = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                return true;
            }
            lua_pop(state, 1);
            return false;
        }
    }

    SceneLuaRuntime::SceneLuaRuntime(
        Scene& scene,
        ReflectionRegistry& reflection,
        SceneReflectionContext& reflectionContext,
        SceneBehaviorHooks& hooks)
        : scene_(scene)
        , reflection_(reflection)
        , reflectionContext_(reflectionContext)
        , hooks_(hooks)
    {
        if (!reflectionContext_.scene) {
            reflectionContext_.scene = &scene_;
        }
        setupLuaState();
    }

    SceneLuaRuntime::~SceneLuaRuntime()
    {
        shutdown();
    }

    LuaScriptInstanceHandle SceneLuaRuntime::loadScript(LuaScriptDescriptor descriptor)
    {
        if (!isValid(descriptor.id)) {
            noteStatus(nullptr, LuaScriptStatus::InvalidInput, "script id is invalid");
            return {};
        }

        LuaScriptStatus status = LuaScriptStatus::Success;
        std::string message;
        const int environmentRef = compileEnvironment(descriptor, status, message);
        if (environmentRef == LuaNoRef) {
            noteStatus(nullptr, status, message);
            return {};
        }

        LuaScriptInstanceHandle handle;
        for (uint32_t index = 0; index < scripts_.size(); ++index) {
            if (!scripts_[index].occupied) {
                handle = {index, scripts_[index].generation};
                break;
            }
        }
        if (!isValid(handle)) {
            ScriptRecord record;
            record.generation = nextGeneration(0);
            scripts_.push_back(std::move(record));
            handle = {static_cast<uint32_t>(scripts_.size() - 1), scripts_.back().generation};
        }

        ScriptRecord& script = scripts_[handle.index];
        script.occupied = true;
        script.descriptor = std::move(descriptor);
        script.environmentRef = environmentRef;
        script.state.state = LuaScriptState::Loaded;
        script.state.enabled = script.descriptor.enabled;
        script.state.target = script.descriptor.target;
        script.state.lastStatus = LuaScriptStatus::Success;
        script.state.lastMessage.clear();

        SceneBehaviorDescriptor behavior = behaviorDescriptorFor(handle, script);
        script.state.behavior = hooks_.registerBehavior(std::move(behavior));
        if (!isValid(script.state.behavior)) {
            luaL_unref(state_, LUA_REGISTRYINDEX, script.environmentRef);
            freeSlot(handle.index);
            noteStatus(nullptr, LuaScriptStatus::InvalidScript, "failed to register Lua behavior hook");
            return {};
        }

        refreshDiagnostics();
        debugRecords_.push_back({handle, script.descriptor.id, "load", LuaScriptStatus::Success});
        return handle;
    }

    bool SceneLuaRuntime::unloadScript(LuaScriptInstanceHandle scriptHandle)
    {
        ScriptRecord* script = record(scriptHandle);
        if (!script) {
            ++diagnostics_.invalidHandleCount;
            noteStatus(nullptr, LuaScriptStatus::InvalidScript, "script handle is invalid");
            return false;
        }
        if (isValid(script->state.behavior)) {
            if (!hooks_.unregisterBehavior(script->state.behavior)) {
                hooks_.requestUnregisterBehavior(script->state.behavior);
            }
        }
        luaL_unref(state_, LUA_REGISTRYINDEX, script->environmentRef);
        debugRecords_.push_back({scriptHandle, script->descriptor.id, "unload", LuaScriptStatus::Success});
        freeSlot(scriptHandle.index);
        refreshDiagnostics();
        return true;
    }

    LuaScriptStatus SceneLuaRuntime::reloadScript(LuaScriptInstanceHandle scriptHandle)
    {
        ScriptRecord* script = record(scriptHandle);
        if (!script) {
            ++diagnostics_.invalidHandleCount;
            noteStatus(nullptr, LuaScriptStatus::InvalidScript, "script handle is invalid");
            return LuaScriptStatus::InvalidScript;
        }

        ++diagnostics_.reloadCount;
        LuaScriptStatus status = LuaScriptStatus::Success;
        std::string message;
        const int newEnvironmentRef = compileEnvironment(script->descriptor, status, message);
        if (newEnvironmentRef == LuaNoRef) {
            ++diagnostics_.reloadFailureCount;
            script->state.lastStatus = status;
            script->state.lastMessage = message;
            if (script->descriptor.failurePolicy == LuaScriptFailurePolicy::DisableScript) {
                setScriptEnabled(scriptHandle, false);
            }
            noteStatus(script, status, message);
            debugRecords_.push_back({scriptHandle, script->descriptor.id, "reload", status, message});
            return status;
        }

        luaL_unref(state_, LUA_REGISTRYINDEX, script->environmentRef);
        script->environmentRef = newEnvironmentRef;
        script->state.state = LuaScriptState::Loaded;
        script->state.lastStatus = LuaScriptStatus::Success;
        script->state.lastMessage.clear();
        refreshDiagnostics();
        debugRecords_.push_back({scriptHandle, script->descriptor.id, "reload", LuaScriptStatus::Success});
        return LuaScriptStatus::Success;
    }

    bool SceneLuaRuntime::setScriptEnabled(LuaScriptInstanceHandle scriptHandle, bool enabled)
    {
        ScriptRecord* script = record(scriptHandle);
        if (!script) {
            ++diagnostics_.invalidHandleCount;
            noteStatus(nullptr, LuaScriptStatus::InvalidScript, "script handle is invalid");
            return false;
        }
        script->descriptor.enabled = enabled;
        script->state.enabled = enabled;
        if (isValid(script->state.behavior)) {
            hooks_.setBehaviorEnabled(script->state.behavior, enabled);
        }
        refreshDiagnostics();
        return true;
    }

    bool SceneLuaRuntime::contains(LuaScriptInstanceHandle script) const
    {
        return record(script) != nullptr;
    }

    std::optional<LuaScriptDescriptor> SceneLuaRuntime::descriptor(LuaScriptInstanceHandle scriptHandle) const
    {
        const ScriptRecord* script = record(scriptHandle);
        if (!script) {
            return std::nullopt;
        }
        return script->descriptor;
    }

    std::optional<LuaScriptInstanceState> SceneLuaRuntime::state(LuaScriptInstanceHandle scriptHandle) const
    {
        const ScriptRecord* script = record(scriptHandle);
        if (!script) {
            return std::nullopt;
        }
        return script->state;
    }

    SceneLuaDiagnostics SceneLuaRuntime::diagnostics() const
    {
        return diagnostics_;
    }

    std::vector<LuaScriptDebugRecord> SceneLuaRuntime::debugRecords() const
    {
        return debugRecords_;
    }

    void SceneLuaRuntime::clearDebugRecords()
    {
        debugRecords_.clear();
    }

    void SceneLuaRuntime::shutdown()
    {
        if (!state_) {
            return;
        }
        for (uint32_t index = 0; index < scripts_.size(); ++index) {
            if (scripts_[index].occupied) {
                if (isValid(scripts_[index].state.behavior)) {
                    if (!hooks_.unregisterBehavior(scripts_[index].state.behavior)) {
                        hooks_.requestUnregisterBehavior(scripts_[index].state.behavior);
                    }
                }
                luaL_unref(state_, LUA_REGISTRYINDEX, scripts_[index].environmentRef);
                scripts_[index] = {};
            }
        }
        lua_close(state_);
        state_ = nullptr;
        scripts_.clear();
        currentScript_ = {};
        currentContext_ = nullptr;
        refreshDiagnostics();
    }

    SceneLuaRuntime::ScriptRecord* SceneLuaRuntime::record(LuaScriptInstanceHandle script)
    {
        if (!isValid(script) || script.index >= scripts_.size()) {
            return nullptr;
        }
        ScriptRecord& current = scripts_[script.index];
        if (!current.occupied || current.generation != script.generation) {
            return nullptr;
        }
        return &current;
    }

    const SceneLuaRuntime::ScriptRecord* SceneLuaRuntime::record(LuaScriptInstanceHandle script) const
    {
        if (!isValid(script) || script.index >= scripts_.size()) {
            return nullptr;
        }
        const ScriptRecord& current = scripts_[script.index];
        if (!current.occupied || current.generation != script.generation) {
            return nullptr;
        }
        return &current;
    }

    uint32_t SceneLuaRuntime::nextGeneration(uint32_t generation) const
    {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    void SceneLuaRuntime::freeSlot(uint32_t index)
    {
        if (index >= scripts_.size()) {
            return;
        }
        const uint32_t next = nextGeneration(scripts_[index].generation);
        scripts_[index] = {};
        scripts_[index].generation = next;
    }

    void SceneLuaRuntime::refreshDiagnostics()
    {
        diagnostics_.loadedScriptCount = 0;
        diagnostics_.enabledScriptCount = 0;
        diagnostics_.errorScriptCount = 0;
        for (const ScriptRecord& script : scripts_) {
            if (!script.occupied) {
                continue;
            }
            if (script.state.state == LuaScriptState::Loaded) {
                ++diagnostics_.loadedScriptCount;
            }
            if (script.state.enabled) {
                ++diagnostics_.enabledScriptCount;
            }
            if (script.state.state == LuaScriptState::Error) {
                ++diagnostics_.errorScriptCount;
            }
        }
    }

    void SceneLuaRuntime::noteStatus(ScriptRecord* script, LuaScriptStatus status, std::string message)
    {
        diagnostics_.lastStatus = status;
        diagnostics_.lastMessage = message;
        diagnostics_.lastScriptName = script ? script->descriptor.debugName : std::string{};
        if (script) {
            script->state.lastStatus = status;
            script->state.lastMessage = std::move(message);
            if (status != LuaScriptStatus::Success) {
                script->state.state = LuaScriptState::Error;
            }
        }
        if (status != LuaScriptStatus::Success && !diagnostics_.lastMessage.empty()) {
            diagnostics_.stackTraces.push_back(diagnostics_.lastMessage);
        }
        refreshDiagnostics();
    }

    std::string SceneLuaRuntime::scriptSource(
        const LuaScriptDescriptor& descriptor,
        LuaScriptStatus& status,
        std::string& message) const
    {
        if (descriptor.sourceKind == LuaScriptSourceKind::Inline) {
            status = LuaScriptStatus::Success;
            return descriptor.inlineSource;
        }

        std::ifstream input(descriptor.sourcePath, std::ios::binary);
        if (!input) {
            status = LuaScriptStatus::IoError;
            message = "failed to open Lua script file";
            return {};
        }
        std::ostringstream stream;
        stream << input.rdbuf();
        status = LuaScriptStatus::Success;
        return stream.str();
    }

    int SceneLuaRuntime::compileEnvironment(
        const LuaScriptDescriptor& descriptor,
        LuaScriptStatus& status,
        std::string& message)
    {
        status = LuaScriptStatus::Success;
        const std::string source = scriptSource(descriptor, status, message);
        if (status != LuaScriptStatus::Success) {
            return LuaNoRef;
        }
        if (source.empty()) {
            status = LuaScriptStatus::InvalidInput;
            message = "Lua script source is empty";
            return LuaNoRef;
        }

        const std::string chunkName = descriptor.debugName.empty() ? "scene_lua_script" : descriptor.debugName;
        if (luaL_loadbufferx(state_, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK) {
            status = LuaScriptStatus::CompileError;
            message = lua_tostring(state_, -1) ? lua_tostring(state_, -1) : "Lua compile error";
            lua_pop(state_, 1);
            return LuaNoRef;
        }

        const int functionIndex = lua_gettop(state_);
        lua_newtable(state_);
        const int envIndex = lua_gettop(state_);
        pushEngineTable();
        lua_setfield(state_, envIndex, "engine");
        lua_newtable(state_);
        lua_pushglobaltable(state_);
        lua_setfield(state_, -2, "__index");
        lua_setmetatable(state_, envIndex);
        lua_pushvalue(state_, envIndex);
        lua_setupvalue(state_, functionIndex, 1);

        lua_pushvalue(state_, envIndex);
        int environmentRef = luaL_ref(state_, LUA_REGISTRYINDEX);
        lua_remove(state_, envIndex);

        if (lua_pcall(state_, 0, 1, 0) != LUA_OK) {
            status = LuaScriptStatus::RuntimeError;
            message = lua_tostring(state_, -1) ? lua_tostring(state_, -1) : "Lua script initialization error";
            lua_pop(state_, 1);
            luaL_unref(state_, LUA_REGISTRYINDEX, environmentRef);
            return LuaNoRef;
        }

        if (lua_istable(state_, -1)) {
            luaL_unref(state_, LUA_REGISTRYINDEX, environmentRef);
            environmentRef = luaL_ref(state_, LUA_REGISTRYINDEX);
        } else {
            lua_pop(state_, 1);
        }
        return environmentRef;
    }

    SceneBehaviorDescriptor SceneLuaRuntime::behaviorDescriptorFor(LuaScriptInstanceHandle handle, ScriptRecord& script)
    {
        SceneBehaviorDescriptor descriptor;
        descriptor.type = SceneBehaviorTypeId{script.descriptor.id.value};
        descriptor.debugName = script.descriptor.debugName;
        descriptor.targetKind = script.descriptor.targetKind;
        descriptor.target = script.descriptor.target;
        descriptor.phases = script.descriptor.phases;
        descriptor.priority = script.descriptor.priority;
        descriptor.enabled = script.descriptor.enabled;
        descriptor.disableOnFailure = script.descriptor.failurePolicy == LuaScriptFailurePolicy::DisableScript;
        descriptor.onLoad = [this, handle](SceneBehaviorContext& context) {
            ScriptRecord* script = record(handle);
            return script ? invokeLifecycle(handle, context, script->descriptor.onLoad, "load") :
                SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
        };
        descriptor.onStart = [this, handle](SceneBehaviorContext& context) {
            ScriptRecord* script = record(handle);
            return script ? invokeLifecycle(handle, context, script->descriptor.onStart, "start") :
                SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
        };
        descriptor.onStop = [this, handle](SceneBehaviorContext& context) {
            ScriptRecord* script = record(handle);
            return script ? invokeLifecycle(handle, context, script->descriptor.onStop, "stop") :
                SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
        };
        descriptor.onUnload = [this, handle](SceneBehaviorContext& context) {
            ScriptRecord* script = record(handle);
            return script ? invokeLifecycle(handle, context, script->descriptor.onUnload, "unload") :
                SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
        };
        descriptor.onTargetInvalidated = [this, handle](SceneBehaviorContext& context) {
            ScriptRecord* script = record(handle);
            return script ? invokeLifecycle(handle, context, script->descriptor.onTargetInvalidated, "target_invalidated") :
                SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
        };
        descriptor.onTick = [this, handle](SceneBehaviorContext& context) {
            return invokeTick(handle, context);
        };
        descriptor.onPropertyChanged = [this, handle](SceneBehaviorContext& context) {
            return invokePropertyChanged(handle, context);
        };
        return descriptor;
    }

    SceneBehaviorResult SceneLuaRuntime::invokeLifecycle(
        LuaScriptInstanceHandle handle,
        SceneBehaviorContext& context,
        const std::string& functionName,
        const std::string& eventName)
    {
        return invokeFunction(handle, context, functionName, eventName);
    }

    SceneBehaviorResult SceneLuaRuntime::invokeTick(LuaScriptInstanceHandle handle, SceneBehaviorContext& context)
    {
        ScriptRecord* script = record(handle);
        return script ? invokeFunction(handle, context, script->descriptor.onTick, "tick") :
            SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
    }

    SceneBehaviorResult SceneLuaRuntime::invokePropertyChanged(LuaScriptInstanceHandle handle, SceneBehaviorContext& context)
    {
        ScriptRecord* script = record(handle);
        return script ? invokeFunction(handle, context, script->descriptor.onPropertyChanged, "property_changed") :
            SceneBehaviorResult{SceneBehaviorStatus::InvalidBehavior, "Lua script handle is invalid"};
    }

    SceneBehaviorResult SceneLuaRuntime::invokeFunction(
        LuaScriptInstanceHandle handle,
        SceneBehaviorContext& context,
        const std::string& functionName,
        const std::string& eventName)
    {
        ScriptRecord* script = record(handle);
        if (!script || functionName.empty() || script->invoking) {
            return SceneBehaviorResult::success();
        }

        lua_rawgeti(state_, LUA_REGISTRYINDEX, script->environmentRef);
        lua_getfield(state_, -1, functionName.c_str());
        if (!lua_isfunction(state_, -1)) {
            lua_pop(state_, 2);
            return SceneBehaviorResult::success();
        }

        const auto started = std::chrono::steady_clock::now();
        currentScript_ = handle;
        currentContext_ = &context;
        script->invoking = true;
        remainingInstructions_ = script->descriptor.instructionBudget;
        if (script->descriptor.instructionBudget > 0) {
            lua_sethook(state_, luaInstructionHook, LUA_MASKCOUNT, 100);
        }
        pushContextTable(context);

        LuaScriptStatus status = LuaScriptStatus::Success;
        std::string message;
        if (lua_pcall(state_, 1, 0, 0) != LUA_OK) {
            message = lua_tostring(state_, -1) ? lua_tostring(state_, -1) : "Lua runtime error";
            status = message.find("instruction budget exceeded") != std::string::npos ?
                LuaScriptStatus::BudgetExceeded :
                LuaScriptStatus::RuntimeError;
            lua_pop(state_, 1);
        }
        lua_sethook(state_, nullptr, 0, 0);
        lua_pop(state_, 1);
        script->invoking = false;
        currentScript_ = {};
        currentContext_ = nullptr;

        const auto finished = std::chrono::steady_clock::now();
        const uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
        script->state.elapsedMicroseconds += elapsed;
        diagnostics_.elapsedMicroseconds += elapsed;
        if (eventName == "tick") {
            ++script->state.tickCallCount;
            ++diagnostics_.tickCallCount;
        } else if (eventName == "property_changed") {
            ++script->state.propertyCallCount;
            ++diagnostics_.propertyCallCount;
        } else {
            ++script->state.lifecycleCallCount;
            ++diagnostics_.lifecycleCallCount;
        }

        if (status != LuaScriptStatus::Success) {
            ++script->state.failedCallCount;
            ++diagnostics_.failedCallCount;
            if (status == LuaScriptStatus::BudgetExceeded) {
                ++script->state.budgetExceededCount;
                ++diagnostics_.budgetExceededCount;
            }
            if (script->descriptor.failurePolicy == LuaScriptFailurePolicy::DisableScript) {
                setScriptEnabled(handle, false);
            } else if (script->descriptor.failurePolicy == LuaScriptFailurePolicy::RequestUnregister) {
                hooks_.requestUnregisterBehavior(script->state.behavior);
                script->state.enabled = false;
            }
            noteStatus(script, status, message);
        } else {
            script->state.lastStatus = LuaScriptStatus::Success;
            script->state.lastMessage.clear();
        }

        debugRecords_.push_back({handle, script->descriptor.id, eventName, status, message, elapsed});
        if (status != LuaScriptStatus::Success) {
            return {SceneBehaviorStatus::CallbackFailed, message};
        }
        return SceneBehaviorResult::success();
    }

    void SceneLuaRuntime::setupLuaState()
    {
        state_ = luaL_newstate();
        *static_cast<SceneLuaRuntime**>(lua_getextraspace(state_)) = this;

        luaL_requiref(state_, "_G", luaopen_base, 1);
        lua_pop(state_, 1);
        luaL_requiref(state_, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(state_, 1);
        luaL_requiref(state_, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(state_, 1);
        luaL_requiref(state_, LUA_MATHLIBNAME, luaopen_math, 1);
        lua_pop(state_, 1);

        const char* blocked[] = {"os", "io", "debug", "package", "dofile", "loadfile", "require"};
        for (const char* name : blocked) {
            lua_pushnil(state_);
            lua_setglobal(state_, name);
        }

        luaL_newmetatable(state_, OpaqueMetatableName);
        lua_pushcfunction(state_, luaOpaqueEquals);
        lua_setfield(state_, -2, "__eq");
        lua_pop(state_, 1);
    }

    void SceneLuaRuntime::pushEngineTable()
    {
        lua_newtable(state_);
        const int table = lua_gettop(state_);
        auto bind = [&](const char* name, lua_CFunction function) {
            lua_pushlightuserdata(state_, this);
            lua_pushcclosure(state_, function, 1);
            lua_setfield(state_, table, name);
        };
        bind("get", luaEngineGet);
        bind("set", luaEngineSet);
        bind("set_and_notify", luaEngineSetAndNotify);
        bind("is_valid", luaEngineIsValid);
        bind("kind", luaEngineKind);
        bind("target", luaEngineTarget);
        bind("disable_self", luaEngineDisableSelf);
        bind("unregister_self", luaEngineUnregisterSelf);
    }

    void SceneLuaRuntime::pushContextTable(const SceneBehaviorContext& context)
    {
        lua_newtable(state_);
        const int table = lua_gettop(state_);
        pushOpaque(context.target);
        lua_setfield(state_, table, "target");
        lua_pushinteger(state_, currentScript_.index);
        lua_setfield(state_, table, "scriptIndex");
        if (context.tick) {
            lua_pushnumber(state_, context.tick->deltaSeconds);
            lua_setfield(state_, table, "deltaSeconds");
            lua_pushinteger(state_, static_cast<lua_Integer>(context.tick->frameIndex));
            lua_setfield(state_, table, "frameIndex");
            lua_pushinteger(state_, context.tick->fixedStepIndex);
            lua_setfield(state_, table, "fixedStepIndex");
            lua_pushboolean(state_, context.tick->paused);
            lua_setfield(state_, table, "paused");
            lua_pushinteger(state_, static_cast<int>(context.tick->phase));
            lua_setfield(state_, table, "phase");
        }
        if (context.propertyChange) {
            lua_pushinteger(state_, static_cast<int>(context.propertyChange->property));
            lua_setfield(state_, table, "property");
            pushReflectedValue(context.propertyChange->oldValue);
            lua_setfield(state_, table, "oldValue");
            pushReflectedValue(context.propertyChange->newValue);
            lua_setfield(state_, table, "newValue");
        }
    }

    void SceneLuaRuntime::pushOpaque(OpaqueHandle handle)
    {
        void* memory = lua_newuserdatauv(state_, sizeof(OpaqueHandle), 0);
        *static_cast<OpaqueHandle*>(memory) = handle;
        luaL_getmetatable(state_, OpaqueMetatableName);
        lua_setmetatable(state_, -2);
    }

    std::optional<OpaqueHandle> SceneLuaRuntime::readOpaque(int index)
    {
        void* memory = luaL_testudata(state_, index, OpaqueMetatableName);
        if (!memory) {
            return std::nullopt;
        }
        return *static_cast<OpaqueHandle*>(memory);
    }

    std::optional<SceneReflectedPropertyId> SceneLuaRuntime::readProperty(OpaqueHandle target, int index)
    {
        if (lua_isinteger(state_, index)) {
            return static_cast<SceneReflectedPropertyId>(static_cast<uint32_t>(lua_tointeger(state_, index)));
        }
        if (!lua_isstring(state_, index)) {
            return std::nullopt;
        }
        const std::string name = lua_tostring(state_, index);
        const auto objectId = reflectedObjectId(target.kind);
        if (!objectId) {
            return std::nullopt;
        }
        const ReflectedPropertyDescriptor* property = reflection_.property(*objectId, name);
        if (!property) {
            return std::nullopt;
        }
        return static_cast<SceneReflectedPropertyId>(property->id);
    }

    std::optional<uint32_t> SceneLuaRuntime::reflectedObjectId(OpaqueHandleKind kind) const
    {
        switch (kind) {
            case OpaqueHandleKind::SceneActor:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneActor);
            case OpaqueHandleKind::SceneComponent:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneComponent);
            case OpaqueHandleKind::SceneMeshComponent:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneMeshComponent);
            case OpaqueHandleKind::SceneSkinnedMeshComponent:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneSkinnedMeshComponent);
            case OpaqueHandleKind::SceneLightComponent:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneLightComponent);
            case OpaqueHandleKind::SceneCameraComponent:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneCameraComponent);
            case OpaqueHandleKind::ScenePhysicsBody:
                return static_cast<uint32_t>(SceneReflectedObjectId::ScenePhysicsBody);
            case OpaqueHandleKind::ScenePhysicsCollider:
                return static_cast<uint32_t>(SceneReflectedObjectId::ScenePhysicsCollider);
            case OpaqueHandleKind::SceneCharacter:
                return static_cast<uint32_t>(SceneReflectedObjectId::SceneCharacter);
            case OpaqueHandleKind::Asset:
                return static_cast<uint32_t>(SceneReflectedObjectId::Asset);
            case OpaqueHandleKind::TerrainSource:
                return static_cast<uint32_t>(SceneReflectedObjectId::TerrainSource);
            case OpaqueHandleKind::TerrainChunk:
                return static_cast<uint32_t>(SceneReflectedObjectId::TerrainChunk);
            default:
                return std::nullopt;
        }
    }

    void SceneLuaRuntime::pushReflectedValue(const ReflectedValue& value)
    {
        switch (reflectedValueType(value)) {
            case ReflectedValueType::Bool:
                lua_pushboolean(state_, std::get<bool>(value));
                break;
            case ReflectedValueType::Int64:
                lua_pushinteger(state_, static_cast<lua_Integer>(std::get<int64_t>(value)));
                break;
            case ReflectedValueType::UInt64:
                lua_pushinteger(state_, static_cast<lua_Integer>(std::get<uint64_t>(value)));
                break;
            case ReflectedValueType::Float:
                lua_pushnumber(state_, std::get<float>(value));
                break;
            case ReflectedValueType::String:
                lua_pushlstring(state_, std::get<std::string>(value).data(), std::get<std::string>(value).size());
                break;
            case ReflectedValueType::Vec2: {
                const glm::vec2 vector = std::get<glm::vec2>(value);
                lua_newtable(state_);
                lua_pushnumber(state_, vector.x);
                lua_setfield(state_, -2, "x");
                lua_pushnumber(state_, vector.y);
                lua_setfield(state_, -2, "y");
                break;
            }
            case ReflectedValueType::Vec3: {
                const glm::vec3 vector = std::get<glm::vec3>(value);
                lua_newtable(state_);
                lua_pushnumber(state_, vector.x);
                lua_setfield(state_, -2, "x");
                lua_pushnumber(state_, vector.y);
                lua_setfield(state_, -2, "y");
                lua_pushnumber(state_, vector.z);
                lua_setfield(state_, -2, "z");
                break;
            }
            case ReflectedValueType::Quat: {
                const glm::quat quat = std::get<glm::quat>(value);
                lua_newtable(state_);
                lua_pushnumber(state_, quat.w);
                lua_setfield(state_, -2, "w");
                lua_pushnumber(state_, quat.x);
                lua_setfield(state_, -2, "x");
                lua_pushnumber(state_, quat.y);
                lua_setfield(state_, -2, "y");
                lua_pushnumber(state_, quat.z);
                lua_setfield(state_, -2, "z");
                break;
            }
            case ReflectedValueType::Mat4: {
                const glm::mat4 matrix = std::get<glm::mat4>(value);
                lua_newtable(state_);
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        lua_pushnumber(state_, matrix[column][row]);
                        lua_seti(state_, -2, column * 4 + row + 1);
                    }
                }
                break;
            }
            case ReflectedValueType::SceneObjectId:
                lua_newtable(state_);
                lua_pushinteger(state_, static_cast<lua_Integer>(std::get<SceneObjectId>(value).value));
                lua_setfield(state_, -2, "sceneObjectId");
                break;
            case ReflectedValueType::AssetId:
                lua_newtable(state_);
                lua_pushinteger(state_, static_cast<lua_Integer>(std::get<AssetId>(value).value));
                lua_setfield(state_, -2, "assetId");
                break;
            case ReflectedValueType::TerrainSourceChunkId: {
                const TerrainSourceChunkId id = std::get<TerrainSourceChunkId>(value);
                lua_newtable(state_);
                lua_pushinteger(state_, static_cast<lua_Integer>(id.source.value));
                lua_setfield(state_, -2, "sourceAssetId");
                lua_pushinteger(state_, id.coord.x);
                lua_setfield(state_, -2, "x");
                lua_pushinteger(state_, id.coord.z);
                lua_setfield(state_, -2, "z");
                break;
            }
            case ReflectedValueType::OpaqueHandle:
                pushOpaque(std::get<OpaqueHandle>(value));
                break;
            default:
                lua_pushnil(state_);
                break;
        }
    }

    std::optional<ReflectedValue> SceneLuaRuntime::readReflectedValue(ReflectedValueType type, int index)
    {
        switch (type) {
            case ReflectedValueType::Bool:
                if (lua_isboolean(state_, index)) {
                    return static_cast<bool>(lua_toboolean(state_, index));
                }
                break;
            case ReflectedValueType::Int64:
                if (lua_isinteger(state_, index)) {
                    return static_cast<int64_t>(lua_tointeger(state_, index));
                }
                break;
            case ReflectedValueType::UInt64:
                if (lua_isinteger(state_, index) && lua_tointeger(state_, index) >= 0) {
                    return static_cast<uint64_t>(lua_tointeger(state_, index));
                }
                break;
            case ReflectedValueType::Float:
                if (lua_isnumber(state_, index)) {
                    return static_cast<float>(lua_tonumber(state_, index));
                }
                break;
            case ReflectedValueType::String:
                if (lua_isstring(state_, index)) {
                    return std::string{lua_tostring(state_, index)};
                }
                break;
            case ReflectedValueType::Vec2:
                if (lua_istable(state_, index)) {
                    float x = 0.0f;
                    float y = 0.0f;
                    if ((numericField(state_, index, "x", x) || numericIndex(state_, index, 1, x)) &&
                        (numericField(state_, index, "y", y) || numericIndex(state_, index, 2, y))) {
                        return glm::vec2{x, y};
                    }
                }
                break;
            case ReflectedValueType::Vec3:
                if (lua_istable(state_, index)) {
                    float x = 0.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                    if ((numericField(state_, index, "x", x) || numericIndex(state_, index, 1, x)) &&
                        (numericField(state_, index, "y", y) || numericIndex(state_, index, 2, y)) &&
                        (numericField(state_, index, "z", z) || numericIndex(state_, index, 3, z))) {
                        return glm::vec3{x, y, z};
                    }
                }
                break;
            case ReflectedValueType::Quat:
                if (lua_istable(state_, index)) {
                    float w = 1.0f;
                    float x = 0.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                    if ((numericField(state_, index, "w", w) || numericIndex(state_, index, 1, w)) &&
                        (numericField(state_, index, "x", x) || numericIndex(state_, index, 2, x)) &&
                        (numericField(state_, index, "y", y) || numericIndex(state_, index, 3, y)) &&
                        (numericField(state_, index, "z", z) || numericIndex(state_, index, 4, z))) {
                        return glm::quat{w, x, y, z};
                    }
                }
                break;
            case ReflectedValueType::Mat4:
                if (lua_istable(state_, index)) {
                    glm::mat4 matrix{1.0f};
                    for (int column = 0; column < 4; ++column) {
                        for (int row = 0; row < 4; ++row) {
                            float element = 0.0f;
                            if (!numericIndex(state_, index, column * 4 + row + 1, element)) {
                                return std::nullopt;
                            }
                            matrix[column][row] = element;
                        }
                    }
                    return matrix;
                }
                break;
            case ReflectedValueType::SceneObjectId:
                if (lua_istable(state_, index)) {
                    if (const auto id = tableUIntField(state_, index, "sceneObjectId")) {
                        return SceneObjectId{*id};
                    }
                }
                break;
            case ReflectedValueType::AssetId:
                if (lua_istable(state_, index)) {
                    if (const auto id = tableUIntField(state_, index, "assetId")) {
                        return AssetId{*id};
                    }
                }
                break;
            case ReflectedValueType::TerrainSourceChunkId:
                if (lua_istable(state_, index)) {
                    const auto source = tableUIntField(state_, index, "sourceAssetId");
                    lua_getfield(state_, index, "x");
                    const bool hasX = lua_isinteger(state_, -1);
                    const int32_t x = hasX ? static_cast<int32_t>(lua_tointeger(state_, -1)) : 0;
                    lua_pop(state_, 1);
                    lua_getfield(state_, index, "z");
                    const bool hasZ = lua_isinteger(state_, -1);
                    const int32_t z = hasZ ? static_cast<int32_t>(lua_tointeger(state_, -1)) : 0;
                    lua_pop(state_, 1);
                    if (source && hasX && hasZ) {
                        return TerrainSourceChunkId{AssetId{*source}, TerrainSourceChunkCoord{x, z}};
                    }
                }
                break;
            case ReflectedValueType::OpaqueHandle:
                if (const auto handle = readOpaque(index)) {
                    return *handle;
                }
                break;
            default:
                break;
        }
        return std::nullopt;
    }

    std::optional<ReflectedValueType> SceneLuaRuntime::propertyType(OpaqueHandle target, SceneReflectedPropertyId property) const
    {
        const auto objectId = reflectedObjectId(target.kind);
        if (!objectId) {
            return std::nullopt;
        }
        if (const ReflectedPropertyDescriptor* descriptor = reflection_.property(*objectId, static_cast<uint32_t>(property))) {
            return descriptor->type;
        }
        return std::nullopt;
    }

    int SceneLuaRuntime::luaEngineGet(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        const auto handle = self->readOpaque(1);
        if (!handle) {
            lua_pushnil(state);
            lua_pushliteral(state, "InvalidHandle");
            return 2;
        }
        const auto property = self->readProperty(*handle, 2);
        if (!property) {
            lua_pushnil(state);
            lua_pushliteral(state, "UnknownProperty");
            return 2;
        }
        ReflectionResult result = getReflectedProperty(self->reflectionContext_, *handle, *property);
        if (result.status != ReflectionStatus::Success) {
            lua_pushnil(state);
            lua_pushliteral(state, "ReflectionFailed");
            return 2;
        }
        self->pushReflectedValue(result.value);
        lua_pushliteral(state, "Success");
        return 2;
    }

    int SceneLuaRuntime::luaEngineSet(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        const auto handle = self->readOpaque(1);
        if (!handle) {
            lua_pushboolean(state, false);
            lua_pushliteral(state, "InvalidHandle");
            return 2;
        }
        const auto property = self->readProperty(*handle, 2);
        if (!property) {
            lua_pushboolean(state, false);
            lua_pushliteral(state, "UnknownProperty");
            return 2;
        }
        const auto type = self->propertyType(*handle, *property);
        if (!type) {
            lua_pushboolean(state, false);
            lua_pushliteral(state, "UnknownProperty");
            return 2;
        }
        const auto value = self->readReflectedValue(*type, 3);
        if (!value) {
            ++self->diagnostics_.typeMismatchCount;
            lua_pushboolean(state, false);
            lua_pushliteral(state, "TypeMismatch");
            return 2;
        }
        ReflectionResult result = setReflectedProperty(self->reflectionContext_, *handle, *property, *value);
        lua_pushboolean(state, result.status == ReflectionStatus::Success);
        lua_pushstring(state, result.status == ReflectionStatus::Success ? "Success" : "ReflectionFailed");
        return 2;
    }

    int SceneLuaRuntime::luaEngineSetAndNotify(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        const auto handle = self->readOpaque(1);
        if (!handle) {
            lua_pushboolean(state, false);
            lua_pushliteral(state, "InvalidHandle");
            return 2;
        }
        const auto property = self->readProperty(*handle, 2);
        if (!property) {
            lua_pushboolean(state, false);
            lua_pushliteral(state, "UnknownProperty");
            return 2;
        }
        const auto type = self->propertyType(*handle, *property);
        const auto value = type ? self->readReflectedValue(*type, 3) : std::optional<ReflectedValue>{};
        if (!type || !value) {
            ++self->diagnostics_.typeMismatchCount;
            lua_pushboolean(state, false);
            lua_pushliteral(state, "TypeMismatch");
            return 2;
        }
        ReflectionResult result = self->hooks_.setReflectedPropertyAndNotify(*handle, *property, *value);
        lua_pushboolean(state, result.status == ReflectionStatus::Success);
        lua_pushstring(state, result.status == ReflectionStatus::Success ? "Success" : "ReflectionFailed");
        return 2;
    }

    int SceneLuaRuntime::luaEngineIsValid(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        const auto handle = self->readOpaque(1);
        bool valid = false;
        if (handle && isValid(*handle)) {
            if (const auto objectId = self->reflectedObjectId(handle->kind)) {
                if (const ReflectedObjectDescriptor* object = self->reflection_.object(*objectId);
                    object && !object->properties.empty()) {
                    valid = getReflectedProperty(
                        self->reflectionContext_,
                        *handle,
                        static_cast<SceneReflectedPropertyId>(object->properties.front().id)).status == ReflectionStatus::Success;
                }
            }
        }
        lua_pushboolean(state, valid);
        return 1;
    }

    int SceneLuaRuntime::luaEngineKind(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        const auto handle = self->readOpaque(1);
        lua_pushstring(state, handle ? opaqueKindName(handle->kind).c_str() : "Invalid");
        return 1;
    }

    int SceneLuaRuntime::luaEngineTarget(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        self->pushOpaque(self->currentContext_ ? self->currentContext_->target : OpaqueHandle{});
        return 1;
    }

    int SceneLuaRuntime::luaEngineDisableSelf(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        lua_pushboolean(state, self->setScriptEnabled(self->currentScript_, false));
        return 1;
    }

    int SceneLuaRuntime::luaEngineUnregisterSelf(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        ScriptRecord* script = self->record(self->currentScript_);
        lua_pushboolean(state, script ? self->hooks_.requestUnregisterBehavior(script->state.behavior) : false);
        return 1;
    }

    int SceneLuaRuntime::luaOpaqueEquals(lua_State* state)
    {
        SceneLuaRuntime* self = runtime(state);
        const auto lhs = self->readOpaque(1);
        const auto rhs = self->readOpaque(2);
        lua_pushboolean(state, lhs && rhs && *lhs == *rhs);
        return 1;
    }

    void SceneLuaRuntime::luaInstructionHook(lua_State* state, lua_Debug*)
    {
        SceneLuaRuntime* self = runtime(state);
        if (!self || self->remainingInstructions_ == 0) {
            luaL_error(state, "instruction budget exceeded");
            return;
        }
        if (self->remainingInstructions_ <= 100) {
            self->remainingInstructions_ = 0;
        } else {
            self->remainingInstructions_ -= 100;
        }
    }

    SceneLuaRuntime* SceneLuaRuntime::runtime(lua_State* state)
    {
        if (lua_type(state, lua_upvalueindex(1)) == LUA_TLIGHTUSERDATA && lua_touserdata(state, lua_upvalueindex(1))) {
            return static_cast<SceneLuaRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        }
        return *static_cast<SceneLuaRuntime**>(lua_getextraspace(state));
    }
}
