#include "Engine/SceneWorldMigrationBridge.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

#include <glm/gtc/quaternion.hpp>

namespace Engine {
    namespace {
        [[nodiscard]] SceneTransform sceneTransformFromWorld(const Transform& transform)
        {
            SceneTransform sceneTransform;
            sceneTransform.translation = transform.position;
            sceneTransform.rotation = glm::quat{transform.rotation};
            sceneTransform.scale = transform.scale;
            return sceneTransform;
        }

        [[nodiscard]] Transform worldTransformFromScene(const SceneTransform& transform)
        {
            Transform worldTransform;
            worldTransform.position = transform.translation;
            worldTransform.rotation = glm::eulerAngles(transform.rotation);
            worldTransform.scale = transform.scale;
            return worldTransform;
        }

        [[nodiscard]] bool validBounds(const Renderer::Aabb& bounds)
        {
            return bounds.max.x > bounds.min.x && bounds.max.y > bounds.min.y && bounds.max.z > bounds.min.z;
        }

        [[nodiscard]] uint64_t fnv1a(std::string_view text)
        {
            uint64_t hash = 1469598103934665603ull;
            for (const char c : text) {
                hash ^= static_cast<uint8_t>(c);
                hash *= 1099511628211ull;
            }
            return hash == 0 ? 1u : hash;
        }
    }

    SceneActorHandle SceneWorldMigrationBridge::mapObject(Scene& scene, const World& world, WorldObjectHandle object)
    {
        if (!world.isValid(object)) {
            noteFailure("Cannot map invalid world object.");
            return {};
        }
        if (SceneWorldObjectMapping* existing = mappingForObject(object)) {
            return existing->actor;
        }

        const SceneActorHandle actor = scene.createActor(SceneObjectId{stableSceneIdForObject(world, object)});
        if (!isValid(actor)) {
            noteFailure("Scene rejected mapped actor creation.");
            return {};
        }
        if (const std::optional<Transform> transform = world.transform(object)) {
            (void)scene.setLocalTransform(actor, sceneTransformFromWorld(*transform));
        }
        mappings_.push_back({object, actor});
        refreshDiagnostics();
        return actor;
    }

    bool SceneWorldMigrationBridge::unmapObject(
        Scene& scene,
        ScenePhysicsWorld* physics,
        SceneCharacterMovementSystem* characters,
        WorldObjectHandle object)
    {
        const auto it = std::find_if(
            mappings_.begin(),
            mappings_.end(),
            [object](const SceneWorldObjectMapping& mapping) { return mapping.object.id == object.id; });
        if (it == mappings_.end()) {
            return false;
        }
        if (characters && it->character) {
            (void)characters->destroyCharacter(*it->character);
        }
        if (physics && it->staticBody) {
            (void)physics->destroyBody(*it->staticBody);
        }
        (void)scene.destroyActor(it->actor);
        mappings_.erase(it);
        scene.flushDestroyedActors();
        refreshDiagnostics();
        return true;
    }

    std::optional<SceneActorHandle> SceneWorldMigrationBridge::actorForObject(WorldObjectHandle object) const
    {
        if (const SceneWorldObjectMapping* mapping = mappingForObject(object)) {
            return mapping->actor;
        }
        return std::nullopt;
    }

    std::optional<WorldObjectHandle> SceneWorldMigrationBridge::objectForActor(SceneActorHandle actor) const
    {
        const auto it = std::find_if(
            mappings_.begin(),
            mappings_.end(),
            [actor](const SceneWorldObjectMapping& mapping) { return mapping.actor == actor; });
        if (it != mappings_.end()) {
            return it->object;
        }
        return std::nullopt;
    }

    std::vector<SceneWorldObjectMapping> SceneWorldMigrationBridge::mappings() const
    {
        return mappings_;
    }

    SceneCharacterHandle SceneWorldMigrationBridge::bindCharacter(
        SceneCharacterMovementSystem& characters,
        WorldObjectHandle object,
        SceneCharacterDescriptor descriptor)
    {
        SceneWorldObjectMapping* mapping = mappingForObject(object);
        if (!mapping) {
            noteFailure("Cannot bind character for unmapped world object.");
            return {};
        }
        descriptor.actor = mapping->actor;
        const SceneCharacterHandle character = characters.createCharacter(std::move(descriptor));
        if (!isValid(character)) {
            noteFailure("Scene character creation failed for mapped world object.");
            return {};
        }
        mapping->character = character;
        refreshDiagnostics();
        return character;
    }

    bool SceneWorldMigrationBridge::createStaticBoxColliderForObject(
        ScenePhysicsWorld& physics,
        const World& world,
        WorldObjectHandle object,
        ScenePhysicsLayer layer,
        ScenePhysicsMaterial material)
    {
        SceneWorldObjectMapping* mapping = mappingForObject(object);
        if (!mapping || !world.isValid(object)) {
            noteFailure("Cannot create static collider for unmapped or invalid world object.");
            return false;
        }
        if (mapping->staticBody && physics.contains(*mapping->staticBody)) {
            return true;
        }
        const std::optional<Renderer::Aabb> bounds = world.worldBounds(object);
        if (!bounds || !validBounds(*bounds)) {
            noteFailure("Cannot create static collider without valid world bounds.");
            return false;
        }

        ScenePhysicsBodyDescriptor bodyDescriptor;
        bodyDescriptor.actor = mapping->actor;
        bodyDescriptor.motionType = ScenePhysicsMotionType::Static;
        bodyDescriptor.layer = layer;
        bodyDescriptor.material = material;
        const ScenePhysicsBodyHandle body = physics.createBody(bodyDescriptor);
        if (!isValid(body)) {
            noteFailure("Scene physics rejected static body for world object.");
            return false;
        }

        ScenePhysicsShapeDescriptor shape;
        shape.type = ScenePhysicsShapeType::Box;
        shape.box.halfExtents = (bounds->max - bounds->min) * 0.5f;
        const SceneColliderHandle collider = physics.attachCollider(body, shape);
        if (!isValid(collider)) {
            (void)physics.destroyBody(body);
            noteFailure("Scene physics rejected static box collider for world object.");
            return false;
        }

        mapping->staticBody = body;
        mapping->staticCollider = collider;
        refreshDiagnostics();
        return true;
    }

    bool SceneWorldMigrationBridge::destroyStaticColliderForObject(ScenePhysicsWorld& physics, WorldObjectHandle object)
    {
        SceneWorldObjectMapping* mapping = mappingForObject(object);
        if (!mapping || !mapping->staticBody) {
            return false;
        }
        const bool destroyed = physics.destroyBody(*mapping->staticBody);
        mapping->staticBody.reset();
        mapping->staticCollider.reset();
        refreshDiagnostics();
        return destroyed;
    }

    void SceneWorldMigrationBridge::syncWorldToScene(Scene& scene, const World& world)
    {
        for (SceneWorldObjectMapping& mapping : mappings_) {
            if (!world.isValid(mapping.object) || !scene.contains(mapping.actor)) {
                continue;
            }
            if (const std::optional<Transform> transform = world.transform(mapping.object)) {
                (void)scene.setLocalTransform(mapping.actor, sceneTransformFromWorld(*transform));
            }
        }
    }

    void SceneWorldMigrationBridge::syncSceneToWorld(const Scene& scene, World& world) const
    {
        for (const SceneWorldObjectMapping& mapping : mappings_) {
            if (!world.isValid(mapping.object) || !scene.contains(mapping.actor)) {
                continue;
            }
            if (const std::optional<SceneTransform> transform = scene.localTransform(mapping.actor)) {
                world.setTransform(mapping.object, worldTransformFromScene(*transform));
            }
        }
    }

    void SceneWorldMigrationBridge::cleanupInvalidMappings(
        Scene& scene,
        const World& world,
        ScenePhysicsWorld* physics,
        SceneCharacterMovementSystem* characters)
    {
        for (size_t index = 0; index < mappings_.size();) {
            const bool worldValid = world.isValid(mappings_[index].object);
            const bool sceneValid = scene.contains(mappings_[index].actor);
            if (worldValid && sceneValid) {
                ++index;
                continue;
            }
            if (!worldValid) {
                ++diagnostics_.invalidWorldObjectCleanupCount;
            }
            if (!sceneValid) {
                ++diagnostics_.invalidSceneActorCleanupCount;
            }
            if (characters && mappings_[index].character) {
                (void)characters->destroyCharacter(*mappings_[index].character);
            }
            if (physics && mappings_[index].staticBody) {
                (void)physics->destroyBody(*mappings_[index].staticBody);
            }
            if (sceneValid) {
                (void)scene.destroyActor(mappings_[index].actor);
            }
            mappings_.erase(mappings_.begin() + static_cast<std::ptrdiff_t>(index));
            scene.flushDestroyedActors();
        }
        refreshDiagnostics();
    }

    void SceneWorldMigrationBridge::clear(Scene& scene, ScenePhysicsWorld* physics, SceneCharacterMovementSystem* characters)
    {
        for (SceneWorldObjectMapping& mapping : mappings_) {
            if (characters && mapping.character) {
                (void)characters->destroyCharacter(*mapping.character);
            }
            if (physics && mapping.staticBody) {
                (void)physics->destroyBody(*mapping.staticBody);
            }
            if (scene.contains(mapping.actor)) {
                (void)scene.destroyActor(mapping.actor);
            }
        }
        mappings_.clear();
        scene.flushDestroyedActors();
        refreshDiagnostics();
    }

    SceneWorldMigrationDiagnostics SceneWorldMigrationBridge::diagnostics() const
    {
        return diagnostics_;
    }

    SceneWorldObjectMapping* SceneWorldMigrationBridge::mappingForObject(WorldObjectHandle object)
    {
        const auto it = std::find_if(
            mappings_.begin(),
            mappings_.end(),
            [object](const SceneWorldObjectMapping& mapping) { return mapping.object.id == object.id; });
        return it == mappings_.end() ? nullptr : &*it;
    }

    const SceneWorldObjectMapping* SceneWorldMigrationBridge::mappingForObject(WorldObjectHandle object) const
    {
        const auto it = std::find_if(
            mappings_.begin(),
            mappings_.end(),
            [object](const SceneWorldObjectMapping& mapping) { return mapping.object.id == object.id; });
        return it == mappings_.end() ? nullptr : &*it;
    }

    uint64_t SceneWorldMigrationBridge::stableSceneIdForObject(const World& world, WorldObjectHandle object) const
    {
        const std::optional<ObjectId> id = world.objectId(object);
        const std::string key = id && id->isValid()
            ? id->toString()
            : std::string{"world-object/"} + std::to_string(object.id);
        return fnv1a(key);
    }

    void SceneWorldMigrationBridge::noteFailure(std::string message)
    {
        ++diagnostics_.failedRequestCount;
        diagnostics_.lastMessage = std::move(message);
    }

    void SceneWorldMigrationBridge::refreshDiagnostics()
    {
        diagnostics_.mappedObjectCount = static_cast<uint32_t>(mappings_.size());
        diagnostics_.characterBindingCount = 0;
        diagnostics_.staticColliderBindingCount = 0;
        for (const SceneWorldObjectMapping& mapping : mappings_) {
            diagnostics_.characterBindingCount += mapping.character.has_value() ? 1u : 0u;
            diagnostics_.staticColliderBindingCount += mapping.staticBody.has_value() ? 1u : 0u;
        }
    }
}
