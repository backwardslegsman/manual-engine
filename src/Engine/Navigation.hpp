#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct NavigationTileHandle {
        uint32_t id = UINT32_MAX;
    };

    struct NavAgentSettings {
        float radius = 0.45f;
        float height = 1.8f;
        float maxSlopeDegrees = 45.0f;
        float maxClimb = 0.45f;
    };

    struct NavBuildSettings {
        float cellSize = 0.3f;
        float cellHeight = 0.2f;
        uint32_t tileBorderSize = 0;
        uint32_t maxTiles = 256;
        uint32_t maxPolysPerTile = 4096;
        uint32_t maxVertsPerPoly = 6;
        uint32_t regionMinSize = 8;
        uint32_t regionMergeSize = 20;
        float edgeMaxLen = 12.0f;
        float edgeMaxError = 1.3f;
        float detailSampleDist = 6.0f;
        float detailSampleMaxError = 1.0f;
    };

    struct NavPath {
        std::vector<glm::vec3> points;
        bool complete = false;
    };

    enum class NavQueryStatus {
        Success,
        NotInitialized,
        NoTile,
        NoNearestPoly,
        NoPath,
        InvalidInput,
        Unsupported,
    };

    struct NavQueryResult {
        NavQueryStatus status = NavQueryStatus::Unsupported;
        NavPath path;
        glm::vec3 point{};
        std::string message;
    };

    struct NavigationTerrainBuildData {
        ChunkCoord coord;
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;
        // Optional conservative static blocker geometry for the same chunk.
        // Terrain triangles are slope-tested for walkability; blocker triangles
        // are rasterized as unwalkable during Recast tile construction.
        std::vector<glm::vec3> blockingVertices;
        std::vector<uint32_t> blockingIndices;
        Renderer::Aabb bounds;
        std::optional<Renderer::Aabb> rasterizationBounds;
    };

    struct NavigationTileCacheData {
        ChunkCoord coord;
        Renderer::Aabb bounds;
        std::vector<uint8_t> detourTileData;
    };

    struct NavDebugLine {
        glm::vec3 a{};
        glm::vec3 b{};
    };

    struct NavigationDebugGeometry {
        std::vector<NavDebugLine> polygonEdges;
    };

    enum class NavigationTileSource {
        Unknown,
        LiveBuild,
        Cache,
    };

    struct NavigationTileDiagnostics {
        ChunkCoord coord;
        NavQueryStatus status = NavQueryStatus::Unsupported;
        std::string message;
        NavigationTileSource source = NavigationTileSource::Unknown;
        uint32_t terrainVertexCount = 0;
        uint32_t terrainTriangleCount = 0;
        uint32_t sourceResolution = 0;
        uint32_t blockerVertexCount = 0;
        uint32_t blockerTriangleCount = 0;
        uint32_t walkableTerrainTriangleCount = 0;
        uint32_t heightfieldWidth = 0;
        uint32_t heightfieldHeight = 0;
        uint32_t compactSpanCount = 0;
        uint32_t contourCount = 0;
        uint32_t navPolygonCount = 0;
        uint32_t detailTriangleCount = 0;
        Renderer::Aabb bounds;
        NavAgentSettings agent;
        NavBuildSettings build;
    };

    struct NavigationTileBuildResult {
        NavQueryStatus status = NavQueryStatus::Unsupported;
        std::string message;
        std::optional<NavigationTileCacheData> tileData;
        NavigationTileDiagnostics diagnostics;
    };

    class NavigationSystem {
    public:
        explicit NavigationSystem(NavBuildSettings settings = {});
        ~NavigationSystem();

        NavigationSystem(const NavigationSystem&) = delete;
        NavigationSystem& operator=(const NavigationSystem&) = delete;
        NavigationSystem(NavigationSystem&&) noexcept;
        NavigationSystem& operator=(NavigationSystem&&) noexcept;

        NavigationTileHandle buildTerrainTile(
            const NavigationTerrainBuildData& buildData,
            const NavAgentSettings& agent);
        NavigationTileHandle loadTerrainTileFromCache(const NavigationTileCacheData& cacheData);
        NavigationTileHandle loadTerrainTileFromCache(
            const NavigationTileCacheData& cacheData,
            const NavigationTileDiagnostics& diagnostics);
        static NavigationTileBuildResult buildTerrainTileData(
            const NavigationTerrainBuildData& buildData,
            const NavAgentSettings& agent,
            const NavBuildSettings& settings);
        std::optional<NavigationTileCacheData> tileCacheData(ChunkCoord coord) const;
        // Tile lifetime mirrors loaded engine chunks, but NavigationSystem does
        // not own chunk streaming or world object lifetime.
        void destroyTile(ChunkCoord coord);
        void clear();
        bool hasTile(ChunkCoord coord) const;
        std::optional<Renderer::Aabb> tileBounds(ChunkCoord coord) const;
        size_t tileCount() const;
        NavigationDebugGeometry debugGeometry() const;
        NavigationDebugGeometry debugGeometry(ChunkCoord coord) const;
        std::optional<NavigationTileDiagnostics> tileDiagnostics(ChunkCoord coord) const;
        std::vector<NavigationTileDiagnostics> allTileDiagnostics() const;

        // Queries operate only on currently loaded nav tiles. Callers should
        // treat NoTile/NoNearestPoly/NoPath as normal gameplay outcomes.
        NavQueryResult nearestNavigablePoint(glm::vec3 point, const NavAgentSettings& agent) const;
        NavQueryResult nearestNavigablePointInTile(ChunkCoord coord, glm::vec3 point, const NavAgentSettings& agent) const;
        NavQueryResult findPath(glm::vec3 start, glm::vec3 end, const NavAgentSettings& agent) const;
        bool isNavigable(glm::vec3 point, const NavAgentSettings& agent) const;
        NavQueryStatus lastBuildStatus() const;
        const std::string& lastBuildMessage() const;
        NavQueryStatus lastQueryStatus() const;
        const std::string& lastQueryMessage() const;

        const NavBuildSettings& settings() const;

    private:
        struct Impl;

        NavBuildSettings settings_;
        std::unique_ptr<Impl> impl_;
    };
}
