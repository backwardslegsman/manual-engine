#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/NavigationRuntime.hpp"
#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Engine {
    struct SceneCharacterHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    enum class SceneCharacterMovementMode {
        Disabled,
        Walking,
        Falling,
    };

    enum class SceneCharacterMovementStatus {
        Success,
        InvalidCharacter,
        InvalidActor,
        InvalidDescriptor,
        PhysicsBodyUnavailable,
        NavigationUnavailable,
        NoPath,
        Blocked,
    };

    enum class SceneCharacterDebugRequestType {
        GroundProbe,
        MovementSweep,
        StepProbe,
        Path,
    };

    struct SceneCharacterDescriptor {
        SceneActorHandle actor;
        float radius = 0.35f;
        float height = 1.8f;
        float maxSpeed = 4.0f;
        float acceleration = 20.0f;
        float braking = 24.0f;
        float gravity = 24.0f;
        float slopeLimitDegrees = 45.0f;
        float stepHeight = 0.35f;
        float snapDistance = 0.25f;
        ScenePhysicsLayer physicsLayer{2};
        ScenePhysicsFilter physicsFilter{UINT32_MAX, 2, true};
        bool enabled = true;
        std::string debugName;
    };

    struct SceneCharacterMoveInput {
        glm::vec3 direction{0.0f};
        float speedScale = 0.0f;
        std::optional<glm::vec3> faceDirection;
        bool jump = false;
    };

    struct SceneCharacterPathRequest {
        glm::vec3 goal{0.0f};
        NavigationAgentConfig agent;
        NavigationQueryFilter filter;
        float waypointAcceptanceRadius = 0.5f;
        float repathIntervalSeconds = 0.0f;
        bool allowPartialPath = false;
    };

    struct SceneCharacterState {
        SceneCharacterMovementMode mode = SceneCharacterMovementMode::Disabled;
        bool grounded = false;
        glm::vec3 floorNormal{0.0f, 1.0f, 0.0f};
        float floorDistance = 0.0f;
        glm::vec3 velocity{0.0f};
        NavPath activePath;
        uint32_t activeWaypointIndex = 0;
        SceneCharacterMovementStatus lastStatus = SceneCharacterMovementStatus::Success;
        std::string lastMessage;
    };

    struct SceneCharacterMovementDiagnostics {
        uint32_t characterCount = 0;
        uint32_t enabledCount = 0;
        uint32_t disabledCount = 0;
        uint32_t groundedCount = 0;
        uint32_t fallingCount = 0;
        uint32_t invalidOwnerCleanupCount = 0;
        uint32_t failedSweepCount = 0;
        uint32_t pathQueryCount = 0;
        uint64_t lastUpdateMicroseconds = 0;
        std::vector<std::string> warnings;
    };

    struct SceneCharacterDebugRequest {
        SceneCharacterDebugRequestType type = SceneCharacterDebugRequestType::MovementSweep;
        SceneCharacterMovementStatus status = SceneCharacterMovementStatus::Success;
        SceneCharacterHandle character;
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    [[nodiscard]] constexpr bool isValid(SceneCharacterHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(SceneCharacterHandle lhs, SceneCharacterHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneCharacterHandle lhs, SceneCharacterHandle rhs)
    {
        return !(lhs == rhs);
    }

    class SceneCharacterMovementSystem {
    public:
        SceneCharacterMovementSystem(Scene& scene, ScenePhysicsWorld& physics);
        ~SceneCharacterMovementSystem();

        SceneCharacterMovementSystem(const SceneCharacterMovementSystem&) = delete;
        SceneCharacterMovementSystem& operator=(const SceneCharacterMovementSystem&) = delete;

        void setNavigationService(SceneNavigationService* navigation);

        [[nodiscard]] SceneCharacterHandle createCharacter(SceneCharacterDescriptor descriptor);
        bool destroyCharacter(SceneCharacterHandle character);
        [[nodiscard]] bool contains(SceneCharacterHandle character) const;
        [[nodiscard]] std::optional<SceneCharacterHandle> characterForActor(SceneActorHandle actor) const;
        [[nodiscard]] std::optional<SceneCharacterDescriptor> descriptor(SceneCharacterHandle character) const;
        [[nodiscard]] std::optional<SceneCharacterState> state(SceneCharacterHandle character) const;

        bool setEnabled(SceneCharacterHandle character, bool enabled);
        bool setMoveInput(SceneCharacterHandle character, const SceneCharacterMoveInput& input);
        bool requestPathTo(SceneCharacterHandle character, const SceneCharacterPathRequest& request);
        bool clearPath(SceneCharacterHandle character);

        void updateFixed(float deltaSeconds);
        [[nodiscard]] SceneSystemHandle registerMovementSystem();
        bool unregisterMovementSystem();

        [[nodiscard]] SceneCharacterMovementDiagnostics diagnostics() const;
        [[nodiscard]] std::vector<SceneCharacterDebugRequest> debugRequests() const;
        void clearDebugRequests();

    private:
        struct CharacterRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneCharacterDescriptor descriptor;
            SceneCharacterMoveInput input;
            SceneCharacterPathRequest pathRequest;
            bool hasPathRequest = false;
            SceneCharacterState state;
            ScenePhysicsBodyHandle body;
            SceneColliderHandle collider;
            ScenePhysicsBodyHandle groundBody;
            SceneColliderHandle groundCollider;
            uint32_t debugTick = 0;
        };

        struct GroundProbe {
            bool hit = false;
            bool walkable = false;
            glm::vec3 position{0.0f};
            glm::vec3 normal{0.0f, 1.0f, 0.0f};
            float distance = 0.0f;
            ScenePhysicsBodyHandle body;
            SceneColliderHandle collider;
        };

        [[nodiscard]] CharacterRecord* record(SceneCharacterHandle character);
        [[nodiscard]] const CharacterRecord* record(SceneCharacterHandle character) const;
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        [[nodiscard]] SceneCharacterHandle handleForIndex(uint32_t index) const;
        [[nodiscard]] bool validDescriptor(const SceneCharacterDescriptor& descriptor) const;
        [[nodiscard]] ScenePhysicsShapeDescriptor capsuleShape(const SceneCharacterDescriptor& descriptor) const;
        [[nodiscard]] ScenePhysicsCapsuleShape capsuleQuery(const SceneCharacterDescriptor& descriptor) const;
        [[nodiscard]] GroundProbe probeGround(
            SceneCharacterHandle handle,
            const CharacterRecord& record,
            const glm::vec3& position);
        [[nodiscard]] bool walkableNormal(const SceneCharacterDescriptor& descriptor, const glm::vec3& normal) const;
        [[nodiscard]] glm::vec3 pathDirection(CharacterRecord& record, const glm::vec3& position);
        [[nodiscard]] glm::vec3 desiredInputDirection(CharacterRecord& record, const glm::vec3& position);
        [[nodiscard]] glm::vec3 moveWithCollision(
            SceneCharacterHandle handle,
            CharacterRecord& record,
            const glm::vec3& position,
            const glm::vec3& desiredPosition,
            float deltaSeconds);
        [[nodiscard]] std::optional<glm::vec3> tryStep(
            SceneCharacterHandle handle,
            CharacterRecord& record,
            const glm::vec3& position,
            const glm::vec3& horizontalDelta);
        void setRecordStatus(CharacterRecord& record, SceneCharacterMovementStatus status, std::string message);
        void cleanupInvalidOwners();
        void refreshDiagnostics();
        void freeRecord(uint32_t index);
        void appendDebug(SceneCharacterDebugRequest request);

        Scene& scene_;
        ScenePhysicsWorld& physics_;
        SceneNavigationService* navigation_ = nullptr;
        std::vector<CharacterRecord> characters_;
        std::optional<SceneSystemHandle> systemHandle_;
        SceneCharacterMovementDiagnostics diagnostics_;
        std::vector<SceneCharacterDebugRequest> debugRequests_;
    };
}
