#include "Engine/SceneReflection.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "Engine/AssetRegistry.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"
#include "Engine/SceneCharacterMovement.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    namespace {
        using ObjectId = SceneReflectedObjectId;
        using PropertyId = SceneReflectedPropertyId;

        constexpr ReflectedPropertyFlag DefaultFlags =
            ReflectedPropertyFlag::EditorVisible | ReflectedPropertyFlag::ScriptVisible;
        constexpr ReflectedPropertyFlag ReadOnlyFlags =
            DefaultFlags | ReflectedPropertyFlag::ReadOnly | ReflectedPropertyFlag::RuntimeOnly;
        constexpr ReflectedPropertyFlag RuntimeWriteFlags =
            DefaultFlags | ReflectedPropertyFlag::RuntimeOnly;
        constexpr ReflectedPropertyFlag StableReadOnlyFlags =
            ReadOnlyFlags | ReflectedPropertyFlag::StableReference;

        [[nodiscard]] ReflectionResult result(ReflectionStatus status, std::string message = {})
        {
            ReflectionResult reflectionResult;
            reflectionResult.status = status;
            reflectionResult.message = std::move(message);
            return reflectionResult;
        }

        [[nodiscard]] ReflectionResult valueResult(ReflectedValue value)
        {
            ReflectionResult reflectionResult;
            reflectionResult.value = std::move(value);
            return reflectionResult;
        }

        [[nodiscard]] ReflectionResult changedResult(bool changed)
        {
            ReflectionResult reflectionResult;
            reflectionResult.changed = changed;
            return reflectionResult;
        }

        [[nodiscard]] ReflectedPropertyDescriptor property(
            PropertyId id,
            std::string name,
            ReflectedValueType type,
            ReflectedPropertyFlag flags,
            ReflectedValue defaultValue = {})
        {
            ReflectedPropertyDescriptor descriptor;
            descriptor.id = static_cast<uint32_t>(id);
            descriptor.name = std::move(name);
            descriptor.displayName = descriptor.name;
            descriptor.type = type;
            descriptor.flags = flags;
            descriptor.defaultValue = std::move(defaultValue);
            return descriptor;
        }

        [[nodiscard]] ReflectedObjectDescriptor object(
            ObjectId id,
            std::string name,
            std::vector<ReflectedPropertyDescriptor> properties)
        {
            ReflectedObjectDescriptor descriptor;
            descriptor.id = static_cast<uint32_t>(id);
            descriptor.name = std::move(name);
            descriptor.displayName = descriptor.name;
            descriptor.category = "Scene";
            descriptor.properties = std::move(properties);
            return descriptor;
        }

        [[nodiscard]] bool ownerMatches(const SceneReflectionContext& context, OpaqueHandle handle)
        {
            return handle.owner == context.owner;
        }

        [[nodiscard]] bool requireKind(const SceneReflectionContext& context, OpaqueHandle handle, OpaqueHandleKind kind)
        {
            return isValid(handle) && handle.kind == kind && ownerMatches(context, handle);
        }

        template <typename T>
        [[nodiscard]] const T* as(const ReflectedValue& value)
        {
            return std::get_if<T>(&value);
        }

        [[nodiscard]] bool finite(float value)
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool finite(glm::vec3 value)
        {
            return finite(value.x) && finite(value.y) && finite(value.z);
        }

        [[nodiscard]] bool finite(glm::quat value)
        {
            return finite(value.w) && finite(value.x) && finite(value.y) && finite(value.z);
        }

        [[nodiscard]] uint64_t handleId(uint32_t id)
        {
            return id == UINT32_MAX ? 0u : static_cast<uint64_t>(id);
        }

        [[nodiscard]] OpaqueHandle parentValue(Scene& scene, SceneActorHandle actor, uint32_t owner)
        {
            const std::optional<SceneActorHandle> parent = scene.parent(actor);
            return parent ? toOpaque(*parent, owner) : OpaqueHandle{};
        }

        [[nodiscard]] ReflectionResult actorGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.scene || !requireKind(context, target, OpaqueHandleKind::SceneActor)) {
                return result(ReflectionStatus::InvalidHandle, "Invalid scene actor opaque handle.");
            }
            const SceneActorHandle actor{target.index, target.generation};
            if (!context.scene->contains(actor)) {
                return result(ReflectionStatus::InvalidHandle, "Scene actor handle is stale or pending destroy.");
            }

            switch (property) {
                case PropertyId::StableId:
                    return valueResult(context.scene->stableId(actor).value_or(SceneObjectId{}));
                case PropertyId::ActorState:
                    return valueResult(static_cast<int64_t>(context.scene->actorState(actor).value_or(SceneActorState::PendingDestroy)));
                case PropertyId::Parent:
                    return valueResult(parentValue(*context.scene, actor, context.owner));
                case PropertyId::LocalTranslation: {
                    const auto transform = context.scene->localTransform(actor);
                    return transform ? valueResult(transform->translation) : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::LocalRotation: {
                    const auto transform = context.scene->localTransform(actor);
                    return transform ? valueResult(transform->rotation) : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::LocalScale: {
                    const auto transform = context.scene->localTransform(actor);
                    return transform ? valueResult(transform->scale) : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::LocalMatrix: {
                    const auto matrix = context.scene->localMatrix(actor);
                    return matrix ? valueResult(*matrix) : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::WorldMatrix: {
                    const auto matrix = context.scene->worldMatrix(actor);
                    return matrix ? valueResult(*matrix) : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::ChildCount:
                    return valueResult(static_cast<uint64_t>(context.scene->children(actor).size()));
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult actorSet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.scene || !requireKind(context, target, OpaqueHandleKind::SceneActor)) {
                return result(ReflectionStatus::InvalidHandle, "Invalid scene actor opaque handle.");
            }
            const SceneActorHandle actor{target.index, target.generation};
            std::optional<SceneTransform> transform = context.scene->localTransform(actor);
            if (!transform) {
                return result(ReflectionStatus::InvalidHandle, "Scene actor handle is stale or pending destroy.");
            }

            switch (property) {
                case PropertyId::LocalTranslation: {
                    const glm::vec3* translation = as<glm::vec3>(value);
                    if (!translation) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*translation)) {
                        return result(ReflectionStatus::ValidationFailed, "Translation must be finite.");
                    }
                    transform->translation = *translation;
                    return context.scene->setLocalTransform(actor, *transform)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::LocalRotation: {
                    const glm::quat* rotation = as<glm::quat>(value);
                    if (!rotation) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*rotation)) {
                        return result(ReflectionStatus::ValidationFailed, "Rotation must be finite.");
                    }
                    transform->rotation = *rotation;
                    return context.scene->setLocalTransform(actor, *transform)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::LocalScale: {
                    const glm::vec3* scale = as<glm::vec3>(value);
                    if (!scale) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*scale)) {
                        return result(ReflectionStatus::ValidationFailed, "Scale must be finite.");
                    }
                    transform->scale = *scale;
                    return context.scene->setLocalTransform(actor, *transform)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
        }

        [[nodiscard]] ReflectionResult sceneComponentGet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property)
        {
            if (!context.scene || !requireKind(context, target, OpaqueHandleKind::SceneComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneComponentHandle component{target.index, target.generation};
            if (!context.scene->contains(component)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ComponentOwner: {
                    const auto owner = context.scene->componentOwner(component);
                    return owner ? valueResult(toOpaque(*owner, context.owner)) : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::ComponentType: {
                    const auto type = context.scene->componentType(component);
                    return type ? valueResult(static_cast<uint64_t>(type->value)) : result(ReflectionStatus::SubsystemRejected);
                }
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult meshGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneMeshComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto descriptor = context.renderBridge->meshDescriptor({target.index, target.generation});
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ComponentOwner:
                    return valueResult(toOpaque(descriptor->actor, context.owner));
                case PropertyId::Enabled:
                    return valueResult(descriptor->enabled);
                case PropertyId::MaxDrawDistance:
                    return valueResult(descriptor->maxDrawDistance);
                case PropertyId::RenderLayer:
                    return valueResult(static_cast<int64_t>(descriptor->layer));
                case PropertyId::MeshResourceId:
                    return valueResult(handleId(descriptor->mesh.id));
                case PropertyId::MaterialResourceId:
                    return valueResult(descriptor->material ? handleId(descriptor->material->id) : uint64_t{0});
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult meshSet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneMeshComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneMeshComponentHandle handle{target.index, target.generation};
            auto descriptor = context.renderBridge->meshDescriptor(handle);
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::Enabled: {
                    const bool* enabled = as<bool>(value);
                    if (!enabled) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    descriptor->enabled = *enabled;
                    return context.renderBridge->setMeshDescriptor(handle, *descriptor)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::MaxDrawDistance: {
                    const float* distance = as<float>(value);
                    if (!distance) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*distance) || *distance < 0.0f) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    descriptor->maxDrawDistance = *distance;
                    return context.renderBridge->setMeshDescriptor(handle, *descriptor)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
        }

        [[nodiscard]] ReflectionResult lightGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneLightComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto descriptor = context.renderBridge->lightDescriptor({target.index, target.generation});
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ComponentOwner:
                    return valueResult(toOpaque(descriptor->actor, context.owner));
                case PropertyId::Enabled:
                    return valueResult(descriptor->enabled);
                case PropertyId::LightType:
                    return valueResult(static_cast<int64_t>(descriptor->type));
                case PropertyId::LightName:
                    return valueResult(descriptor->name);
                case PropertyId::LightColor:
                    return valueResult(descriptor->color);
                case PropertyId::LightIntensity:
                    return valueResult(descriptor->intensity);
                case PropertyId::LightRange:
                    return valueResult(descriptor->range);
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult lightSet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneLightComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneLightComponentHandle handle{target.index, target.generation};
            auto descriptor = context.renderBridge->lightDescriptor(handle);
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::Enabled: {
                    const bool* enabled = as<bool>(value);
                    if (!enabled) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    descriptor->enabled = *enabled;
                    break;
                }
                case PropertyId::LightName: {
                    const std::string* name = as<std::string>(value);
                    if (!name) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    descriptor->name = *name;
                    break;
                }
                case PropertyId::LightColor: {
                    const glm::vec3* color = as<glm::vec3>(value);
                    if (!color) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*color)) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    descriptor->color = *color;
                    break;
                }
                case PropertyId::LightIntensity: {
                    const float* intensity = as<float>(value);
                    if (!intensity) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*intensity) || *intensity < 0.0f) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    descriptor->intensity = *intensity;
                    break;
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
            return context.renderBridge->setLightDescriptor(handle, *descriptor)
                ? changedResult(true)
                : result(ReflectionStatus::SubsystemRejected);
        }

        [[nodiscard]] ReflectionResult cameraGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneCameraComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto descriptor = context.renderBridge->cameraDescriptor({target.index, target.generation});
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ComponentOwner:
                    return valueResult(toOpaque(descriptor->actor, context.owner));
                case PropertyId::Enabled:
                    return valueResult(descriptor->enabled);
                case PropertyId::CameraFov:
                    return valueResult(descriptor->verticalFieldOfViewRadians);
                case PropertyId::CameraNear:
                    return valueResult(descriptor->nearPlane);
                case PropertyId::CameraFar:
                    return valueResult(descriptor->farPlane);
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult cameraSet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneCameraComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneCameraComponentHandle handle{target.index, target.generation};
            auto descriptor = context.renderBridge->cameraDescriptor(handle);
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::Enabled: {
                    const bool* enabled = as<bool>(value);
                    if (!enabled) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    descriptor->enabled = *enabled;
                    break;
                }
                case PropertyId::CameraFov: {
                    const float* fov = as<float>(value);
                    if (!fov) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*fov) || *fov <= 0.0f) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    descriptor->verticalFieldOfViewRadians = *fov;
                    break;
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
            return context.renderBridge->setCameraDescriptor(handle, *descriptor)
                ? changedResult(true)
                : result(ReflectionStatus::SubsystemRejected);
        }

        [[nodiscard]] ReflectionResult skinnedGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneSkinnedMeshComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto descriptor = context.renderBridge->skinnedMeshDescriptor({target.index, target.generation});
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ComponentOwner:
                    return valueResult(toOpaque(descriptor->actor, context.owner));
                case PropertyId::Enabled:
                    return valueResult(descriptor->enabled);
                case PropertyId::MaxDrawDistance:
                    return valueResult(descriptor->maxDrawDistance);
                case PropertyId::RenderLayer:
                    return valueResult(static_cast<int64_t>(descriptor->layer));
                case PropertyId::MeshResourceId:
                    return valueResult(handleId(descriptor->mesh.id));
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult skinnedSet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.renderBridge || !requireKind(context, target, OpaqueHandleKind::SceneSkinnedMeshComponent)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneSkinnedMeshComponentHandle handle{target.index, target.generation};
            auto descriptor = context.renderBridge->skinnedMeshDescriptor(handle);
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::Enabled: {
                    const bool* enabled = as<bool>(value);
                    if (!enabled) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    descriptor->enabled = *enabled;
                    break;
                }
                case PropertyId::MaxDrawDistance: {
                    const float* distance = as<float>(value);
                    if (!distance) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*distance) || *distance < 0.0f) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    descriptor->maxDrawDistance = *distance;
                    break;
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
            return context.renderBridge->setSkinnedMeshDescriptor(handle, *descriptor)
                ? changedResult(true)
                : result(ReflectionStatus::SubsystemRejected);
        }

        [[nodiscard]] ReflectionResult physicsBodyGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.physics || !requireKind(context, target, OpaqueHandleKind::ScenePhysicsBody)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const ScenePhysicsBodyHandle handle{target.index, target.generation};
            const auto descriptor = context.physics->body(handle);
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ComponentOwner:
                    return valueResult(toOpaque(descriptor->actor, context.owner));
                case PropertyId::Enabled:
                    return valueResult(descriptor->enabled);
                case PropertyId::PhysicsMotionType:
                    return valueResult(static_cast<int64_t>(descriptor->motionType));
                case PropertyId::PhysicsLayer:
                    return valueResult(static_cast<uint64_t>(descriptor->layer.value));
                case PropertyId::PhysicsLinearVelocity:
                    return valueResult(descriptor->initialLinearVelocity);
                case PropertyId::PhysicsAngularVelocity:
                    return valueResult(descriptor->initialAngularVelocity);
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult physicsBodySet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.physics || !requireKind(context, target, OpaqueHandleKind::ScenePhysicsBody)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const ScenePhysicsBodyHandle handle{target.index, target.generation};
            if (!context.physics->contains(handle)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::Enabled: {
                    const bool* enabled = as<bool>(value);
                    if (!enabled) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    return context.physics->setBodyEnabled(handle, *enabled)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::PhysicsMotionType: {
                    const int64_t* motion = as<int64_t>(value);
                    if (!motion) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (*motion < 0 || *motion > static_cast<int64_t>(ScenePhysicsMotionType::Dynamic)) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    return context.physics->setMotionType(handle, static_cast<ScenePhysicsMotionType>(*motion))
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::PhysicsLinearVelocity: {
                    const glm::vec3* velocity = as<glm::vec3>(value);
                    if (!velocity) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*velocity)) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    return context.physics->setLinearVelocity(handle, *velocity)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::PhysicsAngularVelocity: {
                    const glm::vec3* velocity = as<glm::vec3>(value);
                    if (!velocity) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*velocity)) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    return context.physics->setAngularVelocity(handle, *velocity)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
        }

        [[nodiscard]] ReflectionResult colliderGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.physics || !requireKind(context, target, OpaqueHandleKind::ScenePhysicsCollider)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto descriptor = context.physics->collider({target.index, target.generation});
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::ColliderBody:
                    return valueResult(toOpaque(descriptor->body, context.owner));
                case PropertyId::ColliderShapeType:
                    return valueResult(static_cast<int64_t>(descriptor->shape.type));
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult characterGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.characters || !requireKind(context, target, OpaqueHandleKind::SceneCharacter)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneCharacterHandle handle{target.index, target.generation};
            const auto descriptor = context.characters->descriptor(handle);
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto state = context.characters->state(handle);
            switch (property) {
                case PropertyId::ComponentOwner:
                    return valueResult(toOpaque(descriptor->actor, context.owner));
                case PropertyId::Enabled:
                    return valueResult(descriptor->enabled);
                case PropertyId::CharacterMaxSpeed:
                    return valueResult(descriptor->maxSpeed);
                case PropertyId::CharacterMode:
                    return state ? valueResult(static_cast<int64_t>(state->mode)) : result(ReflectionStatus::SubsystemRejected);
                case PropertyId::CharacterGrounded:
                    return state ? valueResult(state->grounded) : result(ReflectionStatus::SubsystemRejected);
                case PropertyId::CharacterVelocity:
                    return state ? valueResult(state->velocity) : result(ReflectionStatus::SubsystemRejected);
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult characterSet(
            const SceneReflectionContext& context,
            OpaqueHandle target,
            PropertyId property,
            const ReflectedValue& value)
        {
            if (!context.characters || !requireKind(context, target, OpaqueHandleKind::SceneCharacter)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const SceneCharacterHandle handle{target.index, target.generation};
            switch (property) {
                case PropertyId::Enabled: {
                    const bool* enabled = as<bool>(value);
                    if (!enabled) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    return context.characters->setEnabled(handle, *enabled)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                case PropertyId::CharacterSpeedScale: {
                    const float* speedScale = as<float>(value);
                    if (!speedScale) {
                        return result(ReflectionStatus::TypeMismatch);
                    }
                    if (!finite(*speedScale)) {
                        return result(ReflectionStatus::ValidationFailed);
                    }
                    SceneCharacterMoveInput input;
                    input.speedScale = std::clamp(*speedScale, 0.0f, 1.0f);
                    return context.characters->setMoveInput(handle, input)
                        ? changedResult(true)
                        : result(ReflectionStatus::SubsystemRejected);
                }
                default:
                    return result(ReflectionStatus::ReadOnly);
            }
        }

        [[nodiscard]] ReflectionResult assetGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.assets || !requireKind(context, target, OpaqueHandleKind::Asset)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto metadata = context.assets->metadata({target.index, target.generation});
            if (!metadata) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::AssetId:
                    return valueResult(metadata->id);
                case PropertyId::AssetType:
                    return valueResult(static_cast<int64_t>(metadata->type));
                case PropertyId::AssetStatus:
                    return valueResult(static_cast<int64_t>(metadata->status));
                case PropertyId::AssetSourcePath:
                    return valueResult(metadata->sourcePath.string());
                case PropertyId::AssetCanonicalPath:
                    return valueResult(metadata->canonicalPath.string());
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult terrainSourceGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.terrain || !requireKind(context, target, OpaqueHandleKind::TerrainSource)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto descriptor = context.terrain->sourceMetadata({target.index, target.generation});
            if (!descriptor) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::TerrainSourceId:
                    return valueResult(descriptor->sourceId);
                case PropertyId::TerrainSourceType:
                    return valueResult(static_cast<int64_t>(descriptor->type));
                case PropertyId::TerrainDebugName:
                    return valueResult(descriptor->debugName);
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }

        [[nodiscard]] ReflectionResult terrainChunkGet(const SceneReflectionContext& context, OpaqueHandle target, PropertyId property)
        {
            if (!context.terrain || !requireKind(context, target, OpaqueHandleKind::TerrainChunk)) {
                return result(ReflectionStatus::InvalidHandle);
            }
            const auto chunk = context.terrain->chunk({target.index, target.generation});
            if (!chunk) {
                return result(ReflectionStatus::InvalidHandle);
            }
            switch (property) {
                case PropertyId::TerrainChunkId:
                    return valueResult(chunk->id);
                case PropertyId::TerrainChunkResolution:
                    return valueResult(static_cast<uint64_t>(chunk->resolution));
                case PropertyId::TerrainChunkSize:
                    return valueResult(chunk->size);
                case PropertyId::TerrainChunkOrigin:
                    return valueResult(chunk->origin);
                default:
                    return result(ReflectionStatus::UnknownProperty);
            }
        }
    }

    void registerSceneReflectionDescriptors(ReflectionRegistry& registry)
    {
        [[maybe_unused]] ReflectionStatus status = registry.registerObject(object(ObjectId::SceneActor, "SceneActor", {
            property(PropertyId::StableId, "stableId", ReflectedValueType::SceneObjectId, StableReadOnlyFlags),
            property(PropertyId::ActorState, "actorState", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::Parent, "parent", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::LocalTranslation, "localTranslation", ReflectedValueType::Vec3, RuntimeWriteFlags, glm::vec3{0.0f}),
            property(PropertyId::LocalRotation, "localRotation", ReflectedValueType::Quat, RuntimeWriteFlags, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}),
            property(PropertyId::LocalScale, "localScale", ReflectedValueType::Vec3, RuntimeWriteFlags, glm::vec3{1.0f}),
            property(PropertyId::LocalMatrix, "localMatrix", ReflectedValueType::Mat4, ReadOnlyFlags),
            property(PropertyId::WorldMatrix, "worldMatrix", ReflectedValueType::Mat4, ReadOnlyFlags),
            property(PropertyId::ChildCount, "childCount", ReflectedValueType::UInt64, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::SceneComponent, "SceneComponent", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::ComponentType, "componentType", ReflectedValueType::UInt64, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::SceneMeshComponent, "SceneMeshComponent", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::Enabled, "enabled", ReflectedValueType::Bool, RuntimeWriteFlags, true),
            property(PropertyId::MaxDrawDistance, "maxDrawDistance", ReflectedValueType::Float, RuntimeWriteFlags, 0.0f),
            property(PropertyId::RenderLayer, "renderLayer", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::MeshResourceId, "meshResourceId", ReflectedValueType::UInt64, ReadOnlyFlags),
            property(PropertyId::MaterialResourceId, "materialResourceId", ReflectedValueType::UInt64, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::SceneSkinnedMeshComponent, "SceneSkinnedMeshComponent", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::Enabled, "enabled", ReflectedValueType::Bool, RuntimeWriteFlags, true),
            property(PropertyId::MaxDrawDistance, "maxDrawDistance", ReflectedValueType::Float, RuntimeWriteFlags, 0.0f),
            property(PropertyId::RenderLayer, "renderLayer", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::MeshResourceId, "meshResourceId", ReflectedValueType::UInt64, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::SceneLightComponent, "SceneLightComponent", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::Enabled, "enabled", ReflectedValueType::Bool, RuntimeWriteFlags, true),
            property(PropertyId::LightType, "lightType", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::LightName, "name", ReflectedValueType::String, RuntimeWriteFlags, std::string{}),
            property(PropertyId::LightColor, "color", ReflectedValueType::Vec3, RuntimeWriteFlags, glm::vec3{1.0f}),
            property(PropertyId::LightIntensity, "intensity", ReflectedValueType::Float, RuntimeWriteFlags, 1.0f),
            property(PropertyId::LightRange, "range", ReflectedValueType::Float, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::SceneCameraComponent, "SceneCameraComponent", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::Enabled, "enabled", ReflectedValueType::Bool, RuntimeWriteFlags, true),
            property(PropertyId::CameraFov, "verticalFieldOfViewRadians", ReflectedValueType::Float, RuntimeWriteFlags, 1.0471976f),
            property(PropertyId::CameraNear, "nearPlane", ReflectedValueType::Float, ReadOnlyFlags),
            property(PropertyId::CameraFar, "farPlane", ReflectedValueType::Float, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::ScenePhysicsBody, "ScenePhysicsBody", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::Enabled, "enabled", ReflectedValueType::Bool, RuntimeWriteFlags, true),
            property(PropertyId::PhysicsMotionType, "motionType", ReflectedValueType::Int64, RuntimeWriteFlags),
            property(PropertyId::PhysicsLayer, "layer", ReflectedValueType::UInt64, ReadOnlyFlags),
            property(PropertyId::PhysicsLinearVelocity, "linearVelocity", ReflectedValueType::Vec3, RuntimeWriteFlags),
            property(PropertyId::PhysicsAngularVelocity, "angularVelocity", ReflectedValueType::Vec3, RuntimeWriteFlags),
        }));
        status = registry.registerObject(object(ObjectId::ScenePhysicsCollider, "ScenePhysicsCollider", {
            property(PropertyId::ColliderBody, "body", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::ColliderShapeType, "shapeType", ReflectedValueType::Int64, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::SceneCharacter, "SceneCharacter", {
            property(PropertyId::ComponentOwner, "owner", ReflectedValueType::OpaqueHandle, ReadOnlyFlags),
            property(PropertyId::Enabled, "enabled", ReflectedValueType::Bool, RuntimeWriteFlags, true),
            property(PropertyId::CharacterMaxSpeed, "maxSpeed", ReflectedValueType::Float, ReadOnlyFlags),
            property(PropertyId::CharacterSpeedScale, "speedScale", ReflectedValueType::Float, RuntimeWriteFlags, 0.0f),
            property(PropertyId::CharacterMode, "mode", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::CharacterGrounded, "grounded", ReflectedValueType::Bool, ReadOnlyFlags),
            property(PropertyId::CharacterVelocity, "velocity", ReflectedValueType::Vec3, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::Asset, "Asset", {
            property(PropertyId::AssetId, "assetId", ReflectedValueType::AssetId, StableReadOnlyFlags),
            property(PropertyId::AssetType, "assetType", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::AssetStatus, "status", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::AssetSourcePath, "sourcePath", ReflectedValueType::String, ReadOnlyFlags),
            property(PropertyId::AssetCanonicalPath, "canonicalPath", ReflectedValueType::String, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::TerrainSource, "TerrainSource", {
            property(PropertyId::TerrainSourceId, "sourceId", ReflectedValueType::AssetId, StableReadOnlyFlags),
            property(PropertyId::TerrainSourceType, "sourceType", ReflectedValueType::Int64, ReadOnlyFlags),
            property(PropertyId::TerrainDebugName, "debugName", ReflectedValueType::String, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::TerrainChunk, "TerrainChunk", {
            property(PropertyId::TerrainChunkId, "chunkId", ReflectedValueType::TerrainSourceChunkId, StableReadOnlyFlags),
            property(PropertyId::TerrainChunkResolution, "resolution", ReflectedValueType::UInt64, ReadOnlyFlags),
            property(PropertyId::TerrainChunkSize, "size", ReflectedValueType::Float, ReadOnlyFlags),
            property(PropertyId::TerrainChunkOrigin, "origin", ReflectedValueType::Vec3, ReadOnlyFlags),
        }));
        status = registry.registerObject(object(ObjectId::NavigationService, "SceneNavigationService", {
            property(PropertyId::NavigationRegisteredQueryCount, "debugRequestCount", ReflectedValueType::UInt64, ReadOnlyFlags),
        }));
    }

    OpaqueHandle toOpaque(SceneActorHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneActor, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneComponentHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneComponent, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneSystemHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneSystem, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneMeshComponentHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneMeshComponent, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneSkinnedMeshComponentHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneSkinnedMeshComponent, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneLightComponentHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneLightComponent, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneCameraComponentHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneCameraComponent, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(ScenePhysicsBodyHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::ScenePhysicsBody, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneColliderHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::ScenePhysicsCollider, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(SceneCharacterHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::SceneCharacter, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(AssetHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::Asset, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(TerrainSourceHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::TerrainSource, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(TerrainChunkHandle handle, uint32_t owner)
    {
        return {OpaqueHandleKind::TerrainChunk, handle.index, handle.generation, owner};
    }

    OpaqueHandle toOpaque(TerrainSourceChunkId id, uint32_t owner)
    {
        const uint32_t source = static_cast<uint32_t>(id.source.value & 0xffffffffu);
        const uint32_t coord = static_cast<uint32_t>((static_cast<uint32_t>(id.coord.x) * 73856093u) ^
            (static_cast<uint32_t>(id.coord.z) * 19349663u));
        return {OpaqueHandleKind::TerrainStableChunk, source ^ coord, 1, owner};
    }

    std::optional<SceneActorHandle> sceneActorFromOpaque(const SceneReflectionContext& context, OpaqueHandle handle)
    {
        if (!context.scene || !requireKind(context, handle, OpaqueHandleKind::SceneActor)) {
            return std::nullopt;
        }
        const SceneActorHandle actor{handle.index, handle.generation};
        return context.scene->contains(actor) ? std::optional<SceneActorHandle>{actor} : std::nullopt;
    }

    std::optional<ScenePhysicsBodyHandle> scenePhysicsBodyFromOpaque(const SceneReflectionContext& context, OpaqueHandle handle)
    {
        if (!context.physics || !requireKind(context, handle, OpaqueHandleKind::ScenePhysicsBody)) {
            return std::nullopt;
        }
        const ScenePhysicsBodyHandle body{handle.index, handle.generation};
        return context.physics->contains(body) ? std::optional<ScenePhysicsBodyHandle>{body} : std::nullopt;
    }

    ReflectionResult getReflectedProperty(
        const SceneReflectionContext& context,
        OpaqueHandle target,
        SceneReflectedPropertyId property)
    {
        if (!isValid(target)) {
            return result(ReflectionStatus::InvalidHandle);
        }
        if (!ownerMatches(context, target)) {
            return result(ReflectionStatus::WrongOwner);
        }
        switch (target.kind) {
            case OpaqueHandleKind::SceneActor:
                return actorGet(context, target, property);
            case OpaqueHandleKind::SceneComponent:
                return sceneComponentGet(context, target, property);
            case OpaqueHandleKind::SceneMeshComponent:
                return meshGet(context, target, property);
            case OpaqueHandleKind::SceneSkinnedMeshComponent:
                return skinnedGet(context, target, property);
            case OpaqueHandleKind::SceneLightComponent:
                return lightGet(context, target, property);
            case OpaqueHandleKind::SceneCameraComponent:
                return cameraGet(context, target, property);
            case OpaqueHandleKind::ScenePhysicsBody:
                return physicsBodyGet(context, target, property);
            case OpaqueHandleKind::ScenePhysicsCollider:
                return colliderGet(context, target, property);
            case OpaqueHandleKind::SceneCharacter:
                return characterGet(context, target, property);
            case OpaqueHandleKind::Asset:
                return assetGet(context, target, property);
            case OpaqueHandleKind::TerrainSource:
                return terrainSourceGet(context, target, property);
            case OpaqueHandleKind::TerrainChunk:
                return terrainChunkGet(context, target, property);
            default:
                return result(ReflectionStatus::Unsupported);
        }
    }

    ReflectionResult setReflectedProperty(
        const SceneReflectionContext& context,
        OpaqueHandle target,
        SceneReflectedPropertyId property,
        const ReflectedValue& value)
    {
        if (!isValid(target)) {
            return result(ReflectionStatus::InvalidHandle);
        }
        if (!ownerMatches(context, target)) {
            return result(ReflectionStatus::WrongOwner);
        }
        switch (target.kind) {
            case OpaqueHandleKind::SceneActor:
                return actorSet(context, target, property, value);
            case OpaqueHandleKind::SceneMeshComponent:
                return meshSet(context, target, property, value);
            case OpaqueHandleKind::SceneSkinnedMeshComponent:
                return skinnedSet(context, target, property, value);
            case OpaqueHandleKind::SceneLightComponent:
                return lightSet(context, target, property, value);
            case OpaqueHandleKind::SceneCameraComponent:
                return cameraSet(context, target, property, value);
            case OpaqueHandleKind::ScenePhysicsBody:
                return physicsBodySet(context, target, property, value);
            case OpaqueHandleKind::SceneCharacter:
                return characterSet(context, target, property, value);
            default:
                return result(ReflectionStatus::ReadOnly);
        }
    }
}
