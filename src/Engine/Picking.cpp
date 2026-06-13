#include "Engine/Picking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>

namespace Engine {
    Ray screenPointToRay(
        const glm::vec2& screenPosition,
        const glm::ivec2& viewportSize,
        const glm::mat4& view,
        const glm::mat4& projection)
    {
        const float width = static_cast<float>(std::max(viewportSize.x, 1));
        const float height = static_cast<float>(std::max(viewportSize.y, 1));
        const float x = (screenPosition.x / width) * 2.0f - 1.0f;
        const float y = 1.0f - (screenPosition.y / height) * 2.0f;

        const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
        glm::vec4 nearPoint = inverseViewProjection * glm::vec4{x, y, -1.0f, 1.0f};
        glm::vec4 farPoint = inverseViewProjection * glm::vec4{x, y, 1.0f, 1.0f};
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        Ray ray;
        ray.origin = glm::vec3{nearPoint};
        ray.direction = glm::normalize(glm::vec3{farPoint - nearPoint});
        return ray;
    }

    std::optional<float> intersectRayAabb(const Ray& ray, const Renderer::Aabb& bounds, float maxDistance)
    {
        float tMin = 0.0f;
        float tMax = maxDistance;

        for (uint32_t axis = 0; axis < 3; ++axis) {
            const float origin = ray.origin[axis];
            const float direction = ray.direction[axis];
            const float minValue = bounds.min[axis];
            const float maxValue = bounds.max[axis];

            if (std::abs(direction) < 0.00001f) {
                if (origin < minValue || origin > maxValue) {
                    return std::nullopt;
                }
                continue;
            }

            float t1 = (minValue - origin) / direction;
            float t2 = (maxValue - origin) / direction;
            if (t1 > t2) {
                std::swap(t1, t2);
            }

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                return std::nullopt;
            }
        }

        return tMin <= maxDistance ? std::optional<float>{tMin} : std::nullopt;
    }

    std::optional<ObjectPickHit> pickNearestObject(
        const Ray& ray,
        const SpatialRegistry& spatialRegistry,
        const World& world,
        float maxDistance,
        float queryMargin)
    {
        if (maxDistance <= 0.0f) {
            return std::nullopt;
        }

        const glm::vec3 segmentEnd = ray.origin + ray.direction * maxDistance;
        const glm::vec3 margin{std::max(queryMargin, 0.0f)};
        const glm::vec3 queryMin = glm::min(ray.origin, segmentEnd) - margin;
        const glm::vec3 queryMax = glm::max(ray.origin, segmentEnd) + margin;

        std::optional<ObjectPickHit> nearestHit;
        float nearestDistance = std::numeric_limits<float>::max();
        for (const SpatialQueryResult& candidate : spatialRegistry.objectsInAabb(queryMin, queryMax)) {
            const std::optional<Renderer::Aabb> bounds = world.worldBounds(candidate.object);
            if (!bounds) {
                continue;
            }

            const std::optional<float> hitDistance = intersectRayAabb(ray, *bounds, maxDistance);
            if (!hitDistance || *hitDistance >= nearestDistance) {
                continue;
            }

            nearestDistance = *hitDistance;
            nearestHit = ObjectPickHit{
                candidate.object,
                world.objectId(candidate.object).value_or(ObjectId{}),
                ray.origin + ray.direction * *hitDistance,
                *hitDistance,
                candidate.cell,
            };
        }
        return nearestHit;
    }
}
