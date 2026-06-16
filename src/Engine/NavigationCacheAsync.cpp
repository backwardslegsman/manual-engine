#include "Engine/NavigationCacheAsync.hpp"

#include <any>
#include <utility>

namespace Engine {
    namespace {
        NavigationCacheWriteJobResult cancelledWriteResult(NavigationCacheKind kind, ChunkCoord coord = {})
        {
            NavigationCacheWriteJobResult result;
            result.result.kind = kind;
            result.result.coord = coord;
            result.result.status = NavigationCacheOperationStatus::Cancelled;
            result.result.message = "Navigation cache write cancelled.";
            return result;
        }
    }

    AsyncJobHandle enqueueNavigationTileCacheRead(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        ChunkCoord coord)
    {
        return queue.submit("navigation tile cache read", [settings = std::move(settings), manifest = std::move(manifest), coord](std::stop_token stopToken) -> std::any {
            NavigationTileCacheReadJobResult result;
            if (stopToken.stop_requested()) {
                result.result.kind = NavigationCacheKind::Tile;
                result.result.coord = coord;
                result.result.status = NavigationCacheOperationStatus::Cancelled;
                result.result.message = "Navigation tile cache read cancelled.";
                return result;
            }
            result.result = NavigationCache::readTileCache(settings, manifest, coord);
            return result;
        });
    }

    AsyncJobHandle enqueueNavigationConnectivityCacheRead(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        std::vector<ChunkCoord> coords)
    {
        return queue.submit("navigation connectivity cache read", [settings = std::move(settings), manifest = std::move(manifest), coords = std::move(coords)](std::stop_token stopToken) -> std::any {
            NavigationConnectivityCacheReadJobResult result;
            result.results.reserve(coords.size());
            for (ChunkCoord coord : coords) {
                if (stopToken.stop_requested()) {
                    NavigationCacheConnectivityReadResult cancelled;
                    cancelled.kind = NavigationCacheKind::Connectivity;
                    cancelled.coord = coord;
                    cancelled.status = NavigationCacheOperationStatus::Cancelled;
                    cancelled.message = "Navigation connectivity cache read cancelled.";
                    result.results.push_back(std::move(cancelled));
                    continue;
                }
                result.results.push_back(NavigationCache::readConnectivityCache(settings, manifest, coord));
            }
            return result;
        });
    }

    AsyncJobHandle enqueueNavigationGraphCacheRead(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        ChunkCoord centerChunk)
    {
        return queue.submit("navigation graph cache read", [settings = std::move(settings), manifest = std::move(manifest), centerChunk](std::stop_token stopToken) -> std::any {
            NavigationGraphCacheReadJobResult result;
            if (stopToken.stop_requested()) {
                result.result.kind = NavigationCacheKind::Graph;
                result.result.coord = centerChunk;
                result.result.status = NavigationCacheOperationStatus::Cancelled;
                result.result.message = "Navigation graph cache read cancelled.";
                return result;
            }
            result.result = NavigationCache::readGraphCache(settings, manifest, centerChunk);
            return result;
        });
    }

    AsyncJobHandle enqueueNavigationTileCacheWrite(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        NavigationTileCacheData tile)
    {
        return queue.submit("navigation tile cache write", [settings = std::move(settings), manifest = std::move(manifest), tile = std::move(tile)](std::stop_token stopToken) -> std::any {
            if (stopToken.stop_requested()) {
                return cancelledWriteResult(NavigationCacheKind::Tile, tile.coord);
            }
            NavigationCacheWriteJobResult result;
            result.result = NavigationCache::writeTileCache(settings, manifest, tile);
            return result;
        });
    }

    AsyncJobHandle enqueueNavigationConnectivityCacheWrite(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        ChunkNavConnectivity connectivity)
    {
        return queue.submit("navigation connectivity cache write", [settings = std::move(settings), manifest = std::move(manifest), connectivity = std::move(connectivity)](std::stop_token stopToken) -> std::any {
            if (stopToken.stop_requested()) {
                return cancelledWriteResult(NavigationCacheKind::Connectivity, connectivity.coord);
            }
            NavigationCacheWriteJobResult result;
            result.result = NavigationCache::writeConnectivityCache(settings, manifest, connectivity);
            return result;
        });
    }

    AsyncJobHandle enqueueNavigationGraphCacheWrite(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        WorldNavigationGraphCacheData graph)
    {
        return queue.submit("navigation graph cache write", [settings = std::move(settings), manifest = std::move(manifest), graph = std::move(graph)](std::stop_token stopToken) -> std::any {
            if (stopToken.stop_requested()) {
                return cancelledWriteResult(NavigationCacheKind::Graph, graph.centerChunk);
            }
            NavigationCacheWriteJobResult result;
            result.result = NavigationCache::writeGraphCache(settings, manifest, graph);
            return result;
        });
    }
}
