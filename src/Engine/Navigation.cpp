#include "Engine/Navigation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>
#include <Recast.h>

namespace Engine {
    namespace {
        bool finiteVec3(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool validAabb(const Renderer::Aabb& bounds)
        {
            return finiteVec3(bounds.min) &&
                finiteVec3(bounds.max) &&
                bounds.min.x <= bounds.max.x &&
                bounds.min.y <= bounds.max.y &&
                bounds.min.z <= bounds.max.z;
        }

        bool validAgent(const NavAgentSettings& agent)
        {
            return std::isfinite(agent.radius) &&
                std::isfinite(agent.height) &&
                std::isfinite(agent.maxSlopeDegrees) &&
                std::isfinite(agent.maxClimb) &&
                agent.radius > 0.0f &&
                agent.height > 0.0f &&
                agent.maxSlopeDegrees >= 0.0f &&
                agent.maxClimb >= 0.0f;
        }

        bool validBuildData(const NavigationTerrainBuildData& buildData)
        {
            return !buildData.vertices.empty() &&
                buildData.indices.size() >= 3 &&
                buildData.indices.size() % 3 == 0 &&
                buildData.blockingIndices.size() % 3 == 0 &&
                validAabb(buildData.bounds) &&
                std::ranges::all_of(buildData.vertices, finiteVec3) &&
                std::ranges::all_of(buildData.blockingVertices, finiteVec3) &&
                std::ranges::all_of(buildData.indices, [&](uint32_t index) {
                    return index < buildData.vertices.size();
                }) &&
                std::ranges::all_of(buildData.blockingIndices, [&](uint32_t index) {
                    return index < buildData.blockingVertices.size();
                });
        }

        std::vector<glm::vec3> combinedVertices(const NavigationTerrainBuildData& buildData)
        {
            std::vector<glm::vec3> result;
            result.reserve(buildData.vertices.size() + buildData.blockingVertices.size());
            result.insert(result.end(), buildData.vertices.begin(), buildData.vertices.end());
            result.insert(result.end(), buildData.blockingVertices.begin(), buildData.blockingVertices.end());
            return result;
        }

        std::vector<uint32_t> combinedIndices(const NavigationTerrainBuildData& buildData)
        {
            std::vector<uint32_t> result;
            result.reserve(buildData.indices.size() + buildData.blockingIndices.size());
            result.insert(result.end(), buildData.indices.begin(), buildData.indices.end());
            const uint32_t blockerVertexOffset = static_cast<uint32_t>(buildData.vertices.size());
            for (uint32_t index : buildData.blockingIndices) {
                result.push_back(blockerVertexOffset + index);
            }
            return result;
        }

        NavQueryResult makeResult(NavQueryStatus status, std::string message, glm::vec3 point = {})
        {
            NavQueryResult result;
            result.status = status;
            result.point = point;
            result.message = std::move(message);
            return result;
        }

        std::vector<float> flattenVertices(const std::vector<glm::vec3>& vertices)
        {
            std::vector<float> result;
            result.reserve(vertices.size() * 3);
            for (const glm::vec3& vertex : vertices) {
                result.push_back(vertex.x);
                result.push_back(vertex.y);
                result.push_back(vertex.z);
            }
            return result;
        }

        std::vector<int> convertIndices(const std::vector<uint32_t>& indices)
        {
            std::vector<int> result;
            result.reserve(indices.size());
            for (uint32_t index : indices) {
                result.push_back(static_cast<int>(index));
            }
            return result;
        }

        int areaSize(uint32_t value)
        {
            return static_cast<int>(value * value);
        }

        const char* statusName(NavQueryStatus status)
        {
            switch (status) {
                case NavQueryStatus::Success:
                    return "success";
                case NavQueryStatus::NotInitialized:
                    return "not initialized";
                case NavQueryStatus::NoTile:
                    return "no tile";
                case NavQueryStatus::NoNearestPoly:
                    return "no nearest polygon";
                case NavQueryStatus::NoPath:
                    return "no path";
                case NavQueryStatus::InvalidInput:
                    return "invalid input";
                case NavQueryStatus::Unsupported:
                    return "unsupported";
            }
            return "unknown";
        }

        glm::vec3 detourVertex(const dtMeshTile* tile, uint16_t vertexIndex)
        {
            const float* vertex = &tile->verts[static_cast<size_t>(vertexIndex) * 3];
            return {vertex[0], vertex[1], vertex[2]};
        }

        void appendTileDebugGeometry(const dtMeshTile* tile, NavigationDebugGeometry& geometry)
        {
            if (!tile || !tile->header || !tile->verts || !tile->polys) {
                return;
            }

            for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
                const dtPoly& poly = tile->polys[polyIndex];
                if (poly.getType() != DT_POLYTYPE_GROUND || poly.vertCount < 2) {
                    continue;
                }

                for (uint8_t vertexIndex = 0; vertexIndex < poly.vertCount; ++vertexIndex) {
                    const uint8_t nextVertexIndex = static_cast<uint8_t>((vertexIndex + 1) % poly.vertCount);
                    geometry.polygonEdges.push_back({
                        detourVertex(tile, poly.verts[vertexIndex]),
                        detourVertex(tile, poly.verts[nextVertexIndex]),
                    });
                }
            }
        }
    }

    struct NavigationSystem::Impl {
        struct TileRecord {
            NavigationTileHandle handle;
            dtTileRef tileRef = 0;
            Renderer::Aabb bounds;
            std::vector<uint8_t> detourTileData;
            NavQueryStatus buildStatus = NavQueryStatus::Unsupported;
            std::string buildMessage;
            NavigationTileDiagnostics diagnostics;
        };

        dtNavMesh* navMesh = nullptr;
        dtNavMeshQuery* navQuery = nullptr;
        std::unordered_map<ChunkCoord, TileRecord, ChunkCoordHash> tiles;
        uint32_t nextTileId = 0;
        bool navMeshReady = false;
        float tileWidth = 0.0f;
        float tileHeight = 0.0f;
        NavQueryStatus lastBuildStatus = NavQueryStatus::Unsupported;
        std::string lastBuildMessage = "No navigation tile builds have run.";
        mutable NavQueryStatus lastQueryStatus = NavQueryStatus::Unsupported;
        mutable std::string lastQueryMessage = "No navigation queries have run.";

        Impl()
        {
            navMesh = dtAllocNavMesh();
            navQuery = dtAllocNavMeshQuery();
        }

        ~Impl()
        {
            if (navQuery) {
                dtFreeNavMeshQuery(navQuery);
                navQuery = nullptr;
            }
            if (navMesh) {
                dtFreeNavMesh(navMesh);
                navMesh = nullptr;
            }
        }

        bool initialized() const
        {
            return navMesh && navQuery;
        }

        bool initializeNavMeshForBounds(const Renderer::Aabb& bounds, const NavBuildSettings& settings)
        {
            if (navMeshReady) {
                return true;
            }

            tileWidth = std::max(bounds.max.x - bounds.min.x, 1.0f);
            tileHeight = std::max(bounds.max.z - bounds.min.z, 1.0f);

            dtNavMeshParams params{};
            params.orig[0] = 0.0f;
            params.orig[1] = 0.0f;
            params.orig[2] = 0.0f;
            params.tileWidth = tileWidth;
            params.tileHeight = tileHeight;
            params.maxTiles = static_cast<int>(std::max(settings.maxTiles, 1u));
            params.maxPolys = static_cast<int>(std::max(settings.maxPolysPerTile, 1u));

            if (dtStatusFailed(navMesh->init(&params))) {
                return false;
            }
            if (dtStatusFailed(navQuery->init(navMesh, static_cast<int>(std::max(settings.maxPolysPerTile * 4u, 2048u))))) {
                return false;
            }

            navMeshReady = true;
            return true;
        }

        bool initializeNavMesh(const NavigationTerrainBuildData& buildData, const NavBuildSettings& settings)
        {
            return initializeNavMeshForBounds(buildData.bounds, settings);
        }
    };

    NavigationSystem::NavigationSystem(NavBuildSettings settings)
        : settings_(settings),
          impl_(std::make_unique<Impl>())
    {
        settings_.cellSize = std::max(settings_.cellSize, 0.01f);
        settings_.cellHeight = std::max(settings_.cellHeight, 0.01f);
        settings_.maxTiles = std::max(settings_.maxTiles, 1u);
        settings_.maxPolysPerTile = std::max(settings_.maxPolysPerTile, 1u);
        settings_.maxVertsPerPoly = std::clamp(settings_.maxVertsPerPoly, 3u, 12u);
        settings_.regionMinSize = std::max(settings_.regionMinSize, 1u);
        settings_.regionMergeSize = std::max(settings_.regionMergeSize, settings_.regionMinSize);
        settings_.edgeMaxLen = std::max(settings_.edgeMaxLen, 0.0f);
        settings_.edgeMaxError = std::max(settings_.edgeMaxError, 0.0f);
        settings_.detailSampleDist = std::max(settings_.detailSampleDist, 0.0f);
        settings_.detailSampleMaxError = std::max(settings_.detailSampleMaxError, 0.0f);
    }

    NavigationSystem::~NavigationSystem() = default;

    NavigationSystem::NavigationSystem(NavigationSystem&&) noexcept = default;

    NavigationSystem& NavigationSystem::operator=(NavigationSystem&&) noexcept = default;

    NavigationTileHandle NavigationSystem::buildTerrainTile(
        const NavigationTerrainBuildData& buildData,
        const NavAgentSettings& agent)
    {
        if (!impl_ || !impl_->initialized()) {
            if (impl_) {
                impl_->lastBuildStatus = NavQueryStatus::NotInitialized;
                impl_->lastBuildMessage = "Navigation system is not initialized.";
            }
            return {};
        }
        if (!validAgent(agent) || !validBuildData(buildData)) {
            impl_->lastBuildStatus = NavQueryStatus::InvalidInput;
            impl_->lastBuildMessage = "Invalid terrain navigation build input.";
            return {};
        }

        NavigationTileBuildResult result = buildTerrainTileData(buildData, agent, settings_);
        impl_->lastBuildStatus = result.status;
        impl_->lastBuildMessage = result.message;
        if (result.status != NavQueryStatus::Success || !result.tileData) {
            return {};
        }

        return loadTerrainTileFromCache(*result.tileData, result.diagnostics);
    }

    NavigationTileBuildResult NavigationSystem::buildTerrainTileData(
        const NavigationTerrainBuildData& buildData,
        const NavAgentSettings& agent,
        const NavBuildSettings& settings)
    {
        NavigationTileBuildResult result;
        result.status = NavQueryStatus::Unsupported;
        result.message = "Navigation tile build started.";

        if (!validAgent(agent) || !validBuildData(buildData)) {
            result.status = NavQueryStatus::InvalidInput;
            result.message = "Invalid terrain navigation build input.";
            return result;
        }

        const std::vector<glm::vec3> buildVertices = combinedVertices(buildData);
        const std::vector<uint32_t> buildIndices = combinedIndices(buildData);
        const std::vector<float> verts = flattenVertices(buildVertices);
        const std::vector<int> tris = convertIndices(buildIndices);
        const int vertCount = static_cast<int>(buildVertices.size());
        const int triCount = static_cast<int>(buildIndices.size() / 3);
        const int terrainTriCount = static_cast<int>(buildData.indices.size() / 3);

        NavigationTileDiagnostics diagnostics;
        diagnostics.coord = buildData.coord;
        diagnostics.status = NavQueryStatus::Unsupported;
        diagnostics.message = "Navigation tile build started.";
        diagnostics.source = NavigationTileSource::LiveBuild;
        diagnostics.terrainVertexCount = static_cast<uint32_t>(buildData.vertices.size());
        diagnostics.terrainTriangleCount = static_cast<uint32_t>(buildData.indices.size() / 3);
        diagnostics.sourceResolution =
            static_cast<uint32_t>(std::round(std::sqrt(static_cast<float>(buildData.vertices.size()))));
        diagnostics.blockerVertexCount = static_cast<uint32_t>(buildData.blockingVertices.size());
        diagnostics.blockerTriangleCount = static_cast<uint32_t>(buildData.blockingIndices.size() / 3);
        diagnostics.bounds = buildData.bounds;
        diagnostics.agent = agent;
        diagnostics.build = settings;

        rcContext context;
        rcConfig cfg{};
        cfg.cs = std::max(settings.cellSize, 0.01f);
        cfg.ch = std::max(settings.cellHeight, 0.01f);
        cfg.walkableSlopeAngle = std::clamp(agent.maxSlopeDegrees + 0.5f, 0.0f, 89.0f);
        cfg.walkableHeight = static_cast<int>(std::ceil(agent.height / cfg.ch));
        cfg.walkableClimb = static_cast<int>(std::floor(agent.maxClimb / cfg.ch));
        cfg.walkableRadius = static_cast<int>(std::ceil(agent.radius / cfg.cs));
        cfg.maxEdgeLen = settings.edgeMaxLen <= 0.0f ? 0 : static_cast<int>(settings.edgeMaxLen / cfg.cs);
        cfg.maxSimplificationError = settings.edgeMaxError;
        cfg.minRegionArea = areaSize(settings.regionMinSize);
        cfg.mergeRegionArea = areaSize(settings.regionMergeSize);
        cfg.maxVertsPerPoly = static_cast<int>(std::clamp(settings.maxVertsPerPoly, 3u, 12u));
        cfg.detailSampleDist = settings.detailSampleDist < 0.9f ? 0.0f : cfg.cs * settings.detailSampleDist;
        cfg.detailSampleMaxError = cfg.ch * settings.detailSampleMaxError;
        cfg.borderSize = static_cast<int>(settings.tileBorderSize);
        cfg.bmin[0] = buildData.bounds.min.x;
        cfg.bmin[1] = buildData.bounds.min.y;
        cfg.bmin[2] = buildData.bounds.min.z;
        cfg.bmax[0] = buildData.bounds.max.x;
        cfg.bmax[1] = buildData.bounds.max.y;
        cfg.bmax[2] = buildData.bounds.max.z;
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
        diagnostics.heightfieldWidth = static_cast<uint32_t>(std::max(cfg.width, 0));
        diagnostics.heightfieldHeight = static_cast<uint32_t>(std::max(cfg.height, 0));

        auto failBuild = [&](std::string message) {
            diagnostics.status = NavQueryStatus::NoPath;
            diagnostics.message = message;
            result.status = NavQueryStatus::NoPath;
            result.message = std::move(message);
            result.diagnostics = diagnostics;
            return result;
        };

        rcHeightfield* heightfield = rcAllocHeightfield();
        if (!heightfield || !rcCreateHeightfield(&context, *heightfield, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
            if (heightfield) {
                rcFreeHeightField(heightfield);
            }
            return failBuild("Failed to create Recast heightfield.");
        }

        std::vector<unsigned char> triangleAreas(static_cast<size_t>(triCount), RC_NULL_AREA);
        if (terrainTriCount > 0) {
            rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, verts.data(), vertCount, tris.data(), terrainTriCount, triangleAreas.data());
            rcClearUnwalkableTriangles(&context, cfg.walkableSlopeAngle, verts.data(), vertCount, tris.data(), terrainTriCount, triangleAreas.data());
            diagnostics.walkableTerrainTriangleCount = static_cast<uint32_t>(std::count_if(
                triangleAreas.begin(),
                triangleAreas.begin() + terrainTriCount,
                [](unsigned char area) {
                    return area != RC_NULL_AREA;
                }));
        }
        if (!rcRasterizeTriangles(&context, verts.data(), vertCount, tris.data(), triangleAreas.data(), triCount, *heightfield, cfg.walkableClimb)) {
            rcFreeHeightField(heightfield);
            return failBuild("Failed to rasterize terrain triangles.");
        }

        rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *heightfield);
        rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *heightfield);

        rcCompactHeightfield* compactHeightfield = rcAllocCompactHeightfield();
        if (!compactHeightfield || !rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *heightfield, *compactHeightfield)) {
            rcFreeHeightField(heightfield);
            if (compactHeightfield) {
                rcFreeCompactHeightfield(compactHeightfield);
            }
            return failBuild("Failed to build compact heightfield.");
        }
        rcFreeHeightField(heightfield);
        diagnostics.compactSpanCount = static_cast<uint32_t>(std::max(compactHeightfield->spanCount, 0));

        if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compactHeightfield) ||
            !rcBuildDistanceField(&context, *compactHeightfield) ||
            !rcBuildRegions(&context, *compactHeightfield, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
            rcFreeCompactHeightfield(compactHeightfield);
            return failBuild("Failed to build Recast walkable regions.");
        }

        rcContourSet* contours = rcAllocContourSet();
        if (!contours || !rcBuildContours(&context, *compactHeightfield, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours)) {
            rcFreeCompactHeightfield(compactHeightfield);
            if (contours) {
                rcFreeContourSet(contours);
            }
            return failBuild("Failed to build Recast contours.");
        }
        diagnostics.contourCount = static_cast<uint32_t>(std::max(contours->nconts, 0));

        rcPolyMesh* polyMesh = rcAllocPolyMesh();
        if (!polyMesh || !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *polyMesh)) {
            rcFreeContourSet(contours);
            rcFreeCompactHeightfield(compactHeightfield);
            if (polyMesh) {
                rcFreePolyMesh(polyMesh);
            }
            return failBuild("Failed to build Recast polygon mesh.");
        }
        rcFreeContourSet(contours);
        diagnostics.navPolygonCount = static_cast<uint32_t>(std::max(polyMesh->npolys, 0));

        rcPolyMeshDetail* detailMesh = rcAllocPolyMeshDetail();
        if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polyMesh, *compactHeightfield, cfg.detailSampleDist, cfg.detailSampleMaxError, *detailMesh)) {
            rcFreePolyMesh(polyMesh);
            rcFreeCompactHeightfield(compactHeightfield);
            if (detailMesh) {
                rcFreePolyMeshDetail(detailMesh);
            }
            return failBuild("Failed to build Recast detail mesh.");
        }
        rcFreeCompactHeightfield(compactHeightfield);
        diagnostics.detailTriangleCount = static_cast<uint32_t>(std::max(detailMesh->ntris, 0));

        if (polyMesh->nverts == 0 || polyMesh->npolys == 0) {
            rcFreePolyMesh(polyMesh);
            rcFreePolyMeshDetail(detailMesh);
            return failBuild("Terrain tile produced no walkable navigation polygons.");
        }

        for (int index = 0; index < polyMesh->npolys; ++index) {
            if (polyMesh->areas[index] == RC_WALKABLE_AREA) {
                polyMesh->areas[index] = 0;
                polyMesh->flags[index] = 1;
            }
        }

        dtNavMeshCreateParams params{};
        params.verts = polyMesh->verts;
        params.vertCount = polyMesh->nverts;
        params.polys = polyMesh->polys;
        params.polyAreas = polyMesh->areas;
        params.polyFlags = polyMesh->flags;
        params.polyCount = polyMesh->npolys;
        params.nvp = polyMesh->nvp;
        params.detailMeshes = detailMesh->meshes;
        params.detailVerts = detailMesh->verts;
        params.detailVertsCount = detailMesh->nverts;
        params.detailTris = detailMesh->tris;
        params.detailTriCount = detailMesh->ntris;
        params.walkableHeight = agent.height;
        params.walkableRadius = agent.radius;
        params.walkableClimb = agent.maxClimb;
        std::memcpy(params.bmin, polyMesh->bmin, sizeof(params.bmin));
        std::memcpy(params.bmax, polyMesh->bmax, sizeof(params.bmax));
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;
        params.tileX = buildData.coord.x;
        params.tileY = buildData.coord.z;
        params.tileLayer = 0;

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
            rcFreePolyMesh(polyMesh);
            rcFreePolyMeshDetail(detailMesh);
            return failBuild("Failed to create Detour navmesh tile data.");
        }
        NavigationTileCacheData tileData;
        tileData.coord = buildData.coord;
        tileData.bounds = buildData.bounds;
        tileData.detourTileData.assign(
            navData,
            navData + static_cast<size_t>(std::max(navDataSize, 0)));
        dtFree(navData);
        rcFreePolyMesh(polyMesh);
        rcFreePolyMeshDetail(detailMesh);

        diagnostics.status = NavQueryStatus::Success;
        diagnostics.message = "Built terrain navigation tile.";
        result.status = NavQueryStatus::Success;
        result.message = "Built terrain navigation tile.";
        result.tileData = std::move(tileData);
        result.diagnostics = diagnostics;
        return result;
    }

    NavigationTileHandle NavigationSystem::loadTerrainTileFromCache(const NavigationTileCacheData& cacheData)
    {
        NavigationTileDiagnostics diagnostics;
        diagnostics.coord = cacheData.coord;
        diagnostics.status = NavQueryStatus::Success;
        diagnostics.message = "Loaded terrain navigation tile from cache.";
        diagnostics.source = NavigationTileSource::Cache;
        diagnostics.bounds = cacheData.bounds;
        diagnostics.build = settings_;
        return loadTerrainTileFromCache(cacheData, diagnostics);
    }

    NavigationTileHandle NavigationSystem::loadTerrainTileFromCache(
        const NavigationTileCacheData& cacheData,
        const NavigationTileDiagnostics& sourceDiagnostics)
    {
        if (!impl_ || !impl_->initialized()) {
            if (impl_) {
                impl_->lastBuildStatus = NavQueryStatus::NotInitialized;
                impl_->lastBuildMessage = "Navigation system is not initialized.";
            }
            return {};
        }
        if (cacheData.detourTileData.empty() ||
            cacheData.bounds.min.x > cacheData.bounds.max.x ||
            cacheData.bounds.min.y > cacheData.bounds.max.y ||
            cacheData.bounds.min.z > cacheData.bounds.max.z) {
            impl_->lastBuildStatus = NavQueryStatus::InvalidInput;
            impl_->lastBuildMessage = "Invalid navigation tile cache data.";
            return {};
        }
        if (!impl_->initializeNavMeshForBounds(cacheData.bounds, settings_)) {
            impl_->lastBuildStatus = NavQueryStatus::NotInitialized;
            impl_->lastBuildMessage = "Failed to initialize Detour navmesh for cached tile.";
            return {};
        }

        destroyTile(cacheData.coord);

        unsigned char* navData = static_cast<unsigned char*>(dtAlloc(cacheData.detourTileData.size(), DT_ALLOC_PERM));
        if (!navData) {
            impl_->lastBuildStatus = NavQueryStatus::NoPath;
            impl_->lastBuildMessage = "Failed to allocate Detour cached tile data.";
            return {};
        }
        std::memcpy(navData, cacheData.detourTileData.data(), cacheData.detourTileData.size());

        dtTileRef tileRef = 0;
        const dtStatus addStatus = impl_->navMesh->addTile(
            navData,
            static_cast<int>(cacheData.detourTileData.size()),
            DT_TILE_FREE_DATA,
            0,
            &tileRef);
        if (dtStatusFailed(addStatus)) {
            dtFree(navData);
            impl_->lastBuildStatus = NavQueryStatus::InvalidInput;
            impl_->lastBuildMessage = "Failed to add cached Detour navmesh tile.";
            return {};
        }

        const NavigationTileHandle handle{impl_->nextTileId++};
        NavigationTileDiagnostics diagnostics = sourceDiagnostics;
        diagnostics.coord = cacheData.coord;
        diagnostics.status = NavQueryStatus::Success;
        diagnostics.bounds = cacheData.bounds;
        if (diagnostics.message.empty()) {
            diagnostics.message = "Loaded terrain navigation tile.";
        }
        impl_->tiles[cacheData.coord] = Impl::TileRecord{
            handle,
            tileRef,
            cacheData.bounds,
            cacheData.detourTileData,
            NavQueryStatus::Success,
            diagnostics.message,
            diagnostics,
        };
        impl_->lastBuildStatus = NavQueryStatus::Success;
        impl_->lastBuildMessage = diagnostics.message;
        return handle;
    }

    std::optional<NavigationTileCacheData> NavigationSystem::tileCacheData(ChunkCoord coord) const
    {
        if (!impl_) {
            return std::nullopt;
        }
        const auto tileIt = impl_->tiles.find(coord);
        if (tileIt == impl_->tiles.end() || tileIt->second.detourTileData.empty()) {
            return std::nullopt;
        }
        return NavigationTileCacheData{
            coord,
            tileIt->second.bounds,
            tileIt->second.detourTileData,
        };
    }

    void NavigationSystem::destroyTile(ChunkCoord coord)
    {
        if (!impl_) {
            return;
        }
        const auto tileIt = impl_->tiles.find(coord);
        if (tileIt == impl_->tiles.end()) {
            return;
        }

        if (impl_->navMeshReady && tileIt->second.tileRef != 0) {
            unsigned char* removedData = nullptr;
            int removedDataSize = 0;
            impl_->navMesh->removeTile(tileIt->second.tileRef, &removedData, &removedDataSize);
            if (removedData) {
                dtFree(removedData);
            }
        }
        impl_->tiles.erase(tileIt);
    }

    void NavigationSystem::clear()
    {
        if (!impl_) {
            return;
        }
        std::vector<ChunkCoord> coords;
        coords.reserve(impl_->tiles.size());
        for (const auto& [coord, _] : impl_->tiles) {
            coords.push_back(coord);
        }
        for (ChunkCoord coord : coords) {
            destroyTile(coord);
        }
    }

    bool NavigationSystem::hasTile(ChunkCoord coord) const
    {
        return impl_ && impl_->tiles.contains(coord);
    }

    std::optional<Renderer::Aabb> NavigationSystem::tileBounds(ChunkCoord coord) const
    {
        if (!impl_) {
            return std::nullopt;
        }
        const auto tileIt = impl_->tiles.find(coord);
        return tileIt == impl_->tiles.end() ? std::nullopt : std::optional<Renderer::Aabb>{tileIt->second.bounds};
    }

    size_t NavigationSystem::tileCount() const
    {
        return impl_ ? impl_->tiles.size() : 0;
    }

    NavigationDebugGeometry NavigationSystem::debugGeometry() const
    {
        NavigationDebugGeometry geometry;
        if (!impl_ || !impl_->navMeshReady || !impl_->navMesh) {
            return geometry;
        }

        for (const auto& [_, tile] : impl_->tiles) {
            appendTileDebugGeometry(impl_->navMesh->getTileByRef(tile.tileRef), geometry);
        }
        return geometry;
    }

    NavigationDebugGeometry NavigationSystem::debugGeometry(ChunkCoord coord) const
    {
        NavigationDebugGeometry geometry;
        if (!impl_ || !impl_->navMeshReady || !impl_->navMesh) {
            return geometry;
        }

        const auto tileIt = impl_->tiles.find(coord);
        if (tileIt == impl_->tiles.end()) {
            return geometry;
        }

        appendTileDebugGeometry(impl_->navMesh->getTileByRef(tileIt->second.tileRef), geometry);
        return geometry;
    }

    std::optional<NavigationTileDiagnostics> NavigationSystem::tileDiagnostics(ChunkCoord coord) const
    {
        if (!impl_) {
            return std::nullopt;
        }
        const auto tileIt = impl_->tiles.find(coord);
        return tileIt == impl_->tiles.end()
            ? std::nullopt
            : std::optional<NavigationTileDiagnostics>{tileIt->second.diagnostics};
    }

    std::vector<NavigationTileDiagnostics> NavigationSystem::allTileDiagnostics() const
    {
        std::vector<NavigationTileDiagnostics> result;
        if (!impl_) {
            return result;
        }
        result.reserve(impl_->tiles.size());
        for (const auto& [_, tile] : impl_->tiles) {
            result.push_back(tile.diagnostics);
        }
        std::ranges::sort(result, [](const NavigationTileDiagnostics& lhs, const NavigationTileDiagnostics& rhs) {
            return lhs.coord.x == rhs.coord.x ? lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
        });
        return result;
    }

    NavQueryResult NavigationSystem::nearestNavigablePoint(glm::vec3 point, const NavAgentSettings& agent) const
    {
        if (!impl_ || !impl_->initialized()) {
            NavQueryResult result = makeResult(NavQueryStatus::NotInitialized, "Navigation system is not initialized.", point);
            if (impl_) {
                impl_->lastQueryStatus = result.status;
                impl_->lastQueryMessage = result.message;
            }
            return result;
        }
        if (!finiteVec3(point) || !validAgent(agent)) {
            NavQueryResult result = makeResult(NavQueryStatus::InvalidInput, "Invalid nearest-point query input.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        if (impl_->tiles.empty()) {
            NavQueryResult result = makeResult(NavQueryStatus::NoTile, "No navigation tiles are loaded.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        if (!impl_->navMeshReady) {
            NavQueryResult result = makeResult(NavQueryStatus::NotInitialized, "Detour navmesh is not initialized.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        dtQueryFilter filter;
        filter.setIncludeFlags(1);
        filter.setExcludeFlags(0);
        const std::array<float, 3> center{point.x, point.y, point.z};
        const std::array<float, 3> halfExtents{
            std::max(agent.radius * 2.0f, 0.5f),
            std::max(agent.height, 2.0f),
            std::max(agent.radius * 2.0f, 0.5f),
        };
        dtPolyRef nearestRef = 0;
        float nearestPoint[3]{};
        const dtStatus status = impl_->navQuery->findNearestPoly(center.data(), halfExtents.data(), &filter, &nearestRef, nearestPoint);
        if (dtStatusFailed(status) || nearestRef == 0) {
            NavQueryResult result = makeResult(NavQueryStatus::NoNearestPoly, "No nearest navigation polygon found.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        NavQueryResult result = makeResult(NavQueryStatus::Success, "Found nearest navigable point.", {nearestPoint[0], nearestPoint[1], nearestPoint[2]});
        impl_->lastQueryStatus = result.status;
        impl_->lastQueryMessage = result.message;
        return result;
    }

    NavQueryResult NavigationSystem::nearestNavigablePointInTile(
        ChunkCoord coord,
        glm::vec3 point,
        const NavAgentSettings& agent) const
    {
        if (!impl_ || !impl_->initialized()) {
            NavQueryResult result = makeResult(NavQueryStatus::NotInitialized, "Navigation system is not initialized.", point);
            if (impl_) {
                impl_->lastQueryStatus = result.status;
                impl_->lastQueryMessage = result.message;
            }
            return result;
        }
        if (!finiteVec3(point) || !validAgent(agent)) {
            NavQueryResult result = makeResult(NavQueryStatus::InvalidInput, "Invalid tile nearest-point query input.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        const auto tileIt = impl_->tiles.find(coord);
        if (tileIt == impl_->tiles.end()) {
            NavQueryResult result = makeResult(NavQueryStatus::NoTile, "Requested navigation tile is not loaded.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        if (!impl_->navMeshReady) {
            NavQueryResult result = makeResult(NavQueryStatus::NotInitialized, "Detour navmesh is not initialized.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        const dtMeshTile* tile = impl_->navMesh->getTileByRef(tileIt->second.tileRef);
        if (!tile || !tile->header || tile->header->polyCount <= 0) {
            NavQueryResult result = makeResult(NavQueryStatus::NoNearestPoly, "Requested navigation tile has no polygons.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        const std::array<float, 3> center{point.x, point.y, point.z};
        const dtPolyRef baseRef = impl_->navMesh->getPolyRefBase(tile);
        float bestDistanceSquared = std::numeric_limits<float>::max();
        glm::vec3 bestPoint{};
        bool found = false;
        for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
            const dtPoly& poly = tile->polys[polyIndex];
            if ((poly.flags & 1u) == 0u) {
                continue;
            }
            const dtPolyRef ref = baseRef | static_cast<dtPolyRef>(polyIndex);
            float closest[3]{};
            bool overPoly = false;
            if (dtStatusFailed(impl_->navQuery->closestPointOnPoly(ref, center.data(), closest, &overPoly))) {
                continue;
            }

            const float dx = closest[0] - point.x;
            const float dy = closest[1] - point.y;
            const float dz = closest[2] - point.z;
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestPoint = {closest[0], closest[1], closest[2]};
                found = true;
            }
        }

        if (!found) {
            NavQueryResult result = makeResult(NavQueryStatus::NoNearestPoly, "No nearest polygon found in requested navigation tile.", point);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        NavQueryResult result = makeResult(NavQueryStatus::Success, "Found nearest navigable point in requested tile.", bestPoint);
        impl_->lastQueryStatus = result.status;
        impl_->lastQueryMessage = result.message;
        return result;
    }

    NavQueryResult NavigationSystem::findPath(glm::vec3 start, glm::vec3 end, const NavAgentSettings& agent) const
    {
        if (!impl_ || !impl_->initialized()) {
            NavQueryResult result = makeResult(NavQueryStatus::NotInitialized, "Navigation system is not initialized.", start);
            if (impl_) {
                impl_->lastQueryStatus = result.status;
                impl_->lastQueryMessage = result.message;
            }
            return result;
        }
        if (!finiteVec3(start) || !finiteVec3(end) || !validAgent(agent)) {
            NavQueryResult result = makeResult(NavQueryStatus::InvalidInput, "Invalid path query input.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        if (impl_->tiles.empty()) {
            NavQueryResult result = makeResult(NavQueryStatus::NoTile, "No navigation tiles are loaded.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        if (!impl_->navMeshReady) {
            NavQueryResult result = makeResult(NavQueryStatus::NotInitialized, "Detour navmesh is not initialized.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        const NavQueryResult nearestStart = nearestNavigablePoint(start, agent);
        if (nearestStart.status != NavQueryStatus::Success) {
            NavQueryResult result = makeResult(NavQueryStatus::NoNearestPoly, "No nearest navigation polygon found for path start.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }
        const NavQueryResult nearestEnd = nearestNavigablePoint(end, agent);
        if (nearestEnd.status != NavQueryStatus::Success) {
            NavQueryResult result = makeResult(NavQueryStatus::NoNearestPoly, "No nearest navigation polygon found for path end.", end);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        dtQueryFilter filter;
        filter.setIncludeFlags(1);
        filter.setExcludeFlags(0);
        const std::array<float, 3> halfExtents{
            std::max(agent.radius * 2.0f, 0.5f),
            std::max(agent.height, 2.0f),
            std::max(agent.radius * 2.0f, 0.5f),
        };
        const std::array<float, 3> startPoint{start.x, start.y, start.z};
        const std::array<float, 3> endPoint{end.x, end.y, end.z};
        constexpr float TileContainmentTolerance = 0.25f;
        auto coordForPoint = [&](const glm::vec3& point) -> std::optional<ChunkCoord> {
            for (const auto& [coord, tile] : impl_->tiles) {
                if (point.x >= tile.bounds.min.x - TileContainmentTolerance &&
                    point.x <= tile.bounds.max.x + TileContainmentTolerance &&
                    point.z >= tile.bounds.min.z - TileContainmentTolerance &&
                    point.z <= tile.bounds.max.z + TileContainmentTolerance) {
                    return coord;
                }
            }
            return std::nullopt;
        };
        const std::optional<ChunkCoord> startCoord = coordForPoint(start);
        const std::optional<ChunkCoord> endCoord = coordForPoint(end);
        dtPolyRef startRef = 0;
        dtPolyRef endRef = 0;
        float nearestStartPoint[3]{};
        float nearestEndPoint[3]{};
        if (dtStatusFailed(impl_->navQuery->findNearestPoly(startPoint.data(), halfExtents.data(), &filter, &startRef, nearestStartPoint)) ||
            dtStatusFailed(impl_->navQuery->findNearestPoly(endPoint.data(), halfExtents.data(), &filter, &endRef, nearestEndPoint)) ||
            startRef == 0 ||
            endRef == 0) {
            NavQueryResult result = makeResult(NavQueryStatus::NoNearestPoly, "No nearest navigation polygon found for path query.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        constexpr int MaxPathPolys = 256;
        constexpr int MaxStraightPath = 256;
        dtPolyRef pathPolys[MaxPathPolys]{};
        int pathPolyCount = 0;
        const dtStatus pathStatus = impl_->navQuery->findPath(
            startRef,
            endRef,
            nearestStartPoint,
            nearestEndPoint,
            &filter,
            pathPolys,
            &pathPolyCount,
            MaxPathPolys
        );
        if (dtStatusFailed(pathStatus) || pathPolyCount <= 0) {
            NavQueryResult result = makeResult(NavQueryStatus::NoPath, "No navigation path found.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        float straightPath[MaxStraightPath * 3]{};
        unsigned char straightPathFlags[MaxStraightPath]{};
        dtPolyRef straightPathRefs[MaxStraightPath]{};
        int straightPathCount = 0;
        const dtStatus straightStatus = impl_->navQuery->findStraightPath(
            nearestStartPoint,
            nearestEndPoint,
            pathPolys,
            pathPolyCount,
            straightPath,
            straightPathFlags,
            straightPathRefs,
            &straightPathCount,
            MaxStraightPath
        );
        if (dtStatusFailed(straightStatus) || straightPathCount <= 0) {
            NavQueryResult result = makeResult(NavQueryStatus::NoPath, "No straight navigation path found.", start);
            impl_->lastQueryStatus = result.status;
            impl_->lastQueryMessage = result.message;
            return result;
        }

        NavQueryResult result = makeResult(NavQueryStatus::Success, "Found navigation path.", {straightPath[0], straightPath[1], straightPath[2]});
        result.path.points.reserve(static_cast<size_t>(straightPathCount));
        for (int index = 0; index < straightPathCount; ++index) {
            result.path.points.push_back({
                straightPath[index * 3 + 0],
                straightPath[index * 3 + 1],
                straightPath[index * 3 + 2],
            });
        }
        result.path.complete = pathPolys[pathPolyCount - 1] == endRef;
        if (result.path.complete && startCoord && endCoord && *startCoord == *endCoord) {
            const auto startTileIt = impl_->tiles.find(*startCoord);
            const dtMeshTile* expectedTile = startTileIt == impl_->tiles.end()
                ? nullptr
                : impl_->navMesh->getTileByRef(startTileIt->second.tileRef);
            for (int index = 0; expectedTile && index < pathPolyCount; ++index) {
                const dtMeshTile* pathTile = nullptr;
                const dtPoly* pathPoly = nullptr;
                if (dtStatusFailed(impl_->navMesh->getTileAndPolyByRef(pathPolys[index], &pathTile, &pathPoly)) ||
                    pathTile != expectedTile) {
                    result.path.complete = false;
                    result.message = "Same-tile path requires leaving the tile.";
                    break;
                }
            }
        }
        if (!result.path.complete) {
            constexpr float SeamInset = 0.75f;
            constexpr float MaxSeamSnapDistance = 2.5f;
            if (startCoord && endCoord) {
                const int32_t deltaX = endCoord->x - startCoord->x;
                const int32_t deltaZ = endCoord->z - startCoord->z;
                const bool adjacent = std::abs(deltaX) + std::abs(deltaZ) == 1;
                const auto startTileIt = impl_->tiles.find(*startCoord);
                const auto endTileIt = impl_->tiles.find(*endCoord);
                if (adjacent && startTileIt != impl_->tiles.end() && endTileIt != impl_->tiles.end()) {
                    glm::vec3 sourceSeam = end;
                    glm::vec3 targetSeam = end;
                    if (deltaX > 0) {
                        sourceSeam = {startTileIt->second.bounds.max.x - SeamInset, end.y, end.z};
                        targetSeam = {endTileIt->second.bounds.min.x + SeamInset, end.y, end.z};
                    } else if (deltaX < 0) {
                        sourceSeam = {startTileIt->second.bounds.min.x + SeamInset, end.y, end.z};
                        targetSeam = {endTileIt->second.bounds.max.x - SeamInset, end.y, end.z};
                    } else if (deltaZ > 0) {
                        sourceSeam = {end.x, end.y, startTileIt->second.bounds.max.z - SeamInset};
                        targetSeam = {end.x, end.y, endTileIt->second.bounds.min.z + SeamInset};
                    } else if (deltaZ < 0) {
                        sourceSeam = {end.x, end.y, startTileIt->second.bounds.min.z + SeamInset};
                        targetSeam = {end.x, end.y, endTileIt->second.bounds.max.z - SeamInset};
                    }

                    const NavQueryResult sourceSnap = nearestNavigablePointInTile(*startCoord, sourceSeam, agent);
                    const NavQueryResult targetSnap = nearestNavigablePointInTile(*endCoord, targetSeam, agent);
                    const NavQueryResult endSnap = nearestNavigablePointInTile(*endCoord, end, agent);
                    auto closeEnough = [](const glm::vec3& a, const glm::vec3& b, float maxDistance) {
                        const float dx = a.x - b.x;
                        const float dz = a.z - b.z;
                        return dx * dx + dz * dz <= maxDistance * maxDistance;
                    };
                    const bool climbAllowed =
                        std::abs(targetSnap.point.y - sourceSnap.point.y) <= agent.maxClimb + settings_.cellHeight;
                    if (sourceSnap.status == NavQueryStatus::Success &&
                        targetSnap.status == NavQueryStatus::Success &&
                        endSnap.status == NavQueryStatus::Success &&
                        closeEnough(sourceSnap.point, sourceSeam, MaxSeamSnapDistance) &&
                        closeEnough(targetSnap.point, targetSeam, MaxSeamSnapDistance) &&
                        closeEnough(endSnap.point, end, MaxSeamSnapDistance) &&
                        climbAllowed) {
                        result.path.points.clear();
                        result.path.points.push_back(nearestStart.point);
                        result.path.points.push_back(sourceSnap.point);
                        result.path.points.push_back(targetSnap.point);
                        result.path.points.push_back(endSnap.point);
                        result.path.complete = true;
                        result.point = nearestStart.point;
                        result.message = "Found navigation path using adjacent tile seam bridge.";
                    }
                }
            }
        }
        impl_->lastQueryStatus = result.status;
        impl_->lastQueryMessage = result.message;
        return result;
    }

    bool NavigationSystem::isNavigable(glm::vec3 point, const NavAgentSettings& agent) const
    {
        return nearestNavigablePoint(point, agent).status == NavQueryStatus::Success;
    }

    const NavBuildSettings& NavigationSystem::settings() const
    {
        return settings_;
    }

    NavQueryStatus NavigationSystem::lastBuildStatus() const
    {
        return impl_ ? impl_->lastBuildStatus : NavQueryStatus::NotInitialized;
    }

    const std::string& NavigationSystem::lastBuildMessage() const
    {
        static const std::string notInitialized = "Navigation system is not initialized.";
        return impl_ ? impl_->lastBuildMessage : notInitialized;
    }

    NavQueryStatus NavigationSystem::lastQueryStatus() const
    {
        return impl_ ? impl_->lastQueryStatus : NavQueryStatus::NotInitialized;
    }

    const std::string& NavigationSystem::lastQueryMessage() const
    {
        static const std::string notInitialized = "Navigation system is not initialized.";
        return impl_ ? impl_->lastQueryMessage : notInitialized;
    }
}
