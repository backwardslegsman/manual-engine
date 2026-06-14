#include "Engine/NavigationCache.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

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
            node["biome_config_hash"] = manifest.biomeConfigHash;
            node["archetype_config_hash"] = manifest.archetypeConfigHash;
            node["generator_version"] = manifest.generatorVersion;
            node["identity_hash"] = manifest.identityHash;
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
        const NavBuildSettings& build,
        const NavAgentSettings& agent,
        const std::filesystem::path& biomeConfigPath,
        const std::filesystem::path& archetypeConfigPath)
    {
        NavigationCacheManifest manifest;
        manifest.worldId = settings.worldId;
        manifest.formatVersion = settings.formatVersion;
        manifest.chunkSize = chunkSize;
        manifest.graphRadiusChunks = graphRadiusChunks;
        manifest.build = build;
        manifest.agent = agent;
        manifest.biomeConfigHash = hashFile(biomeConfigPath);
        manifest.archetypeConfigHash = hashFile(archetypeConfigPath);

        std::ostringstream identity;
        identity << manifest.worldId << '|'
                 << manifest.formatVersion << '|'
                 << manifest.chunkSize << '|'
                 << manifest.graphRadiusChunks << '|'
                 << manifest.biomeConfigHash << '|'
                 << manifest.archetypeConfigHash << '|'
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
        const std::filesystem::path path = tilePath(coord);
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            ++stats_.tileMisses;
            setLast(path, "Navigation tile cache miss.");
            return std::nullopt;
        }

        std::array<char, 4> magic{};
        file.read(magic.data(), magic.size());
        if (magic != TileMagic) {
            ++stats_.tileStale;
            setLast(path, "Navigation tile cache magic mismatch.");
            return std::nullopt;
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
            ++stats_.tileStale;
            setLast(path, "Navigation tile cache header is invalid.");
            return std::nullopt;
        }
        data.detourTileData.resize(byteCount);
        file.read(reinterpret_cast<char*>(data.detourTileData.data()), data.detourTileData.size());
        if (!file) {
            ++stats_.tileStale;
            setLast(path, "Navigation tile cache payload is truncated.");
            return std::nullopt;
        }

        ++stats_.tileHits;
        setLast(path, "Loaded navigation tile cache.");
        return data;
    }

    bool NavigationCache::writeTile(const NavigationTileCacheData& tile)
    {
        if (tile.detourTileData.empty()) {
            return false;
        }
        const std::filesystem::path path = tilePath(tile.coord);
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary);
            if (!file) {
                setLast(path, "Failed to open navigation tile cache for writing.");
                return false;
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
            ++stats_.tileWrites;
            setLast(path, "Wrote navigation tile cache.");
            return true;
        } catch (const std::exception& exception) {
            setLast(path, exception.what());
            return false;
        }
    }

    std::optional<ChunkNavConnectivity> NavigationCache::loadConnectivity(ChunkCoord coord)
    {
        const std::filesystem::path path = connectivityPath(coord);
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            if (root["identity_hash"].as<std::string>(std::string{}) != manifest_.identityHash) {
                ++stats_.connectivityMisses;
                setLast(path, "Navigation connectivity cache identity mismatch.");
                return std::nullopt;
            }
            ++stats_.connectivityHits;
            setLast(path, "Loaded navigation connectivity cache.");
            return readConnectivity(root["connectivity"]);
        } catch (const std::exception& exception) {
            ++stats_.connectivityMisses;
            setLast(path, exception.what());
            return std::nullopt;
        }
    }

    bool NavigationCache::writeConnectivity(const ChunkNavConnectivity& connectivity)
    {
        const std::filesystem::path path = connectivityPath(connectivity.coord);
        try {
            std::filesystem::create_directories(path.parent_path());
            YAML::Node root;
            root["identity_hash"] = manifest_.identityHash;
            root["connectivity"] = connectivityNode(connectivity);
            std::ofstream file(path);
            if (!file) {
                setLast(path, "Failed to open navigation connectivity cache for writing.");
                return false;
            }
            file << root;
            ++stats_.connectivityWrites;
            setLast(path, "Wrote navigation connectivity cache.");
            return true;
        } catch (const std::exception& exception) {
            setLast(path, exception.what());
            return false;
        }
    }

    std::optional<WorldNavigationGraphCacheData> NavigationCache::loadGraph(ChunkCoord centerChunk)
    {
        const std::filesystem::path path = graphPath(centerChunk);
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            if (root["identity_hash"].as<std::string>(std::string{}) != manifest_.identityHash) {
                ++stats_.graphMisses;
                setLast(path, "World navigation graph cache identity mismatch.");
                return std::nullopt;
            }
            ++stats_.graphHits;
            setLast(path, "Loaded world navigation graph cache.");
            return readGraph(root["graph"]);
        } catch (const std::exception& exception) {
            ++stats_.graphMisses;
            setLast(path, exception.what());
            return std::nullopt;
        }
    }

    bool NavigationCache::writeGraph(const WorldNavigationGraphCacheData& graph)
    {
        const std::filesystem::path path = graphPath(graph.centerChunk);
        try {
            std::filesystem::create_directories(path.parent_path());
            YAML::Node root;
            root["identity_hash"] = manifest_.identityHash;
            root["graph"] = graphNode(graph);
            std::ofstream file(path);
            if (!file) {
                setLast(path, "Failed to open world navigation graph cache for writing.");
                return false;
            }
            file << root;
            ++stats_.graphWrites;
            setLast(path, "Wrote world navigation graph cache.");
            return true;
        } catch (const std::exception& exception) {
            setLast(path, exception.what());
            return false;
        }
    }

    std::filesystem::path NavigationCache::cacheRoot() const
    {
        return settings_.rootPath / manifest_.identityHash;
    }

    std::filesystem::path NavigationCache::manifestPath() const
    {
        return cacheRoot() / "manifest.yaml";
    }

    std::filesystem::path NavigationCache::tilePath(ChunkCoord coord) const
    {
        return cacheRoot() / "tiles" / chunkFileName(coord, ".navtile");
    }

    std::filesystem::path NavigationCache::connectivityPath(ChunkCoord coord) const
    {
        return cacheRoot() / "connectivity" / chunkFileName(coord, ".yaml");
    }

    std::filesystem::path NavigationCache::graphPath(ChunkCoord centerChunk) const
    {
        return cacheRoot() / "graphs" / chunkFileName(centerChunk, ".yaml");
    }

    void NavigationCache::setLast(std::filesystem::path path, std::string message)
    {
        stats_.lastPath = std::move(path);
        stats_.lastMessage = std::move(message);
    }
}
