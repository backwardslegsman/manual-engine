#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Biome.hpp"
#include "Engine/ChunkTypes.hpp"
#include "Engine/NavigationConnectivity.hpp"
#include "Engine/Terrain.hpp"

namespace Engine {
    struct WorldNavNode {
        ChunkCoord coord;
        std::string biomeId;
        glm::vec3 position{};
        float traversalCost = 1.0f;
    };

    struct WorldNavEdge {
        ChunkCoord from;
        ChunkCoord to;
        NavEdgeDirection direction = NavEdgeDirection::North;
        float cost = 1.0f;
        bool blocked = false;
        glm::vec3 waypoint{};
        glm::vec3 ingressWaypoint{};
    };

    enum class WorldNavRouteStatus {
        Success,
        InvalidEndpoint,
        NoGraph,
        NoRoute,
    };

    struct WorldNavRoute {
        WorldNavRouteStatus status = WorldNavRouteStatus::NoGraph;
        std::vector<ChunkCoord> chunkSequence;
        std::vector<glm::vec3> portalWaypoints;
        std::vector<ChunkCoord> waypointChunks;
        float totalCost = 0.0f;
        std::string message;
    };

    struct WorldNavigationGraphSettings {
        int32_t graphRadiusChunks = 64;
        float chunkSize = 24.0f;
    };

    struct WorldNavigationGraphStats {
        uint32_t nodeCount = 0;
        uint32_t edgeCount = 0;
        uint32_t blockedEdgeCount = 0;
        ChunkCoord centerChunk{};
        bool hasGraph = false;
    };

    struct WorldNavigationGraphCacheData {
        ChunkCoord centerChunk{};
        bool hasGraph = false;
        std::vector<WorldNavNode> nodes;
        std::vector<WorldNavEdge> edges;
    };

    class WorldNavigationGraph {
    public:
        explicit WorldNavigationGraph(WorldNavigationGraphSettings settings = {});

        void rebuild(
            ChunkCoord centerChunk,
            const TerrainSystem& terrain,
            const NavigationConnectivitySystem& loadedConnectivity);
        void clear();

        WorldNavRoute findRoute(const glm::vec3& startWorldPosition, const glm::vec3& endWorldPosition) const;
        WorldNavRoute findRouteAllowingSameChunkDetour(const glm::vec3& startWorldPosition, const glm::vec3& endWorldPosition) const;
        const WorldNavNode* node(ChunkCoord coord) const;
        const std::vector<WorldNavEdge>& edges() const;
        const std::unordered_map<ChunkCoord, WorldNavNode, ChunkCoordHash>& nodes() const;
        WorldNavigationGraphCacheData cacheData() const;
        void loadCacheData(const WorldNavigationGraphCacheData& cacheData);
        WorldNavigationGraphStats stats() const;
        const WorldNavigationGraphSettings& settings() const;

    private:
        ChunkCoord coordForWorldPosition(const glm::vec3& position) const;
        glm::vec3 centerForChunk(ChunkCoord coord, const TerrainSystem& terrain) const;
        float costForChunk(ChunkCoord coord, const TerrainSystem& terrain) const;
        bool edgeBlockedByLoadedConnectivity(ChunkCoord from, NavEdgeDirection direction, const NavigationConnectivitySystem& loadedConnectivity) const;
        std::vector<WorldNavEdge> makeEdges(
            ChunkCoord from,
            ChunkCoord to,
            NavEdgeDirection direction,
            float cost,
            bool blocked,
            const NavigationConnectivitySystem& loadedConnectivity) const;
        WorldNavRoute findRouteInternal(
            const glm::vec3& startWorldPosition,
            const glm::vec3& endWorldPosition,
            bool allowSameChunkDetour) const;

        WorldNavigationGraphSettings settings_;
        ChunkCoord centerChunk_{};
        bool hasGraph_ = false;
        std::unordered_map<ChunkCoord, WorldNavNode, ChunkCoordHash> nodes_;
        std::vector<WorldNavEdge> edges_;
    };
}
