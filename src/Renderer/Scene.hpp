#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Texture.hpp"
#include "Renderer/VertexLayouts.hpp"

namespace Renderer {
    constexpr uint32_t MaxForwardLights = 16;

    enum class RenderLayer : uint32_t {
        None = 0,
        Terrain = 1u << 0,
        Props = 1u << 1,
        Debug = 1u << 2,
        Transparent = 1u << 3,
        All = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3),
    };

    enum class VisibilityFlags : uint32_t {
        None = 0,
        Visible = 1u << 0,
    };

    struct Aabb {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    struct RenderVisibility {
        RenderLayer layer = RenderLayer::Props;
        VisibilityFlags flags = VisibilityFlags::Visible;
        float maxDrawDistance = 0.0f;
    };

    struct DebugDrawSettings {
        bool enabled = true;
        bool selectedBounds = true;
        bool collisionBounds = true;
        bool chunkBorders = true;
        bool terrainTileBounds = true;
        bool terrainSlopeWarnings = false;
        bool navigationTileBounds = true;
        bool navigationMeshEdges = false;
        bool navigationCurrentPath = true;
        bool navigationNearestPoint = true;
        bool navigationBlockerBounds = false;
        bool navigationPortals = true;
        bool navigationConnectivityLinks = true;
        bool worldNavigationGraphNodes = false;
        bool worldNavigationGraphEdges = false;
        bool worldNavigationRoute = true;
        bool cameraFrustum = false;
        bool actorDestination = true;
        uint32_t maxDebugLines = 20000;
        uint32_t maxNavMeshEdgeLines = 6000;
        uint32_t maxWorldGraphEdgeLines = 4000;
        uint32_t maxTerrainSlopeWarningLines = 2000;
        uint32_t maxCollisionAabbs = 2000;
        uint32_t maxChunkBorderRects = 2000;
    };

    struct DebugLinePrimitive {
        glm::vec3 a{};
        glm::vec3 b{};
        uint32_t abgr = 0xffffffff;
    };

    struct DebugPrimitiveBatch {
        std::vector<DebugLinePrimitive> lines;
    };

    struct DebugPrimitiveCategoryStats {
        uint32_t generated = 0;
        uint32_t submitted = 0;
        uint32_t clipped = 0;
    };

    struct DebugDrawStats {
        uint32_t generatedLines = 0;
        uint32_t submittedLines = 0;
        uint32_t clippedLines = 0;
        uint32_t lastFramePrimitiveBufferSize = 0;
        DebugPrimitiveCategoryStats navMeshEdges;
        DebugPrimitiveCategoryStats worldGraphEdges;
        DebugPrimitiveCategoryStats terrainSlopeWarnings;
        DebugPrimitiveCategoryStats collisionBounds;
        DebugPrimitiveCategoryStats chunkBorders;
    };

    struct RenderGroupHandle {
        uint32_t id = UINT32_MAX;
    };

    struct RenderGroupDescriptor {
        std::string name;
        bool hasChunkCoord = false;
        int32_t chunkX = 0;
        int32_t chunkZ = 0;
    };

    enum class LightType {
        Directional,
        Point,
        Spot,
    };

    struct LightHandle {
        uint32_t id = UINT32_MAX;
    };

    struct LightDescriptor {
        std::string name;
        LightType type = LightType::Point;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        float range = 0.0f;
        float innerConeAngle = 0.0f;
        float outerConeAngle = 0.7853982f;
        bool enabled = true;
    };

    struct LightDiagnostics {
        bool valid = false;
        LightDescriptor descriptor;
        bool active = false;
        bool inForwardBudget = false;
    };

    enum class MaterialRenderPass {
        Opaque,
        AlphaMask,
        AlphaBlend,
    };

    struct MaterialDescriptor {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        TextureHandle baseColorTexture;
        TextureHandle normalTexture;
        float normalScale = 1.0f;
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        TextureHandle metallicTexture;
        TextureHandle roughnessTexture;
        TextureHandle metallicRoughnessTexture;
        TextureHandle occlusionTexture;
        float occlusionStrength = 1.0f;
        TextureHandle emissiveTexture;
        glm::vec3 emissiveFactor{0.0f};
        enum class AlphaMode {
            Opaque,
            Mask,
            Blend,
        };
        enum class TextureChannel {
            R = 0,
            G = 1,
            B = 2,
            A = 3,
        };
        enum class TextureColorSpace {
            Linear,
            Srgb,
        };
        enum class TextureWrap {
            Repeat,
            ClampToEdge,
            MirroredRepeat,
        };
        enum class TextureFilter {
            Nearest,
            Linear,
        };
        struct TextureSlotHints {
            TextureColorSpace colorSpace = TextureColorSpace::Linear;
            TextureWrap wrapU = TextureWrap::Repeat;
            TextureWrap wrapV = TextureWrap::Repeat;
            TextureFilter minFilter = TextureFilter::Linear;
            TextureFilter magFilter = TextureFilter::Linear;
        };
        TextureChannel metallicRoughnessMetallicChannel = TextureChannel::B;
        TextureChannel metallicRoughnessRoughnessChannel = TextureChannel::G;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        TextureSlotHints baseColorTextureHints{TextureColorSpace::Srgb};
        TextureSlotHints normalTextureHints;
        TextureSlotHints metallicTextureHints;
        TextureSlotHints roughnessTextureHints;
        TextureSlotHints metallicRoughnessTextureHints;
        TextureSlotHints occlusionTextureHints;
        TextureSlotHints emissiveTextureHints{TextureColorSpace::Srgb};
    };

    struct MaterialDiagnostics {
        bool valid = false;
        std::string name;
        bool hasBaseColorTexture = false;
        bool hasNormalTexture = false;
        bool hasMetallicTexture = false;
        bool hasRoughnessTexture = false;
        bool hasPackedMetallicRoughnessTexture = false;
        bool hasOcclusionTexture = false;
        bool hasEmissiveTexture = false;
        MaterialDescriptor::TextureChannel packedMetallicChannel = MaterialDescriptor::TextureChannel::B;
        MaterialDescriptor::TextureChannel packedRoughnessChannel = MaterialDescriptor::TextureChannel::G;
        MaterialDescriptor::AlphaMode alphaMode = MaterialDescriptor::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        MaterialRenderPass renderPass = MaterialRenderPass::Opaque;
    };

    struct AtmosphereSettings {
        glm::vec4 skyColor{0.27f, 0.20f, 0.33f, 1.0f};
        glm::vec4 fogColor{0.42f, 0.48f, 0.56f, 1.0f};
        float fogDensity = 0.018f;
        bool fogEnabled = false;
        glm::vec3 sunDirection{-0.35f, -0.85f, -0.25f};
        glm::vec3 sunColor{1.0f, 0.94f, 0.84f};
        float sunIntensity = 1.0f;
        float exposure = 1.0f;
        float ambientIntensity = 0.08f;
        glm::vec3 environmentDiffuseColor{1.0f};
        float environmentDiffuseIntensity = 1.0f;
        bool environmentEnabled = true;
    };

    struct Frustum {
        glm::vec4 planes[6]{};
    };

    struct RenderGroupDrawStats {
        RenderGroupHandle group;
        std::string name;
        bool hasChunkCoord = false;
        int32_t chunkX = 0;
        int32_t chunkZ = 0;
        uint32_t liveMeshInstances = 0;
        uint32_t visibleMeshInstances = 0;
        uint32_t submittedMeshInstances = 0;
        uint32_t layerOrFlagCulledMeshInstances = 0;
        uint32_t frustumCulledMeshInstances = 0;
        uint32_t distanceCulledMeshInstances = 0;
        uint32_t liveTerrainTiles = 0;
        uint32_t visibleTerrainTiles = 0;
        uint32_t submittedTerrainTiles = 0;
        uint32_t layerOrFlagCulledTerrainTiles = 0;
        uint32_t frustumCulledTerrainTiles = 0;
        uint32_t distanceCulledTerrainTiles = 0;
    };

    struct SceneDrawStats {
        uint32_t liveMeshInstances = 0;
        uint32_t visibleMeshInstances = 0;
        uint32_t submittedMeshInstances = 0;
        uint32_t layerOrFlagCulledMeshInstances = 0;
        uint32_t frustumCulledMeshInstances = 0;
        uint32_t distanceCulledMeshInstances = 0;
        uint32_t visibleMeshDrawItems = 0;
        uint32_t submittedMeshDrawItems = 0;
        uint32_t visibleOpaqueMeshDrawItems = 0;
        uint32_t visibleAlphaMaskMeshDrawItems = 0;
        uint32_t visibleAlphaBlendMeshDrawItems = 0;
        uint32_t submittedOpaqueMeshDrawItems = 0;
        uint32_t submittedAlphaMaskMeshDrawItems = 0;
        uint32_t submittedAlphaBlendMeshDrawItems = 0;
        uint32_t meshBatchCount = 0;
        uint32_t opaqueMeshBatchCount = 0;
        uint32_t alphaMaskMeshBatchCount = 0;
        uint32_t alphaBlendMeshBatchCount = 0;
        uint32_t largestMeshBatchSize = 0;
        uint32_t liveTerrainTiles = 0;
        uint32_t visibleTerrainTiles = 0;
        uint32_t submittedTerrainTiles = 0;
        uint32_t layerOrFlagCulledTerrainTiles = 0;
        uint32_t frustumCulledTerrainTiles = 0;
        uint32_t distanceCulledTerrainTiles = 0;
        uint32_t liveLightCount = 0;
        uint32_t activeLightCount = 0;
        uint32_t submittedForwardLightCount = 0;
        uint32_t disabledLightCount = 0;
        uint32_t overBudgetLightCount = 0;
        std::vector<RenderGroupDrawStats> renderGroups;
    };

    // Renderer-facing view context for one scene submission. viewProjection
    // must match projection * view; App remains responsible for bgfx view setup.
    struct RenderView {
        bgfx::ViewId viewId = 0;
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::mat4 viewProjection{1.0f};
        glm::vec3 cameraPosition{0.0f};
        uint16_t viewportWidth = 0;
        uint16_t viewportHeight = 0;
        uint32_t layerMask = static_cast<uint32_t>(RenderLayer::All);
        bool enableDistanceCulling = true;
    };

    struct StaticMeshHandle {
        uint32_t id = UINT32_MAX;
    };

    struct MaterialHandle {
        uint32_t id = UINT32_MAX;
    };

    struct MeshInstanceHandle {
        uint32_t id = UINT32_MAX;
    };

    struct TerrainHandle {
        uint32_t id = UINT32_MAX;
    };

    struct StaticSubmeshDescriptor {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        MaterialHandle material;
    };

    struct StaticMeshDescriptor {
        std::string name;
        std::vector<StaticSubmeshDescriptor> submeshes;
    };

    bool initSceneRenderer();
    void shutdownSceneRenderer();

    RenderGroupHandle createRenderGroup(const RenderGroupDescriptor& descriptor);
    void destroyRenderGroup(RenderGroupHandle group);
    LightHandle createLight(const LightDescriptor& descriptor);
    void destroyLight(LightHandle light);
    void setLightDescriptor(LightHandle light, const LightDescriptor& descriptor);
    LightDiagnostics lightDiagnostics(LightHandle light);
    MaterialHandle createMaterial(const MaterialDescriptor& descriptor);
    void destroyMaterial(MaterialHandle material);
    void setMaterialDescriptor(MaterialHandle material, const MaterialDescriptor& descriptor);
    MaterialDiagnostics materialDiagnostics(MaterialHandle material);
    void setAtmosphereSettings(const AtmosphereSettings& settings);
    const AtmosphereSettings& atmosphereSettings();
    void setDebugDrawSettings(const DebugDrawSettings& settings);
    const DebugDrawSettings& debugDrawSettings();
    StaticMeshHandle loadStaticMesh(const std::filesystem::path& path);
    StaticMeshHandle createStaticMesh(const StaticMeshDescriptor& descriptor);
    StaticMeshHandle createTexturedCubeMesh();
    void destroyStaticMesh(StaticMeshHandle mesh);
    MeshInstanceHandle createInstance(StaticMeshHandle mesh);
    void destroyInstance(MeshInstanceHandle instance);
    TerrainHandle createTerrainTile(
        const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices,
        MaterialHandle material
    );
    void destroyTerrainTile(TerrainHandle terrain);

    void setInstancePosition(MeshInstanceHandle instance, const glm::vec3& position);
    void setInstanceRotation(MeshInstanceHandle instance, const glm::vec3& eulerRadians);
    void setInstanceScale(MeshInstanceHandle instance, const glm::vec3& scale);
    void setInstanceTransform(MeshInstanceHandle instance, const glm::mat4& transform);
    void setInstanceMaterial(MeshInstanceHandle instance, MaterialHandle material);
    void clearInstanceMaterial(MeshInstanceHandle instance);
    void setInstanceRenderLayer(MeshInstanceHandle instance, RenderLayer layer);
    void setInstanceVisibilityFlags(MeshInstanceHandle instance, VisibilityFlags flags);
    void setInstanceMaxDrawDistance(MeshInstanceHandle instance, float maxDrawDistance);
    void setInstanceRenderGroup(MeshInstanceHandle instance, RenderGroupHandle group);
    void clearInstanceRenderGroup(MeshInstanceHandle instance);
    void setTerrainMaterial(TerrainHandle terrain, MaterialHandle material);
    void setTerrainRenderLayer(TerrainHandle terrain, RenderLayer layer);
    void setTerrainVisibilityFlags(TerrainHandle terrain, VisibilityFlags flags);
    void setTerrainMaxDrawDistance(TerrainHandle terrain, float maxDrawDistance);
    void setTerrainRenderGroup(TerrainHandle terrain, RenderGroupHandle group);
    void clearTerrainRenderGroup(TerrainHandle terrain);

    SceneDrawStats drawScene(const RenderView& view);
    void clearDebugPrimitives();
    void addDebugLine(const glm::vec3& a, const glm::vec3& b, uint32_t abgr);
    void addDebugLines(std::span<const DebugLinePrimitive> lines);
    void addDebugAabb(const Aabb& bounds, uint32_t abgr);
    void addDebugXZRect(float minX, float minZ, float maxX, float maxZ, float y, uint32_t abgr);
    void addDebugFrustum(const glm::mat4& inverseViewProjection, uint32_t abgr);
    DebugDrawStats& debugDrawStats();
    void drawDebugPrimitives(const RenderView& view);
}
