#include "Engine/OpenWorldStreamingRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "Engine/NavigationCache.hpp"
#include "Engine/TerrainDerivedCache.hpp"

namespace Engine {
    namespace {
        uint64_t fnv1a(std::string_view text, uint64_t hash = 14695981039346656037ull)
        {
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

        void append(std::ostringstream& stream, const AssetImportSettingsKey& settings)
        {
            stream << settings.pipeline << '|'
                   << settings.version << '|'
                   << settings.optionsHash << '|';
        }

        void append(std::ostringstream& stream, const NavBuildSettings& settings)
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

        void append(std::ostringstream& stream, const NavAgentSettings& settings)
        {
            stream << settings.radius << '|'
                   << settings.height << '|'
                   << settings.maxSlopeDegrees << '|'
                   << settings.maxClimb << '|';
        }

        YAML::Node settingsNode(const AssetImportSettingsKey& settings)
        {
            YAML::Node node;
            node["pipeline"] = settings.pipeline;
            node["version"] = settings.version;
            node["options_hash"] = settings.optionsHash;
            return node;
        }

        AssetImportSettingsKey readSettings(const YAML::Node& node)
        {
            AssetImportSettingsKey settings;
            settings.pipeline = node["pipeline"].as<std::string>("");
            settings.version = node["version"].as<std::string>("");
            settings.optionsHash = node["options_hash"].as<std::string>("");
            return settings;
        }

        YAML::Node coordNode(ChunkCoord coord)
        {
            YAML::Node node;
            node["x"] = coord.x;
            node["z"] = coord.z;
            return node;
        }

        ChunkCoord readCoord(const YAML::Node& node)
        {
            return {node["x"].as<int32_t>(0), node["z"].as<int32_t>(0)};
        }

        YAML::Node sourceCoordNode(TerrainSourceChunkCoord coord)
        {
            YAML::Node node;
            node["x"] = coord.x;
            node["z"] = coord.z;
            return node;
        }

        TerrainSourceChunkCoord readSourceCoord(const YAML::Node& node)
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

        YAML::Node boundsNode(const StreamingWorldBounds& bounds)
        {
            YAML::Node node;
            node["min"] = vec3Node(bounds.min);
            node["max"] = vec3Node(bounds.max);
            return node;
        }

        StreamingWorldBounds readBounds(const YAML::Node& node)
        {
            return {readVec3(node["min"]), readVec3(node["max"])};
        }

        YAML::Node keyNode(const StreamingChunkKey& key)
        {
            YAML::Node node;
            node["kind"] = static_cast<uint32_t>(key.kind);
            node["terrain_source"] = key.terrainChunk.source.value;
            node["terrain_coord"] = sourceCoordNode(key.terrainChunk.coord);
            node["asset"] = key.asset.value;
            node["stable_id"] = key.stableId;
            node["variant_id"] = key.variantId;
            return node;
        }

        StreamingChunkKey readKey(const YAML::Node& node)
        {
            StreamingChunkKey key;
            key.kind = static_cast<StreamingChunkKeyKind>(node["kind"].as<uint32_t>(0));
            key.terrainChunk.source = {node["terrain_source"].as<uint64_t>(0)};
            key.terrainChunk.coord = readSourceCoord(node["terrain_coord"]);
            key.asset = {node["asset"].as<uint64_t>(0)};
            key.stableId = node["stable_id"].as<std::string>("");
            key.variantId = node["variant_id"].as<std::string>("");
            return key;
        }

        YAML::Node recordNode(const StreamingChunkManifestRecord& record)
        {
            YAML::Node node;
            node["key"] = keyNode(record.key);
            node["payload"] = static_cast<uint32_t>(record.payload);
            node["bounds"] = boundsNode(record.bounds);
            node["source_hash"] = record.sourceHash;
            node["settings_hash"] = record.settingsHash;
            node["estimated_bytes"] = record.estimatedBytes;
            node["dirty_flags"] = static_cast<uint32_t>(record.dirtyFlags);
            node["halo_profile"] = static_cast<uint32_t>(record.haloProfile);
            node["detail_level"] = record.detailLevel;
            node["priority_bias"] = record.priorityBias;
            node["debug_name"] = record.debugName;
            return node;
        }

        StreamingChunkManifestRecord readRecord(const YAML::Node& node)
        {
            StreamingChunkManifestRecord record;
            record.key = readKey(node["key"]);
            record.payload = static_cast<StreamingPayloadKind>(node["payload"].as<uint32_t>(0));
            record.bounds = readBounds(node["bounds"]);
            record.sourceHash = node["source_hash"].as<uint64_t>(0);
            record.settingsHash = node["settings_hash"].as<uint64_t>(0);
            record.estimatedBytes = node["estimated_bytes"].as<uint64_t>(0);
            record.dirtyFlags = static_cast<StreamingDirtyFlags>(node["dirty_flags"].as<uint32_t>(0));
            record.haloProfile = static_cast<StreamingHaloProfile>(node["halo_profile"].as<uint32_t>(0));
            record.detailLevel = node["detail_level"].as<uint32_t>(0);
            record.priorityBias = node["priority_bias"].as<int32_t>(0);
            record.debugName = node["debug_name"].as<std::string>("");
            if (static_cast<uint32_t>(record.payload) < StreamingPayloadKindCount) {
                record.availablePayloads[static_cast<uint32_t>(record.payload)] = true;
            }
            return record;
        }

        YAML::Node terrainManifestNode(const TerrainDerivedCacheManifest& manifest)
        {
            YAML::Node node;
            node["root"] = manifest.settings.rootPath.generic_string();
            node["format_version"] = manifest.settings.formatVersion;
            node["terrain_import_version"] = manifest.settings.terrainImportVersion;
            node["chunk_payload_version"] = manifest.settings.chunkPayloadVersion;
            node["lod_payload_version"] = manifest.settings.lodMeshPayloadVersion;
            node["physics_payload_version"] = manifest.settings.physicsColliderPayloadVersion;
            node["policy"] = static_cast<uint32_t>(manifest.settings.policy);
            node["kind"] = static_cast<uint32_t>(manifest.kind);
            node["chunk_id"]["source"] = manifest.chunkId.source.value;
            node["chunk_id"]["coord"] = sourceCoordNode(manifest.chunkId.coord);
            node["import_settings"] = settingsNode(manifest.importSettings);
            node["source_hash"] = manifest.sourceHash;
            node["source_type"] = static_cast<uint32_t>(manifest.sourceType);
            node["chunk_size"] = manifest.chunkSize;
            node["chunk_resolution"] = manifest.chunkResolution;
            node["lod_index"] = manifest.lodIndex;
            node["render_resolution"] = manifest.renderResolution;
            node["payload_version"] = manifest.payloadVersion;
            node["payload_file"] = manifest.payloadFileName;
            node["payload_hash"] = manifest.payloadHash;
            node["identity_hash"] = manifest.identityHash;
            return node;
        }

        TerrainDerivedCacheManifest readTerrainManifest(const YAML::Node& node)
        {
            TerrainDerivedCacheManifest manifest;
            manifest.settings.rootPath = node["root"].as<std::string>("");
            manifest.settings.formatVersion = node["format_version"].as<uint32_t>(1);
            manifest.settings.terrainImportVersion = node["terrain_import_version"].as<std::string>("");
            manifest.settings.chunkPayloadVersion = node["chunk_payload_version"].as<std::string>("");
            manifest.settings.lodMeshPayloadVersion = node["lod_payload_version"].as<std::string>("");
            manifest.settings.physicsColliderPayloadVersion = node["physics_payload_version"].as<std::string>("");
            manifest.settings.policy = static_cast<TerrainDerivedCachePolicy>(node["policy"].as<uint32_t>(0));
            manifest.kind = static_cast<TerrainDerivedKind>(node["kind"].as<uint32_t>(0));
            manifest.chunkId.source = {node["chunk_id"]["source"].as<uint64_t>(0)};
            manifest.chunkId.coord = readSourceCoord(node["chunk_id"]["coord"]);
            manifest.importSettings = readSettings(node["import_settings"]);
            manifest.sourceHash = node["source_hash"].as<std::string>("");
            manifest.sourceType = static_cast<TerrainDatasetSourceType>(node["source_type"].as<uint32_t>(0));
            manifest.chunkSize = node["chunk_size"].as<float>(0.0f);
            manifest.chunkResolution = node["chunk_resolution"].as<uint32_t>(0);
            manifest.lodIndex = node["lod_index"].as<uint32_t>(0);
            manifest.renderResolution = node["render_resolution"].as<uint32_t>(0);
            manifest.payloadVersion = node["payload_version"].as<std::string>("");
            manifest.payloadFileName = node["payload_file"].as<std::string>("");
            manifest.payloadHash = node["payload_hash"].as<std::string>("");
            manifest.identityHash = node["identity_hash"].as<std::string>("");
            return manifest;
        }

        YAML::Node navBuildNode(const NavBuildSettings& settings)
        {
            YAML::Node node;
            node["cell_size"] = settings.cellSize;
            node["cell_height"] = settings.cellHeight;
            node["tile_border_size"] = settings.tileBorderSize;
            node["max_tiles"] = settings.maxTiles;
            node["max_polys_per_tile"] = settings.maxPolysPerTile;
            node["max_verts_per_poly"] = settings.maxVertsPerPoly;
            node["region_min_size"] = settings.regionMinSize;
            node["region_merge_size"] = settings.regionMergeSize;
            node["edge_max_len"] = settings.edgeMaxLen;
            node["edge_max_error"] = settings.edgeMaxError;
            node["detail_sample_dist"] = settings.detailSampleDist;
            node["detail_sample_max_error"] = settings.detailSampleMaxError;
            return node;
        }

        NavBuildSettings readNavBuild(const YAML::Node& node)
        {
            NavBuildSettings settings;
            settings.cellSize = node["cell_size"].as<float>(settings.cellSize);
            settings.cellHeight = node["cell_height"].as<float>(settings.cellHeight);
            settings.tileBorderSize = node["tile_border_size"].as<int32_t>(settings.tileBorderSize);
            settings.maxTiles = node["max_tiles"].as<int32_t>(settings.maxTiles);
            settings.maxPolysPerTile = node["max_polys_per_tile"].as<int32_t>(settings.maxPolysPerTile);
            settings.maxVertsPerPoly = node["max_verts_per_poly"].as<int32_t>(settings.maxVertsPerPoly);
            settings.regionMinSize = node["region_min_size"].as<float>(settings.regionMinSize);
            settings.regionMergeSize = node["region_merge_size"].as<float>(settings.regionMergeSize);
            settings.edgeMaxLen = node["edge_max_len"].as<float>(settings.edgeMaxLen);
            settings.edgeMaxError = node["edge_max_error"].as<float>(settings.edgeMaxError);
            settings.detailSampleDist = node["detail_sample_dist"].as<float>(settings.detailSampleDist);
            settings.detailSampleMaxError = node["detail_sample_max_error"].as<float>(settings.detailSampleMaxError);
            return settings;
        }

        YAML::Node navAgentNode(const NavAgentSettings& settings)
        {
            YAML::Node node;
            node["radius"] = settings.radius;
            node["height"] = settings.height;
            node["max_slope_degrees"] = settings.maxSlopeDegrees;
            node["max_climb"] = settings.maxClimb;
            return node;
        }

        NavAgentSettings readNavAgent(const YAML::Node& node)
        {
            NavAgentSettings settings;
            settings.radius = node["radius"].as<float>(settings.radius);
            settings.height = node["height"].as<float>(settings.height);
            settings.maxSlopeDegrees = node["max_slope_degrees"].as<float>(settings.maxSlopeDegrees);
            settings.maxClimb = node["max_climb"].as<float>(settings.maxClimb);
            return settings;
        }

        YAML::Node navigationManifestNode(const NavigationCacheManifest& manifest)
        {
            YAML::Node node;
            node["world_id"] = manifest.worldId;
            node["format_version"] = manifest.formatVersion;
            node["chunk_size"] = manifest.chunkSize;
            node["graph_radius_chunks"] = manifest.graphRadiusChunks;
            node["navigation_resolution"] = manifest.navigationResolution;
            node["build"] = navBuildNode(manifest.build);
            node["agent"] = navAgentNode(manifest.agent);
            node["profile_id"] = manifest.profileId;
            node["terrain_source_id"] = manifest.terrainSourceId.value;
            node["terrain_source_hash"] = manifest.terrainSourceHash;
            node["terrain_import_settings"] = settingsNode(manifest.terrainImportSettings);
            node["terrain_source_type"] = manifest.terrainSourceType;
            node["terrain_navigation_adapter_version"] = manifest.terrainNavigationAdapterVersion;
            node["scene_geometry_hash"] = manifest.sceneGeometryHash;
            node["scene_geometry_max_slope_degrees"] = manifest.sceneGeometryMaxSlopeDegrees;
            node["scene_geometry_tile_bounds_padding"] = manifest.sceneGeometryTileBoundsPadding;
            node["scene_geometry_adapter_version"] = manifest.sceneGeometryAdapterVersion;
            node["generator_version"] = manifest.generatorVersion;
            node["identity_hash"] = manifest.identityHash;
            return node;
        }

        NavigationCacheManifest readNavigationManifest(const YAML::Node& node)
        {
            NavigationCacheManifest manifest;
            manifest.worldId = node["world_id"].as<std::string>("sample");
            manifest.formatVersion = node["format_version"].as<uint32_t>(1);
            manifest.chunkSize = node["chunk_size"].as<float>(24.0f);
            manifest.graphRadiusChunks = node["graph_radius_chunks"].as<int32_t>(64);
            manifest.navigationResolution = node["navigation_resolution"].as<uint32_t>(17);
            manifest.build = readNavBuild(node["build"]);
            manifest.agent = readNavAgent(node["agent"]);
            manifest.profileId = node["profile_id"].as<std::string>("default");
            manifest.terrainSourceId = {node["terrain_source_id"].as<uint64_t>(0)};
            manifest.terrainSourceHash = node["terrain_source_hash"].as<std::string>("");
            manifest.terrainImportSettings = readSettings(node["terrain_import_settings"]);
            manifest.terrainSourceType = node["terrain_source_type"].as<std::string>("unknown");
            manifest.terrainNavigationAdapterVersion =
                node["terrain_navigation_adapter_version"].as<std::string>("");
            manifest.sceneGeometryHash = node["scene_geometry_hash"].as<std::string>("");
            manifest.sceneGeometryMaxSlopeDegrees =
                node["scene_geometry_max_slope_degrees"].as<float>(45.0f);
            manifest.sceneGeometryTileBoundsPadding =
                node["scene_geometry_tile_bounds_padding"].as<float>(0.45f);
            manifest.sceneGeometryAdapterVersion =
                node["scene_geometry_adapter_version"].as<std::string>("none");
            manifest.generatorVersion = node["generator_version"].as<std::string>("");
            manifest.identityHash = node["identity_hash"].as<std::string>("");
            return manifest;
        }

        YAML::Node navigationSettingsNode(const NavigationCacheSettings& settings)
        {
            YAML::Node node;
            node["root"] = settings.rootPath.generic_string();
            node["world_id"] = settings.worldId;
            node["format_version"] = settings.formatVersion;
            return node;
        }

        NavigationCacheSettings readNavigationSettings(const YAML::Node& node)
        {
            NavigationCacheSettings settings;
            settings.rootPath = node["root"].as<std::string>("");
            settings.worldId = node["world_id"].as<std::string>("sample");
            settings.formatVersion = node["format_version"].as<uint32_t>(1);
            return settings;
        }

        YAML::Node descriptorNode(const StreamingReadDescriptorEntry& entry)
        {
            YAML::Node node;
            node["key"] = keyNode(entry.key);
            node["payload"] = static_cast<uint32_t>(entry.payload);
            node["manifest_record_hash"] = entry.manifestRecordHash;
            node["kind"] = static_cast<uint32_t>(entry.descriptor.kind);
            if (entry.descriptor.terrainChunkManifest) {
                node["terrain_manifest"] = terrainManifestNode(*entry.descriptor.terrainChunkManifest);
            }
            if (entry.descriptor.navigationSettings) {
                node["navigation_settings"] = navigationSettingsNode(*entry.descriptor.navigationSettings);
            }
            if (entry.descriptor.navigationManifest) {
                node["navigation_manifest"] = navigationManifestNode(*entry.descriptor.navigationManifest);
            }
            node["navigation_coord"] = coordNode(entry.descriptor.navigationCoord);
            node["scene_chunk_path"] = entry.descriptor.sceneChunkPath.generic_string();
            node["scene_chunk_stable_id"] = entry.descriptor.sceneChunkStableId;
            node["fake_message"] = entry.descriptor.fakeMessage;
            return node;
        }

        StreamingReadDescriptorEntry readDescriptor(const YAML::Node& node)
        {
            StreamingReadDescriptorEntry entry;
            entry.key = readKey(node["key"]);
            entry.payload = static_cast<StreamingPayloadKind>(node["payload"].as<uint32_t>(0));
            entry.manifestRecordHash = node["manifest_record_hash"].as<uint64_t>(0);
            entry.descriptor.kind =
                static_cast<StreamingReadDescriptorKind>(node["kind"].as<uint32_t>(0));
            if (node["terrain_manifest"]) {
                entry.descriptor.terrainChunkManifest =
                    std::make_shared<TerrainDerivedCacheManifest>(readTerrainManifest(node["terrain_manifest"]));
            }
            if (node["navigation_settings"]) {
                entry.descriptor.navigationSettings =
                    std::make_shared<NavigationCacheSettings>(readNavigationSettings(node["navigation_settings"]));
            }
            if (node["navigation_manifest"]) {
                entry.descriptor.navigationManifest =
                    std::make_shared<NavigationCacheManifest>(readNavigationManifest(node["navigation_manifest"]));
            }
            entry.descriptor.navigationCoord = readCoord(node["navigation_coord"]);
            entry.descriptor.sceneChunkPath = node["scene_chunk_path"].as<std::string>("");
            entry.descriptor.sceneChunkStableId = node["scene_chunk_stable_id"].as<std::string>("");
            entry.descriptor.fakeMessage = node["fake_message"].as<std::string>("");
            return entry;
        }

        bool validateDescriptorPayload(const StreamingReadDescriptorEntry& entry)
        {
            switch (entry.descriptor.kind) {
                case StreamingReadDescriptorKind::TerrainChunkCache:
                    return entry.descriptor.terrainChunkManifest &&
                        TerrainDerivedCache::readChunk(*entry.descriptor.terrainChunkManifest).status ==
                            TerrainDerivedCacheStatus::Hit;
                case StreamingReadDescriptorKind::TerrainLodMeshCache:
                    return entry.descriptor.terrainChunkManifest &&
                        TerrainDerivedCache::readLodMesh(*entry.descriptor.terrainChunkManifest).status ==
                            TerrainDerivedCacheStatus::Hit;
                case StreamingReadDescriptorKind::TerrainPhysicsColliderCache:
                    return entry.descriptor.terrainChunkManifest &&
                        TerrainDerivedCache::readPhysicsCollider(*entry.descriptor.terrainChunkManifest).status ==
                            TerrainDerivedCacheStatus::Hit;
                case StreamingReadDescriptorKind::NavigationTileCache:
                    return entry.descriptor.navigationSettings &&
                        entry.descriptor.navigationManifest &&
                        NavigationCache::readTileCache(
                            *entry.descriptor.navigationSettings,
                            *entry.descriptor.navigationManifest,
                            entry.descriptor.navigationCoord).status == NavigationCacheOperationStatus::Hit;
                case StreamingReadDescriptorKind::SceneChunkBinary:
                    return !entry.descriptor.sceneChunkPath.empty() &&
                        std::filesystem::is_regular_file(entry.descriptor.sceneChunkPath);
                case StreamingReadDescriptorKind::MetadataOnly:
                case StreamingReadDescriptorKind::Fake:
                    return true;
                case StreamingReadDescriptorKind::Unsupported:
                case StreamingReadDescriptorKind::None:
                    return false;
            }
            return false;
        }

        bool loadSavedBuild(
            const std::filesystem::path& path,
            const std::string& expectedFingerprint,
            bool validatePayloads,
            StreamingChunkManifest& manifest,
            StreamingReadDescriptorTable& descriptors,
            OpenWorldStreamingBuildResult& result)
        {
            if (!std::filesystem::is_regular_file(path)) {
                result.reason = "Saved streaming build manifest is missing.";
                return false;
            }

            try {
                const YAML::Node root = YAML::LoadFile(path.string());
                if (root["runtime_version"].as<std::string>("") != OpenWorldStreamingRuntimeVersion) {
                    result.reason = "Saved streaming build runtime version changed.";
                    return false;
                }
                if (root["fingerprint"].as<std::string>("") != expectedFingerprint) {
                    result.reason = "Saved streaming build fingerprint changed.";
                    return false;
                }

                manifest.records.clear();
                descriptors.entries.clear();
                for (const YAML::Node& node : root["records"]) {
                    manifest.records.push_back(readRecord(node));
                }
                for (const YAML::Node& node : root["read_descriptors"]) {
                    descriptors.entries.push_back(readDescriptor(node));
                }

                if (manifest.records.empty() || descriptors.entries.empty()) {
                    result.reason = "Saved streaming build has no records.";
                    return false;
                }
                if (validatePayloads) {
                    for (const StreamingReadDescriptorEntry& entry : descriptors.entries) {
                        if (!validateDescriptorPayload(entry)) {
                            result.reason = "Saved streaming build payload is missing or stale: " +
                                stableStreamingChunkKeyString(entry.key);
                            return false;
                        }
                    }
                }

                result.sourceHash = root["source_hash"].as<std::string>("");
                return true;
            } catch (const std::exception& ex) {
                result.reason = ex.what();
                return false;
            }
        }

        bool writeSavedBuild(
            const std::filesystem::path& path,
            const std::string& fingerprint,
            const OpenWorldStreamingBakeManifest& bake)
        {
            YAML::Node root;
            root["runtime_version"] = OpenWorldStreamingRuntimeVersion;
            root["bake_version"] = bake.bakeVersion;
            root["fingerprint"] = fingerprint;
            root["source_hash"] = bake.sourceHash;
            root["source_id"] = bake.sourceMetadata.sourceId.value;
            root["import_settings"] = settingsNode(bake.sourceMetadata.importSettings);
            for (const StreamingChunkManifestRecord& record : bake.streamingManifest.records) {
                root["records"].push_back(recordNode(record));
            }
            for (const StreamingReadDescriptorEntry& descriptor : bake.readDescriptors.entries) {
                root["read_descriptors"].push_back(descriptorNode(descriptor));
            }

            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            if (!output) {
                return false;
            }
            output << root;
            return static_cast<bool>(output);
        }

        OpenWorldStreamingDiagnostics mergeDiagnostics(
            const StreamingHaloPlan& plan,
            const OpenWorldStreamingCacheHalo& cache,
            const OpenWorldStreamingDerivedGenerationHalo& generation,
            const OpenWorldStreamingLiveHalo& live,
            const OpenWorldStreamingBuildResult& build)
        {
            OpenWorldStreamingDiagnostics merged = plan.diagnostics;
            const OpenWorldStreamingDiagnostics cacheDiagnostics = cache.diagnostics();
            const OpenWorldStreamingDiagnostics generationDiagnostics = generation.diagnostics();
            const OpenWorldStreamingDiagnostics liveDiagnostics = live.diagnostics();

            for (uint32_t index = 0; index < StreamingResidencyStateCount; ++index) {
                merged.actualChunksByState[index] =
                    cacheDiagnostics.actualChunksByState[index] +
                    generationDiagnostics.actualChunksByState[index] +
                    liveDiagnostics.actualChunksByState[index];
            }
            for (uint32_t index = 0; index < StreamingTransitionLaneCount; ++index) {
                merged.lanes[index].queuedCount +=
                    cacheDiagnostics.lanes[index].queuedCount +
                    generationDiagnostics.lanes[index].queuedCount +
                    liveDiagnostics.lanes[index].queuedCount;
                merged.lanes[index].activeJobCount +=
                    cacheDiagnostics.lanes[index].activeJobCount +
                    generationDiagnostics.lanes[index].activeJobCount +
                    liveDiagnostics.lanes[index].activeJobCount;
                merged.lanes[index].completedCount +=
                    cacheDiagnostics.lanes[index].completedCount +
                    generationDiagnostics.lanes[index].completedCount +
                    liveDiagnostics.lanes[index].completedCount;
                merged.lanes[index].cancelledCount +=
                    cacheDiagnostics.lanes[index].cancelledCount +
                    generationDiagnostics.lanes[index].cancelledCount +
                    liveDiagnostics.lanes[index].cancelledCount;
                merged.lanes[index].staleCount +=
                    cacheDiagnostics.lanes[index].staleCount +
                    generationDiagnostics.lanes[index].staleCount +
                    liveDiagnostics.lanes[index].staleCount;
                merged.lanes[index].failedCount +=
                    cacheDiagnostics.lanes[index].failedCount +
                    generationDiagnostics.lanes[index].failedCount +
                    liveDiagnostics.lanes[index].failedCount;
                merged.lanes[index].elapsedMicroseconds +=
                    cacheDiagnostics.lanes[index].elapsedMicroseconds +
                    generationDiagnostics.lanes[index].elapsedMicroseconds +
                    liveDiagnostics.lanes[index].elapsedMicroseconds;
                merged.lanes[index].bytesRead += cacheDiagnostics.lanes[index].bytesRead;
                merged.lanes[index].bytesWritten += generationDiagnostics.lanes[index].bytesWritten;
            }
            for (uint32_t index = 0; index < StreamingPayloadKindCount; ++index) {
                merged.payloads[index].hits += cacheDiagnostics.payloads[index].hits;
                merged.payloads[index].misses += cacheDiagnostics.payloads[index].misses;
                merged.payloads[index].stale += cacheDiagnostics.payloads[index].stale;
                merged.payloads[index].corrupt += cacheDiagnostics.payloads[index].corrupt;
                merged.payloads[index].writes += generationDiagnostics.payloads[index].writes;
            }
            merged.pendingReadCount = cacheDiagnostics.pendingReadCount;
            merged.cachedCpuPayloadCount = cacheDiagnostics.cachedCpuPayloadCount;
            merged.staleReadCompletionCount = cacheDiagnostics.staleReadCompletionCount;
            merged.unsupportedReadCount = cacheDiagnostics.unsupportedReadCount;
            merged.generationQueuedCount = generationDiagnostics.generationQueuedCount;
            merged.generationCompletedCount = generationDiagnostics.generationCompletedCount;
            merged.generationFailedCount = generationDiagnostics.generationFailedCount;
            merged.pendingPromoteCount = liveDiagnostics.pendingPromoteCount;
            merged.pendingDemoteCount = liveDiagnostics.pendingDemoteCount;
            merged.failedPromotionCount = liveDiagnostics.failedPromotionCount;
            merged.failedDemotionCount = liveDiagnostics.failedDemotionCount;
            merged.livePayloadCount = liveDiagnostics.livePayloadCount;
            merged.liveResources = liveDiagnostics.liveResources;
            merged.bakeChunkCount = build.bakeDiagnostics.importedChunkCount;
            merged.bakePayloadWriteCount =
                build.bakeDiagnostics.terrainChunkWrites +
                build.bakeDiagnostics.renderLodWrites +
                build.bakeDiagnostics.navigationTileWrites +
                build.bakeDiagnostics.physicsColliderWrites;
            if (cacheDiagnostics.lastFailure.hasFailure) {
                merged.lastFailure = cacheDiagnostics.lastFailure;
            } else if (generationDiagnostics.lastFailure.hasFailure) {
                merged.lastFailure = generationDiagnostics.lastFailure;
            } else if (liveDiagnostics.lastFailure.hasFailure) {
                merged.lastFailure = liveDiagnostics.lastFailure;
            }
            return merged;
        }
    }

    std::string openWorldStreamingRuntimeFingerprint(
        const OpenWorldStreamingRuntimeSettings& settings)
    {
        std::ostringstream input;
        input << OpenWorldStreamingRuntimeVersion << '|'
              << OpenWorldStreamingBakeVersion << '|'
              << settings.bake.heightmap.sourcePath.lexically_normal().generic_string() << '|'
              << TerrainDerivedCache::hashFile(settings.bake.heightmap.sourcePath) << '|'
              << settings.bake.heightmap.sampleSpacing << '|'
              << settings.bake.heightmap.heightScale << '|'
              << settings.bake.heightmap.heightOffset << '|'
              << settings.bake.heightmap.sourceOrigin.x << '|'
              << settings.bake.heightmap.sourceOrigin.y << '|'
              << settings.bake.heightmap.sourceOrigin.z << '|'
              << settings.bake.heightmap.flipRows << '|'
              << settings.bake.heightmap.flipColumns << '|'
              << settings.bake.heightmap.chunkWorldSize << '|'
              << settings.bake.heightmap.chunkResolution << '|'
              << settings.bake.terrainCache.formatVersion << '|'
              << settings.bake.terrainCache.terrainImportVersion << '|'
              << settings.bake.terrainCache.chunkPayloadVersion << '|'
              << settings.bake.terrainCache.lodMeshPayloadVersion << '|'
              << settings.bake.terrainCache.physicsColliderPayloadVersion << '|'
              << settings.bake.navigationCache.rootPath.lexically_normal().generic_string() << '|'
              << settings.bake.navigationCache.worldId << '|'
              << settings.bake.navigationCache.formatVersion << '|'
              << settings.bake.navigationProfileId << '|'
              << settings.bake.navigationResolution << '|'
              << settings.bake.physicsColliderResolution << '|'
              << settings.bake.sceneGeometryHash << '|'
              << settings.bake.sceneGeometryMaxSlopeDegrees << '|'
              << settings.bake.sceneGeometryTileBoundsPadding << '|'
              << settings.bake.sceneGeometryAdapterVersion << '|';
        input << settings.bake.terrainNavigationBorderPaddingWorld << '|'
              << settings.bake.terrainNavigationBorderSampleCount << '|';
        append(input, settings.bake.navBuild);
        append(input, settings.bake.navAgent);
        for (const TerrainLodMeshBuildSettings& lod : settings.bake.renderLods) {
            input << lod.lodIndex << '|'
                  << lod.renderResolution << '|'
                  << lod.skirtDepth << '|';
        }
        return hexHash(fnv1a(input.str()));
    }

    OpenWorldStreamingRuntime::OpenWorldStreamingRuntime(OpenWorldStreamingRuntimeSettings settings)
        : settings_(std::move(settings))
        , cache_(settings_.cache)
        , generation_(settings_.generation)
        , live_(settings_.promotion)
    {
    }

    OpenWorldStreamingBuildResult rebuildOpenWorldStreamingSavedBuild(
        const OpenWorldStreamingRuntimeSettings& settings)
    {
        OpenWorldStreamingBuildResult result;
        result.fingerprint = openWorldStreamingRuntimeFingerprint(settings);
        result.sourceHash = TerrainDerivedCache::hashFile(settings.bake.heightmap.sourcePath);

        const OpenWorldStreamingBakeManifest bake = bakeOpenWorldHeightmap(settings.bake);
        result.bakeDiagnostics = bake.diagnostics;
        result.sourceHash = bake.sourceHash;
        result.rebuilt = true;
        if (bake.streamingManifest.records.empty() || bake.readDescriptors.entries.empty()) {
            result.status = OpenWorldStreamingBuildStatus::Failed;
            result.success = false;
            result.message = bake.diagnostics.message.empty()
                ? "Open-world streaming rebuild produced no records."
                : bake.diagnostics.message;
            return result;
        }

        if (!writeSavedBuild(settings.savedBuildManifestPath, result.fingerprint, bake)) {
            result.status = OpenWorldStreamingBuildStatus::Failed;
            result.success = false;
            result.message = "Open-world streaming rebuild completed but saved manifest write failed.";
            return result;
        }

        result.status = OpenWorldStreamingBuildStatus::RebuiltSavedBuild;
        result.success = true;
        result.message = "Rebuilt saved open-world streaming build.";
        return result;
    }

    const OpenWorldStreamingBuildResult& OpenWorldStreamingRuntime::initializeFromSavedBuild()
    {
        initialized_ = false;
        manifest_.records.clear();
        readDescriptors_.entries.clear();
        generationDescriptors_.entries.clear();
        cache_.clear();
        generation_.clear();
        live_.clear();
        lastPlan_ = {};
        diagnostics_ = {};
        build_ = {};
        build_.fingerprint = openWorldStreamingRuntimeFingerprint(settings_);
        build_.sourceHash = TerrainDerivedCache::hashFile(settings_.bake.heightmap.sourcePath);

        if (loadSavedBuild(
                settings_.savedBuildManifestPath,
                build_.fingerprint,
                settings_.validatePayloadFiles,
                manifest_,
                readDescriptors_,
                build_)) {
            build_.status = OpenWorldStreamingBuildStatus::ReusedSavedBuild;
            build_.success = true;
            build_.reusedSavedBuild = true;
            build_.message = "Reused saved open-world streaming build.";
            initialized_ = true;
            rebuildMergedDiagnostics();
            return build_;
        }

        if (!settings_.rebuildWhenStale) {
            build_.status = OpenWorldStreamingBuildStatus::Failed;
            build_.success = false;
            build_.message = "Saved open-world streaming build is not current and rebuild is disabled.";
            rebuildMergedDiagnostics();
            return build_;
        }

        build_ = rebuildOpenWorldStreamingSavedBuild(settings_);
        if (build_.success &&
            loadSavedBuild(
                settings_.savedBuildManifestPath,
                build_.fingerprint,
                settings_.validatePayloadFiles,
                manifest_,
                readDescriptors_,
                build_)) {
            build_.status = OpenWorldStreamingBuildStatus::RebuiltSavedBuild;
            build_.success = true;
            build_.rebuilt = true;
            build_.message = "Rebuilt saved open-world streaming build.";
            initialized_ = true;
        } else if (build_.success) {
            build_.status = OpenWorldStreamingBuildStatus::Failed;
            build_.success = false;
            build_.message = build_.reason.empty()
                ? "Open-world streaming rebuild completed but saved manifest reload failed."
                : build_.reason;
        }
        rebuildMergedDiagnostics();
        return build_;
    }

    void OpenWorldStreamingRuntime::update(
        const StreamingFocusInput& focus,
        AsyncWorkQueue& asyncQueue,
        MainThreadWorkQueue& mainThreadQueue,
        StreamingPromotionCallbacks callbacks)
    {
        if (!initialized_) {
            return;
        }

        lastPlan_ = planStreamingHalo(manifest_, focus, currentResidency(), settings_.planner);
        cache_.update(asyncQueue, lastPlan_, readDescriptors_);
        generation_.update(asyncQueue, lastPlan_, cache_.snapshot(), generationDescriptors_);
        live_.update(mainThreadQueue, lastPlan_, cache_, std::move(callbacks));
        rebuildMergedDiagnostics();
    }

    void OpenWorldStreamingRuntime::update(
        const glm::vec3& focus,
        AsyncWorkQueue& asyncQueue,
        MainThreadWorkQueue& mainThreadQueue,
        StreamingPromotionCallbacks callbacks)
    {
        StreamingFocusInput input;
        input.position = focus;
        update(input, asyncQueue, mainThreadQueue, std::move(callbacks));
    }

    void OpenWorldStreamingRuntime::pollCompleted(const std::vector<AsyncCompletedJob>& completedJobs)
    {
        cache_.mergeCompleted(completedJobs);
        generation_.mergeCompleted(completedJobs);
        const StreamingGenerationHaloSnapshot generated = generation_.snapshot();
        for (const StreamingGenerationHaloDebugRecord& record : generated.records) {
            (void)record;
        }
        rebuildMergedDiagnostics();
    }

    void OpenWorldStreamingRuntime::shutdown()
    {
        cache_.clear();
        generation_.clear();
        live_.clear();
        lastPlan_ = {};
        diagnostics_ = {};
        initialized_ = false;
    }

    bool OpenWorldStreamingRuntime::initialized() const
    {
        return initialized_;
    }

    const StreamingChunkManifest& OpenWorldStreamingRuntime::manifest() const
    {
        return manifest_;
    }

    const StreamingReadDescriptorTable& OpenWorldStreamingRuntime::readDescriptors() const
    {
        return readDescriptors_;
    }

    const StreamingGenerationDescriptorTable& OpenWorldStreamingRuntime::generationDescriptors() const
    {
        return generationDescriptors_;
    }

    const OpenWorldStreamingBuildResult& OpenWorldStreamingRuntime::buildResult() const
    {
        return build_;
    }

    OpenWorldStreamingRuntimeSnapshot OpenWorldStreamingRuntime::snapshot() const
    {
        return {
            lastPlan_,
            cache_.snapshot(),
            generation_.snapshot(),
            live_.snapshot(),
            diagnostics_,
            build_,
        };
    }

    OpenWorldStreamingDiagnostics OpenWorldStreamingRuntime::diagnostics() const
    {
        return diagnostics_;
    }

    void OpenWorldStreamingRuntime::rebuildMergedDiagnostics()
    {
        diagnostics_ = mergeDiagnostics(lastPlan_, cache_, generation_, live_, build_);
    }

    std::vector<StreamingChunkResidencyInput> OpenWorldStreamingRuntime::currentResidency() const
    {
        std::vector<StreamingChunkResidencyInput> residency;
        const StreamingCacheHaloSnapshot cacheSnapshot = cache_.snapshot();
        residency.insert(residency.end(), cacheSnapshot.residency.begin(), cacheSnapshot.residency.end());
        const StreamingLiveHaloSnapshot liveSnapshot = live_.snapshot();
        for (const StreamingChunkResidencyInput& liveRecord : liveSnapshot.residency) {
            auto existing = std::ranges::find_if(residency, [&](const StreamingChunkResidencyInput& input) {
                return input.key == liveRecord.key && input.payload == liveRecord.payload;
            });
            if (existing == residency.end()) {
                residency.push_back(liveRecord);
            } else {
                *existing = liveRecord;
            }
        }
        return residency;
    }
}
