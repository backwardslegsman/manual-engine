#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Scene.hpp"

namespace Engine {
    using ObjectArchetypeId = std::string;

    struct ObjectArchetypeDescriptor {
        ObjectArchetypeId id;
        std::string displayName;
        std::filesystem::path meshPath;
        std::array<uint8_t, 4> solidColor{255, 255, 255, 255};
        glm::vec3 scale{1.0f};
        Renderer::Aabb localBounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        float terrainYOffset = 0.0f;
        glm::vec3 angularVelocity{};
        std::vector<std::string> tags;
        std::string resourceId;
        uint32_t resourceAmount = 1;
    };

    bool hasTag(const ObjectArchetypeDescriptor& descriptor, std::string_view tag);
    std::string tagsToString(const ObjectArchetypeDescriptor& descriptor);

    struct ObjectArchetypeLoadResult;

    class ObjectArchetypeCatalog {
    public:
        static ObjectArchetypeCatalog sampleDefaults();
        static ObjectArchetypeLoadResult loadFromYaml(const std::filesystem::path& path);

        void add(ObjectArchetypeDescriptor descriptor);
        const ObjectArchetypeDescriptor* find(std::string_view id) const;
        std::vector<const ObjectArchetypeDescriptor*> all() const;

    private:
        std::vector<ObjectArchetypeDescriptor> archetypes_;
    };

    struct ObjectArchetypeLoadResult {
        bool success = false;
        std::string error;
        ObjectArchetypeCatalog catalog;
    };
}
