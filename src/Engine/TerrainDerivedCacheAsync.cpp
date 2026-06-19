#include "Engine/TerrainDerivedCacheAsync.hpp"

#include <any>
#include <utility>

namespace Engine {
    namespace {
        TerrainDerivedCacheWriteJobResult cancelledWrite(TerrainDerivedKind kind)
        {
            TerrainDerivedCacheWriteJobResult result;
            result.result.kind = kind;
            result.result.status = TerrainDerivedCacheStatus::Cancelled;
            result.result.message = "Terrain cache write cancelled.";
            return result;
        }
    }

    AsyncJobHandle enqueueTerrainChunkCacheRead(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest)
    {
        return queue.submit("terrain chunk cache read", [manifest = std::move(manifest)](std::stop_token stopToken) -> std::any {
            TerrainDerivedChunkCacheReadJobResult result;
            if (stopToken.stop_requested()) {
                result.result.kind = TerrainDerivedKind::ChunkHeights;
                result.result.status = TerrainDerivedCacheStatus::Cancelled;
                result.result.message = "Terrain chunk cache read cancelled.";
                return result;
            }
            result.result = TerrainDerivedCache::readChunk(manifest);
            return result;
        });
    }

    AsyncJobHandle enqueueTerrainChunkCacheWrite(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest,
        TerrainCachedChunkPayload payload)
    {
        return queue.submit("terrain chunk cache write", [manifest = std::move(manifest), payload = std::move(payload)](std::stop_token stopToken) -> std::any {
            if (stopToken.stop_requested()) {
                return cancelledWrite(TerrainDerivedKind::ChunkHeights);
            }
            TerrainDerivedCacheWriteJobResult result;
            result.result = TerrainDerivedCache::writeChunk(manifest, payload);
            return result;
        });
    }

    AsyncJobHandle enqueueTerrainLodMeshCacheRead(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest)
    {
        return queue.submit("terrain lod mesh cache read", [manifest = std::move(manifest)](std::stop_token stopToken) -> std::any {
            TerrainDerivedLodMeshCacheReadJobResult result;
            if (stopToken.stop_requested()) {
                result.result.kind = TerrainDerivedKind::LodMesh;
                result.result.status = TerrainDerivedCacheStatus::Cancelled;
                result.result.message = "Terrain LOD mesh cache read cancelled.";
                return result;
            }
            result.result = TerrainDerivedCache::readLodMesh(manifest);
            return result;
        });
    }

    AsyncJobHandle enqueueTerrainLodMeshCacheWrite(
        AsyncWorkQueue& queue,
        TerrainDerivedCacheManifest manifest,
        TerrainCachedLodMeshPayload payload)
    {
        return queue.submit("terrain lod mesh cache write", [manifest = std::move(manifest), payload = std::move(payload)](std::stop_token stopToken) -> std::any {
            if (stopToken.stop_requested()) {
                return cancelledWrite(TerrainDerivedKind::LodMesh);
            }
            TerrainDerivedCacheWriteJobResult result;
            result.result = TerrainDerivedCache::writeLodMesh(manifest, payload);
            return result;
        });
    }
}
