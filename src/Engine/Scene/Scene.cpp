#include "Engine/Scene/Scene.hpp"

#include <algorithm>
#include <limits>

namespace Engine {
    SceneActorHandle Scene::createActor(SceneObjectId stableId)
    {
        for (uint32_t index = 0; index < actors_.size(); ++index) {
            ActorRecord& record = actors_[index];
            if (!record.occupied) {
                record.occupied = true;
                record.state = SceneActorState::Active;
                record.stableId = stableId;
                record.components.clear();
                record.generation = nextGeneration(record.generation);
                return {index, record.generation};
            }
        }

        ActorRecord record;
        record.occupied = true;
        record.state = SceneActorState::Active;
        record.stableId = stableId;
        record.generation = nextGeneration(record.generation);
        actors_.push_back(record);
        return {static_cast<uint32_t>(actors_.size() - 1), record.generation};
    }

    bool Scene::destroyActor(SceneActorHandle actor)
    {
        ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return false;
        }

        record->state = SceneActorState::PendingDestroy;
        return true;
    }

    bool Scene::flushDestroyedActors()
    {
        bool changed = false;
        for (uint32_t index = 0; index < actors_.size(); ++index) {
            const ActorRecord& record = actors_[index];
            if (record.occupied && record.state == SceneActorState::PendingDestroy) {
                freeActorSlot(index);
                changed = true;
            }
        }
        return changed;
    }

    bool Scene::contains(SceneActorHandle actor) const
    {
        return activeActorRecord(actor) != nullptr;
    }

    std::optional<SceneObjectId> Scene::stableId(SceneActorHandle actor) const
    {
        const ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return std::nullopt;
        }
        return record->stableId;
    }

    std::optional<SceneActorState> Scene::actorState(SceneActorHandle actor) const
    {
        const ActorRecord* record = actorRecord(actor);
        if (!record) {
            return std::nullopt;
        }
        return record->state;
    }

    SceneComponentHandle Scene::attachComponent(SceneActorHandle actor, SceneComponentTypeId type)
    {
        ActorRecord* actorRecord = activeActorRecord(actor);
        if (!actorRecord || !isValid(type)) {
            return {};
        }

        for (uint32_t index = 0; index < components_.size(); ++index) {
            ComponentRecord& record = components_[index];
            if (!record.occupied) {
                record.occupied = true;
                record.owner = actor;
                record.type = type;
                record.generation = nextGeneration(record.generation);
                const SceneComponentHandle handle{index, record.generation};
                actorRecord->components.push_back(handle);
                return handle;
            }
        }

        ComponentRecord record;
        record.occupied = true;
        record.owner = actor;
        record.type = type;
        record.generation = nextGeneration(record.generation);
        components_.push_back(record);
        const SceneComponentHandle handle{static_cast<uint32_t>(components_.size() - 1), record.generation};
        actorRecord->components.push_back(handle);
        return handle;
    }

    bool Scene::detachComponent(SceneComponentHandle component)
    {
        ComponentRecord* record = componentRecord(component);
        if (!record) {
            return false;
        }

        const SceneActorHandle owner = record->owner;
        if (ActorRecord* actor = actorRecord(owner)) {
            auto& actorComponents = actor->components;
            actorComponents.erase(
                std::remove(actorComponents.begin(), actorComponents.end(), component),
                actorComponents.end());
        }

        freeComponentSlot(component.index);
        return true;
    }

    bool Scene::contains(SceneComponentHandle component) const
    {
        return componentRecord(component) != nullptr;
    }

    std::optional<SceneActorHandle> Scene::componentOwner(SceneComponentHandle component) const
    {
        const ComponentRecord* record = componentRecord(component);
        if (!record) {
            return std::nullopt;
        }
        return record->owner;
    }

    std::optional<SceneComponentTypeId> Scene::componentType(SceneComponentHandle component) const
    {
        const ComponentRecord* record = componentRecord(component);
        if (!record) {
            return std::nullopt;
        }
        return record->type;
    }

    std::vector<SceneComponentHandle> Scene::components(SceneActorHandle actor) const
    {
        const ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return {};
        }

        std::vector<SceneComponentHandle> result;
        result.reserve(record->components.size());
        for (SceneComponentHandle component : record->components) {
            if (contains(component)) {
                result.push_back(component);
            }
        }
        return result;
    }

    std::optional<SceneComponentHandle> Scene::firstComponent(
        SceneActorHandle actor,
        SceneComponentTypeId type) const
    {
        if (!isValid(type)) {
            return std::nullopt;
        }

        const ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return std::nullopt;
        }

        for (SceneComponentHandle component : record->components) {
            const ComponentRecord* componentRecord = this->componentRecord(component);
            if (componentRecord && componentRecord->type == type) {
                return component;
            }
        }
        return std::nullopt;
    }

    void Scene::forEachActor(const std::function<void(SceneActorHandle)>& callback) const
    {
        for (uint32_t index = 0; index < actors_.size(); ++index) {
            const ActorRecord& record = actors_[index];
            if (record.occupied && record.state == SceneActorState::Active) {
                callback({index, record.generation});
            }
        }
    }

    Scene::ActorRecord* Scene::activeActorRecord(SceneActorHandle actor)
    {
        ActorRecord* record = actorRecord(actor);
        if (!record || record->state != SceneActorState::Active) {
            return nullptr;
        }
        return record;
    }

    const Scene::ActorRecord* Scene::activeActorRecord(SceneActorHandle actor) const
    {
        const ActorRecord* record = actorRecord(actor);
        if (!record || record->state != SceneActorState::Active) {
            return nullptr;
        }
        return record;
    }

    Scene::ActorRecord* Scene::actorRecord(SceneActorHandle actor)
    {
        if (!isValid(actor) || actor.index >= actors_.size()) {
            return nullptr;
        }

        ActorRecord& record = actors_[actor.index];
        if (!record.occupied || record.generation != actor.generation) {
            return nullptr;
        }
        return &record;
    }

    const Scene::ActorRecord* Scene::actorRecord(SceneActorHandle actor) const
    {
        if (!isValid(actor) || actor.index >= actors_.size()) {
            return nullptr;
        }

        const ActorRecord& record = actors_[actor.index];
        if (!record.occupied || record.generation != actor.generation) {
            return nullptr;
        }
        return &record;
    }

    Scene::ComponentRecord* Scene::componentRecord(SceneComponentHandle component)
    {
        if (!isValid(component) || component.index >= components_.size()) {
            return nullptr;
        }

        ComponentRecord& record = components_[component.index];
        if (!record.occupied || record.generation != component.generation) {
            return nullptr;
        }
        return &record;
    }

    const Scene::ComponentRecord* Scene::componentRecord(SceneComponentHandle component) const
    {
        if (!isValid(component) || component.index >= components_.size()) {
            return nullptr;
        }

        const ComponentRecord& record = components_[component.index];
        if (!record.occupied || record.generation != component.generation) {
            return nullptr;
        }
        return &record;
    }

    void Scene::freeActorSlot(uint32_t index)
    {
        ActorRecord& record = actors_[index];
        const std::vector<SceneComponentHandle> attachedComponents = record.components;
        for (SceneComponentHandle component : attachedComponents) {
            if (component.index < components_.size()) {
                const ComponentRecord& componentRecord = components_[component.index];
                if (componentRecord.occupied && componentRecord.generation == component.generation) {
                    freeComponentSlot(component.index);
                }
            }
        }

        record.occupied = false;
        record.state = SceneActorState::PendingDestroy;
        record.stableId = {};
        record.components.clear();
    }

    void Scene::freeComponentSlot(uint32_t index)
    {
        ComponentRecord& record = components_[index];
        record.occupied = false;
        record.owner = {};
        record.type = {};
    }

    uint32_t Scene::nextGeneration(uint32_t generation) const
    {
        if (generation == std::numeric_limits<uint32_t>::max()) {
            return 1;
        }
        return generation + 1;
    }
}
