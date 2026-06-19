#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {
    class Scene;

    struct SceneActorHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneComponentHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneSystemHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneObjectId {
        uint64_t value = 0;
    };

    struct SceneComponentTypeId {
        uint32_t value = 0;
    };

    enum class SceneActorState {
        Active,
        PendingDestroy,
    };

    struct SceneTransform {
        glm::vec3 translation{0.0f, 0.0f, 0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
    };

    enum class SceneTransformUpdateResult {
        Success,
        InvalidActor,
        InvalidParent,
        SelfParent,
        Cycle,
        NonDecomposableTransform,
    };

    enum class SceneLifecycleState {
        Unloaded,
        Loaded,
        Started,
        Stopping,
    };

    enum class SceneTickPhase {
        BeginFrame,
        FixedPrePhysics,
        FixedPhysics,
        FixedPostPhysics,
        VariableAnimation,
        VariableUpdate,
        PreRender,
        EndFrame,
        Count,
    };

    struct SceneTickContext {
        SceneTickPhase phase = SceneTickPhase::BeginFrame;
        float deltaSeconds = 0.0f;
        uint64_t frameIndex = 0;
        uint32_t fixedStepIndex = 0;
        bool fixedStep = false;
        bool paused = false;
    };

    struct SceneSystemDescriptor {
        std::string name;
        std::vector<SceneTickPhase> phases;
        bool enabled = true;
        std::function<void(Scene&)> onLoad;
        std::function<void(Scene&)> onStart;
        std::function<void(Scene&)> onStop;
        std::function<void(Scene&)> onUnload;
        std::function<void(Scene&, const SceneTickContext&)> onTick;
    };

    struct ScenePhaseDiagnostics {
        uint32_t callbackCount = 0;
        uint64_t elapsedMicroseconds = 0;
    };

    struct SceneSchedulerDiagnostics {
        SceneLifecycleState lifecycleState = SceneLifecycleState::Unloaded;
        bool paused = false;
        uint64_t frameIndex = 0;
        uint32_t fixedStepIndex = 0;
        uint32_t registeredSystemCount = 0;
        uint32_t enabledSystemCount = 0;
        std::optional<SceneTickPhase> lastPhase;
        uint32_t skippedPhaseCount = 0;
        uint32_t skippedCallbackCount = 0;
        std::vector<ScenePhaseDiagnostics> phases;
    };

    [[nodiscard]] constexpr bool isValid(SceneActorHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneComponentHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneSystemHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneObjectId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneComponentTypeId type)
    {
        return type.value != 0;
    }

    [[nodiscard]] constexpr bool operator==(SceneActorHandle lhs, SceneActorHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneActorHandle lhs, SceneActorHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneComponentHandle lhs, SceneComponentHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneComponentHandle lhs, SceneComponentHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneSystemHandle lhs, SceneSystemHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneSystemHandle lhs, SceneSystemHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneObjectId lhs, SceneObjectId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(SceneObjectId lhs, SceneObjectId rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneComponentTypeId lhs, SceneComponentTypeId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(SceneComponentTypeId lhs, SceneComponentTypeId rhs)
    {
        return !(lhs == rhs);
    }

    class Scene {
    public:
        [[nodiscard]] SceneActorHandle createActor(SceneObjectId stableId = {});
        bool destroyActor(SceneActorHandle actor);
        bool flushDestroyedActors();

        [[nodiscard]] bool contains(SceneActorHandle actor) const;
        [[nodiscard]] std::optional<SceneObjectId> stableId(SceneActorHandle actor) const;
        [[nodiscard]] std::optional<SceneActorState> actorState(SceneActorHandle actor) const;

        [[nodiscard]] SceneComponentHandle attachComponent(SceneActorHandle actor, SceneComponentTypeId type);
        bool detachComponent(SceneComponentHandle component);
        [[nodiscard]] bool contains(SceneComponentHandle component) const;
        [[nodiscard]] std::optional<SceneActorHandle> componentOwner(SceneComponentHandle component) const;
        [[nodiscard]] std::optional<SceneComponentTypeId> componentType(SceneComponentHandle component) const;

        [[nodiscard]] std::vector<SceneComponentHandle> components(SceneActorHandle actor) const;
        [[nodiscard]] std::optional<SceneComponentHandle> firstComponent(
            SceneActorHandle actor,
            SceneComponentTypeId type) const;

        [[nodiscard]] bool hasTransform(SceneActorHandle actor) const;
        bool setLocalTransform(SceneActorHandle actor, const SceneTransform& transform);
        [[nodiscard]] std::optional<SceneTransform> localTransform(SceneActorHandle actor) const;
        [[nodiscard]] std::optional<glm::mat4> localMatrix(SceneActorHandle actor) const;
        [[nodiscard]] std::optional<glm::mat4> worldMatrix(SceneActorHandle actor);

        [[nodiscard]] std::optional<SceneActorHandle> parent(SceneActorHandle actor) const;
        [[nodiscard]] std::vector<SceneActorHandle> children(SceneActorHandle actor) const;
        [[nodiscard]] std::vector<SceneActorHandle> roots() const;

        SceneTransformUpdateResult attachChild(
            SceneActorHandle child,
            SceneActorHandle parent,
            bool preserveWorldTransform = true);
        SceneTransformUpdateResult detachChild(
            SceneActorHandle child,
            bool preserveWorldTransform = true);
        SceneTransformUpdateResult reparent(
            SceneActorHandle child,
            SceneActorHandle newParent,
            bool preserveWorldTransform = true);

        void markTransformDirty(SceneActorHandle actor);
        void updateWorldTransforms();

        [[nodiscard]] SceneSystemHandle registerSystem(SceneSystemDescriptor descriptor);
        bool unregisterSystem(SceneSystemHandle system);
        bool setSystemEnabled(SceneSystemHandle system, bool enabled);
        [[nodiscard]] bool contains(SceneSystemHandle system) const;

        [[nodiscard]] SceneLifecycleState lifecycleState() const;
        bool load();
        bool start();
        bool stop();
        bool unload();

        void tickFrame(float deltaSeconds);
        void tickFixed(float fixedDeltaSeconds);
        void tickPhase(SceneTickPhase phase, float deltaSeconds);
        void setPaused(bool paused);
        [[nodiscard]] bool paused() const;

        [[nodiscard]] SceneSchedulerDiagnostics schedulerDiagnostics() const;

        void forEachActor(const std::function<void(SceneActorHandle)>& callback) const;

    private:
        struct ActorRecord {
            uint32_t generation = 0;
            SceneObjectId stableId;
            SceneActorState state = SceneActorState::PendingDestroy;
            bool occupied = false;
            std::vector<SceneComponentHandle> components;
            SceneTransform localTransform;
            glm::mat4 localMatrix{1.0f};
            glm::mat4 worldMatrix{1.0f};
            bool transformDirty = true;
            std::optional<SceneActorHandle> parent;
            std::vector<SceneActorHandle> children;
        };

        struct ComponentRecord {
            uint32_t generation = 0;
            SceneActorHandle owner;
            SceneComponentTypeId type;
            bool occupied = false;
        };

        struct SystemRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneSystemDescriptor descriptor;
        };

        [[nodiscard]] ActorRecord* activeActorRecord(SceneActorHandle actor);
        [[nodiscard]] const ActorRecord* activeActorRecord(SceneActorHandle actor) const;
        [[nodiscard]] ActorRecord* actorRecord(SceneActorHandle actor);
        [[nodiscard]] const ActorRecord* actorRecord(SceneActorHandle actor) const;
        [[nodiscard]] ComponentRecord* componentRecord(SceneComponentHandle component);
        [[nodiscard]] const ComponentRecord* componentRecord(SceneComponentHandle component) const;
        [[nodiscard]] SystemRecord* systemRecord(SceneSystemHandle system);
        [[nodiscard]] const SystemRecord* systemRecord(SceneSystemHandle system) const;

        [[nodiscard]] glm::mat4 composeLocalMatrix(const SceneTransform& transform) const;
        [[nodiscard]] std::optional<SceneTransform> decomposeTransform(const glm::mat4& matrix) const;
        [[nodiscard]] bool wouldCreateCycle(SceneActorHandle child, SceneActorHandle parent) const;
        [[nodiscard]] std::optional<glm::mat4> refreshedWorldMatrix(SceneActorHandle actor);
        void updateWorldTransformRecursive(SceneActorHandle actor, const glm::mat4& parentWorld);
        void markTransformDirtyRecursive(SceneActorHandle actor);
        void removeChildReference(ActorRecord& parent, SceneActorHandle child);
        void resetTransformState(ActorRecord& record);
        void detachSurvivingChildrenToRoots(ActorRecord& record, SceneActorHandle actor);

        void freeActorSlot(uint32_t index);
        void freeComponentSlot(uint32_t index);
        void freeSystemSlot(uint32_t index);
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;

        [[nodiscard]] bool canMutateSystems() const;
        [[nodiscard]] uint32_t phaseIndex(SceneTickPhase phase) const;
        [[nodiscard]] bool isFixedPhase(SceneTickPhase phase) const;
        [[nodiscard]] bool phasePaused(SceneTickPhase phase) const;
        [[nodiscard]] bool systemWantsPhase(const SystemRecord& record, SceneTickPhase phase) const;
        [[nodiscard]] std::vector<SceneSystemHandle> systemsForPhase(SceneTickPhase phase) const;
        void runPhase(SceneTickPhase phase, float deltaSeconds, bool fixedStep);
        void resetFrameDiagnostics();
        void refreshSchedulerDiagnostics();

        std::vector<ActorRecord> actors_;
        std::vector<ComponentRecord> components_;
        std::vector<SystemRecord> systems_;
        SceneLifecycleState lifecycleState_ = SceneLifecycleState::Unloaded;
        bool paused_ = false;
        uint64_t frameIndex_ = 0;
        uint32_t fixedStepIndex_ = 0;
        SceneSchedulerDiagnostics schedulerDiagnostics_;
    };
}
