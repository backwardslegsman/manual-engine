#include "Engine/ObjectId.hpp"

#include <charconv>
#include <sstream>
#include <system_error>
#include <utility>

namespace Engine {
    ObjectId::ObjectId(std::string value)
        : value_(std::move(value))
    {
    }

    bool ObjectId::isValid() const
    {
        return !value_.empty();
    }

    const std::string& ObjectId::toString() const
    {
        return value_;
    }

    ObjectId ObjectId::fromString(std::string value)
    {
        return ObjectId{std::move(value)};
    }

    ObjectId ObjectId::player()
    {
        return ObjectId{"player"};
    }

    ObjectId ObjectId::custom(std::string_view archetypeId, uint64_t serial)
    {
        std::ostringstream id;
        id << "custom/" << archetypeId << "/" << serial;
        return ObjectId{id.str()};
    }

    ObjectId ObjectId::proceduralProp(ChunkCoord coord, const std::string& archetypeId, uint32_t localSlot)
    {
        std::ostringstream id;
        id << "proc/chunk_" << coord.x << "_" << coord.z << "/" << archetypeId << "/" << localSlot;
        return ObjectId{id.str()};
    }

    bool isCustomObjectId(const ObjectId& id)
    {
        return std::string_view{id.toString()}.starts_with("custom/");
    }

    std::string archetypeIdFromObjectId(const ObjectId& id, std::string_view fallback)
    {
        const std::string& value = id.toString();
        if (std::string_view{value}.starts_with("proc/")) {
            const size_t firstSlash = value.find('/');
            const size_t secondSlash = value.find('/', firstSlash == std::string::npos ? firstSlash : firstSlash + 1);
            const size_t thirdSlash = value.find('/', secondSlash == std::string::npos ? secondSlash : secondSlash + 1);
            if (secondSlash != std::string::npos && thirdSlash != std::string::npos && thirdSlash > secondSlash + 1) {
                return value.substr(secondSlash + 1, thirdSlash - secondSlash - 1);
            }
        }

        if (std::string_view{value}.starts_with("custom/")) {
            const size_t firstSlash = value.find('/');
            const size_t secondSlash = value.find('/', firstSlash == std::string::npos ? firstSlash : firstSlash + 1);
            if (firstSlash != std::string::npos && secondSlash != std::string::npos && secondSlash > firstSlash + 1) {
                return value.substr(firstSlash + 1, secondSlash - firstSlash - 1);
            }
        }

        return std::string{fallback};
    }

    bool parseProceduralObjectId(const ObjectId& id, std::string& archetypeId, uint32_t& localSlot)
    {
        const std::string& value = id.toString();
        if (!std::string_view{value}.starts_with("proc/")) {
            return false;
        }

        const size_t firstSlash = value.find('/');
        const size_t secondSlash = value.find('/', firstSlash == std::string::npos ? firstSlash : firstSlash + 1);
        const size_t thirdSlash = value.find('/', secondSlash == std::string::npos ? secondSlash : secondSlash + 1);
        if (secondSlash == std::string::npos || thirdSlash == std::string::npos || thirdSlash <= secondSlash + 1) {
            return false;
        }

        archetypeId = value.substr(secondSlash + 1, thirdSlash - secondSlash - 1);
        const std::string_view slotText{value.data() + thirdSlash + 1, value.size() - thirdSlash - 1};
        uint32_t parsedSlot = 0;
        const std::from_chars_result result = std::from_chars(slotText.data(), slotText.data() + slotText.size(), parsedSlot);
        if (result.ec != std::errc{} || result.ptr != slotText.data() + slotText.size()) {
            archetypeId.clear();
            return false;
        }

        localSlot = parsedSlot;
        return true;
    }
}
