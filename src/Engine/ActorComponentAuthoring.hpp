#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Engine/ActorAuthoring.hpp"
#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Engine {
    inline constexpr uint32_t ActorComponentAuthoringMaxDisplayNameBytes = 128;

    struct ActorComponentId {
        uint64_t value = 0;
    };

    [[nodiscard]] constexpr bool isValid(ActorComponentId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool operator==(ActorComponentId lhs, ActorComponentId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(ActorComponentId lhs, ActorComponentId rhs)
    {
        return !(lhs == rhs);
    }

    enum class ActorComponentStatus {
        Success,
        InvalidInput,
        DuplicateComponentId,
        MissingActor,
        UnknownComponentType,
        ValidationFailed,
        SceneRejected,
        CallbackFailed,
    };

    enum class ActorComponentReflectedObjectId : uint32_t {
        AuthoredComponentMetadata = 2100,
    };

    enum class ActorComponentReflectedPropertyId : uint32_t {
        ComponentId = 1,
        OwnerActorId = 2,
        ComponentType = 3,
        DisplayName = 4,
        Enabled = 5,
        Order = 6,
    };

    enum class ActorComponentTypeFlag : uint32_t {
        None = 0,
        Serializable = 1u << 0,
        RuntimeBindable = 1u << 1,
    };

    [[nodiscard]] constexpr ActorComponentTypeFlag operator|(
        ActorComponentTypeFlag lhs,
        ActorComponentTypeFlag rhs)
    {
        return static_cast<ActorComponentTypeFlag>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr ActorComponentTypeFlag operator&(
        ActorComponentTypeFlag lhs,
        ActorComponentTypeFlag rhs)
    {
        return static_cast<ActorComponentTypeFlag>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr bool hasFlag(ActorComponentTypeFlag flags, ActorComponentTypeFlag flag)
    {
        return static_cast<uint32_t>(flags & flag) != 0;
    }

    struct ActorComponentInstanceRecord {
        ActorComponentId componentId;
        SceneObjectId ownerActorId;
        SceneComponentTypeId componentType;
        std::string displayName;
        bool enabled = true;
        uint32_t order = 0;
    };

    struct ActorComponentValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    struct ActorComponentOperationResult {
        ActorComponentStatus status = ActorComponentStatus::Success;
        std::string message;
        ActorComponentId componentId;
        SceneComponentHandle sceneComponent;
    };

    struct ActorComponentRuntimeBindingContext {
        Scene* scene = nullptr;
        ActorAuthoringStore* actorAuthoring = nullptr;
        class ActorComponentDescriptorStore* componentAuthoring = nullptr;
    };

    struct ActorComponentRuntimeBindingResult {
        ActorComponentStatus status = ActorComponentStatus::Success;
        std::string message;
    };

    using ActorComponentInstanceValidationCallback =
        std::function<ActorComponentValidationResult(const ActorComponentInstanceRecord&)>;
    using ActorComponentReflectionRegistrationCallback =
        std::function<void(ReflectionRegistry&)>;
    using ActorComponentRuntimeBindingCallback = std::function<ActorComponentRuntimeBindingResult(
        const ActorComponentRuntimeBindingContext&,
        const ActorComponentInstanceRecord&)>;

    struct ActorComponentRuntimeBindingCallbacks {
        ActorComponentRuntimeBindingCallback bind;
        ActorComponentRuntimeBindingCallback unbind;
        ActorComponentRuntimeBindingCallback applyDescriptor;
    };

    struct ActorComponentTypeDescriptor {
        SceneComponentTypeId type;
        std::string typeName;
        std::string displayName;
        std::string category;
        std::string documentation;
        ActorComponentTypeFlag flags =
            ActorComponentTypeFlag::Serializable | ActorComponentTypeFlag::RuntimeBindable;
        ActorComponentInstanceRecord defaultInstance;
        ActorComponentInstanceValidationCallback validateInstance;
        ActorComponentReflectionRegistrationCallback registerReflection;
        ActorComponentRuntimeBindingCallbacks runtime;
    };

    class ActorComponentDescriptorRegistry {
    public:
        [[nodiscard]] ActorComponentStatus registerType(
            ActorComponentTypeDescriptor descriptor,
            std::string* message = nullptr);
        [[nodiscard]] bool contains(SceneComponentTypeId type) const;
        [[nodiscard]] std::optional<ActorComponentTypeDescriptor> descriptor(SceneComponentTypeId type) const;
        [[nodiscard]] std::vector<ActorComponentTypeDescriptor> descriptors() const;

    private:
        std::vector<ActorComponentTypeDescriptor> descriptors_;
    };

    class ActorComponentDescriptorStore {
    public:
        [[nodiscard]] bool contains(ActorComponentId componentId) const;
        [[nodiscard]] std::optional<ActorComponentInstanceRecord> record(ActorComponentId componentId) const;
        [[nodiscard]] std::vector<ActorComponentInstanceRecord> records() const;

        [[nodiscard]] ActorComponentStatus upsert(
            ActorComponentInstanceRecord record,
            std::string* message = nullptr);
        bool remove(ActorComponentId componentId);
        void clear();
        uint32_t pruneMissingActors(const ActorAuthoringStore& actorAuthoring);

    private:
        std::vector<ActorComponentInstanceRecord> records_;
    };

    struct ActorComponentAuthoringReflectionContext {
        ActorComponentDescriptorStore* store = nullptr;
    };

    [[nodiscard]] ActorComponentInstanceRecord defaultActorComponentInstanceRecord(
        ActorComponentId componentId,
        SceneObjectId ownerActorId,
        const ActorComponentTypeDescriptor& descriptor);
    [[nodiscard]] ActorComponentValidationResult validateActorComponentInstanceRecord(
        const ActorComponentInstanceRecord& record);
    [[nodiscard]] ActorComponentValidationResult validateActorComponentDescriptorStore(
        const ActorComponentDescriptorStore& store,
        const ActorAuthoringStore* actorAuthoring = nullptr,
        const ActorComponentDescriptorRegistry* registry = nullptr);

    [[nodiscard]] ActorComponentOperationResult createAuthoredComponent(
        Scene& scene,
        const ActorAuthoringStore& actorAuthoring,
        const ActorComponentDescriptorRegistry& registry,
        ActorComponentDescriptorStore& store,
        ActorComponentId componentId,
        SceneObjectId ownerActorId,
        SceneComponentTypeId componentType,
        std::optional<ActorComponentInstanceRecord> metadata = std::nullopt);
    bool removeAuthoredComponent(ActorComponentDescriptorStore& store, ActorComponentId componentId);
    uint32_t pruneMissingActorComponents(
        ActorComponentDescriptorStore& store,
        const ActorAuthoringStore& actorAuthoring);

    [[nodiscard]] ActorComponentRuntimeBindingResult bindAuthoredComponents(
        const ActorComponentRuntimeBindingContext& context,
        const ActorComponentDescriptorRegistry& registry,
        const ActorComponentDescriptorStore& store);
    [[nodiscard]] ActorComponentRuntimeBindingResult unbindAuthoredComponents(
        const ActorComponentRuntimeBindingContext& context,
        const ActorComponentDescriptorRegistry& registry,
        const ActorComponentDescriptorStore& store);
    [[nodiscard]] ActorComponentRuntimeBindingResult applyAuthoredComponentDescriptors(
        const ActorComponentRuntimeBindingContext& context,
        const ActorComponentDescriptorRegistry& registry,
        const ActorComponentDescriptorStore& store);

    void registerActorComponentAuthoringReflectionDescriptors(ReflectionRegistry& registry);

    [[nodiscard]] ReflectionResult getAuthoredComponentMetadata(
        const ActorComponentAuthoringReflectionContext& context,
        ActorComponentId componentId,
        ActorComponentReflectedPropertyId property);
    [[nodiscard]] ReflectionResult setAuthoredComponentMetadata(
        const ActorComponentAuthoringReflectionContext& context,
        ActorComponentId componentId,
        ActorComponentReflectedPropertyId property,
        const ReflectedValue& value);
}
