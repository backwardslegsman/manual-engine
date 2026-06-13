#include "Engine/World.hpp"

#include "Renderer/Scene.hpp"

namespace Engine {
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

    void World::attachRendererInstance(WorldObjectHandle object, Renderer::MeshInstanceHandle instance)
    {
        if (!isAlive(object)) {
            return;
        }

        objects_[object.id].rendererInstance = instance;
        objects_[object.id].hasRendererInstance = true;
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
