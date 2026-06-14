#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Terrain.hpp"

namespace Engine {
    enum class NavEdgeDirection : uint32_t {
        North = 0,
        South = 1,
        East = 2,
        West = 3,
        Count = 4,
    };

    constexpr uint32_t NavEdgeDirectionCount = static_cast<uint32_t>(NavEdgeDirection::Count);

    struct ChunkNavPortal {
        NavEdgeDirection direction = NavEdgeDirection::North;
        glm::vec3 position{};
        ChunkCoord neighborCoord{};
        bool reachableFromChunkCenter = false;
        bool connectedToLoadedNeighbor = false;
    };

    struct ChunkNavConnectivity {
        ChunkCoord coord;
        std::string biomeId;
        float traversalCost = 0.0f;
        bool partial = false;
        std::array<std::vector<ChunkNavPortal>, NavEdgeDirectionCount> portalsByEdge;
    };

    struct NavigationConnectivitySettings {
        uint32_t samplesPerEdge = 5;
        float edgeInset = 0.75f;
        float edgeBandWidth = 1.5f;
        float portalMergeDistance = 1.25f;
        float neighborLinkDistance = 2.5f;
    };

    struct NavigationConnectivityStats {
        uint32_t chunkCount = 0;
        uint32_t totalPortals = 0;
        uint32_t connectedPortals = 0;
        uint32_t partialChunks = 0;
        uint32_t blockedChunks = 0;
    };

    struct NavigationConnectivityCacheData {
        std::vector<ChunkNavConnectivity> chunks;
    };

    class NavigationConnectivitySystem {
    public:
        explicit NavigationConnectivitySystem(NavigationConnectivitySettings settings = {});

        void rebuild(
            const std::vector<ChunkCoord>& loadedNavChunks,
            const NavigationSystem& navigation,
            const TerrainSystem& terrain,
            const NavAgentSettings& agent);
        void clear();

        const NavigationConnectivitySettings& settings() const;
        const std::unordered_map<ChunkCoord, ChunkNavConnectivity, ChunkCoordHash>& all() const;
        const ChunkNavConnectivity* connectivity(ChunkCoord coord) const;
        NavigationConnectivityCacheData cacheData() const;
        void loadCacheData(const NavigationConnectivityCacheData& cacheData);
        NavigationConnectivityStats stats() const;

    private:
        std::optional<ChunkNavPortal> buildPortalSample(
            ChunkCoord coord,
            NavEdgeDirection direction,
            float t,
            const Renderer::Aabb& bounds,
            const NavigationSystem& navigation,
            const TerrainSystem& terrain,
            const NavAgentSettings& agent) const;
        bool shouldMergePortal(const std::vector<ChunkNavPortal>& portals, const glm::vec3& position) const;
        void markLoadedNeighborConnections();

        NavigationConnectivitySettings settings_;
        std::unordered_map<ChunkCoord, ChunkNavConnectivity, ChunkCoordHash> connectivity_;
    };
}
