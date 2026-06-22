#include "Engine/ActorAuthoring.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
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

        [[nodiscard]] std::string trim(const std::string& value)
        {
            size_t begin = 0;
            while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
                ++begin;
            }
            size_t end = value.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
                --end;
            }
            return value.substr(begin, end - begin);
        }

        [[nodiscard]] std::string lowerAscii(std::string value)
        {
            for (char& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        [[nodiscard]] bool sceneContainsStableId(const Scene& scene, SceneObjectId actorId)
        {
            bool found = false;
            scene.forEachActor([&](SceneActorHandle actor) {
                found = found || scene.stableId(actor).value_or(SceneObjectId{}) == actorId;
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
            ActorAuthoringReflectedPropertyId id,
            std::string name,
            ReflectedValueType type,
            ReflectedPropertyFlag flags,
            ReflectedValue defaultValue = {})
        {
            ReflectedPropertyDescriptor descriptor;
            descriptor.id = static_cast<uint32_t>(id);
            descriptor.name = std::move(name);
            descriptor.displayName = descriptor.name;
            descriptor.category = "Actor Authoring";
            descriptor.type = type;
            descriptor.flags = flags;
            descriptor.defaultValue = std::move(defaultValue);
            return descriptor;
        }
    }

    bool ActorAuthoringStore::contains(SceneObjectId actorId) const
    {
        return std::ranges::any_of(records_, [actorId](const ActorAuthoringRecord& record) {
            return record.actorId == actorId;
        });
    }

    std::optional<ActorAuthoringRecord> ActorAuthoringStore::record(SceneObjectId actorId) const
    {
        const auto it = std::ranges::find_if(records_, [actorId](const ActorAuthoringRecord& candidate) {
            return candidate.actorId == actorId;
        });
        if (it == records_.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::vector<ActorAuthoringRecord> ActorAuthoringStore::records() const
    {
        std::vector<ActorAuthoringRecord> sorted = records_;
        std::ranges::sort(sorted, [](const ActorAuthoringRecord& lhs, const ActorAuthoringRecord& rhs) {
            return lhs.actorId.value < rhs.actorId.value;
        });
        return sorted;
    }

    ActorAuthoringStatus ActorAuthoringStore::upsert(ActorAuthoringRecord record, std::string* message)
    {
        const ActorAuthoringValidationResult validation = validateActorAuthoringRecord(record);
        if (!validation.valid) {
            if (message && !validation.errors.empty()) {
                *message = validation.errors.front();
            }
            return ActorAuthoringStatus::ValidationFailed;
        }

        const auto it = std::ranges::find_if(records_, [record](const ActorAuthoringRecord& candidate) {
            return candidate.actorId == record.actorId;
        });
        if (it == records_.end()) {
            records_.push_back(std::move(record));
        } else {
            *it = std::move(record);
        }
        return ActorAuthoringStatus::Success;
    }

    bool ActorAuthoringStore::remove(SceneObjectId actorId)
    {
        const auto size = records_.size();
        records_.erase(
            std::remove_if(records_.begin(), records_.end(), [actorId](const ActorAuthoringRecord& record) {
                return record.actorId == actorId;
            }),
            records_.end());
        return records_.size() != size;
    }

    void ActorAuthoringStore::clear()
    {
        records_.clear();
    }

    uint32_t ActorAuthoringStore::pruneMissingActors(const Scene& scene)
    {
        const auto before = records_.size();
        records_.erase(
            std::remove_if(records_.begin(), records_.end(), [&scene](const ActorAuthoringRecord& record) {
                return !sceneContainsStableId(scene, record.actorId);
            }),
            records_.end());
        return static_cast<uint32_t>(before - records_.size());
    }

    ActorAuthoringRecord defaultActorAuthoringRecord(SceneObjectId actorId)
    {
        ActorAuthoringRecord record;
        record.actorId = actorId;
        record.displayName = isValid(actorId) ? "Actor " + std::to_string(actorId.value) : "Actor";
        record.layer = "Default";
        return record;
    }

    ActorAuthoringValidationResult validateActorAuthoringRecord(const ActorAuthoringRecord& record)
    {
        ActorAuthoringValidationResult result;
        auto error = [&](std::string message) {
            result.valid = false;
            result.errors.push_back(std::move(message));
        };

        if (!isValid(record.actorId)) {
            error("Actor authoring metadata requires a valid SceneObjectId.");
        }
        if (record.displayName.size() > ActorAuthoringMaxDisplayNameBytes || hasControlCharacter(record.displayName)) {
            error("Actor display name is invalid.");
        }
        if (record.layer.empty() ||
            record.layer.size() > ActorAuthoringMaxLayerBytes ||
            record.layer.find(',') != std::string::npos ||
            hasControlCharacter(record.layer)) {
            error("Actor layer is invalid.");
        }
        if (record.tags.size() > ActorAuthoringMaxTagCount) {
            error("Actor tag count exceeds the supported limit.");
        }

        std::set<std::string> normalizedTags;
        for (const std::string& tag : record.tags) {
            const std::string trimmed = trim(tag);
            if (trimmed.empty() ||
                trimmed.size() > ActorAuthoringMaxTagBytes ||
                trimmed.find(',') != std::string::npos ||
                hasControlCharacter(trimmed)) {
                error("Actor tag is invalid.");
                continue;
            }
            if (!normalizedTags.insert(lowerAscii(trimmed)).second) {
                error("Actor tags contain a duplicate normalized value.");
            }
        }
        return result;
    }

    ActorAuthoringValidationResult validateActorAuthoringStore(const ActorAuthoringStore& store, const Scene* scene)
    {
        ActorAuthoringValidationResult result;
        auto merge = [&](const ActorAuthoringValidationResult& validation) {
            if (!validation.valid) {
                result.valid = false;
            }
            result.errors.insert(result.errors.end(), validation.errors.begin(), validation.errors.end());
            result.warnings.insert(result.warnings.end(), validation.warnings.begin(), validation.warnings.end());
        };

        std::set<uint64_t> ids;
        for (const ActorAuthoringRecord& record : store.records()) {
            merge(validateActorAuthoringRecord(record));
            if (isValid(record.actorId) && !ids.insert(record.actorId.value).second) {
                result.valid = false;
                result.errors.push_back("Actor authoring store contains duplicate SceneObjectId metadata.");
            }
            if (scene && isValid(record.actorId) && !sceneContainsStableId(*scene, record.actorId)) {
                result.valid = false;
                result.errors.push_back("Actor authoring metadata references an actor missing from the Scene.");
            }
        }
        return result;
    }

    ActorAuthoringCreateResult createAuthoredActor(
        Scene& scene,
        ActorAuthoringStore& store,
        SceneObjectId actorId,
        std::optional<ActorAuthoringRecord> metadata)
    {
        ActorAuthoringCreateResult result;
        if (!isValid(actorId)) {
            result.status = ActorAuthoringStatus::InvalidInput;
            result.message = "Authored actor creation requires a valid SceneObjectId.";
            return result;
        }
        if (sceneContainsStableId(scene, actorId) || store.contains(actorId)) {
            result.status = ActorAuthoringStatus::DuplicateActorId;
            result.message = "Authored actor SceneObjectId already exists.";
            return result;
        }

        ActorAuthoringRecord record = metadata.value_or(defaultActorAuthoringRecord(actorId));
        record.actorId = actorId;
        std::string message;
        const ActorAuthoringStatus upsertStatus = store.upsert(record, &message);
        if (upsertStatus != ActorAuthoringStatus::Success) {
            result.status = upsertStatus;
            result.message = message;
            return result;
        }

        result.actor = scene.createActor(actorId);
        if (!isValid(result.actor)) {
            (void)store.remove(actorId);
            result.status = ActorAuthoringStatus::MissingActor;
            result.message = "Scene rejected authored actor creation.";
            return result;
        }
        return result;
    }

    std::string actorAuthoringTagsToString(const std::vector<std::string>& tags)
    {
        std::ostringstream stream;
        for (size_t index = 0; index < tags.size(); ++index) {
            if (index != 0) {
                stream << ',';
            }
            stream << tags[index];
        }
        return stream.str();
    }

    std::optional<std::vector<std::string>> actorAuthoringTagsFromString(const std::string& tags, std::string* message)
    {
        std::vector<std::string> parsed;
        size_t begin = 0;
        while (begin <= tags.size()) {
            const size_t comma = tags.find(',', begin);
            const size_t end = comma == std::string::npos ? tags.size() : comma;
            const std::string token = trim(tags.substr(begin, end - begin));
            if (!token.empty()) {
                parsed.push_back(token);
            }
            if (comma == std::string::npos) {
                break;
            }
            begin = comma + 1;
        }

        ActorAuthoringRecord probe = defaultActorAuthoringRecord(SceneObjectId{1});
        probe.tags = parsed;
        const ActorAuthoringValidationResult validation = validateActorAuthoringRecord(probe);
        if (!validation.valid) {
            if (message && !validation.errors.empty()) {
                *message = validation.errors.front();
            }
            return std::nullopt;
        }
        return parsed;
    }

    void registerActorAuthoringReflectionDescriptors(ReflectionRegistry& registry)
    {
        ReflectedObjectDescriptor object;
        object.id = static_cast<uint32_t>(ActorAuthoringReflectedObjectId::AuthoredActorMetadata);
        object.name = "AuthoredActorMetadata";
        object.displayName = "Authored Actor Metadata";
        object.category = "Actor Authoring";
        object.properties = {
            property(ActorAuthoringReflectedPropertyId::ActorId, "actorId", ReflectedValueType::SceneObjectId, StableReadOnlyFlags, SceneObjectId{}),
            property(ActorAuthoringReflectedPropertyId::DisplayName, "displayName", ReflectedValueType::String, MetadataFlags, std::string{}),
            property(ActorAuthoringReflectedPropertyId::Layer, "layer", ReflectedValueType::String, MetadataFlags, std::string{"Default"}),
            property(ActorAuthoringReflectedPropertyId::Tags, "tags", ReflectedValueType::String, MetadataFlags, std::string{}),
        };
        [[maybe_unused]] const ReflectionStatus status = registry.registerObject(std::move(object));
    }

    ReflectionResult getAuthoredActorMetadata(
        const ActorAuthoringReflectionContext& context,
        SceneObjectId actorId,
        ActorAuthoringReflectedPropertyId property)
    {
        if (!context.store || !isValid(actorId)) {
            return result(ReflectionStatus::InvalidHandle);
        }
        const std::optional<ActorAuthoringRecord> record = context.store->record(actorId);
        if (!record) {
            return result(ReflectionStatus::InvalidHandle, "Authored actor metadata was not found.");
        }

        switch (property) {
            case ActorAuthoringReflectedPropertyId::ActorId:
                return valueResult(record->actorId);
            case ActorAuthoringReflectedPropertyId::DisplayName:
                return valueResult(record->displayName);
            case ActorAuthoringReflectedPropertyId::Layer:
                return valueResult(record->layer);
            case ActorAuthoringReflectedPropertyId::Tags:
                return valueResult(actorAuthoringTagsToString(record->tags));
            default:
                return result(ReflectionStatus::UnknownProperty);
        }
    }

    ReflectionResult setAuthoredActorMetadata(
        const ActorAuthoringReflectionContext& context,
        SceneObjectId actorId,
        ActorAuthoringReflectedPropertyId property,
        const ReflectedValue& value)
    {
        if (!context.store || !isValid(actorId)) {
            return result(ReflectionStatus::InvalidHandle);
        }
        std::optional<ActorAuthoringRecord> record = context.store->record(actorId);
        if (!record) {
            return result(ReflectionStatus::InvalidHandle, "Authored actor metadata was not found.");
        }

        switch (property) {
            case ActorAuthoringReflectedPropertyId::ActorId:
                return result(ReflectionStatus::ReadOnly);
            case ActorAuthoringReflectedPropertyId::DisplayName: {
                const std::string* displayName = std::get_if<std::string>(&value);
                if (!displayName) {
                    return result(ReflectionStatus::TypeMismatch);
                }
                record->displayName = *displayName;
                break;
            }
            case ActorAuthoringReflectedPropertyId::Layer: {
                const std::string* layer = std::get_if<std::string>(&value);
                if (!layer) {
                    return result(ReflectionStatus::TypeMismatch);
                }
                record->layer = *layer;
                break;
            }
            case ActorAuthoringReflectedPropertyId::Tags: {
                const std::string* tags = std::get_if<std::string>(&value);
                if (!tags) {
                    return result(ReflectionStatus::TypeMismatch);
                }
                std::string message;
                std::optional<std::vector<std::string>> parsed = actorAuthoringTagsFromString(*tags, &message);
                if (!parsed) {
                    return result(ReflectionStatus::ValidationFailed, message);
                }
                record->tags = std::move(*parsed);
                break;
            }
            default:
                return result(ReflectionStatus::UnknownProperty);
        }

        std::string message;
        const ActorAuthoringStatus status = context.store->upsert(*record, &message);
        if (status != ActorAuthoringStatus::Success) {
            return result(ReflectionStatus::ValidationFailed, message);
        }
        ReflectionResult reflection;
        reflection.changed = true;
        return reflection;
    }
}
