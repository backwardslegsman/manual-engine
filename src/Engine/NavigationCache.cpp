#include "Engine/NavigationCache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace Engine {
    namespace {
        constexpr std::array<char, 4> TileMagic{'M', 'N', 'T', '1'};

        uint64_t fnv1a(const std::string& text)
        {
            uint64_t hash = 14695981039346656037ull;
            for (unsigned char value : text) {
                hash ^= value;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string hexHash(uint64_t hash)
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << hash;
            return stream.str();
        }

        void appendHashInput(std::ostringstream& stream, const NavBuildSettings& settings)
        {
            stream << settings.cellSize << '|'
                   << settings.cellHeight << '|'
                   << settings.tileBorderSize << '|'
                   << settings.maxTiles << '|'
                   << settings.maxPolysPerTile << '|'
                   << settings.maxVertsPerPoly << '|'
                   << settings.regionMinSize << '|'
                   << settings.regionMergeSize << '|'
                   << settings.edgeMaxLen << '|'
                   << settings.edgeMaxError << '|'
                   << settings.detailSampleDist << '|'
                   << settings.detailSampleMaxError << '|';
        }

        void appendHashInput(std::ostringstream& stream, const NavAgentSettings& settings)
        {
            stream << settings.radius << '|'
                   << settings.height << '|'
                   << settings.maxSlopeDegrees << '|'
                   << settings.maxClimb << '|';
        }

        YAML::Node chunkNode(ChunkCoord coord)
        {
            YAML::Node node;
            node["x"] = coord.x;
            node["z"] = coord.z;
            return node;
        }

        ChunkCoord readChunk(const YAML::Node& node)
        {
            return {node["x"].as<int32_t>(0), node["z"].as<int32_t>(0)};
        }

        YAML::Node vec3Node(const glm::vec3& value)
        {
            YAML::Node node;
            node.push_back(value.x);
            node.push_back(value.y);
            node.push_back(value.z);
            return node;
        }

        glm::vec3 readVec3(const YAML::Node& node)
        {
            if (!node || !node.IsSequence() || node.size() != 3) {
                return {};
            }
            return {node[0].as<float>(0.0f), node[1].as<float>(0.0f), node[2].as<float>(0.0f)};
        }

        YAML::Node aabbNode(const Renderer::Aabb& bounds)
        {
            YAML::Node node;
            node["min"] = vec3Node(bounds.min);
            node["max"] = vec3Node(bounds.max);
            return node;
        }

        Renderer::Aabb readAabb(const YAML::Node& node)
        {
            return {readVec3(node["min"]), readVec3(node["max"])};
        }

        const char* directionName(NavEdgeDirection direction)
        {
            switch (direction) {
                case NavEdgeDirection::North:
                    return "north";
                case NavEdgeDirection::South:
                    return "south";
                case NavEdgeDirection::East:
                    return "east";
                case NavEdgeDirection::West:
                    return "west";
                case NavEdgeDirection::Count:
                    break;
            }
            return "north";
        }

        NavEdgeDirection readDirection(const YAML::Node& node)
        {
            const std::string value = node.as<std::string>("north");
            if (value == "south") {
                return NavEdgeDirection::South;
            }
            if (value == "east") {
                return NavEdgeDirection::East;
            }
            if (value == "west") {
                return NavEdgeDirection::West;
            }
            return NavEdgeDirection::North;
        }

        YAML::Node portalNode(const ChunkNavPortal& portal)
        {
            YAML::Node node;
            node["direction"] = directionName(portal.direction);
            node["position"] = vec3Node(portal.position);
            node["neighbor"] = chunkNode(portal.neighborCoord);
            node["reachable_from_center"] = portal.reachableFromChunkCenter;
            node["connected_to_loaded_neighbor"] = portal.connectedToLoadedNeighbor;
            node["connected_neighbor_position"] = vec3Node(portal.connectedNeighborPosition);
            return node;
        }

        ChunkNavPortal readPortal(const YAML::Node& node)
        {
            ChunkNavPortal portal;
            portal.direction = readDirection(node["direction"]);
            portal.position = readVec3(node["position"]);
            portal.neighborCoord = readChunk(node["neighbor"]);
            portal.reachableFromChunkCenter = node["reachable_from_center"].as<bool>(false);
            portal.connectedToLoadedNeighbor = node["connected_to_loaded_neighbor"].as<bool>(false);
            portal.connectedNeighborPosition = readVec3(node["connected_neighbor_position"]);
            return portal;
        }

        YAML::Node connectivityNode(const ChunkNavConnectivity& connectivity)
        {
            YAML::Node node;
            node["coord"] = chunkNode(connectivity.coord);
            node["biome"] = connectivity.biomeId;
            node["traversal_cost"] = connectivity.traversalCost;
            node["partial"] = connectivity.partial;
            for (uint32_t index = 0; index < NavEdgeDirectionCount; ++index) {
                YAML::Node portals;
                for (const ChunkNavPortal& portal : connectivity.portalsByEdge[index]) {
                    portals.push_back(portalNode(portal));
                }
                node["edges"][directionName(static_cast<NavEdgeDirection>(index))] = portals;
            }
            return node;
        }

        ChunkNavConnectivity readConnectivity(const YAML::Node& node)
        {
            ChunkNavConnectivity connectivity;
            connectivity.coord = readChunk(node["coord"]);
            connectivity.biomeId = node["biome"].as<std::string>(std::string{});
            connectivity.traversalCost = node["traversal_cost"].as<float>(0.0f);
            connectivity.partial = node["partial"].as<bool>(false);
            for (uint32_t index = 0; index < NavEdgeDirectionCount; ++index) {
                const NavEdgeDirection direction = static_cast<NavEdgeDirection>(index);
                const YAML::Node portals = node["edges"][directionName(direction)];
                if (!portals || !portals.IsSequence()) {
                    continue;
                }
                for (const YAML::Node& portal : portals) {
                    connectivity.portalsByEdge[index].push_back(readPortal(portal));
                }
            }
            return connectivity;
        }

        YAML::Node graphNode(const WorldNavigationGraphCacheData& graph)
        {
            YAML::Node node;
            node["center"] = chunkNode(graph.centerChunk);
            node["has_graph"] = graph.hasGraph;
            for (const WorldNavNode& graphNode : graph.nodes) {
                YAML::Node item;
                item["coord"] = chunkNode(graphNode.coord);
                item["biome"] = graphNode.biomeId;
                item["position"] = vec3Node(graphNode.position);
                item["traversal_cost"] = graphNode.traversalCost;
                node["nodes"].push_back(item);
            }
            for (const WorldNavEdge& edge : graph.edges) {
                YAML::Node item;
                item["from"] = chunkNode(edge.from);
                item["to"] = chunkNode(edge.to);
                item["direction"] = directionName(edge.direction);
                item["cost"] = edge.cost;
                item["blocked"] = edge.blocked;
                item["waypoint"] = vec3Node(edge.waypoint);
                item["ingress_waypoint"] = vec3Node(edge.ingressWaypoint);
                node["edges"].push_back(item);
            }
            return node;
        }

        WorldNavigationGraphCacheData readGraph(const YAML::Node& node)
        {
            WorldNavigationGraphCacheData graph;
            graph.centerChunk = readChunk(node["center"]);
            graph.hasGraph = node["has_graph"].as<bool>(false);
            if (const YAML::Node nodes = node["nodes"]; nodes && nodes.IsSequence()) {
                for (const YAML::Node& item : nodes) {
                    graph.nodes.push_back({
                        readChunk(item["coord"]),
                        item["biome"].as<std::string>(std::string{}),
                        readVec3(item["position"]),
                        item["traversal_cost"].as<float>(1.0f),
                    });
                }
            }
            if (const YAML::Node edges = node["edges"]; edges && edges.IsSequence()) {
                for (const YAML::Node& item : edges) {
                    graph.edges.push_back({
                        readChunk(item["from"]),
                        readChunk(item["to"]),
                        readDirection(item["direction"]),
                        item["cost"].as<float>(1.0f),
                        item["blocked"].as<bool>(false),
                        readVec3(item["waypoint"]),
                        item["ingress_waypoint"] ? readVec3(item["ingress_waypoint"]) : readVec3(item["waypoint"]),
                    });
                }
            }
            return graph;
        }

        YAML::Node manifestNode(const NavigationCacheManifest& manifest)
        {
            YAML::Node node;
            node["world_id"] = manifest.worldId;
            node["format_version"] = manifest.formatVersion;
            node["chunk_size"] = manifest.chunkSize;
            node["graph_radius_chunks"] = manifest.graphRadiusChunks;
            node["navigation_resolution"] = manifest.navigationResolution;
            node["biome_config_hash"] = manifest.biomeConfigHash;
            node["archetype_config_hash"] = manifest.archetypeConfigHash;
            node["terrain_source_id"] = manifest.terrainSourceId.value;
            node["terrain_source_hash"] = manifest.terrainSourceHash;
            node["terrain_source_type"] = manifest.terrainSourceType;
            node["terrain_navigation_adapter_version"] = manifest.terrainNavigationAdapterVersion;
            node["scene_geometry_hash"] = manifest.sceneGeometryHash;
            node["scene_geometry_max_slope_degrees"] = manifest.sceneGeometryMaxSlopeDegrees;
            node["scene_geometry_tile_bounds_padding"] = manifest.sceneGeometryTileBoundsPadding;
            node["scene_geometry_adapter_version"] = manifest.sceneGeometryAdapterVersion;
            node["terrain_import_settings"]["pipeline"] = manifest.terrainImportSettings.pipeline;
            node["terrain_import_settings"]["version"] = manifest.terrainImportSettings.version;
            node["terrain_import_settings"]["options_hash"] = manifest.terrainImportSettings.optionsHash;
            node["generator_version"] = manifest.generatorVersion;
            node["identity_hash"] = manifest.identityHash;
            node["profile_id"] = manifest.profileId;
            node["build"]["cell_size"] = manifest.build.cellSize;
            node["build"]["cell_height"] = manifest.build.cellHeight;
            node["build"]["tile_border_size"] = manifest.build.tileBorderSize;
            node["build"]["max_tiles"] = manifest.build.maxTiles;
            node["build"]["max_polys_per_tile"] = manifest.build.maxPolysPerTile;
            node["build"]["max_verts_per_poly"] = manifest.build.maxVertsPerPoly;
            node["build"]["region_min_size"] = manifest.build.regionMinSize;
            node["build"]["region_merge_size"] = manifest.build.regionMergeSize;
            node["build"]["edge_max_len"] = manifest.build.edgeMaxLen;
            node["build"]["edge_max_error"] = manifest.build.edgeMaxError;
            node["build"]["detail_sample_dist"] = manifest.build.detailSampleDist;
            node["build"]["detail_sample_max_error"] = manifest.build.detailSampleMaxError;
            node["agent"]["radius"] = manifest.agent.radius;
            node["agent"]["height"] = manifest.agent.height;
            node["agent"]["max_slope_degrees"] = manifest.agent.maxSlopeDegrees;
            node["agent"]["max_climb"] = manifest.agent.maxClimb;
            return node;
        }

        std::string chunkFileName(ChunkCoord coord, std::string_view extension)
        {
            return std::to_string(coord.x) + "_" + std::to_string(coord.z) + std::string{extension};
        }

        std::filesystem::path navigationCacheRoot(const NavigationCacheSettings& settings, const NavigationCacheManifest& manifest)
        {
            return settings.rootPath / manifest.identityHash;
        }

        std::filesystem::path navigationTilePath(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            ChunkCoord coord)
        {
            return navigationCacheRoot(settings, manifest) / "tiles" / chunkFileName(coord, ".navtile");
        }

        std::filesystem::path navigationConnectivityPath(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            ChunkCoord coord)
        {
            return navigationCacheRoot(settings, manifest) / "connectivity" / chunkFileName(coord, ".yaml");
        }

        std::filesystem::path navigationGraphPath(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            ChunkCoord centerChunk)
        {
            return navigationCacheRoot(settings, manifest) / "graphs" / chunkFileName(centerChunk, ".yaml");
        }
    }

    NavigationCache::NavigationCache(NavigationCacheSettings settings, NavigationCacheManifest manifest)
        : settings_(std::move(settings)),
          manifest_(std::move(manifest))
    {
    }

    NavigationCacheManifest NavigationCache::buildManifest(
        const NavigationCacheSettings& settings,
        float chunkSize,
        int32_t graphRadiusChunks,
        uint32_t navigationResolution,
        const NavBuildSettings& build,
        const NavAgentSettings& agent,
        std::string profileId,
        const std::filesystem::path& biomeConfigPath,
        const std::filesystem::path& archetypeConfigPath,
        AssetId terrainSourceId,
        std::string terrainSourceHash,
        AssetImportSettingsKey terrainImportSettings,
        std::string terrainSourceType,
        std::string terrainNavigationAdapterVersion,
        std::string sceneGeometryHash,
        float sceneGeometryMaxSlopeDegrees,
        float sceneGeometryTileBoundsPadding,
        std::string sceneGeometryAdapterVersion)
    {
        NavigationCacheManifest manifest;
        manifest.worldId = settings.worldId;
        manifest.formatVersion = settings.formatVersion;
        manifest.chunkSize = chunkSize;
        manifest.graphRadiusChunks = graphRadiusChunks;
        manifest.navigationResolution = std::max(navigationResolution, 2u);
        manifest.build = build;
        manifest.agent = agent;
        manifest.profileId = std::move(profileId);
        manifest.biomeConfigHash = hashFile(biomeConfigPath);
        manifest.archetypeConfigHash = hashFile(archetypeConfigPath);
        manifest.terrainSourceId = terrainSourceId;
        manifest.terrainSourceHash = std::move(terrainSourceHash);
        manifest.terrainImportSettings = std::move(terrainImportSettings);
        manifest.terrainSourceType = std::move(terrainSourceType);
        manifest.terrainNavigationAdapterVersion = std::move(terrainNavigationAdapterVersion);
        manifest.sceneGeometryHash = std::move(sceneGeometryHash);
        manifest.sceneGeometryMaxSlopeDegrees = std::isfinite(sceneGeometryMaxSlopeDegrees)
            ? std::clamp(sceneGeometryMaxSlopeDegrees, 0.0f, 90.0f)
            : 45.0f;
        manifest.sceneGeometryTileBoundsPadding = std::isfinite(sceneGeometryTileBoundsPadding)
            ? std::max(sceneGeometryTileBoundsPadding, 0.0f)
            : 0.45f;
        manifest.sceneGeometryAdapterVersion = std::move(sceneGeometryAdapterVersion);

        std::ostringstream identity;
        identity << manifest.worldId << '|'
                 << manifest.formatVersion << '|'
                 << manifest.chunkSize << '|'
                 << manifest.graphRadiusChunks << '|'
                 << manifest.navigationResolution << '|'
                 << manifest.biomeConfigHash << '|'
                 << manifest.archetypeConfigHash << '|'
                 << manifest.profileId << '|'
                 << manifest.terrainSourceId.value << '|'
                 << manifest.terrainSourceHash << '|'
                 << manifest.terrainImportSettings.pipeline << '|'
                 << manifest.terrainImportSettings.version << '|'
                 << manifest.terrainImportSettings.optionsHash << '|'
                 << manifest.terrainSourceType << '|'
                 << manifest.terrainNavigationAdapterVersion << '|'
                 << manifest.sceneGeometryHash << '|'
                 << manifest.sceneGeometryMaxSlopeDegrees << '|'
                 << manifest.sceneGeometryTileBoundsPadding << '|'
                 << manifest.sceneGeometryAdapterVersion << '|'
                 << manifest.generatorVersion << '|';
        appendHashInput(identity, manifest.build);
        appendHashInput(identity, manifest.agent);
        manifest.identityHash = hexHash(fnv1a(identity.str()));
        return manifest;
    }

    std::string NavigationCache::hashFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return "missing";
        }
        std::ostringstream stream;
        stream << file.rdbuf();
        return hexHash(fnv1a(stream.str()));
    }

    const NavigationCacheManifest& NavigationCache::manifest() const
    {
        return manifest_;
    }

    const NavigationCacheStats& NavigationCache::stats() const
    {
        return stats_;
    }

    void NavigationCache::clearStats()
    {
        stats_ = {};
    }

    NavigationCacheTileReadResult NavigationCache::readTileCache(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        ChunkCoord coord)
    {
        NavigationCacheTileReadResult result;
        result.kind = NavigationCacheKind::Tile;
        result.coord = coord;
        result.path = navigationTilePath(settings, manifest, coord);
        std::ifstream file(result.path, std::ios::binary);
        if (!file) {
            result.status = NavigationCacheOperationStatus::Miss;
            result.message = "Navigation tile cache miss.";
            return result;
        }

        std::array<char, 4> magic{};
        file.read(magic.data(), magic.size());
        if (magic != TileMagic) {
            result.status = NavigationCacheOperationStatus::Stale;
            result.message = "Navigation tile cache magic mismatch.";
            return result;
        }

        NavigationTileCacheData data;
        file.read(reinterpret_cast<char*>(&data.coord.x), sizeof(data.coord.x));
        file.read(reinterpret_cast<char*>(&data.coord.z), sizeof(data.coord.z));
        file.read(reinterpret_cast<char*>(&data.bounds.min.x), sizeof(float));
        file.read(reinterpret_cast<char*>(&data.bounds.min.y), sizeof(float));
        file.read(reinterpret_cast<char*>(&data.bounds.min.z), sizeof(float));
        file.read(reinterpret_cast<char*>(&data.bounds.max.x), sizeof(float));
        file.read(reinterpret_cast<char*>(&data.bounds.max.y), sizeof(float));
        file.read(reinterpret_cast<char*>(&data.bounds.max.z), sizeof(float));
        uint32_t byteCount = 0;
        file.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));
        if (!file || data.coord != coord || byteCount == 0) {
            result.status = NavigationCacheOperationStatus::Stale;
            result.message = "Navigation tile cache header is invalid.";
            return result;
        }
        data.detourTileData.resize(byteCount);
        file.read(reinterpret_cast<char*>(data.detourTileData.data()), data.detourTileData.size());
        if (!file) {
            result.status = NavigationCacheOperationStatus::Corrupt;
            result.message = "Navigation tile cache payload is truncated.";
            return result;
        }

        result.status = NavigationCacheOperationStatus::Hit;
        result.message = "Loaded navigation tile cache.";
        result.tile = std::move(data);
        return result;
    }

    NavigationCacheWriteResult NavigationCache::writeTileCache(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        const NavigationTileCacheData& tile)
    {
        NavigationCacheWriteResult result;
        result.kind = NavigationCacheKind::Tile;
        result.coord = tile.coord;
        result.path = navigationTilePath(settings, manifest, tile.coord);
        if (tile.detourTileData.empty()) {
            result.status = NavigationCacheOperationStatus::WriteFailed;
            result.message = "Navigation tile cache data is empty.";
            return result;
        }
        try {
            std::filesystem::create_directories(result.path.parent_path());
            std::ofstream file(result.path, std::ios::binary);
            if (!file) {
                result.status = NavigationCacheOperationStatus::WriteFailed;
                result.message = "Failed to open navigation tile cache for writing.";
                return result;
            }
            file.write(TileMagic.data(), TileMagic.size());
            file.write(reinterpret_cast<const char*>(&tile.coord.x), sizeof(tile.coord.x));
            file.write(reinterpret_cast<const char*>(&tile.coord.z), sizeof(tile.coord.z));
            file.write(reinterpret_cast<const char*>(&tile.bounds.min.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&tile.bounds.min.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&tile.bounds.min.z), sizeof(float));
            file.write(reinterpret_cast<const char*>(&tile.bounds.max.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&tile.bounds.max.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&tile.bounds.max.z), sizeof(float));
            const uint32_t byteCount = static_cast<uint32_t>(tile.detourTileData.size());
            file.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
            file.write(reinterpret_cast<const char*>(tile.detourTileData.data()), tile.detourTileData.size());
            result.status = NavigationCacheOperationStatus::WriteSuccess;
            result.message = "Wrote navigation tile cache.";
            return result;
        } catch (const std::exception& exception) {
            result.status = NavigationCacheOperationStatus::WriteFailed;
            result.message = exception.what();
            return result;
        }
    }

    NavigationCacheConnectivityReadResult NavigationCache::readConnectivityCache(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        ChunkCoord coord)
    {
        NavigationCacheConnectivityReadResult result;
        result.kind = NavigationCacheKind::Connectivity;
        result.coord = coord;
        result.path = navigationConnectivityPath(settings, manifest, coord);
        try {
            const YAML::Node root = YAML::LoadFile(result.path.string());
            if (root["identity_hash"].as<std::string>(std::string{}) != manifest.identityHash) {
                result.status = NavigationCacheOperationStatus::Stale;
                result.message = "Navigation connectivity cache identity mismatch.";
                return result;
            }
            result.status = NavigationCacheOperationStatus::Hit;
            result.message = "Loaded navigation connectivity cache.";
            result.connectivity = readConnectivity(root["connectivity"]);
            return result;
        } catch (const YAML::BadFile& exception) {
            result.status = NavigationCacheOperationStatus::Miss;
            result.message = exception.what();
            return result;
        } catch (const std::exception& exception) {
            result.status = NavigationCacheOperationStatus::Corrupt;
            result.message = exception.what();
            return result;
        }
    }

    NavigationCacheWriteResult NavigationCache::writeConnectivityCache(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        const ChunkNavConnectivity& connectivity)
    {
        NavigationCacheWriteResult result;
        result.kind = NavigationCacheKind::Connectivity;
        result.coord = connectivity.coord;
        result.path = navigationConnectivityPath(settings, manifest, connectivity.coord);
        try {
            std::filesystem::create_directories(result.path.parent_path());
            YAML::Node root;
            root["identity_hash"] = manifest.identityHash;
            root["connectivity"] = connectivityNode(connectivity);
            std::ofstream file(result.path);
            if (!file) {
                result.status = NavigationCacheOperationStatus::WriteFailed;
                result.message = "Failed to open navigation connectivity cache for writing.";
                return result;
            }
            file << root;
            result.status = NavigationCacheOperationStatus::WriteSuccess;
            result.message = "Wrote navigation connectivity cache.";
            return result;
        } catch (const std::exception& exception) {
            result.status = NavigationCacheOperationStatus::WriteFailed;
            result.message = exception.what();
            return result;
        }
    }

    NavigationCacheGraphReadResult NavigationCache::readGraphCache(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        ChunkCoord centerChunk)
    {
        NavigationCacheGraphReadResult result;
        result.kind = NavigationCacheKind::Graph;
        result.coord = centerChunk;
        result.path = navigationGraphPath(settings, manifest, centerChunk);
        try {
            const YAML::Node root = YAML::LoadFile(result.path.string());
            if (root["identity_hash"].as<std::string>(std::string{}) != manifest.identityHash) {
                result.status = NavigationCacheOperationStatus::Stale;
                result.message = "World navigation graph cache identity mismatch.";
                return result;
            }
            result.status = NavigationCacheOperationStatus::Hit;
            result.message = "Loaded world navigation graph cache.";
            result.graph = readGraph(root["graph"]);
            return result;
        } catch (const YAML::BadFile& exception) {
            result.status = NavigationCacheOperationStatus::Miss;
            result.message = exception.what();
            return result;
        } catch (const std::exception& exception) {
            result.status = NavigationCacheOperationStatus::Corrupt;
            result.message = exception.what();
            return result;
        }
    }

    NavigationCacheWriteResult NavigationCache::writeGraphCache(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        const WorldNavigationGraphCacheData& graph)
    {
        NavigationCacheWriteResult result;
        result.kind = NavigationCacheKind::Graph;
        result.coord = graph.centerChunk;
        result.path = navigationGraphPath(settings, manifest, graph.centerChunk);
        try {
            std::filesystem::create_directories(result.path.parent_path());
            YAML::Node root;
            root["identity_hash"] = manifest.identityHash;
            root["graph"] = graphNode(graph);
            std::ofstream file(result.path);
            if (!file) {
                result.status = NavigationCacheOperationStatus::WriteFailed;
                result.message = "Failed to open world navigation graph cache for writing.";
                return result;
            }
            file << root;
            result.status = NavigationCacheOperationStatus::WriteSuccess;
            result.message = "Wrote world navigation graph cache.";
            return result;
        } catch (const std::exception& exception) {
            result.status = NavigationCacheOperationStatus::WriteFailed;
            result.message = exception.what();
            return result;
        }
    }

    void NavigationCache::recordReadResult(const NavigationCacheOperationResult& result)
    {
        switch (result.kind) {
            case NavigationCacheKind::Tile:
                if (result.status == NavigationCacheOperationStatus::Hit) {
                    ++stats_.tileHits;
                } else if (result.status == NavigationCacheOperationStatus::Stale ||
                    result.status == NavigationCacheOperationStatus::Corrupt) {
                    ++stats_.tileStale;
                } else {
                    ++stats_.tileMisses;
                }
                break;
            case NavigationCacheKind::Connectivity:
                if (result.status == NavigationCacheOperationStatus::Hit) {
                    ++stats_.connectivityHits;
                } else {
                    ++stats_.connectivityMisses;
                }
                break;
            case NavigationCacheKind::Graph:
                if (result.status == NavigationCacheOperationStatus::Hit) {
                    ++stats_.graphHits;
                } else {
                    ++stats_.graphMisses;
                }
                break;
        }
        setLast(result.path, result.message);
    }

    void NavigationCache::recordWriteResult(const NavigationCacheOperationResult& result)
    {
        if (result.status == NavigationCacheOperationStatus::WriteSuccess) {
            switch (result.kind) {
                case NavigationCacheKind::Tile:
                    ++stats_.tileWrites;
                    break;
                case NavigationCacheKind::Connectivity:
                    ++stats_.connectivityWrites;
                    break;
                case NavigationCacheKind::Graph:
                    ++stats_.graphWrites;
                    break;
            }
        }
        setLast(result.path, result.message);
    }

    bool NavigationCache::ensureManifest()
    {
        try {
            std::filesystem::create_directories(cacheRoot());
            std::ofstream file(manifestPath());
            if (!file) {
                setLast(manifestPath(), "Failed to write navigation cache manifest.");
                return false;
            }
            file << manifestNode(manifest_);
            setLast(manifestPath(), "Wrote navigation cache manifest.");
            return true;
        } catch (const std::exception& exception) {
            setLast(manifestPath(), exception.what());
            return false;
        }
    }

    std::optional<NavigationTileCacheData> NavigationCache::loadTile(ChunkCoord coord)
    {
        NavigationCacheTileReadResult result = readTileCache(settings_, manifest_, coord);
        recordReadResult(result);
        return std::move(result.tile);
    }

    bool NavigationCache::writeTile(const NavigationTileCacheData& tile)
    {
        NavigationCacheWriteResult result = writeTileCache(settings_, manifest_, tile);
        recordWriteResult(result);
        return result.status == NavigationCacheOperationStatus::WriteSuccess;
    }

    std::optional<ChunkNavConnectivity> NavigationCache::loadConnectivity(ChunkCoord coord)
    {
        NavigationCacheConnectivityReadResult result = readConnectivityCache(settings_, manifest_, coord);
        recordReadResult(result);
        return std::move(result.connectivity);
    }

    bool NavigationCache::writeConnectivity(const ChunkNavConnectivity& connectivity)
    {
        NavigationCacheWriteResult result = writeConnectivityCache(settings_, manifest_, connectivity);
        recordWriteResult(result);
        return result.status == NavigationCacheOperationStatus::WriteSuccess;
    }

    std::optional<WorldNavigationGraphCacheData> NavigationCache::loadGraph(ChunkCoord centerChunk)
    {
        NavigationCacheGraphReadResult result = readGraphCache(settings_, manifest_, centerChunk);
        recordReadResult(result);
        return std::move(result.graph);
    }

    bool NavigationCache::writeGraph(const WorldNavigationGraphCacheData& graph)
    {
        NavigationCacheWriteResult result = writeGraphCache(settings_, manifest_, graph);
        recordWriteResult(result);
        return result.status == NavigationCacheOperationStatus::WriteSuccess;
    }

    std::filesystem::path NavigationCache::cacheRoot() const
    {
        return navigationCacheRoot(settings_, manifest_);
    }

    std::filesystem::path NavigationCache::manifestPath() const
    {
        return cacheRoot() / "manifest.yaml";
    }

    std::filesystem::path NavigationCache::tilePath(ChunkCoord coord) const
    {
        return navigationTilePath(settings_, manifest_, coord);
    }

    std::filesystem::path NavigationCache::connectivityPath(ChunkCoord coord) const
    {
        return navigationConnectivityPath(settings_, manifest_, coord);
    }

    std::filesystem::path NavigationCache::graphPath(ChunkCoord centerChunk) const
    {
        return navigationGraphPath(settings_, manifest_, centerChunk);
    }

    void NavigationCache::setLast(std::filesystem::path path, std::string message)
    {
        stats_.lastPath = std::move(path);
        stats_.lastMessage = std::move(message);
    }
}
