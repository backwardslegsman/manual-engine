#include "Engine/Scene/RendererSceneRenderBackend.hpp"

namespace Engine {
    Renderer::MeshInstanceHandle RendererSceneRenderBackend::createMeshInstance(Renderer::StaticMeshHandle mesh)
    {
        return Renderer::createInstance(mesh);
    }

    void RendererSceneRenderBackend::destroyMeshInstance(Renderer::MeshInstanceHandle instance)
    {
        Renderer::destroyInstance(instance);
    }

    void RendererSceneRenderBackend::setMeshInstanceTransform(Renderer::MeshInstanceHandle instance, const glm::mat4& transform)
    {
        Renderer::setInstanceTransform(instance, transform);
    }

    void RendererSceneRenderBackend::setMeshInstanceMaterial(Renderer::MeshInstanceHandle instance, Renderer::MaterialHandle material)
    {
        Renderer::setInstanceMaterial(instance, material);
    }

    void RendererSceneRenderBackend::clearMeshInstanceMaterial(Renderer::MeshInstanceHandle instance)
    {
        Renderer::clearInstanceMaterial(instance);
    }

    void RendererSceneRenderBackend::setMeshInstanceRenderLayer(Renderer::MeshInstanceHandle instance, Renderer::RenderLayer layer)
    {
        Renderer::setInstanceRenderLayer(instance, layer);
    }

    void RendererSceneRenderBackend::setMeshInstanceVisibilityFlags(Renderer::MeshInstanceHandle instance, Renderer::VisibilityFlags flags)
    {
        Renderer::setInstanceVisibilityFlags(instance, flags);
    }

    void RendererSceneRenderBackend::setMeshInstanceMaxDrawDistance(Renderer::MeshInstanceHandle instance, float maxDrawDistance)
    {
        Renderer::setInstanceMaxDrawDistance(instance, maxDrawDistance);
    }

    void RendererSceneRenderBackend::setMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance, Renderer::RenderGroupHandle group)
    {
        Renderer::setInstanceRenderGroup(instance, group);
    }

    void RendererSceneRenderBackend::clearMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance)
    {
        Renderer::clearInstanceRenderGroup(instance);
    }

    Renderer::SkinnedMeshInstanceHandle RendererSceneRenderBackend::createSkinnedMeshInstance(Renderer::SkinnedMeshHandle mesh)
    {
        return Renderer::createSkinnedInstance(mesh);
    }

    void RendererSceneRenderBackend::destroySkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance)
    {
        Renderer::destroySkinnedInstance(instance);
    }

    void RendererSceneRenderBackend::setSkinnedMeshInstanceTransform(Renderer::SkinnedMeshInstanceHandle instance, const glm::mat4& transform)
    {
        Renderer::setSkinnedInstanceTransform(instance, transform);
    }

    void RendererSceneRenderBackend::setSkinnedMeshInstanceRenderLayer(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderLayer layer)
    {
        Renderer::setSkinnedInstanceRenderLayer(instance, layer);
    }

    void RendererSceneRenderBackend::setSkinnedMeshInstanceMaxDrawDistance(Renderer::SkinnedMeshInstanceHandle instance, float maxDrawDistance)
    {
        Renderer::setSkinnedInstanceMaxDrawDistance(instance, maxDrawDistance);
    }

    void RendererSceneRenderBackend::setSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderGroupHandle group)
    {
        Renderer::setSkinnedInstanceRenderGroup(instance, group);
    }

    void RendererSceneRenderBackend::clearSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance)
    {
        Renderer::clearSkinnedInstanceRenderGroup(instance);
    }

    void RendererSceneRenderBackend::setSkinnedMeshInstanceJointMatrices(
        Renderer::SkinnedMeshInstanceHandle instance,
        const std::vector<glm::mat4>& matrices)
    {
        Renderer::setSkinnedInstanceJointMatrices(instance, matrices);
    }

    Renderer::LightHandle RendererSceneRenderBackend::createLight(const Renderer::LightDescriptor& descriptor)
    {
        return Renderer::createLight(descriptor);
    }

    void RendererSceneRenderBackend::destroyLight(Renderer::LightHandle light)
    {
        Renderer::destroyLight(light);
    }

    void RendererSceneRenderBackend::setLightDescriptor(Renderer::LightHandle light, const Renderer::LightDescriptor& descriptor)
    {
        Renderer::setLightDescriptor(light, descriptor);
    }
}
