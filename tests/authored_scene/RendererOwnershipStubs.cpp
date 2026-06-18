#include "Renderer/Scene.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace {
    struct InstanceRecord {
        bool alive = false;
        glm::mat4 transform{1.0f};
    };

    struct SkinnedInstanceRecord {
        bool alive = false;
        Renderer::SkinnedMeshHandle mesh;
        glm::mat4 transform{1.0f};
        Renderer::RenderVisibility visibility;
        Renderer::RenderGroupHandle renderGroup;
        std::vector<glm::mat4> jointMatrices;
        uint32_t submittedJointCount = 0;
        uint32_t truncatedJointCount = 0;
    };

    std::vector<bool> g_textures;
    std::vector<Renderer::TextureDescriptor> g_textureDescriptors;
    std::vector<bool> g_materials;
    std::vector<Renderer::MaterialDescriptor> g_materialDescriptors;
    std::vector<bool> g_lights;
    std::vector<Renderer::LightDescriptor> g_lightDescriptors;
    std::vector<bool> g_meshes;
    std::vector<Renderer::StaticMeshDescriptor> g_meshDescriptors;
    std::vector<bool> g_skinnedMeshes;
    std::vector<Renderer::SkinnedMeshDescriptor> g_skinnedMeshDescriptors;
    std::vector<InstanceRecord> g_instances;
    std::vector<SkinnedInstanceRecord> g_skinnedInstances;
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
        g_skinnedMeshes.clear();
        g_skinnedMeshDescriptors.clear();
        g_instances.clear();
        g_skinnedInstances.clear();
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

    uint32_t liveSkinnedMeshCount()
    {
        uint32_t count = 0;
        for (bool alive : g_skinnedMeshes) {
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

    uint32_t liveSkinnedInstanceCount()
    {
        uint32_t count = 0;
        for (const SkinnedInstanceRecord& instance : g_skinnedInstances) {
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

    glm::mat4 skinnedInstanceTransform(Renderer::SkinnedMeshInstanceHandle handle)
    {
        if (handle.id >= g_skinnedInstances.size()) {
            return glm::mat4{1.0f};
        }
        return g_skinnedInstances[handle.id].transform;
    }

    std::vector<glm::mat4> skinnedInstanceJointMatrices(Renderer::SkinnedMeshInstanceHandle handle)
    {
        if (handle.id >= g_skinnedInstances.size()) {
            return {};
        }
        return g_skinnedInstances[handle.id].jointMatrices;
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

    Renderer::SkinnedMeshDescriptor firstSkinnedMeshDescriptor()
    {
        for (uint32_t index = 0; index < g_skinnedMeshes.size(); ++index) {
            if (g_skinnedMeshes[index] && index < g_skinnedMeshDescriptors.size()) {
                return g_skinnedMeshDescriptors[index];
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

    SkinnedMeshHandle createSkinnedMesh(const SkinnedMeshDescriptor& descriptor)
    {
        if (descriptor.submeshes.empty()) {
            return {};
        }
        const uint32_t index = storeAlive(g_skinnedMeshes);
        if (index >= g_skinnedMeshDescriptors.size()) {
            g_skinnedMeshDescriptors.resize(index + 1);
        }
        g_skinnedMeshDescriptors[index] = descriptor;
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

    void destroySkinnedMesh(SkinnedMeshHandle mesh)
    {
        if (mesh.id < g_skinnedMeshes.size()) {
            for (SkinnedInstanceRecord& instance : g_skinnedInstances) {
                if (instance.alive && instance.mesh.id == mesh.id) {
                    instance = {};
                }
            }
            g_skinnedMeshes[mesh.id] = false;
        }
    }

    SkinnedMeshDiagnostics skinnedMeshDiagnostics(SkinnedMeshHandle mesh)
    {
        SkinnedMeshDiagnostics diagnostics;
        if (mesh.id >= g_skinnedMeshes.size() || !g_skinnedMeshes[mesh.id] || mesh.id >= g_skinnedMeshDescriptors.size()) {
            return diagnostics;
        }

        const SkinnedMeshDescriptor& descriptor = g_skinnedMeshDescriptors[mesh.id];
        diagnostics.valid = true;
        diagnostics.name = descriptor.name;
        diagnostics.submeshCount = static_cast<uint32_t>(descriptor.submeshes.size());
        diagnostics.jointCount = descriptor.jointCount;
        diagnostics.maxInfluenceCount = descriptor.maxInfluencesPerVertex;
        diagnostics.truncatedInfluenceVertexCount = descriptor.truncatedInfluenceVertexCount;
        diagnostics.zeroWeightVertexCount = descriptor.zeroWeightVertexCount;
        diagnostics.normalizedWeightVertexCount = descriptor.normalizedWeightVertexCount;
        for (const SkinnedSubmeshDescriptor& submesh : descriptor.submeshes) {
            diagnostics.vertexCount += static_cast<uint32_t>(submesh.vertices.size());
            diagnostics.indexCount += static_cast<uint32_t>(submesh.indices.size());
            if (submesh.material.id < g_materials.size() && g_materials[submesh.material.id]) {
                ++diagnostics.validMaterialReferenceCount;
            } else {
                ++diagnostics.invalidMaterialReferenceCount;
            }
        }
        return diagnostics;
    }

    SkinnedMeshInstanceHandle createSkinnedInstance(SkinnedMeshHandle mesh)
    {
        if (mesh.id >= g_skinnedMeshes.size() || !g_skinnedMeshes[mesh.id]) {
            return {};
        }

        for (uint32_t index = 0; index < g_skinnedInstances.size(); ++index) {
            if (!g_skinnedInstances[index].alive) {
                g_skinnedInstances[index] = {};
                g_skinnedInstances[index].alive = true;
                g_skinnedInstances[index].mesh = mesh;
                g_skinnedInstances[index].visibility = {RenderLayer::Props, VisibilityFlags::Visible, 0.0f};
                return {index};
            }
        }

        const uint32_t index = static_cast<uint32_t>(g_skinnedInstances.size());
        SkinnedInstanceRecord instance;
        instance.alive = true;
        instance.mesh = mesh;
        instance.visibility = {RenderLayer::Props, VisibilityFlags::Visible, 0.0f};
        g_skinnedInstances.push_back(std::move(instance));
        return {index};
    }

    void destroySkinnedInstance(SkinnedMeshInstanceHandle instance)
    {
        if (instance.id < g_skinnedInstances.size()) {
            g_skinnedInstances[instance.id] = {};
        }
    }

    void setSkinnedInstanceTransform(SkinnedMeshInstanceHandle instance, const glm::mat4& transform)
    {
        if (instance.id < g_skinnedInstances.size() && g_skinnedInstances[instance.id].alive) {
            g_skinnedInstances[instance.id].transform = transform;
        }
    }

    void setSkinnedInstanceRenderLayer(SkinnedMeshInstanceHandle instance, RenderLayer layer)
    {
        if (instance.id < g_skinnedInstances.size() && g_skinnedInstances[instance.id].alive) {
            g_skinnedInstances[instance.id].visibility.layer = layer;
        }
    }

    void setSkinnedInstanceMaxDrawDistance(SkinnedMeshInstanceHandle instance, float maxDrawDistance)
    {
        if (instance.id < g_skinnedInstances.size() && g_skinnedInstances[instance.id].alive) {
            g_skinnedInstances[instance.id].visibility.maxDrawDistance = maxDrawDistance;
        }
    }

    void setSkinnedInstanceRenderGroup(SkinnedMeshInstanceHandle instance, RenderGroupHandle group)
    {
        if (instance.id < g_skinnedInstances.size() && g_skinnedInstances[instance.id].alive) {
            g_skinnedInstances[instance.id].renderGroup = group;
        }
    }

    void clearSkinnedInstanceRenderGroup(SkinnedMeshInstanceHandle instance)
    {
        if (instance.id < g_skinnedInstances.size() && g_skinnedInstances[instance.id].alive) {
            g_skinnedInstances[instance.id].renderGroup = {};
        }
    }

    void setSkinnedInstanceJointMatrices(SkinnedMeshInstanceHandle instance, std::span<const glm::mat4> matrices)
    {
        if (instance.id >= g_skinnedInstances.size() || !g_skinnedInstances[instance.id].alive) {
            return;
        }
        SkinnedInstanceRecord& record = g_skinnedInstances[instance.id];
        record.jointMatrices.assign(matrices.begin(), matrices.end());
        record.submittedJointCount = std::min(
            static_cast<uint32_t>(record.jointMatrices.size()),
            MaxSkinnedJointsPerMesh);
        record.truncatedJointCount = record.jointMatrices.size() > MaxSkinnedJointsPerMesh
            ? static_cast<uint32_t>(record.jointMatrices.size() - MaxSkinnedJointsPerMesh)
            : 0;
    }

    SkinnedInstanceDiagnostics skinnedInstanceDiagnostics(SkinnedMeshInstanceHandle instance)
    {
        SkinnedInstanceDiagnostics diagnostics;
        if (instance.id >= g_skinnedInstances.size() || !g_skinnedInstances[instance.id].alive) {
            return diagnostics;
        }
        const SkinnedInstanceRecord& record = g_skinnedInstances[instance.id];
        diagnostics.valid = true;
        diagnostics.mesh = record.mesh;
        diagnostics.visibility = record.visibility;
        diagnostics.renderGroup = record.renderGroup;
        diagnostics.submittedJointCount = record.submittedJointCount;
        diagnostics.truncatedJointCount = record.truncatedJointCount;
        diagnostics.boundsValid = record.mesh.id < g_skinnedMeshes.size() && g_skinnedMeshes[record.mesh.id];
        return diagnostics;
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
