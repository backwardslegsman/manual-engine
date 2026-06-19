#pragma once

#include <cstdint>
#include <optional>

#include "Engine/Reflection.hpp"

namespace Engine {
    class AssetRegistry;
    class Scene;
    class SceneCharacterMovementSystem;
    class SceneNavigationService;
    class ScenePhysicsWorld;
    class SceneRenderBridge;
    class TerrainDataset;

    struct SceneActorHandle;
    struct SceneComponentHandle;
    struct SceneSystemHandle;
    struct SceneMeshComponentHandle;
    struct SceneSkinnedMeshComponentHandle;
    struct SceneLightComponentHandle;
    struct SceneCameraComponentHandle;
    struct ScenePhysicsBodyHandle;
    struct SceneColliderHandle;
    struct SceneCharacterHandle;
    struct AssetHandle;
    struct TerrainSourceHandle;
    struct TerrainChunkHandle;

    inline constexpr uint32_t SceneReflectionDefaultOwner = 1;

    enum class SceneReflectedObjectId : uint32_t {
        SceneActor = 1,
        SceneComponent = 2,
        SceneMeshComponent = 3,
        SceneSkinnedMeshComponent = 4,
        SceneLightComponent = 5,
        SceneCameraComponent = 6,
        ScenePhysicsBody = 7,
        ScenePhysicsCollider = 8,
        SceneCharacter = 9,
        Asset = 10,
        TerrainSource = 11,
        TerrainChunk = 12,
        NavigationService = 13,
    };

    enum class SceneReflectedPropertyId : uint32_t {
        StableId = 1,
        ActorState = 2,
        Parent = 3,
        LocalTranslation = 4,
        LocalRotation = 5,
        LocalScale = 6,
        LocalMatrix = 7,
        WorldMatrix = 8,
        ChildCount = 9,
        ComponentOwner = 20,
        ComponentType = 21,
        Enabled = 30,
        MaxDrawDistance = 31,
        RenderLayer = 32,
        MeshResourceId = 33,
        MaterialResourceId = 34,
        LightType = 40,
        LightName = 41,
        LightColor = 42,
        LightIntensity = 43,
        LightRange = 44,
        CameraFov = 50,
        CameraNear = 51,
        CameraFar = 52,
        PhysicsMotionType = 60,
        PhysicsLayer = 61,
        PhysicsLinearVelocity = 62,
        PhysicsAngularVelocity = 63,
        ColliderBody = 70,
        ColliderShapeType = 71,
        CharacterMaxSpeed = 80,
        CharacterSpeedScale = 81,
        CharacterMode = 82,
        CharacterGrounded = 83,
        CharacterVelocity = 84,
        AssetId = 90,
        AssetType = 91,
        AssetStatus = 92,
        AssetSourcePath = 93,
        AssetCanonicalPath = 94,
        TerrainSourceId = 100,
        TerrainSourceType = 101,
        TerrainDebugName = 102,
        TerrainChunkId = 110,
        TerrainChunkResolution = 111,
        TerrainChunkSize = 112,
        TerrainChunkOrigin = 113,
        NavigationRegisteredQueryCount = 120,
    };

    struct SceneReflectionContext {
        uint32_t owner = SceneReflectionDefaultOwner;
        Scene* scene = nullptr;
        SceneRenderBridge* renderBridge = nullptr;
        ScenePhysicsWorld* physics = nullptr;
        SceneCharacterMovementSystem* characters = nullptr;
        AssetRegistry* assets = nullptr;
        TerrainDataset* terrain = nullptr;
        SceneNavigationService* navigation = nullptr;
    };

    void registerSceneReflectionDescriptors(ReflectionRegistry& registry);

    [[nodiscard]] OpaqueHandle toOpaque(SceneActorHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneComponentHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneSystemHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneMeshComponentHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneSkinnedMeshComponentHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneLightComponentHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneCameraComponentHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(ScenePhysicsBodyHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneColliderHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(SceneCharacterHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(AssetHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(TerrainSourceHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(TerrainChunkHandle handle, uint32_t owner = SceneReflectionDefaultOwner);
    [[nodiscard]] OpaqueHandle toOpaque(TerrainSourceChunkId id, uint32_t owner = SceneReflectionDefaultOwner);

    [[nodiscard]] std::optional<SceneActorHandle> sceneActorFromOpaque(
        const SceneReflectionContext& context,
        OpaqueHandle handle);
    [[nodiscard]] std::optional<ScenePhysicsBodyHandle> scenePhysicsBodyFromOpaque(
        const SceneReflectionContext& context,
        OpaqueHandle handle);

    [[nodiscard]] ReflectionResult getReflectedProperty(
        const SceneReflectionContext& context,
        OpaqueHandle target,
        SceneReflectedPropertyId property);
    [[nodiscard]] ReflectionResult setReflectedProperty(
        const SceneReflectionContext& context,
        OpaqueHandle target,
        SceneReflectedPropertyId property,
        const ReflectedValue& value);
}
