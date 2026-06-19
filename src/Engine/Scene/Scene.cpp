#include "Engine/Scene/Scene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Engine {
    namespace {
        bool isFinite(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!std::isfinite(matrix[column][row])) {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    SceneActorHandle Scene::createActor(SceneObjectId stableId)
    {
        for (uint32_t index = 0; index < actors_.size(); ++index) {
            ActorRecord& record = actors_[index];
            if (!record.occupied) {
                record.occupied = true;
                record.state = SceneActorState::Active;
                record.stableId = stableId;
                record.components.clear();
                resetTransformState(record);
                record.generation = nextGeneration(record.generation);
                return {index, record.generation};
            }
        }

        ActorRecord record;
        record.occupied = true;
        record.state = SceneActorState::Active;
        record.stableId = stableId;
        record.generation = nextGeneration(record.generation);
        resetTransformState(record);
        actors_.push_back(record);
        return {static_cast<uint32_t>(actors_.size() - 1), record.generation};
    }

    bool Scene::destroyActor(SceneActorHandle actor)
    {
        ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return false;
        }

        const std::optional<glm::mat4> refreshedWorld = refreshedWorldMatrix(actor);
        (void)refreshedWorld;
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

    bool Scene::hasTransform(SceneActorHandle actor) const
    {
        return activeActorRecord(actor) != nullptr;
    }

    bool Scene::setLocalTransform(SceneActorHandle actor, const SceneTransform& transform)
    {
        ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return false;
        }

        record->localTransform = transform;
        record->localMatrix = composeLocalMatrix(transform);
        markTransformDirtyRecursive(actor);
        return true;
    }

    std::optional<SceneTransform> Scene::localTransform(SceneActorHandle actor) const
    {
        const ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return std::nullopt;
        }
        return record->localTransform;
    }

    std::optional<glm::mat4> Scene::localMatrix(SceneActorHandle actor) const
    {
        const ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return std::nullopt;
        }
        return record->localMatrix;
    }

    std::optional<glm::mat4> Scene::worldMatrix(SceneActorHandle actor)
    {
        return refreshedWorldMatrix(actor);
    }

    std::optional<SceneActorHandle> Scene::parent(SceneActorHandle actor) const
    {
        const ActorRecord* record = activeActorRecord(actor);
        if (!record || !record->parent.has_value() || !activeActorRecord(*record->parent)) {
            return std::nullopt;
        }
        return record->parent;
    }

    std::vector<SceneActorHandle> Scene::children(SceneActorHandle actor) const
    {
        const ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return {};
        }

        std::vector<SceneActorHandle> result;
        result.reserve(record->children.size());
        for (SceneActorHandle child : record->children) {
            if (activeActorRecord(child)) {
                result.push_back(child);
            }
        }
        return result;
    }

    std::vector<SceneActorHandle> Scene::roots() const
    {
        std::vector<SceneActorHandle> result;
        for (uint32_t index = 0; index < actors_.size(); ++index) {
            const ActorRecord& record = actors_[index];
            if (!record.occupied || record.state != SceneActorState::Active) {
                continue;
            }
            if (!record.parent.has_value() || !activeActorRecord(*record.parent)) {
                result.push_back({index, record.generation});
            }
        }
        return result;
    }

    SceneTransformUpdateResult Scene::attachChild(
        SceneActorHandle child,
        SceneActorHandle parent,
        bool preserveWorldTransform)
    {
        ActorRecord* childRecord = activeActorRecord(child);
        if (!childRecord) {
            return SceneTransformUpdateResult::InvalidActor;
        }

        ActorRecord* parentRecord = activeActorRecord(parent);
        if (!parentRecord) {
            return SceneTransformUpdateResult::InvalidParent;
        }

        if (child == parent) {
            return SceneTransformUpdateResult::SelfParent;
        }

        if (wouldCreateCycle(child, parent)) {
            return SceneTransformUpdateResult::Cycle;
        }

        std::optional<SceneTransform> preservedLocalTransform;
        if (preserveWorldTransform) {
            const std::optional<glm::mat4> childWorld = refreshedWorldMatrix(child);
            const std::optional<glm::mat4> parentWorld = refreshedWorldMatrix(parent);
            if (!childWorld.has_value() || !parentWorld.has_value()) {
                return SceneTransformUpdateResult::InvalidActor;
            }

            const glm::mat4 newLocalMatrix = glm::inverse(*parentWorld) * *childWorld;
            preservedLocalTransform = decomposeTransform(newLocalMatrix);
            if (!preservedLocalTransform.has_value()) {
                return SceneTransformUpdateResult::NonDecomposableTransform;
            }
        }

        if (childRecord->parent.has_value()) {
            if (ActorRecord* oldParent = actorRecord(*childRecord->parent)) {
                removeChildReference(*oldParent, child);
            }
        }

        childRecord->parent = parent;
        parentRecord->children.push_back(child);

        if (preservedLocalTransform.has_value()) {
            childRecord->localTransform = *preservedLocalTransform;
            childRecord->localMatrix = composeLocalMatrix(*preservedLocalTransform);
        }
        markTransformDirtyRecursive(child);
        return SceneTransformUpdateResult::Success;
    }

    SceneTransformUpdateResult Scene::detachChild(SceneActorHandle child, bool preserveWorldTransform)
    {
        ActorRecord* childRecord = activeActorRecord(child);
        if (!childRecord) {
            return SceneTransformUpdateResult::InvalidActor;
        }

        std::optional<SceneTransform> preservedLocalTransform;
        if (preserveWorldTransform) {
            const std::optional<glm::mat4> childWorld = refreshedWorldMatrix(child);
            if (!childWorld.has_value()) {
                return SceneTransformUpdateResult::InvalidActor;
            }

            preservedLocalTransform = decomposeTransform(*childWorld);
            if (!preservedLocalTransform.has_value()) {
                return SceneTransformUpdateResult::NonDecomposableTransform;
            }
        }

        if (childRecord->parent.has_value()) {
            if (ActorRecord* oldParent = actorRecord(*childRecord->parent)) {
                removeChildReference(*oldParent, child);
            }
        }
        childRecord->parent = std::nullopt;

        if (preservedLocalTransform.has_value()) {
            childRecord->localTransform = *preservedLocalTransform;
            childRecord->localMatrix = composeLocalMatrix(*preservedLocalTransform);
        }
        markTransformDirtyRecursive(child);
        return SceneTransformUpdateResult::Success;
    }

    SceneTransformUpdateResult Scene::reparent(
        SceneActorHandle child,
        SceneActorHandle newParent,
        bool preserveWorldTransform)
    {
        return attachChild(child, newParent, preserveWorldTransform);
    }

    void Scene::markTransformDirty(SceneActorHandle actor)
    {
        if (!activeActorRecord(actor)) {
            return;
        }
        markTransformDirtyRecursive(actor);
    }

    void Scene::updateWorldTransforms()
    {
        for (SceneActorHandle root : roots()) {
            updateWorldTransformRecursive(root, glm::mat4{1.0f});
        }
    }

    SceneSystemHandle Scene::registerSystem(SceneSystemDescriptor descriptor)
    {
        if (!canMutateSystems()) {
            return {};
        }

        for (uint32_t index = 0; index < systems_.size(); ++index) {
            SystemRecord& record = systems_[index];
            if (!record.occupied) {
                record.occupied = true;
                record.descriptor = std::move(descriptor);
                record.generation = nextGeneration(record.generation);
                refreshSchedulerDiagnostics();
                return {index, record.generation};
            }
        }

        SystemRecord record;
        record.occupied = true;
        record.descriptor = std::move(descriptor);
        record.generation = nextGeneration(record.generation);
        systems_.push_back(std::move(record));
        refreshSchedulerDiagnostics();
        return {static_cast<uint32_t>(systems_.size() - 1), systems_.back().generation};
    }

    bool Scene::unregisterSystem(SceneSystemHandle system)
    {
        if (!canMutateSystems()) {
            return false;
        }

        SystemRecord* record = systemRecord(system);
        if (!record) {
            return false;
        }

        freeSystemSlot(system.index);
        refreshSchedulerDiagnostics();
        return true;
    }

    bool Scene::setSystemEnabled(SceneSystemHandle system, bool enabled)
    {
        SystemRecord* record = systemRecord(system);
        if (!record) {
            return false;
        }

        record->descriptor.enabled = enabled;
        refreshSchedulerDiagnostics();
        return true;
    }

    bool Scene::contains(SceneSystemHandle system) const
    {
        return systemRecord(system) != nullptr;
    }

    SceneLifecycleState Scene::lifecycleState() const
    {
        return lifecycleState_;
    }

    bool Scene::load()
    {
        if (lifecycleState_ != SceneLifecycleState::Unloaded) {
            return false;
        }

        for (SystemRecord& record : systems_) {
            if (!record.occupied || !record.descriptor.onLoad) {
                continue;
            }
            if (!record.descriptor.enabled) {
                ++schedulerDiagnostics_.skippedCallbackCount;
                continue;
            }
            if (record.descriptor.enabled && record.descriptor.onLoad) {
                record.descriptor.onLoad(*this);
            }
        }
        lifecycleState_ = SceneLifecycleState::Loaded;
        refreshSchedulerDiagnostics();
        return true;
    }

    bool Scene::start()
    {
        if (lifecycleState_ != SceneLifecycleState::Loaded) {
            return false;
        }

        for (SystemRecord& record : systems_) {
            if (!record.occupied || !record.descriptor.onStart) {
                continue;
            }
            if (!record.descriptor.enabled) {
                ++schedulerDiagnostics_.skippedCallbackCount;
                continue;
            }
            if (record.descriptor.enabled && record.descriptor.onStart) {
                record.descriptor.onStart(*this);
            }
        }
        lifecycleState_ = SceneLifecycleState::Started;
        refreshSchedulerDiagnostics();
        return true;
    }

    bool Scene::stop()
    {
        if (lifecycleState_ != SceneLifecycleState::Started) {
            return false;
        }

        lifecycleState_ = SceneLifecycleState::Stopping;
        refreshSchedulerDiagnostics();
        for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
            SystemRecord& record = *it;
            if (!record.occupied || !record.descriptor.onStop) {
                continue;
            }
            if (!record.descriptor.enabled) {
                ++schedulerDiagnostics_.skippedCallbackCount;
                continue;
            }
            if (record.descriptor.enabled && record.descriptor.onStop) {
                record.descriptor.onStop(*this);
            }
        }
        lifecycleState_ = SceneLifecycleState::Loaded;
        refreshSchedulerDiagnostics();
        return true;
    }

    bool Scene::unload()
    {
        if (lifecycleState_ != SceneLifecycleState::Loaded) {
            return false;
        }

        for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
            SystemRecord& record = *it;
            if (!record.occupied || !record.descriptor.onUnload) {
                continue;
            }
            if (!record.descriptor.enabled) {
                ++schedulerDiagnostics_.skippedCallbackCount;
                continue;
            }
            if (record.descriptor.enabled && record.descriptor.onUnload) {
                record.descriptor.onUnload(*this);
            }
        }
        lifecycleState_ = SceneLifecycleState::Unloaded;
        refreshSchedulerDiagnostics();
        return true;
    }

    void Scene::tickFrame(float deltaSeconds)
    {
        ++frameIndex_;
        resetFrameDiagnostics();
        runPhase(SceneTickPhase::BeginFrame, deltaSeconds, false);
        runPhase(SceneTickPhase::VariableAnimation, deltaSeconds, false);
        runPhase(SceneTickPhase::VariableUpdate, deltaSeconds, false);
        runPhase(SceneTickPhase::PreRender, deltaSeconds, false);
        runPhase(SceneTickPhase::EndFrame, deltaSeconds, false);
        refreshSchedulerDiagnostics();
    }

    void Scene::tickFixed(float fixedDeltaSeconds)
    {
        ++fixedStepIndex_;
        runPhase(SceneTickPhase::FixedPrePhysics, fixedDeltaSeconds, true);
        runPhase(SceneTickPhase::FixedPhysics, fixedDeltaSeconds, true);
        runPhase(SceneTickPhase::FixedPostPhysics, fixedDeltaSeconds, true);
        refreshSchedulerDiagnostics();
    }

    void Scene::tickPhase(SceneTickPhase phase, float deltaSeconds)
    {
        runPhase(phase, deltaSeconds, isFixedPhase(phase));
        refreshSchedulerDiagnostics();
    }

    void Scene::setPaused(bool paused)
    {
        paused_ = paused;
        refreshSchedulerDiagnostics();
    }

    bool Scene::paused() const
    {
        return paused_;
    }

    SceneSchedulerDiagnostics Scene::schedulerDiagnostics() const
    {
        return schedulerDiagnostics_;
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

    Scene::SystemRecord* Scene::systemRecord(SceneSystemHandle system)
    {
        if (!isValid(system) || system.index >= systems_.size()) {
            return nullptr;
        }

        SystemRecord& record = systems_[system.index];
        if (!record.occupied || record.generation != system.generation) {
            return nullptr;
        }
        return &record;
    }

    const Scene::SystemRecord* Scene::systemRecord(SceneSystemHandle system) const
    {
        if (!isValid(system) || system.index >= systems_.size()) {
            return nullptr;
        }

        const SystemRecord& record = systems_[system.index];
        if (!record.occupied || record.generation != system.generation) {
            return nullptr;
        }
        return &record;
    }

    glm::mat4 Scene::composeLocalMatrix(const SceneTransform& transform) const
    {
        glm::mat4 matrix{1.0f};
        matrix = glm::translate(matrix, transform.translation);
        matrix *= glm::mat4_cast(glm::normalize(transform.rotation));
        matrix = glm::scale(matrix, transform.scale);
        return matrix;
    }

    std::optional<SceneTransform> Scene::decomposeTransform(const glm::mat4& matrix) const
    {
        if (!isFinite(matrix)) {
            return std::nullopt;
        }

        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
            return std::nullopt;
        }

        if (!std::isfinite(translation.x) || !std::isfinite(translation.y) || !std::isfinite(translation.z)
            || !std::isfinite(rotation.w) || !std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z)
            || !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)) {
            return std::nullopt;
        }

        if (glm::length(rotation) == 0.0f) {
            return std::nullopt;
        }

        return SceneTransform{
            translation,
            glm::normalize(rotation),
            scale,
        };
    }

    bool Scene::wouldCreateCycle(SceneActorHandle child, SceneActorHandle parent) const
    {
        SceneActorHandle current = parent;
        while (const ActorRecord* record = activeActorRecord(current)) {
            if (current == child) {
                return true;
            }
            if (!record->parent.has_value()) {
                return false;
            }
            current = *record->parent;
        }
        return false;
    }

    std::optional<glm::mat4> Scene::refreshedWorldMatrix(SceneActorHandle actor)
    {
        ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return std::nullopt;
        }

        glm::mat4 parentWorld{1.0f};
        if (record->parent.has_value()) {
            const std::optional<glm::mat4> resolvedParentWorld = refreshedWorldMatrix(*record->parent);
            if (resolvedParentWorld.has_value()) {
                parentWorld = *resolvedParentWorld;
            }
        }

        if (record->transformDirty) {
            record->worldMatrix = parentWorld * record->localMatrix;
            record->transformDirty = false;
        }
        return record->worldMatrix;
    }

    void Scene::updateWorldTransformRecursive(SceneActorHandle actor, const glm::mat4& parentWorld)
    {
        ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return;
        }

        if (record->transformDirty) {
            record->worldMatrix = parentWorld * record->localMatrix;
            record->transformDirty = false;
        }

        const glm::mat4 world = record->worldMatrix;
        const std::vector<SceneActorHandle> childHandles = record->children;
        for (SceneActorHandle child : childHandles) {
            updateWorldTransformRecursive(child, world);
        }
    }

    void Scene::markTransformDirtyRecursive(SceneActorHandle actor)
    {
        ActorRecord* record = activeActorRecord(actor);
        if (!record) {
            return;
        }

        record->transformDirty = true;
        const std::vector<SceneActorHandle> childHandles = record->children;
        for (SceneActorHandle child : childHandles) {
            markTransformDirtyRecursive(child);
        }
    }

    void Scene::removeChildReference(ActorRecord& parent, SceneActorHandle child)
    {
        parent.children.erase(
            std::remove(parent.children.begin(), parent.children.end(), child),
            parent.children.end());
    }

    void Scene::resetTransformState(ActorRecord& record)
    {
        record.localTransform = {};
        record.localMatrix = glm::mat4{1.0f};
        record.worldMatrix = glm::mat4{1.0f};
        record.transformDirty = true;
        record.parent = std::nullopt;
        record.children.clear();
    }

    void Scene::detachSurvivingChildrenToRoots(ActorRecord& record, SceneActorHandle actor)
    {
        const std::vector<SceneActorHandle> childHandles = record.children;
        for (SceneActorHandle child : childHandles) {
            ActorRecord* childRecord = activeActorRecord(child);
            if (!childRecord) {
                continue;
            }
            if (!childRecord->parent.has_value() || *childRecord->parent != actor) {
                continue;
            }

            const glm::mat4 childWorld = record.worldMatrix * childRecord->localMatrix;
            if (const std::optional<SceneTransform> localAsWorld = decomposeTransform(childWorld)) {
                childRecord->localTransform = *localAsWorld;
                childRecord->localMatrix = composeLocalMatrix(*localAsWorld);
            }
            childRecord->parent = std::nullopt;
            markTransformDirtyRecursive(child);
        }
        record.children.clear();
    }

    void Scene::freeActorSlot(uint32_t index)
    {
        ActorRecord& record = actors_[index];
        detachSurvivingChildrenToRoots(record, {index, record.generation});
        if (record.parent.has_value()) {
            if (ActorRecord* parent = activeActorRecord(*record.parent)) {
                removeChildReference(*parent, {index, record.generation});
            }
        }

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
        resetTransformState(record);
    }

    void Scene::freeComponentSlot(uint32_t index)
    {
        ComponentRecord& record = components_[index];
        record.occupied = false;
        record.owner = {};
        record.type = {};
    }

    void Scene::freeSystemSlot(uint32_t index)
    {
        SystemRecord& record = systems_[index];
        record.occupied = false;
        record.descriptor = {};
    }

    uint32_t Scene::nextGeneration(uint32_t generation) const
    {
        if (generation == std::numeric_limits<uint32_t>::max()) {
            return 1;
        }
        return generation + 1;
    }

    bool Scene::canMutateSystems() const
    {
        return lifecycleState_ == SceneLifecycleState::Unloaded || lifecycleState_ == SceneLifecycleState::Loaded;
    }

    uint32_t Scene::phaseIndex(SceneTickPhase phase) const
    {
        return static_cast<uint32_t>(phase);
    }

    bool Scene::isFixedPhase(SceneTickPhase phase) const
    {
        return phase == SceneTickPhase::FixedPrePhysics
            || phase == SceneTickPhase::FixedPhysics
            || phase == SceneTickPhase::FixedPostPhysics;
    }

    bool Scene::phasePaused(SceneTickPhase phase) const
    {
        return paused_ && (isFixedPhase(phase) || phase == SceneTickPhase::VariableUpdate);
    }

    bool Scene::systemWantsPhase(const SystemRecord& record, SceneTickPhase phase) const
    {
        return std::find(record.descriptor.phases.begin(), record.descriptor.phases.end(), phase)
            != record.descriptor.phases.end();
    }

    std::vector<SceneSystemHandle> Scene::systemsForPhase(SceneTickPhase phase) const
    {
        std::vector<SceneSystemHandle> result;
        for (uint32_t index = 0; index < systems_.size(); ++index) {
            const SystemRecord& record = systems_[index];
            if (record.occupied && systemWantsPhase(record, phase)) {
                result.push_back({index, record.generation});
            }
        }
        return result;
    }

    void Scene::runPhase(SceneTickPhase phase, float deltaSeconds, bool fixedStep)
    {
        const uint32_t index = phaseIndex(phase);
        if (index >= phaseIndex(SceneTickPhase::Count)) {
            ++schedulerDiagnostics_.skippedPhaseCount;
            refreshSchedulerDiagnostics();
            return;
        }

        if (lifecycleState_ != SceneLifecycleState::Started || phasePaused(phase)) {
            ++schedulerDiagnostics_.skippedPhaseCount;
            refreshSchedulerDiagnostics();
            return;
        }

        if (phase == SceneTickPhase::PreRender) {
            updateWorldTransforms();
        }

        const auto started = std::chrono::steady_clock::now();
        schedulerDiagnostics_.lastPhase = phase;
        const std::vector<SceneSystemHandle> phaseSystems = systemsForPhase(phase);
        SceneTickContext context;
        context.phase = phase;
        context.deltaSeconds = deltaSeconds;
        context.frameIndex = frameIndex_;
        context.fixedStepIndex = fixedStepIndex_;
        context.fixedStep = fixedStep;
        context.paused = paused_;

        for (SceneSystemHandle handle : phaseSystems) {
            SystemRecord* record = systemRecord(handle);
            if (!record) {
                ++schedulerDiagnostics_.skippedCallbackCount;
                continue;
            }
            if (!record->descriptor.enabled || !record->descriptor.onTick) {
                ++schedulerDiagnostics_.skippedCallbackCount;
                continue;
            }
            record->descriptor.onTick(*this, context);
            ++schedulerDiagnostics_.phases[index].callbackCount;
        }

        const auto finished = std::chrono::steady_clock::now();
        schedulerDiagnostics_.phases[index].elapsedMicroseconds =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
        refreshSchedulerDiagnostics();
    }

    void Scene::resetFrameDiagnostics()
    {
        schedulerDiagnostics_.lastPhase = std::nullopt;
        schedulerDiagnostics_.skippedPhaseCount = 0;
        schedulerDiagnostics_.skippedCallbackCount = 0;
        if (schedulerDiagnostics_.phases.size() != phaseIndex(SceneTickPhase::Count)) {
            schedulerDiagnostics_.phases.assign(phaseIndex(SceneTickPhase::Count), {});
        } else {
            for (ScenePhaseDiagnostics& phase : schedulerDiagnostics_.phases) {
                phase = {};
            }
        }
        refreshSchedulerDiagnostics();
    }

    void Scene::refreshSchedulerDiagnostics()
    {
        schedulerDiagnostics_.lifecycleState = lifecycleState_;
        schedulerDiagnostics_.paused = paused_;
        schedulerDiagnostics_.frameIndex = frameIndex_;
        schedulerDiagnostics_.fixedStepIndex = fixedStepIndex_;
        if (schedulerDiagnostics_.phases.size() != phaseIndex(SceneTickPhase::Count)) {
            schedulerDiagnostics_.phases.assign(phaseIndex(SceneTickPhase::Count), {});
        }

        uint32_t registeredCount = 0;
        uint32_t enabledCount = 0;
        for (const SystemRecord& record : systems_) {
            if (!record.occupied) {
                continue;
            }
            ++registeredCount;
            if (record.descriptor.enabled) {
                ++enabledCount;
            }
        }
        schedulerDiagnostics_.registeredSystemCount = registeredCount;
        schedulerDiagnostics_.enabledSystemCount = enabledCount;
    }
}
