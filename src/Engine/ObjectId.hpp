#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Engine/ChunkTypes.hpp"

namespace Engine {
    class ObjectId {
    public:
        ObjectId() = default;

        bool isValid() const;
        const std::string& toString() const;

        static ObjectId fromString(std::string value);
        static ObjectId player();
        static ObjectId custom(std::string_view archetypeId, uint64_t serial);
        // Persistence key for regenerated baseline procedural props.
        // Format is intentionally readable and save-facing:
        // proc/chunk_x_z/archetype/slot. Changing this format or the inputs
        // requires a save migration.
        static ObjectId proceduralProp(ChunkCoord coord, const std::string& archetypeId, uint32_t localSlot);

        friend bool operator==(const ObjectId& lhs, const ObjectId& rhs) = default;

    private:
        explicit ObjectId(std::string value);

        std::string value_;
    };

    bool isCustomObjectId(const ObjectId& id);
    std::string archetypeIdFromObjectId(const ObjectId& id, std::string_view fallback = {});
    bool parseProceduralObjectId(const ObjectId& id, std::string& archetypeId, uint32_t& localSlot);
}
