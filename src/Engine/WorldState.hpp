#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/OrbitCamera.hpp"

namespace Engine {
    struct SavedPlayerState {
        glm::vec3 position{};
    };

    struct SavedWorldSettings {
        uint32_t seed = 0;
        float chunkSize = 16.0f;
        int32_t loadRadiusChunks = 1;
        float terrainHeightScale = 1.25f;
    };

    struct SavedPersistentObject {
        std::string id;
        std::string archetypeId;
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{1.0f};
    };

    struct SavedRemovedObject {
        std::string id;
        std::string archetypeId;
        ChunkCoord chunk;
    };

    struct WorldStateSnapshot {
        uint32_t version = 1;
        SavedWorldSettings settings;
        SavedPlayerState player;
        CameraState camera;
        std::vector<SavedPersistentObject> persistentObjects;
        std::vector<SavedRemovedObject> removedObjects;
    };

    struct WorldStateIoResult {
        bool success = false;
        std::string error;
        WorldStateSnapshot snapshot;
    };

    bool saveWorldState(const std::filesystem::path& path, const WorldStateSnapshot& snapshot, std::string* error = nullptr);
    WorldStateIoResult loadWorldState(const std::filesystem::path& path);
}
