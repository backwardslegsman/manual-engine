#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Engine/NavigationCache.hpp"
#include "Engine/OpenWorldStreaming.hpp"
#include "Engine/TerrainDerivedCache.hpp"
#include "Engine/TerrainImport.hpp"
#include "Engine/TerrainNavigationAdapter.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"

namespace Engine {
    inline constexpr const char* OpenWorldStreamingBakeVersion = "open_world_streaming_bake_s4_v1";

    struct OpenWorldStreamingBakeSettings {
        TerrainHeightmapImportSettings heightmap;
        TerrainDerivedCacheSettings terrainCache;
        NavigationCacheSettings navigationCache;
        NavBuildSettings navBuild;
        NavAgentSettings navAgent;
        std::string navigationProfileId = "default";
        uint32_t navigationResolution = 17;
        std::vector<TerrainLodMeshBuildSettings> renderLods;
        uint32_t physicsColliderResolution = 17;
        bool bakeTerrainChunks = true;
        bool bakeRenderLods = true;
        bool bakeNavigationTiles = true;
        bool bakePhysicsColliders = true;
        std::string sceneGeometryHash;
        float sceneGeometryMaxSlopeDegrees = 45.0f;
        float sceneGeometryTileBoundsPadding = 0.45f;
        std::string sceneGeometryAdapterVersion = "none";
    };

    struct OpenWorldStreamingBakeDiagnostics {
        bool success = false;
        uint32_t importedChunkCount = 0;
        uint32_t terrainChunkWrites = 0;
        uint32_t renderLodWrites = 0;
        uint32_t navigationTileWrites = 0;
        uint32_t physicsColliderWrites = 0;
        uint32_t failedPayloadCount = 0;
        uint64_t bytesWritten = 0;
        std::string sourceHash;
        std::string message;
        std::vector<std::string> warnings;
    };

    struct OpenWorldStreamingBakedChunk {
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        StreamingWorldBounds bounds;
        TerrainDerivedCacheManifest terrainChunkManifest;
        std::vector<TerrainDerivedCacheManifest> renderLodManifests;
        NavigationCacheManifest navigationManifest;
        TerrainDerivedCacheManifest physicsColliderManifest;
    };

    struct OpenWorldStreamingBakeManifest {
        StreamingChunkManifest streamingManifest;
        StreamingReadDescriptorTable readDescriptors;
        TerrainHeightmapSourceMetadata sourceMetadata;
        std::string sourceHash;
        std::string bakeVersion = OpenWorldStreamingBakeVersion;
        std::vector<OpenWorldStreamingBakedChunk> chunks;
        OpenWorldStreamingBakeDiagnostics diagnostics;
    };

    [[nodiscard]] OpenWorldStreamingBakeManifest bakeOpenWorldHeightmap(
        const OpenWorldStreamingBakeSettings& settings);
}
