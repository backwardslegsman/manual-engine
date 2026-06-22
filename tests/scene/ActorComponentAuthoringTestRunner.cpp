#include "Engine/ActorComponentAuthoring.hpp"

#include <iostream>
#include <string>
#include <utility>
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

    Engine::ActorComponentTypeDescriptor descriptor(
        Engine::SceneComponentTypeId type = Engine::SceneComponentTypeId{10},
        std::vector<std::string>* calls = nullptr)
    {
        Engine::ActorComponentTypeDescriptor descriptor;
        descriptor.type = type;
        descriptor.typeName = "test.component";
        descriptor.displayName = "Test Component";
        descriptor.category = "Tests";
        descriptor.documentation = "Fake component type used by actor component authoring tests.";
        descriptor.defaultInstance.displayName = "Test Component";
        descriptor.validateInstance = [](const Engine::ActorComponentInstanceRecord& record) {
            Engine::ActorComponentValidationResult result;
            if (record.displayName == "bad") {
                result.valid = false;
                result.errors.push_back("bad display name rejected by fake descriptor");
            }
            return result;
        };
        if (calls) {
            descriptor.runtime.bind = [calls](const Engine::ActorComponentRuntimeBindingContext& context,
                                              const Engine::ActorComponentInstanceRecord& record) {
                calls->push_back("bind:" + std::to_string(record.componentId.value) +
                    ":" + std::to_string(context.scene != nullptr));
                return Engine::ActorComponentRuntimeBindingResult{};
            };
            descriptor.runtime.unbind = [calls](const Engine::ActorComponentRuntimeBindingContext&,
                                                const Engine::ActorComponentInstanceRecord& record) {
                calls->push_back("unbind:" + std::to_string(record.componentId.value));
                return Engine::ActorComponentRuntimeBindingResult{};
            };
            descriptor.runtime.applyDescriptor = [calls](const Engine::ActorComponentRuntimeBindingContext&,
                                                         const Engine::ActorComponentInstanceRecord& record) {
                calls->push_back("apply:" + std::to_string(record.componentId.value));
                return Engine::ActorComponentRuntimeBindingResult{};
            };
        }
        return descriptor;
    }

    void RegistryValidation(TestContext& ctx)
    {
        Engine::ActorComponentDescriptorRegistry registry;
        std::string message;
        ctx.expect(
            registry.registerType(descriptor(), &message) == Engine::ActorComponentStatus::Success,
            "valid descriptor was rejected");
        ctx.expect(registry.contains(Engine::SceneComponentTypeId{10}), "registered descriptor was not found");
        ctx.expect(
            registry.registerType(descriptor(), &message) == Engine::ActorComponentStatus::DuplicateComponentId,
            "duplicate descriptor type was accepted");

        Engine::ActorComponentTypeDescriptor invalid = descriptor(Engine::SceneComponentTypeId{11});
        invalid.displayName.clear();
        ctx.expect(
            registry.registerType(invalid, &message) == Engine::ActorComponentStatus::InvalidInput,
            "incomplete descriptor metadata was accepted");

        invalid = descriptor(Engine::SceneComponentTypeId{12});
        invalid.defaultInstance.displayName = "bad";
        ctx.expect(
            registry.registerType(invalid, &message) == Engine::ActorComponentStatus::ValidationFailed,
            "invalid descriptor default metadata was accepted");
    }

    void CreateAndEnumerateComponents(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ActorAuthoringStore actors;
        const Engine::ActorAuthoringCreateResult actor =
            Engine::createAuthoredActor(scene, actors, Engine::SceneObjectId{100});
        ctx.expect(actor.status == Engine::ActorAuthoringStatus::Success, "authored actor creation failed");

        Engine::ActorComponentDescriptorRegistry registry;
        (void)registry.registerType(descriptor());
        Engine::ActorComponentDescriptorStore components;

        const Engine::ActorComponentOperationResult invalid =
            Engine::createAuthoredComponent(
                scene,
                actors,
                registry,
                components,
                {},
                Engine::SceneObjectId{100},
                Engine::SceneComponentTypeId{10});
        ctx.expect(invalid.status == Engine::ActorComponentStatus::InvalidInput, "invalid component ID was accepted");

        const Engine::ActorComponentOperationResult missingActor =
            Engine::createAuthoredComponent(
                scene,
                actors,
                registry,
                components,
                Engine::ActorComponentId{1},
                Engine::SceneObjectId{999},
                Engine::SceneComponentTypeId{10});
        ctx.expect(missingActor.status == Engine::ActorComponentStatus::MissingActor, "missing owner was accepted");

        const Engine::ActorComponentOperationResult first =
            Engine::createAuthoredComponent(
                scene,
                actors,
                registry,
                components,
                Engine::ActorComponentId{1},
                Engine::SceneObjectId{100},
                Engine::SceneComponentTypeId{10});
        ctx.expect(first.status == Engine::ActorComponentStatus::Success, "valid component was rejected");
        ctx.expect(Engine::isValid(first.sceneComponent), "scene component was not attached");
        ctx.expect(scene.components(actor.actor).size() == 1, "scene component count mismatch");

        Engine::ActorComponentInstanceRecord secondRecord =
            Engine::defaultActorComponentInstanceRecord(
                Engine::ActorComponentId{2},
                Engine::SceneObjectId{100},
                *registry.descriptor(Engine::SceneComponentTypeId{10}));
        secondRecord.order = 0;
        secondRecord.displayName = "Second";
        const Engine::ActorComponentOperationResult second =
            Engine::createAuthoredComponent(
                scene,
                actors,
                registry,
                components,
                Engine::ActorComponentId{2},
                Engine::SceneObjectId{100},
                Engine::SceneComponentTypeId{10},
                secondRecord);
        ctx.expect(second.status == Engine::ActorComponentStatus::Success, "second same-type component was rejected");

        const Engine::ActorComponentOperationResult duplicate =
            Engine::createAuthoredComponent(
                scene,
                actors,
                registry,
                components,
                Engine::ActorComponentId{2},
                Engine::SceneObjectId{100},
                Engine::SceneComponentTypeId{10});
        ctx.expect(duplicate.status == Engine::ActorComponentStatus::DuplicateComponentId, "duplicate component ID was accepted");

        const std::vector<Engine::ActorComponentInstanceRecord> records = components.records();
        ctx.expect(records.size() == 2, "component store enumeration count mismatch");
        ctx.expect(records[0].componentId.value == 1 && records[1].componentId.value == 2, "component enumeration was not deterministic");

        (void)actors.remove(Engine::SceneObjectId{100});
        ctx.expect(
            Engine::pruneMissingActorComponents(components, actors) == 2,
            "missing actor components were not pruned");
    }

    void ReflectionMetadata(TestContext& ctx)
    {
        Engine::ActorComponentDescriptorStore store;
        Engine::ActorComponentInstanceRecord record;
        record.componentId = {55};
        record.ownerActorId = {100};
        record.componentType = {10};
        record.displayName = "Reflected";
        record.enabled = true;
        record.order = 7;
        (void)store.upsert(record);

        Engine::ReflectionRegistry registry;
        Engine::registerActorComponentAuthoringReflectionDescriptors(registry);
        const auto* object = registry.object(
            static_cast<uint32_t>(Engine::ActorComponentReflectedObjectId::AuthoredComponentMetadata));
        ctx.expect(object != nullptr, "component metadata reflection object was not registered");
        ctx.expect(object && object->properties.size() == 6, "component metadata property count mismatch");
        const auto* idProperty = registry.property(object->id, static_cast<uint32_t>(Engine::ActorComponentReflectedPropertyId::ComponentId));
        ctx.expect(idProperty && Engine::hasFlag(idProperty->flags, Engine::ReflectedPropertyFlag::ReadOnly), "component ID is not read-only");

        Engine::ActorComponentAuthoringReflectionContext context{&store};
        const Engine::ReflectionResult name = Engine::getAuthoredComponentMetadata(
            context,
            Engine::ActorComponentId{55},
            Engine::ActorComponentReflectedPropertyId::DisplayName);
        ctx.expect(std::get<std::string>(name.value) == "Reflected", "reflected display name mismatch");

        const Engine::ReflectionResult setName = Engine::setAuthoredComponentMetadata(
            context,
            Engine::ActorComponentId{55},
            Engine::ActorComponentReflectedPropertyId::DisplayName,
            std::string{"Edited"});
        ctx.expect(setName.status == Engine::ReflectionStatus::Success && setName.changed, "valid reflected edit failed");
        ctx.expect(store.record(Engine::ActorComponentId{55})->displayName == "Edited", "valid reflected edit did not commit");

        const Engine::ReflectionResult badOrder = Engine::setAuthoredComponentMetadata(
            context,
            Engine::ActorComponentId{55},
            Engine::ActorComponentReflectedPropertyId::Order,
            std::string{"wrong"});
        ctx.expect(badOrder.status == Engine::ReflectionStatus::TypeMismatch, "invalid order type was accepted");
        ctx.expect(store.record(Engine::ActorComponentId{55})->order == 7, "invalid reflected edit mutated order");

        const Engine::ReflectionResult readOnly = Engine::setAuthoredComponentMetadata(
            context,
            Engine::ActorComponentId{55},
            Engine::ActorComponentReflectedPropertyId::ComponentId,
            uint64_t{99});
        ctx.expect(readOnly.status == Engine::ReflectionStatus::ReadOnly, "component ID reflected write was accepted");
    }

    void RuntimeCallbacks(TestContext& ctx)
    {
        std::vector<std::string> calls;
        Engine::ActorComponentDescriptorRegistry registry;
        (void)registry.registerType(descriptor(Engine::SceneComponentTypeId{10}, &calls));

        Engine::ActorComponentDescriptorStore store;
        Engine::ActorComponentInstanceRecord first;
        first.componentId = {2};
        first.ownerActorId = {100};
        first.componentType = {10};
        first.displayName = "Second";
        first.order = 2;
        (void)store.upsert(first);

        Engine::ActorComponentInstanceRecord second = first;
        second.componentId = {1};
        second.displayName = "First";
        second.order = 1;
        (void)store.upsert(second);

        Engine::Scene scene;
        Engine::ActorComponentRuntimeBindingContext context;
        context.scene = &scene;
        const Engine::ActorComponentRuntimeBindingResult bind = Engine::bindAuthoredComponents(context, registry, store);
        const Engine::ActorComponentRuntimeBindingResult apply = Engine::applyAuthoredComponentDescriptors(context, registry, store);
        const Engine::ActorComponentRuntimeBindingResult unbind = Engine::unbindAuthoredComponents(context, registry, store);
        ctx.expect(bind.status == Engine::ActorComponentStatus::Success, "bind callback failed");
        ctx.expect(apply.status == Engine::ActorComponentStatus::Success, "apply callback failed");
        ctx.expect(unbind.status == Engine::ActorComponentStatus::Success, "unbind callback failed");
        const std::vector<std::string> expected{
            "bind:1:1",
            "bind:2:1",
            "apply:1",
            "apply:2",
            "unbind:1",
            "unbind:2",
        };
        ctx.expect(calls == expected, "runtime callbacks were not invoked in deterministic order");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"RegistryValidation", RegistryValidation},
        {"CreateAndEnumerateComponents", CreateAndEnumerateComponents},
        {"ReflectionMetadata", ReflectionMetadata},
        {"RuntimeCallbacks", RuntimeCallbacks},
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

    std::cout << "Actor component authoring tests passed (" << tests.size() << " tests)\n";
    return 0;
}
