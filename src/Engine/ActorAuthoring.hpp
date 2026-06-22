#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Engine {
    inline constexpr uint32_t ActorAuthoringMaxDisplayNameBytes = 128;
    inline constexpr uint32_t ActorAuthoringMaxLayerBytes = 64;
    inline constexpr uint32_t ActorAuthoringMaxTagBytes = 64;
    inline constexpr uint32_t ActorAuthoringMaxTagCount = 32;

    enum class ActorAuthoringStatus {
        Success,
        InvalidInput,
        DuplicateActorId,
        MissingActor,
        ValidationFailed,
    };

    enum class ActorAuthoringReflectedObjectId : uint32_t {
        AuthoredActorMetadata = 2000,
    };

    enum class ActorAuthoringReflectedPropertyId : uint32_t {
        ActorId = 1,
        DisplayName = 2,
        Layer = 3,
        Tags = 4,
    };

    struct ActorAuthoringRecord {
        SceneObjectId actorId;
        std::string displayName;
        std::vector<std::string> tags;
        std::string layer = "Default";
    };

    struct ActorAuthoringValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    struct ActorAuthoringCreateResult {
        ActorAuthoringStatus status = ActorAuthoringStatus::Success;
        std::string message;
        SceneActorHandle actor;
    };

    class ActorAuthoringStore {
    public:
        [[nodiscard]] bool contains(SceneObjectId actorId) const;
        [[nodiscard]] std::optional<ActorAuthoringRecord> record(SceneObjectId actorId) const;
        [[nodiscard]] std::vector<ActorAuthoringRecord> records() const;

        [[nodiscard]] ActorAuthoringStatus upsert(ActorAuthoringRecord record, std::string* message = nullptr);
        bool remove(SceneObjectId actorId);
        void clear();
        uint32_t pruneMissingActors(const Scene& scene);

    private:
        std::vector<ActorAuthoringRecord> records_;
    };

    struct ActorAuthoringReflectionContext {
        ActorAuthoringStore* store = nullptr;
    };

    [[nodiscard]] ActorAuthoringRecord defaultActorAuthoringRecord(SceneObjectId actorId);
    [[nodiscard]] ActorAuthoringValidationResult validateActorAuthoringRecord(const ActorAuthoringRecord& record);
    [[nodiscard]] ActorAuthoringValidationResult validateActorAuthoringStore(
        const ActorAuthoringStore& store,
        const Scene* scene = nullptr);

    [[nodiscard]] ActorAuthoringCreateResult createAuthoredActor(
        Scene& scene,
        ActorAuthoringStore& store,
        SceneObjectId actorId,
        std::optional<ActorAuthoringRecord> metadata = std::nullopt);

    [[nodiscard]] std::string actorAuthoringTagsToString(const std::vector<std::string>& tags);
    [[nodiscard]] std::optional<std::vector<std::string>> actorAuthoringTagsFromString(
        const std::string& tags,
        std::string* message = nullptr);

    void registerActorAuthoringReflectionDescriptors(ReflectionRegistry& registry);

    [[nodiscard]] ReflectionResult getAuthoredActorMetadata(
        const ActorAuthoringReflectionContext& context,
        SceneObjectId actorId,
        ActorAuthoringReflectedPropertyId property);
    [[nodiscard]] ReflectionResult setAuthoredActorMetadata(
        const ActorAuthoringReflectionContext& context,
        SceneObjectId actorId,
        ActorAuthoringReflectedPropertyId property,
        const ReflectedValue& value);
}
