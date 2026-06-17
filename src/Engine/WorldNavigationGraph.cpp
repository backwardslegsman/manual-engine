#include "Engine/WorldNavigationGraph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>
#include <utility>

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

        float xzDistance(const glm::vec3& a, const glm::vec3& b)
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return std::sqrt(dx * dx + dz * dz);
        }

        const ChunkNavConnectivity* connectivityFor(
            const NavigationConnectivityCacheData& loadedConnectivity,
            ChunkCoord coord)
        {
            const auto connectivity = std::ranges::find_if(loadedConnectivity.chunks, [coord](const ChunkNavConnectivity& chunk) {
                return chunk.coord == coord;
            });
            return connectivity == loadedConnectivity.chunks.end() ? nullptr : &*connectivity;
        }

        BiomeSample sampleChunkBiome(const WorldNavigationTerrainSnapshot& terrain, ChunkCoord coord)
        {
            return terrain.biomes.sampleChunk(coord, terrain.chunkSize);
        }

        float generatedHeight(const WorldNavigationTerrainSnapshot& terrain, float worldX, float worldZ)
        {
            const BiomeSample sample = terrain.biomes.sample(worldX, worldZ);
            const BiomeDescriptor* biome = terrain.biomes.descriptor(sample.id);
            const float heightScale = biome ? biome->heightScale : 1.0f;
            const float baseHeight = biome ? biome->baseHeight : 0.0f;
            const float rollingAmplitude = biome ? biome->rollingAmplitude * biome->rollingScale : 1.0f;
            const float rollingFrequencyX = biome ? biome->rollingFrequencyX : 0.18f;
            const float rollingFrequencyZ = biome ? biome->rollingFrequencyZ : 0.15f;
            const float detailAmplitude = biome ? biome->detailAmplitude * biome->detailScale : 0.35f;
            const float detailFrequency = biome ? biome->detailFrequency : 0.07f;
            const float rolling =
                std::sin(worldX * rollingFrequencyX) * rollingAmplitude +
                std::cos(worldZ * rollingFrequencyZ) * rollingAmplitude;
            const float detail = std::sin((worldX + worldZ) * detailFrequency) * detailAmplitude;
            return baseHeight + (rolling + detail) * terrain.heightScale * heightScale;
        }

        glm::vec3 centerForChunk(ChunkCoord coord, const WorldNavigationGraphSettings& settings, const WorldNavigationTerrainSnapshot& terrain)
        {
            glm::vec3 center{
                (static_cast<float>(coord.x) + 0.5f) * settings.chunkSize,
                0.0f,
                (static_cast<float>(coord.z) + 0.5f) * settings.chunkSize,
            };
            center.y = generatedHeight(terrain, center.x, center.z);
            return center;
        }

        float costForChunk(ChunkCoord coord, const WorldNavigationTerrainSnapshot& terrain)
        {
            const BiomeSample sample = sampleChunkBiome(terrain, coord);
            return std::max(0.1f, 1.0f + sample.roughness * 2.0f + std::abs(sample.elevation) * 0.5f);
        }

        bool edgeBlockedByLoadedConnectivity(
            ChunkCoord from,
            NavEdgeDirection direction,
            const NavigationConnectivityCacheData& loadedConnectivity)
        {
            const ChunkNavConnectivity* connectivity = connectivityFor(loadedConnectivity, from);
            if (!connectivity) {
                return false;
            }

            const std::vector<ChunkNavPortal>& portals = connectivity->portalsByEdge[static_cast<uint32_t>(direction)];
            if (portals.empty()) {
                return true;
            }

            const ChunkCoord neighbor = neighborFor(from, direction);
            if (!connectivityFor(loadedConnectivity, neighbor)) {
                return false;
            }

            return std::ranges::none_of(portals, [](const ChunkNavPortal& portal) {
                return portal.connectedToLoadedNeighbor;
            });
        }

        std::vector<WorldNavEdge> makeEdges(
            ChunkCoord from,
            ChunkCoord to,
            NavEdgeDirection direction,
            float cost,
            bool blocked,
            const WorldNavigationGraphSettings& settings,
            const NavigationConnectivityCacheData& loadedConnectivity)
        {
            std::vector<WorldNavEdge> result;
            glm::vec3 waypoint{
                (static_cast<float>(from.x) + 0.5f) * settings.chunkSize,
                0.0f,
                (static_cast<float>(from.z) + 0.5f) * settings.chunkSize,
            };
            glm::vec3 ingressWaypoint{
                (static_cast<float>(to.x) + 0.5f) * settings.chunkSize,
                0.0f,
                (static_cast<float>(to.z) + 0.5f) * settings.chunkSize,
            };

            if (const ChunkNavConnectivity* connectivity = connectivityFor(loadedConnectivity, from)) {
                const std::vector<ChunkNavPortal>& portals = connectivity->portalsByEdge[static_cast<uint32_t>(direction)];
                for (const ChunkNavPortal& portal : portals) {
                    if (!portal.connectedToLoadedNeighbor) {
                        continue;
                    }
                    result.push_back({from, to, direction, cost, blocked, portal.position, portal.connectedNeighborPosition});
                }
                if (!result.empty()) {
                    return result;
                }
                if (!portals.empty()) {
                    waypoint = portals.front().position;
                }
            }

            switch (direction) {
                case NavEdgeDirection::North:
                    waypoint.z += settings.chunkSize * 0.5f;
                    ingressWaypoint.z -= settings.chunkSize * 0.5f;
                    break;
                case NavEdgeDirection::South:
                    waypoint.z -= settings.chunkSize * 0.5f;
                    ingressWaypoint.z += settings.chunkSize * 0.5f;
                    break;
                case NavEdgeDirection::East:
                    waypoint.x += settings.chunkSize * 0.5f;
                    ingressWaypoint.x -= settings.chunkSize * 0.5f;
                    break;
                case NavEdgeDirection::West:
                    waypoint.x -= settings.chunkSize * 0.5f;
                    ingressWaypoint.x += settings.chunkSize * 0.5f;
                    break;
                case NavEdgeDirection::Count:
                    break;
            }
            result.push_back({from, to, direction, cost, blocked, waypoint, ingressWaypoint});
            return result;
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
        const WorldNavigationGraphBuildResult result = buildCacheData(buildInput(centerChunk, settings_, terrain, loadedConnectivity));
        loadCacheData(result.graph);
    }

    WorldNavigationGraphBuildInput WorldNavigationGraph::buildInput(
        ChunkCoord centerChunk,
        const WorldNavigationGraphSettings& settings,
        const TerrainSystem& terrain,
        const NavigationConnectivitySystem& loadedConnectivity)
    {
        WorldNavigationGraphBuildInput input;
        input.centerChunk = centerChunk;
        input.settings = settings;
        input.settings.graphRadiusChunks = std::max(input.settings.graphRadiusChunks, 0);
        input.settings.chunkSize = std::max(input.settings.chunkSize, 1.0f);
        input.terrain.chunkSize = terrain.settings().chunkSize;
        input.terrain.heightScale = terrain.settings().heightScale;
        input.terrain.biomes = terrain.settings().biomes ? *terrain.settings().biomes : BiomeSystem::sampleDefaults();
        input.loadedConnectivity = loadedConnectivity.cacheData();
        return input;
    }

    WorldNavigationGraphBuildResult WorldNavigationGraph::buildCacheData(const WorldNavigationGraphBuildInput& input)
    {
        const auto start = std::chrono::steady_clock::now();
        WorldNavigationGraphBuildResult result;
        result.centerChunk = input.centerChunk;
        result.graph.centerChunk = input.centerChunk;
        result.graph.hasGraph = true;

        const int32_t radius = std::max(input.settings.graphRadiusChunks, 0);
        const float chunkSize = std::max(input.settings.chunkSize, 1.0f);
        WorldNavigationGraphSettings settings = input.settings;
        settings.graphRadiusChunks = radius;
        settings.chunkSize = chunkSize;
        WorldNavigationTerrainSnapshot terrain = input.terrain;
        terrain.chunkSize = std::max(terrain.chunkSize, 1.0f);

        std::unordered_map<ChunkCoord, WorldNavNode, ChunkCoordHash> nodes;
        for (int32_t z = input.centerChunk.z - radius; z <= input.centerChunk.z + radius; ++z) {
            for (int32_t x = input.centerChunk.x - radius; x <= input.centerChunk.x + radius; ++x) {
                const ChunkCoord coord{x, z};
                const BiomeSample biome = sampleChunkBiome(terrain, coord);
                nodes.emplace(coord, WorldNavNode{
                    coord,
                    biome.id,
                    centerForChunk(coord, settings, terrain),
                    costForChunk(coord, terrain),
                });
            }
        }

        for (const auto& [coord, node] : nodes) {
            for (uint32_t index = 0; index < NavEdgeDirectionCount; ++index) {
                const NavEdgeDirection direction = static_cast<NavEdgeDirection>(index);
                const ChunkCoord neighbor = neighborFor(coord, direction);
                const auto neighborIt = nodes.find(neighbor);
                if (neighborIt == nodes.end()) {
                    continue;
                }

                const bool blocked = edgeBlockedByLoadedConnectivity(coord, direction, input.loadedConnectivity);
                const float cost = (node.traversalCost + neighborIt->second.traversalCost) * 0.5f;
                std::vector<WorldNavEdge> edges = makeEdges(coord, neighbor, direction, cost, blocked, settings, input.loadedConnectivity);
                result.graph.edges.insert(result.graph.edges.end(), edges.begin(), edges.end());
            }
        }

        result.graph.nodes.reserve(nodes.size());
        for (const auto& [_, node] : nodes) {
            result.graph.nodes.push_back(node);
        }
        std::ranges::sort(result.graph.nodes, [](const WorldNavNode& lhs, const WorldNavNode& rhs) {
            return lhs.coord.x == rhs.coord.x ? lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
        });
        std::ranges::sort(result.graph.edges, [](const WorldNavEdge& lhs, const WorldNavEdge& rhs) {
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

        const auto end = std::chrono::steady_clock::now();
        result.buildMs = std::chrono::duration<float, std::milli>(end - start).count();
        result.success = !result.graph.nodes.empty();
        std::ostringstream message;
        message << "Built world navigation graph with " << result.graph.nodes.size() << " nodes and "
                << result.graph.edges.size() << " edges.";
        result.message = message.str();
        return result;
    }

    void WorldNavigationGraph::clear()
    {
        nodes_.clear();
        edges_.clear();
        hasGraph_ = false;
    }

    WorldNavRoute WorldNavigationGraph::findRoute(const glm::vec3& startWorldPosition, const glm::vec3& endWorldPosition) const
    {
        return findRouteInternal(startWorldPosition, endWorldPosition, false);
    }

    WorldNavRoute WorldNavigationGraph::findRouteAllowingSameChunkDetour(
        const glm::vec3& startWorldPosition,
        const glm::vec3& endWorldPosition) const
    {
        return findRouteInternal(startWorldPosition, endWorldPosition, true);
    }

    WorldNavRoute WorldNavigationGraph::findRouteInternal(
        const glm::vec3& startWorldPosition,
        const glm::vec3& endWorldPosition,
        bool allowSameChunkDetour) const
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

        if (start == goal && allowSameChunkDetour) {
            const WorldNavEdge* bestExit = nullptr;
            const WorldNavEdge* bestReturn = nullptr;
            float bestCost = std::numeric_limits<float>::max();
            for (const WorldNavEdge& exitEdge : edges_) {
                if (exitEdge.from != start || exitEdge.blocked) {
                    continue;
                }
                for (const WorldNavEdge& returnEdge : edges_) {
                    if (returnEdge.from != exitEdge.to || returnEdge.to != start || returnEdge.blocked) {
                        continue;
                    }

                    const float cost =
                        exitEdge.cost +
                        returnEdge.cost +
                        xzDistance(startWorldPosition, exitEdge.waypoint) / settings_.chunkSize +
                        xzDistance(exitEdge.ingressWaypoint, returnEdge.waypoint) / settings_.chunkSize +
                        xzDistance(returnEdge.ingressWaypoint, endWorldPosition) / settings_.chunkSize;
                    if (cost < bestCost) {
                        bestCost = cost;
                        bestExit = &exitEdge;
                        bestReturn = &returnEdge;
                    }
                }
            }

            if (!bestExit || !bestReturn) {
                route.status = WorldNavRouteStatus::NoRoute;
                route.message = "No same-chunk detour route found.";
                return route;
            }

            route.chunkSequence = {start, bestExit->to, start};
            route.portalWaypoints = {
                startWorldPosition,
                bestExit->waypoint,
                bestExit->ingressWaypoint,
                bestReturn->waypoint,
                bestReturn->ingressWaypoint,
                endWorldPosition,
            };
            route.waypointChunks = {
                start,
                start,
                bestExit->to,
                bestReturn->from,
                start,
                goal,
            };
            route.totalCost = bestCost;
            route.status = WorldNavRouteStatus::Success;
            route.message = "Same-chunk detour route found.";
            return route;
        }

        std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> frontier;
        std::unordered_map<ChunkCoord, ChunkCoord, ChunkCoordHash> cameFrom;
        std::unordered_map<ChunkCoord, size_t, ChunkCoordHash> cameFromEdge;
        std::unordered_map<ChunkCoord, float, ChunkCoordHash> costSoFar;
        frontier.push({start, 0.0f});
        costSoFar[start] = 0.0f;

        while (!frontier.empty()) {
            const ChunkCoord current = frontier.top().coord;
            frontier.pop();
            if (current == goal) {
                break;
            }

            for (size_t edgeIndex = 0; edgeIndex < edges_.size(); ++edgeIndex) {
                const WorldNavEdge& edge = edges_[edgeIndex];
                if (edge.from != current || edge.blocked) {
                    continue;
                }

                float traversalCost = edge.cost;
                if (current == start) {
                    traversalCost += xzDistance(startWorldPosition, edge.waypoint) / settings_.chunkSize;
                }
                if (edge.to == goal) {
                    traversalCost += xzDistance(edge.ingressWaypoint, endWorldPosition) / settings_.chunkSize;
                }
                const float newCost = costSoFar[current] + traversalCost;
                const auto costIt = costSoFar.find(edge.to);
                if (costIt != costSoFar.end() && newCost >= costIt->second) {
                    continue;
                }

                costSoFar[edge.to] = newCost;
                cameFrom[edge.to] = current;
                cameFromEdge[edge.to] = edgeIndex;
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
        route.waypointChunks.push_back(start);
        for (size_t index = 1; index < route.chunkSequence.size(); ++index) {
            const ChunkCoord to = route.chunkSequence[index];
            const auto selectedEdgeIt = cameFromEdge.find(to);
            if (selectedEdgeIt != cameFromEdge.end() && selectedEdgeIt->second < edges_.size()) {
                const WorldNavEdge& edge = edges_[selectedEdgeIt->second];
                route.portalWaypoints.push_back(edge.waypoint);
                route.waypointChunks.push_back(edge.from);
                route.portalWaypoints.push_back(edge.ingressWaypoint);
                route.waypointChunks.push_back(to);
            }
        }
        route.portalWaypoints.push_back(endWorldPosition);
        route.waypointChunks.push_back(goal);
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

}
