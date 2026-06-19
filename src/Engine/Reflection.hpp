#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/AssetRegistry.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/TerrainImport.hpp"

namespace Engine {
    enum class OpaqueHandleKind {
        None,
        SceneActor,
        SceneComponent,
        SceneSystem,
        SceneMeshComponent,
        SceneSkinnedMeshComponent,
        SceneLightComponent,
        SceneCameraComponent,
        ScenePhysicsBody,
        ScenePhysicsCollider,
        SceneCharacter,
        Asset,
        TerrainSource,
        TerrainChunk,
        TerrainStableChunk,
    };

    struct OpaqueHandle {
        OpaqueHandleKind kind = OpaqueHandleKind::None;
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
        uint32_t owner = 0;
    };

    [[nodiscard]] constexpr bool isValid(OpaqueHandle handle)
    {
        return handle.kind != OpaqueHandleKind::None && handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(OpaqueHandle lhs, OpaqueHandle rhs)
    {
        return lhs.kind == rhs.kind &&
            lhs.index == rhs.index &&
            lhs.generation == rhs.generation &&
            lhs.owner == rhs.owner;
    }

    [[nodiscard]] constexpr bool operator!=(OpaqueHandle lhs, OpaqueHandle rhs)
    {
        return !(lhs == rhs);
    }

    enum class ReflectedValueType {
        None,
        Bool,
        Int64,
        UInt64,
        Float,
        String,
        Vec2,
        Vec3,
        Quat,
        Mat4,
        SceneObjectId,
        AssetId,
        TerrainSourceChunkId,
        OpaqueHandle,
    };

    using ReflectedValue = std::variant<
        std::monostate,
        bool,
        int64_t,
        uint64_t,
        float,
        std::string,
        glm::vec2,
        glm::vec3,
        glm::quat,
        glm::mat4,
        SceneObjectId,
        AssetId,
        TerrainSourceChunkId,
        OpaqueHandle>;

    enum class ReflectedPropertyFlag : uint32_t {
        None = 0,
        ReadOnly = 1u << 0,
        RuntimeOnly = 1u << 1,
        Serializable = 1u << 2,
        EditorVisible = 1u << 3,
        ScriptVisible = 1u << 4,
        Advanced = 1u << 5,
        Transient = 1u << 6,
        AssetReference = 1u << 7,
        StableReference = 1u << 8,
        RequiresExplicitApply = 1u << 9,
    };

    [[nodiscard]] constexpr ReflectedPropertyFlag operator|(ReflectedPropertyFlag lhs, ReflectedPropertyFlag rhs)
    {
        return static_cast<ReflectedPropertyFlag>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr ReflectedPropertyFlag operator&(ReflectedPropertyFlag lhs, ReflectedPropertyFlag rhs)
    {
        return static_cast<ReflectedPropertyFlag>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr bool hasFlag(ReflectedPropertyFlag flags, ReflectedPropertyFlag flag)
    {
        return static_cast<uint32_t>(flags & flag) != 0;
    }

    struct ReflectedPropertyDescriptor {
        uint32_t id = 0;
        std::string name;
        std::string displayName;
        std::string category;
        ReflectedValueType type = ReflectedValueType::None;
        ReflectedValue defaultValue;
        std::optional<ReflectedValue> minimum;
        std::optional<ReflectedValue> maximum;
        std::vector<std::string> enumLabels;
        std::string units;
        ReflectedPropertyFlag flags = ReflectedPropertyFlag::None;
        std::string documentation;
    };

    struct ReflectedObjectDescriptor {
        uint32_t id = 0;
        std::string name;
        std::string displayName;
        std::string category;
        std::vector<ReflectedPropertyDescriptor> properties;
    };

    enum class ReflectionStatus {
        Success,
        NotFound,
        DuplicateObject,
        DuplicateProperty,
        InvalidHandle,
        WrongHandleKind,
        WrongOwner,
        UnknownProperty,
        TypeMismatch,
        ReadOnly,
        ValidationFailed,
        Unsupported,
        SubsystemRejected,
    };

    struct ReflectionResult {
        ReflectionStatus status = ReflectionStatus::Success;
        std::string message;
        ReflectedValue value;
        bool changed = false;
    };

    struct ReflectionDiagnostics {
        uint32_t objectCount = 0;
        uint32_t propertyCount = 0;
        uint32_t duplicateObjectCount = 0;
        uint32_t duplicatePropertyCount = 0;
        std::string lastMessage;
    };

    [[nodiscard]] ReflectedValueType reflectedValueType(const ReflectedValue& value);

    class ReflectionRegistry {
    public:
        [[nodiscard]] ReflectionStatus registerObject(ReflectedObjectDescriptor descriptor);
        [[nodiscard]] const ReflectedObjectDescriptor* object(uint32_t objectId) const;
        [[nodiscard]] const ReflectedObjectDescriptor* object(const std::string& name) const;
        [[nodiscard]] const ReflectedPropertyDescriptor* property(uint32_t objectId, uint32_t propertyId) const;
        [[nodiscard]] const ReflectedPropertyDescriptor* property(uint32_t objectId, const std::string& propertyName) const;
        [[nodiscard]] std::vector<ReflectedObjectDescriptor> objects() const;
        [[nodiscard]] ReflectionDiagnostics diagnostics() const;

    private:
        std::vector<ReflectedObjectDescriptor> objects_;
        ReflectionDiagnostics diagnostics_;
    };
}
