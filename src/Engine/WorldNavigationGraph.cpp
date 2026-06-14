#include "Engine/WorldNavigationGraph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>

namespace Engine {
    namespace {
        struct QueueNode {
            ChunkCoord coord;
            float priority = 0.0f;
        };

        bool operator>(const QueueNode& lhs, const QueueNode& rhs)
        {
            return lhs.priority > rhs.priority;
        }

        NavEdgeDirection directionTo(ChunkCoord from, ChunkCoord to)
        {
            if (to.x > from.x) {
                return NavEdgeDirection::East;
            }
            if (to.x < from.x) {
                return NavEdgeDirection::West;
            }
            if (to.z > from.z) {
                return NavEdgeDirection::North;
            }
            return NavEdgeDirection::South;
        }

        ChunkCoord neighborFor(ChunkCoord coord, NavEdgeDirection direction)
        {
            switch (direction) {
                case NavEdgeDirection::North:
                    return {coord.x, coord.z + 1};
                case NavEdgeDirection::South:
                    return {coord.x, coord.z - 1};
                case NavEdgeDirection::East:
                    return {coord.x + 1, coord.z};
                case NavEdgeDirection::West:
                    return {coord.x - 1, coord.z};
                case NavEdgeDirection::Count:
                    break;
            }
            return coord;
        }

        float heuristic(ChunkCoord a, ChunkCoord b)
        {
            return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.z - b.z));
        }
    }

    WorldNavigationGraph::WorldNavigationGraph(WorldNavigationGraphSettings settings)
        : settings_(settings)
    {
        settings_.graphRadiusChunks = std::max(settings_.graphRadiusChunks, 0);
        settings_.chunkSize = std::max(settings_.chunkSize, 1.0f);
    }

    void WorldNavigationGraph::rebuild(
        ChunkCoord centerChunk,
        const TerrainSystem& terrain,
        const NavigationConnectivitySystem& loadedConnectivity)
    {
        clear();
        centerChunk_ = centerChunk;
        hasGraph_ = true;

        const int32_t radius = settings_.graphRadiusChunks;
        for (int32_t z = centerChunk.z - radius; z <= centerChunk.z + radius; ++z) {
            for (int32_t x = centerChunk.x - radius; x <= centerChunk.x + radius; ++x) {
                const ChunkCoord coord{x, z};
                const BiomeSample biome = terrain.sampleChunkBiome(coord);
                nodes_.emplace(coord, WorldNavNode{
                    coord,
                    biome.id,
                    centerForChunk(coord, terrain),
                    costForChunk(coord, terrain),
                });
            }
        }

        for (const auto& [coord, node] : nodes_) {
            (void)node;
            for (uint32_t index = 0; index < NavEdgeDirectionCount; ++index) {
                const NavEdgeDirection direction = static_cast<NavEdgeDirection>(index);
                const ChunkCoord neighbor = neighborFor(coord, direction);
                const auto neighborIt = nodes_.find(neighbor);
                if (neighborIt == nodes_.end()) {
                    continue;
                }

                const bool blocked = edgeBlockedByLoadedConnectivity(coord, direction, loadedConnectivity);
                const float cost = (node.traversalCost + neighborIt->second.traversalCost) * 0.5f;
                edges_.push_back({coord, neighbor, direction, cost, blocked, waypointBetween(coord, neighbor, loadedConnectivity)});
            }
        }
    }

    void WorldNavigationGraph::clear()
    {
        nodes_.clear();
        edges_.clear();
        hasGraph_ = false;
    }

    WorldNavRoute WorldNavigationGraph::findRoute(const glm::vec3& startWorldPosition, const glm::vec3& endWorldPosition) const
    {
        WorldNavRoute route;
        if (!hasGraph_ || nodes_.empty()) {
            route.status = WorldNavRouteStatus::NoGraph;
            route.message = "World navigation graph is empty.";
            return route;
        }

        const ChunkCoord start = coordForWorldPosition(startWorldPosition);
        const ChunkCoord goal = coordForWorldPosition(endWorldPosition);
        if (!nodes_.contains(start) || !nodes_.contains(goal)) {
            route.status = WorldNavRouteStatus::InvalidEndpoint;
            route.message = "Route endpoint is outside the generated graph.";
            return route;
        }

        std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> frontier;
        std::unordered_map<ChunkCoord, ChunkCoord, ChunkCoordHash> cameFrom;
        std::unordered_map<ChunkCoord, float, ChunkCoordHash> costSoFar;
        frontier.push({start, 0.0f});
        costSoFar[start] = 0.0f;

        while (!frontier.empty()) {
            const ChunkCoord current = frontier.top().coord;
            frontier.pop();
            if (current == goal) {
                break;
            }

            for (const WorldNavEdge& edge : edges_) {
                if (edge.from != current || edge.blocked) {
                    continue;
                }

                const float newCost = costSoFar[current] + edge.cost;
                const auto costIt = costSoFar.find(edge.to);
                if (costIt != costSoFar.end() && newCost >= costIt->second) {
                    continue;
                }

                costSoFar[edge.to] = newCost;
                cameFrom[edge.to] = current;
                frontier.push({edge.to, newCost + heuristic(edge.to, goal)});
            }
        }

        if (!costSoFar.contains(goal)) {
            route.status = WorldNavRouteStatus::NoRoute;
            route.message = "No coarse route found.";
            return route;
        }

        std::vector<ChunkCoord> reversed;
        for (ChunkCoord current = goal; !(current == start); current = cameFrom[current]) {
            reversed.push_back(current);
        }
        reversed.push_back(start);
        route.chunkSequence.assign(reversed.rbegin(), reversed.rend());
        route.portalWaypoints.push_back(startWorldPosition);
        for (size_t index = 1; index < route.chunkSequence.size(); ++index) {
            const ChunkCoord from = route.chunkSequence[index - 1];
            const ChunkCoord to = route.chunkSequence[index];
            const auto edgeIt = std::ranges::find_if(edges_, [&](const WorldNavEdge& edge) {
                return edge.from == from && edge.to == to && !edge.blocked;
            });
            if (edgeIt != edges_.end()) {
                route.portalWaypoints.push_back(edgeIt->waypoint);
            }
        }
        route.portalWaypoints.push_back(endWorldPosition);
        route.totalCost = costSoFar[goal];
        route.status = WorldNavRouteStatus::Success;
        route.message = "Coarse route found.";
        return route;
    }

    const WorldNavNode* WorldNavigationGraph::node(ChunkCoord coord) const
    {
        const auto nodeIt = nodes_.find(coord);
        return nodeIt == nodes_.end() ? nullptr : &nodeIt->second;
    }

    const std::vector<WorldNavEdge>& WorldNavigationGraph::edges() const
    {
        return edges_;
    }

    const std::unordered_map<ChunkCoord, WorldNavNode, ChunkCoordHash>& WorldNavigationGraph::nodes() const
    {
        return nodes_;
    }

    WorldNavigationGraphCacheData WorldNavigationGraph::cacheData() const
    {
        WorldNavigationGraphCacheData data;
        data.centerChunk = centerChunk_;
        data.hasGraph = hasGraph_;
        data.nodes.reserve(nodes_.size());
        for (const auto& [_, node] : nodes_) {
            data.nodes.push_back(node);
        }
        std::ranges::sort(data.nodes, [](const WorldNavNode& lhs, const WorldNavNode& rhs) {
            return lhs.coord.x == rhs.coord.x ? lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
        });
        data.edges = edges_;
        std::ranges::sort(data.edges, [](const WorldNavEdge& lhs, const WorldNavEdge& rhs) {
            if (lhs.from.x != rhs.from.x) {
                return lhs.from.x < rhs.from.x;
            }
            if (lhs.from.z != rhs.from.z) {
                return lhs.from.z < rhs.from.z;
            }
            if (lhs.to.x != rhs.to.x) {
                return lhs.to.x < rhs.to.x;
            }
            return lhs.to.z < rhs.to.z;
        });
        return data;
    }

    void WorldNavigationGraph::loadCacheData(const WorldNavigationGraphCacheData& cacheData)
    {
        nodes_.clear();
        for (const WorldNavNode& node : cacheData.nodes) {
            nodes_[node.coord] = node;
        }
        edges_ = cacheData.edges;
        centerChunk_ = cacheData.centerChunk;
        hasGraph_ = cacheData.hasGraph && !nodes_.empty();
        if (!hasGraph_) {
            edges_.clear();
        }
    }

    WorldNavigationGraphStats WorldNavigationGraph::stats() const
    {
        WorldNavigationGraphStats result;
        result.nodeCount = static_cast<uint32_t>(nodes_.size());
        result.edgeCount = static_cast<uint32_t>(edges_.size());
        result.centerChunk = centerChunk_;
        result.hasGraph = hasGraph_;
        for (const WorldNavEdge& edge : edges_) {
            if (edge.blocked) {
                ++result.blockedEdgeCount;
            }
        }
        return result;
    }

    const WorldNavigationGraphSettings& WorldNavigationGraph::settings() const
    {
        return settings_;
    }

    ChunkCoord WorldNavigationGraph::coordForWorldPosition(const glm::vec3& position) const
    {
        return {
            static_cast<int32_t>(std::floor(position.x / settings_.chunkSize)),
            static_cast<int32_t>(std::floor(position.z / settings_.chunkSize)),
        };
    }

    glm::vec3 WorldNavigationGraph::centerForChunk(ChunkCoord coord, const TerrainSystem& terrain) const
    {
        glm::vec3 center{
            (static_cast<float>(coord.x) + 0.5f) * settings_.chunkSize,
            0.0f,
            (static_cast<float>(coord.z) + 0.5f) * settings_.chunkSize,
        };
        center.y = terrain.sampleHeight(center.x, center.z).value_or(terrain.generatedHeight(center.x, center.z));
        return center;
    }

    float WorldNavigationGraph::costForChunk(ChunkCoord coord, const TerrainSystem& terrain) const
    {
        const BiomeSample sample = terrain.sampleChunkBiome(coord);
        return std::max(0.1f, 1.0f + sample.roughness * 2.0f + std::abs(sample.elevation) * 0.5f);
    }

    bool WorldNavigationGraph::edgeBlockedByLoadedConnectivity(
        ChunkCoord from,
        NavEdgeDirection direction,
        const NavigationConnectivitySystem& loadedConnectivity) const
    {
        const ChunkNavConnectivity* connectivity = loadedConnectivity.connectivity(from);
        if (!connectivity) {
            return false;
        }

        const std::vector<ChunkNavPortal>& portals = connectivity->portalsByEdge[static_cast<uint32_t>(direction)];
        if (portals.empty()) {
            return true;
        }

        const ChunkCoord neighbor = neighborFor(from, direction);
        if (!loadedConnectivity.connectivity(neighbor)) {
            return false;
        }

        return std::ranges::none_of(portals, [](const ChunkNavPortal& portal) {
            return portal.connectedToLoadedNeighbor;
        });
    }

    glm::vec3 WorldNavigationGraph::waypointBetween(ChunkCoord from, ChunkCoord to, const NavigationConnectivitySystem& loadedConnectivity) const
    {
        const NavEdgeDirection direction = directionTo(from, to);
        if (const ChunkNavConnectivity* connectivity = loadedConnectivity.connectivity(from)) {
            const std::vector<ChunkNavPortal>& portals = connectivity->portalsByEdge[static_cast<uint32_t>(direction)];
            const auto portalIt = std::ranges::find_if(portals, [](const ChunkNavPortal& portal) {
                return portal.connectedToLoadedNeighbor;
            });
            if (portalIt != portals.end()) {
                return portalIt->position;
            }
            if (!portals.empty()) {
                return portals.front().position;
            }
        }

        glm::vec3 waypoint{
            (static_cast<float>(from.x) + 0.5f) * settings_.chunkSize,
            0.0f,
            (static_cast<float>(from.z) + 0.5f) * settings_.chunkSize,
        };
        switch (direction) {
            case NavEdgeDirection::North:
                waypoint.z += settings_.chunkSize * 0.5f;
                break;
            case NavEdgeDirection::South:
                waypoint.z -= settings_.chunkSize * 0.5f;
                break;
            case NavEdgeDirection::East:
                waypoint.x += settings_.chunkSize * 0.5f;
                break;
            case NavEdgeDirection::West:
                waypoint.x -= settings_.chunkSize * 0.5f;
                break;
            case NavEdgeDirection::Count:
                break;
        }
        return waypoint;
    }
}
