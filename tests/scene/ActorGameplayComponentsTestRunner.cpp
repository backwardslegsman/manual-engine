#include "Engine/ActorGameplayComponents.hpp"

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

    Engine::ActorComponentInstanceRecord metadata(
        Engine::ActorComponentId id,
        Engine::SceneObjectId owner,
        Engine::SceneComponentTypeId type)
    {
        Engine::ActorComponentInstanceRecord record;
        record.componentId = id;
        record.ownerActorId = owner;
        record.componentType = type;
        record.displayName = "Component";
        return record;
    }

    struct Fixture {
        Engine::Scene scene;
        Engine::ActorAuthoringStore actors;
        Engine::ActorComponentDescriptorRegistry registry;
        Engine::ActorComponentDescriptorStore components;
        Engine::ActorStatsComponentStore stats;
        Engine::ActorMovementComponentStore movement;
        Engine::ActorSensoryComponentStore sensory;
        Engine::SceneActorHandle actor;

        Fixture()
        {
            Engine::registerBuiltInActorComponentTypes(registry);
            const Engine::ActorAuthoringCreateResult created =
                Engine::createAuthoredActor(scene, actors, Engine::SceneObjectId{100});
            actor = created.actor;
            Engine::SceneTransform transform;
            transform.translation = {0.0f, 2.0f, 0.0f};
            (void)scene.setLocalTransform(actor, transform);
        }
    };

    void BuiltInRegistration(TestContext& ctx)
    {
        Engine::ActorComponentDescriptorRegistry registry;
        Engine::registerBuiltInActorComponentTypes(registry);
        ctx.expect(registry.contains(Engine::ActorStatsComponentType), "Stats type was not registered");
        ctx.expect(registry.contains(Engine::ActorMovementComponentType), "Movement type was not registered");
        ctx.expect(registry.contains(Engine::ActorSensoryComponentType), "Sensory type was not registered");
        std::string message;
        Engine::ActorComponentTypeDescriptor duplicate = *registry.descriptor(Engine::ActorStatsComponentType);
        ctx.expect(
            registry.registerType(duplicate, &message) == Engine::ActorComponentStatus::DuplicateComponentId,
            "duplicate built-in component type was accepted");
    }

    void ValidationAndStores(TestContext& ctx)
    {
        Fixture fixture;
        (void)fixture.components.upsert(metadata({1}, {100}, Engine::ActorStatsComponentType));
        (void)fixture.components.upsert(metadata({2}, {100}, Engine::ActorMovementComponentType));
        (void)fixture.components.upsert(metadata({3}, {100}, Engine::ActorSensoryComponentType));
        ctx.expect(fixture.stats.upsert(Engine::defaultStatsComponentDescriptor({1})) == Engine::ActorComponentStatus::Success, "default Stats descriptor rejected");
        ctx.expect(fixture.movement.upsert(Engine::defaultMovementComponentDescriptor({2})) == Engine::ActorComponentStatus::Success, "default Movement descriptor rejected");
        ctx.expect(fixture.sensory.upsert(Engine::defaultSensoryComponentDescriptor({3})) == Engine::ActorComponentStatus::Success, "default Sensory descriptor rejected");
        ctx.expect(
            Engine::validateActorGameplayComponentStores(
                fixture.components,
                &fixture.stats,
                &fixture.movement,
                &fixture.sensory).valid,
            "valid gameplay component stores failed validation");

        Engine::StatsComponentDescriptor invalidStats = Engine::defaultStatsComponentDescriptor({4});
        invalidStats.currentHealth = 200.0f;
        ctx.expect(fixture.stats.upsert(invalidStats) == Engine::ActorComponentStatus::ValidationFailed, "invalid Stats descriptor accepted");

        Engine::MovementComponentDescriptor invalidMovement = Engine::defaultMovementComponentDescriptor({5});
        invalidMovement.radius = -1.0f;
        ctx.expect(fixture.movement.upsert(invalidMovement) == Engine::ActorComponentStatus::ValidationFailed, "invalid Movement descriptor accepted");

        Engine::SensoryComponentDescriptor invalidSensory = Engine::defaultSensoryComponentDescriptor({6});
        invalidSensory.fieldOfViewDegrees = 0.0f;
        ctx.expect(fixture.sensory.upsert(invalidSensory) == Engine::ActorComponentStatus::ValidationFailed, "invalid Sensory descriptor accepted");

        Engine::ActorStatsComponentStore orphanStats;
        (void)orphanStats.upsert(Engine::defaultStatsComponentDescriptor({99}));
        ctx.expect(
            !Engine::validateActorGameplayComponentStores(fixture.components, &orphanStats, nullptr, nullptr).valid,
            "orphan typed payload was accepted");

        Engine::ActorComponentDescriptorStore wrongGeneric;
        (void)wrongGeneric.upsert(metadata({1}, {100}, Engine::ActorMovementComponentType));
        ctx.expect(
            !Engine::validateActorGameplayComponentStores(wrongGeneric, &fixture.stats, nullptr, nullptr).valid,
            "typed payload with wrong generic component type was accepted");
    }

    void Reflection(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry;
        Engine::registerActorGameplayComponentReflectionDescriptors(registry);
        ctx.expect(registry.object(static_cast<uint32_t>(Engine::ActorGameplayComponentReflectedObjectId::StatsComponentDescriptor)) != nullptr, "Stats reflection object missing");
        ctx.expect(registry.object(static_cast<uint32_t>(Engine::ActorGameplayComponentReflectedObjectId::MovementComponentDescriptor)) != nullptr, "Movement reflection object missing");
        ctx.expect(registry.object(static_cast<uint32_t>(Engine::ActorGameplayComponentReflectedObjectId::SensoryComponentDescriptor)) != nullptr, "Sensory reflection object missing");

        Engine::ActorStatsComponentStore stats;
        (void)stats.upsert(Engine::defaultStatsComponentDescriptor({1}));
        Engine::ActorGameplayComponentReflectionContext context;
        context.stats = &stats;
        Engine::ReflectionResult setHealth = Engine::setStatsComponentDescriptor(
            context,
            {1},
            Engine::StatsComponentReflectedPropertyId::CurrentHealth,
            80.0f);
        ctx.expect(setHealth.status == Engine::ReflectionStatus::Success && setHealth.changed, "valid reflected Stats write failed");
        Engine::ReflectionResult invalidHealth = Engine::setStatsComponentDescriptor(
            context,
            {1},
            Engine::StatsComponentReflectedPropertyId::CurrentHealth,
            800.0f);
        ctx.expect(invalidHealth.status == Engine::ReflectionStatus::ValidationFailed, "invalid reflected Stats write accepted");
        ctx.expect(stats.descriptor({1})->currentHealth == 80.0f, "invalid reflected Stats write mutated descriptor");
    }

    void MovementBinding(TestContext& ctx)
    {
        Fixture fixture;
        Engine::ActorComponentInstanceRecord movementMeta = metadata({2}, {100}, Engine::ActorMovementComponentType);
        (void)fixture.components.upsert(movementMeta);
        Engine::MovementComponentDescriptor movement = Engine::defaultMovementComponentDescriptor({2});
        movement.debugName = "BoundCharacter";
        (void)fixture.movement.upsert(movement);

        Engine::ScenePhysicsWorld physics(fixture.scene);
        Engine::SceneCharacterMovementSystem characterMovement(fixture.scene, physics);
        Engine::ActorMovementRuntimeBindingStore runtimeBindings;
        Engine::ActorComponentRuntimeBindingContext context;
        context.scene = &fixture.scene;
        context.componentAuthoring = &fixture.components;
        context.movementComponents = &fixture.movement;
        context.movementRuntimeBindings = &runtimeBindings;
        context.characterMovement = &characterMovement;
        context.createCharacter = [&](Engine::SceneCharacterDescriptor descriptor) {
            return characterMovement.createCharacter(std::move(descriptor));
        };
        context.destroyCharacter = [&](Engine::SceneCharacterHandle character) {
            return characterMovement.destroyCharacter(character);
        };
        context.containsCharacter = [&](Engine::SceneCharacterHandle character) {
            return characterMovement.contains(character);
        };

        const Engine::ActorComponentRuntimeBindingResult bind =
            Engine::bindAuthoredComponents(context, fixture.registry, fixture.components);
        ctx.expect(bind.status == Engine::ActorComponentStatus::Success, "movement bind failed");
        ctx.expect(runtimeBindings.contains({2}), "movement bind did not record transient character mapping");
        const std::optional<Engine::SceneCharacterHandle> first = runtimeBindings.character({2});
        ctx.expect(first && characterMovement.contains(*first), "movement bind did not create character");

        movement.maxSpeed = 6.0f;
        (void)fixture.movement.upsert(movement);
        const Engine::ActorComponentRuntimeBindingResult apply =
            Engine::applyAuthoredComponentDescriptors(context, fixture.registry, fixture.components);
        const std::optional<Engine::SceneCharacterHandle> second = runtimeBindings.character({2});
        ctx.expect(apply.status == Engine::ActorComponentStatus::Success, "movement apply failed");
        ctx.expect(second && characterMovement.contains(*second), "movement apply did not recreate character");
        ctx.expect(first && second && *first != *second, "movement apply did not recreate the transient character");

        const Engine::ActorComponentRuntimeBindingResult unbind =
            Engine::unbindAuthoredComponents(context, fixture.registry, fixture.components);
        ctx.expect(unbind.status == Engine::ActorComponentStatus::Success, "movement unbind failed");
        ctx.expect(!runtimeBindings.contains({2}), "movement unbind did not clear transient mapping");
        ctx.expect(second && !characterMovement.contains(*second), "movement unbind did not destroy character");
    }

    void DataOnlyComponentsDoNotBind(TestContext& ctx)
    {
        Fixture fixture;
        (void)fixture.components.upsert(metadata({1}, {100}, Engine::ActorStatsComponentType));
        (void)fixture.components.upsert(metadata({3}, {100}, Engine::ActorSensoryComponentType));
        (void)fixture.stats.upsert(Engine::defaultStatsComponentDescriptor({1}));
        (void)fixture.sensory.upsert(Engine::defaultSensoryComponentDescriptor({3}));
        Engine::ScenePhysicsWorld physics(fixture.scene);
        Engine::SceneCharacterMovementSystem characterMovement(fixture.scene, physics);
        Engine::ActorMovementRuntimeBindingStore runtimeBindings;
        Engine::ActorComponentRuntimeBindingContext context;
        context.scene = &fixture.scene;
        context.statsComponents = &fixture.stats;
        context.sensoryComponents = &fixture.sensory;
        context.movementRuntimeBindings = &runtimeBindings;
        context.characterMovement = &characterMovement;
        context.createCharacter = [&](Engine::SceneCharacterDescriptor descriptor) {
            return characterMovement.createCharacter(std::move(descriptor));
        };
        context.destroyCharacter = [&](Engine::SceneCharacterHandle character) {
            return characterMovement.destroyCharacter(character);
        };
        context.containsCharacter = [&](Engine::SceneCharacterHandle character) {
            return characterMovement.contains(character);
        };
        const Engine::ActorComponentRuntimeBindingResult bind =
            Engine::bindAuthoredComponents(context, fixture.registry, fixture.components);
        ctx.expect(bind.status == Engine::ActorComponentStatus::Success, "data-only bind callbacks failed");
        ctx.expect(runtimeBindings.bindings().empty(), "data-only components created movement runtime bindings");
        ctx.expect(characterMovement.diagnostics().characterCount == 0, "data-only components created characters");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"BuiltInRegistration", BuiltInRegistration},
        {"ValidationAndStores", ValidationAndStores},
        {"Reflection", Reflection},
        {"MovementBinding", MovementBinding},
        {"DataOnlyComponentsDoNotBind", DataOnlyComponentsDoNotBind},
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

    std::cout << "Actor gameplay component tests passed (" << tests.size() << " tests)\n";
    return 0;
}
