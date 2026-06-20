#include "Engine/NavigationRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <limits>
#include <unordered_map>
#include <utility>

#include "Engine/NavigationConnectivity.hpp"

namespace Engine {
    namespace {
        constexpr size_t RuntimeStatusCount = 10;
        constexpr float DefaultRaycastDeviationPadding = 0.25f;

        uint64_t elapsedMicroseconds(std::chrono::steady_clock::time_point start)
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count());
        }

        bool finiteVec3(glm::vec3 point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        float xzDistance(glm::vec3 a, glm::vec3 b)
        {
            const glm::vec2 delta{a.x - b.x, a.z - b.z};
            return std::sqrt(glm::dot(delta, delta));
        }

        float xzDistanceToSegment(glm::vec3 point, glm::vec3 a, glm::vec3 b)
        {
            const glm::vec2 p{point.x, point.z};
            const glm::vec2 start{a.x, a.z};
            const glm::vec2 end{b.x, b.z};
            const glm::vec2 segment = end - start;
            const float lengthSquared = glm::dot(segment, segment);
            if (lengthSquared <= 0.000001f) {
                return glm::length(p - start);
            }
            const float t = std::clamp(glm::dot(p - start, segment) / lengthSquared, 0.0f, 1.0f);
            return glm::length(p - (start + segment * t));
        }

        bool completeEnough(const NavPath& path, const NavigationQueryFilter& filter)
        {
            return path.complete || (filter.allowPartialPath && !path.points.empty());
        }

        bool containsXZ(const Renderer::Aabb& bounds, glm::vec3 point)
        {
            constexpr float Epsilon = 0.05f;
            return point.x >= bounds.min.x - Epsilon &&
                point.x <= bounds.max.x + Epsilon &&
                point.z >= bounds.min.z - Epsilon &&
                point.z <= bounds.max.z + Epsilon;
        }

        void appendPathPoints(std::vector<glm::vec3>& out, const std::vector<glm::vec3>& points)
        {
            for (const glm::vec3& point : points) {
                if (!out.empty() && glm::length(out.back() - point) <= 0.001f) {
                    continue;
                }
                out.push_back(point);
            }
        }

        std::optional<ChunkCoord> findPointTile(
            const NavigationSystem& navigation,
            glm::vec3 point,
            const NavAgentSettings& agent)
        {
            std::vector<NavigationTileDiagnostics> tiles = navigation.allTileDiagnostics();
            std::ranges::sort(tiles, [](const NavigationTileDiagnostics& lhs, const NavigationTileDiagnostics& rhs) {
                return lhs.coord.x == rhs.coord.x ? lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
            });
            for (const NavigationTileDiagnostics& tile : tiles) {
                if (!containsXZ(tile.bounds, point)) {
                    continue;
                }
                const NavQueryResult projected = navigation.nearestNavigablePointInTile(tile.coord, point, agent);
                if (projected.status == NavQueryStatus::Success) {
                    return tile.coord;
                }
            }
            return std::nullopt;
        }

        struct PortalRouteEdge {
            ChunkCoord from;
            ChunkCoord to;
            glm::vec3 waypoint{};
            glm::vec3 ingress{};
            float cost = 0.0f;
        };

        std::vector<PortalRouteEdge> sortedPortalEdges(const NavigationConnectivitySystem& connectivity)
        {
            std::vector<PortalRouteEdge> edges;
            for (const auto& [_, chunk] : connectivity.all()) {
                for (const std::vector<ChunkNavPortal>& portals : chunk.portalsByEdge) {
                    for (const ChunkNavPortal& portal : portals) {
                        if (!portal.connectedToLoadedNeighbor) {
                            continue;
                        }
                        edges.push_back({
                            chunk.coord,
                            portal.neighborCoord,
                            portal.position,
                            portal.connectedNeighborPosition,
                            std::max(0.01f, xzDistance(portal.position, portal.connectedNeighborPosition)),
                        });
                    }
                }
            }
            std::ranges::sort(edges, [](const PortalRouteEdge& lhs, const PortalRouteEdge& rhs) {
                if (lhs.from.x != rhs.from.x) {
                    return lhs.from.x < rhs.from.x;
                }
                if (lhs.from.z != rhs.from.z) {
                    return lhs.from.z < rhs.from.z;
                }
                if (lhs.to.x != rhs.to.x) {
                    return lhs.to.x < rhs.to.x;
                }
                if (lhs.to.z != rhs.to.z) {
                    return lhs.to.z < rhs.to.z;
                }
                if (lhs.waypoint.x != rhs.waypoint.x) {
                    return lhs.waypoint.x < rhs.waypoint.x;
                }
                return lhs.waypoint.z < rhs.waypoint.z;
            });
            return edges;
        }
    }

    NavAgentSettings toNavAgentSettings(const NavigationAgentConfig& config)
    {
        return {
            config.radius,
            config.height,
            config.maxSlopeDegrees,
            config.maxClimb,
        };
    }

    NavigationRuntimeStatus toNavigationRuntimeStatus(NavQueryStatus status, bool complete)
    {
        switch (status) {
            case NavQueryStatus::Success:
                return complete ? NavigationRuntimeStatus::Success : NavigationRuntimeStatus::PartialPath;
            case NavQueryStatus::NotInitialized:
                return NavigationRuntimeStatus::NotInitialized;
            case NavQueryStatus::NoTile:
                return NavigationRuntimeStatus::NoTile;
            case NavQueryStatus::NoNearestPoly:
                return NavigationRuntimeStatus::NoNearestPoly;
            case NavQueryStatus::NoPath:
                return NavigationRuntimeStatus::NoPath;
            case NavQueryStatus::InvalidInput:
                return NavigationRuntimeStatus::InvalidInput;
            case NavQueryStatus::Unsupported:
                return NavigationRuntimeStatus::Unsupported;
        }
        return NavigationRuntimeStatus::Unsupported;
    }

    float navigationPathLength(const NavPath& path)
    {
        float length = 0.0f;
        for (size_t index = 1; index < path.points.size(); ++index) {
            length += glm::length(path.points[index] - path.points[index - 1]);
        }
        return length;
    }

    const char* navigationRuntimeStatusName(NavigationRuntimeStatus status)
    {
        switch (status) {
            case NavigationRuntimeStatus::Success:
                return "success";
            case NavigationRuntimeStatus::NotInitialized:
                return "not initialized";
            case NavigationRuntimeStatus::NoTile:
                return "no tile";
            case NavigationRuntimeStatus::NoNearestPoly:
                return "no nearest polygon";
            case NavigationRuntimeStatus::NoPath:
                return "no path";
            case NavigationRuntimeStatus::PartialPath:
                return "partial path";
            case NavigationRuntimeStatus::Blocked:
                return "blocked";
            case NavigationRuntimeStatus::InvalidInput:
                return "invalid input";
            case NavigationRuntimeStatus::InvalidActor:
                return "invalid actor";
            case NavigationRuntimeStatus::Unsupported:
                return "unsupported";
        }
        return "unknown";
    }

    SceneNavigationService::SceneNavigationService(
        const NavigationSystem& navigation,
        const NavigationConnectivitySystem* connectivity,
        const WorldNavigationGraph* graph)
        : navigation_(navigation)
        , connectivity_(connectivity)
        , graph_(graph)
    {
    }

    NavigationProjectionResult SceneNavigationService::projectPoint(
        glm::vec3 point,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto start = std::chrono::steady_clock::now();
        NavigationProjectionResult result;
        result.requestedPoint = point;
        if (!validPoint(point) || !validAgent(agent)) {
            result.status = NavigationRuntimeStatus::InvalidInput;
            result.point = point;
            result.message = "Invalid navigation projection input.";
        } else {
            const NavQueryResult query = navigation_.nearestNavigablePoint(point, toNavAgentSettings(agent));
            result.status = toNavigationRuntimeStatus(query.status);
            result.point = query.point;
            result.message = query.message;
        }
        record(NavigationRuntimeQueryType::Projection, result.status, result.message, elapsedMicroseconds(start));
        appendDebugRequest(filter, result.status == NavigationRuntimeStatus::Success
                ? NavigationDebugRequestType::Projection
                : NavigationDebugRequestType::FailedQuery,
            result.status,
            {point, result.point},
            result.message);
        return result;
    }

    NavigationProjectionResult SceneNavigationService::projectPointInTile(
        ChunkCoord coord,
        glm::vec3 point,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto start = std::chrono::steady_clock::now();
        NavigationProjectionResult result;
        result.requestedPoint = point;
        if (!validPoint(point) || !validAgent(agent)) {
            result.status = NavigationRuntimeStatus::InvalidInput;
            result.point = point;
            result.message = "Invalid tile navigation projection input.";
        } else {
            const NavQueryResult query = navigation_.nearestNavigablePointInTile(coord, point, toNavAgentSettings(agent));
            result.status = toNavigationRuntimeStatus(query.status);
            result.point = query.point;
            result.message = query.message;
        }
        record(NavigationRuntimeQueryType::Projection, result.status, result.message, elapsedMicroseconds(start));
        appendDebugRequest(filter, result.status == NavigationRuntimeStatus::Success
                ? NavigationDebugRequestType::Projection
                : NavigationDebugRequestType::FailedQuery,
            result.status,
            {point, result.point},
            result.message);
        return result;
    }

    NavigationPathResult SceneNavigationService::findPath(
        glm::vec3 startPoint,
        glm::vec3 endPoint,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto start = std::chrono::steady_clock::now();
        NavigationPathResult result;
        result.requestedStart = startPoint;
        result.requestedEnd = endPoint;
        if (!validPoint(startPoint) || !validPoint(endPoint) || !validAgent(agent)) {
            result.status = NavigationRuntimeStatus::InvalidInput;
            result.message = "Invalid navigation path input.";
        } else {
            const NavQueryResult query = navigation_.findPath(startPoint, endPoint, toNavAgentSettings(agent));
            result.path = query.path;
            if (query.status == NavQueryStatus::Success && !completeEnough(query.path, filter)) {
                result.status = NavigationRuntimeStatus::PartialPath;
                result.message = query.message.empty() ? "Navigation returned a partial path." : query.message;
            } else {
                result.status = toNavigationRuntimeStatus(query.status, query.path.complete);
                result.message = query.message;
            }
            result.length = navigationPathLength(result.path);
        }
        record(
            NavigationRuntimeQueryType::Path,
            result.status,
            result.message,
            elapsedMicroseconds(start),
            static_cast<uint32_t>(result.path.points.size()),
            result.length);
        appendDebugRequest(filter, result.status == NavigationRuntimeStatus::Success
                ? NavigationDebugRequestType::Path
                : NavigationDebugRequestType::FailedQuery,
            result.status,
            result.path.points.empty() ? std::vector<glm::vec3>{startPoint, endPoint} : result.path.points,
            result.message);
        return result;
    }

    NavigationPathResult SceneNavigationService::findPathAcrossLoadedTiles(
        glm::vec3 startPoint,
        glm::vec3 endPoint,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto started = std::chrono::steady_clock::now();
        NavigationPathResult direct = findPath(startPoint, endPoint, agent, filter);
        if (direct.status == NavigationRuntimeStatus::Success && direct.path.complete) {
            return direct;
        }

        NavigationPathResult result;
        result.requestedStart = startPoint;
        result.requestedEnd = endPoint;
        if (!validPoint(startPoint) || !validPoint(endPoint) || !validAgent(agent)) {
            result.status = NavigationRuntimeStatus::InvalidInput;
            result.message = "Invalid cross-tile navigation path input.";
        } else if (!connectivity_) {
            result.status = direct.status;
            result.path = direct.path;
            result.message = "No navigation connectivity is available for cross-tile routing.";
        } else {
            const NavAgentSettings navAgent = toNavAgentSettings(agent);
            const std::optional<ChunkCoord> startCoord = findPointTile(navigation_, startPoint, navAgent);
            const std::optional<ChunkCoord> goalCoord = findPointTile(navigation_, endPoint, navAgent);
            if (!startCoord || !goalCoord) {
                result.status = NavigationRuntimeStatus::NoTile;
                result.message = "Cross-tile route endpoint is outside loaded navigation tiles.";
            } else if (*startCoord == *goalCoord) {
                result.status = direct.status;
                result.path = direct.path;
                result.message = direct.message.empty()
                    ? "Direct same-tile route failed and no cross-tile detour was attempted."
                    : direct.message;
            } else {
                const std::vector<PortalRouteEdge> edges = sortedPortalEdges(*connectivity_);
                struct QueueNode {
                    ChunkCoord coord;
                    float cost = 0.0f;
                    bool operator>(const QueueNode& rhs) const { return cost > rhs.cost; }
                };
                std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> queue;
                std::unordered_map<ChunkCoord, float, ChunkCoordHash> costs;
                std::unordered_map<ChunkCoord, size_t, ChunkCoordHash> cameFromEdge;
                std::unordered_map<ChunkCoord, ChunkCoord, ChunkCoordHash> cameFrom;
                queue.push({*startCoord, 0.0f});
                costs[*startCoord] = 0.0f;

                while (!queue.empty()) {
                    const ChunkCoord current = queue.top().coord;
                    queue.pop();
                    if (current == *goalCoord) {
                        break;
                    }
                    for (size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
                        const PortalRouteEdge& edge = edges[edgeIndex];
                        if (edge.from != current) {
                            continue;
                        }
                        const float baseCost = costs[current];
                        const float extra = edge.cost +
                            (current == *startCoord ? xzDistance(startPoint, edge.waypoint) : 0.0f) +
                            (edge.to == *goalCoord ? xzDistance(edge.ingress, endPoint) : 0.0f);
                        const float nextCost = baseCost + extra;
                        const auto existing = costs.find(edge.to);
                        if (existing != costs.end() && nextCost >= existing->second) {
                            continue;
                        }
                        costs[edge.to] = nextCost;
                        cameFrom[edge.to] = current;
                        cameFromEdge[edge.to] = edgeIndex;
                        queue.push({edge.to, nextCost});
                    }
                }

                if (!costs.contains(*goalCoord)) {
                    result.status = NavigationRuntimeStatus::NoPath;
                    result.message = "No connected portal route crosses the loaded navigation tiles.";
                } else {
                    std::vector<size_t> edgeIndices;
                    for (ChunkCoord current = *goalCoord; current != *startCoord; current = cameFrom[current]) {
                        const auto edgeIt = cameFromEdge.find(current);
                        if (edgeIt == cameFromEdge.end()) {
                            break;
                        }
                        edgeIndices.push_back(edgeIt->second);
                    }
                    std::ranges::reverse(edgeIndices);

                    glm::vec3 segmentStart = startPoint;
                    bool failedSegment = false;
                    for (size_t edgeIndex : edgeIndices) {
                        const PortalRouteEdge& edge = edges[edgeIndex];
                        const NavQueryResult segment = navigation_.findPath(segmentStart, edge.waypoint, navAgent);
                        if (segment.status != NavQueryStatus::Success || segment.path.points.empty()) {
                            failedSegment = true;
                            result.status = toNavigationRuntimeStatus(segment.status, segment.path.complete);
                            result.message = "Failed to stitch local path to a navigation portal.";
                            break;
                        }
                        appendPathPoints(result.path.points, segment.path.points);
                        if (result.path.points.empty() || glm::length(result.path.points.back() - edge.ingress) > 0.001f) {
                            result.path.points.push_back(edge.ingress);
                        }
                        segmentStart = edge.ingress;
                    }

                    if (!failedSegment) {
                        const NavQueryResult finalSegment = navigation_.findPath(segmentStart, endPoint, navAgent);
                        if (finalSegment.status != NavQueryStatus::Success || finalSegment.path.points.empty()) {
                            result.status = toNavigationRuntimeStatus(finalSegment.status, finalSegment.path.complete);
                            result.message = "Failed to stitch final local path segment after portal route.";
                        } else {
                            appendPathPoints(result.path.points, finalSegment.path.points);
                            result.path.complete = finalSegment.path.complete;
                            result.status = toNavigationRuntimeStatus(finalSegment.status, result.path.complete);
                            result.message = result.status == NavigationRuntimeStatus::Success
                                ? "Cross-tile portal route found."
                                : finalSegment.message;
                        }
                    }
                }
            }
        }

        result.length = navigationPathLength(result.path);
        record(
            NavigationRuntimeQueryType::Path,
            result.status,
            result.message,
            elapsedMicroseconds(started),
            static_cast<uint32_t>(result.path.points.size()),
            result.length);
        appendDebugRequest(filter, result.status == NavigationRuntimeStatus::Success
                ? NavigationDebugRequestType::Path
                : NavigationDebugRequestType::FailedQuery,
            result.status,
            result.path.points.empty() ? std::vector<glm::vec3>{startPoint, endPoint} : result.path.points,
            result.message);
        return result;
    }

    NavigationPathResult SceneNavigationService::findPathFromActor(
        Scene& scene,
        SceneActorHandle actor,
        glm::vec3 endPoint,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto start = std::chrono::steady_clock::now();
        if (!scene.contains(actor)) {
            NavigationPathResult result;
            result.requestedEnd = endPoint;
            result.status = NavigationRuntimeStatus::InvalidActor;
            result.message = "Invalid scene actor navigation path request.";
            record(NavigationRuntimeQueryType::Path, result.status, result.message, elapsedMicroseconds(start));
            appendDebugRequest(filter, NavigationDebugRequestType::FailedQuery, result.status, {endPoint}, result.message);
            return result;
        }
        const std::optional<glm::mat4> world = scene.worldMatrix(actor);
        if (!world) {
            NavigationPathResult result;
            result.requestedEnd = endPoint;
            result.status = NavigationRuntimeStatus::InvalidActor;
            result.message = "Scene actor has no navigation world transform.";
            record(NavigationRuntimeQueryType::Path, result.status, result.message, elapsedMicroseconds(start));
            appendDebugRequest(filter, NavigationDebugRequestType::FailedQuery, result.status, {endPoint}, result.message);
            return result;
        }
        const glm::vec3 startPoint{(*world)[3]};
        return findPath(startPoint, endPoint, agent, filter);
    }

    NavigationReachabilityResult SceneNavigationService::reachable(
        glm::vec3 start,
        glm::vec3 end,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto started = std::chrono::steady_clock::now();
        NavigationReachabilityResult result;
        result.path = findPath(start, end, agent, filter);
        result.reachable = result.path.status == NavigationRuntimeStatus::Success && result.path.path.complete;
        result.status = result.reachable ? NavigationRuntimeStatus::Success : result.path.status;
        result.message = result.reachable ? "Navigation target is reachable." : result.path.message;
        record(
            NavigationRuntimeQueryType::Reachability,
            result.status,
            result.message,
            elapsedMicroseconds(started),
            static_cast<uint32_t>(result.path.path.points.size()),
            result.path.length);
        return result;
    }

    NavigationRaycastResult SceneNavigationService::raycast(
        glm::vec3 startPoint,
        glm::vec3 endPoint,
        const NavigationAgentConfig& agent,
        const NavigationQueryFilter& filter)
    {
        const auto started = std::chrono::steady_clock::now();
        NavigationRaycastResult result;
        result.start = startPoint;
        result.end = endPoint;
        result.hitPoint = endPoint;
        if (!validPoint(startPoint) || !validPoint(endPoint) || !validAgent(agent)) {
            result.status = NavigationRuntimeStatus::InvalidInput;
            result.blocked = true;
            result.hitPoint = startPoint;
            result.message = "Invalid navigation raycast input.";
        } else {
            const NavigationPathResult path = findPath(startPoint, endPoint, agent, filter);
            result.status = path.status;
            result.message = path.message;
            if (path.status == NavigationRuntimeStatus::Success && path.path.complete) {
                float maxDeviation = 0.0f;
                for (const glm::vec3& point : path.path.points) {
                    maxDeviation = std::max(maxDeviation, xzDistanceToSegment(point, startPoint, endPoint));
                }
                result.pathDeviation = maxDeviation;
                const float allowedDeviation = std::max(agent.radius * 2.0f, DefaultRaycastDeviationPadding);
                result.blocked = maxDeviation > allowedDeviation;
                result.status = result.blocked ? NavigationRuntimeStatus::Blocked : NavigationRuntimeStatus::Success;
                result.hitPoint = result.blocked && !path.path.points.empty() ? path.path.points.back() : endPoint;
                result.message = result.blocked
                    ? "Navigation segment is blocked by path deviation."
                    : "Navigation segment is clear.";
            } else {
                result.blocked = true;
                result.hitPoint = startPoint;
            }
        }
        record(NavigationRuntimeQueryType::Raycast, result.status, result.message, elapsedMicroseconds(started));
        appendDebugRequest(
            filter,
            result.status == NavigationRuntimeStatus::Success ? NavigationDebugRequestType::Raycast : NavigationDebugRequestType::FailedQuery,
            result.status,
            {startPoint, result.hitPoint, endPoint},
            result.message);
        return result;
    }

    std::vector<NavigationDebugRequest> SceneNavigationService::debugRequests() const
    {
        return debugRequests_;
    }

    void SceneNavigationService::clearDebugRequests()
    {
        debugRequests_.clear();
        diagnostics_.debugRequestCount = 0;
    }

    NavigationRuntimeDiagnostics SceneNavigationService::diagnostics() const
    {
        return diagnostics_;
    }

    void SceneNavigationService::resetDiagnostics()
    {
        diagnostics_ = {};
        diagnostics_.lastStatus = NavigationRuntimeStatus::Unsupported;
        diagnostics_.lastMessage = "No navigation runtime queries have run.";
    }

    bool SceneNavigationService::validAgent(const NavigationAgentConfig& agent) const
    {
        return std::isfinite(agent.radius) &&
            std::isfinite(agent.height) &&
            std::isfinite(agent.maxSlopeDegrees) &&
            std::isfinite(agent.maxClimb) &&
            std::isfinite(agent.queryExtents.x) &&
            std::isfinite(agent.queryExtents.y) &&
            std::isfinite(agent.queryExtents.z) &&
            agent.radius > 0.0f &&
            agent.height > 0.0f &&
            agent.maxSlopeDegrees >= 0.0f &&
            agent.maxClimb >= 0.0f &&
            agent.maxPathPoints > 0;
    }

    bool SceneNavigationService::validPoint(glm::vec3 point) const
    {
        return finiteVec3(point);
    }

    void SceneNavigationService::record(
        NavigationRuntimeQueryType type,
        NavigationRuntimeStatus status,
        std::string message,
        uint64_t elapsedMicrosecondsValue,
        uint32_t pathPointCount,
        float pathLength)
    {
        const size_t queryIndex = static_cast<size_t>(type);
        if (queryIndex < diagnostics_.queryCounts.size()) {
            ++diagnostics_.queryCounts[queryIndex];
        }
        const size_t statusIndex = static_cast<size_t>(status);
        if (statusIndex < RuntimeStatusCount) {
            ++diagnostics_.statusCounts[statusIndex];
        }
        if (status == NavigationRuntimeStatus::NoTile) {
            ++diagnostics_.missingTileCount;
        }
        if (status == NavigationRuntimeStatus::InvalidActor) {
            ++diagnostics_.invalidActorCount;
        }
        diagnostics_.lastQueryType = type;
        diagnostics_.lastStatus = status;
        diagnostics_.lastMessage = std::move(message);
        diagnostics_.lastElapsedMicroseconds = elapsedMicrosecondsValue;
        diagnostics_.lastPathPointCount = pathPointCount;
        diagnostics_.lastPathLength = pathLength;
    }

    void SceneNavigationService::appendDebugRequest(
        const NavigationQueryFilter& filter,
        NavigationDebugRequestType type,
        NavigationRuntimeStatus status,
        std::vector<glm::vec3> points,
        std::string message)
    {
        if (!filter.captureDebug) {
            return;
        }
        debugRequests_.push_back({type, status, std::move(points), std::move(message)});
        diagnostics_.debugRequestCount = static_cast<uint32_t>(debugRequests_.size());
    }
}
