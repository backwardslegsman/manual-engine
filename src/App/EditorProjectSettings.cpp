#include "EditorProjectSettings.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace ManualEngine::App {
    namespace {
        constexpr uint32_t EditorProjectSettingsSchemaVersion = 1;

        YAML::Node vec2Node(const glm::vec2& value)
        {
            YAML::Node node;
            node["x"] = value.x;
            node["y"] = value.y;
            return node;
        }

        YAML::Node mapChild(const YAML::Node& node, const char* key)
        {
            return node && node.IsMap() ? node[key] : YAML::Node{};
        }

        YAML::Node vec3Node(const glm::vec3& value)
        {
            YAML::Node node;
            node["x"] = value.x;
            node["y"] = value.y;
            node["z"] = value.z;
            return node;
        }

        glm::vec2 readVec2(const YAML::Node& node, glm::vec2 fallback)
        {
            if (!node || !node.IsMap()) {
                return fallback;
            }
            fallback.x = node["x"].as<float>(fallback.x);
            fallback.y = node["y"].as<float>(fallback.y);
            return fallback;
        }

        glm::vec3 readVec3(const YAML::Node& node, glm::vec3 fallback)
        {
            if (!node || !node.IsMap()) {
                return fallback;
            }
            fallback.x = node["x"].as<float>(fallback.x);
            fallback.y = node["y"].as<float>(fallback.y);
            fallback.z = node["z"].as<float>(fallback.z);
            return fallback;
        }

        const char* cachePolicyName(Engine::TerrainDerivedCachePolicy policy)
        {
            switch (policy) {
                case Engine::TerrainDerivedCachePolicy::Disabled:
                    return "disabled";
                case Engine::TerrainDerivedCachePolicy::ReadOnly:
                    return "read_only";
                case Engine::TerrainDerivedCachePolicy::GenerateOnMiss:
                    return "generate_on_miss";
                case Engine::TerrainDerivedCachePolicy::Refresh:
                    return "refresh";
            }
            return "disabled";
        }

        bool readCachePolicy(
            const YAML::Node& node,
            Engine::TerrainDerivedCachePolicy& policy,
            std::vector<std::string>& errors)
        {
            if (!node) {
                return true;
            }
            const std::string value = node.as<std::string>();
            if (value == "disabled") {
                policy = Engine::TerrainDerivedCachePolicy::Disabled;
            } else if (value == "read_only") {
                policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
            } else if (value == "generate_on_miss") {
                policy = Engine::TerrainDerivedCachePolicy::GenerateOnMiss;
            } else if (value == "refresh") {
                policy = Engine::TerrainDerivedCachePolicy::Refresh;
            } else {
                errors.push_back("Unknown terrain cache policy '" + value + "'.");
                return false;
            }
            return true;
        }

        const char* generationPolicyName(Engine::StreamingDerivedGenerationPolicy policy)
        {
            switch (policy) {
                case Engine::StreamingDerivedGenerationPolicy::ReadOnly:
                    return "read_only";
                case Engine::StreamingDerivedGenerationPolicy::GenerateOnMiss:
                    return "generate_on_miss";
                case Engine::StreamingDerivedGenerationPolicy::Refresh:
                    return "refresh";
            }
            return "read_only";
        }

        bool readGenerationPolicy(
            const YAML::Node& node,
            Engine::StreamingDerivedGenerationPolicy& policy,
            std::vector<std::string>& errors)
        {
            if (!node) {
                return true;
            }
            const std::string value = node.as<std::string>();
            if (value == "read_only") {
                policy = Engine::StreamingDerivedGenerationPolicy::ReadOnly;
            } else if (value == "generate_on_miss") {
                policy = Engine::StreamingDerivedGenerationPolicy::GenerateOnMiss;
            } else if (value == "refresh") {
                policy = Engine::StreamingDerivedGenerationPolicy::Refresh;
            } else {
                errors.push_back("Unknown streaming generation policy '" + value + "'.");
                return false;
            }
            return true;
        }

        const char* heightmapChannelName(Engine::TerrainHeightmapChannel channel)
        {
            switch (channel) {
                case Engine::TerrainHeightmapChannel::Red:
                    return "red";
                case Engine::TerrainHeightmapChannel::Green:
                    return "green";
                case Engine::TerrainHeightmapChannel::Blue:
                    return "blue";
                case Engine::TerrainHeightmapChannel::Alpha:
                    return "alpha";
                case Engine::TerrainHeightmapChannel::Average:
                    return "average";
            }
            return "red";
        }

        bool readHeightmapChannel(
            const YAML::Node& node,
            Engine::TerrainHeightmapChannel& channel,
            std::vector<std::string>& errors)
        {
            if (!node) {
                return true;
            }
            const std::string value = node.as<std::string>();
            if (value == "red") {
                channel = Engine::TerrainHeightmapChannel::Red;
            } else if (value == "green") {
                channel = Engine::TerrainHeightmapChannel::Green;
            } else if (value == "blue") {
                channel = Engine::TerrainHeightmapChannel::Blue;
            } else if (value == "alpha") {
                channel = Engine::TerrainHeightmapChannel::Alpha;
            } else if (value == "average") {
                channel = Engine::TerrainHeightmapChannel::Average;
            } else {
                errors.push_back("Unknown terrain heightmap channel '" + value + "'.");
                return false;
            }
            return true;
        }

        const char* cameraModeName(Engine::CameraMode mode)
        {
            return mode == Engine::CameraMode::FollowTarget ? "follow_target" : "free";
        }

        bool readCameraMode(
            const YAML::Node& node,
            Engine::CameraMode& mode,
            std::vector<std::string>& errors)
        {
            if (!node) {
                return true;
            }
            const std::string value = node.as<std::string>();
            if (value == "free") {
                mode = Engine::CameraMode::Free;
            } else if (value == "follow_target") {
                mode = Engine::CameraMode::FollowTarget;
            } else {
                errors.push_back("Unknown camera mode '" + value + "'.");
                return false;
            }
            return true;
        }

        YAML::Node residencyPolicyNode(const Engine::StreamingPayloadResidencyPolicy& policy)
        {
            YAML::Node node;
            node["active_radius"] = policy.activeRadius;
            node["cache_radius"] = policy.cacheRadius;
            node["hysteresis"] = policy.hysteresis;
            node["max_transitions_per_frame"] = policy.maxTransitionsPerFrame;
            node["live_allowed"] = policy.liveAllowed;
            node["predictive_live_allowed"] = policy.predictiveLiveAllowed;
            return node;
        }

        void readResidencyPolicy(const YAML::Node& node, Engine::StreamingPayloadResidencyPolicy& policy)
        {
            if (!node || !node.IsMap()) {
                return;
            }
            policy.activeRadius = node["active_radius"].as<float>(policy.activeRadius);
            policy.cacheRadius = node["cache_radius"].as<float>(policy.cacheRadius);
            policy.hysteresis = node["hysteresis"].as<float>(policy.hysteresis);
            policy.maxTransitionsPerFrame =
                node["max_transitions_per_frame"].as<uint32_t>(policy.maxTransitionsPerFrame);
            policy.liveAllowed = node["live_allowed"].as<bool>(policy.liveAllowed);
            policy.predictiveLiveAllowed =
                node["predictive_live_allowed"].as<bool>(policy.predictiveLiveAllowed);
        }

        void applyPolicyToAllPayloads(
            Engine::StreamingHaloPlannerSettings& planner,
            const Engine::StreamingPayloadResidencyPolicy& policy)
        {
            for (Engine::StreamingPayloadResidencyPolicy& payloadPolicy : planner.payloadPolicies) {
                payloadPolicy = policy;
            }
            for (auto& payloadProfiles : planner.profilePolicies) {
                for (Engine::StreamingPayloadResidencyPolicy& profilePolicy : payloadProfiles) {
                    profilePolicy = policy;
                }
            }
        }

        void applyProfilePolicyToAllPayloads(
            Engine::StreamingHaloPlannerSettings& planner,
            Engine::StreamingHaloProfile profile,
            const Engine::StreamingPayloadResidencyPolicy& policy)
        {
            const uint32_t profileIndex = static_cast<uint32_t>(profile);
            for (auto& payloadProfiles : planner.profilePolicies) {
                payloadProfiles[profileIndex] = policy;
            }
        }

        void readStreamingResidency(
            const YAML::Node& node,
            Engine::StreamingHaloPlannerSettings& planner)
        {
            if (!node || !node.IsMap()) {
                return;
            }
            Engine::StreamingPayloadResidencyPolicy defaultPolicy = planner.payloadPolicies[0];
            readResidencyPolicy(node["default"], defaultPolicy);
            applyPolicyToAllPayloads(planner, defaultPolicy);

            Engine::StreamingPayloadResidencyPolicy highDetail = defaultPolicy;
            highDetail.activeRadius = 96.0f;
            highDetail.cacheRadius = 192.0f;
            readResidencyPolicy(node["high_detail_live"], highDetail);
            applyProfilePolicyToAllPayloads(planner, Engine::StreamingHaloProfile::HighDetailLive, highDetail);

            Engine::StreamingPayloadResidencyPolicy cacheOnly = defaultPolicy;
            cacheOnly.liveAllowed = false;
            readResidencyPolicy(node["cache_only"], cacheOnly);
            applyProfilePolicyToAllPayloads(planner, Engine::StreamingHaloProfile::CacheOnly, cacheOnly);

            Engine::StreamingPayloadResidencyPolicy farMetadata = defaultPolicy;
            farMetadata.cacheRadius = 512.0f;
            farMetadata.liveAllowed = false;
            readResidencyPolicy(node["far_metadata"], farMetadata);
            applyProfilePolicyToAllPayloads(planner, Engine::StreamingHaloProfile::FarMetadata, farMetadata);
        }

        YAML::Node streamingResidencyNode(const Engine::StreamingHaloPlannerSettings& planner)
        {
            YAML::Node node;
            node["default"] = residencyPolicyNode(planner.payloadPolicies[0]);
            node["high_detail_live"] = residencyPolicyNode(
                planner.profilePolicies[0][static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)]);
            node["cache_only"] = residencyPolicyNode(
                planner.profilePolicies[0][static_cast<uint32_t>(Engine::StreamingHaloProfile::CacheOnly)]);
            node["far_metadata"] = residencyPolicyNode(
                planner.profilePolicies[0][static_cast<uint32_t>(Engine::StreamingHaloProfile::FarMetadata)]);
            return node;
        }

        YAML::Node settingsNode(const EditorProjectSettings& settings)
        {
            YAML::Node root;
            root["schema_version"] = settings.schemaVersion;
            root["project_name"] = settings.projectName;

            const Engine::OpenWorldStreamingRuntimeSettings& streaming = settings.streaming;
            YAML::Node terrain;
            terrain["heightmap"]["source_path"] = streaming.bake.heightmap.sourcePath.generic_string();
            terrain["heightmap"]["channel"] = heightmapChannelName(streaming.bake.heightmap.channel);
            terrain["heightmap"]["sample_spacing"] = streaming.bake.heightmap.sampleSpacing;
            terrain["heightmap"]["height_scale"] = streaming.bake.heightmap.heightScale;
            terrain["heightmap"]["height_offset"] = streaming.bake.heightmap.heightOffset;
            terrain["heightmap"]["source_origin"] = vec3Node(streaming.bake.heightmap.sourceOrigin);
            terrain["heightmap"]["flip_rows"] = streaming.bake.heightmap.flipRows;
            terrain["heightmap"]["flip_columns"] = streaming.bake.heightmap.flipColumns;
            terrain["heightmap"]["chunk_world_size"] = streaming.bake.heightmap.chunkWorldSize;
            terrain["heightmap"]["chunk_resolution"] = streaming.bake.heightmap.chunkResolution;
            terrain["cache"]["root_path"] = streaming.bake.terrainCache.rootPath.generic_string();
            terrain["cache"]["format_version"] = streaming.bake.terrainCache.formatVersion;
            terrain["cache"]["terrain_import_version"] = streaming.bake.terrainCache.terrainImportVersion;
            terrain["cache"]["chunk_payload_version"] = streaming.bake.terrainCache.chunkPayloadVersion;
            terrain["cache"]["lod_mesh_payload_version"] = streaming.bake.terrainCache.lodMeshPayloadVersion;
            terrain["cache"]["physics_collider_payload_version"] =
                streaming.bake.terrainCache.physicsColliderPayloadVersion;
            terrain["cache"]["policy"] = cachePolicyName(streaming.bake.terrainCache.policy);
            for (const Engine::TerrainLodMeshBuildSettings& lod : streaming.bake.renderLods) {
                YAML::Node lodNode;
                lodNode["lod_index"] = lod.lodIndex;
                lodNode["render_resolution"] = lod.renderResolution;
                lodNode["skirt_depth"] = lod.skirtDepth;
                terrain["render_lods"].push_back(lodNode);
            }
            root["terrain"] = terrain;

            YAML::Node navigation;
            navigation["cache"]["root_path"] = streaming.bake.navigationCache.rootPath.generic_string();
            navigation["cache"]["world_id"] = streaming.bake.navigationCache.worldId;
            navigation["cache"]["format_version"] = streaming.bake.navigationCache.formatVersion;
            navigation["agent"]["radius"] = streaming.bake.navAgent.radius;
            navigation["agent"]["height"] = streaming.bake.navAgent.height;
            navigation["agent"]["max_slope_degrees"] = streaming.bake.navAgent.maxSlopeDegrees;
            navigation["agent"]["max_climb"] = streaming.bake.navAgent.maxClimb;
            navigation["build"]["cell_size"] = streaming.bake.navBuild.cellSize;
            navigation["build"]["cell_height"] = streaming.bake.navBuild.cellHeight;
            navigation["build"]["tile_border_size"] = streaming.bake.navBuild.tileBorderSize;
            navigation["build"]["max_tiles"] = streaming.bake.navBuild.maxTiles;
            navigation["build"]["max_polys_per_tile"] = streaming.bake.navBuild.maxPolysPerTile;
            navigation["build"]["max_verts_per_poly"] = streaming.bake.navBuild.maxVertsPerPoly;
            navigation["build"]["region_min_size"] = streaming.bake.navBuild.regionMinSize;
            navigation["build"]["region_merge_size"] = streaming.bake.navBuild.regionMergeSize;
            navigation["build"]["edge_max_len"] = streaming.bake.navBuild.edgeMaxLen;
            navigation["build"]["edge_max_error"] = streaming.bake.navBuild.edgeMaxError;
            navigation["build"]["detail_sample_dist"] = streaming.bake.navBuild.detailSampleDist;
            navigation["build"]["detail_sample_max_error"] = streaming.bake.navBuild.detailSampleMaxError;
            navigation["terrain"]["navigation_resolution"] = streaming.bake.navigationResolution;
            navigation["terrain"]["border_padding_world"] = streaming.bake.terrainNavigationBorderPaddingWorld;
            navigation["terrain"]["border_sample_count"] = streaming.bake.terrainNavigationBorderSampleCount;
            navigation["scene_geometry"]["max_walkable_slope_degrees"] =
                streaming.bake.sceneGeometryMaxSlopeDegrees;
            navigation["scene_geometry"]["tile_bounds_padding"] = streaming.bake.sceneGeometryTileBoundsPadding;
            navigation["scene_geometry"]["adapter_version"] = streaming.bake.sceneGeometryAdapterVersion;
            navigation["profile_id"] = streaming.bake.navigationProfileId;
            root["navigation"] = navigation;

            root["physics"]["collider_resolution"] = streaming.bake.physicsColliderResolution;

            YAML::Node streamingNode;
            streamingNode["saved_build_manifest_path"] = streaming.savedBuildManifestPath.generic_string();
            streamingNode["validate_payload_files"] = streaming.validatePayloadFiles;
            streamingNode["rebuild_when_stale"] = streaming.rebuildWhenStale;
            streamingNode["residency"] = streamingResidencyNode(streaming.planner);
            streamingNode["cache"]["max_read_jobs_queued_per_update"] = streaming.cache.maxReadJobsQueuedPerUpdate;
            streamingNode["cache"]["max_completed_jobs_merged_per_update"] =
                streaming.cache.maxCompletedJobsMergedPerUpdate;
            streamingNode["generation"]["policy"] = generationPolicyName(streaming.generation.policy);
            streamingNode["generation"]["max_generation_jobs_queued_per_update"] =
                streaming.generation.maxGenerationJobsQueuedPerUpdate;
            streamingNode["generation"]["max_completed_jobs_merged_per_update"] =
                streaming.generation.maxCompletedJobsMergedPerUpdate;
            streamingNode["promotion"]["max_promotes_queued_per_update"] =
                streaming.promotion.maxPromotesQueuedPerUpdate;
            streamingNode["promotion"]["max_demotes_queued_per_update"] =
                streaming.promotion.maxDemotesQueuedPerUpdate;
            root["streaming"] = streamingNode;

            YAML::Node renderer;
            renderer["layer_mask"] = settings.renderer.layerMask;
            renderer["enable_distance_culling"] = settings.renderer.enableDistanceCulling;
            renderer["prop_max_draw_distance"] = settings.renderer.propMaxDrawDistance;
            renderer["terrain_max_draw_distance"] = settings.renderer.terrainMaxDrawDistance;
            root["renderer"] = renderer;

            YAML::Node debugDraw;
            debugDraw["enabled"] = settings.debugDraw.enabled;
            debugDraw["selected_bounds"] = settings.debugDraw.selectedBounds;
            debugDraw["collision_bounds"] = settings.debugDraw.collisionBounds;
            debugDraw["chunk_borders"] = settings.debugDraw.chunkBorders;
            debugDraw["terrain_tile_bounds"] = settings.debugDraw.terrainTileBounds;
            debugDraw["terrain_slope_warnings"] = settings.debugDraw.terrainSlopeWarnings;
            debugDraw["navigation_tile_bounds"] = settings.debugDraw.navigationTileBounds;
            debugDraw["navigation_mesh_edges"] = settings.debugDraw.navigationMeshEdges;
            debugDraw["navigation_current_path"] = settings.debugDraw.navigationCurrentPath;
            debugDraw["navigation_nearest_point"] = settings.debugDraw.navigationNearestPoint;
            debugDraw["navigation_blocker_bounds"] = settings.debugDraw.navigationBlockerBounds;
            debugDraw["navigation_portals"] = settings.debugDraw.navigationPortals;
            debugDraw["navigation_connectivity_links"] = settings.debugDraw.navigationConnectivityLinks;
            debugDraw["camera_frustum"] = settings.debugDraw.cameraFrustum;
            debugDraw["actor_destination"] = settings.debugDraw.actorDestination;
            debugDraw["collider_shapes"] = settings.debugDraw.colliderShapes;
            debugDraw["max_debug_lines"] = settings.debugDraw.maxDebugLines;
            debugDraw["max_nav_mesh_edge_lines"] = settings.debugDraw.maxNavMeshEdgeLines;
            debugDraw["max_collider_shape_lines"] = settings.debugDraw.maxColliderShapeLines;
            debugDraw["max_world_graph_edge_lines"] = settings.debugDraw.maxWorldGraphEdgeLines;
            debugDraw["max_terrain_slope_warning_lines"] = settings.debugDraw.maxTerrainSlopeWarningLines;
            debugDraw["max_collision_aabbs"] = settings.debugDraw.maxCollisionAabbs;
            debugDraw["max_chunk_border_rects"] = settings.debugDraw.maxChunkBorderRects;
            root["debug_draw"] = debugDraw;

            YAML::Node camera;
            camera["far_plane"] = settings.camera.settings.farPlane;
            camera["max_distance"] = settings.camera.settings.maxDistance;
            camera["keyboard_pan_speed"] = settings.camera.settings.keyboardPanSpeed;
            camera["edge_pan_speed"] = settings.camera.settings.edgePanSpeed;
            camera["mouse_pan_sensitivity"] = settings.camera.settings.mousePanSensitivity;
            camera["rotation_sensitivity"] = settings.camera.settings.rotationSensitivity;
            camera["zoom_sensitivity"] = settings.camera.settings.zoomSensitivity;
            camera["edge_scroll_margin_pixels"] = settings.camera.settings.edgeScrollMarginPixels;
            camera["enable_keyboard_pan"] = settings.camera.settings.enableKeyboardPan;
            camera["enable_edge_pan"] = settings.camera.settings.enableEdgePan;
            camera["enable_mouse_pan"] = settings.camera.settings.enableMousePan;
            camera["enable_mouse_rotate"] = settings.camera.settings.enableMouseRotate;
            camera["enable_zoom"] = settings.camera.settings.enableZoom;
            camera["follow_smoothing"] = settings.camera.follow.followSmoothing;
            camera["max_follow_lag"] = settings.camera.follow.maxFollowLag;
            camera["allow_manual_follow_offset"] = settings.camera.follow.allowManualFollowOffset;
            camera["pivot_offset_from_focus"] = vec3Node(settings.camera.pivotOffsetFromFocus);
            camera["yaw_radians"] = settings.camera.yawRadians;
            camera["pitch_radians"] = settings.camera.pitchRadians;
            camera["distance"] = settings.camera.distance;
            camera["mode"] = cameraModeName(settings.camera.mode);
            root["camera"] = camera;

            return root;
        }

        void readSettingsNode(
            const YAML::Node& root,
            EditorProjectSettings& settings,
            std::vector<std::string>& errors)
        {
            settings.schemaVersion = root["schema_version"].as<uint32_t>(settings.schemaVersion);
            settings.projectName = root["project_name"].as<std::string>(settings.projectName);

            Engine::OpenWorldStreamingRuntimeSettings& streaming = settings.streaming;
            const YAML::Node terrain = mapChild(root, "terrain");
            const YAML::Node heightmap = mapChild(terrain, "heightmap");
            if (heightmap) {
                streaming.bake.heightmap.sourcePath =
                    heightmap["source_path"].as<std::string>(streaming.bake.heightmap.sourcePath.generic_string());
                readHeightmapChannel(heightmap["channel"], streaming.bake.heightmap.channel, errors);
                streaming.bake.heightmap.sampleSpacing =
                    heightmap["sample_spacing"].as<float>(streaming.bake.heightmap.sampleSpacing);
                streaming.bake.heightmap.heightScale =
                    heightmap["height_scale"].as<float>(streaming.bake.heightmap.heightScale);
                streaming.bake.heightmap.heightOffset =
                    heightmap["height_offset"].as<float>(streaming.bake.heightmap.heightOffset);
                streaming.bake.heightmap.sourceOrigin =
                    readVec3(heightmap["source_origin"], streaming.bake.heightmap.sourceOrigin);
                streaming.bake.heightmap.flipRows = heightmap["flip_rows"].as<bool>(streaming.bake.heightmap.flipRows);
                streaming.bake.heightmap.flipColumns =
                    heightmap["flip_columns"].as<bool>(streaming.bake.heightmap.flipColumns);
                streaming.bake.heightmap.chunkWorldSize =
                    heightmap["chunk_world_size"].as<float>(streaming.bake.heightmap.chunkWorldSize);
                streaming.bake.heightmap.chunkResolution =
                    heightmap["chunk_resolution"].as<uint32_t>(streaming.bake.heightmap.chunkResolution);
            }
            const YAML::Node terrainCache = mapChild(terrain, "cache");
            if (terrainCache) {
                streaming.bake.terrainCache.rootPath =
                    terrainCache["root_path"].as<std::string>(streaming.bake.terrainCache.rootPath.generic_string());
                streaming.bake.terrainCache.formatVersion =
                    terrainCache["format_version"].as<uint32_t>(streaming.bake.terrainCache.formatVersion);
                streaming.bake.terrainCache.terrainImportVersion =
                    terrainCache["terrain_import_version"].as<std::string>(
                        streaming.bake.terrainCache.terrainImportVersion);
                streaming.bake.terrainCache.chunkPayloadVersion =
                    terrainCache["chunk_payload_version"].as<std::string>(
                        streaming.bake.terrainCache.chunkPayloadVersion);
                streaming.bake.terrainCache.lodMeshPayloadVersion =
                    terrainCache["lod_mesh_payload_version"].as<std::string>(
                        streaming.bake.terrainCache.lodMeshPayloadVersion);
                streaming.bake.terrainCache.physicsColliderPayloadVersion =
                    terrainCache["physics_collider_payload_version"].as<std::string>(
                        streaming.bake.terrainCache.physicsColliderPayloadVersion);
                readCachePolicy(terrainCache["policy"], streaming.bake.terrainCache.policy, errors);
            }
            if (const YAML::Node renderLods = mapChild(terrain, "render_lods"); renderLods && renderLods.IsSequence()) {
                streaming.bake.renderLods.clear();
                for (const YAML::Node& lodNode : renderLods) {
                    Engine::TerrainLodMeshBuildSettings lod;
                    lod.lodIndex = lodNode["lod_index"].as<uint32_t>(lod.lodIndex);
                    lod.renderResolution = lodNode["render_resolution"].as<uint32_t>(lod.renderResolution);
                    lod.skirtDepth = lodNode["skirt_depth"].as<float>(lod.skirtDepth);
                    streaming.bake.renderLods.push_back(lod);
                }
            }

            const YAML::Node navigation = mapChild(root, "navigation");
            if (const YAML::Node cache = mapChild(navigation, "cache")) {
                streaming.bake.navigationCache.rootPath =
                    cache["root_path"].as<std::string>(streaming.bake.navigationCache.rootPath.generic_string());
                streaming.bake.navigationCache.worldId =
                    cache["world_id"].as<std::string>(streaming.bake.navigationCache.worldId);
                streaming.bake.navigationCache.formatVersion =
                    cache["format_version"].as<uint32_t>(streaming.bake.navigationCache.formatVersion);
            }
            if (const YAML::Node agent = mapChild(navigation, "agent")) {
                streaming.bake.navAgent.radius = agent["radius"].as<float>(streaming.bake.navAgent.radius);
                streaming.bake.navAgent.height = agent["height"].as<float>(streaming.bake.navAgent.height);
                streaming.bake.navAgent.maxSlopeDegrees =
                    agent["max_slope_degrees"].as<float>(streaming.bake.navAgent.maxSlopeDegrees);
                streaming.bake.navAgent.maxClimb = agent["max_climb"].as<float>(streaming.bake.navAgent.maxClimb);
            }
            if (const YAML::Node build = mapChild(navigation, "build")) {
                streaming.bake.navBuild.cellSize = build["cell_size"].as<float>(streaming.bake.navBuild.cellSize);
                streaming.bake.navBuild.cellHeight = build["cell_height"].as<float>(streaming.bake.navBuild.cellHeight);
                streaming.bake.navBuild.tileBorderSize =
                    build["tile_border_size"].as<uint32_t>(streaming.bake.navBuild.tileBorderSize);
                streaming.bake.navBuild.maxTiles = build["max_tiles"].as<uint32_t>(streaming.bake.navBuild.maxTiles);
                streaming.bake.navBuild.maxPolysPerTile =
                    build["max_polys_per_tile"].as<uint32_t>(streaming.bake.navBuild.maxPolysPerTile);
                streaming.bake.navBuild.maxVertsPerPoly =
                    build["max_verts_per_poly"].as<uint32_t>(streaming.bake.navBuild.maxVertsPerPoly);
                streaming.bake.navBuild.regionMinSize =
                    build["region_min_size"].as<uint32_t>(streaming.bake.navBuild.regionMinSize);
                streaming.bake.navBuild.regionMergeSize =
                    build["region_merge_size"].as<uint32_t>(streaming.bake.navBuild.regionMergeSize);
                streaming.bake.navBuild.edgeMaxLen = build["edge_max_len"].as<float>(streaming.bake.navBuild.edgeMaxLen);
                streaming.bake.navBuild.edgeMaxError =
                    build["edge_max_error"].as<float>(streaming.bake.navBuild.edgeMaxError);
                streaming.bake.navBuild.detailSampleDist =
                    build["detail_sample_dist"].as<float>(streaming.bake.navBuild.detailSampleDist);
                streaming.bake.navBuild.detailSampleMaxError =
                    build["detail_sample_max_error"].as<float>(streaming.bake.navBuild.detailSampleMaxError);
            }
            if (const YAML::Node terrainNav = mapChild(navigation, "terrain")) {
                streaming.bake.navigationResolution =
                    terrainNav["navigation_resolution"].as<uint32_t>(streaming.bake.navigationResolution);
                streaming.bake.terrainNavigationBorderPaddingWorld =
                    terrainNav["border_padding_world"].as<float>(
                        streaming.bake.terrainNavigationBorderPaddingWorld);
                streaming.bake.terrainNavigationBorderSampleCount =
                    terrainNav["border_sample_count"].as<uint32_t>(
                        streaming.bake.terrainNavigationBorderSampleCount);
            }
            if (const YAML::Node sceneGeometry = mapChild(navigation, "scene_geometry")) {
                streaming.bake.sceneGeometryMaxSlopeDegrees =
                    sceneGeometry["max_walkable_slope_degrees"].as<float>(
                        streaming.bake.sceneGeometryMaxSlopeDegrees);
                streaming.bake.sceneGeometryTileBoundsPadding =
                    sceneGeometry["tile_bounds_padding"].as<float>(
                        streaming.bake.sceneGeometryTileBoundsPadding);
                streaming.bake.sceneGeometryAdapterVersion =
                    sceneGeometry["adapter_version"].as<std::string>(
                        streaming.bake.sceneGeometryAdapterVersion);
            }
            streaming.bake.navigationProfileId =
                mapChild(navigation, "profile_id").as<std::string>(streaming.bake.navigationProfileId);

            const YAML::Node physics = mapChild(root, "physics");
            streaming.bake.physicsColliderResolution =
                mapChild(physics, "collider_resolution").as<uint32_t>(streaming.bake.physicsColliderResolution);

            if (const YAML::Node streamingNode = mapChild(root, "streaming")) {
                streaming.savedBuildManifestPath =
                    streamingNode["saved_build_manifest_path"].as<std::string>(
                        streaming.savedBuildManifestPath.generic_string());
                streaming.validatePayloadFiles =
                    streamingNode["validate_payload_files"].as<bool>(streaming.validatePayloadFiles);
                streaming.rebuildWhenStale = streamingNode["rebuild_when_stale"].as<bool>(streaming.rebuildWhenStale);
                readStreamingResidency(streamingNode["residency"], streaming.planner);
                if (const YAML::Node cache = mapChild(streamingNode, "cache")) {
                    streaming.cache.maxReadJobsQueuedPerUpdate =
                        cache["max_read_jobs_queued_per_update"].as<uint32_t>(
                            streaming.cache.maxReadJobsQueuedPerUpdate);
                    streaming.cache.maxCompletedJobsMergedPerUpdate =
                        cache["max_completed_jobs_merged_per_update"].as<uint32_t>(
                            streaming.cache.maxCompletedJobsMergedPerUpdate);
                }
                if (const YAML::Node generation = mapChild(streamingNode, "generation")) {
                    readGenerationPolicy(generation["policy"], streaming.generation.policy, errors);
                    streaming.generation.maxGenerationJobsQueuedPerUpdate =
                        generation["max_generation_jobs_queued_per_update"].as<uint32_t>(
                            streaming.generation.maxGenerationJobsQueuedPerUpdate);
                    streaming.generation.maxCompletedJobsMergedPerUpdate =
                        generation["max_completed_jobs_merged_per_update"].as<uint32_t>(
                            streaming.generation.maxCompletedJobsMergedPerUpdate);
                }
                if (const YAML::Node promotion = mapChild(streamingNode, "promotion")) {
                    streaming.promotion.maxPromotesQueuedPerUpdate =
                        promotion["max_promotes_queued_per_update"].as<uint32_t>(
                            streaming.promotion.maxPromotesQueuedPerUpdate);
                    streaming.promotion.maxDemotesQueuedPerUpdate =
                        promotion["max_demotes_queued_per_update"].as<uint32_t>(
                            streaming.promotion.maxDemotesQueuedPerUpdate);
                }
            }

            if (const YAML::Node renderer = mapChild(root, "renderer")) {
                settings.renderer.layerMask = renderer["layer_mask"].as<uint32_t>(settings.renderer.layerMask);
                settings.renderer.enableDistanceCulling =
                    renderer["enable_distance_culling"].as<bool>(settings.renderer.enableDistanceCulling);
                settings.renderer.propMaxDrawDistance =
                    renderer["prop_max_draw_distance"].as<float>(settings.renderer.propMaxDrawDistance);
                settings.renderer.terrainMaxDrawDistance =
                    renderer["terrain_max_draw_distance"].as<float>(settings.renderer.terrainMaxDrawDistance);
            }

            if (const YAML::Node debugDraw = mapChild(root, "debug_draw")) {
                settings.debugDraw.enabled = debugDraw["enabled"].as<bool>(settings.debugDraw.enabled);
                settings.debugDraw.selectedBounds =
                    debugDraw["selected_bounds"].as<bool>(settings.debugDraw.selectedBounds);
                settings.debugDraw.collisionBounds =
                    debugDraw["collision_bounds"].as<bool>(settings.debugDraw.collisionBounds);
                settings.debugDraw.chunkBorders = debugDraw["chunk_borders"].as<bool>(settings.debugDraw.chunkBorders);
                settings.debugDraw.terrainTileBounds =
                    debugDraw["terrain_tile_bounds"].as<bool>(settings.debugDraw.terrainTileBounds);
                settings.debugDraw.terrainSlopeWarnings =
                    debugDraw["terrain_slope_warnings"].as<bool>(settings.debugDraw.terrainSlopeWarnings);
                settings.debugDraw.navigationTileBounds =
                    debugDraw["navigation_tile_bounds"].as<bool>(settings.debugDraw.navigationTileBounds);
                settings.debugDraw.navigationMeshEdges =
                    debugDraw["navigation_mesh_edges"].as<bool>(settings.debugDraw.navigationMeshEdges);
                settings.debugDraw.navigationCurrentPath =
                    debugDraw["navigation_current_path"].as<bool>(settings.debugDraw.navigationCurrentPath);
                settings.debugDraw.navigationNearestPoint =
                    debugDraw["navigation_nearest_point"].as<bool>(settings.debugDraw.navigationNearestPoint);
                settings.debugDraw.navigationBlockerBounds =
                    debugDraw["navigation_blocker_bounds"].as<bool>(settings.debugDraw.navigationBlockerBounds);
                settings.debugDraw.navigationPortals =
                    debugDraw["navigation_portals"].as<bool>(settings.debugDraw.navigationPortals);
                settings.debugDraw.navigationConnectivityLinks =
                    debugDraw["navigation_connectivity_links"].as<bool>(
                        settings.debugDraw.navigationConnectivityLinks);
                settings.debugDraw.cameraFrustum =
                    debugDraw["camera_frustum"].as<bool>(settings.debugDraw.cameraFrustum);
                settings.debugDraw.actorDestination =
                    debugDraw["actor_destination"].as<bool>(settings.debugDraw.actorDestination);
                settings.debugDraw.colliderShapes =
                    debugDraw["collider_shapes"].as<bool>(settings.debugDraw.colliderShapes);
                settings.debugDraw.maxDebugLines =
                    debugDraw["max_debug_lines"].as<uint32_t>(settings.debugDraw.maxDebugLines);
                settings.debugDraw.maxNavMeshEdgeLines =
                    debugDraw["max_nav_mesh_edge_lines"].as<uint32_t>(
                        settings.debugDraw.maxNavMeshEdgeLines);
                settings.debugDraw.maxColliderShapeLines =
                    debugDraw["max_collider_shape_lines"].as<uint32_t>(
                        settings.debugDraw.maxColliderShapeLines);
                settings.debugDraw.maxWorldGraphEdgeLines =
                    debugDraw["max_world_graph_edge_lines"].as<uint32_t>(
                        settings.debugDraw.maxWorldGraphEdgeLines);
                settings.debugDraw.maxTerrainSlopeWarningLines =
                    debugDraw["max_terrain_slope_warning_lines"].as<uint32_t>(
                        settings.debugDraw.maxTerrainSlopeWarningLines);
                settings.debugDraw.maxCollisionAabbs =
                    debugDraw["max_collision_aabbs"].as<uint32_t>(settings.debugDraw.maxCollisionAabbs);
                settings.debugDraw.maxChunkBorderRects =
                    debugDraw["max_chunk_border_rects"].as<uint32_t>(settings.debugDraw.maxChunkBorderRects);
            }

            if (const YAML::Node camera = mapChild(root, "camera")) {
                settings.camera.settings.farPlane = camera["far_plane"].as<float>(settings.camera.settings.farPlane);
                settings.camera.settings.maxDistance =
                    camera["max_distance"].as<float>(settings.camera.settings.maxDistance);
                settings.camera.settings.keyboardPanSpeed =
                    camera["keyboard_pan_speed"].as<float>(settings.camera.settings.keyboardPanSpeed);
                settings.camera.settings.edgePanSpeed =
                    camera["edge_pan_speed"].as<float>(settings.camera.settings.edgePanSpeed);
                settings.camera.settings.mousePanSensitivity =
                    camera["mouse_pan_sensitivity"].as<float>(settings.camera.settings.mousePanSensitivity);
                settings.camera.settings.rotationSensitivity =
                    camera["rotation_sensitivity"].as<float>(settings.camera.settings.rotationSensitivity);
                settings.camera.settings.zoomSensitivity =
                    camera["zoom_sensitivity"].as<float>(settings.camera.settings.zoomSensitivity);
                settings.camera.settings.edgeScrollMarginPixels =
                    camera["edge_scroll_margin_pixels"].as<int>(settings.camera.settings.edgeScrollMarginPixels);
                settings.camera.settings.enableKeyboardPan =
                    camera["enable_keyboard_pan"].as<bool>(settings.camera.settings.enableKeyboardPan);
                settings.camera.settings.enableEdgePan =
                    camera["enable_edge_pan"].as<bool>(settings.camera.settings.enableEdgePan);
                settings.camera.settings.enableMousePan =
                    camera["enable_mouse_pan"].as<bool>(settings.camera.settings.enableMousePan);
                settings.camera.settings.enableMouseRotate =
                    camera["enable_mouse_rotate"].as<bool>(settings.camera.settings.enableMouseRotate);
                settings.camera.settings.enableZoom =
                    camera["enable_zoom"].as<bool>(settings.camera.settings.enableZoom);
                settings.camera.follow.followSmoothing =
                    camera["follow_smoothing"].as<float>(settings.camera.follow.followSmoothing);
                settings.camera.follow.maxFollowLag =
                    camera["max_follow_lag"].as<float>(settings.camera.follow.maxFollowLag);
                settings.camera.follow.allowManualFollowOffset =
                    camera["allow_manual_follow_offset"].as<bool>(settings.camera.follow.allowManualFollowOffset);
                settings.camera.pivotOffsetFromFocus =
                    readVec3(camera["pivot_offset_from_focus"], settings.camera.pivotOffsetFromFocus);
                settings.camera.yawRadians = camera["yaw_radians"].as<float>(settings.camera.yawRadians);
                settings.camera.pitchRadians = camera["pitch_radians"].as<float>(settings.camera.pitchRadians);
                settings.camera.distance = camera["distance"].as<float>(settings.camera.distance);
                readCameraMode(camera["mode"], settings.camera.mode, errors);
            }
        }

        void requirePositive(
            bool condition,
            std::vector<std::string>& errors,
            std::string message)
        {
            if (!condition) {
                errors.push_back(std::move(message));
            }
        }

        void validateResidencyPolicy(
            const Engine::StreamingPayloadResidencyPolicy& policy,
            std::vector<std::string>& errors,
            std::string_view label)
        {
            const std::string prefix(label);
            requirePositive(policy.activeRadius >= 0.0f, errors, prefix + " active radius must be non-negative.");
            requirePositive(policy.cacheRadius >= 0.0f, errors, prefix + " cache radius must be non-negative.");
            requirePositive(policy.hysteresis >= 0.0f, errors, prefix + " hysteresis must be non-negative.");
            requirePositive(
                policy.maxTransitionsPerFrame > 0,
                errors,
                prefix + " max transitions per frame must be greater than zero.");
        }
    }

    EditorProjectSettings defaultEditorProjectSettings()
    {
        EditorProjectSettings settings;
        settings.schemaVersion = EditorProjectSettingsSchemaVersion;
        settings.projectName = "ManualEngine Default";

        settings.streaming.savedBuildManifestPath = "generated/open_world_streaming/modern_default/manifest.yaml";
        settings.streaming.bake.heightmap.sourcePath =
            "assets/heightmaps/47_648_-122_332_13_505_505_16bit.png";
        settings.streaming.bake.heightmap.sampleSpacing = 1.0f;
        settings.streaming.bake.heightmap.heightScale = 80.0f;
        settings.streaming.bake.heightmap.heightOffset = 0.0f;
        settings.streaming.bake.heightmap.sourceOrigin = {-256.0f, 0.0f, 256.0f};
        settings.streaming.bake.heightmap.chunkWorldSize = 64.0f;
        settings.streaming.bake.heightmap.chunkResolution = 33;
        settings.streaming.bake.terrainCache.rootPath = "generated/terrain_cache/modern_default";
        settings.streaming.bake.terrainCache.policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
        settings.streaming.bake.navigationCache.rootPath = "generated/navigation_cache/modern_default";
        settings.streaming.bake.navigationCache.worldId = "modern_default_streaming";
        settings.streaming.bake.navigationResolution = 17;
        settings.streaming.bake.physicsColliderResolution = 17;
        settings.streaming.bake.renderLods = {
            {0, 33, 2.0f},
            {1, 17, 2.0f},
        };
        settings.streaming.bake.navAgent = {};
        settings.streaming.bake.terrainNavigationBorderPaddingWorld = std::max(
            settings.streaming.bake.navAgent.radius * 2.0f,
            settings.streaming.bake.navBuild.cellSize * 4.0f);
        settings.streaming.bake.terrainNavigationBorderSampleCount = 1;
        settings.streaming.bake.sceneGeometryMaxSlopeDegrees =
            settings.streaming.bake.navAgent.maxSlopeDegrees;
        settings.streaming.bake.sceneGeometryTileBoundsPadding =
            settings.streaming.bake.navAgent.radius +
            settings.streaming.bake.terrainNavigationBorderPaddingWorld;
        settings.streaming.bake.sceneGeometryAdapterVersion = "modern_scene_nav_geometry_slope_v1";

        Engine::StreamingPayloadResidencyPolicy defaultPolicy;
        defaultPolicy.activeRadius = 192.0f;
        defaultPolicy.cacheRadius = 320.0f;
        defaultPolicy.hysteresis = 32.0f;
        defaultPolicy.maxTransitionsPerFrame = 4;
        applyPolicyToAllPayloads(settings.streaming.planner, defaultPolicy);

        Engine::StreamingPayloadResidencyPolicy highDetail = defaultPolicy;
        highDetail.activeRadius = 96.0f;
        highDetail.cacheRadius = 192.0f;
        applyProfilePolicyToAllPayloads(
            settings.streaming.planner,
            Engine::StreamingHaloProfile::HighDetailLive,
            highDetail);

        Engine::StreamingPayloadResidencyPolicy cacheOnly = defaultPolicy;
        cacheOnly.liveAllowed = false;
        applyProfilePolicyToAllPayloads(
            settings.streaming.planner,
            Engine::StreamingHaloProfile::CacheOnly,
            cacheOnly);

        Engine::StreamingPayloadResidencyPolicy farMetadata = defaultPolicy;
        farMetadata.cacheRadius = 512.0f;
        farMetadata.liveAllowed = false;
        applyProfilePolicyToAllPayloads(
            settings.streaming.planner,
            Engine::StreamingHaloProfile::FarMetadata,
            farMetadata);

        settings.streaming.cache.maxReadJobsQueuedPerUpdate = 8;
        settings.streaming.cache.maxCompletedJobsMergedPerUpdate = 16;
        settings.streaming.generation.policy = Engine::StreamingDerivedGenerationPolicy::ReadOnly;
        settings.streaming.promotion.maxPromotesQueuedPerUpdate = 8;
        settings.streaming.promotion.maxDemotesQueuedPerUpdate = 8;

        settings.camera.settings.farPlane = 1200.0f;
        settings.camera.settings.maxDistance = 420.0f;
        settings.camera.settings.keyboardPanSpeed = 55.0f;
        settings.camera.settings.edgePanSpeed = 70.0f;
        settings.camera.settings.mousePanSensitivity = 0.12f;
        settings.camera.settings.zoomSensitivity = 28.0f;
        settings.camera.pivotOffsetFromFocus = {0.0f, 12.0f, 0.0f};
        settings.camera.yawRadians = glm::radians(35.0f);
        settings.camera.pitchRadians = glm::radians(-46.0f);
        settings.camera.distance = 170.0f;
        settings.camera.mode = Engine::CameraMode::Free;
        return settings;
    }

    EditorProjectSettingsValidationResult validateEditorProjectSettings(
        const EditorProjectSettings& settings)
    {
        EditorProjectSettingsValidationResult result;
        if (settings.schemaVersion == 0) {
            result.errors.push_back("schema_version must be greater than zero.");
        }
        if (settings.projectName.empty()) {
            result.errors.push_back("project_name must not be empty.");
        }
        const Engine::OpenWorldStreamingRuntimeSettings& streaming = settings.streaming;
        requirePositive(!streaming.savedBuildManifestPath.empty(), result.errors, "saved build manifest path is empty.");
        requirePositive(!streaming.bake.heightmap.sourcePath.empty(), result.errors, "heightmap source path is empty.");
        requirePositive(
            streaming.bake.heightmap.sampleSpacing > 0.0f,
            result.errors,
            "heightmap sample spacing must be greater than zero.");
        requirePositive(
            streaming.bake.heightmap.chunkWorldSize > 0.0f,
            result.errors,
            "heightmap chunk world size must be greater than zero.");
        requirePositive(
            streaming.bake.heightmap.chunkResolution >= 2,
            result.errors,
            "heightmap chunk resolution must be at least 2.");
        requirePositive(!streaming.bake.terrainCache.rootPath.empty(), result.errors, "terrain cache root is empty.");
        requirePositive(!streaming.bake.navigationCache.rootPath.empty(), result.errors, "navigation cache root is empty.");
        requirePositive(!streaming.bake.navigationCache.worldId.empty(), result.errors, "navigation cache world id is empty.");
        requirePositive(
            !streaming.bake.renderLods.empty(),
            result.errors,
            "at least one terrain render LOD must be configured.");
        for (const Engine::TerrainLodMeshBuildSettings& lod : streaming.bake.renderLods) {
            requirePositive(lod.renderResolution >= 2, result.errors, "render LOD resolution must be at least 2.");
            requirePositive(lod.skirtDepth >= 0.0f, result.errors, "render LOD skirt depth must be non-negative.");
        }
        requirePositive(streaming.bake.navAgent.radius > 0.0f, result.errors, "nav agent radius must be positive.");
        requirePositive(streaming.bake.navAgent.height > 0.0f, result.errors, "nav agent height must be positive.");
        requirePositive(
            streaming.bake.navAgent.maxSlopeDegrees >= 0.0f && streaming.bake.navAgent.maxSlopeDegrees < 90.0f,
            result.errors,
            "nav agent max slope must be in [0, 90).");
        requirePositive(streaming.bake.navAgent.maxClimb >= 0.0f, result.errors, "nav agent max climb must be non-negative.");
        requirePositive(streaming.bake.navBuild.cellSize > 0.0f, result.errors, "nav build cell size must be positive.");
        requirePositive(streaming.bake.navBuild.cellHeight > 0.0f, result.errors, "nav build cell height must be positive.");
        requirePositive(streaming.bake.navBuild.maxTiles > 0, result.errors, "nav build max tiles must be positive.");
        requirePositive(
            streaming.bake.navBuild.maxPolysPerTile > 0,
            result.errors,
            "nav build max polys per tile must be positive.");
        requirePositive(
            streaming.bake.navBuild.maxVertsPerPoly >= 3 && streaming.bake.navBuild.maxVertsPerPoly <= 12,
            result.errors,
            "nav build max verts per poly must be in [3, 12].");
        requirePositive(
            streaming.bake.navigationResolution >= 2,
            result.errors,
            "terrain navigation resolution must be at least 2.");
        requirePositive(
            streaming.bake.physicsColliderResolution >= 2,
            result.errors,
            "physics collider resolution must be at least 2.");
        requirePositive(
            streaming.bake.sceneGeometryMaxSlopeDegrees >= 0.0f &&
                streaming.bake.sceneGeometryMaxSlopeDegrees < 90.0f,
            result.errors,
            "scene geometry max walkable slope must be in [0, 90).");
        requirePositive(
            streaming.bake.sceneGeometryTileBoundsPadding >= 0.0f,
            result.errors,
            "scene geometry tile bounds padding must be non-negative.");
        requirePositive(
            streaming.cache.maxReadJobsQueuedPerUpdate > 0,
            result.errors,
            "streaming max read jobs queued per update must be positive.");
        requirePositive(
            streaming.cache.maxCompletedJobsMergedPerUpdate > 0,
            result.errors,
            "streaming max completed read jobs merged per update must be positive.");
        requirePositive(
            streaming.promotion.maxPromotesQueuedPerUpdate > 0,
            result.errors,
            "streaming max promotes queued per update must be positive.");
        requirePositive(
            streaming.promotion.maxDemotesQueuedPerUpdate > 0,
            result.errors,
            "streaming max demotes queued per update must be positive.");
        validateResidencyPolicy(streaming.planner.payloadPolicies[0], result.errors, "default residency");
        validateResidencyPolicy(
            streaming.planner.profilePolicies[0][static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)],
            result.errors,
            "high detail residency");
        validateResidencyPolicy(
            streaming.planner.profilePolicies[0][static_cast<uint32_t>(Engine::StreamingHaloProfile::CacheOnly)],
            result.errors,
            "cache only residency");
        validateResidencyPolicy(
            streaming.planner.profilePolicies[0][static_cast<uint32_t>(Engine::StreamingHaloProfile::FarMetadata)],
            result.errors,
            "far metadata residency");

        requirePositive(settings.renderer.propMaxDrawDistance >= 0.0f, result.errors, "prop max draw distance must be non-negative.");
        requirePositive(settings.renderer.terrainMaxDrawDistance >= 0.0f, result.errors, "terrain max draw distance must be non-negative.");
        requirePositive(settings.debugDraw.maxDebugLines > 0, result.errors, "max debug lines must be positive.");
        requirePositive(settings.debugDraw.maxNavMeshEdgeLines > 0, result.errors, "max nav mesh edge lines must be positive.");
        requirePositive(settings.debugDraw.maxColliderShapeLines > 0, result.errors, "max collider shape lines must be positive.");
        requirePositive(settings.camera.settings.farPlane > settings.camera.settings.nearPlane, result.errors, "camera far plane must exceed near plane.");
        requirePositive(settings.camera.settings.maxDistance > 0.0f, result.errors, "camera max distance must be positive.");
        requirePositive(settings.camera.distance > 0.0f, result.errors, "camera distance must be positive.");
        requirePositive(settings.camera.follow.followSmoothing >= 0.0f, result.errors, "camera follow smoothing must be non-negative.");
        requirePositive(settings.camera.follow.maxFollowLag >= 0.0f, result.errors, "camera max follow lag must be non-negative.");

        result.valid = result.errors.empty();
        return result;
    }

    EditorProjectSettingsLoadResult loadEditorProjectSettings(
        const std::filesystem::path& path)
    {
        EditorProjectSettingsLoadResult result;
        result.settings = defaultEditorProjectSettings();
        try {
            if (!std::filesystem::exists(path)) {
                result.message = "Editor project profile was not found; using built-in defaults.";
                result.validation = validateEditorProjectSettings(result.settings);
                return result;
            }
            const YAML::Node root = YAML::LoadFile(path.string());
            std::vector<std::string> parseErrors;
            readSettingsNode(root, result.settings, parseErrors);
            result.validation = validateEditorProjectSettings(result.settings);
            result.validation.errors.insert(
                result.validation.errors.end(),
                parseErrors.begin(),
                parseErrors.end());
            result.validation.valid = result.validation.errors.empty();
            if (!result.validation.valid) {
                result.settings = defaultEditorProjectSettings();
                result.usedFallback = true;
                result.loaded = false;
                result.message = "Editor project profile was invalid; using built-in defaults.";
                return result;
            }
            result.loaded = true;
            result.usedFallback = false;
            result.message = "Loaded editor project profile '" + path.generic_string() + "'.";
            return result;
        } catch (const std::exception& exception) {
            result.settings = defaultEditorProjectSettings();
            result.validation = validateEditorProjectSettings(result.settings);
            result.loaded = false;
            result.usedFallback = true;
            result.message = std::string{"Failed to load editor project profile; using built-in defaults: "} +
                exception.what();
            return result;
        }
    }

    EditorProjectSettingsSaveResult saveEditorProjectSettings(
        const std::filesystem::path& path,
        const EditorProjectSettings& settings)
    {
        const EditorProjectSettingsValidationResult validation = validateEditorProjectSettings(settings);
        if (!validation.valid) {
            return {false, "Editor project settings are invalid; refusing to save."};
        }
        try {
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::ofstream file(path, std::ios::trunc);
            if (!file) {
                return {false, "Failed to open editor project profile for writing."};
            }
            file << settingsNode(settings);
            return {true, "Saved editor project profile '" + path.generic_string() + "'."};
        } catch (const std::exception& exception) {
            return {false, std::string{"Failed to save editor project profile: "} + exception.what()};
        }
    }

    Engine::OpenWorldStreamingRuntimeSettings streamingRuntimeSettingsFromEditorProject(
        const EditorProjectSettings& settings)
    {
        return settings.streaming;
    }

}
