#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Navigation.hpp"

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
        glm::vec3 connectedNeighborPosition{};
    };

    struct ChunkNavConnectivity {
        ChunkCoord coord;
        std::string biomeId;
        float traversalCost = 0.0f;
        bool partial = false;
        std::array<std::vector<ChunkNavPortal>, NavEdgeDirectionCount> portalsByEdge;
    };

    struct NavigationConnectivitySettings {
        uint32_t samplesPerEdge = 9;
        float edgeInset = 1.5f;
        float edgeBandWidth = 4.0f;
        float portalMergeDistance = 2.0f;
        float neighborLinkDistance = 6.0f;
        float maxPortalHeightDelta = 0.65f;
        bool requireCenterReachability = true;
    };

    struct NavigationConnectivityStats {
        uint32_t chunkCount = 0;
        uint32_t totalPortals = 0;
        uint32_t connectedPortals = 0;
        uint32_t partialChunks = 0;
        uint32_t blockedChunks = 0;
    };

    struct NavigationPortalEdgeDiagnostics {
        uint32_t sampleCount = 0;
        uint32_t acceptedPortalCount = 0;
        uint32_t connectedPortalCount = 0;
        uint32_t rejectedNoNearestPolyCount = 0;
        uint32_t rejectedEdgeBandCount = 0;
        uint32_t rejectedCenterReachabilityCount = 0;
        uint32_t rejectedHeightDeltaCount = 0;
        uint32_t mergedDuplicateCount = 0;
    };

    struct ChunkPortalDiagnostics {
        ChunkCoord coord;
        std::array<NavigationPortalEdgeDiagnostics, NavEdgeDirectionCount> edges;
    };

    struct NavigationConnectivityCacheData {
        std::vector<ChunkNavConnectivity> chunks;
    };

    enum class NavigationConnectivityBuildPhase : uint32_t {
        StartChunk,
        NorthEdge,
        SouthEdge,
        EastEdge,
        WestEdge,
        RelinkNeighbors,
        FinalizeChunk,
        Complete,
    };

    struct NavigationConnectivityBuildHandle {
        uint64_t id = UINT64_MAX;
    };

    inline bool operator==(NavigationConnectivityBuildHandle lhs, NavigationConnectivityBuildHandle rhs)
    {
        return lhs.id == rhs.id;
    }

    struct NavigationConnectivityBuildRequest {
        std::vector<ChunkCoord> chunks;
        bool clearExisting = false;
    };

    struct NavigationConnectivityBuildStepResult {
        bool ranStep = false;
        bool complete = false;
        ChunkCoord coord;
        NavigationConnectivityBuildPhase phase = NavigationConnectivityBuildPhase::Complete;
        uint32_t samplesProcessed = 0;
        std::string label;
    };

    class NavigationConnectivitySystem {
    public:
        explicit NavigationConnectivitySystem(NavigationConnectivitySettings settings = {});

        void rebuild(
            const std::vector<ChunkCoord>& loadedNavChunks,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent);
        void rebuildChunk(
            ChunkCoord coord,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent);
        void rebuildChunks(
            std::span<const ChunkCoord> coords,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent);
        NavigationConnectivityBuildHandle beginRebuild(NavigationConnectivityBuildRequest request);
        NavigationConnectivityBuildStepResult stepRebuild(
            NavigationConnectivityBuildHandle handle,
            const NavigationSystem& navigation,
            const NavAgentSettings& agent,
            uint32_t maxSamples);
        void cancelRebuild(NavigationConnectivityBuildHandle handle);
        bool hasActiveRebuild(NavigationConnectivityBuildHandle handle) const;
        void removeChunk(ChunkCoord coord);
        void relinkChunkAndNeighbors(ChunkCoord coord);
        void clear();

        const NavigationConnectivitySettings& settings() const;
        void setSettings(NavigationConnectivitySettings settings);
        const std::unordered_map<ChunkCoord, ChunkNavConnectivity, ChunkCoordHash>& all() const;
        const ChunkNavConnectivity* connectivity(ChunkCoord coord) const;
        const ChunkPortalDiagnostics* portalDiagnostics(ChunkCoord coord) const;
        const std::unordered_map<ChunkCoord, ChunkPortalDiagnostics, ChunkCoordHash>& allPortalDiagnostics() const;
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
            const NavAgentSettings& agent,
            NavigationPortalEdgeDiagnostics& diagnostics) const;
        bool shouldMergePortal(const std::vector<ChunkNavPortal>& portals, const glm::vec3& position) const;
        void markLoadedNeighborConnections();
        static const char* phaseLabel(NavigationConnectivityBuildPhase phase);

        struct ActiveRebuild {
            NavigationConnectivityBuildHandle handle;
            std::vector<ChunkCoord> chunks;
            size_t chunkIndex = 0;
            NavigationConnectivityBuildPhase phase = NavigationConnectivityBuildPhase::StartChunk;
            uint32_t sampleIndex = 0;
            std::optional<Renderer::Aabb> bounds;
            ChunkNavConnectivity connectivity;
            ChunkPortalDiagnostics diagnostics;
        };

        NavigationConnectivitySettings settings_;
        std::unordered_map<ChunkCoord, ChunkNavConnectivity, ChunkCoordHash> connectivity_;
        std::unordered_map<ChunkCoord, ChunkPortalDiagnostics, ChunkCoordHash> diagnostics_;
        std::optional<ActiveRebuild> activeRebuild_;
        uint64_t nextBuildHandleId_ = 1;
    };
}
