#include "Renderer/Scene.hpp"

#include <vector>

namespace {
    struct InstanceRecord {
        bool alive = false;
        glm::mat4 transform{1.0f};
    };

    std::vector<bool> g_textures;
    std::vector<Renderer::TextureDescriptor> g_textureDescriptors;
    std::vector<bool> g_materials;
    std::vector<Renderer::MaterialDescriptor> g_materialDescriptors;
    std::vector<bool> g_lights;
    std::vector<Renderer::LightDescriptor> g_lightDescriptors;
    std::vector<bool> g_meshes;
    std::vector<Renderer::StaticMeshDescriptor> g_meshDescriptors;
    std::vector<InstanceRecord> g_instances;
    std::vector<bool> g_renderGroups;

    template <typename Container>
    uint32_t storeAlive(Container& container)
    {
        for (uint32_t index = 0; index < container.size(); ++index) {
            if (!container[index]) {
                container[index] = true;
                return index;
            }
        }
        const uint32_t index = static_cast<uint32_t>(container.size());
        container.push_back(true);
        return index;
    }
}

namespace TestRenderer {
    void reset()
    {
        g_textures.clear();
        g_textureDescriptors.clear();
        g_materials.clear();
        g_materialDescriptors.clear();
        g_lights.clear();
        g_lightDescriptors.clear();
        g_meshes.clear();
        g_meshDescriptors.clear();
        g_instances.clear();
        g_renderGroups.clear();
    }

    uint32_t liveMeshCount()
    {
        uint32_t count = 0;
        for (bool alive : g_meshes) {
            count += alive ? 1u : 0u;
        }
        return count;
    }

    uint32_t liveMaterialCount()
    {
        uint32_t count = 0;
        for (bool alive : g_materials) {
            count += alive ? 1u : 0u;
        }
        return count;
    }

    uint32_t liveTextureCount()
    {
        uint32_t count = 0;
        for (bool alive : g_textures) {
            count += alive ? 1u : 0u;
        }
        return count;
    }

    uint32_t liveInstanceCount()
    {
        uint32_t count = 0;
        for (const InstanceRecord& instance : g_instances) {
            count += instance.alive ? 1u : 0u;
        }
        return count;
    }

    uint32_t liveLightCount()
    {
        uint32_t count = 0;
        for (bool alive : g_lights) {
            count += alive ? 1u : 0u;
        }
        return count;
    }

    uint32_t liveRenderGroupCount()
    {
        uint32_t count = 0;
        for (bool alive : g_renderGroups) {
            count += alive ? 1u : 0u;
        }
        return count;
    }

    Renderer::LightDescriptor lightDescriptor(Renderer::LightHandle handle)
    {
        if (handle.id >= g_lightDescriptors.size()) {
            return {};
        }
        return g_lightDescriptors[handle.id];
    }

    glm::mat4 instanceTransform(Renderer::MeshInstanceHandle handle)
    {
        if (handle.id >= g_instances.size()) {
            return glm::mat4{1.0f};
        }
        return g_instances[handle.id].transform;
    }

    Renderer::MaterialDescriptor materialDescriptor(Renderer::MaterialHandle handle)
    {
        if (handle.id >= g_materialDescriptors.size()) {
            return {};
        }
        return g_materialDescriptors[handle.id];
    }

    Renderer::MaterialDescriptor firstMaterialDescriptor()
    {
        for (uint32_t index = 0; index < g_materials.size(); ++index) {
            if (g_materials[index] && index < g_materialDescriptors.size()) {
                return g_materialDescriptors[index];
            }
        }
        return {};
    }

    Renderer::MaterialDiagnostics firstMaterialDiagnostics()
    {
        for (uint32_t index = 0; index < g_materials.size(); ++index) {
            if (g_materials[index] && index < g_materialDescriptors.size()) {
                return Renderer::materialDiagnostics(Renderer::MaterialHandle{index});
            }
        }
        return {};
    }

    Renderer::StaticMeshDescriptor firstMeshDescriptor()
    {
        for (uint32_t index = 0; index < g_meshes.size(); ++index) {
            if (g_meshes[index] && index < g_meshDescriptors.size()) {
                return g_meshDescriptors[index];
            }
        }
        return {};
    }

    Renderer::TextureDescriptor textureDescriptor(Renderer::TextureHandle handle)
    {
        if (handle.id >= g_textureDescriptors.size()) {
            return {};
        }
        return g_textureDescriptors[handle.id];
    }
}

namespace Renderer {
    RenderGroupHandle createRenderGroup(const RenderGroupDescriptor&)
    {
        return {storeAlive(g_renderGroups)};
    }

    void destroyRenderGroup(RenderGroupHandle group)
    {
        if (group.id < g_renderGroups.size()) {
            g_renderGroups[group.id] = false;
        }
    }

    bool isValid(TextureHandle handle)
    {
        return handle.id < g_textures.size() && g_textures[handle.id];
    }

    TextureHandle loadTexture(const std::filesystem::path& path)
    {
        return loadTexture(path, {});
    }

    TextureHandle loadTexture(const std::filesystem::path&, const TextureDescriptor& descriptor)
    {
        const uint32_t index = storeAlive(g_textures);
        if (index >= g_textureDescriptors.size()) {
            g_textureDescriptors.resize(index + 1);
        }
        g_textureDescriptors[index] = descriptor;
        return {index};
    }

    TextureHandle createSolidTexture(uint8_t, uint8_t, uint8_t, uint8_t)
    {
        const uint32_t index = storeAlive(g_textures);
        if (index >= g_textureDescriptors.size()) {
            g_textureDescriptors.resize(index + 1);
        }
        return {index};
    }

    TextureInfo textureInfo(TextureHandle handle)
    {
        TextureInfo info;
        if (handle.id < g_textures.size() && g_textures[handle.id]) {
            info.valid = true;
            info.width = 4;
            info.height = 4;
            info.mipCount = g_textureDescriptors[handle.id].generateMips ? 3 : 1;
            info.estimatedBytes = g_textureDescriptors[handle.id].generateMips ? 84 : 64;
            info.srgbRequested = g_textureDescriptors[handle.id].colorSpace == TextureColorSpace::Srgb;
            info.srgbApplied = info.srgbRequested;
            info.descriptor = g_textureDescriptors[handle.id];
        }
        return info;
    }

    void destroyTexture(TextureHandle handle)
    {
        if (handle.id < g_textures.size()) {
            g_textures[handle.id] = false;
        }
    }

    bgfx::TextureHandle getNativeTexture(TextureHandle)
    {
        return BGFX_INVALID_HANDLE;
    }

    void destroyTextures()
    {
        g_textures.clear();
    }

    MaterialHandle createMaterial(const MaterialDescriptor& descriptor)
    {
        const uint32_t index = storeAlive(g_materials);
        if (index >= g_materialDescriptors.size()) {
            g_materialDescriptors.resize(index + 1);
        }
        g_materialDescriptors[index] = descriptor;
        return {index};
    }

    LightHandle createLight(const LightDescriptor& descriptor)
    {
        const uint32_t index = storeAlive(g_lights);
        if (index >= g_lightDescriptors.size()) {
            g_lightDescriptors.resize(index + 1);
        }
        g_lightDescriptors[index] = descriptor;
        return {index};
    }

    void destroyLight(LightHandle light)
    {
        if (light.id < g_lights.size()) {
            g_lights[light.id] = false;
        }
    }

    void setLightDescriptor(LightHandle light, const LightDescriptor& descriptor)
    {
        if (light.id < g_lightDescriptors.size()) {
            g_lightDescriptors[light.id] = descriptor;
        }
    }

    LightDiagnostics lightDiagnostics(LightHandle light)
    {
        LightDiagnostics diagnostics;
        if (light.id >= g_lights.size() || !g_lights[light.id] || light.id >= g_lightDescriptors.size()) {
            return diagnostics;
        }
        diagnostics.valid = true;
        diagnostics.descriptor = g_lightDescriptors[light.id];
        diagnostics.active = diagnostics.descriptor.enabled && diagnostics.descriptor.intensity > 0.0f;
        diagnostics.inForwardBudget = diagnostics.active && light.id < MaxForwardLights;
        return diagnostics;
    }

    void destroyMaterial(MaterialHandle material)
    {
        if (material.id < g_materials.size()) {
            g_materials[material.id] = false;
        }
    }

    void setMaterialDescriptor(MaterialHandle material, const MaterialDescriptor& descriptor)
    {
        if (material.id < g_materialDescriptors.size()) {
            g_materialDescriptors[material.id] = descriptor;
        }
    }

    MaterialDiagnostics materialDiagnostics(MaterialHandle material)
    {
        MaterialDiagnostics diagnostics;
        if (material.id >= g_materials.size() || !g_materials[material.id] || material.id >= g_materialDescriptors.size()) {
            return diagnostics;
        }

        const MaterialDescriptor& descriptor = g_materialDescriptors[material.id];
        diagnostics.valid = true;
        diagnostics.name = descriptor.name;
        diagnostics.hasBaseColorTexture = isValid(descriptor.baseColorTexture);
        diagnostics.hasNormalTexture = isValid(descriptor.normalTexture);
        diagnostics.hasMetallicTexture = isValid(descriptor.metallicTexture);
        diagnostics.hasRoughnessTexture = isValid(descriptor.roughnessTexture);
        diagnostics.hasPackedMetallicRoughnessTexture = isValid(descriptor.metallicRoughnessTexture);
        diagnostics.hasOcclusionTexture = isValid(descriptor.occlusionTexture);
        diagnostics.hasEmissiveTexture = isValid(descriptor.emissiveTexture);
        diagnostics.packedMetallicChannel = descriptor.metallicRoughnessMetallicChannel;
        diagnostics.packedRoughnessChannel = descriptor.metallicRoughnessRoughnessChannel;
        diagnostics.alphaMode = descriptor.alphaMode;
        diagnostics.alphaCutoff = descriptor.alphaCutoff;
        diagnostics.doubleSided = descriptor.doubleSided;
        switch (descriptor.alphaMode) {
            case MaterialDescriptor::AlphaMode::Mask:
                diagnostics.renderPass = MaterialRenderPass::AlphaMask;
                break;
            case MaterialDescriptor::AlphaMode::Blend:
                diagnostics.renderPass = MaterialRenderPass::AlphaBlend;
                break;
            case MaterialDescriptor::AlphaMode::Opaque:
            default:
                diagnostics.renderPass = MaterialRenderPass::Opaque;
                break;
        }
        return diagnostics;
    }

    StaticMeshHandle createStaticMesh(const StaticMeshDescriptor& descriptor)
    {
        if (descriptor.submeshes.empty()) {
            return {};
        }
        const uint32_t index = storeAlive(g_meshes);
        if (index >= g_meshDescriptors.size()) {
            g_meshDescriptors.resize(index + 1);
        }
        g_meshDescriptors[index] = descriptor;
        return {index};
    }

    StaticMeshHandle loadStaticMesh(const std::filesystem::path&)
    {
        return {storeAlive(g_meshes)};
    }

    StaticMeshHandle createTexturedCubeMesh()
    {
        return {storeAlive(g_meshes)};
    }

    void destroyStaticMesh(StaticMeshHandle mesh)
    {
        if (mesh.id < g_meshes.size()) {
            g_meshes[mesh.id] = false;
        }
    }

    MeshInstanceHandle createInstance(StaticMeshHandle mesh)
    {
        if (mesh.id >= g_meshes.size() || !g_meshes[mesh.id]) {
            return {};
        }

        for (uint32_t index = 0; index < g_instances.size(); ++index) {
            if (!g_instances[index].alive) {
                g_instances[index] = {true, glm::mat4{1.0f}};
                return {index};
            }
        }
        const uint32_t index = static_cast<uint32_t>(g_instances.size());
        g_instances.push_back({true, glm::mat4{1.0f}});
        return {index};
    }

    void destroyInstance(MeshInstanceHandle instance)
    {
        if (instance.id < g_instances.size()) {
            g_instances[instance.id].alive = false;
        }
    }

    void setInstanceTransform(MeshInstanceHandle instance, const glm::mat4& transform)
    {
        if (instance.id < g_instances.size() && g_instances[instance.id].alive) {
            g_instances[instance.id].transform = transform;
        }
    }

    void setInstanceRenderLayer(MeshInstanceHandle, RenderLayer)
    {
    }

    void setInstanceMaxDrawDistance(MeshInstanceHandle, float)
    {
    }

    void setInstanceRenderGroup(MeshInstanceHandle, RenderGroupHandle)
    {
    }
}
