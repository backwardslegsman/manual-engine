#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Scene/Scene.hpp"

namespace Engine {
    struct ScenePhysicsBodyHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneColliderHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct ScenePhysicsLayer {
        uint32_t value = 1;
    };

    enum class ScenePhysicsMotionType {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class ScenePhysicsShapeType {
        Box,
        Sphere,
        Capsule,
        StaticTriangleMesh,
    };

    enum class ScenePhysicsQueryStatus {
        Success,
        InvalidInput,
        NoWorld,
        NoHit,
        UnsupportedShape,
    };

    enum class ScenePhysicsDebugRequestType {
        BodyBounds,
        Raycast,
        Sweep,
        Overlap,
        ClosestPoint,
        FailedQuery,
    };

    struct ScenePhysicsMaterial {
        float friction = 0.5f;
        float restitution = 0.0f;
        float density = 1.0f;
    };

    struct ScenePhysicsFilter {
        uint32_t includeLayerMask = UINT32_MAX;
        uint32_t excludeLayerMask = 0;
        bool includeSensors = true;
    };

    struct ScenePhysicsBoxShape {
        glm::vec3 halfExtents{0.5f};
    };

    struct ScenePhysicsSphereShape {
        float radius = 0.5f;
    };

    struct ScenePhysicsCapsuleShape {
        float radius = 0.35f;
        float halfHeight = 0.5f;
    };

    struct ScenePhysicsTriangleMeshShape {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;
    };

    struct ScenePhysicsShapeDescriptor {
        ScenePhysicsShapeType type = ScenePhysicsShapeType::Box;
        ScenePhysicsBoxShape box;
        ScenePhysicsSphereShape sphere;
        ScenePhysicsCapsuleShape capsule;
        ScenePhysicsTriangleMeshShape triangleMesh;
    };

    struct ScenePhysicsBodyDescriptor {
        SceneActorHandle actor;
        ScenePhysicsMotionType motionType = ScenePhysicsMotionType::Static;
        bool enabled = true;
        bool sensor = false;
        ScenePhysicsLayer layer;
        ScenePhysicsMaterial material;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float gravityFactor = 1.0f;
        glm::vec3 initialLinearVelocity{0.0f};
        glm::vec3 initialAngularVelocity{0.0f};
    };

    struct ScenePhysicsHit {
        ScenePhysicsBodyHandle body;
        SceneColliderHandle collider;
        SceneActorHandle actor;
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
    };

    struct ScenePhysicsRaycastResult {
        ScenePhysicsQueryStatus status = ScenePhysicsQueryStatus::NoHit;
        std::string message;
        std::optional<ScenePhysicsHit> hit;
    };

    struct ScenePhysicsSweepResult {
        ScenePhysicsQueryStatus status = ScenePhysicsQueryStatus::NoHit;
        std::string message;
        std::optional<ScenePhysicsHit> hit;
    };

    struct ScenePhysicsOverlapResult {
        ScenePhysicsQueryStatus status = ScenePhysicsQueryStatus::NoHit;
        std::string message;
        std::vector<ScenePhysicsHit> hits;
    };

    struct ScenePhysicsClosestPointResult {
        ScenePhysicsQueryStatus status = ScenePhysicsQueryStatus::NoHit;
        std::string message;
        std::optional<ScenePhysicsHit> hit;
    };

    struct ScenePhysicsDebugRequest {
        ScenePhysicsDebugRequestType type = ScenePhysicsDebugRequestType::FailedQuery;
        ScenePhysicsQueryStatus status = ScenePhysicsQueryStatus::Success;
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 extents{0.0f};
        ScenePhysicsBodyHandle body;
        SceneColliderHandle collider;
    };

    struct ScenePhysicsDiagnostics {
        uint32_t bodyCount = 0;
        uint32_t colliderCount = 0;
        uint32_t activeBodyCount = 0;
        uint32_t invalidOwnerCleanupCount = 0;
        uint32_t raycastCount = 0;
        uint32_t sweepCount = 0;
        uint32_t overlapCount = 0;
        uint32_t closestPointCount = 0;
        uint64_t lastStepMicroseconds = 0;
        uint64_t lastQueryMicroseconds = 0;
        ScenePhysicsQueryStatus lastStatus = ScenePhysicsQueryStatus::Success;
        std::string lastMessage;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] constexpr bool isValid(ScenePhysicsBodyHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneColliderHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(ScenePhysicsBodyHandle lhs, ScenePhysicsBodyHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(ScenePhysicsBodyHandle lhs, ScenePhysicsBodyHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneColliderHandle lhs, SceneColliderHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneColliderHandle lhs, SceneColliderHandle rhs)
    {
        return !(lhs == rhs);
    }

    class ScenePhysicsWorld {
    public:
        explicit ScenePhysicsWorld(Scene& scene);
        ~ScenePhysicsWorld();

        ScenePhysicsWorld(const ScenePhysicsWorld&) = delete;
        ScenePhysicsWorld& operator=(const ScenePhysicsWorld&) = delete;

        [[nodiscard]] ScenePhysicsBodyHandle createBody(const ScenePhysicsBodyDescriptor& descriptor);
        bool destroyBody(ScenePhysicsBodyHandle body);
        [[nodiscard]] bool contains(ScenePhysicsBodyHandle body) const;
        [[nodiscard]] std::optional<ScenePhysicsBodyDescriptor> body(ScenePhysicsBodyHandle body) const;
        [[nodiscard]] std::optional<ScenePhysicsBodyHandle> bodyForActor(SceneActorHandle actor) const;

        [[nodiscard]] SceneColliderHandle attachCollider(
            ScenePhysicsBodyHandle body,
            const ScenePhysicsShapeDescriptor& shape);
        bool detachCollider(SceneColliderHandle collider);
        [[nodiscard]] bool contains(SceneColliderHandle collider) const;
        [[nodiscard]] std::vector<SceneColliderHandle> colliders(ScenePhysicsBodyHandle body) const;

        bool setBodyEnabled(ScenePhysicsBodyHandle body, bool enabled);
        bool setMotionType(ScenePhysicsBodyHandle body, ScenePhysicsMotionType motionType);
        bool setKinematicTarget(ScenePhysicsBodyHandle body, const glm::vec3& translation, const glm::quat& rotation);
        bool setLinearVelocity(ScenePhysicsBodyHandle body, const glm::vec3& velocity);
        bool setAngularVelocity(ScenePhysicsBodyHandle body, const glm::vec3& velocity);
        bool applyImpulse(ScenePhysicsBodyHandle body, const glm::vec3& impulse);
        bool applyForce(ScenePhysicsBodyHandle body, const glm::vec3& force);

        void syncFromScene();
        void stepFixed(float fixedDeltaSeconds);
        void syncToScene();
        [[nodiscard]] SceneSystemHandle registerPhysicsSystems();
        bool unregisterPhysicsSystems();

        [[nodiscard]] ScenePhysicsRaycastResult raycast(
            const glm::vec3& start,
            const glm::vec3& end,
            const ScenePhysicsFilter& filter = {});
        [[nodiscard]] ScenePhysicsSweepResult sweepCapsule(
            const ScenePhysicsCapsuleShape& capsule,
            const glm::vec3& start,
            const glm::vec3& end,
            const ScenePhysicsFilter& filter = {});
        [[nodiscard]] ScenePhysicsOverlapResult overlap(
            const ScenePhysicsShapeDescriptor& shape,
            const glm::vec3& position,
            const ScenePhysicsFilter& filter = {});
        [[nodiscard]] ScenePhysicsClosestPointResult closestPoint(
            const glm::vec3& point,
            float maxDistance,
            const ScenePhysicsFilter& filter = {});

        [[nodiscard]] ScenePhysicsDiagnostics diagnostics() const;
        [[nodiscard]] std::vector<ScenePhysicsDebugRequest> debugRequests() const;
        void clearDebugRequests();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
