#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Engine/ActorAuthoring.hpp"
#include "Engine/ActorComponentAuthoring.hpp"
#include "Engine/Reflection.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneSerialization.hpp"
#include "Engine/TerrainSerializationPrep.hpp"

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

    std::filesystem::path testRoot()
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() / "manual_engine_scene_serialization_tests";
        std::filesystem::create_directories(root);
        return root;
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!bytes.empty()) {
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        return bytes;
    }

    void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void writeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        for (uint32_t shift = 0; shift < 32; shift += 8) {
            bytes[offset++] = static_cast<uint8_t>((value >> shift) & 0xffu);
        }
    }

    void writeU64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
    {
        for (uint32_t shift = 0; shift < 64; shift += 8) {
            bytes[offset++] = static_cast<uint8_t>((value >> shift) & 0xffu);
        }
    }

    Engine::ReflectionRegistry serializableRegistry()
    {
        Engine::ReflectionRegistry registry;
        Engine::ReflectedObjectDescriptor object;
        object.id = 1000;
        object.name = "SerializableFixture";

        Engine::ReflectedPropertyDescriptor serializable;
        serializable.id = 1;
        serializable.name = "stableName";
        serializable.type = Engine::ReflectedValueType::String;
        serializable.defaultValue = std::string{};
        serializable.flags = Engine::ReflectedPropertyFlag::Serializable;
        object.properties.push_back(serializable);

        Engine::ReflectedPropertyDescriptor runtimeOnly;
        runtimeOnly.id = 2;
        runtimeOnly.name = "runtimeOnly";
        runtimeOnly.type = Engine::ReflectedValueType::Float;
        runtimeOnly.defaultValue = 0.0f;
        runtimeOnly.flags = Engine::ReflectedPropertyFlag::Serializable | Engine::ReflectedPropertyFlag::RuntimeOnly;
        object.properties.push_back(runtimeOnly);

        Engine::ReflectedPropertyDescriptor opaque;
        opaque.id = 3;
        opaque.name = "opaque";
        opaque.type = Engine::ReflectedValueType::OpaqueHandle;
        opaque.defaultValue = Engine::OpaqueHandle{};
        opaque.flags = Engine::ReflectedPropertyFlag::Serializable;
        object.properties.push_back(opaque);

        (void)registry.registerObject(object);
        return registry;
    }

    Engine::ActorComponentTypeDescriptor componentDescriptor(Engine::SceneComponentTypeId type = Engine::SceneComponentTypeId{700})
    {
        Engine::ActorComponentTypeDescriptor descriptor;
        descriptor.type = type;
        descriptor.typeName = "test.serialized_component";
        descriptor.displayName = "Serialized Component";
        descriptor.category = "Tests";
        descriptor.documentation = "Fake component type for scene serialization tests.";
        descriptor.defaultInstance.displayName = "Serialized Component";
        return descriptor;
    }

    Engine::SceneSerializedScene fixtureSnapshot(Engine::ReflectionRegistry& registry)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor(Engine::SceneObjectId{100});
        const Engine::SceneActorHandle child = scene.createActor(Engine::SceneObjectId{200});
        Engine::SceneTransform parentTransform;
        parentTransform.translation = {1.0f, 2.0f, 3.0f};
        (void)scene.setLocalTransform(parent, parentTransform);
        Engine::SceneTransform childTransform;
        childTransform.translation = {4.0f, 5.0f, 6.0f};
        childTransform.scale = {2.0f, 2.0f, 2.0f};
        (void)scene.setLocalTransform(child, childTransform);
        (void)scene.attachChild(child, parent, false);
        (void)scene.attachComponent(parent, Engine::SceneComponentTypeId{77});
        (void)scene.attachComponent(child, Engine::SceneComponentTypeId{88});
        return Engine::buildSerializedScene(scene, registry);
    }

    void HeaderAndDirectoryRoundTrip(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::SceneSerializedScene snapshot = fixtureSnapshot(registry);
        const std::filesystem::path path = testRoot() / "roundtrip.mescene";

        const Engine::SceneSerializationWriteResult write = Engine::writeSceneBinary(path, snapshot);
        ctx.expect(write.status == Engine::SceneSerializationStatus::Success, "write failed");
        ctx.expect(write.header.headerSize == 72, "header size mismatch");
        ctx.expect(write.header.directoryCount == write.directory.size(), "directory count mismatch");

        const Engine::SceneSerializationHeaderReadResult header = Engine::readSceneBinaryHeader(path);
        ctx.expect(header.status == Engine::SceneSerializationStatus::Success, "header read failed");
        ctx.expect(header.header.formatVersion == Engine::SceneBinaryNumericVersion, "format version mismatch");
        ctx.expect(header.directory.size() == write.directory.size(), "header-only directory was not read");

        const Engine::SceneSerializationReadResult read = Engine::readSceneBinary(path);
        ctx.expect(read.status == Engine::SceneSerializationStatus::Success, "full read failed");
        ctx.expect(read.scene.actors.size() == 2, "actor count did not round trip");
        ctx.expect(read.scene.components.size() == 2, "component count did not round trip");
        ctx.expect(read.scene.schema.size() == 1, "runtime-only or opaque schema properties were not skipped");
    }

    void SceneRestoreRoundTrip(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::SceneSerializedScene snapshot = fixtureSnapshot(registry);
        const std::filesystem::path path = testRoot() / "restore.mescene";
        (void)Engine::writeSceneBinary(path, snapshot);
        const Engine::SceneSerializationReadResult read = Engine::readSceneBinary(path);

        Engine::Scene restored;
        const Engine::SceneSerializationStatus status = Engine::applySerializedScene(restored, read.scene);
        ctx.expect(status == Engine::SceneSerializationStatus::Success, "apply failed");

        std::vector<Engine::SceneActorHandle> actors;
        restored.forEachActor([&](Engine::SceneActorHandle actor) {
            actors.push_back(actor);
        });
        ctx.expect(actors.size() == 2, "restored actor count mismatch");

        Engine::SceneActorHandle parent;
        Engine::SceneActorHandle child;
        for (Engine::SceneActorHandle actor : actors) {
            const Engine::SceneObjectId id = restored.stableId(actor).value_or(Engine::SceneObjectId{});
            if (id.value == 100) {
                parent = actor;
            } else if (id.value == 200) {
                child = actor;
            }
        }
        ctx.expect(Engine::isValid(parent) && Engine::isValid(child), "stable actor IDs were not restored");
        ctx.expect(restored.parent(child).has_value() && *restored.parent(child) == parent, "hierarchy was not restored");
        ctx.expect(restored.components(parent).size() == 1, "parent component was not restored");
        ctx.expect(restored.components(child).size() == 1, "child component was not restored");
    }

    void DeterministicOutput(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        const Engine::SceneSerializedScene snapshot = fixtureSnapshot(registry);
        const std::filesystem::path first = testRoot() / "deterministic_a.mescene";
        const std::filesystem::path second = testRoot() / "deterministic_b.mescene";
        (void)Engine::writeSceneBinary(first, snapshot);
        (void)Engine::writeSceneBinary(second, snapshot);
        ctx.expect(readBytes(first) == readBytes(second), "identical input did not produce identical bytes");
    }

    void InvalidSnapshotsAreRejected(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::Scene scene;
        (void)scene.createActor();
        const Engine::SceneSerializedScene missingId = Engine::buildSerializedScene(scene, registry);
        ctx.expect(!Engine::validateSerializedScene(missingId, registry).errors.empty(), "missing actor stable ID was not rejected");

        Engine::SceneSerializedScene duplicate;
        duplicate.actors.push_back({Engine::SceneObjectId{1}, std::nullopt, {}, 0});
        duplicate.actors.push_back({Engine::SceneObjectId{1}, std::nullopt, {}, 1});
        ctx.expect(!Engine::validateSerializedScene(duplicate, registry).errors.empty(), "duplicate actor IDs were not rejected");

        Engine::SceneSerializedScene invalidParent;
        invalidParent.actors.push_back({Engine::SceneObjectId{1}, Engine::SceneObjectId{99}, {}, 0});
        ctx.expect(!Engine::validateSerializedScene(invalidParent, registry).errors.empty(), "invalid parent ID was not rejected");

        Engine::SceneSerializedScene invalidComponent;
        invalidComponent.actors.push_back({Engine::SceneObjectId{1}, std::nullopt, {}, 0});
        invalidComponent.components.push_back({Engine::SceneObjectId{2}, Engine::SceneComponentTypeId{4}, 0});
        ctx.expect(!Engine::validateSerializedScene(invalidComponent, registry).errors.empty(), "missing component owner was not rejected");
        invalidComponent.components[0].owner = Engine::SceneObjectId{1};
        invalidComponent.components[0].type = {};
        ctx.expect(!Engine::validateSerializedScene(invalidComponent, registry).errors.empty(), "invalid component type was not rejected");
    }

    void AssetAndTerrainReferencesUseDurableIdentity(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::SceneSerializedScene snapshot = fixtureSnapshot(registry);
        Engine::SceneSerializedAssetReference asset;
        asset.id = Engine::AssetId{700};
        asset.type = Engine::AssetType::Texture;
        asset.importSettings = {"texture", "v1", "abc"};
        asset.sourcePath = "assets/example.png";
        snapshot.assets.push_back(asset);

        Engine::TerrainChunkStableIdentity identity;
        identity.chunkId = {Engine::AssetId{800}, {3, -2}};
        identity.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        identity.importSettings = {"heightmap", "v1", "def"};
        identity.sourceRevision = "source";
        identity.materialRevision = "material";
        identity.chunkResolution = 5;
        identity.chunkSize = 16.0f;
        Engine::SceneSerializedTerrainReference terrain;
        terrain.metadata = Engine::buildTerrainSerializedChunkFileMetadata(identity);
        snapshot.terrain.push_back(terrain);

        const std::filesystem::path path = testRoot() / "references.mescene";
        const Engine::SceneSerializationWriteResult write = Engine::writeSceneBinary(path, snapshot);
        ctx.expect(write.status == Engine::SceneSerializationStatus::Success, "reference write failed");
        const Engine::SceneSerializationReadResult read = Engine::readSceneBinary(path);
        ctx.expect(read.status == Engine::SceneSerializationStatus::Success, "reference read failed");
        ctx.expect(read.scene.assets.size() == 1 && read.scene.assets[0].id == asset.id, "asset ID did not round trip");
        ctx.expect(read.scene.terrain.size() == 1 && read.scene.terrain[0].metadata.identity.chunkId == identity.chunkId, "terrain stable chunk ID did not round trip");
    }

    void ActorAuthoringMetadataRoundTrip(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::Scene scene;
        Engine::ActorAuthoringStore authoring;

        const Engine::SceneActorHandle parent = scene.createActor(Engine::SceneObjectId{1000});
        const Engine::SceneActorHandle child = scene.createActor(Engine::SceneObjectId{2000});
        (void)scene.attachChild(child, parent, false);

        Engine::ActorAuthoringRecord parentMetadata = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{1000});
        parentMetadata.displayName = "Village Elder";
        parentMetadata.layer = "Characters";
        parentMetadata.tags = {"npc", "quest_giver"};
        (void)authoring.upsert(parentMetadata);

        Engine::ActorAuthoringRecord childMetadata = Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{2000});
        childMetadata.displayName = "Torch";
        childMetadata.layer = "Props";
        childMetadata.tags = {"prop"};
        (void)authoring.upsert(childMetadata);

        const Engine::SceneSerializedScene snapshot = Engine::buildSerializedScene(scene, authoring, registry);
        ctx.expect(snapshot.actorAuthoring.size() == 2, "actor authoring metadata was not serialized");

        const std::filesystem::path path = testRoot() / "actor_authoring.mescene";
        const Engine::SceneSerializationWriteResult write = Engine::writeSceneBinary(path, snapshot);
        ctx.expect(write.status == Engine::SceneSerializationStatus::Success, "actor authoring scene write failed");
        const Engine::SceneSerializationReadResult read = Engine::readSceneBinary(path);
        ctx.expect(read.status == Engine::SceneSerializationStatus::Success, "actor authoring scene read failed");
        ctx.expect(read.scene.actorAuthoring.size() == 2, "actor authoring metadata did not round trip");

        Engine::Scene restored;
        Engine::ActorAuthoringStore restoredAuthoring;
        const Engine::SceneSerializationStatus status =
            Engine::applySerializedScene(restored, restoredAuthoring, read.scene);
        ctx.expect(status == Engine::SceneSerializationStatus::Success, "actor authoring apply failed");
        const std::optional<Engine::ActorAuthoringRecord> restoredParent =
            restoredAuthoring.record(Engine::SceneObjectId{1000});
        ctx.expect(restoredParent.has_value(), "restored metadata missing");
        ctx.expect(restoredParent && restoredParent->displayName == "Village Elder", "restored display name mismatch");
        ctx.expect(restoredParent && restoredParent->tags.size() == 2, "restored tags mismatch");
    }

    void InvalidActorAuthoringMetadataRejected(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::SceneSerializedScene snapshot;
        snapshot.actors.push_back({Engine::SceneObjectId{1}, std::nullopt, {}, 0});
        snapshot.actorAuthoring.push_back({Engine::ActorAuthoringRecord{
            Engine::SceneObjectId{2},
            "Orphan",
            {},
            "Default",
        }});
        ctx.expect(!Engine::validateSerializedScene(snapshot, registry).errors.empty(), "orphan actor metadata was accepted");

        snapshot.actorAuthoring[0].metadata.actorId = Engine::SceneObjectId{1};
        snapshot.actorAuthoring[0].metadata.tags = {"tag", "TAG"};
        ctx.expect(!Engine::validateSerializedScene(snapshot, registry).errors.empty(), "invalid actor metadata was accepted");

        snapshot.actorAuthoring[0].metadata.tags = {"tag"};
        snapshot.actorAuthoring.push_back(snapshot.actorAuthoring[0]);
        ctx.expect(!Engine::validateSerializedScene(snapshot, registry).errors.empty(), "duplicate actor metadata was accepted");
    }

    void ActorComponentAuthoringMetadataRoundTrip(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::ActorComponentDescriptorRegistry componentRegistry;
        (void)componentRegistry.registerType(componentDescriptor());

        Engine::Scene scene;
        Engine::ActorAuthoringStore actorAuthoring;
        Engine::ActorComponentDescriptorStore componentAuthoring;
        const Engine::ActorAuthoringCreateResult actor =
            Engine::createAuthoredActor(scene, actorAuthoring, Engine::SceneObjectId{5000});
        ctx.expect(actor.status == Engine::ActorAuthoringStatus::Success, "authored actor creation failed");

        Engine::ActorComponentInstanceRecord metadata =
            Engine::defaultActorComponentInstanceRecord(
                Engine::ActorComponentId{9000},
                Engine::SceneObjectId{5000},
                *componentRegistry.descriptor(Engine::SceneComponentTypeId{700}));
        metadata.displayName = "Serialized Metadata";
        metadata.enabled = false;
        metadata.order = 3;

        const Engine::ActorComponentOperationResult component =
            Engine::createAuthoredComponent(
                scene,
                actorAuthoring,
                componentRegistry,
                componentAuthoring,
                Engine::ActorComponentId{9000},
                Engine::SceneObjectId{5000},
                Engine::SceneComponentTypeId{700},
                metadata);
        ctx.expect(component.status == Engine::ActorComponentStatus::Success, "authored component creation failed");

        const Engine::SceneSerializedScene snapshot =
            Engine::buildSerializedScene(scene, actorAuthoring, componentAuthoring, registry);
        ctx.expect(snapshot.components.size() == 1, "scene component record was not serialized");
        ctx.expect(snapshot.actorComponents.size() == 1, "authored component metadata was not serialized");

        const std::filesystem::path path = testRoot() / "actor_component_authoring.mescene";
        const Engine::SceneSerializationWriteResult write = Engine::writeSceneBinary(path, snapshot);
        ctx.expect(write.status == Engine::SceneSerializationStatus::Success, "component authoring scene write failed");
        const Engine::SceneSerializationReadResult read = Engine::readSceneBinary(path);
        ctx.expect(read.status == Engine::SceneSerializationStatus::Success, "component authoring scene read failed");
        ctx.expect(read.scene.actorComponents.size() == 1, "component authoring metadata did not round trip");

        Engine::Scene restored;
        Engine::ActorAuthoringStore restoredActors;
        Engine::ActorComponentDescriptorStore restoredComponents;
        const Engine::SceneSerializationStatus status =
            Engine::applySerializedScene(
                restored,
                restoredActors,
                restoredComponents,
                componentRegistry,
                read.scene);
        ctx.expect(status == Engine::SceneSerializationStatus::Success, "component authoring apply failed");
        const std::optional<Engine::ActorComponentInstanceRecord> restoredComponent =
            restoredComponents.record(Engine::ActorComponentId{9000});
        ctx.expect(restoredComponent.has_value(), "restored component metadata missing");
        ctx.expect(restoredComponent && restoredComponent->displayName == "Serialized Metadata", "restored component name mismatch");
        ctx.expect(restoredComponent && !restoredComponent->enabled, "restored component enabled flag mismatch");
    }

    void InvalidActorComponentAuthoringMetadataRejected(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        Engine::ActorComponentDescriptorRegistry componentRegistry;
        (void)componentRegistry.registerType(componentDescriptor());

        Engine::SceneSerializedScene snapshot;
        snapshot.actors.push_back({Engine::SceneObjectId{1}, std::nullopt, {}, 0});
        snapshot.actorAuthoring.push_back({Engine::defaultActorAuthoringRecord(Engine::SceneObjectId{1})});
        snapshot.actorComponents.push_back({Engine::ActorComponentInstanceRecord{
            Engine::ActorComponentId{100},
            Engine::SceneObjectId{2},
            Engine::SceneComponentTypeId{700},
            "Orphan Component",
            true,
            0,
        }});
        ctx.expect(
            !Engine::validateSerializedScene(snapshot, registry, componentRegistry).errors.empty(),
            "missing component owner was accepted");

        snapshot.actorComponents[0].metadata.ownerActorId = Engine::SceneObjectId{1};
        snapshot.actorComponents[0].metadata.componentType = Engine::SceneComponentTypeId{701};
        ctx.expect(
            !Engine::validateSerializedScene(snapshot, registry, componentRegistry).errors.empty(),
            "unregistered component type was accepted");

        snapshot.actorComponents[0].metadata.componentType = Engine::SceneComponentTypeId{700};
        snapshot.actorComponents.push_back(snapshot.actorComponents[0]);
        ctx.expect(
            !Engine::validateSerializedScene(snapshot, registry, componentRegistry).errors.empty(),
            "duplicate component ID was accepted");

        Engine::Scene scene;
        Engine::ActorAuthoringStore actors;
        Engine::ActorComponentDescriptorStore components;
        const Engine::SceneSerializationStatus status =
            Engine::applySerializedScene(scene, actors, components, componentRegistry, snapshot);
        ctx.expect(status == Engine::SceneSerializationStatus::InvalidReference, "invalid component metadata applied");
        ctx.expect(components.records().empty(), "invalid component metadata mutated target store");
    }

    void CorruptFilesFailCleanly(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry = serializableRegistry();
        const Engine::SceneSerializedScene snapshot = fixtureSnapshot(registry);
        const std::filesystem::path source = testRoot() / "corrupt_source.mescene";
        (void)Engine::writeSceneBinary(source, snapshot);

        std::vector<uint8_t> bytes = readBytes(source);
        std::vector<uint8_t> badMagic = bytes;
        badMagic[0] = 'X';
        const std::filesystem::path badMagicPath = testRoot() / "bad_magic.mescene";
        writeBytes(badMagicPath, badMagic);
        ctx.expect(Engine::readSceneBinaryHeader(badMagicPath).status == Engine::SceneSerializationStatus::CorruptHeader, "bad magic was not rejected");

        std::vector<uint8_t> badVersion = bytes;
        writeU32(badVersion, 16, 999);
        const std::filesystem::path badVersionPath = testRoot() / "bad_version.mescene";
        writeBytes(badVersionPath, badVersion);
        Engine::SceneSerializationSettings noChecksum;
        noChecksum.enableChecksums = false;
        ctx.expect(Engine::readSceneBinaryHeader(badVersionPath, noChecksum).status == Engine::SceneSerializationStatus::UnsupportedVersion, "bad version was not rejected");

        std::vector<uint8_t> badEndian = bytes;
        writeU32(badEndian, 12, 0x04030201u);
        const std::filesystem::path badEndianPath = testRoot() / "bad_endian.mescene";
        writeBytes(badEndianPath, badEndian);
        ctx.expect(Engine::readSceneBinaryHeader(badEndianPath, noChecksum).status == Engine::SceneSerializationStatus::CorruptHeader, "bad endian was not rejected");

        std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + 20);
        const std::filesystem::path truncatedPath = testRoot() / "truncated.mescene";
        writeBytes(truncatedPath, truncated);
        ctx.expect(Engine::readSceneBinaryHeader(truncatedPath).status == Engine::SceneSerializationStatus::CorruptHeader, "truncated file was not rejected");

        std::vector<uint8_t> overlap = bytes;
        const uint64_t firstPayloadOffset = 72 + 6 * 56;
        writeU64(overlap, 72 + 56 + 12, firstPayloadOffset);
        const std::filesystem::path overlapPath = testRoot() / "overlap.mescene";
        writeBytes(overlapPath, overlap);
        ctx.expect(Engine::readSceneBinaryHeader(overlapPath, noChecksum).status == Engine::SceneSerializationStatus::CorruptDirectory, "overlapping chunks were not rejected");

        std::vector<uint8_t> checksum = bytes;
        checksum.back() ^= 0xffu;
        const std::filesystem::path checksumPath = testRoot() / "checksum.mescene";
        writeBytes(checksumPath, checksum);
        ctx.expect(Engine::readSceneBinary(checksumPath).status == Engine::SceneSerializationStatus::ChecksumMismatch, "checksum mismatch was not rejected");
    }
}

int main()
{
    std::filesystem::remove_all(testRoot());
    std::vector<TestFailure> failures;

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"HeaderAndDirectoryRoundTrip", HeaderAndDirectoryRoundTrip},
        {"SceneRestoreRoundTrip", SceneRestoreRoundTrip},
        {"DeterministicOutput", DeterministicOutput},
        {"InvalidSnapshotsAreRejected", InvalidSnapshotsAreRejected},
        {"AssetAndTerrainReferencesUseDurableIdentity", AssetAndTerrainReferencesUseDurableIdentity},
        {"ActorAuthoringMetadataRoundTrip", ActorAuthoringMetadataRoundTrip},
        {"InvalidActorAuthoringMetadataRejected", InvalidActorAuthoringMetadataRejected},
        {"ActorComponentAuthoringMetadataRoundTrip", ActorComponentAuthoringMetadataRoundTrip},
        {"InvalidActorComponentAuthoringMetadataRejected", InvalidActorComponentAuthoringMetadataRejected},
        {"CorruptFilesFailCleanly", CorruptFilesFailCleanly},
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

    std::cout << "Scene serialization tests passed (" << tests.size() << " tests)\n";
    return 0;
}
