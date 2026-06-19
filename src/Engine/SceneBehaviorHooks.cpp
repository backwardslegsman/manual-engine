#include "Engine/SceneBehaviorHooks.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace Engine {
    namespace {
        [[nodiscard]] const char* lifecycleName(SceneBehaviorLifecycleEvent event)
        {
            switch (event) {
                case SceneBehaviorLifecycleEvent::Load:
                    return "load";
                case SceneBehaviorLifecycleEvent::Start:
                    return "start";
                case SceneBehaviorLifecycleEvent::Stop:
                    return "stop";
                case SceneBehaviorLifecycleEvent::Unload:
                    return "unload";
                case SceneBehaviorLifecycleEvent::TargetInvalidated:
                    return "target_invalidated";
            }
            return "unknown";
        }

        [[nodiscard]] bool sameOpaqueTarget(OpaqueHandle lhs, OpaqueHandle rhs)
        {
            return lhs == rhs;
        }

        [[nodiscard]] bool isComponentTargetKind(OpaqueHandleKind kind)
        {
            return kind == OpaqueHandleKind::SceneComponent ||
                kind == OpaqueHandleKind::SceneMeshComponent ||
                kind == OpaqueHandleKind::SceneSkinnedMeshComponent ||
                kind == OpaqueHandleKind::SceneLightComponent ||
                kind == OpaqueHandleKind::SceneCameraComponent ||
                kind == OpaqueHandleKind::ScenePhysicsCollider;
        }
    }

    ReflectionResult SceneBehaviorContext::get(SceneReflectedPropertyId property) const
    {
        if (!reflectionContext) {
            return {ReflectionStatus::Unsupported, "missing scene reflection context"};
        }
        return getReflectedProperty(*reflectionContext, target, property);
    }

    ReflectionResult SceneBehaviorContext::set(SceneReflectedPropertyId property, const ReflectedValue& value) const
    {
        if (!reflectionContext) {
            return {ReflectionStatus::Unsupported, "missing scene reflection context"};
        }
        return setReflectedProperty(*reflectionContext, target, property, value);
    }

    bool SceneBehaviorContext::requestUnregisterSelf() const
    {
        return hooks ? hooks->requestUnregisterBehavior(behavior) : false;
    }

    SceneBehaviorHooks::SceneBehaviorHooks(
        Scene& scene,
        ReflectionRegistry& reflection,
        SceneReflectionContext& reflectionContext)
        : scene_(scene)
        , reflection_(reflection)
        , reflectionContext_(reflectionContext)
    {
        if (!reflectionContext_.scene) {
            reflectionContext_.scene = &scene_;
        }
    }

    SceneBehaviorHooks::~SceneBehaviorHooks()
    {
        unregisterSchedulerSystem();
    }

    SceneBehaviorHandle SceneBehaviorHooks::registerBehavior(SceneBehaviorDescriptor descriptor)
    {
        purgeDeferredUnregisters();
        if (!canMutateRegistrations()) {
            noteStatus(SceneBehaviorStatus::MutationRejected, "behavior registration is not allowed while scene is started");
            return {};
        }
        if (!isValid(descriptor.type)) {
            noteStatus(SceneBehaviorStatus::InvalidInput, "behavior type id is invalid");
            return {};
        }
        if (descriptor.targetKind != SceneBehaviorTargetKind::Scene && !isValid(descriptor.target)) {
            noteStatus(SceneBehaviorStatus::InvalidTarget, "behavior target is invalid");
            return {};
        }

        for (uint32_t index = 0; index < behaviors_.size(); ++index) {
            BehaviorRecord& candidate = behaviors_[index];
            if (candidate.occupied) {
                continue;
            }
            candidate.occupied = true;
            candidate.pendingUnregister = false;
            candidate.targetInvalidatedNotified = false;
            candidate.registrationOrder = nextRegistrationOrder_++;
            candidate.descriptor = std::move(descriptor);
            refreshDiagnostics();
            return {index, candidate.generation};
        }

        BehaviorRecord record;
        record.generation = nextGeneration(0);
        record.occupied = true;
        record.registrationOrder = nextRegistrationOrder_++;
        record.descriptor = std::move(descriptor);
        behaviors_.push_back(std::move(record));
        refreshDiagnostics();
        return {static_cast<uint32_t>(behaviors_.size() - 1), behaviors_.back().generation};
    }

    bool SceneBehaviorHooks::unregisterBehavior(SceneBehaviorHandle behavior)
    {
        purgeDeferredUnregisters();
        if (!canMutateRegistrations()) {
            noteStatus(SceneBehaviorStatus::MutationRejected, "behavior unregister is not allowed while scene is started");
            return false;
        }
        BehaviorRecord* current = record(behavior);
        if (!current) {
            noteStatus(SceneBehaviorStatus::InvalidBehavior, "behavior handle is invalid");
            return false;
        }
        freeSlot(behavior.index);
        refreshDiagnostics();
        return true;
    }

    bool SceneBehaviorHooks::requestUnregisterBehavior(SceneBehaviorHandle behavior)
    {
        BehaviorRecord* current = record(behavior);
        if (!current) {
            noteStatus(SceneBehaviorStatus::InvalidBehavior, "behavior handle is invalid");
            return false;
        }
        current->descriptor.enabled = false;
        current->pendingUnregister = true;
        ++diagnostics_.deferredUnregisterCount;
        if (dispatching_) {
            ++diagnostics_.mutationDuringDispatchCount;
        }
        purgeDeferredUnregisters();
        refreshDiagnostics();
        return true;
    }

    bool SceneBehaviorHooks::setBehaviorEnabled(SceneBehaviorHandle behavior, bool enabled)
    {
        BehaviorRecord* current = record(behavior);
        if (!current) {
            noteStatus(SceneBehaviorStatus::InvalidBehavior, "behavior handle is invalid");
            return false;
        }
        current->descriptor.enabled = enabled;
        refreshDiagnostics();
        return true;
    }

    bool SceneBehaviorHooks::contains(SceneBehaviorHandle behavior) const
    {
        return record(behavior) != nullptr;
    }

    std::optional<SceneBehaviorDescriptor> SceneBehaviorHooks::descriptor(SceneBehaviorHandle behavior) const
    {
        const BehaviorRecord* current = record(behavior);
        if (!current) {
            return std::nullopt;
        }
        return current->descriptor;
    }

    SceneBehaviorStatus SceneBehaviorHooks::registerSchedulerSystem()
    {
        if (scene_.contains(schedulerSystem_)) {
            noteStatus(SceneBehaviorStatus::DuplicateSchedulerSystem, "scene behavior scheduler system is already registered");
            return SceneBehaviorStatus::DuplicateSchedulerSystem;
        }

        SceneSystemDescriptor descriptor;
        descriptor.name = "SceneBehaviorHooks";
        descriptor.phases = {
            SceneTickPhase::BeginFrame,
            SceneTickPhase::FixedPrePhysics,
            SceneTickPhase::FixedPhysics,
            SceneTickPhase::FixedPostPhysics,
            SceneTickPhase::VariableAnimation,
            SceneTickPhase::VariableUpdate,
            SceneTickPhase::PreRender,
            SceneTickPhase::EndFrame,
        };
        descriptor.onLoad = [this](Scene&) {
            dispatchLifecycle(SceneBehaviorLifecycleEvent::Load);
        };
        descriptor.onStart = [this](Scene&) {
            dispatchLifecycle(SceneBehaviorLifecycleEvent::Start);
        };
        descriptor.onStop = [this](Scene&) {
            dispatchLifecycle(SceneBehaviorLifecycleEvent::Stop);
        };
        descriptor.onUnload = [this](Scene&) {
            dispatchLifecycle(SceneBehaviorLifecycleEvent::Unload);
            purgeDeferredUnregisters();
        };
        descriptor.onTick = [this](Scene&, const SceneTickContext& tick) {
            dispatchTick(tick);
        };

        schedulerSystem_ = scene_.registerSystem(std::move(descriptor));
        if (!isValid(schedulerSystem_)) {
            noteStatus(SceneBehaviorStatus::SchedulerRejected, "scene rejected behavior scheduler registration");
            return SceneBehaviorStatus::SchedulerRejected;
        }
        refreshDiagnostics();
        return SceneBehaviorStatus::Success;
    }

    bool SceneBehaviorHooks::unregisterSchedulerSystem()
    {
        if (!scene_.contains(schedulerSystem_)) {
            schedulerSystem_ = {};
            return true;
        }
        const bool removed = scene_.unregisterSystem(schedulerSystem_);
        if (removed) {
            schedulerSystem_ = {};
        }
        return removed;
    }

    SceneBehaviorResult SceneBehaviorHooks::notifyPropertyChanged(const SceneBehaviorPropertyChange& change)
    {
        return dispatchPropertyChanged(change);
    }

    ReflectionResult SceneBehaviorHooks::setReflectedPropertyAndNotify(
        OpaqueHandle target,
        SceneReflectedPropertyId property,
        const ReflectedValue& value)
    {
        ReflectionResult oldValue = getReflectedProperty(reflectionContext_, target, property);
        ReflectionResult setResult = setReflectedProperty(reflectionContext_, target, property, value);
        if (setResult.status != ReflectionStatus::Success || !setResult.changed) {
            return setResult;
        }

        ReflectionResult newValue = getReflectedProperty(reflectionContext_, target, property);
        SceneBehaviorPropertyChange change;
        change.target = target;
        change.property = property;
        change.oldValue = std::move(oldValue.value);
        change.newValue = std::move(newValue.value);
        dispatchPropertyChanged(change);
        return setResult;
    }

    SceneBehaviorDiagnostics SceneBehaviorHooks::diagnostics() const
    {
        return diagnostics_;
    }

    std::vector<SceneBehaviorDebugRecord> SceneBehaviorHooks::debugRecords() const
    {
        return debugRecords_;
    }

    void SceneBehaviorHooks::clearDebugRecords()
    {
        debugRecords_.clear();
    }

    void SceneBehaviorHooks::purgeDeferredUnregisters()
    {
        if (dispatching_ || scene_.lifecycleState() == SceneLifecycleState::Started) {
            return;
        }
        for (uint32_t index = 0; index < behaviors_.size(); ++index) {
            if (behaviors_[index].occupied && behaviors_[index].pendingUnregister) {
                freeSlot(index);
            }
        }
        refreshDiagnostics();
    }

    SceneBehaviorHooks::BehaviorRecord* SceneBehaviorHooks::record(SceneBehaviorHandle behavior)
    {
        if (!isValid(behavior) || behavior.index >= behaviors_.size()) {
            return nullptr;
        }
        BehaviorRecord& current = behaviors_[behavior.index];
        if (!current.occupied || current.generation != behavior.generation) {
            return nullptr;
        }
        return &current;
    }

    const SceneBehaviorHooks::BehaviorRecord* SceneBehaviorHooks::record(SceneBehaviorHandle behavior) const
    {
        if (!isValid(behavior) || behavior.index >= behaviors_.size()) {
            return nullptr;
        }
        const BehaviorRecord& current = behaviors_[behavior.index];
        if (!current.occupied || current.generation != behavior.generation) {
            return nullptr;
        }
        return &current;
    }

    bool SceneBehaviorHooks::canMutateRegistrations() const
    {
        return !dispatching_ &&
            scene_.lifecycleState() != SceneLifecycleState::Started &&
            scene_.lifecycleState() != SceneLifecycleState::Stopping;
    }

    uint32_t SceneBehaviorHooks::nextGeneration(uint32_t generation) const
    {
        ++generation;
        if (generation == 0) {
            generation = 1;
        }
        return generation;
    }

    void SceneBehaviorHooks::freeSlot(uint32_t index)
    {
        if (index >= behaviors_.size()) {
            return;
        }
        BehaviorRecord& current = behaviors_[index];
        current.occupied = false;
        current.pendingUnregister = false;
        current.targetInvalidatedNotified = false;
        current.registrationOrder = 0;
        current.descriptor = {};
        current.generation = nextGeneration(current.generation);
    }

    void SceneBehaviorHooks::refreshDiagnostics()
    {
        diagnostics_.registeredCount = 0;
        diagnostics_.enabledCount = 0;
        diagnostics_.disabledCount = 0;
        for (const BehaviorRecord& current : behaviors_) {
            if (!current.occupied) {
                continue;
            }
            ++diagnostics_.registeredCount;
            if (current.descriptor.enabled && !current.pendingUnregister) {
                ++diagnostics_.enabledCount;
            } else {
                ++diagnostics_.disabledCount;
            }
        }
    }

    void SceneBehaviorHooks::noteStatus(SceneBehaviorStatus status, std::string message)
    {
        diagnostics_.lastStatus = status;
        diagnostics_.lastMessage = std::move(message);
        if (status != SceneBehaviorStatus::Success) {
            diagnostics_.warnings.push_back(diagnostics_.lastMessage);
        }
    }

    bool SceneBehaviorHooks::wantsPhase(const BehaviorRecord& record, SceneTickPhase phase) const
    {
        return std::find(record.descriptor.phases.begin(), record.descriptor.phases.end(), phase) !=
            record.descriptor.phases.end();
    }

    bool SceneBehaviorHooks::targetValid(const BehaviorRecord& record) const
    {
        const SceneBehaviorDescriptor& descriptor = record.descriptor;
        switch (descriptor.targetKind) {
            case SceneBehaviorTargetKind::Scene:
                return true;
            case SceneBehaviorTargetKind::Actor:
                return sceneActorFromOpaque(reflectionContext_, descriptor.target).has_value();
            case SceneBehaviorTargetKind::Component:
                if (descriptor.target.owner != reflectionContext_.owner || !isComponentTargetKind(descriptor.target.kind)) {
                    return false;
                }
                if (descriptor.target.kind == OpaqueHandleKind::SceneComponent && reflectionContext_.scene) {
                    return reflectionContext_.scene->contains(SceneComponentHandle{
                        descriptor.target.index,
                        descriptor.target.generation,
                    });
                }
                return getReflectedProperty(
                    reflectionContext_,
                    descriptor.target,
                    SceneReflectedPropertyId::ComponentOwner).status == ReflectionStatus::Success ||
                    getReflectedProperty(
                        reflectionContext_,
                        descriptor.target,
                        SceneReflectedPropertyId::ColliderBody).status == ReflectionStatus::Success;
            case SceneBehaviorTargetKind::System:
                return descriptor.target.owner == reflectionContext_.owner &&
                    descriptor.target.kind == OpaqueHandleKind::SceneSystem &&
                    reflectionContext_.scene &&
                    reflectionContext_.scene->contains(SceneSystemHandle{
                        descriptor.target.index,
                        descriptor.target.generation,
                    });
            case SceneBehaviorTargetKind::CustomOpaque:
                return descriptor.target.owner == reflectionContext_.owner && isValid(descriptor.target);
        }
        return false;
    }

    std::vector<SceneBehaviorHandle> SceneBehaviorHooks::handlesForLifecycle(bool reverse) const
    {
        std::vector<SceneBehaviorHandle> handles;
        for (uint32_t index = 0; index < behaviors_.size(); ++index) {
            const BehaviorRecord& current = behaviors_[index];
            if (current.occupied) {
                handles.push_back({index, current.generation});
            }
        }
        std::stable_sort(handles.begin(), handles.end(), [this](SceneBehaviorHandle lhs, SceneBehaviorHandle rhs) {
            return behaviors_[lhs.index].registrationOrder < behaviors_[rhs.index].registrationOrder;
        });
        if (reverse) {
            std::reverse(handles.begin(), handles.end());
        }
        return handles;
    }

    std::vector<SceneBehaviorHandle> SceneBehaviorHooks::handlesForPhase(SceneTickPhase phase) const
    {
        std::vector<SceneBehaviorHandle> handles;
        for (uint32_t index = 0; index < behaviors_.size(); ++index) {
            const BehaviorRecord& current = behaviors_[index];
            if (current.occupied && wantsPhase(current, phase)) {
                handles.push_back({index, current.generation});
            }
        }
        std::stable_sort(handles.begin(), handles.end(), [this](SceneBehaviorHandle lhs, SceneBehaviorHandle rhs) {
            const BehaviorRecord& left = behaviors_[lhs.index];
            const BehaviorRecord& right = behaviors_[rhs.index];
            if (left.descriptor.priority != right.descriptor.priority) {
                return left.descriptor.priority < right.descriptor.priority;
            }
            return left.registrationOrder < right.registrationOrder;
        });
        return handles;
    }

    std::vector<SceneBehaviorHandle> SceneBehaviorHooks::handlesForPropertyChange(
        const SceneBehaviorPropertyChange& change) const
    {
        std::vector<SceneBehaviorHandle> handles;
        for (uint32_t index = 0; index < behaviors_.size(); ++index) {
            const BehaviorRecord& current = behaviors_[index];
            if (!current.occupied || !current.descriptor.onPropertyChanged) {
                continue;
            }
            if (current.descriptor.targetKind != SceneBehaviorTargetKind::Scene &&
                !sameOpaqueTarget(current.descriptor.target, change.target)) {
                continue;
            }
            handles.push_back({index, current.generation});
        }
        std::stable_sort(handles.begin(), handles.end(), [this](SceneBehaviorHandle lhs, SceneBehaviorHandle rhs) {
            const BehaviorRecord& left = behaviors_[lhs.index];
            const BehaviorRecord& right = behaviors_[rhs.index];
            if (left.descriptor.priority != right.descriptor.priority) {
                return left.descriptor.priority < right.descriptor.priority;
            }
            return left.registrationOrder < right.registrationOrder;
        });
        return handles;
    }

    SceneBehaviorResult SceneBehaviorHooks::dispatchLifecycle(SceneBehaviorLifecycleEvent event)
    {
        const bool reverse = event == SceneBehaviorLifecycleEvent::Stop || event == SceneBehaviorLifecycleEvent::Unload;
        const auto handles = handlesForLifecycle(reverse);
        dispatching_ = true;
        for (SceneBehaviorHandle handle : handles) {
            BehaviorRecord* current = record(handle);
            if (!current) {
                ++diagnostics_.skippedCallbackCount;
                continue;
            }
            invokeLifecycle(*current, handle, event);
        }
        dispatching_ = false;
        purgeDeferredUnregisters();
        refreshDiagnostics();
        return SceneBehaviorResult::success();
    }

    SceneBehaviorResult SceneBehaviorHooks::dispatchTick(const SceneTickContext& tick)
    {
        const auto handles = handlesForPhase(tick.phase);
        dispatching_ = true;
        for (SceneBehaviorHandle handle : handles) {
            BehaviorRecord* current = record(handle);
            if (!current) {
                ++diagnostics_.skippedCallbackCount;
                continue;
            }
            invokeTick(*current, handle, tick);
        }
        dispatching_ = false;
        purgeDeferredUnregisters();
        refreshDiagnostics();
        return SceneBehaviorResult::success();
    }

    SceneBehaviorResult SceneBehaviorHooks::dispatchPropertyChanged(const SceneBehaviorPropertyChange& change)
    {
        const auto handles = handlesForPropertyChange(change);
        dispatching_ = true;
        for (SceneBehaviorHandle handle : handles) {
            BehaviorRecord* current = record(handle);
            if (!current) {
                ++diagnostics_.skippedCallbackCount;
                continue;
            }
            invokePropertyChanged(*current, handle, change);
        }
        dispatching_ = false;
        purgeDeferredUnregisters();
        refreshDiagnostics();
        return SceneBehaviorResult::success();
    }

    SceneBehaviorResult SceneBehaviorHooks::invokeLifecycle(
        BehaviorRecord& record,
        SceneBehaviorHandle handle,
        SceneBehaviorLifecycleEvent event)
    {
        if (!record.descriptor.enabled || record.pendingUnregister) {
            ++diagnostics_.skippedCallbackCount;
            return SceneBehaviorResult{SceneBehaviorStatus::Success, "behavior disabled"};
        }

        if (!targetValid(record)) {
            ++diagnostics_.invalidTargetCount;
            if (record.targetInvalidatedNotified || !record.descriptor.onTargetInvalidated) {
                ++diagnostics_.skippedCallbackCount;
                return SceneBehaviorResult{SceneBehaviorStatus::InvalidTarget, "behavior target is invalid"};
            }
            record.targetInvalidatedNotified = true;
            SceneBehaviorContext context = makeContext(record, handle);
            context.lifecycleEvent = SceneBehaviorLifecycleEvent::TargetInvalidated;
            return invokeCallback(
                record,
                handle,
                lifecycleName(SceneBehaviorLifecycleEvent::TargetInvalidated),
                record.descriptor.onTargetInvalidated,
                context);
        }

        SceneBehaviorLifecycleCallback callback;
        switch (event) {
            case SceneBehaviorLifecycleEvent::Load:
                callback = record.descriptor.onLoad;
                break;
            case SceneBehaviorLifecycleEvent::Start:
                callback = record.descriptor.onStart;
                break;
            case SceneBehaviorLifecycleEvent::Stop:
                callback = record.descriptor.onStop;
                break;
            case SceneBehaviorLifecycleEvent::Unload:
                callback = record.descriptor.onUnload;
                break;
            case SceneBehaviorLifecycleEvent::TargetInvalidated:
                callback = record.descriptor.onTargetInvalidated;
                break;
        }
        if (!callback) {
            ++diagnostics_.skippedCallbackCount;
            return SceneBehaviorResult::success();
        }

        SceneBehaviorContext context = makeContext(record, handle);
        context.lifecycleEvent = event;
        return invokeCallback(record, handle, lifecycleName(event), callback, context);
    }

    SceneBehaviorResult SceneBehaviorHooks::invokeTick(
        BehaviorRecord& record,
        SceneBehaviorHandle handle,
        const SceneTickContext& tick)
    {
        if (!record.descriptor.enabled || record.pendingUnregister || !record.descriptor.onTick) {
            ++diagnostics_.skippedCallbackCount;
            return SceneBehaviorResult::success();
        }
        if (!targetValid(record)) {
            ++diagnostics_.invalidTargetCount;
            if (!record.targetInvalidatedNotified && record.descriptor.onTargetInvalidated) {
                record.targetInvalidatedNotified = true;
                SceneBehaviorContext invalidated = makeContext(record, handle);
                invalidated.lifecycleEvent = SceneBehaviorLifecycleEvent::TargetInvalidated;
                invokeCallback(
                    record,
                    handle,
                    lifecycleName(SceneBehaviorLifecycleEvent::TargetInvalidated),
                    record.descriptor.onTargetInvalidated,
                    invalidated);
            } else {
                ++diagnostics_.skippedCallbackCount;
            }
            return SceneBehaviorResult{SceneBehaviorStatus::InvalidTarget, "behavior target is invalid"};
        }
        SceneBehaviorContext context = makeContext(record, handle);
        context.tick = tick;
        return invokeCallback(record, handle, "tick", record.descriptor.onTick, context);
    }

    SceneBehaviorResult SceneBehaviorHooks::invokePropertyChanged(
        BehaviorRecord& record,
        SceneBehaviorHandle handle,
        const SceneBehaviorPropertyChange& change)
    {
        if (!record.descriptor.enabled || record.pendingUnregister || !record.descriptor.onPropertyChanged) {
            ++diagnostics_.skippedCallbackCount;
            return SceneBehaviorResult::success();
        }
        if (!targetValid(record)) {
            ++diagnostics_.invalidTargetCount;
            ++diagnostics_.skippedCallbackCount;
            return SceneBehaviorResult{SceneBehaviorStatus::InvalidTarget, "behavior target is invalid"};
        }
        SceneBehaviorContext context = makeContext(record, handle);
        context.propertyChange = &change;
        return invokeCallback(record, handle, "property_changed", record.descriptor.onPropertyChanged, context);
    }

    SceneBehaviorResult SceneBehaviorHooks::invokeCallback(
        BehaviorRecord& record,
        SceneBehaviorHandle handle,
        const std::string& eventName,
        const std::function<SceneBehaviorResult(SceneBehaviorContext&)>& callback,
        SceneBehaviorContext& context)
    {
        const auto started = std::chrono::steady_clock::now();
        SceneBehaviorResult result = SceneBehaviorResult::success();
        try {
            result = callback(context);
        } catch (const std::exception& exception) {
            result.status = SceneBehaviorStatus::CallbackFailed;
            result.message = exception.what();
        } catch (...) {
            result.status = SceneBehaviorStatus::CallbackFailed;
            result.message = "unknown callback exception";
        }

        const auto finished = std::chrono::steady_clock::now();
        const uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
        diagnostics_.elapsedMicroseconds += elapsed;

        if (eventName == "tick") {
            ++diagnostics_.tickCallbackCount;
        } else if (eventName == "property_changed") {
            ++diagnostics_.propertyCallbackCount;
        } else {
            ++diagnostics_.lifecycleCallbackCount;
        }

        if (result.status != SceneBehaviorStatus::Success) {
            ++diagnostics_.failedCallbackCount;
            noteStatus(result.status, result.message);
            if (record.descriptor.disableOnFailure) {
                record.descriptor.enabled = false;
            }
        } else {
            noteStatus(SceneBehaviorStatus::Success, {});
        }

        debugRecords_.push_back({
            handle,
            record.descriptor.type,
            record.descriptor.targetKind,
            record.descriptor.target,
            eventName,
            result.status,
            result.message,
            elapsed,
        });
        return result;
    }

    SceneBehaviorContext SceneBehaviorHooks::makeContext(BehaviorRecord& record, SceneBehaviorHandle handle)
    {
        SceneBehaviorContext context;
        context.scene = &scene_;
        context.reflection = &reflection_;
        context.reflectionContext = &reflectionContext_;
        context.hooks = this;
        context.behavior = handle;
        context.behaviorType = record.descriptor.type;
        context.targetKind = record.descriptor.targetKind;
        context.target = record.descriptor.target;
        return context;
    }
}
