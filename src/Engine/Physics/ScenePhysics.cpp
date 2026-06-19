#include "Engine/Physics/ScenePhysics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <thread>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace {
    namespace ObjectLayers {
        constexpr JPH::ObjectLayer NonMoving = 0;
        constexpr JPH::ObjectLayer Moving = 1;
        constexpr JPH::ObjectLayer Count = 2;
    }

    namespace BroadPhaseLayers {
        const JPH::BroadPhaseLayer NonMoving{0};
        const JPH::BroadPhaseLayer Moving{1};
        constexpr JPH::uint Count = 2;
    }

    class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::Count;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return layer == ObjectLayers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
        {
            return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
        }
#endif
    };

    class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const override
        {
            if (objectLayer == ObjectLayers::NonMoving) {
                return broadPhaseLayer == BroadPhaseLayers::Moving;
            }
            return true;
        }
    };

    class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer lhs, JPH::ObjectLayer rhs) const override
        {
            return lhs == ObjectLayers::Moving || rhs == ObjectLayers::Moving;
        }
    };

    struct JoltRuntime {
        JoltRuntime()
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }

        ~JoltRuntime()
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    };

    JoltRuntime& joltRuntime()
    {
        static JoltRuntime runtime;
        return runtime;
    }

    struct Aabb {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    bool finite(float value)
    {
        return std::isfinite(value);
    }

    bool finite(const glm::vec3& value)
    {
        return finite(value.x) && finite(value.y) && finite(value.z);
    }

    bool finite(const glm::quat& value)
    {
        return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
    }

    JPH::Vec3 toJolt(const glm::vec3& value)
    {
        return {value.x, value.y, value.z};
    }

    JPH::RVec3 toJoltR(const glm::vec3& value)
    {
        return {value.x, value.y, value.z};
    }

    JPH::Quat toJolt(const glm::quat& value)
    {
        const glm::quat normalized = glm::normalize(value);
        return {normalized.x, normalized.y, normalized.z, normalized.w};
    }

    glm::vec3 fromJolt(const JPH::Vec3& value)
    {
        return {value.GetX(), value.GetY(), value.GetZ()};
    }

    glm::quat fromJolt(const JPH::Quat& value)
    {
        return glm::normalize(glm::quat{value.GetW(), value.GetX(), value.GetY(), value.GetZ()});
    }

    JPH::ObjectLayer objectLayerFor(Engine::ScenePhysicsMotionType type)
    {
        return type == Engine::ScenePhysicsMotionType::Static ? ObjectLayers::NonMoving : ObjectLayers::Moving;
    }

    std::optional<std::pair<glm::vec3, glm::quat>> decomposeWorld(const glm::mat4& matrix)
    {
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 translation{0.0f};
        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
            return std::nullopt;
        }
        if (!finite(translation) || !finite(rotation)) {
            return std::nullopt;
        }
        return std::pair{translation, glm::normalize(rotation)};
    }

    bool overlaps(const Aabb& lhs, const Aabb& rhs)
    {
        return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x
            && lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y
            && lhs.min.z <= rhs.max.z && lhs.max.z >= rhs.min.z;
    }

    glm::vec3 clampPoint(const glm::vec3& point, const Aabb& bounds)
    {
        return glm::clamp(point, bounds.min, bounds.max);
    }

    std::optional<float> rayAabb(const glm::vec3& start, const glm::vec3& end, const Aabb& bounds)
    {
        const glm::vec3 direction = end - start;
        float tMin = 0.0f;
        float tMax = 1.0f;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(direction[axis]) < 0.000001f) {
                if (start[axis] < bounds.min[axis] || start[axis] > bounds.max[axis]) {
                    return std::nullopt;
                }
                continue;
            }
            const float invDirection = 1.0f / direction[axis];
            float t1 = (bounds.min[axis] - start[axis]) * invDirection;
            float t2 = (bounds.max[axis] - start[axis]) * invDirection;
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                return std::nullopt;
            }
        }
        return tMin;
    }

    glm::vec3 normalForPointOnAabb(const glm::vec3& point, const Aabb& bounds)
    {
        const float dxMin = std::abs(point.x - bounds.min.x);
        const float dxMax = std::abs(point.x - bounds.max.x);
        const float dyMin = std::abs(point.y - bounds.min.y);
        const float dyMax = std::abs(point.y - bounds.max.y);
        const float dzMin = std::abs(point.z - bounds.min.z);
        const float dzMax = std::abs(point.z - bounds.max.z);
        float best = dxMin;
        glm::vec3 normal{-1.0f, 0.0f, 0.0f};
        const auto consider = [&](float distance, const glm::vec3& candidate, float& currentBest, glm::vec3& currentNormal) {
            if (distance < currentBest) {
                currentBest = distance;
                currentNormal = candidate;
            }
        };
        consider(dxMax, {1.0f, 0.0f, 0.0f}, best, normal);
        consider(dyMin, {0.0f, -1.0f, 0.0f}, best, normal);
        consider(dyMax, {0.0f, 1.0f, 0.0f}, best, normal);
        consider(dzMin, {0.0f, 0.0f, -1.0f}, best, normal);
        consider(dzMax, {0.0f, 0.0f, 1.0f}, best, normal);
        return normal;
    }
}

namespace Engine {
    struct ScenePhysicsWorld::Impl {
        struct ColliderRecord {
            uint32_t generation = 0;
            bool occupied = false;
            ScenePhysicsBodyHandle body;
            ScenePhysicsShapeDescriptor shape;
        };

        struct BodyRecord {
            uint32_t generation = 0;
            bool occupied = false;
            ScenePhysicsBodyDescriptor descriptor;
            std::vector<SceneColliderHandle> colliders;
            JPH::BodyID bodyId;
            bool hasJoltBody = false;
            glm::vec3 position{0.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            std::optional<std::pair<glm::vec3, glm::quat>> kinematicTarget;
        };

        explicit Impl(Scene& scene)
            : scene(scene)
            , runtime(joltRuntime())
            , jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::max(1u, std::thread::hardware_concurrency()))
        {
            physicsSystem.Init(2048, 0, 2048, 2048, broadPhaseLayerInterface, objectVsBroadPhaseLayerFilter, objectLayerPairFilter);
            refreshDiagnostics();
        }

        ~Impl()
        {
            for (uint32_t index = 0; index < bodies.size(); ++index) {
                if (bodies[index].occupied) {
                    destroyBodySlot(index);
                }
            }
        }

        uint32_t nextGeneration(uint32_t generation) const
        {
            return generation == UINT32_MAX ? 1u : generation + 1u;
        }

        BodyRecord* record(ScenePhysicsBodyHandle handle)
        {
            if (!isValid(handle) || handle.index >= bodies.size()) {
                return nullptr;
            }
            BodyRecord& candidate = bodies[handle.index];
            return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
        }

        const BodyRecord* record(ScenePhysicsBodyHandle handle) const
        {
            if (!isValid(handle) || handle.index >= bodies.size()) {
                return nullptr;
            }
            const BodyRecord& candidate = bodies[handle.index];
            return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
        }

        ColliderRecord* record(SceneColliderHandle handle)
        {
            if (!isValid(handle) || handle.index >= colliders.size()) {
                return nullptr;
            }
            ColliderRecord& candidate = colliders[handle.index];
            return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
        }

        const ColliderRecord* record(SceneColliderHandle handle) const
        {
            if (!isValid(handle) || handle.index >= colliders.size()) {
                return nullptr;
            }
            const ColliderRecord& candidate = colliders[handle.index];
            return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
        }

        bool validBodyDescriptor(const ScenePhysicsBodyDescriptor& descriptor)
        {
            if (!scene.contains(descriptor.actor)) {
                setStatus(ScenePhysicsQueryStatus::InvalidInput, "Physics body owner actor is invalid.");
                return false;
            }
            if (descriptor.motionType == ScenePhysicsMotionType::Dynamic && scene.parent(descriptor.actor).has_value()) {
                setStatus(ScenePhysicsQueryStatus::InvalidInput, "Dynamic physics bodies cannot be parented in Phase 10.");
                return false;
            }
            if (!finite(descriptor.initialLinearVelocity) || !finite(descriptor.initialAngularVelocity) ||
                !finite(descriptor.linearDamping) || !finite(descriptor.angularDamping) || !finite(descriptor.gravityFactor)) {
                setStatus(ScenePhysicsQueryStatus::InvalidInput, "Physics body descriptor contains non-finite values.");
                return false;
            }
            return true;
        }

        bool validShape(const ScenePhysicsShapeDescriptor& shape) const
        {
            switch (shape.type) {
                case ScenePhysicsShapeType::Box:
                    return finite(shape.box.halfExtents) &&
                        shape.box.halfExtents.x > 0.0f &&
                        shape.box.halfExtents.y > 0.0f &&
                        shape.box.halfExtents.z > 0.0f;
                case ScenePhysicsShapeType::Sphere:
                    return finite(shape.sphere.radius) && shape.sphere.radius > 0.0f;
                case ScenePhysicsShapeType::Capsule:
                    return finite(shape.capsule.radius) && finite(shape.capsule.halfHeight) &&
                        shape.capsule.radius > 0.0f && shape.capsule.halfHeight > 0.0f;
                case ScenePhysicsShapeType::StaticTriangleMesh:
                    return shape.triangleMesh.vertices.size() >= 3 &&
                        shape.triangleMesh.indices.size() >= 3 &&
                        shape.triangleMesh.indices.size() % 3 == 0 &&
                        std::ranges::all_of(shape.triangleMesh.vertices, [](const glm::vec3& vertex) { return finite(vertex); }) &&
                        std::ranges::all_of(shape.triangleMesh.indices, [&](uint32_t index) {
                            return index < shape.triangleMesh.vertices.size();
                        });
            }
            return false;
        }

        std::optional<JPH::RefConst<JPH::Shape>> makeJoltShape(const ScenePhysicsShapeDescriptor& shape)
        {
            if (!validShape(shape)) {
                return std::nullopt;
            }

            switch (shape.type) {
                case ScenePhysicsShapeType::Box:
                    return JPH::RefConst<JPH::Shape>{
                        new JPH::BoxShape(toJolt(shape.box.halfExtents), 0.02f)};
                case ScenePhysicsShapeType::Sphere:
                    return JPH::RefConst<JPH::Shape>{new JPH::SphereShape(shape.sphere.radius)};
                case ScenePhysicsShapeType::Capsule:
                    return JPH::RefConst<JPH::Shape>{
                        new JPH::CapsuleShape(shape.capsule.halfHeight, shape.capsule.radius)};
                case ScenePhysicsShapeType::StaticTriangleMesh: {
                    JPH::VertexList vertices;
                    vertices.reserve(shape.triangleMesh.vertices.size());
                    for (const glm::vec3& vertex : shape.triangleMesh.vertices) {
                        vertices.push_back(JPH::Float3{vertex.x, vertex.y, vertex.z});
                    }
                    JPH::IndexedTriangleList triangles;
                    triangles.reserve(shape.triangleMesh.indices.size() / 3);
                    for (size_t index = 0; index + 2 < shape.triangleMesh.indices.size(); index += 3) {
                        triangles.emplace_back(
                            shape.triangleMesh.indices[index],
                            shape.triangleMesh.indices[index + 1],
                            shape.triangleMesh.indices[index + 2]);
                    }
                    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
                    JPH::ShapeSettings::ShapeResult result = settings.Create();
                    if (result.HasError()) {
                        return std::nullopt;
                    }
                    return JPH::RefConst<JPH::Shape>{result.Get()};
                }
            }
            return std::nullopt;
        }

        std::optional<JPH::RefConst<JPH::Shape>> makeBodyShape(const BodyRecord& body)
        {
            std::vector<JPH::RefConst<JPH::Shape>> shapes;
            for (SceneColliderHandle colliderHandle : body.colliders) {
                const ColliderRecord* collider = record(colliderHandle);
                if (!collider) {
                    continue;
                }
                std::optional<JPH::RefConst<JPH::Shape>> shape = makeJoltShape(collider->shape);
                if (!shape) {
                    return std::nullopt;
                }
                shapes.push_back(*shape);
            }
            if (shapes.empty()) {
                return std::nullopt;
            }
            if (shapes.size() == 1) {
                return shapes.front();
            }

            JPH::StaticCompoundShapeSettings compound;
            for (const JPH::RefConst<JPH::Shape>& shape : shapes) {
                compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape);
            }
            JPH::ShapeSettings::ShapeResult result = compound.Create();
            if (result.HasError()) {
                return std::nullopt;
            }
            return JPH::RefConst<JPH::Shape>{result.Get()};
        }

        bool readActorWorld(SceneActorHandle actor, glm::vec3& outPosition, glm::quat& outRotation)
        {
            std::optional<glm::mat4> world = scene.worldMatrix(actor);
            if (!world.has_value()) {
                return false;
            }
            std::optional<std::pair<glm::vec3, glm::quat>> decomposed = decomposeWorld(*world);
            if (!decomposed.has_value()) {
                return false;
            }
            outPosition = decomposed->first;
            outRotation = decomposed->second;
            return true;
        }

        bool rebuildJoltBody(BodyRecord& body)
        {
            destroyJoltBody(body);
            if (body.colliders.empty() || !body.descriptor.enabled) {
                return true;
            }

            std::optional<JPH::RefConst<JPH::Shape>> shape = makeBodyShape(body);
            if (!shape.has_value()) {
                setStatus(ScenePhysicsQueryStatus::InvalidInput, "Physics body has invalid collider geometry.");
                return false;
            }

            glm::vec3 position{0.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            if (!readActorWorld(body.descriptor.actor, position, rotation)) {
                setStatus(ScenePhysicsQueryStatus::InvalidInput, "Physics body owner transform is invalid.");
                return false;
            }
            body.position = position;
            body.rotation = rotation;

            JPH::BodyCreationSettings settings(
                *shape,
                toJoltR(position),
                toJolt(rotation),
                body.descriptor.motionType == ScenePhysicsMotionType::Static ? JPH::EMotionType::Static :
                    body.descriptor.motionType == ScenePhysicsMotionType::Kinematic ? JPH::EMotionType::Kinematic :
                    JPH::EMotionType::Dynamic,
                objectLayerFor(body.descriptor.motionType));
            settings.mIsSensor = body.descriptor.sensor;
            settings.mFriction = body.descriptor.material.friction;
            settings.mRestitution = body.descriptor.material.restitution;
            settings.mLinearDamping = body.descriptor.linearDamping;
            settings.mAngularDamping = body.descriptor.angularDamping;
            settings.mGravityFactor = body.descriptor.gravityFactor;

            JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
            body.bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
            body.hasJoltBody = !body.bodyId.IsInvalid();
            if (!body.hasJoltBody) {
                setStatus(ScenePhysicsQueryStatus::InvalidInput, "Jolt rejected physics body creation.");
                return false;
            }
            if (body.descriptor.motionType == ScenePhysicsMotionType::Dynamic) {
                bodyInterface.SetLinearVelocity(body.bodyId, toJolt(body.descriptor.initialLinearVelocity));
                bodyInterface.SetAngularVelocity(body.bodyId, toJolt(body.descriptor.initialAngularVelocity));
            }
            return true;
        }

        void destroyJoltBody(BodyRecord& body)
        {
            if (!body.hasJoltBody) {
                return;
            }
            JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
            bodyInterface.RemoveBody(body.bodyId);
            bodyInterface.DestroyBody(body.bodyId);
            body.hasJoltBody = false;
            body.bodyId = {};
        }

        void destroyBodySlot(uint32_t index)
        {
            BodyRecord& body = bodies[index];
            destroyJoltBody(body);
            for (SceneColliderHandle colliderHandle : body.colliders) {
                if (ColliderRecord* collider = record(colliderHandle)) {
                    collider->generation = nextGeneration(collider->generation);
                    collider->occupied = false;
                    collider->body = {};
                    collider->shape = {};
                    freeColliders.push_back(colliderHandle.index);
                }
            }
            body.colliders.clear();
            body.generation = nextGeneration(body.generation);
            body.occupied = false;
            body.descriptor = {};
            body.kinematicTarget.reset();
            freeBodies.push_back(index);
            refreshDiagnostics();
        }

        void cleanupInvalidOwners()
        {
            for (uint32_t index = 0; index < bodies.size(); ++index) {
                if (!bodies[index].occupied) {
                    continue;
                }
                if (!scene.contains(bodies[index].descriptor.actor)) {
                    destroyBodySlot(index);
                    ++diagnostics.invalidOwnerCleanupCount;
                }
            }
        }

        void updateCachedPoseFromJolt(BodyRecord& body)
        {
            if (!body.hasJoltBody) {
                return;
            }
            const JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
            body.position = fromJolt(bodyInterface.GetPosition(body.bodyId));
            body.rotation = fromJolt(bodyInterface.GetRotation(body.bodyId));
        }

        void setStatus(ScenePhysicsQueryStatus status, std::string message)
        {
            diagnostics.lastStatus = status;
            diagnostics.lastMessage = std::move(message);
        }

        bool passesFilter(const BodyRecord& body, const ScenePhysicsFilter& filter) const
        {
            if (!filter.includeSensors && body.descriptor.sensor) {
                return false;
            }
            const uint32_t layer = body.descriptor.layer.value;
            return (layer & filter.includeLayerMask) != 0u && (layer & filter.excludeLayerMask) == 0u;
        }

        std::optional<Aabb> shapeAabb(const ScenePhysicsShapeDescriptor& shape, const glm::vec3& position) const
        {
            if (!validShape(shape)) {
                return std::nullopt;
            }
            switch (shape.type) {
                case ScenePhysicsShapeType::Box:
                    return Aabb{position - shape.box.halfExtents, position + shape.box.halfExtents};
                case ScenePhysicsShapeType::Sphere:
                    return Aabb{position - glm::vec3{shape.sphere.radius}, position + glm::vec3{shape.sphere.radius}};
                case ScenePhysicsShapeType::Capsule: {
                    const glm::vec3 extents{shape.capsule.radius, shape.capsule.halfHeight + shape.capsule.radius, shape.capsule.radius};
                    return Aabb{position - extents, position + extents};
                }
                case ScenePhysicsShapeType::StaticTriangleMesh: {
                    glm::vec3 minPoint{std::numeric_limits<float>::max()};
                    glm::vec3 maxPoint{std::numeric_limits<float>::lowest()};
                    for (const glm::vec3& vertex : shape.triangleMesh.vertices) {
                        minPoint = glm::min(minPoint, position + vertex);
                        maxPoint = glm::max(maxPoint, position + vertex);
                    }
                    return Aabb{minPoint, maxPoint};
                }
            }
            return std::nullopt;
        }

        std::optional<Aabb> bodyAabb(const BodyRecord& body) const
        {
            std::optional<Aabb> result;
            for (SceneColliderHandle colliderHandle : body.colliders) {
                const ColliderRecord* collider = record(colliderHandle);
                if (!collider) {
                    continue;
                }
                std::optional<Aabb> bounds = shapeAabb(collider->shape, body.position);
                if (!bounds) {
                    continue;
                }
                if (!result) {
                    result = bounds;
                } else {
                    result->min = glm::min(result->min, bounds->min);
                    result->max = glm::max(result->max, bounds->max);
                }
            }
            return result;
        }

        void appendDebug(ScenePhysicsDebugRequest request)
        {
            if (debugRequests.size() < 256) {
                debugRequests.push_back(std::move(request));
            }
        }

        void refreshDiagnostics()
        {
            uint32_t bodyCount = 0;
            uint32_t colliderCount = 0;
            uint32_t activeCount = 0;
            for (const BodyRecord& body : bodies) {
                if (body.occupied) {
                    ++bodyCount;
                    if (body.hasJoltBody && body.descriptor.enabled) {
                        ++activeCount;
                    }
                }
            }
            for (const ColliderRecord& collider : colliders) {
                if (collider.occupied) {
                    ++colliderCount;
                }
            }
            diagnostics.bodyCount = bodyCount;
            diagnostics.colliderCount = colliderCount;
            diagnostics.activeBodyCount = activeCount;
        }

        Scene& scene;
        JoltRuntime& runtime;
        BroadPhaseLayerInterface broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilter objectLayerPairFilter;
        JPH::PhysicsSystem physicsSystem;
        JPH::TempAllocatorImpl tempAllocator{16 * 1024 * 1024};
        JPH::JobSystemThreadPool jobSystem;
        std::vector<BodyRecord> bodies;
        std::vector<ColliderRecord> colliders;
        std::vector<uint32_t> freeBodies;
        std::vector<uint32_t> freeColliders;
        std::optional<SceneSystemHandle> physicsSystemHandle;
        ScenePhysicsDiagnostics diagnostics;
        std::vector<ScenePhysicsDebugRequest> debugRequests;
    };

    ScenePhysicsWorld::ScenePhysicsWorld(Scene& scene)
        : impl_(std::make_unique<Impl>(scene))
    {
    }

    ScenePhysicsWorld::~ScenePhysicsWorld() = default;

    ScenePhysicsBodyHandle ScenePhysicsWorld::createBody(const ScenePhysicsBodyDescriptor& descriptor)
    {
        if (!impl_->validBodyDescriptor(descriptor)) {
            return {};
        }

        uint32_t index = 0;
        if (!impl_->freeBodies.empty()) {
            index = impl_->freeBodies.back();
            impl_->freeBodies.pop_back();
            Impl::BodyRecord& record = impl_->bodies[index];
            const uint32_t previousGeneration = record.generation;
            record = {};
            record.occupied = true;
            record.generation = impl_->nextGeneration(previousGeneration);
        } else {
            Impl::BodyRecord record;
            record.occupied = true;
            record.generation = 1;
            impl_->bodies.push_back(record);
            index = static_cast<uint32_t>(impl_->bodies.size() - 1);
        }

        Impl::BodyRecord& record = impl_->bodies[index];
        record.descriptor = descriptor;
        impl_->readActorWorld(descriptor.actor, record.position, record.rotation);
        impl_->refreshDiagnostics();
        return {index, record.generation};
    }

    bool ScenePhysicsWorld::destroyBody(ScenePhysicsBodyHandle body)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record) {
            return false;
        }
        impl_->destroyBodySlot(body.index);
        return true;
    }

    bool ScenePhysicsWorld::contains(ScenePhysicsBodyHandle body) const
    {
        return impl_->record(body) != nullptr;
    }

    std::optional<ScenePhysicsBodyDescriptor> ScenePhysicsWorld::body(ScenePhysicsBodyHandle body) const
    {
        const Impl::BodyRecord* record = impl_->record(body);
        if (!record) {
            return std::nullopt;
        }
        return record->descriptor;
    }

    std::optional<ScenePhysicsBodyHandle> ScenePhysicsWorld::bodyForActor(SceneActorHandle actor) const
    {
        for (uint32_t index = 0; index < impl_->bodies.size(); ++index) {
            const Impl::BodyRecord& record = impl_->bodies[index];
            if (record.occupied && record.descriptor.actor == actor) {
                return ScenePhysicsBodyHandle{index, record.generation};
            }
        }
        return std::nullopt;
    }

    SceneColliderHandle ScenePhysicsWorld::attachCollider(
        ScenePhysicsBodyHandle body,
        const ScenePhysicsShapeDescriptor& shape)
    {
        Impl::BodyRecord* bodyRecord = impl_->record(body);
        if (!bodyRecord || !impl_->validShape(shape)) {
            impl_->setStatus(ScenePhysicsQueryStatus::InvalidInput, "Invalid collider attach request.");
            return {};
        }

        uint32_t index = 0;
        if (!impl_->freeColliders.empty()) {
            index = impl_->freeColliders.back();
            impl_->freeColliders.pop_back();
            Impl::ColliderRecord& record = impl_->colliders[index];
            const uint32_t previousGeneration = record.generation;
            record = {};
            record.occupied = true;
            record.generation = impl_->nextGeneration(previousGeneration);
        } else {
            Impl::ColliderRecord record;
            record.occupied = true;
            record.generation = 1;
            impl_->colliders.push_back(record);
            index = static_cast<uint32_t>(impl_->colliders.size() - 1);
        }

        Impl::ColliderRecord& collider = impl_->colliders[index];
        collider.body = body;
        collider.shape = shape;
        const SceneColliderHandle handle{index, collider.generation};
        bodyRecord->colliders.push_back(handle);
        if (!impl_->rebuildJoltBody(*bodyRecord)) {
            bodyRecord->colliders.erase(std::remove(bodyRecord->colliders.begin(), bodyRecord->colliders.end(), handle), bodyRecord->colliders.end());
            collider.generation = impl_->nextGeneration(collider.generation);
            collider.occupied = false;
            collider.body = {};
            collider.shape = {};
            impl_->freeColliders.push_back(index);
            impl_->refreshDiagnostics();
            return {};
        }
        impl_->refreshDiagnostics();
        return handle;
    }

    bool ScenePhysicsWorld::detachCollider(SceneColliderHandle collider)
    {
        Impl::ColliderRecord* colliderRecord = impl_->record(collider);
        if (!colliderRecord) {
            return false;
        }
        Impl::BodyRecord* bodyRecord = impl_->record(colliderRecord->body);
        if (bodyRecord) {
            bodyRecord->colliders.erase(
                std::remove(bodyRecord->colliders.begin(), bodyRecord->colliders.end(), collider),
                bodyRecord->colliders.end());
        }
        colliderRecord->generation = impl_->nextGeneration(colliderRecord->generation);
        colliderRecord->occupied = false;
        colliderRecord->body = {};
        colliderRecord->shape = {};
        impl_->freeColliders.push_back(collider.index);
        if (bodyRecord) {
            impl_->rebuildJoltBody(*bodyRecord);
        }
        impl_->refreshDiagnostics();
        return true;
    }

    bool ScenePhysicsWorld::contains(SceneColliderHandle collider) const
    {
        return impl_->record(collider) != nullptr;
    }

    std::optional<ScenePhysicsColliderDescriptor> ScenePhysicsWorld::collider(SceneColliderHandle collider) const
    {
        const Impl::ColliderRecord* record = impl_->record(collider);
        if (!record) {
            return std::nullopt;
        }
        return ScenePhysicsColliderDescriptor{record->body, record->shape};
    }

    std::vector<SceneColliderHandle> ScenePhysicsWorld::colliders(ScenePhysicsBodyHandle body) const
    {
        const Impl::BodyRecord* record = impl_->record(body);
        return record ? record->colliders : std::vector<SceneColliderHandle>{};
    }

    bool ScenePhysicsWorld::setBodyEnabled(ScenePhysicsBodyHandle body, bool enabled)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record) {
            return false;
        }
        if (record->descriptor.enabled == enabled) {
            return true;
        }
        record->descriptor.enabled = enabled;
        const bool rebuilt = impl_->rebuildJoltBody(*record);
        impl_->refreshDiagnostics();
        return rebuilt;
    }

    bool ScenePhysicsWorld::setMotionType(ScenePhysicsBodyHandle body, ScenePhysicsMotionType motionType)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record) {
            return false;
        }
        if (motionType == ScenePhysicsMotionType::Dynamic && impl_->scene.parent(record->descriptor.actor).has_value()) {
            impl_->setStatus(ScenePhysicsQueryStatus::InvalidInput, "Dynamic physics bodies cannot be parented in Phase 10.");
            return false;
        }
        record->descriptor.motionType = motionType;
        const bool rebuilt = impl_->rebuildJoltBody(*record);
        impl_->refreshDiagnostics();
        return rebuilt;
    }

    bool ScenePhysicsWorld::setKinematicTarget(ScenePhysicsBodyHandle body, const glm::vec3& translation, const glm::quat& rotation)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record || record->descriptor.motionType != ScenePhysicsMotionType::Kinematic ||
            !finite(translation) || !finite(rotation)) {
            return false;
        }
        record->kinematicTarget = std::pair{translation, glm::normalize(rotation)};
        return true;
    }

    bool ScenePhysicsWorld::setLinearVelocity(ScenePhysicsBodyHandle body, const glm::vec3& velocity)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record || !record->hasJoltBody || !finite(velocity)) {
            return false;
        }
        impl_->physicsSystem.GetBodyInterface().SetLinearVelocity(record->bodyId, toJolt(velocity));
        return true;
    }

    bool ScenePhysicsWorld::setAngularVelocity(ScenePhysicsBodyHandle body, const glm::vec3& velocity)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record || !record->hasJoltBody || !finite(velocity)) {
            return false;
        }
        impl_->physicsSystem.GetBodyInterface().SetAngularVelocity(record->bodyId, toJolt(velocity));
        return true;
    }

    bool ScenePhysicsWorld::applyImpulse(ScenePhysicsBodyHandle body, const glm::vec3& impulse)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record || !record->hasJoltBody || !finite(impulse)) {
            return false;
        }
        impl_->physicsSystem.GetBodyInterface().AddImpulse(record->bodyId, toJolt(impulse));
        return true;
    }

    bool ScenePhysicsWorld::applyForce(ScenePhysicsBodyHandle body, const glm::vec3& force)
    {
        Impl::BodyRecord* record = impl_->record(body);
        if (!record || !record->hasJoltBody || !finite(force)) {
            return false;
        }
        impl_->physicsSystem.GetBodyInterface().AddForce(record->bodyId, toJolt(force));
        return true;
    }

    void ScenePhysicsWorld::syncFromScene()
    {
        impl_->cleanupInvalidOwners();
        for (Impl::BodyRecord& body : impl_->bodies) {
            if (!body.occupied || !body.hasJoltBody ||
                body.descriptor.motionType == ScenePhysicsMotionType::Dynamic) {
                continue;
            }

            if (body.descriptor.motionType == ScenePhysicsMotionType::Kinematic && body.kinematicTarget.has_value()) {
                body.position = body.kinematicTarget->first;
                body.rotation = body.kinematicTarget->second;
                body.kinematicTarget.reset();
            } else if (!impl_->readActorWorld(body.descriptor.actor, body.position, body.rotation)) {
                continue;
            }
            impl_->physicsSystem.GetBodyInterface().SetPositionAndRotation(
                body.bodyId,
                toJoltR(body.position),
                toJolt(body.rotation),
                JPH::EActivation::Activate);
        }
    }

    void ScenePhysicsWorld::stepFixed(float fixedDeltaSeconds)
    {
        if (!finite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f) {
            impl_->setStatus(ScenePhysicsQueryStatus::InvalidInput, "Invalid physics fixed delta.");
            return;
        }
        impl_->cleanupInvalidOwners();
        const auto start = std::chrono::steady_clock::now();
        impl_->physicsSystem.Update(fixedDeltaSeconds, 1, &impl_->tempAllocator, &impl_->jobSystem);
        impl_->diagnostics.lastStepMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        for (Impl::BodyRecord& body : impl_->bodies) {
            if (body.occupied && body.hasJoltBody) {
                impl_->updateCachedPoseFromJolt(body);
            }
        }
        impl_->refreshDiagnostics();
    }

    void ScenePhysicsWorld::syncToScene()
    {
        impl_->cleanupInvalidOwners();
        for (Impl::BodyRecord& body : impl_->bodies) {
            if (!body.occupied || body.descriptor.motionType != ScenePhysicsMotionType::Dynamic || !body.hasJoltBody) {
                continue;
            }
            if (impl_->scene.parent(body.descriptor.actor).has_value()) {
                impl_->diagnostics.warnings.push_back("Skipped dynamic physics writeback for parented actor.");
                continue;
            }
            impl_->updateCachedPoseFromJolt(body);
            std::optional<SceneTransform> transform = impl_->scene.localTransform(body.descriptor.actor);
            if (!transform.has_value()) {
                continue;
            }
            transform->translation = body.position;
            transform->rotation = body.rotation;
            impl_->scene.setLocalTransform(body.descriptor.actor, *transform);
        }
    }

    SceneSystemHandle ScenePhysicsWorld::registerPhysicsSystems()
    {
        if (impl_->physicsSystemHandle.has_value() && impl_->scene.contains(*impl_->physicsSystemHandle)) {
            return *impl_->physicsSystemHandle;
        }

        SceneSystemDescriptor descriptor;
        descriptor.name = "ScenePhysicsWorld";
        descriptor.phases = {SceneTickPhase::FixedPrePhysics, SceneTickPhase::FixedPhysics, SceneTickPhase::FixedPostPhysics};
        descriptor.onTick = [this](Scene&, const SceneTickContext& context) {
            switch (context.phase) {
                case SceneTickPhase::FixedPrePhysics:
                    syncFromScene();
                    break;
                case SceneTickPhase::FixedPhysics:
                    stepFixed(context.deltaSeconds);
                    break;
                case SceneTickPhase::FixedPostPhysics:
                    syncToScene();
                    break;
                default:
                    break;
            }
        };
        const SceneSystemHandle handle = impl_->scene.registerSystem(std::move(descriptor));
        if (isValid(handle)) {
            impl_->physicsSystemHandle = handle;
        }
        return handle;
    }

    bool ScenePhysicsWorld::unregisterPhysicsSystems()
    {
        if (!impl_->physicsSystemHandle.has_value()) {
            return false;
        }
        const bool removed = impl_->scene.unregisterSystem(*impl_->physicsSystemHandle);
        if (removed) {
            impl_->physicsSystemHandle.reset();
        }
        return removed;
    }

    ScenePhysicsRaycastResult ScenePhysicsWorld::raycast(
        const glm::vec3& start,
        const glm::vec3& end,
        const ScenePhysicsFilter& filter)
    {
        const auto queryStart = std::chrono::steady_clock::now();
        ++impl_->diagnostics.raycastCount;
        ScenePhysicsRaycastResult result;
        if (!finite(start) || !finite(end) || glm::length2(end - start) <= 0.000001f) {
            result.status = ScenePhysicsQueryStatus::InvalidInput;
            result.message = "Invalid physics raycast.";
            impl_->setStatus(result.status, result.message);
            return result;
        }

        float bestT = std::numeric_limits<float>::max();
        ScenePhysicsHit bestHit;
        for (uint32_t bodyIndex = 0; bodyIndex < impl_->bodies.size(); ++bodyIndex) {
            const Impl::BodyRecord& body = impl_->bodies[bodyIndex];
            if (!body.occupied || !impl_->passesFilter(body, filter)) {
                continue;
            }
            std::optional<Aabb> bounds = impl_->bodyAabb(body);
            if (!bounds) {
                continue;
            }
            std::optional<float> t = rayAabb(start, end, *bounds);
            if (!t || *t > bestT) {
                continue;
            }
            bestT = *t;
            bestHit.body = {bodyIndex, body.generation};
            bestHit.collider = body.colliders.empty() ? SceneColliderHandle{} : body.colliders.front();
            bestHit.actor = body.descriptor.actor;
            bestHit.position = start + (end - start) * *t;
            bestHit.normal = normalForPointOnAabb(bestHit.position, *bounds);
            bestHit.distance = glm::length(bestHit.position - start);
        }

        if (bestT != std::numeric_limits<float>::max()) {
            result.status = ScenePhysicsQueryStatus::Success;
            result.hit = bestHit;
            result.message = "Raycast hit.";
        } else {
            result.status = ScenePhysicsQueryStatus::NoHit;
            result.message = "Raycast missed.";
        }
        impl_->diagnostics.lastQueryMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - queryStart).count());
        impl_->setStatus(result.status, result.message);
        impl_->appendDebug({ScenePhysicsDebugRequestType::Raycast, result.status, start, end, bestHit.position, bestHit.normal, {}, bestHit.body, bestHit.collider});
        return result;
    }

    ScenePhysicsSweepResult ScenePhysicsWorld::sweepCapsule(
        const ScenePhysicsCapsuleShape& capsule,
        const glm::vec3& start,
        const glm::vec3& end,
        const ScenePhysicsFilter& filter)
    {
        const auto queryStart = std::chrono::steady_clock::now();
        ++impl_->diagnostics.sweepCount;
        ScenePhysicsSweepResult result;
        if (!finite(start) || !finite(end) || !finite(capsule.radius) || !finite(capsule.halfHeight) ||
            capsule.radius <= 0.0f || capsule.halfHeight <= 0.0f) {
            result.status = ScenePhysicsQueryStatus::InvalidInput;
            result.message = "Invalid capsule sweep.";
            impl_->setStatus(result.status, result.message);
            return result;
        }

        const glm::vec3 inflation{capsule.radius, capsule.halfHeight + capsule.radius, capsule.radius};
        float bestT = std::numeric_limits<float>::max();
        ScenePhysicsHit bestHit;
        for (uint32_t bodyIndex = 0; bodyIndex < impl_->bodies.size(); ++bodyIndex) {
            const Impl::BodyRecord& body = impl_->bodies[bodyIndex];
            if (!body.occupied || !impl_->passesFilter(body, filter)) {
                continue;
            }
            std::optional<Aabb> bounds = impl_->bodyAabb(body);
            if (!bounds) {
                continue;
            }
            bounds->min -= inflation;
            bounds->max += inflation;
            std::optional<float> t = rayAabb(start, end, *bounds);
            if (!t || *t > bestT) {
                continue;
            }
            bestT = *t;
            bestHit.body = {bodyIndex, body.generation};
            bestHit.collider = body.colliders.empty() ? SceneColliderHandle{} : body.colliders.front();
            bestHit.actor = body.descriptor.actor;
            bestHit.position = start + (end - start) * *t;
            bestHit.normal = normalForPointOnAabb(bestHit.position, *bounds);
            bestHit.distance = glm::length(bestHit.position - start);
        }

        result.status = bestT == std::numeric_limits<float>::max() ? ScenePhysicsQueryStatus::NoHit : ScenePhysicsQueryStatus::Success;
        result.message = result.status == ScenePhysicsQueryStatus::Success ? "Capsule sweep hit." : "Capsule sweep missed.";
        if (result.status == ScenePhysicsQueryStatus::Success) {
            result.hit = bestHit;
        }
        impl_->diagnostics.lastQueryMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - queryStart).count());
        impl_->setStatus(result.status, result.message);
        impl_->appendDebug({ScenePhysicsDebugRequestType::Sweep, result.status, start, end, bestHit.position, bestHit.normal, inflation, bestHit.body, bestHit.collider});
        return result;
    }

    ScenePhysicsOverlapResult ScenePhysicsWorld::overlap(
        const ScenePhysicsShapeDescriptor& shape,
        const glm::vec3& position,
        const ScenePhysicsFilter& filter)
    {
        const auto queryStart = std::chrono::steady_clock::now();
        ++impl_->diagnostics.overlapCount;
        ScenePhysicsOverlapResult result;
        std::optional<Aabb> queryBounds = impl_->shapeAabb(shape, position);
        if (!finite(position) || !queryBounds) {
            result.status = ScenePhysicsQueryStatus::InvalidInput;
            result.message = "Invalid overlap query.";
            impl_->setStatus(result.status, result.message);
            return result;
        }

        for (uint32_t bodyIndex = 0; bodyIndex < impl_->bodies.size(); ++bodyIndex) {
            const Impl::BodyRecord& body = impl_->bodies[bodyIndex];
            if (!body.occupied || !impl_->passesFilter(body, filter)) {
                continue;
            }
            std::optional<Aabb> bounds = impl_->bodyAabb(body);
            if (!bounds || !overlaps(*queryBounds, *bounds)) {
                continue;
            }
            result.hits.push_back({{bodyIndex, body.generation}, body.colliders.empty() ? SceneColliderHandle{} : body.colliders.front(), body.descriptor.actor, body.position, {0.0f, 1.0f, 0.0f}, 0.0f});
        }
        result.status = result.hits.empty() ? ScenePhysicsQueryStatus::NoHit : ScenePhysicsQueryStatus::Success;
        result.message = result.hits.empty() ? "Overlap found no bodies." : "Overlap found bodies.";
        impl_->diagnostics.lastQueryMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - queryStart).count());
        impl_->setStatus(result.status, result.message);
        ScenePhysicsDebugRequest debug;
        debug.type = ScenePhysicsDebugRequestType::Overlap;
        debug.status = result.status;
        debug.position = position;
        debug.extents = (queryBounds->max - queryBounds->min) * 0.5f;
        impl_->appendDebug(debug);
        return result;
    }

    ScenePhysicsClosestPointResult ScenePhysicsWorld::closestPoint(
        const glm::vec3& point,
        float maxDistance,
        const ScenePhysicsFilter& filter)
    {
        const auto queryStart = std::chrono::steady_clock::now();
        ++impl_->diagnostics.closestPointCount;
        ScenePhysicsClosestPointResult result;
        if (!finite(point) || !finite(maxDistance) || maxDistance < 0.0f) {
            result.status = ScenePhysicsQueryStatus::InvalidInput;
            result.message = "Invalid closest point query.";
            impl_->setStatus(result.status, result.message);
            return result;
        }

        float bestDistance = maxDistance;
        ScenePhysicsHit bestHit;
        for (uint32_t bodyIndex = 0; bodyIndex < impl_->bodies.size(); ++bodyIndex) {
            const Impl::BodyRecord& body = impl_->bodies[bodyIndex];
            if (!body.occupied || !impl_->passesFilter(body, filter)) {
                continue;
            }
            std::optional<Aabb> bounds = impl_->bodyAabb(body);
            if (!bounds) {
                continue;
            }
            const glm::vec3 closest = clampPoint(point, *bounds);
            const float distance = glm::length(point - closest);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestHit.body = {bodyIndex, body.generation};
                bestHit.collider = body.colliders.empty() ? SceneColliderHandle{} : body.colliders.front();
                bestHit.actor = body.descriptor.actor;
                bestHit.position = closest;
                bestHit.normal = distance > 0.000001f ? glm::normalize(point - closest) : normalForPointOnAabb(closest, *bounds);
                bestHit.distance = distance;
            }
        }
        result.status = bestHit.body.generation == 0 ? ScenePhysicsQueryStatus::NoHit : ScenePhysicsQueryStatus::Success;
        result.message = result.status == ScenePhysicsQueryStatus::Success ? "Closest point found." : "Closest point found no bodies.";
        if (result.status == ScenePhysicsQueryStatus::Success) {
            result.hit = bestHit;
        }
        impl_->diagnostics.lastQueryMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - queryStart).count());
        impl_->setStatus(result.status, result.message);
        impl_->appendDebug({ScenePhysicsDebugRequestType::ClosestPoint, result.status, point, bestHit.position, bestHit.position, bestHit.normal, {}, bestHit.body, bestHit.collider});
        return result;
    }

    ScenePhysicsDiagnostics ScenePhysicsWorld::diagnostics() const
    {
        impl_->refreshDiagnostics();
        return impl_->diagnostics;
    }

    std::vector<ScenePhysicsDebugRequest> ScenePhysicsWorld::debugRequests() const
    {
        return impl_->debugRequests;
    }

    void ScenePhysicsWorld::clearDebugRequests()
    {
        impl_->debugRequests.clear();
    }
}
