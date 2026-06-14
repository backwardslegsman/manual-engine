#include "Renderer/Scene.hpp"

namespace Renderer {
    MeshInstanceHandle createInstance(StaticMeshHandle)
    {
        return {};
    }

    void destroyInstance(MeshInstanceHandle)
    {
    }

    TerrainHandle createTerrainTile(
        const std::vector<MeshVertex>&,
        const std::vector<uint32_t>&,
        MaterialHandle)
    {
        return {};
    }

    void destroyTerrainTile(TerrainHandle)
    {
    }

    void setInstanceTransform(MeshInstanceHandle, const glm::mat4&)
    {
    }

    void setInstancePosition(MeshInstanceHandle, const glm::vec3&)
    {
    }

    void setInstanceRotation(MeshInstanceHandle, const glm::vec3&)
    {
    }

    void setInstanceScale(MeshInstanceHandle, const glm::vec3&)
    {
    }
}
