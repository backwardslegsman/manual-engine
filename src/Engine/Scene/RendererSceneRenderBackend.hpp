#pragma once

#include "Engine/Scene/SceneRenderBridge.hpp"

namespace Engine {
    class RendererSceneRenderBackend final : public SceneRenderBackend {
    public:
        [[nodiscard]] Renderer::MeshInstanceHandle createMeshInstance(Renderer::StaticMeshHandle mesh) override;
        void destroyMeshInstance(Renderer::MeshInstanceHandle instance) override;
        void setMeshInstanceTransform(Renderer::MeshInstanceHandle instance, const glm::mat4& transform) override;
        void setMeshInstanceMaterial(Renderer::MeshInstanceHandle instance, Renderer::MaterialHandle material) override;
        void clearMeshInstanceMaterial(Renderer::MeshInstanceHandle instance) override;
        void setMeshInstanceRenderLayer(Renderer::MeshInstanceHandle instance, Renderer::RenderLayer layer) override;
        void setMeshInstanceVisibilityFlags(Renderer::MeshInstanceHandle instance, Renderer::VisibilityFlags flags) override;
        void setMeshInstanceMaxDrawDistance(Renderer::MeshInstanceHandle instance, float maxDrawDistance) override;
        void setMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance, Renderer::RenderGroupHandle group) override;
        void clearMeshInstanceRenderGroup(Renderer::MeshInstanceHandle instance) override;

        [[nodiscard]] Renderer::SkinnedMeshInstanceHandle createSkinnedMeshInstance(Renderer::SkinnedMeshHandle mesh) override;
        void destroySkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle instance) override;
        void setSkinnedMeshInstanceTransform(Renderer::SkinnedMeshInstanceHandle instance, const glm::mat4& transform) override;
        void setSkinnedMeshInstanceRenderLayer(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderLayer layer) override;
        void setSkinnedMeshInstanceMaxDrawDistance(Renderer::SkinnedMeshInstanceHandle instance, float maxDrawDistance) override;
        void setSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance, Renderer::RenderGroupHandle group) override;
        void clearSkinnedMeshInstanceRenderGroup(Renderer::SkinnedMeshInstanceHandle instance) override;
        void setSkinnedMeshInstanceJointMatrices(
            Renderer::SkinnedMeshInstanceHandle instance,
            const std::vector<glm::mat4>& matrices) override;

        [[nodiscard]] Renderer::LightHandle createLight(const Renderer::LightDescriptor& descriptor) override;
        void destroyLight(Renderer::LightHandle light) override;
        void setLightDescriptor(Renderer::LightHandle light, const Renderer::LightDescriptor& descriptor) override;
    };
}
