#include "App/EditorSettingsReflection.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

namespace ManualEngine::App {
    namespace {
        using ObjectId = EditorSettingsReflectedObjectId;
        using PropertyId = EditorSettingsReflectedPropertyId;
        using Engine::ReflectedPropertyFlag;
        using Engine::ReflectedValueType;

        constexpr ReflectedPropertyFlag EditorFlag =
            ReflectedPropertyFlag::EditorVisible | ReflectedPropertyFlag::ScriptVisible;
        constexpr ReflectedPropertyFlag RebuildFlag =
            EditorFlag | ReflectedPropertyFlag::RequiresExplicitApply;
        constexpr ReflectedPropertyFlag AdvancedRebuildFlag =
            RebuildFlag | ReflectedPropertyFlag::Advanced;
        constexpr ReflectedPropertyFlag AdvancedEditorFlag =
            EditorFlag | ReflectedPropertyFlag::Advanced;
        constexpr ReflectedPropertyFlag ReadOnlyEditorFlag =
            EditorFlag | ReflectedPropertyFlag::ReadOnly;

        [[nodiscard]] Engine::ReflectionResult result(Engine::ReflectionStatus status, std::string message = {})
        {
            Engine::ReflectionResult out;
            out.status = status;
            out.message = std::move(message);
            return out;
        }

        [[nodiscard]] Engine::ReflectionResult valueResult(Engine::ReflectedValue value)
        {
            Engine::ReflectionResult out;
            out.value = std::move(value);
            return out;
        }

        [[nodiscard]] Engine::ReflectionResult changedResult(bool changed)
        {
            Engine::ReflectionResult out;
            out.changed = changed;
            return out;
        }

        template <typename T>
        [[nodiscard]] const T* as(const Engine::ReflectedValue& value)
        {
            return std::get_if<T>(&value);
        }

        [[nodiscard]] bool finite(float value)
        {
            return std::isfinite(value);
        }

        [[nodiscard]] Engine::ReflectedPropertyDescriptor prop(
            PropertyId id,
            std::string name,
            std::string displayName,
            std::string category,
            ReflectedValueType type,
            ReflectedPropertyFlag flags,
            Engine::ReflectedValue defaultValue,
            Engine::ReflectedValue minimum = {},
            Engine::ReflectedValue maximum = {},
            std::string units = {},
            std::vector<std::string> enumLabels = {},
            std::string documentation = {})
        {
            Engine::ReflectedPropertyDescriptor descriptor;
            descriptor.id = static_cast<uint32_t>(id);
            descriptor.name = std::move(name);
            descriptor.displayName = std::move(displayName);
            descriptor.category = std::move(category);
            descriptor.type = type;
            descriptor.flags = flags;
            descriptor.defaultValue = std::move(defaultValue);
            if (Engine::reflectedValueType(minimum) != ReflectedValueType::None) {
                descriptor.minimum = std::move(minimum);
            }
            if (Engine::reflectedValueType(maximum) != ReflectedValueType::None) {
                descriptor.maximum = std::move(maximum);
            }
            descriptor.units = std::move(units);
            descriptor.enumLabels = std::move(enumLabels);
            descriptor.documentation = std::move(documentation);
            return descriptor;
        }

        [[nodiscard]] Engine::ReflectedObjectDescriptor object(
            ObjectId id,
            std::string name,
            std::string displayName,
            std::string category,
            std::vector<Engine::ReflectedPropertyDescriptor> properties)
        {
            Engine::ReflectedObjectDescriptor descriptor;
            descriptor.id = static_cast<uint32_t>(id);
            descriptor.name = std::move(name);
            descriptor.displayName = std::move(displayName);
            descriptor.category = std::move(category);
            descriptor.properties = std::move(properties);
            return descriptor;
        }

        [[nodiscard]] std::string cachePolicyName(Engine::TerrainDerivedCachePolicy policy)
        {
            switch (policy) {
                case Engine::TerrainDerivedCachePolicy::Disabled: return "disabled";
                case Engine::TerrainDerivedCachePolicy::ReadOnly: return "read_only";
                case Engine::TerrainDerivedCachePolicy::GenerateOnMiss: return "generate_on_miss";
                case Engine::TerrainDerivedCachePolicy::Refresh: return "refresh";
            }
            return "disabled";
        }

        [[nodiscard]] bool parseCachePolicy(std::string_view value, Engine::TerrainDerivedCachePolicy& policy)
        {
            if (value == "disabled") policy = Engine::TerrainDerivedCachePolicy::Disabled;
            else if (value == "read_only") policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
            else if (value == "generate_on_miss") policy = Engine::TerrainDerivedCachePolicy::GenerateOnMiss;
            else if (value == "refresh") policy = Engine::TerrainDerivedCachePolicy::Refresh;
            else return false;
            return true;
        }

        [[nodiscard]] std::string generationPolicyName(Engine::StreamingDerivedGenerationPolicy policy)
        {
            switch (policy) {
                case Engine::StreamingDerivedGenerationPolicy::ReadOnly: return "read_only";
                case Engine::StreamingDerivedGenerationPolicy::GenerateOnMiss: return "generate_on_miss";
                case Engine::StreamingDerivedGenerationPolicy::Refresh: return "refresh";
            }
            return "read_only";
        }

        [[nodiscard]] bool parseGenerationPolicy(std::string_view value, Engine::StreamingDerivedGenerationPolicy& policy)
        {
            if (value == "read_only") policy = Engine::StreamingDerivedGenerationPolicy::ReadOnly;
            else if (value == "generate_on_miss") policy = Engine::StreamingDerivedGenerationPolicy::GenerateOnMiss;
            else if (value == "refresh") policy = Engine::StreamingDerivedGenerationPolicy::Refresh;
            else return false;
            return true;
        }

        [[nodiscard]] std::string heightmapChannelName(Engine::TerrainHeightmapChannel channel)
        {
            switch (channel) {
                case Engine::TerrainHeightmapChannel::Red: return "red";
                case Engine::TerrainHeightmapChannel::Green: return "green";
                case Engine::TerrainHeightmapChannel::Blue: return "blue";
                case Engine::TerrainHeightmapChannel::Alpha: return "alpha";
                case Engine::TerrainHeightmapChannel::Average: return "average";
            }
            return "red";
        }

        [[nodiscard]] bool parseHeightmapChannel(std::string_view value, Engine::TerrainHeightmapChannel& channel)
        {
            if (value == "red") channel = Engine::TerrainHeightmapChannel::Red;
            else if (value == "green") channel = Engine::TerrainHeightmapChannel::Green;
            else if (value == "blue") channel = Engine::TerrainHeightmapChannel::Blue;
            else if (value == "alpha") channel = Engine::TerrainHeightmapChannel::Alpha;
            else if (value == "average") channel = Engine::TerrainHeightmapChannel::Average;
            else return false;
            return true;
        }

        [[nodiscard]] std::string cameraModeName(Engine::CameraMode mode)
        {
            return mode == Engine::CameraMode::FollowTarget ? "follow_target" : "free";
        }

        [[nodiscard]] bool parseCameraMode(std::string_view value, Engine::CameraMode& mode)
        {
            if (value == "free") mode = Engine::CameraMode::Free;
            else if (value == "follow_target") mode = Engine::CameraMode::FollowTarget;
            else return false;
            return true;
        }

        [[nodiscard]] Engine::StreamingPayloadResidencyPolicy& defaultPolicy(EditorProjectSettings& settings)
        {
            return settings.streaming.planner.payloadPolicies[0];
        }

        [[nodiscard]] const Engine::StreamingPayloadResidencyPolicy& defaultPolicy(const EditorProjectSettings& settings)
        {
            return settings.streaming.planner.payloadPolicies[0];
        }

        void applyDefaultPolicyToAllPayloads(EditorProjectSettings& settings)
        {
            const Engine::StreamingPayloadResidencyPolicy policy = settings.streaming.planner.payloadPolicies[0];
            for (Engine::StreamingPayloadResidencyPolicy& payloadPolicy : settings.streaming.planner.payloadPolicies) {
                payloadPolicy = policy;
            }
            for (auto& profiles : settings.streaming.planner.profilePolicies) {
                profiles[static_cast<uint32_t>(Engine::StreamingHaloProfile::Default)] = policy;
            }
        }

        template <typename Assign>
        [[nodiscard]] Engine::ReflectionResult assignValidated(
            const EditorSettingsReflectionContext& context,
            Assign&& assign)
        {
            if (!context.settings) {
                return result(Engine::ReflectionStatus::InvalidHandle, "Editor settings context is missing.");
            }
            EditorProjectSettings next = *context.settings;
            const Engine::ReflectionResult assigned = assign(next);
            if (assigned.status != Engine::ReflectionStatus::Success) {
                return assigned;
            }
            const EditorProjectSettingsValidationResult validation = validateEditorProjectSettings(next);
            if (!validation.valid) {
                return result(
                    Engine::ReflectionStatus::ValidationFailed,
                    validation.errors.empty() ? "Editor setting validation failed." : validation.errors.front());
            }
            *context.settings = std::move(next);
            return changedResult(true);
        }

        template <typename T>
        [[nodiscard]] Engine::ReflectionResult typeMismatchUnless(const Engine::ReflectedValue& value, const T*& typed)
        {
            typed = as<T>(value);
            return typed ? result(Engine::ReflectionStatus::Success) : result(Engine::ReflectionStatus::TypeMismatch);
        }

        [[nodiscard]] Engine::TerrainLodMeshBuildSettings* lod(EditorProjectSettings& settings, uint32_t index)
        {
            return index < settings.streaming.bake.renderLods.size() ? &settings.streaming.bake.renderLods[index] : nullptr;
        }

        [[nodiscard]] const Engine::TerrainLodMeshBuildSettings* lod(const EditorProjectSettings& settings, uint32_t index)
        {
            return index < settings.streaming.bake.renderLods.size() ? &settings.streaming.bake.renderLods[index] : nullptr;
        }

        [[nodiscard]] Engine::ReflectionResult getFromSettings(
            const EditorProjectSettings& settings,
            EditorSettingsReflectionTarget target,
            PropertyId property)
        {
            const auto& bake = settings.streaming.bake;
            switch (target.object) {
                case ObjectId::Project:
                    if (property == PropertyId::SchemaVersion) return valueResult(static_cast<uint64_t>(settings.schemaVersion));
                    if (property == PropertyId::ProjectName) return valueResult(settings.projectName);
                    break;
                case ObjectId::TerrainImport:
                    switch (property) {
                        case PropertyId::SourcePath: return valueResult(bake.heightmap.sourcePath.generic_string());
                        case PropertyId::HeightmapChannel: return valueResult(heightmapChannelName(bake.heightmap.channel));
                        case PropertyId::SampleSpacing: return valueResult(bake.heightmap.sampleSpacing);
                        case PropertyId::HeightScale: return valueResult(bake.heightmap.heightScale);
                        case PropertyId::HeightOffset: return valueResult(bake.heightmap.heightOffset);
                        case PropertyId::SourceOrigin: return valueResult(bake.heightmap.sourceOrigin);
                        case PropertyId::FlipRows: return valueResult(bake.heightmap.flipRows);
                        case PropertyId::FlipColumns: return valueResult(bake.heightmap.flipColumns);
                        case PropertyId::ChunkWorldSize: return valueResult(bake.heightmap.chunkWorldSize);
                        case PropertyId::ChunkResolution: return valueResult(static_cast<uint64_t>(bake.heightmap.chunkResolution));
                        default: break;
                    }
                    break;
                case ObjectId::TerrainCache:
                    switch (property) {
                        case PropertyId::CacheRootPath: return valueResult(bake.terrainCache.rootPath.generic_string());
                        case PropertyId::CacheFormatVersion: return valueResult(static_cast<uint64_t>(bake.terrainCache.formatVersion));
                        case PropertyId::CachePolicy: return valueResult(cachePolicyName(bake.terrainCache.policy));
                        case PropertyId::TerrainImportVersion: return valueResult(bake.terrainCache.terrainImportVersion);
                        case PropertyId::ChunkPayloadVersion: return valueResult(bake.terrainCache.chunkPayloadVersion);
                        case PropertyId::LodMeshPayloadVersion: return valueResult(bake.terrainCache.lodMeshPayloadVersion);
                        case PropertyId::PhysicsColliderPayloadVersion: return valueResult(bake.terrainCache.physicsColliderPayloadVersion);
                        default: break;
                    }
                    break;
                case ObjectId::TerrainRenderLod: {
                    const Engine::TerrainLodMeshBuildSettings* entry = lod(settings, target.index);
                    if (!entry) return result(Engine::ReflectionStatus::InvalidHandle);
                    if (property == PropertyId::LodIndex) return valueResult(static_cast<uint64_t>(entry->lodIndex));
                    if (property == PropertyId::RenderResolution) return valueResult(static_cast<uint64_t>(entry->renderResolution));
                    if (property == PropertyId::SkirtDepth) return valueResult(entry->skirtDepth);
                    break;
                }
                case ObjectId::NavigationAgent:
                    if (property == PropertyId::AgentRadius) return valueResult(bake.navAgent.radius);
                    if (property == PropertyId::AgentHeight) return valueResult(bake.navAgent.height);
                    if (property == PropertyId::AgentMaxSlope) return valueResult(bake.navAgent.maxSlopeDegrees);
                    if (property == PropertyId::AgentMaxClimb) return valueResult(bake.navAgent.maxClimb);
                    break;
                case ObjectId::NavigationBuild:
                    switch (property) {
                        case PropertyId::CellSize: return valueResult(bake.navBuild.cellSize);
                        case PropertyId::CellHeight: return valueResult(bake.navBuild.cellHeight);
                        case PropertyId::TileBorderSize: return valueResult(static_cast<uint64_t>(bake.navBuild.tileBorderSize));
                        case PropertyId::MaxTiles: return valueResult(static_cast<uint64_t>(bake.navBuild.maxTiles));
                        case PropertyId::MaxPolysPerTile: return valueResult(static_cast<uint64_t>(bake.navBuild.maxPolysPerTile));
                        case PropertyId::MaxVertsPerPoly: return valueResult(static_cast<uint64_t>(bake.navBuild.maxVertsPerPoly));
                        case PropertyId::RegionMinSize: return valueResult(static_cast<uint64_t>(bake.navBuild.regionMinSize));
                        case PropertyId::RegionMergeSize: return valueResult(static_cast<uint64_t>(bake.navBuild.regionMergeSize));
                        case PropertyId::EdgeMaxLen: return valueResult(bake.navBuild.edgeMaxLen);
                        case PropertyId::EdgeMaxError: return valueResult(bake.navBuild.edgeMaxError);
                        case PropertyId::DetailSampleDist: return valueResult(bake.navBuild.detailSampleDist);
                        case PropertyId::DetailSampleMaxError: return valueResult(bake.navBuild.detailSampleMaxError);
                        default: break;
                    }
                    break;
                case ObjectId::TerrainNavigation:
                    if (property == PropertyId::NavigationResolution) return valueResult(static_cast<uint64_t>(bake.navigationResolution));
                    if (property == PropertyId::BorderPaddingWorld) return valueResult(bake.terrainNavigationBorderPaddingWorld);
                    if (property == PropertyId::BorderSampleCount) return valueResult(static_cast<uint64_t>(bake.terrainNavigationBorderSampleCount));
                    if (property == PropertyId::NavigationCacheRootPath) return valueResult(bake.navigationCache.rootPath.generic_string());
                    if (property == PropertyId::NavigationCacheWorldId) return valueResult(bake.navigationCache.worldId);
                    if (property == PropertyId::NavigationCacheFormatVersion) return valueResult(static_cast<uint64_t>(bake.navigationCache.formatVersion));
                    break;
                case ObjectId::SceneGeometryFiltering:
                    if (property == PropertyId::SceneGeometryMaxSlope) return valueResult(bake.sceneGeometryMaxSlopeDegrees);
                    if (property == PropertyId::SceneGeometryTilePadding) return valueResult(bake.sceneGeometryTileBoundsPadding);
                    if (property == PropertyId::SceneGeometryAdapterVersion) return valueResult(bake.sceneGeometryAdapterVersion);
                    break;
                case ObjectId::PhysicsColliders:
                    if (property == PropertyId::ColliderResolution) return valueResult(static_cast<uint64_t>(bake.physicsColliderResolution));
                    break;
                case ObjectId::Streaming:
                    switch (property) {
                        case PropertyId::SavedBuildManifestPath: return valueResult(settings.streaming.savedBuildManifestPath.generic_string());
                        case PropertyId::ValidatePayloadFiles: return valueResult(settings.streaming.validatePayloadFiles);
                        case PropertyId::RebuildWhenStale: return valueResult(settings.streaming.rebuildWhenStale);
                        case PropertyId::StreamingDefaultActiveRadius: return valueResult(defaultPolicy(settings).activeRadius);
                        case PropertyId::StreamingDefaultCacheRadius: return valueResult(defaultPolicy(settings).cacheRadius);
                        case PropertyId::StreamingDefaultHysteresis: return valueResult(defaultPolicy(settings).hysteresis);
                        case PropertyId::StreamingDefaultTransitions: return valueResult(static_cast<uint64_t>(defaultPolicy(settings).maxTransitionsPerFrame));
                        case PropertyId::StreamingGenerationPolicy: return valueResult(generationPolicyName(settings.streaming.generation.policy));
                        case PropertyId::StreamingMaxReadJobs: return valueResult(static_cast<uint64_t>(settings.streaming.cache.maxReadJobsQueuedPerUpdate));
                        case PropertyId::StreamingMaxCompletedReads: return valueResult(static_cast<uint64_t>(settings.streaming.cache.maxCompletedJobsMergedPerUpdate));
                        case PropertyId::StreamingMaxPromotes: return valueResult(static_cast<uint64_t>(settings.streaming.promotion.maxPromotesQueuedPerUpdate));
                        case PropertyId::StreamingMaxDemotes: return valueResult(static_cast<uint64_t>(settings.streaming.promotion.maxDemotesQueuedPerUpdate));
                        default: break;
                    }
                    break;
                case ObjectId::Renderer:
                    if (property == PropertyId::RendererLayerMask) return valueResult(static_cast<uint64_t>(settings.renderer.layerMask));
                    if (property == PropertyId::EnableDistanceCulling) return valueResult(settings.renderer.enableDistanceCulling);
                    if (property == PropertyId::PropMaxDrawDistance) return valueResult(settings.renderer.propMaxDrawDistance);
                    if (property == PropertyId::TerrainMaxDrawDistance) return valueResult(settings.renderer.terrainMaxDrawDistance);
                    break;
                case ObjectId::DebugDraw:
                    switch (property) {
                        case PropertyId::Enabled: return valueResult(settings.debugDraw.enabled);
                        case PropertyId::MaxDebugLines: return valueResult(static_cast<uint64_t>(settings.debugDraw.maxDebugLines));
                        case PropertyId::MaxNavMeshEdgeLines: return valueResult(static_cast<uint64_t>(settings.debugDraw.maxNavMeshEdgeLines));
                        case PropertyId::MaxColliderShapeLines: return valueResult(static_cast<uint64_t>(settings.debugDraw.maxColliderShapeLines));
                        case PropertyId::ColliderShapes: return valueResult(settings.debugDraw.colliderShapes);
                        case PropertyId::TerrainTileBounds: return valueResult(settings.debugDraw.terrainTileBounds);
                        case PropertyId::NavigationMeshEdges: return valueResult(settings.debugDraw.navigationMeshEdges);
                        default: break;
                    }
                    break;
                case ObjectId::Camera:
                    switch (property) {
                        case PropertyId::CameraFarPlane: return valueResult(settings.camera.settings.farPlane);
                        case PropertyId::CameraMaxDistance: return valueResult(settings.camera.settings.maxDistance);
                        case PropertyId::CameraKeyboardPanSpeed: return valueResult(settings.camera.settings.keyboardPanSpeed);
                        case PropertyId::CameraEdgePanSpeed: return valueResult(settings.camera.settings.edgePanSpeed);
                        case PropertyId::CameraMousePanSensitivity: return valueResult(settings.camera.settings.mousePanSensitivity);
                        case PropertyId::CameraZoomSensitivity: return valueResult(settings.camera.settings.zoomSensitivity);
                        case PropertyId::CameraPivotOffset: return valueResult(settings.camera.pivotOffsetFromFocus);
                        case PropertyId::CameraYaw: return valueResult(settings.camera.yawRadians);
                        case PropertyId::CameraPitch: return valueResult(settings.camera.pitchRadians);
                        case PropertyId::CameraDistance: return valueResult(settings.camera.distance);
                        case PropertyId::CameraMode: return valueResult(cameraModeName(settings.camera.mode));
                        case PropertyId::CameraFollowSmoothing: return valueResult(settings.camera.follow.followSmoothing);
                        case PropertyId::CameraMaxFollowLag: return valueResult(settings.camera.follow.maxFollowLag);
                        default: break;
                    }
                    break;
            }
            return result(Engine::ReflectionStatus::UnknownProperty);
        }

        [[nodiscard]] Engine::ReflectionResult setOnSettings(
            EditorProjectSettings& settings,
            EditorSettingsReflectionTarget target,
            PropertyId property,
            const Engine::ReflectedValue& value)
        {
            const std::string* stringValue = nullptr;
            const float* floatValue = nullptr;
            const bool* boolValue = nullptr;
            const uint64_t* uintValue = nullptr;
            const glm::vec3* vec3Value = nullptr;
            auto requireString = [&]() -> Engine::ReflectionResult { return typeMismatchUnless(value, stringValue); };
            auto requireFloat = [&]() -> Engine::ReflectionResult { return typeMismatchUnless(value, floatValue); };
            auto requireBool = [&]() -> Engine::ReflectionResult { return typeMismatchUnless(value, boolValue); };
            auto requireUInt = [&]() -> Engine::ReflectionResult { return typeMismatchUnless(value, uintValue); };
            auto requireVec3 = [&]() -> Engine::ReflectionResult { return typeMismatchUnless(value, vec3Value); };

            auto& bake = settings.streaming.bake;
            switch (target.object) {
                case ObjectId::Project:
                    if (property == PropertyId::SchemaVersion) return result(Engine::ReflectionStatus::ReadOnly);
                    if (property == PropertyId::ProjectName) {
                        if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                        settings.projectName = *stringValue;
                        return changedResult(true);
                    }
                    break;
                case ObjectId::TerrainImport:
                    switch (property) {
                        case PropertyId::SourcePath:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.sourcePath = *stringValue; return changedResult(true);
                        case PropertyId::HeightmapChannel: {
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            Engine::TerrainHeightmapChannel channel;
                            if (!parseHeightmapChannel(*stringValue, channel)) return result(Engine::ReflectionStatus::ValidationFailed, "Unknown heightmap channel.");
                            bake.heightmap.channel = channel; return changedResult(true);
                        }
                        case PropertyId::SampleSpacing:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.sampleSpacing = *floatValue; return changedResult(true);
                        case PropertyId::HeightScale:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.heightScale = *floatValue; return changedResult(true);
                        case PropertyId::HeightOffset:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.heightOffset = *floatValue; return changedResult(true);
                        case PropertyId::SourceOrigin:
                            if (auto r = requireVec3(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.sourceOrigin = *vec3Value; return changedResult(true);
                        case PropertyId::FlipRows:
                            if (auto r = requireBool(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.flipRows = *boolValue; return changedResult(true);
                        case PropertyId::FlipColumns:
                            if (auto r = requireBool(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.flipColumns = *boolValue; return changedResult(true);
                        case PropertyId::ChunkWorldSize:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.chunkWorldSize = *floatValue; return changedResult(true);
                        case PropertyId::ChunkResolution:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.heightmap.chunkResolution = static_cast<uint32_t>(*uintValue); return changedResult(true);
                        default: break;
                    }
                    break;
                case ObjectId::TerrainCache:
                    switch (property) {
                        case PropertyId::CacheRootPath:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.terrainCache.rootPath = *stringValue; return changedResult(true);
                        case PropertyId::CachePolicy: {
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            Engine::TerrainDerivedCachePolicy policy;
                            if (!parseCachePolicy(*stringValue, policy)) return result(Engine::ReflectionStatus::ValidationFailed, "Unknown cache policy.");
                            bake.terrainCache.policy = policy; return changedResult(true);
                        }
                        case PropertyId::CacheFormatVersion:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.terrainCache.formatVersion = static_cast<uint32_t>(*uintValue); return changedResult(true);
                        case PropertyId::TerrainImportVersion:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.terrainCache.terrainImportVersion = *stringValue; return changedResult(true);
                        case PropertyId::ChunkPayloadVersion:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.terrainCache.chunkPayloadVersion = *stringValue; return changedResult(true);
                        case PropertyId::LodMeshPayloadVersion:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.terrainCache.lodMeshPayloadVersion = *stringValue; return changedResult(true);
                        case PropertyId::PhysicsColliderPayloadVersion:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.terrainCache.physicsColliderPayloadVersion = *stringValue; return changedResult(true);
                        default: break;
                    }
                    break;
                case ObjectId::TerrainRenderLod: {
                    Engine::TerrainLodMeshBuildSettings* entry = lod(settings, target.index);
                    if (!entry) return result(Engine::ReflectionStatus::InvalidHandle);
                    if (property == PropertyId::LodIndex) return result(Engine::ReflectionStatus::ReadOnly);
                    if (property == PropertyId::RenderResolution) {
                        if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                        entry->renderResolution = static_cast<uint32_t>(*uintValue); return changedResult(true);
                    }
                    if (property == PropertyId::SkirtDepth) {
                        if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                        entry->skirtDepth = *floatValue; return changedResult(true);
                    }
                    break;
                }
                case ObjectId::NavigationAgent:
                    if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                    if (property == PropertyId::AgentRadius) bake.navAgent.radius = *floatValue;
                    else if (property == PropertyId::AgentHeight) bake.navAgent.height = *floatValue;
                    else if (property == PropertyId::AgentMaxSlope) bake.navAgent.maxSlopeDegrees = *floatValue;
                    else if (property == PropertyId::AgentMaxClimb) bake.navAgent.maxClimb = *floatValue;
                    else break;
                    return changedResult(true);
                case ObjectId::NavigationBuild:
                    switch (property) {
                        case PropertyId::CellSize:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.navBuild.cellSize = *floatValue; return changedResult(true);
                        case PropertyId::CellHeight:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.navBuild.cellHeight = *floatValue; return changedResult(true);
                        case PropertyId::MaxTiles:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.navBuild.maxTiles = static_cast<uint32_t>(*uintValue); return changedResult(true);
                        case PropertyId::MaxVertsPerPoly:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            bake.navBuild.maxVertsPerPoly = static_cast<uint32_t>(*uintValue); return changedResult(true);
                        case PropertyId::TileBorderSize:
                        case PropertyId::MaxPolysPerTile:
                        case PropertyId::RegionMinSize:
                        case PropertyId::RegionMergeSize:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            if (property == PropertyId::TileBorderSize) bake.navBuild.tileBorderSize = static_cast<uint32_t>(*uintValue);
                            if (property == PropertyId::MaxPolysPerTile) bake.navBuild.maxPolysPerTile = static_cast<uint32_t>(*uintValue);
                            if (property == PropertyId::RegionMinSize) bake.navBuild.regionMinSize = static_cast<uint32_t>(*uintValue);
                            if (property == PropertyId::RegionMergeSize) bake.navBuild.regionMergeSize = static_cast<uint32_t>(*uintValue);
                            return changedResult(true);
                        case PropertyId::EdgeMaxLen:
                        case PropertyId::EdgeMaxError:
                        case PropertyId::DetailSampleDist:
                        case PropertyId::DetailSampleMaxError:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            if (property == PropertyId::EdgeMaxLen) bake.navBuild.edgeMaxLen = *floatValue;
                            if (property == PropertyId::EdgeMaxError) bake.navBuild.edgeMaxError = *floatValue;
                            if (property == PropertyId::DetailSampleDist) bake.navBuild.detailSampleDist = *floatValue;
                            if (property == PropertyId::DetailSampleMaxError) bake.navBuild.detailSampleMaxError = *floatValue;
                            return changedResult(true);
                        default: break;
                    }
                    break;
                case ObjectId::TerrainNavigation:
                    if (property == PropertyId::NavigationCacheRootPath || property == PropertyId::NavigationCacheWorldId) {
                        if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                        if (property == PropertyId::NavigationCacheRootPath) bake.navigationCache.rootPath = *stringValue;
                        else bake.navigationCache.worldId = *stringValue;
                        return changedResult(true);
                    }
                    if (property == PropertyId::BorderPaddingWorld) {
                        if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                        bake.terrainNavigationBorderPaddingWorld = *floatValue; return changedResult(true);
                    }
                    if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                    if (property == PropertyId::NavigationResolution) bake.navigationResolution = static_cast<uint32_t>(*uintValue);
                    else if (property == PropertyId::BorderSampleCount) bake.terrainNavigationBorderSampleCount = static_cast<uint32_t>(*uintValue);
                    else if (property == PropertyId::NavigationCacheFormatVersion) bake.navigationCache.formatVersion = static_cast<uint32_t>(*uintValue);
                    else break;
                    return changedResult(true);
                case ObjectId::SceneGeometryFiltering:
                    if (property == PropertyId::SceneGeometryAdapterVersion) {
                        if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                        bake.sceneGeometryAdapterVersion = *stringValue; return changedResult(true);
                    }
                    if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                    if (property == PropertyId::SceneGeometryMaxSlope) bake.sceneGeometryMaxSlopeDegrees = *floatValue;
                    else if (property == PropertyId::SceneGeometryTilePadding) bake.sceneGeometryTileBoundsPadding = *floatValue;
                    else break;
                    return changedResult(true);
                case ObjectId::PhysicsColliders:
                    if (property == PropertyId::ColliderResolution) {
                        if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                        bake.physicsColliderResolution = static_cast<uint32_t>(*uintValue); return changedResult(true);
                    }
                    break;
                case ObjectId::Streaming:
                    switch (property) {
                        case PropertyId::SavedBuildManifestPath:
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            settings.streaming.savedBuildManifestPath = *stringValue; return changedResult(true);
                        case PropertyId::ValidatePayloadFiles:
                            if (auto r = requireBool(); r.status != Engine::ReflectionStatus::Success) return r;
                            settings.streaming.validatePayloadFiles = *boolValue; return changedResult(true);
                        case PropertyId::RebuildWhenStale:
                            if (auto r = requireBool(); r.status != Engine::ReflectionStatus::Success) return r;
                            settings.streaming.rebuildWhenStale = *boolValue; return changedResult(true);
                        case PropertyId::StreamingDefaultActiveRadius:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            defaultPolicy(settings).activeRadius = *floatValue; applyDefaultPolicyToAllPayloads(settings); return changedResult(true);
                        case PropertyId::StreamingDefaultCacheRadius:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            defaultPolicy(settings).cacheRadius = *floatValue; applyDefaultPolicyToAllPayloads(settings); return changedResult(true);
                        case PropertyId::StreamingDefaultHysteresis:
                            if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                            defaultPolicy(settings).hysteresis = *floatValue; applyDefaultPolicyToAllPayloads(settings); return changedResult(true);
                        case PropertyId::StreamingDefaultTransitions:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            defaultPolicy(settings).maxTransitionsPerFrame = static_cast<uint32_t>(*uintValue); applyDefaultPolicyToAllPayloads(settings); return changedResult(true);
                        case PropertyId::StreamingGenerationPolicy: {
                            if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                            Engine::StreamingDerivedGenerationPolicy policy;
                            if (!parseGenerationPolicy(*stringValue, policy)) return result(Engine::ReflectionStatus::ValidationFailed, "Unknown generation policy.");
                            settings.streaming.generation.policy = policy; return changedResult(true);
                        }
                        case PropertyId::StreamingMaxReadJobs:
                        case PropertyId::StreamingMaxCompletedReads:
                        case PropertyId::StreamingMaxPromotes:
                        case PropertyId::StreamingMaxDemotes:
                            if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                            if (property == PropertyId::StreamingMaxReadJobs) settings.streaming.cache.maxReadJobsQueuedPerUpdate = static_cast<uint32_t>(*uintValue);
                            if (property == PropertyId::StreamingMaxCompletedReads) settings.streaming.cache.maxCompletedJobsMergedPerUpdate = static_cast<uint32_t>(*uintValue);
                            if (property == PropertyId::StreamingMaxPromotes) settings.streaming.promotion.maxPromotesQueuedPerUpdate = static_cast<uint32_t>(*uintValue);
                            if (property == PropertyId::StreamingMaxDemotes) settings.streaming.promotion.maxDemotesQueuedPerUpdate = static_cast<uint32_t>(*uintValue);
                            return changedResult(true);
                        default: break;
                    }
                    break;
                case ObjectId::Renderer:
                    if (property == PropertyId::EnableDistanceCulling) {
                        if (auto r = requireBool(); r.status != Engine::ReflectionStatus::Success) return r;
                        settings.renderer.enableDistanceCulling = *boolValue; return changedResult(true);
                    }
                    if (property == PropertyId::RendererLayerMask) {
                        if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                        settings.renderer.layerMask = static_cast<uint32_t>(*uintValue); return changedResult(true);
                    }
                    if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                    if (property == PropertyId::PropMaxDrawDistance) settings.renderer.propMaxDrawDistance = *floatValue;
                    else if (property == PropertyId::TerrainMaxDrawDistance) settings.renderer.terrainMaxDrawDistance = *floatValue;
                    else break;
                    return changedResult(true);
                case ObjectId::DebugDraw:
                    if (property == PropertyId::MaxDebugLines || property == PropertyId::MaxNavMeshEdgeLines ||
                        property == PropertyId::MaxColliderShapeLines) {
                        if (auto r = requireUInt(); r.status != Engine::ReflectionStatus::Success) return r;
                        if (property == PropertyId::MaxDebugLines) settings.debugDraw.maxDebugLines = static_cast<uint32_t>(*uintValue);
                        if (property == PropertyId::MaxNavMeshEdgeLines) settings.debugDraw.maxNavMeshEdgeLines = static_cast<uint32_t>(*uintValue);
                        if (property == PropertyId::MaxColliderShapeLines) settings.debugDraw.maxColliderShapeLines = static_cast<uint32_t>(*uintValue);
                        return changedResult(true);
                    }
                    if (auto r = requireBool(); r.status != Engine::ReflectionStatus::Success) return r;
                    if (property == PropertyId::Enabled) settings.debugDraw.enabled = *boolValue;
                    else if (property == PropertyId::ColliderShapes) settings.debugDraw.colliderShapes = *boolValue;
                    else if (property == PropertyId::TerrainTileBounds) settings.debugDraw.terrainTileBounds = *boolValue;
                    else if (property == PropertyId::NavigationMeshEdges) settings.debugDraw.navigationMeshEdges = *boolValue;
                    else break;
                    return changedResult(true);
                case ObjectId::Camera:
                    if (property == PropertyId::CameraPivotOffset) {
                        if (auto r = requireVec3(); r.status != Engine::ReflectionStatus::Success) return r;
                        settings.camera.pivotOffsetFromFocus = *vec3Value; return changedResult(true);
                    }
                    if (property == PropertyId::CameraMode) {
                        if (auto r = requireString(); r.status != Engine::ReflectionStatus::Success) return r;
                        Engine::CameraMode mode;
                        if (!parseCameraMode(*stringValue, mode)) return result(Engine::ReflectionStatus::ValidationFailed, "Unknown camera mode.");
                        settings.camera.mode = mode; return changedResult(true);
                    }
                    if (auto r = requireFloat(); r.status != Engine::ReflectionStatus::Success) return r;
                    if (property == PropertyId::CameraFarPlane) settings.camera.settings.farPlane = *floatValue;
                    else if (property == PropertyId::CameraMaxDistance) settings.camera.settings.maxDistance = *floatValue;
                    else if (property == PropertyId::CameraKeyboardPanSpeed) settings.camera.settings.keyboardPanSpeed = *floatValue;
                    else if (property == PropertyId::CameraEdgePanSpeed) settings.camera.settings.edgePanSpeed = *floatValue;
                    else if (property == PropertyId::CameraMousePanSensitivity) settings.camera.settings.mousePanSensitivity = *floatValue;
                    else if (property == PropertyId::CameraZoomSensitivity) settings.camera.settings.zoomSensitivity = *floatValue;
                    else if (property == PropertyId::CameraYaw) settings.camera.yawRadians = *floatValue;
                    else if (property == PropertyId::CameraPitch) settings.camera.pitchRadians = *floatValue;
                    else if (property == PropertyId::CameraDistance) settings.camera.distance = *floatValue;
                    else if (property == PropertyId::CameraFollowSmoothing) settings.camera.follow.followSmoothing = *floatValue;
                    else if (property == PropertyId::CameraMaxFollowLag) settings.camera.follow.maxFollowLag = *floatValue;
                    else break;
                    return changedResult(true);
            }
            return result(Engine::ReflectionStatus::UnknownProperty);
        }
    }

    void registerEditorSettingsReflectionDescriptors(Engine::ReflectionRegistry& registry)
    {
        const EditorProjectSettings defaults = defaultEditorProjectSettings();
        const auto rebuild = RebuildFlag;
        const auto advRebuild = AdvancedRebuildFlag;
        const auto editor = EditorFlag;
        const auto advEditor = AdvancedEditorFlag;
        [[maybe_unused]] Engine::ReflectionStatus status = registry.registerObject(object(ObjectId::Project, "EditorProject", "Project", "Project", {
            prop(PropertyId::SchemaVersion, "schemaVersion", "Schema Version", "Project", ReflectedValueType::UInt64, ReadOnlyEditorFlag, static_cast<uint64_t>(defaults.schemaVersion), 1ull),
            prop(PropertyId::ProjectName, "projectName", "Project Name", "Project", ReflectedValueType::String, editor, defaults.projectName),
        }));
        status = registry.registerObject(object(ObjectId::TerrainImport, "EditorTerrainImportSettings", "Terrain Import", "Terrain Import", {
            prop(PropertyId::SourcePath, "sourcePath", "Source Path", "Terrain Import", ReflectedValueType::String, rebuild | ReflectedPropertyFlag::AssetReference, defaults.streaming.bake.heightmap.sourcePath.generic_string()),
            prop(PropertyId::HeightmapChannel, "channel", "Channel", "Terrain Import", ReflectedValueType::String, rebuild, heightmapChannelName(defaults.streaming.bake.heightmap.channel), {}, {}, {}, {"red", "green", "blue", "alpha", "average"}),
            prop(PropertyId::SampleSpacing, "sampleSpacing", "Sample Spacing", "Terrain Import", ReflectedValueType::Float, rebuild, defaults.streaming.bake.heightmap.sampleSpacing, 0.001f, {}, "m"),
            prop(PropertyId::HeightScale, "heightScale", "Height Scale", "Terrain Import", ReflectedValueType::Float, rebuild, defaults.streaming.bake.heightmap.heightScale, 0.0f, {}, "m"),
            prop(PropertyId::HeightOffset, "heightOffset", "Height Offset", "Terrain Import", ReflectedValueType::Float, rebuild, defaults.streaming.bake.heightmap.heightOffset, {}, {}, "m"),
            prop(PropertyId::SourceOrigin, "sourceOrigin", "Source Origin", "Terrain Import", ReflectedValueType::Vec3, rebuild, defaults.streaming.bake.heightmap.sourceOrigin, {}, {}, "m"),
            prop(PropertyId::FlipRows, "flipRows", "Flip Rows", "Terrain Import", ReflectedValueType::Bool, rebuild, defaults.streaming.bake.heightmap.flipRows),
            prop(PropertyId::FlipColumns, "flipColumns", "Flip Columns", "Terrain Import", ReflectedValueType::Bool, rebuild, defaults.streaming.bake.heightmap.flipColumns),
            prop(PropertyId::ChunkWorldSize, "chunkWorldSize", "Chunk World Size", "Terrain Import", ReflectedValueType::Float, rebuild, defaults.streaming.bake.heightmap.chunkWorldSize, 0.001f, {}, "m"),
            prop(PropertyId::ChunkResolution, "chunkResolution", "Chunk Resolution", "Terrain Import", ReflectedValueType::UInt64, rebuild, static_cast<uint64_t>(defaults.streaming.bake.heightmap.chunkResolution), 2ull),
        }));
        status = registry.registerObject(object(ObjectId::TerrainCache, "EditorTerrainCacheSettings", "Terrain Cache", "Terrain Cache", {
            prop(PropertyId::CacheRootPath, "rootPath", "Root Path", "Terrain Cache", ReflectedValueType::String, rebuild, defaults.streaming.bake.terrainCache.rootPath.generic_string()),
            prop(PropertyId::CachePolicy, "policy", "Policy", "Terrain Cache", ReflectedValueType::String, rebuild, cachePolicyName(defaults.streaming.bake.terrainCache.policy), {}, {}, {}, {"disabled", "read_only", "generate_on_miss", "refresh"}),
            prop(PropertyId::CacheFormatVersion, "formatVersion", "Format Version", "Terrain Cache", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.terrainCache.formatVersion), 1ull),
            prop(PropertyId::TerrainImportVersion, "terrainImportVersion", "Terrain Import Version", "Terrain Cache", ReflectedValueType::String, advRebuild, defaults.streaming.bake.terrainCache.terrainImportVersion),
            prop(PropertyId::ChunkPayloadVersion, "chunkPayloadVersion", "Chunk Payload Version", "Terrain Cache", ReflectedValueType::String, advRebuild, defaults.streaming.bake.terrainCache.chunkPayloadVersion),
            prop(PropertyId::LodMeshPayloadVersion, "lodMeshPayloadVersion", "LOD Mesh Payload Version", "Terrain Cache", ReflectedValueType::String, advRebuild, defaults.streaming.bake.terrainCache.lodMeshPayloadVersion),
            prop(PropertyId::PhysicsColliderPayloadVersion, "physicsColliderPayloadVersion", "Physics Collider Payload Version", "Terrain Cache", ReflectedValueType::String, advRebuild, defaults.streaming.bake.terrainCache.physicsColliderPayloadVersion),
        }));
        status = registry.registerObject(object(ObjectId::TerrainRenderLod, "EditorTerrainRenderLodSettings", "Render LOD", "Render LODs", {
            prop(PropertyId::LodIndex, "lodIndex", "LOD Index", "Render LODs", ReflectedValueType::UInt64, ReadOnlyEditorFlag | ReflectedPropertyFlag::RequiresExplicitApply, 0ull, 0ull),
            prop(PropertyId::RenderResolution, "renderResolution", "Render Resolution", "Render LODs", ReflectedValueType::UInt64, rebuild, 17ull, 2ull),
            prop(PropertyId::SkirtDepth, "skirtDepth", "Skirt Depth", "Render LODs", ReflectedValueType::Float, rebuild, 0.0f, 0.0f, {}, "m"),
        }));
        status = registry.registerObject(object(ObjectId::NavigationAgent, "EditorNavigationAgentSettings", "Navigation Agent", "Navigation Agent", {
            prop(PropertyId::AgentRadius, "radius", "Radius", "Navigation Agent", ReflectedValueType::Float, rebuild, defaults.streaming.bake.navAgent.radius, 0.001f, {}, "m"),
            prop(PropertyId::AgentHeight, "height", "Height", "Navigation Agent", ReflectedValueType::Float, rebuild, defaults.streaming.bake.navAgent.height, 0.001f, {}, "m"),
            prop(PropertyId::AgentMaxSlope, "maxSlopeDegrees", "Max Slope", "Navigation Agent", ReflectedValueType::Float, rebuild, defaults.streaming.bake.navAgent.maxSlopeDegrees, 0.0f, 89.0f, "deg"),
            prop(PropertyId::AgentMaxClimb, "maxClimb", "Max Climb", "Navigation Agent", ReflectedValueType::Float, rebuild, defaults.streaming.bake.navAgent.maxClimb, 0.0f, {}, "m"),
        }));
        status = registry.registerObject(object(ObjectId::NavigationBuild, "EditorNavigationBuildSettings", "Navigation Build", "Navigation Build", {
            prop(PropertyId::CellSize, "cellSize", "Cell Size", "Navigation Build", ReflectedValueType::Float, advRebuild, defaults.streaming.bake.navBuild.cellSize, 0.001f, {}, "m"),
            prop(PropertyId::CellHeight, "cellHeight", "Cell Height", "Navigation Build", ReflectedValueType::Float, advRebuild, defaults.streaming.bake.navBuild.cellHeight, 0.001f, {}, "m"),
            prop(PropertyId::TileBorderSize, "tileBorderSize", "Tile Border Size", "Navigation Build", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navBuild.tileBorderSize), 0ull),
            prop(PropertyId::MaxTiles, "maxTiles", "Max Tiles", "Navigation Build", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navBuild.maxTiles), 1ull),
            prop(PropertyId::MaxPolysPerTile, "maxPolysPerTile", "Max Polys Per Tile", "Navigation Build", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navBuild.maxPolysPerTile), 1ull),
            prop(PropertyId::MaxVertsPerPoly, "maxVertsPerPoly", "Max Verts Per Poly", "Navigation Build", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navBuild.maxVertsPerPoly), 3ull, 12ull),
            prop(PropertyId::RegionMinSize, "regionMinSize", "Region Min Size", "Navigation Build", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navBuild.regionMinSize), 0ull),
            prop(PropertyId::RegionMergeSize, "regionMergeSize", "Region Merge Size", "Navigation Build", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navBuild.regionMergeSize), 0ull),
            prop(PropertyId::EdgeMaxLen, "edgeMaxLen", "Edge Max Len", "Navigation Build", ReflectedValueType::Float, advRebuild, defaults.streaming.bake.navBuild.edgeMaxLen, 0.0f),
            prop(PropertyId::EdgeMaxError, "edgeMaxError", "Edge Max Error", "Navigation Build", ReflectedValueType::Float, advRebuild, defaults.streaming.bake.navBuild.edgeMaxError, 0.0f),
            prop(PropertyId::DetailSampleDist, "detailSampleDist", "Detail Sample Dist", "Navigation Build", ReflectedValueType::Float, advRebuild, defaults.streaming.bake.navBuild.detailSampleDist, 0.0f),
            prop(PropertyId::DetailSampleMaxError, "detailSampleMaxError", "Detail Sample Max Error", "Navigation Build", ReflectedValueType::Float, advRebuild, defaults.streaming.bake.navBuild.detailSampleMaxError, 0.0f),
        }));
        status = registry.registerObject(object(ObjectId::TerrainNavigation, "EditorTerrainNavigationSettings", "Terrain Navigation", "Terrain Navigation", {
            prop(PropertyId::NavigationResolution, "navigationResolution", "Navigation Resolution", "Terrain Navigation", ReflectedValueType::UInt64, rebuild, static_cast<uint64_t>(defaults.streaming.bake.navigationResolution), 2ull),
            prop(PropertyId::BorderPaddingWorld, "borderPaddingWorld", "Border Padding", "Terrain Navigation", ReflectedValueType::Float, rebuild, defaults.streaming.bake.terrainNavigationBorderPaddingWorld, 0.0f, {}, "m"),
            prop(PropertyId::BorderSampleCount, "borderSampleCount", "Border Samples", "Terrain Navigation", ReflectedValueType::UInt64, rebuild, static_cast<uint64_t>(defaults.streaming.bake.terrainNavigationBorderSampleCount), 0ull),
            prop(PropertyId::NavigationCacheRootPath, "navigationCacheRootPath", "Cache Root", "Terrain Navigation", ReflectedValueType::String, rebuild, defaults.streaming.bake.navigationCache.rootPath.generic_string()),
            prop(PropertyId::NavigationCacheWorldId, "navigationCacheWorldId", "World ID", "Terrain Navigation", ReflectedValueType::String, rebuild, defaults.streaming.bake.navigationCache.worldId),
            prop(PropertyId::NavigationCacheFormatVersion, "navigationCacheFormatVersion", "Cache Format Version", "Terrain Navigation", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.bake.navigationCache.formatVersion), 1ull),
        }));
        status = registry.registerObject(object(ObjectId::SceneGeometryFiltering, "EditorSceneGeometryFilteringSettings", "Scene Geometry Filtering", "Scene Geometry Filtering", {
            prop(PropertyId::SceneGeometryMaxSlope, "maxWalkableSlopeDegrees", "Max Walkable Slope", "Scene Geometry Filtering", ReflectedValueType::Float, rebuild, defaults.streaming.bake.sceneGeometryMaxSlopeDegrees, 0.0f, 89.0f, "deg"),
            prop(PropertyId::SceneGeometryTilePadding, "tileBoundsPadding", "Tile Bounds Padding", "Scene Geometry Filtering", ReflectedValueType::Float, rebuild, defaults.streaming.bake.sceneGeometryTileBoundsPadding, 0.0f, {}, "m"),
            prop(PropertyId::SceneGeometryAdapterVersion, "adapterVersion", "Adapter Version", "Scene Geometry Filtering", ReflectedValueType::String, advRebuild, defaults.streaming.bake.sceneGeometryAdapterVersion),
        }));
        status = registry.registerObject(object(ObjectId::PhysicsColliders, "EditorPhysicsColliderSettings", "Physics Colliders", "Physics Colliders", {
            prop(PropertyId::ColliderResolution, "colliderResolution", "Collider Resolution", "Physics Colliders", ReflectedValueType::UInt64, rebuild, static_cast<uint64_t>(defaults.streaming.bake.physicsColliderResolution), 2ull),
        }));
        status = registry.registerObject(object(ObjectId::Streaming, "EditorStreamingSettings", "Streaming", "Streaming", {
            prop(PropertyId::SavedBuildManifestPath, "savedBuildManifestPath", "Saved Build Manifest", "Streaming", ReflectedValueType::String, rebuild, defaults.streaming.savedBuildManifestPath.generic_string()),
            prop(PropertyId::ValidatePayloadFiles, "validatePayloadFiles", "Validate Payload Files", "Streaming", ReflectedValueType::Bool, rebuild, defaults.streaming.validatePayloadFiles),
            prop(PropertyId::RebuildWhenStale, "rebuildWhenStale", "Rebuild When Stale", "Streaming", ReflectedValueType::Bool, rebuild, defaults.streaming.rebuildWhenStale),
            prop(PropertyId::StreamingDefaultActiveRadius, "defaultActiveRadius", "Default Active Radius", "Streaming", ReflectedValueType::Float, rebuild, defaultPolicy(defaults).activeRadius, 0.0f, {}, "m"),
            prop(PropertyId::StreamingDefaultCacheRadius, "defaultCacheRadius", "Default Cache Radius", "Streaming", ReflectedValueType::Float, rebuild, defaultPolicy(defaults).cacheRadius, 0.0f, {}, "m"),
            prop(PropertyId::StreamingDefaultHysteresis, "defaultHysteresis", "Default Hysteresis", "Streaming", ReflectedValueType::Float, rebuild, defaultPolicy(defaults).hysteresis, 0.0f, {}, "m"),
            prop(PropertyId::StreamingDefaultTransitions, "defaultMaxTransitions", "Default Max Transitions", "Streaming", ReflectedValueType::UInt64, rebuild, static_cast<uint64_t>(defaultPolicy(defaults).maxTransitionsPerFrame), 1ull),
            prop(PropertyId::StreamingGenerationPolicy, "generationPolicy", "Generation Policy", "Streaming", ReflectedValueType::String, advRebuild, generationPolicyName(defaults.streaming.generation.policy), {}, {}, {}, {"read_only", "generate_on_miss", "refresh"}),
            prop(PropertyId::StreamingMaxReadJobs, "maxReadJobsQueuedPerUpdate", "Max Read Jobs", "Streaming", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.cache.maxReadJobsQueuedPerUpdate), 1ull),
            prop(PropertyId::StreamingMaxCompletedReads, "maxCompletedReadsPerUpdate", "Max Completed Reads", "Streaming", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.cache.maxCompletedJobsMergedPerUpdate), 1ull),
            prop(PropertyId::StreamingMaxPromotes, "maxPromotesQueuedPerUpdate", "Max Promotes", "Streaming", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.promotion.maxPromotesQueuedPerUpdate), 1ull),
            prop(PropertyId::StreamingMaxDemotes, "maxDemotesQueuedPerUpdate", "Max Demotes", "Streaming", ReflectedValueType::UInt64, advRebuild, static_cast<uint64_t>(defaults.streaming.promotion.maxDemotesQueuedPerUpdate), 1ull),
        }));
        status = registry.registerObject(object(ObjectId::Renderer, "EditorRendererSettings", "Renderer", "Renderer", {
            prop(PropertyId::RendererLayerMask, "layerMask", "Layer Mask", "Renderer", ReflectedValueType::UInt64, advEditor, static_cast<uint64_t>(defaults.renderer.layerMask), 0ull),
            prop(PropertyId::EnableDistanceCulling, "enableDistanceCulling", "Distance Culling", "Renderer", ReflectedValueType::Bool, editor, defaults.renderer.enableDistanceCulling),
            prop(PropertyId::PropMaxDrawDistance, "propMaxDrawDistance", "Prop Max Draw Distance", "Renderer", ReflectedValueType::Float, editor, defaults.renderer.propMaxDrawDistance, 0.0f, {}, "m"),
            prop(PropertyId::TerrainMaxDrawDistance, "terrainMaxDrawDistance", "Terrain Max Draw Distance", "Renderer", ReflectedValueType::Float, editor, defaults.renderer.terrainMaxDrawDistance, 0.0f, {}, "m"),
        }));
        status = registry.registerObject(object(ObjectId::DebugDraw, "EditorDebugDrawSettings", "Debug Draw", "Debug Draw", {
            prop(PropertyId::Enabled, "enabled", "Enabled", "Debug Draw", ReflectedValueType::Bool, editor, defaults.debugDraw.enabled),
            prop(PropertyId::MaxDebugLines, "maxDebugLines", "Max Debug Lines", "Debug Draw", ReflectedValueType::UInt64, advEditor, static_cast<uint64_t>(defaults.debugDraw.maxDebugLines), 1ull),
            prop(PropertyId::MaxNavMeshEdgeLines, "maxNavMeshEdgeLines", "Max Nav Mesh Edge Lines", "Debug Draw", ReflectedValueType::UInt64, advEditor, static_cast<uint64_t>(defaults.debugDraw.maxNavMeshEdgeLines), 1ull),
            prop(PropertyId::MaxColliderShapeLines, "maxColliderShapeLines", "Max Collider Shape Lines", "Debug Draw", ReflectedValueType::UInt64, advEditor, static_cast<uint64_t>(defaults.debugDraw.maxColliderShapeLines), 1ull),
            prop(PropertyId::ColliderShapes, "colliderShapes", "Collider Shapes", "Debug Draw", ReflectedValueType::Bool, editor, defaults.debugDraw.colliderShapes),
            prop(PropertyId::TerrainTileBounds, "terrainTileBounds", "Terrain Tile Bounds", "Debug Draw", ReflectedValueType::Bool, editor, defaults.debugDraw.terrainTileBounds),
            prop(PropertyId::NavigationMeshEdges, "navigationMeshEdges", "Navigation Mesh Edges", "Debug Draw", ReflectedValueType::Bool, editor, defaults.debugDraw.navigationMeshEdges),
        }));
        status = registry.registerObject(object(ObjectId::Camera, "EditorCameraSettings", "Camera", "Camera", {
            prop(PropertyId::CameraFarPlane, "farPlane", "Far Plane", "Camera", ReflectedValueType::Float, editor, defaults.camera.settings.farPlane, 0.001f, {}, "m"),
            prop(PropertyId::CameraMaxDistance, "maxDistance", "Max Distance", "Camera", ReflectedValueType::Float, editor, defaults.camera.settings.maxDistance, 0.001f, {}, "m"),
            prop(PropertyId::CameraKeyboardPanSpeed, "keyboardPanSpeed", "Keyboard Pan Speed", "Camera", ReflectedValueType::Float, editor, defaults.camera.settings.keyboardPanSpeed, 0.0f),
            prop(PropertyId::CameraEdgePanSpeed, "edgePanSpeed", "Edge Pan Speed", "Camera", ReflectedValueType::Float, editor, defaults.camera.settings.edgePanSpeed, 0.0f),
            prop(PropertyId::CameraMousePanSensitivity, "mousePanSensitivity", "Mouse Pan Sensitivity", "Camera", ReflectedValueType::Float, editor, defaults.camera.settings.mousePanSensitivity, 0.0f),
            prop(PropertyId::CameraZoomSensitivity, "zoomSensitivity", "Zoom Sensitivity", "Camera", ReflectedValueType::Float, editor, defaults.camera.settings.zoomSensitivity, 0.0f),
            prop(PropertyId::CameraPivotOffset, "pivotOffsetFromFocus", "Pivot Offset", "Camera", ReflectedValueType::Vec3, editor, defaults.camera.pivotOffsetFromFocus),
            prop(PropertyId::CameraYaw, "yawRadians", "Yaw", "Camera", ReflectedValueType::Float, editor, defaults.camera.yawRadians, {}, {}, "rad"),
            prop(PropertyId::CameraPitch, "pitchRadians", "Pitch", "Camera", ReflectedValueType::Float, editor, defaults.camera.pitchRadians, {}, {}, "rad"),
            prop(PropertyId::CameraDistance, "distance", "Distance", "Camera", ReflectedValueType::Float, editor, defaults.camera.distance, 0.001f, {}, "m"),
            prop(PropertyId::CameraMode, "mode", "Mode", "Camera", ReflectedValueType::String, editor, cameraModeName(defaults.camera.mode), {}, {}, {}, {"free", "follow_target"}),
            prop(PropertyId::CameraFollowSmoothing, "followSmoothing", "Follow Smoothing", "Camera", ReflectedValueType::Float, editor, defaults.camera.follow.followSmoothing, 0.0f),
            prop(PropertyId::CameraMaxFollowLag, "maxFollowLag", "Max Follow Lag", "Camera", ReflectedValueType::Float, editor, defaults.camera.follow.maxFollowLag, 0.0f, {}, "m"),
        }));
    }

    std::vector<EditorSettingsReflectionTarget> enumerateEditorSettingsTargets(
        const EditorProjectSettings& settings)
    {
        std::vector<EditorSettingsReflectionTarget> targets = {
            {ObjectId::Project, 0},
            {ObjectId::TerrainImport, 0},
            {ObjectId::TerrainCache, 0},
            {ObjectId::NavigationAgent, 0},
            {ObjectId::NavigationBuild, 0},
            {ObjectId::TerrainNavigation, 0},
            {ObjectId::SceneGeometryFiltering, 0},
            {ObjectId::PhysicsColliders, 0},
            {ObjectId::Streaming, 0},
            {ObjectId::Renderer, 0},
            {ObjectId::DebugDraw, 0},
            {ObjectId::Camera, 0},
        };
        for (uint32_t index = 0; index < settings.streaming.bake.renderLods.size(); ++index) {
            targets.push_back({ObjectId::TerrainRenderLod, index});
        }
        return targets;
    }

    Engine::ReflectionResult getEditorSetting(
        const EditorSettingsReflectionContext& context,
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property)
    {
        if (!context.settings) {
            return result(Engine::ReflectionStatus::InvalidHandle, "Editor settings context is missing.");
        }
        return getFromSettings(*context.settings, target, property);
    }

    Engine::ReflectionResult setEditorSetting(
        const EditorSettingsReflectionContext& context,
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property,
        const Engine::ReflectedValue& value)
    {
        return assignValidated(context, [&](EditorProjectSettings& next) {
            return setOnSettings(next, target, property, value);
        });
    }

}
