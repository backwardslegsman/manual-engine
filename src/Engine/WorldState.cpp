#include "Engine/WorldState.hpp"

#include <fstream>

#include <yaml-cpp/yaml.h>

namespace Engine {
    namespace {
        YAML::Node vec3Node(const glm::vec3& value)
        {
            YAML::Node node;
            node.push_back(value.x);
            node.push_back(value.y);
            node.push_back(value.z);
            return node;
        }

        glm::vec3 readVec3(const YAML::Node& node, const glm::vec3& fallback = {})
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

        YAML::Node chunkNode(ChunkCoord coord)
        {
            YAML::Node node;
            node["x"] = coord.x;
            node["z"] = coord.z;
            return node;
        }

        ChunkCoord readChunk(const YAML::Node& node)
        {
            return {
                node["x"].as<int32_t>(0),
                node["z"].as<int32_t>(0),
            };
        }

        const char* cameraModeName(CameraMode mode)
        {
            switch (mode) {
                case CameraMode::Free:
                    return "free";
                case CameraMode::FollowTarget:
                    return "follow_target";
            }
            return "free";
        }

        CameraMode readCameraMode(const YAML::Node& node)
        {
            const std::string value = node.as<std::string>("free");
            if (value == "follow_target" || value == "follow" || value == "player_follow") {
                return CameraMode::FollowTarget;
            }
            return CameraMode::Free;
        }

        YAML::Node persistentObjectNode(const SavedPersistentObject& object)
        {
            YAML::Node node;
            node["id"] = object.id.toString();
            node["archetype"] = object.archetypeId;
            node["chunk"] = chunkNode(object.chunk);
            node["position"] = vec3Node(object.position);
            node["rotation"] = vec3Node(object.rotation);
            node["scale"] = vec3Node(object.scale);
            return node;
        }

        SavedPersistentObject readPersistentObject(const YAML::Node& node)
        {
            SavedPersistentObject object;
            object.id = ObjectId::fromString(node["id"].as<std::string>(std::string{}));
            object.archetypeId = node["archetype"].as<std::string>(std::string{});
            if (node["chunk"]) {
                object.chunk = readChunk(node["chunk"]);
                object.hasChunk = true;
            }
            object.position = readVec3(node["position"]);
            object.rotation = readVec3(node["rotation"]);
            object.scale = readVec3(node["scale"], {1.0f, 1.0f, 1.0f});
            return object;
        }

        YAML::Node removedObjectNode(const SavedRemovedObject& object)
        {
            YAML::Node node;
            node["id"] = object.id.toString();
            node["archetype"] = object.archetypeId;
            node["chunk"] = chunkNode(object.chunk);
            return node;
        }

        SavedRemovedObject readRemovedObject(const YAML::Node& node)
        {
            SavedRemovedObject object;
            object.id = ObjectId::fromString(node["id"].as<std::string>(std::string{}));
            object.archetypeId = node["archetype"].as<std::string>(std::string{});
            object.chunk = readChunk(node["chunk"]);
            return object;
        }
    }

    bool saveWorldState(const std::filesystem::path& path, const WorldStateSnapshot& snapshot, std::string* error)
    {
        try {
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }

            YAML::Node root;
            root["version"] = snapshot.version;
            root["settings"]["seed"] = snapshot.settings.seed;
            root["settings"]["chunk_size"] = snapshot.settings.chunkSize;
            root["settings"]["load_radius_chunks"] = snapshot.settings.loadRadiusChunks;
            root["settings"]["terrain_height_scale"] = snapshot.settings.terrainHeightScale;
            root["player"]["position"] = vec3Node(snapshot.player.position);
            root["camera"]["pivot"] = vec3Node(snapshot.camera.pivot);
            root["camera"]["yaw_radians"] = snapshot.camera.yawRadians;
            root["camera"]["pitch_radians"] = snapshot.camera.pitchRadians;
            root["camera"]["distance"] = snapshot.camera.distance;
            root["camera"]["mode"] = cameraModeName(snapshot.camera.mode);
            root["camera"]["follow_offset"] = vec3Node(snapshot.camera.followOffset);
            root["next_custom_object_serial"] = snapshot.nextCustomObjectSerial;

            YAML::Node persistentObjects;
            for (const SavedPersistentObject& object : snapshot.persistentObjects) {
                persistentObjects.push_back(persistentObjectNode(object));
            }
            root["persistent_objects"] = persistentObjects;

            YAML::Node removedObjects;
            for (const SavedRemovedObject& object : snapshot.removedObjects) {
                removedObjects.push_back(removedObjectNode(object));
            }
            root["removed_objects"] = removedObjects;

            std::ofstream file(path);
            if (!file) {
                if (error) {
                    *error = "Unable to open save file for writing: " + path.string();
                }
                return false;
            }
            file << root;
            return true;
        } catch (const std::exception& exception) {
            if (error) {
                *error = exception.what();
            }
            return false;
        }
    }

    WorldStateIoResult loadWorldState(const std::filesystem::path& path)
    {
        WorldStateIoResult result;
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            result.snapshot.version = root["version"].as<uint32_t>(1);
            result.snapshot.settings.seed = root["settings"]["seed"].as<uint32_t>(0);
            result.snapshot.settings.chunkSize = root["settings"]["chunk_size"].as<float>(16.0f);
            result.snapshot.settings.loadRadiusChunks = root["settings"]["load_radius_chunks"].as<int32_t>(1);
            result.snapshot.settings.terrainHeightScale = root["settings"]["terrain_height_scale"].as<float>(1.25f);
            result.snapshot.player.position = readVec3(root["player"]["position"]);
            result.snapshot.camera.pivot = readVec3(root["camera"]["pivot"], result.snapshot.player.position);
            result.snapshot.camera.yawRadians = root["camera"]["yaw_radians"].as<float>(0.0f);
            result.snapshot.camera.pitchRadians = root["camera"]["pitch_radians"].as<float>(glm::radians(-40.0f));
            result.snapshot.camera.distance = root["camera"]["distance"].as<float>(14.0f);
            result.snapshot.camera.mode = readCameraMode(root["camera"]["mode"]);
            result.snapshot.camera.followOffset = readVec3(root["camera"]["follow_offset"]);
            result.snapshot.nextCustomObjectSerial = root["next_custom_object_serial"].as<uint64_t>(1);

            if (const YAML::Node objects = root["persistent_objects"]; objects && objects.IsSequence()) {
                for (const YAML::Node& object : objects) {
                    result.snapshot.persistentObjects.push_back(readPersistentObject(object));
                }
            }

            if (const YAML::Node objects = root["removed_objects"]; objects && objects.IsSequence()) {
                for (const YAML::Node& object : objects) {
                    result.snapshot.removedObjects.push_back(readRemovedObject(object));
                }
            }

            result.success = true;
        } catch (const std::exception& exception) {
            result.success = false;
            result.error = exception.what();
        }
        return result;
    }
}
