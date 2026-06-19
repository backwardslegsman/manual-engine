#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Engine/SceneBehaviorHooks.hpp"

struct lua_State;
struct lua_Debug;

namespace Engine {
    struct LuaScriptId {
        uint32_t value = 0;
    };

    struct LuaScriptInstanceHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    [[nodiscard]] constexpr bool isValid(LuaScriptId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool isValid(LuaScriptInstanceHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(LuaScriptId lhs, LuaScriptId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(LuaScriptId lhs, LuaScriptId rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(LuaScriptInstanceHandle lhs, LuaScriptInstanceHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(LuaScriptInstanceHandle lhs, LuaScriptInstanceHandle rhs)
    {
        return !(lhs == rhs);
    }

    enum class LuaScriptSourceKind {
        Inline,
        File,
    };

    enum class LuaScriptState {
        Unloaded,
        Loaded,
        Error,
    };

    enum class LuaScriptFailurePolicy {
        Report,
        DisableScript,
        RequestUnregister,
    };

    enum class LuaScriptStatus {
        Success,
        InvalidInput,
        InvalidScript,
        CompileError,
        RuntimeError,
        BudgetExceeded,
        TypeMismatch,
        ReflectionFailed,
        IoError,
    };

    struct LuaScriptDescriptor {
        LuaScriptId id;
        std::string debugName;
        LuaScriptSourceKind sourceKind = LuaScriptSourceKind::Inline;
        std::string inlineSource;
        std::filesystem::path sourcePath;
        SceneBehaviorTargetKind targetKind = SceneBehaviorTargetKind::Scene;
        OpaqueHandle target;
        std::vector<SceneTickPhase> phases;
        int32_t priority = 0;
        bool enabled = true;
        std::string onLoad = "on_load";
        std::string onStart = "on_start";
        std::string onStop = "on_stop";
        std::string onUnload = "on_unload";
        std::string onTargetInvalidated = "on_target_invalidated";
        std::string onTick = "on_tick";
        std::string onPropertyChanged = "on_property_changed";
        uint32_t instructionBudget = 10000;
        LuaScriptFailurePolicy failurePolicy = LuaScriptFailurePolicy::Report;
    };

    struct LuaScriptInstanceState {
        LuaScriptState state = LuaScriptState::Unloaded;
        bool enabled = false;
        OpaqueHandle target;
        SceneBehaviorHandle behavior;
        LuaScriptStatus lastStatus = LuaScriptStatus::Success;
        std::string lastMessage;
        uint32_t lifecycleCallCount = 0;
        uint32_t tickCallCount = 0;
        uint32_t propertyCallCount = 0;
        uint32_t failedCallCount = 0;
        uint32_t budgetExceededCount = 0;
        uint64_t elapsedMicroseconds = 0;
    };

    struct LuaScriptDebugRecord {
        LuaScriptInstanceHandle script;
        LuaScriptId id;
        std::string event;
        LuaScriptStatus status = LuaScriptStatus::Success;
        std::string message;
        uint64_t elapsedMicroseconds = 0;
    };

    struct SceneLuaDiagnostics {
        uint32_t loadedScriptCount = 0;
        uint32_t enabledScriptCount = 0;
        uint32_t errorScriptCount = 0;
        uint32_t lifecycleCallCount = 0;
        uint32_t tickCallCount = 0;
        uint32_t propertyCallCount = 0;
        uint32_t failedCallCount = 0;
        uint32_t budgetExceededCount = 0;
        uint32_t invalidHandleCount = 0;
        uint32_t typeMismatchCount = 0;
        uint32_t reloadCount = 0;
        uint32_t reloadFailureCount = 0;
        uint64_t elapsedMicroseconds = 0;
        LuaScriptStatus lastStatus = LuaScriptStatus::Success;
        std::string lastScriptName;
        std::string lastMessage;
        std::vector<std::string> stackTraces;
    };

    class SceneLuaRuntime {
    public:
        SceneLuaRuntime(
            Scene& scene,
            ReflectionRegistry& reflection,
            SceneReflectionContext& reflectionContext,
            SceneBehaviorHooks& hooks);
        ~SceneLuaRuntime();

        SceneLuaRuntime(const SceneLuaRuntime&) = delete;
        SceneLuaRuntime& operator=(const SceneLuaRuntime&) = delete;

        [[nodiscard]] LuaScriptInstanceHandle loadScript(LuaScriptDescriptor descriptor);
        bool unloadScript(LuaScriptInstanceHandle script);
        [[nodiscard]] LuaScriptStatus reloadScript(LuaScriptInstanceHandle script);
        bool setScriptEnabled(LuaScriptInstanceHandle script, bool enabled);
        [[nodiscard]] bool contains(LuaScriptInstanceHandle script) const;
        [[nodiscard]] std::optional<LuaScriptDescriptor> descriptor(LuaScriptInstanceHandle script) const;
        [[nodiscard]] std::optional<LuaScriptInstanceState> state(LuaScriptInstanceHandle script) const;

        [[nodiscard]] SceneLuaDiagnostics diagnostics() const;
        [[nodiscard]] std::vector<LuaScriptDebugRecord> debugRecords() const;
        void clearDebugRecords();
        void shutdown();

    private:
        struct ScriptRecord {
            uint32_t generation = 0;
            bool occupied = false;
            LuaScriptDescriptor descriptor;
            LuaScriptInstanceState state;
            int environmentRef = -2;
            bool invoking = false;
        };

        [[nodiscard]] ScriptRecord* record(LuaScriptInstanceHandle script);
        [[nodiscard]] const ScriptRecord* record(LuaScriptInstanceHandle script) const;
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        void freeSlot(uint32_t index);
        void refreshDiagnostics();
        void noteStatus(ScriptRecord* script, LuaScriptStatus status, std::string message = {});

        [[nodiscard]] std::string scriptSource(const LuaScriptDescriptor& descriptor, LuaScriptStatus& status, std::string& message) const;
        [[nodiscard]] int compileEnvironment(const LuaScriptDescriptor& descriptor, LuaScriptStatus& status, std::string& message);
        [[nodiscard]] SceneBehaviorDescriptor behaviorDescriptorFor(LuaScriptInstanceHandle handle, ScriptRecord& script);
        SceneBehaviorResult invokeLifecycle(LuaScriptInstanceHandle handle, SceneBehaviorContext& context, const std::string& functionName, const std::string& eventName);
        SceneBehaviorResult invokeTick(LuaScriptInstanceHandle handle, SceneBehaviorContext& context);
        SceneBehaviorResult invokePropertyChanged(LuaScriptInstanceHandle handle, SceneBehaviorContext& context);
        SceneBehaviorResult invokeFunction(
            LuaScriptInstanceHandle handle,
            SceneBehaviorContext& context,
            const std::string& functionName,
            const std::string& eventName);

        void setupLuaState();
        void pushEngineTable();
        void pushContextTable(const SceneBehaviorContext& context);
        void pushOpaque(OpaqueHandle handle);
        [[nodiscard]] std::optional<OpaqueHandle> readOpaque(int index);
        [[nodiscard]] std::optional<SceneReflectedPropertyId> readProperty(OpaqueHandle target, int index);
        [[nodiscard]] std::optional<uint32_t> reflectedObjectId(OpaqueHandleKind kind) const;
        void pushReflectedValue(const ReflectedValue& value);
        [[nodiscard]] std::optional<ReflectedValue> readReflectedValue(ReflectedValueType type, int index);
        [[nodiscard]] std::optional<ReflectedValueType> propertyType(OpaqueHandle target, SceneReflectedPropertyId property) const;

        static int luaEngineGet(lua_State* state);
        static int luaEngineSet(lua_State* state);
        static int luaEngineSetAndNotify(lua_State* state);
        static int luaEngineIsValid(lua_State* state);
        static int luaEngineKind(lua_State* state);
        static int luaEngineTarget(lua_State* state);
        static int luaEngineDisableSelf(lua_State* state);
        static int luaEngineUnregisterSelf(lua_State* state);
        static int luaOpaqueEquals(lua_State* state);
        static void luaInstructionHook(lua_State* state, lua_Debug* debug);

        [[nodiscard]] static SceneLuaRuntime* runtime(lua_State* state);

        Scene& scene_;
        ReflectionRegistry& reflection_;
        SceneReflectionContext& reflectionContext_;
        SceneBehaviorHooks& hooks_;
        lua_State* state_ = nullptr;
        std::vector<ScriptRecord> scripts_;
        LuaScriptInstanceHandle currentScript_;
        SceneBehaviorContext* currentContext_ = nullptr;
        uint32_t remainingInstructions_ = 0;
        SceneLuaDiagnostics diagnostics_;
        std::vector<LuaScriptDebugRecord> debugRecords_;
    };
}
