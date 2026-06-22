#include "Engine/ActorComponentAuthoring.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <utility>

namespace Engine {
    namespace {
        constexpr ReflectedPropertyFlag MetadataFlags =
            ReflectedPropertyFlag::EditorVisible |
            ReflectedPropertyFlag::ScriptVisible |
            ReflectedPropertyFlag::Serializable;
        constexpr ReflectedPropertyFlag StableReadOnlyFlags =
            MetadataFlags |
            ReflectedPropertyFlag::ReadOnly |
            ReflectedPropertyFlag::StableReference;

        [[nodiscard]] bool hasControlCharacter(const std::string& value)
        {
            return std::ranges::any_of(value, [](unsigned char ch) {
                return std::iscntrl(ch) != 0;
            });
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

        [[nodiscard]] ReflectionResult result(ReflectionStatus status, std::string message = {})
        {
            ReflectionResult reflection;
            reflection.status = status;
            reflection.message = std::move(message);
            return reflection;
        }

        [[nodiscard]] ReflectionResult valueResult(ReflectedValue value)
        {
            ReflectionResult reflection;
            reflection.value = std::move(value);
            return reflection;
        }

        [[nodiscard]] ReflectedPropertyDescriptor property(
            ActorComponentReflectedPropertyId id,
            std::string name,
            ReflectedValueType type,
            ReflectedPropertyFlag flags,
            ReflectedValue defaultValue = {})
        {
            ReflectedPropertyDescriptor descriptor;
            descriptor.id = static_cast<uint32_t>(id);
            descriptor.name = std::move(name);
            descriptor.displayName = descriptor.name;
            descriptor.category = "Actor Components";
            descriptor.type = type;
            descriptor.flags = flags;
            descriptor.defaultValue = std::move(defaultValue);
            return descriptor;
        }

        void mergeValidation(ActorComponentValidationResult& target, const ActorComponentValidationResult& source)
        {
            if (!source.valid) {
                target.valid = false;
            }
            target.errors.insert(target.errors.end(), source.errors.begin(), source.errors.end());
            target.warnings.insert(target.warnings.end(), source.warnings.begin(), source.warnings.end());
        }

        [[nodiscard]] ActorComponentRuntimeBindingResult invokeCallbacks(
            const ActorComponentRuntimeBindingContext& context,
            const ActorComponentDescriptorRegistry& registry,
            const ActorComponentDescriptorStore& store,
            ActorComponentRuntimeBindingCallback ActorComponentRuntimeBindingCallbacks::* callback)
        {
            for (const ActorComponentInstanceRecord& record : store.records()) {
                const std::optional<ActorComponentTypeDescriptor> descriptor = registry.descriptor(record.componentType);
                if (!descriptor) {
                    return {ActorComponentStatus::UnknownComponentType, "Authored component type is not registered."};
                }
                const ActorComponentRuntimeBindingCallback& fn = descriptor->runtime.*callback;
                if (!fn) {
                    continue;
                }
                const ActorComponentRuntimeBindingResult callbackResult = fn(context, record);
                if (callbackResult.status != ActorComponentStatus::Success) {
                    return callbackResult;
                }
            }
            return {};
        }
    }

    ActorComponentStatus ActorComponentDescriptorRegistry::registerType(
        ActorComponentTypeDescriptor descriptor,
        std::string* message)
    {
        auto fail = [&](ActorComponentStatus status, std::string text) {
            if (message) {
                *message = std::move(text);
            }
            return status;
        };

        if (!isValid(descriptor.type)) {
            return fail(ActorComponentStatus::InvalidInput, "Component type descriptor requires a valid type ID.");
        }
        if (descriptor.typeName.empty() || descriptor.displayName.empty() || descriptor.category.empty()) {
            return fail(ActorComponentStatus::InvalidInput, "Component type descriptor metadata is incomplete.");
        }
        if (hasControlCharacter(descriptor.typeName) ||
            hasControlCharacter(descriptor.displayName) ||
            hasControlCharacter(descriptor.category)) {
            return fail(ActorComponentStatus::InvalidInput, "Component type descriptor metadata is invalid.");
        }
        if (contains(descriptor.type)) {
            return fail(ActorComponentStatus::DuplicateComponentId, "Component type descriptor ID is already registered.");
        }

        ActorComponentInstanceRecord defaultInstance = descriptor.defaultInstance;
        if (!isValid(defaultInstance.componentId)) {
            defaultInstance.componentId = {1};
        }
        if (!isValid(defaultInstance.ownerActorId)) {
            defaultInstance.ownerActorId = {1};
        }
        defaultInstance.componentType = descriptor.type;
        if (defaultInstance.displayName.empty()) {
            defaultInstance.displayName = descriptor.displayName;
        }

        ActorComponentValidationResult validation = validateActorComponentInstanceRecord(defaultInstance);
        if (descriptor.validateInstance) {
            mergeValidation(validation, descriptor.validateInstance(defaultInstance));
        }
        if (!validation.valid) {
            return fail(
                ActorComponentStatus::ValidationFailed,
                validation.errors.empty() ? "Component type default metadata is invalid." : validation.errors.front());
        }

        descriptor.defaultInstance = defaultInstance;
        descriptors_.push_back(std::move(descriptor));
        return ActorComponentStatus::Success;
    }

    bool ActorComponentDescriptorRegistry::contains(SceneComponentTypeId type) const
    {
        return std::ranges::any_of(descriptors_, [type](const ActorComponentTypeDescriptor& descriptor) {
            return descriptor.type == type;
        });
    }

    std::optional<ActorComponentTypeDescriptor> ActorComponentDescriptorRegistry::descriptor(
        SceneComponentTypeId type) const
    {
        const auto it = std::ranges::find_if(descriptors_, [type](const ActorComponentTypeDescriptor& descriptor) {
            return descriptor.type == type;
        });
        if (it == descriptors_.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::vector<ActorComponentTypeDescriptor> ActorComponentDescriptorRegistry::descriptors() const
    {
        std::vector<ActorComponentTypeDescriptor> sorted = descriptors_;
        std::ranges::sort(sorted, [](const auto& lhs, const auto& rhs) {
            return lhs.type.value < rhs.type.value;
        });
        return sorted;
    }

    bool ActorComponentDescriptorStore::contains(ActorComponentId componentId) const
    {
        return std::ranges::any_of(records_, [componentId](const ActorComponentInstanceRecord& record) {
            return record.componentId == componentId;
        });
    }

    std::optional<ActorComponentInstanceRecord> ActorComponentDescriptorStore::record(
        ActorComponentId componentId) const
    {
        const auto it = std::ranges::find_if(records_, [componentId](const ActorComponentInstanceRecord& record) {
            return record.componentId == componentId;
        });
        if (it == records_.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::vector<ActorComponentInstanceRecord> ActorComponentDescriptorStore::records() const
    {
        std::vector<ActorComponentInstanceRecord> sorted = records_;
        std::ranges::sort(sorted, [](const auto& lhs, const auto& rhs) {
            if (lhs.ownerActorId.value != rhs.ownerActorId.value) {
                return lhs.ownerActorId.value < rhs.ownerActorId.value;
            }
            if (lhs.order != rhs.order) {
                return lhs.order < rhs.order;
            }
            if (lhs.componentType.value != rhs.componentType.value) {
                return lhs.componentType.value < rhs.componentType.value;
            }
            return lhs.componentId.value < rhs.componentId.value;
        });
        return sorted;
    }

    ActorComponentStatus ActorComponentDescriptorStore::upsert(
        ActorComponentInstanceRecord record,
        std::string* message)
    {
        const ActorComponentValidationResult validation = validateActorComponentInstanceRecord(record);
        if (!validation.valid) {
            if (message && !validation.errors.empty()) {
                *message = validation.errors.front();
            }
            return ActorComponentStatus::ValidationFailed;
        }

        const auto it = std::ranges::find_if(records_, [record](const ActorComponentInstanceRecord& candidate) {
            return candidate.componentId == record.componentId;
        });
        if (it == records_.end()) {
            records_.push_back(std::move(record));
        } else {
            *it = std::move(record);
        }
        return ActorComponentStatus::Success;
    }

    bool ActorComponentDescriptorStore::remove(ActorComponentId componentId)
    {
        const auto size = records_.size();
        records_.erase(
            std::remove_if(records_.begin(), records_.end(), [componentId](const ActorComponentInstanceRecord& record) {
                return record.componentId == componentId;
            }),
            records_.end());
        return records_.size() != size;
    }

    void ActorComponentDescriptorStore::clear()
    {
        records_.clear();
    }

    uint32_t ActorComponentDescriptorStore::pruneMissingActors(const ActorAuthoringStore& actorAuthoring)
    {
        const auto before = records_.size();
        records_.erase(
            std::remove_if(records_.begin(), records_.end(), [&actorAuthoring](const ActorComponentInstanceRecord& record) {
                return !actorAuthoring.contains(record.ownerActorId);
            }),
            records_.end());
        return static_cast<uint32_t>(before - records_.size());
    }

    ActorComponentInstanceRecord defaultActorComponentInstanceRecord(
        ActorComponentId componentId,
        SceneObjectId ownerActorId,
        const ActorComponentTypeDescriptor& descriptor)
    {
        ActorComponentInstanceRecord record = descriptor.defaultInstance;
        record.componentId = componentId;
        record.ownerActorId = ownerActorId;
        record.componentType = descriptor.type;
        if (record.displayName.empty()) {
            record.displayName = descriptor.displayName;
        }
        return record;
    }

    ActorComponentValidationResult validateActorComponentInstanceRecord(
        const ActorComponentInstanceRecord& record)
    {
        ActorComponentValidationResult result;
        auto error = [&](std::string message) {
            result.valid = false;
            result.errors.push_back(std::move(message));
        };

        if (!isValid(record.componentId)) {
            error("Authored component metadata requires a valid ActorComponentId.");
        }
        if (!isValid(record.ownerActorId)) {
            error("Authored component metadata requires a valid owner SceneObjectId.");
        }
        if (!isValid(record.componentType)) {
            error("Authored component metadata requires a valid SceneComponentTypeId.");
        }
        if (record.displayName.size() > ActorComponentAuthoringMaxDisplayNameBytes ||
            hasControlCharacter(record.displayName)) {
            error("Authored component display name is invalid.");
        }
        return result;
    }

    ActorComponentValidationResult validateActorComponentDescriptorStore(
        const ActorComponentDescriptorStore& store,
        const ActorAuthoringStore* actorAuthoring,
        const ActorComponentDescriptorRegistry* registry)
    {
        ActorComponentValidationResult result;
        std::set<uint64_t> componentIds;
        for (const ActorComponentInstanceRecord& record : store.records()) {
            mergeValidation(result, validateActorComponentInstanceRecord(record));
            if (isValid(record.componentId) && !componentIds.insert(record.componentId.value).second) {
                result.valid = false;
                result.errors.push_back("Authored component store contains duplicate ActorComponentId records.");
            }
            if (actorAuthoring && isValid(record.ownerActorId) && !actorAuthoring->contains(record.ownerActorId)) {
                result.valid = false;
                result.errors.push_back("Authored component metadata references a missing actor.");
            }
            if (registry && isValid(record.componentType)) {
                const std::optional<ActorComponentTypeDescriptor> descriptor = registry->descriptor(record.componentType);
                if (!descriptor) {
                    result.valid = false;
                    result.errors.push_back("Authored component metadata references an unregistered component type.");
                } else if (descriptor->validateInstance) {
                    mergeValidation(result, descriptor->validateInstance(record));
                }
            }
        }
        return result;
    }

    ActorComponentOperationResult createAuthoredComponent(
        Scene& scene,
        const ActorAuthoringStore& actorAuthoring,
        const ActorComponentDescriptorRegistry& registry,
        ActorComponentDescriptorStore& store,
        ActorComponentId componentId,
        SceneObjectId ownerActorId,
        SceneComponentTypeId componentType,
        std::optional<ActorComponentInstanceRecord> metadata)
    {
        ActorComponentOperationResult result;
        result.componentId = componentId;
        if (!isValid(componentId) || !isValid(ownerActorId) || !isValid(componentType)) {
            result.status = ActorComponentStatus::InvalidInput;
            result.message = "Authored component creation requires valid stable IDs.";
            return result;
        }
        if (store.contains(componentId)) {
            result.status = ActorComponentStatus::DuplicateComponentId;
            result.message = "Authored component ID already exists.";
            return result;
        }
        if (!actorAuthoring.contains(ownerActorId)) {
            result.status = ActorComponentStatus::MissingActor;
            result.message = "Authored component owner actor metadata was not found.";
            return result;
        }
        const std::optional<ActorComponentTypeDescriptor> descriptor = registry.descriptor(componentType);
        if (!descriptor) {
            result.status = ActorComponentStatus::UnknownComponentType;
            result.message = "Authored component type is not registered.";
            return result;
        }

        ActorComponentInstanceRecord record =
            metadata.value_or(defaultActorComponentInstanceRecord(componentId, ownerActorId, *descriptor));
        record.componentId = componentId;
        record.ownerActorId = ownerActorId;
        record.componentType = componentType;

        ActorComponentValidationResult validation = validateActorComponentInstanceRecord(record);
        if (descriptor->validateInstance) {
            mergeValidation(validation, descriptor->validateInstance(record));
        }
        if (!validation.valid) {
            result.status = ActorComponentStatus::ValidationFailed;
            result.message = validation.errors.empty() ? "Authored component metadata is invalid." : validation.errors.front();
            return result;
        }

        std::string message;
        const ActorComponentStatus upsertStatus = store.upsert(record, &message);
        if (upsertStatus != ActorComponentStatus::Success) {
            result.status = upsertStatus;
            result.message = message;
            return result;
        }

        const SceneActorHandle ownerActor = findActorByStableId(scene, ownerActorId);
        if (!isValid(ownerActor)) {
            (void)store.remove(componentId);
            result.status = ActorComponentStatus::MissingActor;
            result.message = "Authored component owner actor was not found in the Scene.";
            return result;
        }

        result.sceneComponent = scene.attachComponent(ownerActor, componentType);
        if (!isValid(result.sceneComponent)) {
            (void)store.remove(componentId);
            result.status = ActorComponentStatus::SceneRejected;
            result.message = "Scene rejected authored component attachment.";
            return result;
        }
        return result;
    }

    bool removeAuthoredComponent(ActorComponentDescriptorStore& store, ActorComponentId componentId)
    {
        return store.remove(componentId);
    }

    uint32_t pruneMissingActorComponents(
        ActorComponentDescriptorStore& store,
        const ActorAuthoringStore& actorAuthoring)
    {
        return store.pruneMissingActors(actorAuthoring);
    }

    ActorComponentRuntimeBindingResult bindAuthoredComponents(
        const ActorComponentRuntimeBindingContext& context,
        const ActorComponentDescriptorRegistry& registry,
        const ActorComponentDescriptorStore& store)
    {
        return invokeCallbacks(context, registry, store, &ActorComponentRuntimeBindingCallbacks::bind);
    }

    ActorComponentRuntimeBindingResult unbindAuthoredComponents(
        const ActorComponentRuntimeBindingContext& context,
        const ActorComponentDescriptorRegistry& registry,
        const ActorComponentDescriptorStore& store)
    {
        return invokeCallbacks(context, registry, store, &ActorComponentRuntimeBindingCallbacks::unbind);
    }

    ActorComponentRuntimeBindingResult applyAuthoredComponentDescriptors(
        const ActorComponentRuntimeBindingContext& context,
        const ActorComponentDescriptorRegistry& registry,
        const ActorComponentDescriptorStore& store)
    {
        return invokeCallbacks(context, registry, store, &ActorComponentRuntimeBindingCallbacks::applyDescriptor);
    }

    void registerActorComponentAuthoringReflectionDescriptors(ReflectionRegistry& registry)
    {
        ReflectedObjectDescriptor object;
        object.id = static_cast<uint32_t>(ActorComponentReflectedObjectId::AuthoredComponentMetadata);
        object.name = "AuthoredComponentMetadata";
        object.displayName = "Authored Component Metadata";
        object.category = "Actor Components";
        object.properties = {
            property(ActorComponentReflectedPropertyId::ComponentId, "componentId", ReflectedValueType::UInt64, StableReadOnlyFlags, uint64_t{}),
            property(ActorComponentReflectedPropertyId::OwnerActorId, "ownerActorId", ReflectedValueType::SceneObjectId, StableReadOnlyFlags, SceneObjectId{}),
            property(ActorComponentReflectedPropertyId::ComponentType, "componentType", ReflectedValueType::UInt64, StableReadOnlyFlags, uint64_t{}),
            property(ActorComponentReflectedPropertyId::DisplayName, "displayName", ReflectedValueType::String, MetadataFlags, std::string{}),
            property(ActorComponentReflectedPropertyId::Enabled, "enabled", ReflectedValueType::Bool, MetadataFlags, true),
            property(ActorComponentReflectedPropertyId::Order, "order", ReflectedValueType::UInt64, MetadataFlags, uint64_t{}),
        };
        [[maybe_unused]] const ReflectionStatus status = registry.registerObject(std::move(object));
    }

    ReflectionResult getAuthoredComponentMetadata(
        const ActorComponentAuthoringReflectionContext& context,
        ActorComponentId componentId,
        ActorComponentReflectedPropertyId property)
    {
        if (!context.store || !isValid(componentId)) {
            return result(ReflectionStatus::InvalidHandle);
        }
        const std::optional<ActorComponentInstanceRecord> record = context.store->record(componentId);
        if (!record) {
            return result(ReflectionStatus::InvalidHandle, "Authored component metadata was not found.");
        }

        switch (property) {
            case ActorComponentReflectedPropertyId::ComponentId:
                return valueResult(record->componentId.value);
            case ActorComponentReflectedPropertyId::OwnerActorId:
                return valueResult(record->ownerActorId);
            case ActorComponentReflectedPropertyId::ComponentType:
                return valueResult(static_cast<uint64_t>(record->componentType.value));
            case ActorComponentReflectedPropertyId::DisplayName:
                return valueResult(record->displayName);
            case ActorComponentReflectedPropertyId::Enabled:
                return valueResult(record->enabled);
            case ActorComponentReflectedPropertyId::Order:
                return valueResult(static_cast<uint64_t>(record->order));
            default:
                return result(ReflectionStatus::UnknownProperty);
        }
    }

    ReflectionResult setAuthoredComponentMetadata(
        const ActorComponentAuthoringReflectionContext& context,
        ActorComponentId componentId,
        ActorComponentReflectedPropertyId property,
        const ReflectedValue& value)
    {
        if (!context.store || !isValid(componentId)) {
            return result(ReflectionStatus::InvalidHandle);
        }
        std::optional<ActorComponentInstanceRecord> record = context.store->record(componentId);
        if (!record) {
            return result(ReflectionStatus::InvalidHandle, "Authored component metadata was not found.");
        }

        switch (property) {
            case ActorComponentReflectedPropertyId::ComponentId:
            case ActorComponentReflectedPropertyId::OwnerActorId:
            case ActorComponentReflectedPropertyId::ComponentType:
                return result(ReflectionStatus::ReadOnly);
            case ActorComponentReflectedPropertyId::DisplayName: {
                const std::string* displayName = std::get_if<std::string>(&value);
                if (!displayName) {
                    return result(ReflectionStatus::TypeMismatch);
                }
                record->displayName = *displayName;
                break;
            }
            case ActorComponentReflectedPropertyId::Enabled: {
                const bool* enabled = std::get_if<bool>(&value);
                if (!enabled) {
                    return result(ReflectionStatus::TypeMismatch);
                }
                record->enabled = *enabled;
                break;
            }
            case ActorComponentReflectedPropertyId::Order: {
                const uint64_t* order = std::get_if<uint64_t>(&value);
                if (!order) {
                    return result(ReflectionStatus::TypeMismatch);
                }
                if (*order > std::numeric_limits<uint32_t>::max()) {
                    return result(ReflectionStatus::ValidationFailed, "Authored component order is out of range.");
                }
                record->order = static_cast<uint32_t>(*order);
                break;
            }
            default:
                return result(ReflectionStatus::UnknownProperty);
        }

        std::string message;
        const ActorComponentStatus status = context.store->upsert(*record, &message);
        if (status != ActorComponentStatus::Success) {
            return result(ReflectionStatus::ValidationFailed, message);
        }
        ReflectionResult reflection;
        reflection.changed = true;
        return reflection;
    }
}
