#include "Engine/OpenWorldStreamingBake.hpp"

#include <algorithm>
#include <limits>

#include "Engine/Navigation.hpp"

namespace Engine {
    namespace {
        constexpr uint64_t StableHashOffset = 1469598103934665603ull;
        constexpr uint64_t StableHashPrime = 1099511628211ull;

        [[nodiscard]] uint64_t stableHashString(const std::string& value)
        {
            uint64_t hash = StableHashOffset;
            for (const unsigned char byte : value) {
                hash ^= static_cast<uint64_t>(byte);
                hash *= StableHashPrime;
            }
            return hash;
        }

        [[nodiscard]] ChunkCoord chunkCoordFromSource(TerrainSourceChunkCoord coord)
        {
            return {coord.x, coord.z};
        }

        [[nodiscard]] StreamingWorldBounds boundsFromImportedChunk(const TerrainImportedChunk& chunk)
        {
            StreamingWorldBounds bounds;
            bounds.min = {chunk.origin.x, std::numeric_limits<float>::max(), chunk.origin.z};
            bounds.max = {chunk.origin.x + chunk.size, std::numeric_limits<float>::lowest(), chunk.origin.z + chunk.size};
            for (float height : chunk.heights) {
                bounds.min.y = std::min(bounds.min.y, height);
                bounds.max.y = std::max(bounds.max.y, height);
            }
            if (chunk.heights.empty()) {
                bounds.min.y = chunk.origin.y;
                bounds.max.y = chunk.origin.y;
            }
            return bounds;
        }

        void recordTerrainWrite(
            OpenWorldStreamingBakeDiagnostics& diagnostics,
            const TerrainDerivedCacheWriteResult& result,
            uint32_t& counter)
        {
            if (result.status == TerrainDerivedCacheStatus::WriteSuccess) {
                ++counter;
                diagnostics.bytesWritten += result.bytes;
            } else {
                ++diagnostics.failedPayloadCount;
                diagnostics.warnings.push_back(result.message);
            }
        }

        void recordNavigationWrite(
            OpenWorldStreamingBakeDiagnostics& diagnostics,
            const NavigationCacheWriteResult& result)
        {
            if (result.status == NavigationCacheOperationStatus::WriteSuccess) {
                ++diagnostics.navigationTileWrites;
                if (!result.path.empty() && std::filesystem::is_regular_file(result.path)) {
                    diagnostics.bytesWritten += std::filesystem::file_size(result.path);
                }
            } else {
                ++diagnostics.failedPayloadCount;
                diagnostics.warnings.push_back(result.message);
            }
        }
    }

    OpenWorldStreamingBakeManifest bakeOpenWorldHeightmap(
        const OpenWorldStreamingBakeSettings& settings)
    {
        OpenWorldStreamingBakeManifest result;
        result.diagnostics.sourceHash = TerrainDerivedCache::hashFile(settings.heightmap.sourcePath);
        result.sourceHash = result.diagnostics.sourceHash;

        const TerrainHeightmapTerrainImportResult imported = importHeightmapTerrain(settings.heightmap);
        if (!imported.success) {
            result.diagnostics.success = false;
            result.diagnostics.message = imported.message;
            result.diagnostics.warnings = imported.warnings;
            return result;
        }

        result.sourceMetadata = imported.metadata;
        result.diagnostics.importedChunkCount = static_cast<uint32_t>(imported.chunks.size());
        result.diagnostics.warnings = imported.warnings;

        const TerrainNavigationSourceIdentity navIdentity{
            imported.metadata.sourceId,
            result.sourceHash,
            imported.metadata.importSettings,
            TerrainDatasetSourceType::HeightmapImported,
        };
        const TerrainPhysicsSourceIdentity physicsIdentity{
            imported.metadata.sourceId,
            result.sourceHash,
            imported.metadata.importSettings,
            TerrainDatasetSourceType::HeightmapImported,
        };
        const float navigationStep = settings.heightmap.chunkWorldSize /
            static_cast<float>(std::max(settings.navigationResolution, 2u) - 1u);
        const float defaultBorderPadding = std::max(
            settings.navAgent.radius * 2.0f,
            settings.navBuild.cellSize * 4.0f);
        const float navigationBorderPadding = settings.terrainNavigationBorderPaddingWorld > 0.0f
            ? settings.terrainNavigationBorderPaddingWorld
            : defaultBorderPadding;
        const uint32_t navigationBorderSamples = settings.terrainNavigationBorderSampleCount > 0
            ? settings.terrainNavigationBorderSampleCount
            : static_cast<uint32_t>(std::ceil(navigationBorderPadding / std::max(navigationStep, 0.0001f)));

        const NavigationCacheManifest navigationManifest = NavigationCache::buildManifest(
            settings.navigationCache,
            settings.heightmap.chunkWorldSize,
            0,
            settings.navigationResolution,
            settings.navBuild,
            settings.navAgent,
            settings.navigationProfileId,
            {},
            {},
            imported.metadata.sourceId,
            result.sourceHash,
            imported.metadata.importSettings,
            "heightmap_imported",
            TerrainNavigationAdapterVersion,
            navigationBorderPadding,
            navigationBorderSamples,
            settings.sceneGeometryHash,
            settings.sceneGeometryMaxSlopeDegrees,
            settings.sceneGeometryTileBoundsPadding,
            settings.sceneGeometryAdapterVersion);

        for (const TerrainImportedChunk& chunk : imported.chunks) {
            OpenWorldStreamingBakedChunk baked;
            baked.chunkId = chunk.id;
            baked.coord = chunkCoordFromSource(chunk.coord);
            baked.bounds = boundsFromImportedChunk(chunk);
            baked.navigationManifest = navigationManifest;

            const TerrainCachedChunkPayload cachedChunk = terrainCachedChunkPayloadFromImported(
                chunk,
                TerrainDatasetSourceType::HeightmapImported,
                imported.metadata.importSettings);

            if (settings.bakeTerrainChunks) {
                baked.terrainChunkManifest = TerrainDerivedCache::buildChunkManifest(
                    settings.terrainCache,
                    cachedChunk,
                    result.sourceHash);
                const TerrainDerivedCacheWriteResult write =
                    TerrainDerivedCache::writeChunk(baked.terrainChunkManifest, cachedChunk);
                recordTerrainWrite(result.diagnostics, write, result.diagnostics.terrainChunkWrites);
                if (write.status == TerrainDerivedCacheStatus::WriteSuccess) {
                    result.streamingManifest.records.push_back(makeTerrainStreamingManifestRecord(
                        chunk.id,
                        baked.bounds,
                        stableHashString(result.sourceHash),
                        stableHashString(baked.terrainChunkManifest.identityHash),
                        write.bytes,
                        StreamingPayloadKind::TerrainChunk,
                        "baked terrain chunk"));
                    setStreamingReadDescriptor(
                        result.readDescriptors,
                        result.streamingManifest.records.back().key,
                        StreamingPayloadKind::TerrainChunk,
                        hashStreamingManifestRecord(result.streamingManifest.records.back()),
                        terrainChunkCacheReadDescriptor(baked.terrainChunkManifest));
                }
            }

            if (settings.bakeRenderLods) {
                for (const TerrainLodMeshBuildSettings& lod : settings.renderLods) {
                    TerrainCachedLodMeshPayload lodPayload = buildTerrainCachedLodMesh(cachedChunk, lod);
                    TerrainDerivedCacheManifest lodManifest = TerrainDerivedCache::buildLodMeshManifest(
                        settings.terrainCache,
                        cachedChunk,
                        lod,
                        result.sourceHash);
                    const TerrainDerivedCacheWriteResult write =
                        TerrainDerivedCache::writeLodMesh(lodManifest, lodPayload);
                    recordTerrainWrite(result.diagnostics, write, result.diagnostics.renderLodWrites);
                    baked.renderLodManifests.push_back(lodManifest);
                    if (write.status == TerrainDerivedCacheStatus::WriteSuccess) {
                        result.streamingManifest.records.push_back(makeTerrainStreamingManifestRecord(
                            chunk.id,
                            baked.bounds,
                            stableHashString(result.sourceHash),
                            stableHashString(lodManifest.identityHash),
                            write.bytes,
                            StreamingPayloadKind::TerrainRenderLod,
                            "baked terrain render lod"));
                        result.streamingManifest.records.back().key.variantId =
                            "terrain_lod_" + std::to_string(lod.lodIndex);
                        result.streamingManifest.records.back().haloProfile = lod.lodIndex == 0
                            ? StreamingHaloProfile::HighDetailLive
                            : StreamingHaloProfile::LowDetailLive;
                        result.streamingManifest.records.back().detailLevel = lod.lodIndex;
                        setStreamingReadDescriptor(
                            result.readDescriptors,
                            result.streamingManifest.records.back().key,
                            StreamingPayloadKind::TerrainRenderLod,
                            hashStreamingManifestRecord(result.streamingManifest.records.back()),
                            terrainLodMeshCacheReadDescriptor(lodManifest));
                    }
                }
            }

            if (settings.bakeNavigationTiles) {
                TerrainNavigationBuildSettings navSettings;
                navSettings.navigationResolution = settings.navigationResolution;
                navSettings.borderPaddingWorld = navigationBorderPadding;
                navSettings.borderSampleCount = navigationBorderSamples;
                const std::optional<TerrainNavigationBuildRequest> navRequest =
                    terrainNavigationRequestFromImportedChunkNeighborhood(
                        imported.chunks,
                        chunk.id,
                        navSettings,
                        navIdentity);
                const TerrainNavigationBuildResult terrainNav = navRequest
                    ? buildTerrainNavigationData(*navRequest)
                    : TerrainNavigationBuildResult{};
                if (terrainNav.success && terrainNav.buildData) {
                    const NavigationTileBuildResult tile = NavigationSystem::buildTerrainTileData(
                        *terrainNav.buildData,
                        settings.navAgent,
                        settings.navBuild);
                    if (tile.tileData) {
                        const NavigationCacheWriteResult write =
                            NavigationCache::writeTileCache(settings.navigationCache, navigationManifest, *tile.tileData);
                        recordNavigationWrite(result.diagnostics, write);
                        if (write.status == NavigationCacheOperationStatus::WriteSuccess) {
                            result.streamingManifest.records.push_back(makeTerrainStreamingManifestRecord(
                                chunk.id,
                                baked.bounds,
                                stableHashString(result.sourceHash),
                                stableHashString(navigationManifest.identityHash),
                                static_cast<uint64_t>(tile.tileData->detourTileData.size()),
                                StreamingPayloadKind::NavigationTile,
                                "baked navigation tile"));
                            setStreamingReadDescriptor(
                                result.readDescriptors,
                                result.streamingManifest.records.back().key,
                                StreamingPayloadKind::NavigationTile,
                                hashStreamingManifestRecord(result.streamingManifest.records.back()),
                                navigationTileCacheReadDescriptor(settings.navigationCache, navigationManifest, baked.coord));
                        }
                    } else {
                        ++result.diagnostics.failedPayloadCount;
                        result.diagnostics.warnings.push_back(tile.message);
                    }
                } else {
                    ++result.diagnostics.failedPayloadCount;
                    result.diagnostics.warnings.push_back(terrainNav.diagnostics.message);
                }
            }

            if (settings.bakePhysicsColliders) {
                TerrainPhysicsColliderBuildRequest physicsRequest;
                physicsRequest.chunkId = chunk.id;
                physicsRequest.coord = baked.coord;
                physicsRequest.origin = chunk.origin;
                physicsRequest.size = chunk.size;
                physicsRequest.sourceResolution = chunk.resolution;
                physicsRequest.colliderResolution = settings.physicsColliderResolution;
                physicsRequest.heights = chunk.heights;
                physicsRequest.identity = physicsIdentity;
                const TerrainPhysicsColliderBuildResult physics = buildTerrainPhysicsCollider(physicsRequest);
                if (physics.success && physics.payload) {
                    baked.physicsColliderManifest = TerrainDerivedCache::buildPhysicsColliderManifest(
                        settings.terrainCache,
                        *physics.payload,
                        result.sourceHash);
                    const TerrainDerivedCacheWriteResult write =
                        TerrainDerivedCache::writePhysicsCollider(baked.physicsColliderManifest, *physics.payload);
                    recordTerrainWrite(result.diagnostics, write, result.diagnostics.physicsColliderWrites);
                    if (write.status == TerrainDerivedCacheStatus::WriteSuccess) {
                        result.streamingManifest.records.push_back(makeTerrainStreamingManifestRecord(
                            chunk.id,
                            baked.bounds,
                            stableHashString(result.sourceHash),
                            stableHashString(baked.physicsColliderManifest.identityHash),
                            write.bytes,
                            StreamingPayloadKind::PhysicsCollider,
                            "baked terrain physics collider"));
                        setStreamingReadDescriptor(
                            result.readDescriptors,
                            result.streamingManifest.records.back().key,
                            StreamingPayloadKind::PhysicsCollider,
                            hashStreamingManifestRecord(result.streamingManifest.records.back()),
                            terrainPhysicsColliderCacheReadDescriptor(baked.physicsColliderManifest));
                    }
                } else {
                    ++result.diagnostics.failedPayloadCount;
                    result.diagnostics.warnings.push_back(physics.diagnostics.message);
                }
            }

            result.chunks.push_back(std::move(baked));
        }

        result.diagnostics.success = result.diagnostics.failedPayloadCount == 0;
        result.diagnostics.message = result.diagnostics.success
            ? "Open-world heightmap bake completed."
            : "Open-world heightmap bake completed with payload failures.";
        return result;
    }
}
