#pragma once

#include <cstdint>
#include <vector>

#include "App/EditorProjectSettings.hpp"
#include "Engine/Reflection.hpp"

namespace ManualEngine::App {

    enum class EditorSettingsReflectedObjectId : uint32_t {
        Project = 1000,
        TerrainImport = 1010,
        TerrainCache = 1020,
        TerrainRenderLod = 1030,
        NavigationAgent = 1040,
        NavigationBuild = 1050,
        TerrainNavigation = 1060,
        SceneGeometryFiltering = 1070,
        PhysicsColliders = 1080,
        Streaming = 1090,
        Renderer = 1100,
        DebugDraw = 1110,
        Camera = 1120,
    };

    enum class EditorSettingsReflectedPropertyId : uint32_t {
        SchemaVersion = 1,
        ProjectName = 2,
        SourcePath = 10,
        HeightmapChannel = 11,
        SampleSpacing = 12,
        HeightScale = 13,
        HeightOffset = 14,
        SourceOrigin = 15,
        FlipRows = 16,
        FlipColumns = 17,
        ChunkWorldSize = 18,
        ChunkResolution = 19,
        CacheRootPath = 30,
        CacheFormatVersion = 31,
        CachePolicy = 32,
        TerrainImportVersion = 33,
        ChunkPayloadVersion = 34,
        LodMeshPayloadVersion = 35,
        PhysicsColliderPayloadVersion = 36,
        LodIndex = 50,
        RenderResolution = 51,
        SkirtDepth = 52,
        AgentRadius = 70,
        AgentHeight = 71,
        AgentMaxSlope = 72,
        AgentMaxClimb = 73,
        CellSize = 90,
        CellHeight = 91,
        TileBorderSize = 92,
        MaxTiles = 93,
        MaxPolysPerTile = 94,
        MaxVertsPerPoly = 95,
        RegionMinSize = 96,
        RegionMergeSize = 97,
        EdgeMaxLen = 98,
        EdgeMaxError = 99,
        DetailSampleDist = 100,
        DetailSampleMaxError = 101,
        NavigationResolution = 120,
        BorderPaddingWorld = 121,
        BorderSampleCount = 122,
        SceneGeometryMaxSlope = 140,
        SceneGeometryTilePadding = 141,
        SceneGeometryAdapterVersion = 142,
        ColliderResolution = 160,
        SavedBuildManifestPath = 180,
        ValidatePayloadFiles = 181,
        RebuildWhenStale = 182,
        StreamingDefaultActiveRadius = 183,
        StreamingDefaultCacheRadius = 184,
        StreamingDefaultHysteresis = 185,
        StreamingDefaultTransitions = 186,
        StreamingGenerationPolicy = 187,
        StreamingMaxReadJobs = 188,
        StreamingMaxCompletedReads = 189,
        StreamingMaxPromotes = 190,
        StreamingMaxDemotes = 191,
        NavigationCacheRootPath = 210,
        NavigationCacheWorldId = 211,
        NavigationCacheFormatVersion = 212,
        RendererLayerMask = 230,
        EnableDistanceCulling = 231,
        PropMaxDrawDistance = 232,
        TerrainMaxDrawDistance = 233,
        Enabled = 250,
        MaxDebugLines = 251,
        MaxNavMeshEdgeLines = 252,
        MaxColliderShapeLines = 253,
        ColliderShapes = 254,
        TerrainTileBounds = 255,
        NavigationMeshEdges = 256,
        CameraFarPlane = 270,
        CameraMaxDistance = 271,
        CameraKeyboardPanSpeed = 272,
        CameraEdgePanSpeed = 273,
        CameraMousePanSensitivity = 274,
        CameraZoomSensitivity = 275,
        CameraPivotOffset = 276,
        CameraYaw = 277,
        CameraPitch = 278,
        CameraDistance = 279,
        CameraMode = 280,
        CameraFollowSmoothing = 281,
        CameraMaxFollowLag = 282,
    };

    struct EditorSettingsReflectionTarget {
        EditorSettingsReflectedObjectId object = EditorSettingsReflectedObjectId::Project;
        uint32_t index = 0;
    };

    struct EditorSettingsReflectionContext {
        EditorProjectSettings* settings = nullptr;
    };

    void registerEditorSettingsReflectionDescriptors(Engine::ReflectionRegistry& registry);
    [[nodiscard]] std::vector<EditorSettingsReflectionTarget> enumerateEditorSettingsTargets(
        const EditorProjectSettings& settings);
    [[nodiscard]] Engine::ReflectionResult getEditorSetting(
        const EditorSettingsReflectionContext& context,
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property);
    [[nodiscard]] Engine::ReflectionResult setEditorSetting(
        const EditorSettingsReflectionContext& context,
        EditorSettingsReflectionTarget target,
        EditorSettingsReflectedPropertyId property,
        const Engine::ReflectedValue& value);

}
