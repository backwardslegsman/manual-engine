#include "Engine/WorldObjectOverrides.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {
    void WorldObjectOverrides::replaceFromSnapshot(const WorldStateSnapshot& snapshot, float chunkSize)
    {
        clear();
        nextCustomObjectSerial_ = std::max<uint64_t>(snapshot.nextCustomObjectSerial, 1);

        for (const SavedRemovedObject& removed : snapshot.removedObjects) {
            if (removed.id.isValid()) {
                removedObjects_[removed.id.toString()] = removed;
            }
        }

        for (SavedPersistentObject object : snapshot.persistentObjects) {
            if (!object.id.isValid()) {
                continue;
            }

            if (!object.hasChunk) {
                object.chunk = chunkForPosition(object.position, chunkSize);
                object.hasChunk = true;
            }
            persistentObjects_[object.id.toString()] = object;
        }
    }

    void WorldObjectOverrides::writeToSnapshot(WorldStateSnapshot& snapshot) const
    {
        snapshot.persistentObjects.clear();
        snapshot.persistentObjects.reserve(persistentObjects_.size());
        for (const auto& [_, object] : persistentObjects_) {
            snapshot.persistentObjects.push_back(object);
        }
        std::ranges::sort(snapshot.persistentObjects, {}, [](const SavedPersistentObject& object) {
            return object.id.toString();
        });

        snapshot.removedObjects.clear();
        snapshot.removedObjects.reserve(removedObjects_.size());
        for (const auto& [_, object] : removedObjects_) {
            snapshot.removedObjects.push_back(object);
        }
        std::ranges::sort(snapshot.removedObjects, {}, [](const SavedRemovedObject& object) {
            return object.id.toString();
        });

        snapshot.nextCustomObjectSerial = nextCustomObjectSerial_;
    }

    void WorldObjectOverrides::clear()
    {
        persistentObjects_.clear();
        removedObjects_.clear();
        nextCustomObjectSerial_ = 1;
    }

    bool WorldObjectOverrides::isRemoved(ObjectId id) const
    {
        return id.isValid() && removedObjects_.contains(id.toString());
    }

    void WorldObjectOverrides::markRemoved(ObjectId id, std::string_view archetypeId, ChunkCoord chunk)
    {
        if (!id.isValid()) {
            return;
        }

        removedObjects_[id.toString()] = {
            id,
            std::string{archetypeId},
            chunk,
        };
        persistentObjects_.erase(id.toString());
    }

    void WorldObjectOverrides::clearRemoved(ObjectId id)
    {
        if (id.isValid()) {
            removedObjects_.erase(id.toString());
        }
    }

    void WorldObjectOverrides::upsertPersistent(SavedPersistentObject object)
    {
        if (!object.id.isValid()) {
            return;
        }

        object.hasChunk = true;
        removedObjects_.erase(object.id.toString());
        persistentObjects_[object.id.toString()] = std::move(object);
    }

    void WorldObjectOverrides::removePersistent(ObjectId id)
    {
        if (id.isValid()) {
            persistentObjects_.erase(id.toString());
        }
    }

    std::optional<SavedPersistentObject> WorldObjectOverrides::persistentObject(ObjectId id) const
    {
        if (!id.isValid()) {
            return std::nullopt;
        }

        const auto objectIt = persistentObjects_.find(id.toString());
        if (objectIt == persistentObjects_.end()) {
            return std::nullopt;
        }
        return objectIt->second;
    }

    std::vector<SavedPersistentObject> WorldObjectOverrides::persistentObjectsForChunk(ChunkCoord chunk) const
    {
        std::vector<SavedPersistentObject> objects;
        for (const auto& [_, object] : persistentObjects_) {
            if (object.chunk == chunk) {
                objects.push_back(object);
            }
        }
        std::ranges::sort(objects, {}, [](const SavedPersistentObject& object) {
            return object.id.toString();
        });
        return objects;
    }

    bool WorldObjectOverrides::hasOverridesForChunk(ChunkCoord chunk) const
    {
        const auto persistentIt = std::ranges::find_if(persistentObjects_, [&](const auto& entry) {
            return entry.second.chunk == chunk;
        });
        if (persistentIt != persistentObjects_.end()) {
            return true;
        }

        return std::ranges::any_of(removedObjects_, [&](const auto& entry) {
            return entry.second.chunk == chunk;
        });
    }

    ObjectId WorldObjectOverrides::allocateCustomObjectId(std::string_view archetypeId)
    {
        return ObjectId::custom(archetypeId, nextCustomObjectSerial_++);
    }

    uint64_t WorldObjectOverrides::nextCustomObjectSerial() const
    {
        return nextCustomObjectSerial_;
    }

    ChunkCoord WorldObjectOverrides::chunkForPosition(const glm::vec3& position, float chunkSize)
    {
        const float safeChunkSize = std::max(chunkSize, 1.0f);
        return {
            static_cast<int32_t>(std::floor(position.x / safeChunkSize)),
            static_cast<int32_t>(std::floor(position.z / safeChunkSize)),
        };
    }
}
