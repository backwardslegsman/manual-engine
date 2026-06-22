#include "Engine/ActorGameplayComponents.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Engine {
    namespace {
        constexpr ReflectedPropertyFlag PayloadFlags =
            ReflectedPropertyFlag::EditorVisible |
            ReflectedPropertyFlag::ScriptVisible |
            ReflectedPropertyFlag::Serializable;
        constexpr ReflectedPropertyFlag StableReadOnlyFlags =
            PayloadFlags |
            ReflectedPropertyFlag::ReadOnly |
            ReflectedPropertyFlag::StableReference;

        [[nodiscard]] bool finitePositive(float value)
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] bool finiteNonNegative(float value)
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        void error(ActorGameplayComponentValidationResult& result, std::string message)
        {
            result.valid = false;
            result.errors.push_back(std::move(message));
        }

        void merge(ActorGameplayComponentValidationResult& result, const ActorGameplayComponentValidationResult& other)
        {
            if (!other.valid) {
                result.valid = false;
            }
            result.errors.insert(result.errors.end(), other.errors.begin(), other.errors.end());
            result.warnings.insert(result.warnings.end(), other.warnings.begin(), other.warnings.end());
        }

        [[nodiscard]] SceneActorHandle findActorByStableId(const Scene& scene, SceneObjectId actorId)
        {
            SceneActorHandle found;
            scene.forEachActor([&](SceneActorHandle actor) {
                if (!isValid(found) && scene.stableId(actor).value_or(SceneObjectId{}) == actorId) {
                    found = actor;
                }
            });
            return found;
        }

        [[nodiscard]] ActorComponentInstanceRecord findRequiredMetadata(
            const ActorComponentDescriptorStore& store,
            ActorComponentId componentId)
        {
            return store.record(componentId).value_or(ActorComponentInstanceRecord{});
        }

        [[nodiscard]] ReflectionResult reflectionResult(ReflectionStatus status, std::string message = {})
        {
            ReflectionResult result;
            result.status = status;
            result.message = std::move(message);
            return result;
        }

        [[nodiscard]] ReflectionResult valueResult(ReflectedValue value)
        {
            ReflectionResult result;
            result.value = std::move(value);
            return result;
        }

        [[nodiscard]] ReflectedPropertyDescriptor reflectedProperty(
            uint32_t id,
            std::string name,
            ReflectedValueType type,
            ReflectedPropertyFlag flags,
            ReflectedValue defaultValue = {})
        {
            ReflectedPropertyDescriptor descriptor;
            descriptor.id = id;
            descriptor.name = std::move(name);
            descriptor.displayName = descriptor.name;
            descriptor.category = "Actor Components";
            descriptor.type = type;
            descriptor.flags = flags;
            descriptor.defaultValue = std::move(defaultValue);
            return descriptor;
        }

        template <typename Descriptor>
        [[nodiscard]] bool containsId(const std::vector<Descriptor>& descriptors, ActorComponentId componentId)
        {
            return std::ranges::any_of(descriptors, [componentId](const Descriptor& descriptor) {
                return descriptor.componentId == componentId;
            });
        }

        template <typename Descriptor>
        [[nodiscard]] std::optional<Descriptor> findDescriptor(
            const std::vector<Descriptor>& descriptors,
            ActorComponentId componentId)
        {
            const auto it = std::ranges::find_if(descriptors, [componentId](const Descriptor& descriptor) {
                return descriptor.componentId == componentId;
            });
            if (it == descriptors.end()) {
                return std::nullopt;
            }
            return *it;
        }

        template <typename Descriptor>
        [[nodiscard]] std::vector<Descriptor> sortedDescriptors(const std::vector<Descriptor>& descriptors)
        {
            std::vector<Descriptor> sorted = descriptors;
            std::ranges::sort(sorted, [](const Descriptor& lhs, const Descriptor& rhs) {
                return lhs.componentId.value < rhs.componentId.value;
            });
            return sorted;
        }

        template <typename Descriptor>
        ActorComponentStatus upsertDescriptor(
            std::vector<Descriptor>& descriptors,
            Descriptor descriptor,
            ActorGameplayComponentValidationResult (*validate)(const Descriptor&),
            std::string* message)
        {
            const ActorGameplayComponentValidationResult validation = validate(descriptor);
            if (!validation.valid) {
                if (message && !validation.errors.empty()) {
                    *message = validation.errors.front();
                }
                return ActorComponentStatus::ValidationFailed;
            }
            const auto it = std::ranges::find_if(descriptors, [descriptor](const Descriptor& candidate) {
                return candidate.componentId == descriptor.componentId;
            });
            if (it == descriptors.end()) {
                descriptors.push_back(std::move(descriptor));
            } else {
                *it = std::move(descriptor);
            }
            return ActorComponentStatus::Success;
        }

        template <typename Descriptor>
        bool removeDescriptor(std::vector<Descriptor>& descriptors, ActorComponentId componentId)
        {
            const auto before = descriptors.size();
            descriptors.erase(
                std::remove_if(descriptors.begin(), descriptors.end(), [componentId](const Descriptor& descriptor) {
                    return descriptor.componentId == componentId;
                }),
                descriptors.end());
            return before != descriptors.size();
        }

        [[nodiscard]] ActorComponentValidationResult metadataValidation(
            const ActorComponentInstanceRecord& record,
            SceneComponentTypeId expectedType)
        {
            ActorComponentValidationResult result;
            if (record.componentType != expectedType) {
                result.valid = false;
                result.errors.push_back("Authored component metadata has the wrong built-in component type.");
            }
            return result;
        }

        [[nodiscard]] SceneCharacterDescriptor characterDescriptor(
            SceneActorHandle actor,
            const ActorComponentInstanceRecord& metadata,
            const MovementComponentDescriptor& descriptor)
        {
            SceneCharacterDescriptor character;
            character.actor = actor;
            character.radius = descriptor.radius;
            character.height = descriptor.height;
            character.maxSpeed = descriptor.maxSpeed;
            character.acceleration = descriptor.acceleration;
            character.braking = descriptor.braking;
            character.gravity = descriptor.gravity;
            character.slopeLimitDegrees = descriptor.slopeLimitDegrees;
            character.stepHeight = descriptor.stepHeight;
            character.snapDistance = descriptor.snapDistance;
            character.physicsLayer = ScenePhysicsLayer{descriptor.physicsLayer};
            character.physicsFilter = ScenePhysicsFilter{
                descriptor.physicsFilterMask,
                descriptor.physicsFilterLayer,
                descriptor.physicsFilterCollideWithTriggers,
            };
            character.enabled = metadata.enabled;
            character.debugName = descriptor.debugName;
            return character;
        }

        [[nodiscard]] ActorComponentRuntimeBindingResult unbindMovementComponent(
            const ActorComponentRuntimeBindingContext& context,
            ActorComponentId componentId)
        {
            if (!context.movementRuntimeBindings || !context.destroyCharacter || !context.containsCharacter) {
                return {ActorComponentStatus::InvalidInput, "Movement binding context is incomplete."};
            }
            const std::optional<SceneCharacterHandle> existing = context.movementRuntimeBindings->character(componentId);
            if (existing && context.containsCharacter(*existing)) {
                (void)context.destroyCharacter(*existing);
            }
            (void)context.movementRuntimeBindings->remove(componentId);
            return {};
        }

        [[nodiscard]] ActorComponentRuntimeBindingResult bindMovementComponent(
            const ActorComponentRuntimeBindingContext& context,
            const ActorComponentInstanceRecord& metadata)
        {
            if (!context.scene || !context.movementComponents || !context.movementRuntimeBindings || !context.createCharacter) {
                return {ActorComponentStatus::InvalidInput, "Movement binding context is incomplete."};
            }
            const ActorComponentRuntimeBindingResult unbound = unbindMovementComponent(context, metadata.componentId);
            if (unbound.status != ActorComponentStatus::Success) {
                return unbound;
            }
            if (!metadata.enabled) {
                return {};
            }
            const std::optional<MovementComponentDescriptor> descriptor =
                context.movementComponents->descriptor(metadata.componentId);
            if (!descriptor) {
                return {ActorComponentStatus::ValidationFailed, "Movement descriptor was not found."};
            }
            const SceneActorHandle actor = findActorByStableId(*context.scene, metadata.ownerActorId);
            if (!isValid(actor)) {
                return {ActorComponentStatus::MissingActor, "Movement owner actor was not found."};
            }
            const SceneCharacterHandle character =
                context.createCharacter(characterDescriptor(actor, metadata, *descriptor));
            if (!isValid(character)) {
                return {ActorComponentStatus::SceneRejected, "SceneCharacterMovementSystem rejected movement binding."};
            }
            const ActorComponentStatus status =
                context.movementRuntimeBindings->upsert({metadata.componentId, character});
            if (status != ActorComponentStatus::Success) {
                if (context.destroyCharacter) {
                    (void)context.destroyCharacter(character);
                }
                return {status, "Movement runtime binding store rejected character mapping."};
            }
            return {};
        }

        [[nodiscard]] ActorComponentRuntimeBindingResult movementBindCallback(
            const ActorComponentRuntimeBindingContext& context,
            const ActorComponentInstanceRecord& metadata)
        {
            return bindMovementComponent(context, metadata);
        }

        [[nodiscard]] ActorComponentRuntimeBindingResult movementUnbindCallback(
            const ActorComponentRuntimeBindingContext& context,
            const ActorComponentInstanceRecord& metadata)
        {
            return unbindMovementComponent(context, metadata.componentId);
        }

        [[nodiscard]] ActorComponentRuntimeBindingResult movementApplyCallback(
            const ActorComponentRuntimeBindingContext& context,
            const ActorComponentInstanceRecord& metadata)
        {
            return bindMovementComponent(context, metadata);
        }
    }

    bool ActorStatsComponentStore::contains(ActorComponentId componentId) const { return containsId(descriptors_, componentId); }
    std::optional<StatsComponentDescriptor> ActorStatsComponentStore::descriptor(ActorComponentId componentId) const { return findDescriptor(descriptors_, componentId); }
    std::vector<StatsComponentDescriptor> ActorStatsComponentStore::descriptors() const { return sortedDescriptors(descriptors_); }
    ActorComponentStatus ActorStatsComponentStore::upsert(StatsComponentDescriptor descriptor, std::string* message) { return upsertDescriptor(descriptors_, descriptor, validateStatsComponentDescriptor, message); }
    bool ActorStatsComponentStore::remove(ActorComponentId componentId) { return removeDescriptor(descriptors_, componentId); }
    void ActorStatsComponentStore::clear() { descriptors_.clear(); }

    bool ActorMovementComponentStore::contains(ActorComponentId componentId) const { return containsId(descriptors_, componentId); }
    std::optional<MovementComponentDescriptor> ActorMovementComponentStore::descriptor(ActorComponentId componentId) const { return findDescriptor(descriptors_, componentId); }
    std::vector<MovementComponentDescriptor> ActorMovementComponentStore::descriptors() const { return sortedDescriptors(descriptors_); }
    ActorComponentStatus ActorMovementComponentStore::upsert(MovementComponentDescriptor descriptor, std::string* message) { return upsertDescriptor(descriptors_, descriptor, validateMovementComponentDescriptor, message); }
    bool ActorMovementComponentStore::remove(ActorComponentId componentId) { return removeDescriptor(descriptors_, componentId); }
    void ActorMovementComponentStore::clear() { descriptors_.clear(); }

    bool ActorSensoryComponentStore::contains(ActorComponentId componentId) const { return containsId(descriptors_, componentId); }
    std::optional<SensoryComponentDescriptor> ActorSensoryComponentStore::descriptor(ActorComponentId componentId) const { return findDescriptor(descriptors_, componentId); }
    std::vector<SensoryComponentDescriptor> ActorSensoryComponentStore::descriptors() const { return sortedDescriptors(descriptors_); }
    ActorComponentStatus ActorSensoryComponentStore::upsert(SensoryComponentDescriptor descriptor, std::string* message) { return upsertDescriptor(descriptors_, descriptor, validateSensoryComponentDescriptor, message); }
    bool ActorSensoryComponentStore::remove(ActorComponentId componentId) { return removeDescriptor(descriptors_, componentId); }
    void ActorSensoryComponentStore::clear() { descriptors_.clear(); }

    bool ActorMovementRuntimeBindingStore::contains(ActorComponentId componentId) const
    {
        return character(componentId).has_value();
    }

    std::optional<SceneCharacterHandle> ActorMovementRuntimeBindingStore::character(ActorComponentId componentId) const
    {
        const auto it = std::ranges::find_if(bindings_, [componentId](const ActorMovementRuntimeBinding& binding) {
            return binding.componentId == componentId;
        });
        if (it == bindings_.end()) {
            return std::nullopt;
        }
        return it->character;
    }

    std::vector<ActorMovementRuntimeBinding> ActorMovementRuntimeBindingStore::bindings() const
    {
        std::vector<ActorMovementRuntimeBinding> sorted = bindings_;
        std::ranges::sort(sorted, [](const auto& lhs, const auto& rhs) {
            return lhs.componentId.value < rhs.componentId.value;
        });
        return sorted;
    }

    ActorComponentStatus ActorMovementRuntimeBindingStore::upsert(ActorMovementRuntimeBinding binding)
    {
        if (!isValid(binding.componentId) || !isValid(binding.character)) {
            return ActorComponentStatus::InvalidInput;
        }
        const auto it = std::ranges::find_if(bindings_, [binding](const ActorMovementRuntimeBinding& candidate) {
            return candidate.componentId == binding.componentId;
        });
        if (it == bindings_.end()) {
            bindings_.push_back(binding);
        } else {
            *it = binding;
        }
        return ActorComponentStatus::Success;
    }

    bool ActorMovementRuntimeBindingStore::remove(ActorComponentId componentId)
    {
        const auto before = bindings_.size();
        bindings_.erase(
            std::remove_if(bindings_.begin(), bindings_.end(), [componentId](const ActorMovementRuntimeBinding& binding) {
                return binding.componentId == componentId;
            }),
            bindings_.end());
        return before != bindings_.size();
    }

    void ActorMovementRuntimeBindingStore::clear()
    {
        bindings_.clear();
    }

    StatsComponentDescriptor defaultStatsComponentDescriptor(ActorComponentId componentId)
    {
        StatsComponentDescriptor descriptor;
        descriptor.componentId = componentId;
        return descriptor;
    }

    MovementComponentDescriptor defaultMovementComponentDescriptor(ActorComponentId componentId)
    {
        MovementComponentDescriptor descriptor;
        descriptor.componentId = componentId;
        return descriptor;
    }

    SensoryComponentDescriptor defaultSensoryComponentDescriptor(ActorComponentId componentId)
    {
        SensoryComponentDescriptor descriptor;
        descriptor.componentId = componentId;
        return descriptor;
    }

    ActorGameplayComponentValidationResult validateStatsComponentDescriptor(const StatsComponentDescriptor& descriptor)
    {
        ActorGameplayComponentValidationResult result;
        if (!isValid(descriptor.componentId)) {
            error(result, "Stats descriptor requires a valid ActorComponentId.");
        }
        if (descriptor.level == 0) {
            error(result, "Stats level must be greater than zero.");
        }
        if (!finitePositive(descriptor.maxHealth) || !finiteNonNegative(descriptor.currentHealth) || descriptor.currentHealth > descriptor.maxHealth) {
            error(result, "Stats health values are invalid.");
        }
        if (!finiteNonNegative(descriptor.maxMana) || !finiteNonNegative(descriptor.currentMana) || descriptor.currentMana > descriptor.maxMana) {
            error(result, "Stats mana values are invalid.");
        }
        if (!finitePositive(descriptor.maxStamina) || !finiteNonNegative(descriptor.currentStamina) || descriptor.currentStamina > descriptor.maxStamina) {
            error(result, "Stats stamina values are invalid.");
        }
        if (descriptor.strength < 0 || descriptor.agility < 0 || descriptor.intellect < 0 || descriptor.vitality < 0) {
            error(result, "Stats attributes must be non-negative.");
        }
        return result;
    }

    ActorGameplayComponentValidationResult validateMovementComponentDescriptor(const MovementComponentDescriptor& descriptor)
    {
        ActorGameplayComponentValidationResult result;
        if (!isValid(descriptor.componentId)) {
            error(result, "Movement descriptor requires a valid ActorComponentId.");
        }
        if (!finitePositive(descriptor.radius) || !finitePositive(descriptor.height) || descriptor.height <= descriptor.radius * 2.0f) {
            error(result, "Movement capsule dimensions are invalid.");
        }
        if (!finitePositive(descriptor.maxSpeed) ||
            !finiteNonNegative(descriptor.acceleration) ||
            !finiteNonNegative(descriptor.braking) ||
            !finiteNonNegative(descriptor.gravity)) {
            error(result, "Movement speed and force settings are invalid.");
        }
        if (!std::isfinite(descriptor.slopeLimitDegrees) || descriptor.slopeLimitDegrees < 0.0f || descriptor.slopeLimitDegrees > 89.0f) {
            error(result, "Movement slope limit is invalid.");
        }
        if (!finiteNonNegative(descriptor.stepHeight) || !finiteNonNegative(descriptor.snapDistance)) {
            error(result, "Movement step or snap settings are invalid.");
        }
        if (descriptor.physicsLayer == 0 || descriptor.physicsFilterLayer == 0) {
            error(result, "Movement physics layer settings are invalid.");
        }
        if (descriptor.debugName.size() > ActorComponentAuthoringMaxDisplayNameBytes) {
            error(result, "Movement debug name is too long.");
        }
        return result;
    }

    ActorGameplayComponentValidationResult validateSensoryComponentDescriptor(const SensoryComponentDescriptor& descriptor)
    {
        ActorGameplayComponentValidationResult result;
        if (!isValid(descriptor.componentId)) {
            error(result, "Sensory descriptor requires a valid ActorComponentId.");
        }
        if (!finitePositive(descriptor.radius)) {
            error(result, "Sensory radius must be positive.");
        }
        if (!std::isfinite(descriptor.fieldOfViewDegrees) || descriptor.fieldOfViewDegrees <= 0.0f || descriptor.fieldOfViewDegrees > 360.0f) {
            error(result, "Sensory field of view is invalid.");
        }
        if (!finitePositive(descriptor.queryIntervalSeconds)) {
            error(result, "Sensory query interval must be positive.");
        }
        return result;
    }

    ActorGameplayComponentValidationResult validateActorGameplayComponentStores(
        const ActorComponentDescriptorStore& componentAuthoring,
        const ActorStatsComponentStore* stats,
        const ActorMovementComponentStore* movement,
        const ActorSensoryComponentStore* sensory)
    {
        ActorGameplayComponentValidationResult result;
        auto validatePayload = [&](ActorComponentId componentId, SceneComponentTypeId expectedType, const char* label) {
            const std::optional<ActorComponentInstanceRecord> metadata = componentAuthoring.record(componentId);
            if (!metadata) {
                error(result, std::string(label) + " payload is missing generic authored component metadata.");
                return;
            }
            if (metadata->componentType != expectedType) {
                error(result, std::string(label) + " payload has generic metadata with the wrong component type.");
            }
        };
        if (stats) {
            for (const StatsComponentDescriptor& descriptor : stats->descriptors()) {
                merge(result, validateStatsComponentDescriptor(descriptor));
                validatePayload(descriptor.componentId, ActorStatsComponentType, "Stats");
            }
        }
        if (movement) {
            for (const MovementComponentDescriptor& descriptor : movement->descriptors()) {
                merge(result, validateMovementComponentDescriptor(descriptor));
                validatePayload(descriptor.componentId, ActorMovementComponentType, "Movement");
            }
        }
        if (sensory) {
            for (const SensoryComponentDescriptor& descriptor : sensory->descriptors()) {
                merge(result, validateSensoryComponentDescriptor(descriptor));
                validatePayload(descriptor.componentId, ActorSensoryComponentType, "Sensory");
            }
        }
        return result;
    }

    void registerBuiltInActorComponentTypes(ActorComponentDescriptorRegistry& registry)
    {
        ActorComponentTypeDescriptor stats;
        stats.type = ActorStatsComponentType;
        stats.typeName = "manual.stats";
        stats.displayName = "Stats";
        stats.category = "Gameplay";
        stats.documentation = "RPG-style fixed actor attributes and resource values.";
        stats.defaultInstance.displayName = "Stats";
        stats.validateInstance = [](const ActorComponentInstanceRecord& record) {
            return metadataValidation(record, ActorStatsComponentType);
        };
        [[maybe_unused]] ActorComponentStatus statsStatus = registry.registerType(std::move(stats));

        ActorComponentTypeDescriptor movement;
        movement.type = ActorMovementComponentType;
        movement.typeName = "manual.movement";
        movement.displayName = "Movement";
        movement.category = "Gameplay";
        movement.documentation = "Descriptor-backed kinematic character movement binding.";
        movement.defaultInstance.displayName = "Movement";
        movement.validateInstance = [](const ActorComponentInstanceRecord& record) {
            return metadataValidation(record, ActorMovementComponentType);
        };
        movement.runtime.bind = movementBindCallback;
        movement.runtime.unbind = movementUnbindCallback;
        movement.runtime.applyDescriptor = movementApplyCallback;
        [[maybe_unused]] ActorComponentStatus movementStatus = registry.registerType(std::move(movement));

        ActorComponentTypeDescriptor sensory;
        sensory.type = ActorSensoryComponentType;
        sensory.typeName = "manual.sensory";
        sensory.displayName = "Sensory";
        sensory.category = "Gameplay";
        sensory.documentation = "Initial data-only world query settings for future AI/sensory systems.";
        sensory.defaultInstance.displayName = "Sensory";
        sensory.validateInstance = [](const ActorComponentInstanceRecord& record) {
            return metadataValidation(record, ActorSensoryComponentType);
        };
        [[maybe_unused]] ActorComponentStatus sensoryStatus = registry.registerType(std::move(sensory));
    }

    void registerActorGameplayComponentReflectionDescriptors(ReflectionRegistry& registry)
    {
        ReflectedObjectDescriptor stats;
        stats.id = static_cast<uint32_t>(ActorGameplayComponentReflectedObjectId::StatsComponentDescriptor);
        stats.name = "StatsComponentDescriptor";
        stats.displayName = "Stats Component Descriptor";
        stats.category = "Actor Components";
        stats.properties = {
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::ComponentId), "componentId", ReflectedValueType::UInt64, StableReadOnlyFlags, uint64_t{}),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::Level), "level", ReflectedValueType::UInt64, PayloadFlags, uint64_t{1}),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::CurrentHealth), "currentHealth", ReflectedValueType::Float, PayloadFlags, 100.0f),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::MaxHealth), "maxHealth", ReflectedValueType::Float, PayloadFlags, 100.0f),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::CurrentMana), "currentMana", ReflectedValueType::Float, PayloadFlags, 50.0f),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::MaxMana), "maxMana", ReflectedValueType::Float, PayloadFlags, 50.0f),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::CurrentStamina), "currentStamina", ReflectedValueType::Float, PayloadFlags, 100.0f),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::MaxStamina), "maxStamina", ReflectedValueType::Float, PayloadFlags, 100.0f),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::Strength), "strength", ReflectedValueType::Int64, PayloadFlags, int64_t{10}),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::Agility), "agility", ReflectedValueType::Int64, PayloadFlags, int64_t{10}),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::Intellect), "intellect", ReflectedValueType::Int64, PayloadFlags, int64_t{10}),
            reflectedProperty(static_cast<uint32_t>(StatsComponentReflectedPropertyId::Vitality), "vitality", ReflectedValueType::Int64, PayloadFlags, int64_t{10}),
        };
        (void)registry.registerObject(std::move(stats));

        ReflectedObjectDescriptor movement;
        movement.id = static_cast<uint32_t>(ActorGameplayComponentReflectedObjectId::MovementComponentDescriptor);
        movement.name = "MovementComponentDescriptor";
        movement.displayName = "Movement Component Descriptor";
        movement.category = "Actor Components";
        movement.properties = {
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::ComponentId), "componentId", ReflectedValueType::UInt64, StableReadOnlyFlags, uint64_t{}),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::Radius), "radius", ReflectedValueType::Float, PayloadFlags, 0.35f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::Height), "height", ReflectedValueType::Float, PayloadFlags, 1.8f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::MaxSpeed), "maxSpeed", ReflectedValueType::Float, PayloadFlags, 4.0f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::Acceleration), "acceleration", ReflectedValueType::Float, PayloadFlags, 20.0f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::Braking), "braking", ReflectedValueType::Float, PayloadFlags, 24.0f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::Gravity), "gravity", ReflectedValueType::Float, PayloadFlags, 24.0f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::SlopeLimitDegrees), "slopeLimitDegrees", ReflectedValueType::Float, PayloadFlags, 45.0f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::StepHeight), "stepHeight", ReflectedValueType::Float, PayloadFlags, 0.35f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::SnapDistance), "snapDistance", ReflectedValueType::Float, PayloadFlags, 0.25f),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::PhysicsLayer), "physicsLayer", ReflectedValueType::UInt64, PayloadFlags, uint64_t{2}),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::PhysicsFilterMask), "physicsFilterMask", ReflectedValueType::UInt64, PayloadFlags, uint64_t{UINT32_MAX}),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::PhysicsFilterLayer), "physicsFilterLayer", ReflectedValueType::UInt64, PayloadFlags, uint64_t{2}),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::PhysicsFilterCollideWithTriggers), "physicsFilterCollideWithTriggers", ReflectedValueType::Bool, PayloadFlags, true),
            reflectedProperty(static_cast<uint32_t>(MovementComponentReflectedPropertyId::DebugName), "debugName", ReflectedValueType::String, PayloadFlags, std::string{}),
        };
        (void)registry.registerObject(std::move(movement));

        ReflectedObjectDescriptor sensory;
        sensory.id = static_cast<uint32_t>(ActorGameplayComponentReflectedObjectId::SensoryComponentDescriptor);
        sensory.name = "SensoryComponentDescriptor";
        sensory.displayName = "Sensory Component Descriptor";
        sensory.category = "Actor Components";
        sensory.properties = {
            reflectedProperty(static_cast<uint32_t>(SensoryComponentReflectedPropertyId::ComponentId), "componentId", ReflectedValueType::UInt64, StableReadOnlyFlags, uint64_t{}),
            reflectedProperty(static_cast<uint32_t>(SensoryComponentReflectedPropertyId::Radius), "radius", ReflectedValueType::Float, PayloadFlags, 12.0f),
            reflectedProperty(static_cast<uint32_t>(SensoryComponentReflectedPropertyId::FieldOfViewDegrees), "fieldOfViewDegrees", ReflectedValueType::Float, PayloadFlags, 120.0f),
            reflectedProperty(static_cast<uint32_t>(SensoryComponentReflectedPropertyId::QueryIntervalSeconds), "queryIntervalSeconds", ReflectedValueType::Float, PayloadFlags, 0.25f),
            reflectedProperty(static_cast<uint32_t>(SensoryComponentReflectedPropertyId::FactionMask), "factionMask", ReflectedValueType::UInt64, PayloadFlags, uint64_t{UINT32_MAX}),
            reflectedProperty(static_cast<uint32_t>(SensoryComponentReflectedPropertyId::RequireLineOfSight), "requireLineOfSight", ReflectedValueType::Bool, PayloadFlags, true),
        };
        (void)registry.registerObject(std::move(sensory));
    }

    ReflectionResult getStatsComponentDescriptor(const ActorGameplayComponentReflectionContext& context, ActorComponentId componentId, StatsComponentReflectedPropertyId property)
    {
        if (!context.stats || !isValid(componentId)) return reflectionResult(ReflectionStatus::InvalidHandle);
        const std::optional<StatsComponentDescriptor> descriptor = context.stats->descriptor(componentId);
        if (!descriptor) return reflectionResult(ReflectionStatus::InvalidHandle);
        switch (property) {
            case StatsComponentReflectedPropertyId::ComponentId: return valueResult(descriptor->componentId.value);
            case StatsComponentReflectedPropertyId::Level: return valueResult(static_cast<uint64_t>(descriptor->level));
            case StatsComponentReflectedPropertyId::CurrentHealth: return valueResult(descriptor->currentHealth);
            case StatsComponentReflectedPropertyId::MaxHealth: return valueResult(descriptor->maxHealth);
            case StatsComponentReflectedPropertyId::CurrentMana: return valueResult(descriptor->currentMana);
            case StatsComponentReflectedPropertyId::MaxMana: return valueResult(descriptor->maxMana);
            case StatsComponentReflectedPropertyId::CurrentStamina: return valueResult(descriptor->currentStamina);
            case StatsComponentReflectedPropertyId::MaxStamina: return valueResult(descriptor->maxStamina);
            case StatsComponentReflectedPropertyId::Strength: return valueResult(static_cast<int64_t>(descriptor->strength));
            case StatsComponentReflectedPropertyId::Agility: return valueResult(static_cast<int64_t>(descriptor->agility));
            case StatsComponentReflectedPropertyId::Intellect: return valueResult(static_cast<int64_t>(descriptor->intellect));
            case StatsComponentReflectedPropertyId::Vitality: return valueResult(static_cast<int64_t>(descriptor->vitality));
            default: return reflectionResult(ReflectionStatus::UnknownProperty);
        }
    }

    ReflectionResult setStatsComponentDescriptor(const ActorGameplayComponentReflectionContext& context, ActorComponentId componentId, StatsComponentReflectedPropertyId property, const ReflectedValue& value)
    {
        if (!context.stats || !isValid(componentId)) return reflectionResult(ReflectionStatus::InvalidHandle);
        std::optional<StatsComponentDescriptor> descriptor = context.stats->descriptor(componentId);
        if (!descriptor) return reflectionResult(ReflectionStatus::InvalidHandle);
        auto asFloat = [&]() -> const float* { return std::get_if<float>(&value); };
        auto asUInt = [&]() -> const uint64_t* { return std::get_if<uint64_t>(&value); };
        auto asInt = [&]() -> const int64_t* { return std::get_if<int64_t>(&value); };
        switch (property) {
            case StatsComponentReflectedPropertyId::ComponentId: return reflectionResult(ReflectionStatus::ReadOnly);
            case StatsComponentReflectedPropertyId::Level: if (const uint64_t* v = asUInt()) descriptor->level = static_cast<uint32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::CurrentHealth: if (const float* v = asFloat()) descriptor->currentHealth = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::MaxHealth: if (const float* v = asFloat()) descriptor->maxHealth = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::CurrentMana: if (const float* v = asFloat()) descriptor->currentMana = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::MaxMana: if (const float* v = asFloat()) descriptor->maxMana = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::CurrentStamina: if (const float* v = asFloat()) descriptor->currentStamina = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::MaxStamina: if (const float* v = asFloat()) descriptor->maxStamina = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::Strength: if (const int64_t* v = asInt()) descriptor->strength = static_cast<int32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::Agility: if (const int64_t* v = asInt()) descriptor->agility = static_cast<int32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::Intellect: if (const int64_t* v = asInt()) descriptor->intellect = static_cast<int32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case StatsComponentReflectedPropertyId::Vitality: if (const int64_t* v = asInt()) descriptor->vitality = static_cast<int32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            default: return reflectionResult(ReflectionStatus::UnknownProperty);
        }
        std::string message;
        if (context.stats->upsert(*descriptor, &message) != ActorComponentStatus::Success) return reflectionResult(ReflectionStatus::ValidationFailed, message);
        ReflectionResult result;
        result.changed = true;
        return result;
    }

    ReflectionResult getMovementComponentDescriptor(const ActorGameplayComponentReflectionContext& context, ActorComponentId componentId, MovementComponentReflectedPropertyId property)
    {
        if (!context.movement || !isValid(componentId)) return reflectionResult(ReflectionStatus::InvalidHandle);
        const std::optional<MovementComponentDescriptor> descriptor = context.movement->descriptor(componentId);
        if (!descriptor) return reflectionResult(ReflectionStatus::InvalidHandle);
        switch (property) {
            case MovementComponentReflectedPropertyId::ComponentId: return valueResult(descriptor->componentId.value);
            case MovementComponentReflectedPropertyId::Radius: return valueResult(descriptor->radius);
            case MovementComponentReflectedPropertyId::Height: return valueResult(descriptor->height);
            case MovementComponentReflectedPropertyId::MaxSpeed: return valueResult(descriptor->maxSpeed);
            case MovementComponentReflectedPropertyId::Acceleration: return valueResult(descriptor->acceleration);
            case MovementComponentReflectedPropertyId::Braking: return valueResult(descriptor->braking);
            case MovementComponentReflectedPropertyId::Gravity: return valueResult(descriptor->gravity);
            case MovementComponentReflectedPropertyId::SlopeLimitDegrees: return valueResult(descriptor->slopeLimitDegrees);
            case MovementComponentReflectedPropertyId::StepHeight: return valueResult(descriptor->stepHeight);
            case MovementComponentReflectedPropertyId::SnapDistance: return valueResult(descriptor->snapDistance);
            case MovementComponentReflectedPropertyId::PhysicsLayer: return valueResult(static_cast<uint64_t>(descriptor->physicsLayer));
            case MovementComponentReflectedPropertyId::PhysicsFilterMask: return valueResult(static_cast<uint64_t>(descriptor->physicsFilterMask));
            case MovementComponentReflectedPropertyId::PhysicsFilterLayer: return valueResult(static_cast<uint64_t>(descriptor->physicsFilterLayer));
            case MovementComponentReflectedPropertyId::PhysicsFilterCollideWithTriggers: return valueResult(descriptor->physicsFilterCollideWithTriggers);
            case MovementComponentReflectedPropertyId::DebugName: return valueResult(descriptor->debugName);
            default: return reflectionResult(ReflectionStatus::UnknownProperty);
        }
    }

    ReflectionResult setMovementComponentDescriptor(const ActorGameplayComponentReflectionContext& context, ActorComponentId componentId, MovementComponentReflectedPropertyId property, const ReflectedValue& value)
    {
        if (!context.movement || !isValid(componentId)) return reflectionResult(ReflectionStatus::InvalidHandle);
        std::optional<MovementComponentDescriptor> descriptor = context.movement->descriptor(componentId);
        if (!descriptor) return reflectionResult(ReflectionStatus::InvalidHandle);
        auto f = [&]() { return std::get_if<float>(&value); };
        auto u = [&]() { return std::get_if<uint64_t>(&value); };
        switch (property) {
            case MovementComponentReflectedPropertyId::ComponentId: return reflectionResult(ReflectionStatus::ReadOnly);
            case MovementComponentReflectedPropertyId::Radius: if (const float* v = f()) descriptor->radius = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::Height: if (const float* v = f()) descriptor->height = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::MaxSpeed: if (const float* v = f()) descriptor->maxSpeed = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::Acceleration: if (const float* v = f()) descriptor->acceleration = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::Braking: if (const float* v = f()) descriptor->braking = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::Gravity: if (const float* v = f()) descriptor->gravity = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::SlopeLimitDegrees: if (const float* v = f()) descriptor->slopeLimitDegrees = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::StepHeight: if (const float* v = f()) descriptor->stepHeight = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::SnapDistance: if (const float* v = f()) descriptor->snapDistance = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::PhysicsLayer: if (const uint64_t* v = u()) descriptor->physicsLayer = static_cast<uint32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::PhysicsFilterMask: if (const uint64_t* v = u()) descriptor->physicsFilterMask = static_cast<uint32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::PhysicsFilterLayer: if (const uint64_t* v = u()) descriptor->physicsFilterLayer = static_cast<uint32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::PhysicsFilterCollideWithTriggers: if (const bool* v = std::get_if<bool>(&value)) descriptor->physicsFilterCollideWithTriggers = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case MovementComponentReflectedPropertyId::DebugName: if (const std::string* v = std::get_if<std::string>(&value)) descriptor->debugName = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            default: return reflectionResult(ReflectionStatus::UnknownProperty);
        }
        std::string message;
        if (context.movement->upsert(*descriptor, &message) != ActorComponentStatus::Success) return reflectionResult(ReflectionStatus::ValidationFailed, message);
        ReflectionResult result;
        result.changed = true;
        return result;
    }

    ReflectionResult getSensoryComponentDescriptor(const ActorGameplayComponentReflectionContext& context, ActorComponentId componentId, SensoryComponentReflectedPropertyId property)
    {
        if (!context.sensory || !isValid(componentId)) return reflectionResult(ReflectionStatus::InvalidHandle);
        const std::optional<SensoryComponentDescriptor> descriptor = context.sensory->descriptor(componentId);
        if (!descriptor) return reflectionResult(ReflectionStatus::InvalidHandle);
        switch (property) {
            case SensoryComponentReflectedPropertyId::ComponentId: return valueResult(descriptor->componentId.value);
            case SensoryComponentReflectedPropertyId::Radius: return valueResult(descriptor->radius);
            case SensoryComponentReflectedPropertyId::FieldOfViewDegrees: return valueResult(descriptor->fieldOfViewDegrees);
            case SensoryComponentReflectedPropertyId::QueryIntervalSeconds: return valueResult(descriptor->queryIntervalSeconds);
            case SensoryComponentReflectedPropertyId::FactionMask: return valueResult(static_cast<uint64_t>(descriptor->factionMask));
            case SensoryComponentReflectedPropertyId::RequireLineOfSight: return valueResult(descriptor->requireLineOfSight);
            default: return reflectionResult(ReflectionStatus::UnknownProperty);
        }
    }

    ReflectionResult setSensoryComponentDescriptor(const ActorGameplayComponentReflectionContext& context, ActorComponentId componentId, SensoryComponentReflectedPropertyId property, const ReflectedValue& value)
    {
        if (!context.sensory || !isValid(componentId)) return reflectionResult(ReflectionStatus::InvalidHandle);
        std::optional<SensoryComponentDescriptor> descriptor = context.sensory->descriptor(componentId);
        if (!descriptor) return reflectionResult(ReflectionStatus::InvalidHandle);
        switch (property) {
            case SensoryComponentReflectedPropertyId::ComponentId: return reflectionResult(ReflectionStatus::ReadOnly);
            case SensoryComponentReflectedPropertyId::Radius: if (const float* v = std::get_if<float>(&value)) descriptor->radius = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case SensoryComponentReflectedPropertyId::FieldOfViewDegrees: if (const float* v = std::get_if<float>(&value)) descriptor->fieldOfViewDegrees = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case SensoryComponentReflectedPropertyId::QueryIntervalSeconds: if (const float* v = std::get_if<float>(&value)) descriptor->queryIntervalSeconds = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case SensoryComponentReflectedPropertyId::FactionMask: if (const uint64_t* v = std::get_if<uint64_t>(&value)) descriptor->factionMask = static_cast<uint32_t>(*v); else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            case SensoryComponentReflectedPropertyId::RequireLineOfSight: if (const bool* v = std::get_if<bool>(&value)) descriptor->requireLineOfSight = *v; else return reflectionResult(ReflectionStatus::TypeMismatch); break;
            default: return reflectionResult(ReflectionStatus::UnknownProperty);
        }
        std::string message;
        if (context.sensory->upsert(*descriptor, &message) != ActorComponentStatus::Success) return reflectionResult(ReflectionStatus::ValidationFailed, message);
        ReflectionResult result;
        result.changed = true;
        return result;
    }
}
