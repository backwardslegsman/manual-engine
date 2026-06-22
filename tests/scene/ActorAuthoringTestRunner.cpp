#include "Engine/ActorAuthoring.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
    struct TestFailure {
        std::string testName;
        std::string message;
    };

    struct TestContext {
        std::string name;
        std::vector<TestFailure>& failures;

        void expect(bool condition, std::string message)
        {
            if (!condition) {
                failures.push_back({name, std::move(message)});
            }
        }
    };

    void CreateRequiresValidUniqueIds(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ActorAuthoringStore store;

        const Engine::ActorAuthoringCreateResult invalid =
            Engine::createAuthoredActor(scene, store, {});
        ctx.expect(invalid.status == Engine::ActorAuthoringStatus::InvalidInput, "invalid actor ID was accepted");

        Engine::ActorAuthoringRecord metadata = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{100});
        metadata.displayName = "NPC";
        metadata.tags = {"npc", "friendly"};
        metadata.layer = "Characters";
        const Engine::ActorAuthoringCreateResult created =
            Engine::createAuthoredActor(scene, store, Engine::SceneObjectId{100}, metadata);
        ctx.expect(created.status == Engine::ActorAuthoringStatus::Success, "valid authored actor was rejected");
        ctx.expect(scene.contains(created.actor), "scene actor was not created");
        ctx.expect(store.contains(Engine::SceneObjectId{100}), "metadata record was not inserted");

        const Engine::ActorAuthoringCreateResult duplicate =
            Engine::createAuthoredActor(scene, store, Engine::SceneObjectId{100});
        ctx.expect(duplicate.status == Engine::ActorAuthoringStatus::DuplicateActorId, "duplicate actor ID was accepted");
    }

    void StoreEnumerationAndPruneAreStable(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ActorAuthoringStore store;
        const auto actor200 = Engine::createAuthoredActor(scene, store, Engine::SceneObjectId{200}).actor;
        (void)Engine::createAuthoredActor(scene, store, Engine::SceneObjectId{100});

        const std::vector<Engine::ActorAuthoringRecord> records = store.records();
        ctx.expect(records.size() == 2, "unexpected metadata record count");
        ctx.expect(records[0].actorId.value == 100 && records[1].actorId.value == 200, "records were not sorted by stable ID");

        (void)scene.destroyActor(actor200);
        (void)scene.flushDestroyedActors();
        ctx.expect(store.pruneMissingActors(scene) == 1, "prune did not remove destroyed actor metadata");
        ctx.expect(store.contains(Engine::SceneObjectId{100}), "prune removed valid actor metadata");
        ctx.expect(!store.contains(Engine::SceneObjectId{200}), "prune kept missing actor metadata");
    }

    void ValidationRejectsInvalidMetadata(TestContext& ctx)
    {
        Engine::ActorAuthoringRecord invalid = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{300});
        invalid.tags = {"Guard", "guard"};
        ctx.expect(!Engine::validateActorAuthoringRecord(invalid).valid, "duplicate normalized tags were accepted");

        invalid = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{300});
        invalid.tags = {"bad,tag"};
        ctx.expect(!Engine::validateActorAuthoringRecord(invalid).valid, "comma tag was accepted");

        invalid = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{300});
        invalid.layer.clear();
        ctx.expect(!Engine::validateActorAuthoringRecord(invalid).valid, "empty layer was accepted");

        Engine::Scene scene;
        Engine::ActorAuthoringStore store;
        (void)store.upsert(Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{999}));
        ctx.expect(!Engine::validateActorAuthoringStore(store, &scene).valid, "orphan metadata was accepted");
    }

    void ReflectionReadsAndWritesMetadata(TestContext& ctx)
    {
        Engine::ActorAuthoringStore store;
        Engine::ActorAuthoringRecord record = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{400});
        record.tags = {"npc"};
        (void)store.upsert(record);

        Engine::ReflectionRegistry registry;
        Engine::registerActorAuthoringReflectionDescriptors(registry);
        const auto* object = registry.object(static_cast<uint32_t>(Engine::ActorAuthoringReflectedObjectId::AuthoredActorMetadata));
        ctx.expect(object != nullptr, "actor authoring reflection object missing");
        ctx.expect(object && object->properties.size() == 4, "unexpected actor authoring property count");
        for (const Engine::ReflectedPropertyDescriptor& property : object->properties) {
            ctx.expect(Engine::hasFlag(property.flags, Engine::ReflectedPropertyFlag::EditorVisible), "property missing editor flag");
            ctx.expect(Engine::hasFlag(property.flags, Engine::ReflectedPropertyFlag::ScriptVisible), "property missing script flag");
            ctx.expect(Engine::hasFlag(property.flags, Engine::ReflectedPropertyFlag::Serializable), "property missing serializable flag");
        }

        Engine::ActorAuthoringReflectionContext context{&store};
        Engine::ReflectionResult result = Engine::getAuthoredActorMetadata(
            context,
            Engine::SceneObjectId{400},
            Engine::ActorAuthoringReflectedPropertyId::Tags);
        ctx.expect(result.status == Engine::ReflectionStatus::Success, "tag read failed");
        ctx.expect(std::get<std::string>(result.value) == "npc", "tag string read mismatch");

        result = Engine::setAuthoredActorMetadata(
            context,
            Engine::SceneObjectId{400},
            Engine::ActorAuthoringReflectedPropertyId::DisplayName,
            std::string{"Merchant"});
        ctx.expect(result.status == Engine::ReflectionStatus::Success && result.changed, "display name write failed");
        ctx.expect(store.record(Engine::SceneObjectId{400})->displayName == "Merchant", "display name was not updated");

        result = Engine::setAuthoredActorMetadata(
            context,
            Engine::SceneObjectId{400},
            Engine::ActorAuthoringReflectedPropertyId::Tags,
            std::string{"npc, NPC"});
        ctx.expect(result.status == Engine::ReflectionStatus::ValidationFailed, "duplicate reflected tags were accepted");
        ctx.expect(store.record(Engine::SceneObjectId{400})->tags.size() == 1, "invalid reflected write mutated tags");

        result = Engine::setAuthoredActorMetadata(
            context,
            Engine::SceneObjectId{400},
            Engine::ActorAuthoringReflectedPropertyId::ActorId,
            Engine::SceneObjectId{401});
        ctx.expect(result.status == Engine::ReflectionStatus::ReadOnly, "actor ID was writable");
    }

    void TagsConvertThroughStableString(TestContext& ctx)
    {
        const std::vector<std::string> tags{"npc", "quest_giver", "friendly"};
        const std::string encoded = Engine::actorAuthoringTagsToString(tags);
        std::string message;
        const std::optional<std::vector<std::string>> decoded =
            Engine::actorAuthoringTagsFromString(encoded, &message);
        ctx.expect(decoded.has_value(), "valid tag string did not parse");
        ctx.expect(decoded == tags, "tag string did not round trip");

        const std::optional<std::vector<std::string>> invalid =
            Engine::actorAuthoringTagsFromString("npc,npc", &message);
        ctx.expect(!invalid.has_value(), "duplicate tag string parsed successfully");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"CreateRequiresValidUniqueIds", CreateRequiresValidUniqueIds},
        {"StoreEnumerationAndPruneAreStable", StoreEnumerationAndPruneAreStable},
        {"ValidationRejectsInvalidMetadata", ValidationRejectsInvalidMetadata},
        {"ReflectionReadsAndWritesMetadata", ReflectionReadsAndWritesMetadata},
        {"TagsConvertThroughStableString", TagsConvertThroughStableString},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Actor authoring tests passed (" << tests.size() << " tests)\n";
    return 0;
}
