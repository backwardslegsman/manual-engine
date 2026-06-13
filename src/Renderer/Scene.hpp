#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Texture.hpp"
#include "Renderer/VertexLayouts.hpp"

namespace Renderer {
    struct Aabb {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    struct Frustum {
        glm::vec4 planes[6]{};
    };

    struct SceneDrawStats {
        uint32_t liveMeshInstances = 0;
        uint32_t visibleMeshInstances = 0;
        uint32_t submittedMeshInstances = 0;
        uint32_t liveTerrainTiles = 0;
        uint32_t visibleTerrainTiles = 0;
        uint32_t submittedTerrainTiles = 0;
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

    StaticMeshHandle loadStaticMesh(const std::filesystem::path& path);
    StaticMeshHandle createTexturedCubeMesh();
    void destroyStaticMesh(StaticMeshHandle mesh);
    MeshInstanceHandle createInstance(StaticMeshHandle mesh);
    void destroyInstance(MeshInstanceHandle instance);
    TerrainHandle createTerrainTile(
        const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices,
        TextureHandle baseColorTexture
    );
    void destroyTerrainTile(TerrainHandle terrain);

    void setInstancePosition(MeshInstanceHandle instance, const glm::vec3& position);
    void setInstanceRotation(MeshInstanceHandle instance, const glm::vec3& eulerRadians);
    void setInstanceScale(MeshInstanceHandle instance, const glm::vec3& scale);
    void setInstanceTransform(MeshInstanceHandle instance, const glm::mat4& transform);
    void setInstanceBaseColorTexture(MeshInstanceHandle instance, TextureHandle texture);

    SceneDrawStats drawScene(const RenderView& view);
}
