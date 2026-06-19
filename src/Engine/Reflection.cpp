#include "Engine/Reflection.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Engine {
    ReflectedValueType reflectedValueType(const ReflectedValue& value)
    {
        return std::visit(
            [](const auto& typed) -> ReflectedValueType {
                using Value = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Value, std::monostate>) {
                    return ReflectedValueType::None;
                } else if constexpr (std::is_same_v<Value, bool>) {
                    return ReflectedValueType::Bool;
                } else if constexpr (std::is_same_v<Value, int64_t>) {
                    return ReflectedValueType::Int64;
                } else if constexpr (std::is_same_v<Value, uint64_t>) {
                    return ReflectedValueType::UInt64;
                } else if constexpr (std::is_same_v<Value, float>) {
                    return ReflectedValueType::Float;
                } else if constexpr (std::is_same_v<Value, std::string>) {
                    return ReflectedValueType::String;
                } else if constexpr (std::is_same_v<Value, glm::vec2>) {
                    return ReflectedValueType::Vec2;
                } else if constexpr (std::is_same_v<Value, glm::vec3>) {
                    return ReflectedValueType::Vec3;
                } else if constexpr (std::is_same_v<Value, glm::quat>) {
                    return ReflectedValueType::Quat;
                } else if constexpr (std::is_same_v<Value, glm::mat4>) {
                    return ReflectedValueType::Mat4;
                } else if constexpr (std::is_same_v<Value, SceneObjectId>) {
                    return ReflectedValueType::SceneObjectId;
                } else if constexpr (std::is_same_v<Value, AssetId>) {
                    return ReflectedValueType::AssetId;
                } else if constexpr (std::is_same_v<Value, TerrainSourceChunkId>) {
                    return ReflectedValueType::TerrainSourceChunkId;
                } else if constexpr (std::is_same_v<Value, OpaqueHandle>) {
                    return ReflectedValueType::OpaqueHandle;
                }
            },
            value);
    }

    ReflectionStatus ReflectionRegistry::registerObject(ReflectedObjectDescriptor descriptor)
    {
        if (descriptor.id == 0 || descriptor.name.empty()) {
            diagnostics_.lastMessage = "Reflected object descriptors require non-zero id and name.";
            return ReflectionStatus::ValidationFailed;
        }

        if (object(descriptor.id) || object(descriptor.name)) {
            ++diagnostics_.duplicateObjectCount;
            diagnostics_.lastMessage = "Duplicate reflected object descriptor.";
            return ReflectionStatus::DuplicateObject;
        }

        for (uint32_t lhs = 0; lhs < descriptor.properties.size(); ++lhs) {
            const ReflectedPropertyDescriptor& property = descriptor.properties[lhs];
            if (property.id == 0 || property.name.empty() || property.type == ReflectedValueType::None) {
                diagnostics_.lastMessage = "Reflected property descriptors require non-zero id, name, and type.";
                return ReflectionStatus::ValidationFailed;
            }
            if (reflectedValueType(property.defaultValue) != ReflectedValueType::None &&
                reflectedValueType(property.defaultValue) != property.type) {
                diagnostics_.lastMessage = "Reflected property default value type does not match descriptor type.";
                return ReflectionStatus::TypeMismatch;
            }
            for (uint32_t rhs = lhs + 1; rhs < descriptor.properties.size(); ++rhs) {
                if (property.id == descriptor.properties[rhs].id || property.name == descriptor.properties[rhs].name) {
                    ++diagnostics_.duplicatePropertyCount;
                    diagnostics_.lastMessage = "Duplicate reflected property descriptor.";
                    return ReflectionStatus::DuplicateProperty;
                }
            }
        }

        std::ranges::sort(descriptor.properties, [](const auto& lhs, const auto& rhs) {
            return lhs.id < rhs.id;
        });

        objects_.push_back(std::move(descriptor));
        std::ranges::sort(objects_, [](const auto& lhs, const auto& rhs) {
            return lhs.id < rhs.id;
        });

        diagnostics_.objectCount = static_cast<uint32_t>(objects_.size());
        diagnostics_.propertyCount = 0;
        for (const ReflectedObjectDescriptor& object : objects_) {
            diagnostics_.propertyCount += static_cast<uint32_t>(object.properties.size());
        }
        diagnostics_.lastMessage.clear();
        return ReflectionStatus::Success;
    }

    const ReflectedObjectDescriptor* ReflectionRegistry::object(uint32_t objectId) const
    {
        const auto it = std::ranges::find_if(objects_, [objectId](const ReflectedObjectDescriptor& descriptor) {
            return descriptor.id == objectId;
        });
        return it == objects_.end() ? nullptr : &*it;
    }

    const ReflectedObjectDescriptor* ReflectionRegistry::object(const std::string& name) const
    {
        const auto it = std::ranges::find_if(objects_, [&name](const ReflectedObjectDescriptor& descriptor) {
            return descriptor.name == name;
        });
        return it == objects_.end() ? nullptr : &*it;
    }

    const ReflectedPropertyDescriptor* ReflectionRegistry::property(uint32_t objectId, uint32_t propertyId) const
    {
        const ReflectedObjectDescriptor* descriptor = object(objectId);
        if (!descriptor) {
            return nullptr;
        }
        const auto it = std::ranges::find_if(descriptor->properties, [propertyId](const ReflectedPropertyDescriptor& property) {
            return property.id == propertyId;
        });
        return it == descriptor->properties.end() ? nullptr : &*it;
    }

    const ReflectedPropertyDescriptor* ReflectionRegistry::property(uint32_t objectId, const std::string& propertyName) const
    {
        const ReflectedObjectDescriptor* descriptor = object(objectId);
        if (!descriptor) {
            return nullptr;
        }
        const auto it = std::ranges::find_if(descriptor->properties, [&propertyName](const ReflectedPropertyDescriptor& property) {
            return property.name == propertyName;
        });
        return it == descriptor->properties.end() ? nullptr : &*it;
    }

    std::vector<ReflectedObjectDescriptor> ReflectionRegistry::objects() const
    {
        return objects_;
    }

    ReflectionDiagnostics ReflectionRegistry::diagnostics() const
    {
        return diagnostics_;
    }
}
