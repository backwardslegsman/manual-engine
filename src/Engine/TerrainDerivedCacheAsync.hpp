#pragma once

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/TerrainDerivedCache.hpp"

namespace Engine {
    struct TerrainDerivedChunkCacheReadJobResult {
        TerrainDerivedCacheChunkReadResult result;
    };

    struct TerrainDerivedLodMeshCacheReadJobResult {
        TerrainDerivedCacheLodMeshReadResult result;
    };

    struct TerrainDerivedCacheWriteJobResult {
        TerrainDerivedCacheWriteResult result;
    };

    AsyncJobHandle enqueueTerrainChunkCacheRead(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest);

    AsyncJobHandle enqueueTerrainChunkCacheWrite(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest,
        TerrainCachedChunkPayload payload);

    AsyncJobHandle enqueueTerrainLodMeshCacheRead(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest);

    AsyncJobHandle enqueueTerrainLodMeshCacheWrite(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest,
        TerrainCachedLodMeshPayload payload);
}
