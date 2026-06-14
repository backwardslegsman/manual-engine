#include "Engine/NavigationConnectivity.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {
    namespace {
        uint32_t edgeIndex(NavEdgeDirection direction)
        {
            return static_cast<uint32_t>(direction);
        }

        NavEdgeDirection opposite(NavEdgeDirection direction)
        {
            switch (direction) {
                case NavEdgeDirection::North:
                    return NavEdgeDirection::South;
                case NavEdgeDirection::South:
                    return NavEdgeDirection::North;
                case NavEdgeDirection::East:
                    return NavEdgeDirection::West;
                case NavEdgeDirection::West:
                    return NavEdgeDirection::East;
                case NavEdgeDirection::Count:
                    break;
            }
            return NavEdgeDirection::North;
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

        float xzDistanceSquared(const glm::vec3& a, const glm::vec3& b)
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return dx * dx + dz * dz;
        }

        glm::vec3 chunkCenter(const Renderer::Aabb& bounds, const TerrainSystem& terrain)
        {
            glm::vec3 center{
                (bounds.min.x + bounds.max.x) * 0.5f,
                0.0f,
                (bounds.min.z + bounds.max.z) * 0.5f,
            };
            center.y = terrain.sampleHeight(center.x, center.z).value_or((bounds.min.y + bounds.max.y) * 0.5f);
            return center;
        }

        bool insideEdgeBand(
            const glm::vec3& point,
            NavEdgeDirection direction,
            const Renderer::Aabb& bounds,
            float edgeBandWidth)
        {
            constexpr float edgeTolerance = 0.05f;
            const bool insideX = point.x >= bounds.min.x - edgeTolerance && point.x <= bounds.max.x + edgeTolerance;
            const bool insideZ = point.z >= bounds.min.z - edgeTolerance && point.z <= bounds.max.z + edgeTolerance;
            switch (direction) {
                case NavEdgeDirection::North:
                    return insideX && std::abs(point.z - bounds.max.z) <= edgeBandWidth;
                case NavEdgeDirection::South:
                    return insideX && std::abs(point.z - bounds.min.z) <= edgeBandWidth;
                case NavEdgeDirection::East:
                    return insideZ && std::abs(point.x - bounds.max.x) <= edgeBandWidth;
                case NavEdgeDirection::West:
                    return insideZ && std::abs(point.x - bounds.min.x) <= edgeBandWidth;
                case NavEdgeDirection::Count:
                    break;
            }
            return false;
        }
    }

    NavigationConnectivitySystem::NavigationConnectivitySystem(NavigationConnectivitySettings settings)
        : settings_(settings)
    {
    }

    void NavigationConnectivitySystem::rebuild(
        const std::vector<ChunkCoord>& loadedNavChunks,
        const NavigationSystem& navigation,
        const TerrainSystem& terrain,
        const NavAgentSettings& agent)
    {
        connectivity_.clear();
        diagnostics_.clear();

        for (ChunkCoord coord : loadedNavChunks) {
            if (!navigation.hasTile(coord)) {
                continue;
            }

            const std::optional<Renderer::Aabb> bounds = navigation.tileBounds(coord);
            if (!bounds) {
                continue;
            }

            ChunkNavConnectivity connectivity;
            connectivity.coord = coord;
            connectivity.biomeId = terrain.sampleChunkBiome(coord).id;
            connectivity.traversalCost = std::max(0.0f, bounds->max.x - bounds->min.x);
            ChunkPortalDiagnostics diagnostics;
            diagnostics.coord = coord;

            for (uint32_t directionIndex = 0; directionIndex < NavEdgeDirectionCount; ++directionIndex) {
                const NavEdgeDirection direction = static_cast<NavEdgeDirection>(directionIndex);
                std::vector<ChunkNavPortal>& portals = connectivity.portalsByEdge[directionIndex];
                NavigationPortalEdgeDiagnostics& edgeDiagnostics = diagnostics.edges[directionIndex];
                const uint32_t samples = std::max(settings_.samplesPerEdge, 1u);
                for (uint32_t sample = 0; sample < samples; ++sample) {
                    ++edgeDiagnostics.sampleCount;
                    const float t = samples == 1
                        ? 0.5f
                        : static_cast<float>(sample) / static_cast<float>(samples - 1);
                    std::optional<ChunkNavPortal> portal =
                        buildPortalSample(coord, direction, t, *bounds, navigation, terrain, agent, edgeDiagnostics);
                    if (!portal) {
                        continue;
                    }
                    if (shouldMergePortal(portals, portal->position)) {
                        ++edgeDiagnostics.mergedDuplicateCount;
                        continue;
                    }
                    portals.push_back(*portal);
                    ++edgeDiagnostics.acceptedPortalCount;
                }
            }

            uint32_t edgeCountWithPortals = 0;
            for (const std::vector<ChunkNavPortal>& portals : connectivity.portalsByEdge) {
                if (!portals.empty()) {
                    ++edgeCountWithPortals;
                }
            }
            connectivity.partial = edgeCountWithPortals < NavEdgeDirectionCount;
            diagnostics_.emplace(coord, diagnostics);
            connectivity_.emplace(coord, std::move(connectivity));
        }

        markLoadedNeighborConnections();
    }

    void NavigationConnectivitySystem::clear()
    {
        connectivity_.clear();
        diagnostics_.clear();
    }

    const NavigationConnectivitySettings& NavigationConnectivitySystem::settings() const
    {
        return settings_;
    }

    void NavigationConnectivitySystem::setSettings(NavigationConnectivitySettings settings)
    {
        settings.samplesPerEdge = std::max(settings.samplesPerEdge, 1u);
        settings.edgeInset = std::max(settings.edgeInset, 0.0f);
        settings.edgeBandWidth = std::max(settings.edgeBandWidth, 0.0f);
        settings.portalMergeDistance = std::max(settings.portalMergeDistance, 0.0f);
        settings.neighborLinkDistance = std::max(settings.neighborLinkDistance, 0.0f);
        settings_ = settings;
    }

    const std::unordered_map<ChunkCoord, ChunkNavConnectivity, ChunkCoordHash>& NavigationConnectivitySystem::all() const
    {
        return connectivity_;
    }

    const ChunkNavConnectivity* NavigationConnectivitySystem::connectivity(ChunkCoord coord) const
    {
        const auto connectivityIt = connectivity_.find(coord);
        return connectivityIt == connectivity_.end() ? nullptr : &connectivityIt->second;
    }

    const ChunkPortalDiagnostics* NavigationConnectivitySystem::portalDiagnostics(ChunkCoord coord) const
    {
        const auto diagnosticsIt = diagnostics_.find(coord);
        return diagnosticsIt == diagnostics_.end() ? nullptr : &diagnosticsIt->second;
    }

    const std::unordered_map<ChunkCoord, ChunkPortalDiagnostics, ChunkCoordHash>& NavigationConnectivitySystem::allPortalDiagnostics() const
    {
        return diagnostics_;
    }

    NavigationConnectivityCacheData NavigationConnectivitySystem::cacheData() const
    {
        NavigationConnectivityCacheData data;
        data.chunks.reserve(connectivity_.size());
        for (const auto& [_, connectivity] : connectivity_) {
            data.chunks.push_back(connectivity);
        }
        std::ranges::sort(data.chunks, [](const ChunkNavConnectivity& lhs, const ChunkNavConnectivity& rhs) {
            return lhs.coord.x == rhs.coord.x ? lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
        });
        return data;
    }

    void NavigationConnectivitySystem::loadCacheData(const NavigationConnectivityCacheData& cacheData)
    {
        connectivity_.clear();
        diagnostics_.clear();
        for (const ChunkNavConnectivity& connectivity : cacheData.chunks) {
            connectivity_[connectivity.coord] = connectivity;
            ChunkPortalDiagnostics diagnostics;
            diagnostics.coord = connectivity.coord;
            for (uint32_t edge = 0; edge < NavEdgeDirectionCount; ++edge) {
                diagnostics.edges[edge].acceptedPortalCount = static_cast<uint32_t>(connectivity.portalsByEdge[edge].size());
                for (const ChunkNavPortal& portal : connectivity.portalsByEdge[edge]) {
                    if (portal.connectedToLoadedNeighbor) {
                        ++diagnostics.edges[edge].connectedPortalCount;
                    }
                }
            }
            diagnostics_[connectivity.coord] = diagnostics;
        }
    }

    NavigationConnectivityStats NavigationConnectivitySystem::stats() const
    {
        NavigationConnectivityStats result;
        result.chunkCount = static_cast<uint32_t>(connectivity_.size());
        for (const auto& [coord, connectivity] : connectivity_) {
            (void)coord;
            uint32_t chunkPortals = 0;
            for (const std::vector<ChunkNavPortal>& portals : connectivity.portalsByEdge) {
                chunkPortals += static_cast<uint32_t>(portals.size());
                for (const ChunkNavPortal& portal : portals) {
                    if (portal.connectedToLoadedNeighbor) {
                        ++result.connectedPortals;
                    }
                }
            }
            result.totalPortals += chunkPortals;
            if (chunkPortals == 0) {
                ++result.blockedChunks;
            } else if (connectivity.partial) {
                ++result.partialChunks;
            }
        }
        return result;
    }

    std::optional<ChunkNavPortal> NavigationConnectivitySystem::buildPortalSample(
        ChunkCoord coord,
        NavEdgeDirection direction,
        float t,
        const Renderer::Aabb& bounds,
        const NavigationSystem& navigation,
        const TerrainSystem& terrain,
        const NavAgentSettings& agent,
        NavigationPortalEdgeDiagnostics& diagnostics) const
    {
        glm::vec3 sample{};
        switch (direction) {
            case NavEdgeDirection::North:
                sample = {std::lerp(bounds.min.x, bounds.max.x, t), 0.0f, bounds.max.z - settings_.edgeInset};
                break;
            case NavEdgeDirection::South:
                sample = {std::lerp(bounds.min.x, bounds.max.x, t), 0.0f, bounds.min.z + settings_.edgeInset};
                break;
            case NavEdgeDirection::East:
                sample = {bounds.max.x - settings_.edgeInset, 0.0f, std::lerp(bounds.min.z, bounds.max.z, t)};
                break;
            case NavEdgeDirection::West:
                sample = {bounds.min.x + settings_.edgeInset, 0.0f, std::lerp(bounds.min.z, bounds.max.z, t)};
                break;
            case NavEdgeDirection::Count:
                return std::nullopt;
        }
        sample.y = terrain.sampleHeight(sample.x, sample.z).value_or((bounds.min.y + bounds.max.y) * 0.5f);

        NavQueryResult nearest = navigation.nearestNavigablePoint(sample, agent);
        if (nearest.status != NavQueryStatus::Success) {
            ++diagnostics.rejectedNoNearestPolyCount;
            return std::nullopt;
        }
        if (!insideEdgeBand(nearest.point, direction, bounds, settings_.edgeBandWidth)) {
            ++diagnostics.rejectedEdgeBandCount;
            return std::nullopt;
        }

        const NavQueryResult path = navigation.findPath(chunkCenter(bounds, terrain), nearest.point, agent);
        if (path.status != NavQueryStatus::Success || path.path.points.empty()) {
            ++diagnostics.rejectedCenterReachabilityCount;
            return std::nullopt;
        }

        return ChunkNavPortal{
            direction,
            nearest.point,
            neighborFor(coord, direction),
            true,
            false,
        };
    }

    bool NavigationConnectivitySystem::shouldMergePortal(
        const std::vector<ChunkNavPortal>& portals,
        const glm::vec3& position) const
    {
        const float mergeDistanceSquared = settings_.portalMergeDistance * settings_.portalMergeDistance;
        return std::ranges::any_of(portals, [&](const ChunkNavPortal& portal) {
            return xzDistanceSquared(portal.position, position) <= mergeDistanceSquared;
        });
    }

    void NavigationConnectivitySystem::markLoadedNeighborConnections()
    {
        const float linkDistanceSquared = settings_.neighborLinkDistance * settings_.neighborLinkDistance;
        for (auto& [coord, connectivity] : connectivity_) {
            (void)coord;
            for (uint32_t directionIndex = 0; directionIndex < NavEdgeDirectionCount; ++directionIndex) {
                const NavEdgeDirection direction = static_cast<NavEdgeDirection>(directionIndex);
                std::vector<ChunkNavPortal>& portals = connectivity.portalsByEdge[directionIndex];
                for (ChunkNavPortal& portal : portals) {
                    const auto neighborIt = connectivity_.find(portal.neighborCoord);
                    if (neighborIt == connectivity_.end()) {
                        continue;
                    }

                    std::vector<ChunkNavPortal>& neighborPortals =
                        neighborIt->second.portalsByEdge[edgeIndex(opposite(direction))];
                    ChunkNavPortal* bestNeighbor = nullptr;
                    float bestDistanceSquared = linkDistanceSquared;
                    for (ChunkNavPortal& neighborPortal : neighborPortals) {
                        const float distanceSquared = xzDistanceSquared(portal.position, neighborPortal.position);
                        if (distanceSquared <= bestDistanceSquared) {
                            bestDistanceSquared = distanceSquared;
                            bestNeighbor = &neighborPortal;
                        }
                    }
                    if (!bestNeighbor) {
                        continue;
                    }

                    const bool portalWasConnected = portal.connectedToLoadedNeighbor;
                    portal.connectedToLoadedNeighbor = true;
                    portal.connectedNeighborPosition = bestNeighbor->position;
                    if (!portalWasConnected) {
                        if (const auto diagnosticsIt = diagnostics_.find(connectivity.coord); diagnosticsIt != diagnostics_.end()) {
                            ++diagnosticsIt->second.edges[directionIndex].connectedPortalCount;
                        }
                    }
                    const bool neighborWasConnected = bestNeighbor->connectedToLoadedNeighbor;
                    if (!bestNeighbor->connectedToLoadedNeighbor ||
                        xzDistanceSquared(bestNeighbor->position, portal.position) <
                            xzDistanceSquared(bestNeighbor->position, bestNeighbor->connectedNeighborPosition)) {
                        bestNeighbor->connectedToLoadedNeighbor = true;
                        bestNeighbor->connectedNeighborPosition = portal.position;
                    }
                    if (!neighborWasConnected && bestNeighbor->connectedToLoadedNeighbor) {
                        if (const auto diagnosticsIt = diagnostics_.find(neighborIt->second.coord); diagnosticsIt != diagnostics_.end()) {
                            ++diagnosticsIt->second.edges[edgeIndex(opposite(direction))].connectedPortalCount;
                        }
                    }
                }
            }
        }
    }
}
