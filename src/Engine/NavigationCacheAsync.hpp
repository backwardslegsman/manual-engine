#pragma once

#include <vector>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/NavigationCache.hpp"

namespace Engine {
    struct NavigationTileCacheReadJobResult {
        NavigationCacheTileReadResult result;
    };

    struct NavigationConnectivityCacheReadJobResult {
        std::vector<NavigationCacheConnectivityReadResult> results;
    };

    struct NavigationGraphCacheReadJobResult {
        NavigationCacheGraphReadResult result;
    };

    struct NavigationCacheWriteJobResult {
        NavigationCacheWriteResult result;
    };

    // Worker-job wrappers for navigation cache file I/O. Callers pass immutable
    // settings/manifest snapshots and merge the returned plain result structs
    // into live systems on the main thread.
    AsyncJobHandle enqueueNavigationTileCacheRead(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        ChunkCoord coord);

    AsyncJobHandle enqueueNavigationConnectivityCacheRead(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        std::vector<ChunkCoord> coords);

    AsyncJobHandle enqueueNavigationGraphCacheRead(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        ChunkCoord centerChunk);

    AsyncJobHandle enqueueNavigationTileCacheWrite(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        NavigationTileCacheData tile);

    AsyncJobHandle enqueueNavigationConnectivityCacheWrite(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        ChunkNavConnectivity connectivity);

    AsyncJobHandle enqueueNavigationGraphCacheWrite(
        AsyncWorkQueue& queue,
        NavigationCacheSettings settings,
        NavigationCacheManifest manifest,
        WorldNavigationGraphCacheData graph);
}
