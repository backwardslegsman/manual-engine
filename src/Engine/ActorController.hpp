#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/EventQueue.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"

namespace Engine {
    struct ActorHandle {
        uint32_t id = UINT32_MAX;
    };

    struct ActorControllerSettings {
        float movementSpeed = 6.0f;
        float groundOffset = 0.65f;
        float facingTurnSpeed = 20.0f;
    };

    struct ActorState {
        WorldObjectHandle object;
        glm::vec3 velocity{};
        float facingRadians = 0.0f;
    };

    class ActorController {
    public:
        ActorHandle createActor(WorldObjectHandle object, ActorControllerSettings settings = {});
        void destroyActor(ActorHandle actor);

        void setPosition(ActorHandle actor, const glm::vec3& position, const TerrainSystem* terrain = nullptr, World* world = nullptr);
        std::optional<glm::vec3> position(ActorHandle actor, const World& world) const;
        std::optional<ActorState> state(ActorHandle actor) const;

        void fixedUpdate(ActorHandle actor, const EventQueue& events, const TerrainSystem& terrain, World& world, float dt);

    private:
        struct ActorRecord {
            bool alive = false;
            ActorState state;
            ActorControllerSettings settings;
        };

        ActorRecord* record(ActorHandle actor);
        const ActorRecord* record(ActorHandle actor) const;

        std::vector<ActorRecord> actors_;
    };
}
