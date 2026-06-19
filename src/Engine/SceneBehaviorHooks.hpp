#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneReflection.hpp"

namespace Engine {
    class SceneBehaviorHooks;

    struct SceneBehaviorTypeId {
        uint32_t value = 0;
    };

    struct SceneBehaviorHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    [[nodiscard]] constexpr bool isValid(SceneBehaviorTypeId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneBehaviorHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(SceneBehaviorTypeId lhs, SceneBehaviorTypeId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(SceneBehaviorTypeId lhs, SceneBehaviorTypeId rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneBehaviorHandle lhs, SceneBehaviorHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneBehaviorHandle lhs, SceneBehaviorHandle rhs)
    {
        return !(lhs == rhs);
    }

    enum class SceneBehaviorTargetKind {
        Scene,
        Actor,
        Component,
        System,
        CustomOpaque,
    };

    enum class SceneBehaviorLifecycleEvent {
        Load,
        Start,
        Stop,
        Unload,
        TargetInvalidated,
    };

    enum class SceneBehaviorStatus {
        Success,
        InvalidInput,
        InvalidBehavior,
        InvalidTarget,
        WrongTargetKind,
        DuplicateSchedulerSystem,
        SchedulerRejected,
        MutationRejected,
        CallbackFailed,
        ReflectionFailed,
    };

    struct SceneBehaviorResult {
        SceneBehaviorStatus status = SceneBehaviorStatus::Success;
        std::string message;

        [[nodiscard]] static SceneBehaviorResult success()
        {
            return {};
        }
    };

    struct SceneBehaviorPropertyChange {
        OpaqueHandle target;
        SceneReflectedPropertyId property = SceneReflectedPropertyId::StableId;
        ReflectedValue oldValue;
        ReflectedValue newValue;
    };

    struct SceneBehaviorContext {
        Scene* scene = nullptr;
        ReflectionRegistry* reflection = nullptr;
        SceneReflectionContext* reflectionContext = nullptr;
        SceneBehaviorHooks* hooks = nullptr;
        SceneBehaviorHandle behavior;
        SceneBehaviorTypeId behaviorType;
        SceneBehaviorTargetKind targetKind = SceneBehaviorTargetKind::Scene;
        OpaqueHandle target;
        std::optional<SceneBehaviorLifecycleEvent> lifecycleEvent;
        std::optional<SceneTickContext> tick;
        const SceneBehaviorPropertyChange* propertyChange = nullptr;

        [[nodiscard]] ReflectionResult get(SceneReflectedPropertyId property) const;
        [[nodiscard]] ReflectionResult set(SceneReflectedPropertyId property, const ReflectedValue& value) const;
        bool requestUnregisterSelf() const;
    };

    using SceneBehaviorLifecycleCallback = std::function<SceneBehaviorResult(SceneBehaviorContext&)>;
    using SceneBehaviorTickCallback = std::function<SceneBehaviorResult(SceneBehaviorContext&)>;
    using SceneBehaviorPropertyChangedCallback = std::function<SceneBehaviorResult(SceneBehaviorContext&)>;

    struct SceneBehaviorDescriptor {
        SceneBehaviorTypeId type;
        std::string debugName;
        SceneBehaviorTargetKind targetKind = SceneBehaviorTargetKind::Scene;
        OpaqueHandle target;
        std::vector<SceneTickPhase> phases;
        int32_t priority = 0;
        bool enabled = true;
        bool disableOnFailure = false;
        std::vector<SceneReflectedPropertyId> requiredProperties;
        SceneBehaviorLifecycleCallback onLoad;
        SceneBehaviorLifecycleCallback onStart;
        SceneBehaviorLifecycleCallback onStop;
        SceneBehaviorLifecycleCallback onUnload;
        SceneBehaviorLifecycleCallback onTargetInvalidated;
        SceneBehaviorTickCallback onTick;
        SceneBehaviorPropertyChangedCallback onPropertyChanged;
    };

    struct SceneBehaviorDebugRecord {
        SceneBehaviorHandle behavior;
        SceneBehaviorTypeId type;
        SceneBehaviorTargetKind targetKind = SceneBehaviorTargetKind::Scene;
        OpaqueHandle target;
        std::string event;
        SceneBehaviorStatus status = SceneBehaviorStatus::Success;
        std::string message;
        uint64_t elapsedMicroseconds = 0;
    };

    struct SceneBehaviorDiagnostics {
        uint32_t registeredCount = 0;
        uint32_t enabledCount = 0;
        uint32_t disabledCount = 0;
        uint32_t lifecycleCallbackCount = 0;
        uint32_t tickCallbackCount = 0;
        uint32_t propertyCallbackCount = 0;
        uint32_t skippedCallbackCount = 0;
        uint32_t failedCallbackCount = 0;
        uint32_t invalidTargetCount = 0;
        uint32_t deferredUnregisterCount = 0;
        uint32_t mutationDuringDispatchCount = 0;
        uint64_t elapsedMicroseconds = 0;
        SceneBehaviorStatus lastStatus = SceneBehaviorStatus::Success;
        std::string lastMessage;
        std::vector<std::string> warnings;
    };

    class SceneBehaviorHooks {
    public:
        SceneBehaviorHooks(Scene& scene, ReflectionRegistry& reflection, SceneReflectionContext& reflectionContext);
        ~SceneBehaviorHooks();

        [[nodiscard]] SceneBehaviorHandle registerBehavior(SceneBehaviorDescriptor descriptor);
        bool unregisterBehavior(SceneBehaviorHandle behavior);
        bool requestUnregisterBehavior(SceneBehaviorHandle behavior);
        bool setBehaviorEnabled(SceneBehaviorHandle behavior, bool enabled);
        [[nodiscard]] bool contains(SceneBehaviorHandle behavior) const;
        [[nodiscard]] std::optional<SceneBehaviorDescriptor> descriptor(SceneBehaviorHandle behavior) const;

        [[nodiscard]] SceneBehaviorStatus registerSchedulerSystem();
        bool unregisterSchedulerSystem();

        SceneBehaviorResult notifyPropertyChanged(const SceneBehaviorPropertyChange& change);
        [[nodiscard]] ReflectionResult setReflectedPropertyAndNotify(
            OpaqueHandle target,
            SceneReflectedPropertyId property,
            const ReflectedValue& value);

        [[nodiscard]] SceneBehaviorDiagnostics diagnostics() const;
        [[nodiscard]] std::vector<SceneBehaviorDebugRecord> debugRecords() const;
        void clearDebugRecords();
        void purgeDeferredUnregisters();

    private:
        struct BehaviorRecord {
            uint32_t generation = 0;
            bool occupied = false;
            bool pendingUnregister = false;
            bool targetInvalidatedNotified = false;
            uint64_t registrationOrder = 0;
            SceneBehaviorDescriptor descriptor;
        };

        [[nodiscard]] BehaviorRecord* record(SceneBehaviorHandle behavior);
        [[nodiscard]] const BehaviorRecord* record(SceneBehaviorHandle behavior) const;
        [[nodiscard]] bool canMutateRegistrations() const;
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        void freeSlot(uint32_t index);
        void refreshDiagnostics();
        void noteStatus(SceneBehaviorStatus status, std::string message = {});

        [[nodiscard]] bool wantsPhase(const BehaviorRecord& record, SceneTickPhase phase) const;
        [[nodiscard]] bool targetValid(const BehaviorRecord& record) const;
        [[nodiscard]] std::vector<SceneBehaviorHandle> handlesForLifecycle(bool reverse) const;
        [[nodiscard]] std::vector<SceneBehaviorHandle> handlesForPhase(SceneTickPhase phase) const;
        [[nodiscard]] std::vector<SceneBehaviorHandle> handlesForPropertyChange(const SceneBehaviorPropertyChange& change) const;

        SceneBehaviorResult dispatchLifecycle(SceneBehaviorLifecycleEvent event);
        SceneBehaviorResult dispatchTick(const SceneTickContext& tick);
        SceneBehaviorResult dispatchPropertyChanged(const SceneBehaviorPropertyChange& change);
        SceneBehaviorResult invokeLifecycle(BehaviorRecord& record, SceneBehaviorHandle handle, SceneBehaviorLifecycleEvent event);
        SceneBehaviorResult invokeTick(BehaviorRecord& record, SceneBehaviorHandle handle, const SceneTickContext& tick);
        SceneBehaviorResult invokePropertyChanged(
            BehaviorRecord& record,
            SceneBehaviorHandle handle,
            const SceneBehaviorPropertyChange& change);
        SceneBehaviorResult invokeCallback(
            BehaviorRecord& record,
            SceneBehaviorHandle handle,
            const std::string& eventName,
            const std::function<SceneBehaviorResult(SceneBehaviorContext&)>& callback,
            SceneBehaviorContext& context);
        SceneBehaviorContext makeContext(BehaviorRecord& record, SceneBehaviorHandle handle);

        Scene& scene_;
        ReflectionRegistry& reflection_;
        SceneReflectionContext& reflectionContext_;
        std::vector<BehaviorRecord> behaviors_;
        uint64_t nextRegistrationOrder_ = 1;
        bool dispatching_ = false;
        SceneSystemHandle schedulerSystem_;
        SceneBehaviorDiagnostics diagnostics_;
        std::vector<SceneBehaviorDebugRecord> debugRecords_;
    };
}
