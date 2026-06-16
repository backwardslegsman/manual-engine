#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "Engine/Navigation.hpp"
#include "Engine/NavigationConnectivity.hpp"
#include "Engine/WorldNavigationGraph.hpp"

namespace Engine {
    struct NavigationCacheSettings {
        std::filesystem::path rootPath = "generated/navigation_cache";
        std::string worldId = "sample";
        uint32_t formatVersion = 1;
    };

    struct NavigationCacheManifest {
        std::string worldId = "sample";
        uint32_t formatVersion = 1;
        float chunkSize = 24.0f;
        int32_t graphRadiusChunks = 64;
        uint32_t navigationResolution = 17;
        NavBuildSettings build;
        NavAgentSettings agent;
        std::string profileId = "default";
        std::string biomeConfigHash;
        std::string archetypeConfigHash;
        std::string generatorVersion = "navigation_phase_12_v6_async_runtime_tiles";
        std::string identityHash;
    };

    struct NavigationCacheStats {
        uint32_t tileHits = 0;
        uint32_t tileMisses = 0;
        uint32_t tileStale = 0;
        uint32_t tileWrites = 0;
        uint32_t connectivityHits = 0;
        uint32_t connectivityMisses = 0;
        uint32_t connectivityWrites = 0;
        uint32_t graphHits = 0;
        uint32_t graphMisses = 0;
        uint32_t graphWrites = 0;
        std::filesystem::path lastPath;
        std::string lastMessage;
    };

    enum class NavigationCacheKind {
        Tile,
        Connectivity,
        Graph,
    };

    enum class NavigationCacheOperationStatus {
        Hit,
        Miss,
        Stale,
        Corrupt,
        WriteSuccess,
        WriteFailed,
        Cancelled,
    };

    struct NavigationCacheOperationResult {
        NavigationCacheKind kind = NavigationCacheKind::Tile;
        NavigationCacheOperationStatus status = NavigationCacheOperationStatus::Miss;
        ChunkCoord coord;
        std::filesystem::path path;
        std::string message;
    };

    struct NavigationCacheTileReadResult : NavigationCacheOperationResult {
        std::optional<NavigationTileCacheData> tile;
    };

    struct NavigationCacheConnectivityReadResult : NavigationCacheOperationResult {
        std::optional<ChunkNavConnectivity> connectivity;
    };

    struct NavigationCacheGraphReadResult : NavigationCacheOperationResult {
        std::optional<WorldNavigationGraphCacheData> graph;
    };

    using NavigationCacheWriteResult = NavigationCacheOperationResult;

    // Disk cache for deterministic baseline navigation data. Cache files are
    // derived data and must never be required for startup or save loading.
    class NavigationCache {
    public:
        NavigationCache(NavigationCacheSettings settings, NavigationCacheManifest manifest);

        static NavigationCacheManifest buildManifest(
            const NavigationCacheSettings& settings,
            float chunkSize,
            int32_t graphRadiusChunks,
            uint32_t navigationResolution,
            const NavBuildSettings& build,
            const NavAgentSettings& agent,
            std::string profileId,
            const std::filesystem::path& biomeConfigPath,
            const std::filesystem::path& archetypeConfigPath);
        static std::string hashFile(const std::filesystem::path& path);

        const NavigationCacheManifest& manifest() const;
        const NavigationCacheStats& stats() const;
        void clearStats();
        bool ensureManifest();

        static NavigationCacheTileReadResult readTileCache(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            ChunkCoord coord);
        static NavigationCacheWriteResult writeTileCache(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            const NavigationTileCacheData& tile);
        static NavigationCacheConnectivityReadResult readConnectivityCache(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            ChunkCoord coord);
        static NavigationCacheWriteResult writeConnectivityCache(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            const ChunkNavConnectivity& connectivity);
        static NavigationCacheGraphReadResult readGraphCache(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            ChunkCoord centerChunk);
        static NavigationCacheWriteResult writeGraphCache(
            const NavigationCacheSettings& settings,
            const NavigationCacheManifest& manifest,
            const WorldNavigationGraphCacheData& graph);

        void recordReadResult(const NavigationCacheOperationResult& result);
        void recordWriteResult(const NavigationCacheOperationResult& result);

        std::optional<NavigationTileCacheData> loadTile(ChunkCoord coord);
        bool writeTile(const NavigationTileCacheData& tile);

        std::optional<ChunkNavConnectivity> loadConnectivity(ChunkCoord coord);
        bool writeConnectivity(const ChunkNavConnectivity& connectivity);

        std::optional<WorldNavigationGraphCacheData> loadGraph(ChunkCoord centerChunk);
        bool writeGraph(const WorldNavigationGraphCacheData& graph);

    private:
        std::filesystem::path cacheRoot() const;
        std::filesystem::path manifestPath() const;
        std::filesystem::path tilePath(ChunkCoord coord) const;
        std::filesystem::path connectivityPath(ChunkCoord coord) const;
        std::filesystem::path graphPath(ChunkCoord centerChunk) const;
        void setLast(std::filesystem::path path, std::string message);

        NavigationCacheSettings settings_;
        NavigationCacheManifest manifest_;
        NavigationCacheStats stats_;
    };
}
