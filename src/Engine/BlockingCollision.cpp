#include "Engine/BlockingCollision.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "Engine/SpatialRegistry.hpp"

namespace Engine {
    namespace {
        struct BlockingCandidate {
            WorldObjectHandle object;
            ObjectId objectId;
            Renderer::Aabb bounds;
        };

        bool isFinite(float value)
        {
            return std::isfinite(value);
        }

        bool validRequest(const BlockingCollisionRequest& request)
        {
            return request.world &&
                request.spatialRegistry &&
                request.world->isValid(request.actor) &&
                request.radius > 0.0f &&
                request.height > 0.0f &&
                isFinite(request.currentPosition.x) &&
                isFinite(request.currentPosition.y) &&
                isFinite(request.currentPosition.z) &&
                isFinite(request.desiredPosition.x) &&
                isFinite(request.desiredPosition.y) &&
                isFinite(request.desiredPosition.z);
        }

        bool verticalOverlap(const glm::vec3& actorPosition, float actorHeight, const Renderer::Aabb& bounds, float skinWidth)
        {
            const float halfHeight = actorHeight * 0.5f;
            const float actorMinY = actorPosition.y - halfHeight;
            const float actorMaxY = actorPosition.y + halfHeight;
            return actorMaxY >= bounds.min.y - skinWidth && actorMinY <= bounds.max.y + skinWidth;
        }

        bool overlapsExpandedBounds(
            const glm::vec3& actorPosition,
            float actorRadius,
            float actorHeight,
            float skinWidth,
            const Renderer::Aabb& bounds)
        {
            if (!verticalOverlap(actorPosition, actorHeight, bounds, skinWidth)) {
                return false;
            }

            const float expandedRadius = actorRadius + skinWidth;
            return actorPosition.x >= bounds.min.x - expandedRadius &&
                actorPosition.x <= bounds.max.x + expandedRadius &&
                actorPosition.z >= bounds.min.z - expandedRadius &&
                actorPosition.z <= bounds.max.z + expandedRadius;
        }

        std::vector<BlockingCandidate> collectCandidates(const BlockingCollisionRequest& request)
        {
            const float padding = std::max(0.0f, request.candidateQueryPadding + request.radius);
            const glm::vec3 queryMin{
                std::min(request.currentPosition.x, request.desiredPosition.x) - padding,
                std::min(request.currentPosition.y, request.desiredPosition.y) - request.height,
                std::min(request.currentPosition.z, request.desiredPosition.z) - padding,
            };
            const glm::vec3 queryMax{
                std::max(request.currentPosition.x, request.desiredPosition.x) + padding,
                std::max(request.currentPosition.y, request.desiredPosition.y) + request.height,
                std::max(request.currentPosition.z, request.desiredPosition.z) + padding,
            };

            std::vector<BlockingCandidate> candidates;
            for (const SpatialQueryResult& result : request.spatialRegistry->objectsInAabb(queryMin, queryMax)) {
                if (result.object.id == request.actor.id ||
                    !request.world->isValid(result.object) ||
                    !request.world->collisionEnabled(result.object)) {
                    continue;
                }

                const std::optional<Renderer::Aabb> bounds = request.world->worldBounds(result.object);
                if (!bounds) {
                    continue;
                }

                candidates.push_back({result.object, result.objectId, *bounds});
            }
            return candidates;
        }

        const BlockingCandidate* firstBlockingCandidate(
            const glm::vec3& actorPosition,
            float actorRadius,
            float actorHeight,
            float skinWidth,
            const std::vector<BlockingCandidate>& candidates)
        {
            for (const BlockingCandidate& candidate : candidates) {
                if (overlapsExpandedBounds(actorPosition, actorRadius, actorHeight, skinWidth, candidate.bounds)) {
                    return &candidate;
                }
            }
            return nullptr;
        }

        void recordHit(BlockingCollisionResult& result, const BlockingCandidate& candidate)
        {
            ++result.hitCount;
            if (result.firstBlockingObject.id == UINT32_MAX) {
                result.firstBlockingObject = candidate.object;
                result.firstBlockingObjectId = candidate.objectId;
            }
        }
    }

    BlockingCollisionResult BlockingCollisionSystem::resolveActorMovement(const BlockingCollisionRequest& request) const
    {
        BlockingCollisionResult result;
        result.resolvedPosition = request.desiredPosition;

        if (!validRequest(request)) {
            return result;
        }

        const std::vector<BlockingCandidate> candidates = collectCandidates(request);
        const float skinWidth = std::max(request.skinWidth, 0.0f);
        glm::vec3 resolved = request.currentPosition;

        glm::vec3 xStep = resolved;
        xStep.x = request.desiredPosition.x;
        if (const BlockingCandidate* hit = firstBlockingCandidate(xStep, request.radius, request.height, skinWidth, candidates)) {
            result.blockedX = true;
            recordHit(result, *hit);
        } else {
            resolved.x = xStep.x;
        }

        glm::vec3 zStep = resolved;
        zStep.z = request.desiredPosition.z;
        if (const BlockingCandidate* hit = firstBlockingCandidate(zStep, request.radius, request.height, skinWidth, candidates)) {
            result.blockedZ = true;
            recordHit(result, *hit);
        } else {
            resolved.z = zStep.z;
        }

        resolved.y = request.desiredPosition.y;
        result.resolvedPosition = resolved;
        return result;
    }
}
