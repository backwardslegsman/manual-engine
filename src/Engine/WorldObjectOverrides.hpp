#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Engine/ObjectId.hpp"
#include "Engine/WorldState.hpp"

namespace Engine {
    // Runtime view of save-file object overrides. Baseline terrain and
    // procedural content regenerate; this state only filters or patches objects
    // by stable ObjectId.
    class WorldObjectOverrides {
    public:
        void replaceFromSnapshot(const WorldStateSnapshot& snapshot, float chunkSize);
        void writeToSnapshot(WorldStateSnapshot& snapshot) const;
        void clear();

        bool isRemoved(ObjectId id) const;
        void markRemoved(ObjectId id, std::string_view archetypeId, ChunkCoord chunk);
        void clearRemoved(ObjectId id);

        void upsertPersistent(SavedPersistentObject object);
        void removePersistent(ObjectId id);
        std::optional<SavedPersistentObject> persistentObject(ObjectId id) const;
        std::vector<SavedPersistentObject> persistentObjectsForChunk(ChunkCoord chunk) const;
        bool hasOverridesForChunk(ChunkCoord chunk) const;

        ObjectId allocateCustomObjectId(std::string_view archetypeId);
        uint64_t nextCustomObjectSerial() const;

    private:
        static ChunkCoord chunkForPosition(const glm::vec3& position, float chunkSize);

        std::unordered_map<std::string, SavedPersistentObject> persistentObjects_;
        std::unordered_map<std::string, SavedRemovedObject> removedObjects_;
        uint64_t nextCustomObjectSerial_ = 1;
    };
}
