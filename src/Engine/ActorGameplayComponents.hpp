#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Engine/ActorComponentAuthoring.hpp"
#include "Engine/SceneCharacterMovement.hpp"

namespace Engine {
    inline constexpr SceneComponentTypeId ActorStatsComponentType{3001};
    inline constexpr SceneComponentTypeId ActorMovementComponentType{3002};
    inline constexpr SceneComponentTypeId ActorSensoryComponentType{3003};

    enum class ActorGameplayComponentReflectedObjectId : uint32_t {
        StatsComponentDescriptor = 2200,
        MovementComponentDescriptor = 2201,
        SensoryComponentDescriptor = 2202,
    };

    enum class StatsComponentReflectedPropertyId : uint32_t {
        ComponentId = 1,
        Level = 2,
        CurrentHealth = 3,
        MaxHealth = 4,
        CurrentMana = 5,
        MaxMana = 6,
        CurrentStamina = 7,
        MaxStamina = 8,
        Strength = 9,
        Agility = 10,
        Intellect = 11,
        Vitality = 12,
    };

    enum class MovementComponentReflectedPropertyId : uint32_t {
        ComponentId = 1,
        Radius = 2,
        Height = 3,
        MaxSpeed = 4,
        Acceleration = 5,
        Braking = 6,
        Gravity = 7,
        SlopeLimitDegrees = 8,
        StepHeight = 9,
        SnapDistance = 10,
        PhysicsLayer = 11,
        PhysicsFilterMask = 12,
        PhysicsFilterLayer = 13,
        PhysicsFilterCollideWithTriggers = 14,
        DebugName = 15,
    };

    enum class SensoryComponentReflectedPropertyId : uint32_t {
        ComponentId = 1,
        Radius = 2,
        FieldOfViewDegrees = 3,
        QueryIntervalSeconds = 4,
        FactionMask = 5,
        RequireLineOfSight = 6,
    };

    struct StatsComponentDescriptor {
        ActorComponentId componentId;
        uint32_t level = 1;
        float currentHealth = 100.0f;
        float maxHealth = 100.0f;
        float currentMana = 50.0f;
        float maxMana = 50.0f;
        float currentStamina = 100.0f;
        float maxStamina = 100.0f;
        int32_t strength = 10;
        int32_t agility = 10;
        int32_t intellect = 10;
        int32_t vitality = 10;
    };

    struct MovementComponentDescriptor {
        ActorComponentId componentId;
        float radius = 0.35f;
        float height = 1.8f;
        float maxSpeed = 4.0f;
        float acceleration = 20.0f;
        float braking = 24.0f;
        float gravity = 24.0f;
        float slopeLimitDegrees = 45.0f;
        float stepHeight = 0.35f;
        float snapDistance = 0.25f;
        uint32_t physicsLayer = 2;
        uint32_t physicsFilterMask = UINT32_MAX;
        uint32_t physicsFilterLayer = 2;
        bool physicsFilterCollideWithTriggers = true;
        std::string debugName;
    };

    struct SensoryComponentDescriptor {
        ActorComponentId componentId;
        float radius = 12.0f;
        float fieldOfViewDegrees = 120.0f;
        float queryIntervalSeconds = 0.25f;
        uint32_t factionMask = UINT32_MAX;
        bool requireLineOfSight = true;
    };

    struct ActorGameplayComponentValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    class ActorStatsComponentStore {
    public:
        [[nodiscard]] bool contains(ActorComponentId componentId) const;
        [[nodiscard]] std::optional<StatsComponentDescriptor> descriptor(ActorComponentId componentId) const;
        [[nodiscard]] std::vector<StatsComponentDescriptor> descriptors() const;
        [[nodiscard]] ActorComponentStatus upsert(StatsComponentDescriptor descriptor, std::string* message = nullptr);
        bool remove(ActorComponentId componentId);
        void clear();

    private:
        std::vector<StatsComponentDescriptor> descriptors_;
    };

    class ActorMovementComponentStore {
    public:
        [[nodiscard]] bool contains(ActorComponentId componentId) const;
        [[nodiscard]] std::optional<MovementComponentDescriptor> descriptor(ActorComponentId componentId) const;
        [[nodiscard]] std::vector<MovementComponentDescriptor> descriptors() const;
        [[nodiscard]] ActorComponentStatus upsert(MovementComponentDescriptor descriptor, std::string* message = nullptr);
        bool remove(ActorComponentId componentId);
        void clear();

    private:
        std::vector<MovementComponentDescriptor> descriptors_;
    };

    class ActorSensoryComponentStore {
    public:
        [[nodiscard]] bool contains(ActorComponentId componentId) const;
        [[nodiscard]] std::optional<SensoryComponentDescriptor> descriptor(ActorComponentId componentId) const;
        [[nodiscard]] std::vector<SensoryComponentDescriptor> descriptors() const;
        [[nodiscard]] ActorComponentStatus upsert(SensoryComponentDescriptor descriptor, std::string* message = nullptr);
        bool remove(ActorComponentId componentId);
        void clear();

    private:
        std::vector<SensoryComponentDescriptor> descriptors_;
    };

    struct ActorMovementRuntimeBinding {
        ActorComponentId componentId;
        SceneCharacterHandle character;
    };

    class ActorMovementRuntimeBindingStore {
    public:
        [[nodiscard]] bool contains(ActorComponentId componentId) const;
        [[nodiscard]] std::optional<SceneCharacterHandle> character(ActorComponentId componentId) const;
        [[nodiscard]] std::vector<ActorMovementRuntimeBinding> bindings() const;
        [[nodiscard]] ActorComponentStatus upsert(ActorMovementRuntimeBinding binding);
        bool remove(ActorComponentId componentId);
        void clear();

    private:
        std::vector<ActorMovementRuntimeBinding> bindings_;
    };

    struct ActorGameplayComponentReflectionContext {
        ActorStatsComponentStore* stats = nullptr;
        ActorMovementComponentStore* movement = nullptr;
        ActorSensoryComponentStore* sensory = nullptr;
    };

    [[nodiscard]] StatsComponentDescriptor defaultStatsComponentDescriptor(ActorComponentId componentId);
    [[nodiscard]] MovementComponentDescriptor defaultMovementComponentDescriptor(ActorComponentId componentId);
    [[nodiscard]] SensoryComponentDescriptor defaultSensoryComponentDescriptor(ActorComponentId componentId);

    [[nodiscard]] ActorGameplayComponentValidationResult validateStatsComponentDescriptor(const StatsComponentDescriptor& descriptor);
    [[nodiscard]] ActorGameplayComponentValidationResult validateMovementComponentDescriptor(const MovementComponentDescriptor& descriptor);
    [[nodiscard]] ActorGameplayComponentValidationResult validateSensoryComponentDescriptor(const SensoryComponentDescriptor& descriptor);
    [[nodiscard]] ActorGameplayComponentValidationResult validateActorGameplayComponentStores(
        const ActorComponentDescriptorStore& componentAuthoring,
        const ActorStatsComponentStore* stats,
        const ActorMovementComponentStore* movement,
        const ActorSensoryComponentStore* sensory);

    void registerBuiltInActorComponentTypes(ActorComponentDescriptorRegistry& registry);
    void registerActorGameplayComponentReflectionDescriptors(ReflectionRegistry& registry);

    [[nodiscard]] ReflectionResult getStatsComponentDescriptor(
        const ActorGameplayComponentReflectionContext& context,
        ActorComponentId componentId,
        StatsComponentReflectedPropertyId property);
    [[nodiscard]] ReflectionResult setStatsComponentDescriptor(
        const ActorGameplayComponentReflectionContext& context,
        ActorComponentId componentId,
        StatsComponentReflectedPropertyId property,
        const ReflectedValue& value);
    [[nodiscard]] ReflectionResult getMovementComponentDescriptor(
        const ActorGameplayComponentReflectionContext& context,
        ActorComponentId componentId,
        MovementComponentReflectedPropertyId property);
    [[nodiscard]] ReflectionResult setMovementComponentDescriptor(
        const ActorGameplayComponentReflectionContext& context,
        ActorComponentId componentId,
        MovementComponentReflectedPropertyId property,
        const ReflectedValue& value);
    [[nodiscard]] ReflectionResult getSensoryComponentDescriptor(
        const ActorGameplayComponentReflectionContext& context,
        ActorComponentId componentId,
        SensoryComponentReflectedPropertyId property);
    [[nodiscard]] ReflectionResult setSensoryComponentDescriptor(
        const ActorGameplayComponentReflectionContext& context,
        ActorComponentId componentId,
        SensoryComponentReflectedPropertyId property,
        const ReflectedValue& value);
}
