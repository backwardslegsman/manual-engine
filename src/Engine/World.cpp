#include "Engine/World.hpp"

#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Scene.hpp"

namespace Engine {
    namespace {
        Renderer::Aabb emptyAabb()
        {
            const float maxFloat = std::numeric_limits<float>::max();
            return {{maxFloat, maxFloat, maxFloat}, {-maxFloat, -maxFloat, -maxFloat}};
        }

        glm::mat4 composeTransform(const Transform& value)
        {
            glm::mat4 transform{1.0f};
            transform = glm::translate(transform, value.position);
            transform = glm::rotate(transform, value.rotation.x, {1.0f, 0.0f, 0.0f});
            transform = glm::rotate(transform, value.rotation.y, {0.0f, 1.0f, 0.0f});
            transform = glm::rotate(transform, value.rotation.z, {0.0f, 0.0f, 1.0f});
            transform = glm::scale(transform, value.scale);
            return transform;
        }

        Renderer::Aabb transformAabb(const Renderer::Aabb& bounds, const Transform& value)
        {
            Renderer::Aabb transformed = emptyAabb();
            const glm::mat4 transform = composeTransform(value);
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const glm::vec3 point{
                    (corner & 1) ? bounds.max.x : bounds.min.x,
                    (corner & 2) ? bounds.max.y : bounds.min.y,
                    (corner & 4) ? bounds.max.z : bounds.min.z,
                };
                const glm::vec4 transformedPoint = transform * glm::vec4{point, 1.0f};
                transformed.min = glm::min(transformed.min, glm::vec3{transformedPoint});
                transformed.max = glm::max(transformed.max, glm::vec3{transformedPoint});
            }
            return transformed;
        }
    }

    WorldObjectHandle World::createObject()
    {
        for (uint32_t index = 0; index < objects_.size(); ++index) {
            if (!objects_[index].alive) {
                objects_[index] = {};
                objects_[index].alive = true;
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(objects_.size());
        WorldObject object;
        object.alive = true;
        objects_.push_back(object);
        return {id};
    }

    void World::destroyObject(WorldObjectHandle object)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].rendererInstance = {};
        objects_[object.id].hasRendererInstance = false;
        objects_[object.id] = {};
    }

    void World::destroyObjectAndRendererInstance(WorldObjectHandle object)
    {
        if (!isAlive(object)) {
            return;
        }

        if (objects_[object.id].hasRendererInstance) {
            Renderer::destroyInstance(objects_[object.id].rendererInstance);
        }
        destroyObject(object);
    }

    void World::setPosition(WorldObjectHandle object, const glm::vec3& position)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].transform.position = position;
    }

    void World::setRotation(WorldObjectHandle object, const glm::vec3& eulerRadians)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].transform.rotation = eulerRadians;
    }

    void World::setScale(WorldObjectHandle object, const glm::vec3& scale)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].transform.scale = scale;
    }

    void World::setTransform(WorldObjectHandle object, const Transform& transform)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].transform = transform;
    }

    void World::setAngularVelocity(WorldObjectHandle object, const glm::vec3& radiansPerSecond)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].angularVelocity = radiansPerSecond;
    }

    void World::setLocalBounds(WorldObjectHandle object, const Renderer::Aabb& bounds)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].localBounds = bounds;
        objects_[object.id].hasLocalBounds = true;
    }

    void World::clearLocalBounds(WorldObjectHandle object)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].localBounds = {};
        objects_[object.id].hasLocalBounds = false;
    }

    void World::attachRendererInstance(WorldObjectHandle object, Renderer::MeshInstanceHandle instance)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].rendererInstance = instance;
        objects_[object.id].hasRendererInstance = true;
    }

    bool World::isValid(WorldObjectHandle object) const
    {
        return isAlive(object);
    }

    std::optional<Transform> World::transform(WorldObjectHandle object) const
    {
        if (!isAlive(object)) {
            return std::nullopt;
        }

        return objects_[object.id].transform;
    }

    std::optional<glm::vec3> World::position(WorldObjectHandle object) const
    {
        if (!isAlive(object)) {
            return std::nullopt;
        }

        return objects_[object.id].transform.position;
    }

    std::optional<Renderer::Aabb> World::worldBounds(WorldObjectHandle object) const
    {
        if (!isAlive(object) || !objects_[object.id].hasLocalBounds) {
            return std::nullopt;
        }

        return transformAabb(objects_[object.id].localBounds, objects_[object.id].transform);
    }

    Renderer::MeshInstanceHandle World::rendererInstance(WorldObjectHandle object) const
    {
        if (!isAlive(object) || !objects_[object.id].hasRendererInstance) {
            return {};
        }

        return objects_[object.id].rendererInstance;
    }

    void World::fixedUpdate(float dt)
    {
        for (WorldObject& object : objects_) {
            if (!object.alive) {
                continue;
            }

            object.transform.rotation += object.angularVelocity * dt;
        }
    }

    void World::syncRenderState() const
    {
        for (const WorldObject& object : objects_) {
            if (!object.alive || !object.hasRendererInstance) {
                continue;
            }

            Renderer::setInstancePosition(object.rendererInstance, object.transform.position);
            Renderer::setInstanceRotation(object.rendererInstance, object.transform.rotation);
            Renderer::setInstanceScale(object.rendererInstance, object.transform.scale);
        }
    }

    bool World::isAlive(WorldObjectHandle object) const
    {
        return object.id < objects_.size() && objects_[object.id].alive;
    }
}
