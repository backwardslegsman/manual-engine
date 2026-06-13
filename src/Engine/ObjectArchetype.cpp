#include "Engine/ObjectArchetype.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace {
    glm::vec3 readVec3(const YAML::Node& node, const glm::vec3& fallback)
    {
        if (!node || !node.IsSequence() || node.size() != 3) {
            return fallback;
        }

        return {
            node[0].as<float>(fallback.x),
            node[1].as<float>(fallback.y),
            node[2].as<float>(fallback.z),
        };
    }

    std::array<uint8_t, 4> readColor(const YAML::Node& node, std::array<uint8_t, 4> fallback)
    {
        if (!node || !node.IsSequence() || (node.size() != 3 && node.size() != 4)) {
            return fallback;
        }

        std::array<uint8_t, 4> color = fallback;
        for (std::size_t index = 0; index < node.size(); ++index) {
            color[index] = static_cast<uint8_t>(std::clamp(node[index].as<int>(color[index]), 0, 255));
        }
        if (node.size() == 3) {
            color[3] = 255;
        }
        return color;
    }

    Renderer::Aabb readBounds(const YAML::Node& node, const Renderer::Aabb& fallback)
    {
        if (!node) {
            return fallback;
        }

        return {
            readVec3(node["min"], fallback.min),
            readVec3(node["max"], fallback.max),
        };
    }

    std::vector<std::string> readTags(const YAML::Node& node)
    {
        std::vector<std::string> tags;
        if (!node || !node.IsSequence()) {
            return tags;
        }

        for (const YAML::Node& tag : node) {
            const std::string value = tag.as<std::string>(std::string{});
            if (!value.empty()) {
                tags.push_back(value);
            }
        }
        return tags;
    }
}

namespace Engine {
    ObjectArchetypeCatalog ObjectArchetypeCatalog::sampleDefaults()
    {
        ObjectArchetypeCatalog catalog;
        catalog.add({
            "tree_cluster",
            "Tree Cluster",
            {},
            {64, 190, 110, 255},
            {0.55f, 1.15f, 0.55f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            1.15f,
            {0.0f, 0.10f, 0.0f},
            {"procedural", "removable", "inspectable", "resource_node", "blocking", "vegetation"},
            "wood",
            2,
        });
        catalog.add({
            "rock",
            "Rock",
            {},
            {220, 190, 70, 255},
            {0.75f, 0.45f, 0.70f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            0.45f,
            {0.0f, 0.05f, 0.0f},
            {"procedural", "removable", "inspectable", "resource_node", "blocking", "stone"},
            "stone",
            1,
        });
        catalog.add({
            "camp_marker",
            "Camp Marker",
            {},
            {70, 120, 230, 255},
            {0.85f, 0.85f, 0.85f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            0.85f,
            {0.0f, 0.25f, 0.0f},
            {"procedural", "placeable", "inspectable", "blocking", "marker"},
            {},
            1,
        });
        catalog.add({
            "field_marker",
            "Field Marker",
            {},
            {220, 64, 64, 255},
            {0.65f, 0.65f, 0.65f},
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            0.65f,
            {0.0f, 0.15f, 0.0f},
            {"procedural", "removable", "inspectable", "blocking", "marker"},
            {},
            1,
        });
        return catalog;
    }

    ObjectArchetypeLoadResult ObjectArchetypeCatalog::loadFromYaml(const std::filesystem::path& path)
    {
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            const YAML::Node archetypes = root["object_archetypes"];
            if (!archetypes || !archetypes.IsMap()) {
                return {false, "object_archetypes must be a YAML map.", sampleDefaults()};
            }

            ObjectArchetypeCatalog catalog;
            for (const auto& archetypeNode : archetypes) {
                const std::string id = archetypeNode.first.as<std::string>();
                const YAML::Node config = archetypeNode.second;
                if (id.empty() || !config.IsMap()) {
                    continue;
                }

                ObjectArchetypeDescriptor descriptor;
                descriptor.id = id;
                descriptor.displayName = config["display_name"].as<std::string>(id);
                descriptor.meshPath = config["mesh"].as<std::string>(std::string{});
                descriptor.solidColor = readColor(config["solid_color"], descriptor.solidColor);
                descriptor.scale = readVec3(config["scale"], descriptor.scale);
                descriptor.localBounds = readBounds(config["bounds"], descriptor.localBounds);
                descriptor.terrainYOffset = config["terrain_y_offset"].as<float>(descriptor.terrainYOffset);
                descriptor.angularVelocity = readVec3(config["angular_velocity"], descriptor.angularVelocity);
                descriptor.tags = readTags(config["tags"]);
                descriptor.resourceId = config["resource_id"].as<std::string>(descriptor.id);
                descriptor.resourceAmount = config["resource_amount"].as<uint32_t>(descriptor.resourceAmount);
                catalog.add(std::move(descriptor));
            }

            if (catalog.all().empty()) {
                return {false, "object_archetypes did not contain any valid archetypes.", sampleDefaults()};
            }

            return {true, {}, std::move(catalog)};
        } catch (const std::exception& error) {
            std::ostringstream message;
            message << "Failed to load object archetypes '" << path.string() << "': " << error.what();
            return {false, message.str(), sampleDefaults()};
        }
    }

    void ObjectArchetypeCatalog::add(ObjectArchetypeDescriptor descriptor)
    {
        if (descriptor.id.empty()) {
            return;
        }

        const auto existing = std::ranges::find_if(archetypes_, [&](const ObjectArchetypeDescriptor& archetype) {
            return archetype.id == descriptor.id;
        });
        if (existing != archetypes_.end()) {
            *existing = std::move(descriptor);
        } else {
            archetypes_.push_back(std::move(descriptor));
        }
    }

    const ObjectArchetypeDescriptor* ObjectArchetypeCatalog::find(std::string_view id) const
    {
        const auto archetype = std::ranges::find_if(archetypes_, [id](const ObjectArchetypeDescriptor& descriptor) {
            return descriptor.id == id;
        });
        return archetype == archetypes_.end() ? nullptr : &*archetype;
    }

    std::vector<const ObjectArchetypeDescriptor*> ObjectArchetypeCatalog::all() const
    {
        std::vector<const ObjectArchetypeDescriptor*> result;
        result.reserve(archetypes_.size());
        for (const ObjectArchetypeDescriptor& archetype : archetypes_) {
            result.push_back(&archetype);
        }
        return result;
    }

    bool hasTag(const ObjectArchetypeDescriptor& descriptor, std::string_view tag)
    {
        return std::ranges::any_of(descriptor.tags, [tag](const std::string& value) {
            return value == tag;
        });
    }

    std::string tagsToString(const ObjectArchetypeDescriptor& descriptor)
    {
        std::string result;
        for (const std::string& tag : descriptor.tags) {
            if (!result.empty()) {
                result += ", ";
            }
            result += tag;
        }
        return result;
    }
}
