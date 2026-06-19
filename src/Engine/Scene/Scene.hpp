#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace Engine {
    struct SceneActorHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneComponentHandle {
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

    [[nodiscard]] constexpr bool isValid(SceneActorHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneComponentHandle handle)
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

        void forEachActor(const std::function<void(SceneActorHandle)>& callback) const;

    private:
        struct ActorRecord {
            uint32_t generation = 0;
            SceneObjectId stableId;
            SceneActorState state = SceneActorState::PendingDestroy;
            bool occupied = false;
            std::vector<SceneComponentHandle> components;
        };

        struct ComponentRecord {
            uint32_t generation = 0;
            SceneActorHandle owner;
            SceneComponentTypeId type;
            bool occupied = false;
        };

        [[nodiscard]] ActorRecord* activeActorRecord(SceneActorHandle actor);
        [[nodiscard]] const ActorRecord* activeActorRecord(SceneActorHandle actor) const;
        [[nodiscard]] ActorRecord* actorRecord(SceneActorHandle actor);
        [[nodiscard]] const ActorRecord* actorRecord(SceneActorHandle actor) const;
        [[nodiscard]] ComponentRecord* componentRecord(SceneComponentHandle component);
        [[nodiscard]] const ComponentRecord* componentRecord(SceneComponentHandle component) const;

        void freeActorSlot(uint32_t index);
        void freeComponentSlot(uint32_t index);
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;

        std::vector<ActorRecord> actors_;
        std::vector<ComponentRecord> components_;
    };
}
