#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Scene.hpp"

namespace Engine {
    struct WorldObjectHandle {
        uint32_t id = UINT32_MAX;
    };

    // CPU-side world transform. Rotation is stored as Euler radians for the
    // intentionally simple first pass.
    struct Transform {
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{1.0f};
    };

    // Explicit renderer binding for a world object. World owns simulation state;
    // Renderer owns GPU resources and draw instances.
    struct RenderableBinding {
        Renderer::MeshInstanceHandle instance;
    };

    // Minimal world container for object lifetime, simulation transforms, and
    // explicit synchronization to renderer instances.
    class World {
    public:
        WorldObjectHandle createObject();
        void destroyObject(WorldObjectHandle object);
        void destroyObjectAndRendererInstance(WorldObjectHandle object);

        void setPosition(WorldObjectHandle object, const glm::vec3& position);
        void setRotation(WorldObjectHandle object, const glm::vec3& eulerRadians);
        void setScale(WorldObjectHandle object, const glm::vec3& scale);
        void setTransform(WorldObjectHandle object, const Transform& transform);
        void setAngularVelocity(WorldObjectHandle object, const glm::vec3& radiansPerSecond);
        void setLocalBounds(WorldObjectHandle object, const Renderer::Aabb& bounds);
        void clearLocalBounds(WorldObjectHandle object);
        void attachRendererInstance(WorldObjectHandle object, Renderer::MeshInstanceHandle instance);
        bool isValid(WorldObjectHandle object) const;
        std::optional<Transform> transform(WorldObjectHandle object) const;
        std::optional<glm::vec3> position(WorldObjectHandle object) const;
        std::optional<Renderer::Aabb> worldBounds(WorldObjectHandle object) const;
        Renderer::MeshInstanceHandle rendererInstance(WorldObjectHandle object) const;

        void fixedUpdate(float dt);
        void syncRenderState() const;

    private:
        struct WorldObject {
            bool alive = false;
            Transform transform;
            glm::vec3 angularVelocity{};
            Renderer::MeshInstanceHandle rendererInstance;
            bool hasRendererInstance = false;
            Renderer::Aabb localBounds;
            bool hasLocalBounds = false;
        };

        bool isAlive(WorldObjectHandle object) const;

        std::vector<WorldObject> objects_;
    };
}
