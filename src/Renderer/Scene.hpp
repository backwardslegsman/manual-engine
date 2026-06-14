#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Texture.hpp"
#include "Renderer/VertexLayouts.hpp"

namespace Renderer {
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
        bool navigationTileBounds = true;
        bool navigationMeshEdges = false;
        bool navigationCurrentPath = true;
        bool navigationNearestPoint = true;
        bool navigationBlockerBounds = false;
        bool cameraFrustum = false;
        bool actorDestination = true;
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

    struct MaterialDescriptor {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        TextureHandle baseColorTexture;
        TextureHandle normalTexture;
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
    };

    struct AtmosphereSettings {
        glm::vec4 skyColor{0.27f, 0.20f, 0.33f, 1.0f};
        glm::vec4 fogColor{0.42f, 0.48f, 0.56f, 1.0f};
        float fogDensity = 0.018f;
        bool fogEnabled = true;
        glm::vec3 sunDirection{-0.35f, -0.85f, -0.25f};
        glm::vec3 sunColor{1.0f, 0.94f, 0.84f};
        float sunIntensity = 1.0f;
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
        uint32_t meshBatchCount = 0;
        uint32_t largestMeshBatchSize = 0;
        uint32_t liveTerrainTiles = 0;
        uint32_t visibleTerrainTiles = 0;
        uint32_t submittedTerrainTiles = 0;
        uint32_t layerOrFlagCulledTerrainTiles = 0;
        uint32_t frustumCulledTerrainTiles = 0;
        uint32_t distanceCulledTerrainTiles = 0;
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

    bool initSceneRenderer();
    void shutdownSceneRenderer();

    RenderGroupHandle createRenderGroup(const RenderGroupDescriptor& descriptor);
    void destroyRenderGroup(RenderGroupHandle group);
    MaterialHandle createMaterial(const MaterialDescriptor& descriptor);
    void destroyMaterial(MaterialHandle material);
    void setMaterialDescriptor(MaterialHandle material, const MaterialDescriptor& descriptor);
    void setAtmosphereSettings(const AtmosphereSettings& settings);
    const AtmosphereSettings& atmosphereSettings();
    void setDebugDrawSettings(const DebugDrawSettings& settings);
    const DebugDrawSettings& debugDrawSettings();
    StaticMeshHandle loadStaticMesh(const std::filesystem::path& path);
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
    void addDebugAabb(const Aabb& bounds, uint32_t abgr);
    void addDebugXZRect(float minX, float minZ, float maxX, float maxZ, float y, uint32_t abgr);
    void addDebugFrustum(const glm::mat4& inverseViewProjection, uint32_t abgr);
    void drawDebugPrimitives(const RenderView& view);
}
