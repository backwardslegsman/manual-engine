#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneCharacterMovement.hpp"
#include "Engine/World.hpp"

namespace Engine {
    struct SceneWorldObjectMapping {
        WorldObjectHandle object;
        SceneActorHandle actor;
        std::optional<SceneCharacterHandle> character;
        std::optional<ScenePhysicsBodyHandle> staticBody;
        std::optional<SceneColliderHandle> staticCollider;
    };

    struct SceneWorldMigrationDiagnostics {
        uint32_t mappedObjectCount = 0;
        uint32_t characterBindingCount = 0;
        uint32_t staticColliderBindingCount = 0;
        uint32_t invalidWorldObjectCleanupCount = 0;
        uint32_t invalidSceneActorCleanupCount = 0;
        uint32_t failedRequestCount = 0;
        std::string lastMessage;
    };

    class SceneWorldMigrationBridge {
    public:
        [[nodiscard]] SceneActorHandle mapObject(Scene& scene, const World& world, WorldObjectHandle object);
        bool unmapObject(Scene& scene, ScenePhysicsWorld* physics, SceneCharacterMovementSystem* characters, WorldObjectHandle object);

        [[nodiscard]] std::optional<SceneActorHandle> actorForObject(WorldObjectHandle object) const;
        [[nodiscard]] std::optional<WorldObjectHandle> objectForActor(SceneActorHandle actor) const;
        [[nodiscard]] std::vector<SceneWorldObjectMapping> mappings() const;

        [[nodiscard]] SceneCharacterHandle bindCharacter(
            SceneCharacterMovementSystem& characters,
            WorldObjectHandle object,
            SceneCharacterDescriptor descriptor);
        bool createStaticBoxColliderForObject(
            ScenePhysicsWorld& physics,
            const World& world,
            WorldObjectHandle object,
            ScenePhysicsLayer layer = {},
            ScenePhysicsMaterial material = {});
        bool destroyStaticColliderForObject(ScenePhysicsWorld& physics, WorldObjectHandle object);

        void syncWorldToScene(Scene& scene, const World& world);
        void syncSceneToWorld(const Scene& scene, World& world) const;
        void cleanupInvalidMappings(
            Scene& scene,
            const World& world,
            ScenePhysicsWorld* physics = nullptr,
            SceneCharacterMovementSystem* characters = nullptr);
        void clear(Scene& scene, ScenePhysicsWorld* physics = nullptr, SceneCharacterMovementSystem* characters = nullptr);

        [[nodiscard]] SceneWorldMigrationDiagnostics diagnostics() const;

    private:
        [[nodiscard]] SceneWorldObjectMapping* mappingForObject(WorldObjectHandle object);
        [[nodiscard]] const SceneWorldObjectMapping* mappingForObject(WorldObjectHandle object) const;
        [[nodiscard]] uint64_t stableSceneIdForObject(const World& world, WorldObjectHandle object) const;
        void noteFailure(std::string message);
        void refreshDiagnostics();

        std::vector<SceneWorldObjectMapping> mappings_;
        SceneWorldMigrationDiagnostics diagnostics_;
    };
}
