#include "Renderer/Scene.hpp"

#ifndef MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
#define MANUAL_ENGINE_ENABLE_DEBUG_TOOLS 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

namespace {
    struct RenderGroupResource {
        bool alive = false;
        std::string name;
        bool hasChunkCoord = false;
        int32_t chunkX = 0;
        int32_t chunkZ = 0;
    };

    struct LightResource {
        bool alive = false;
        Renderer::LightDescriptor descriptor;
    };

    struct MaterialResource {
        bool alive = false;
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        Renderer::TextureHandle baseColorTexture;
        Renderer::TextureHandle normalTexture;
        Renderer::TextureHandle metallicTexture;
        Renderer::TextureHandle roughnessTexture;
        Renderer::TextureHandle metallicRoughnessTexture;
        Renderer::TextureHandle occlusionTexture;
        float occlusionStrength = 1.0f;
        Renderer::TextureHandle emissiveTexture;
        glm::vec3 emissiveFactor{0.0f};
        Renderer::MaterialDescriptor::TextureChannel packedMetallicChannel = Renderer::MaterialDescriptor::TextureChannel::B;
        Renderer::MaterialDescriptor::TextureChannel packedRoughnessChannel = Renderer::MaterialDescriptor::TextureChannel::G;
        Renderer::MaterialDescriptor::AlphaMode alphaMode = Renderer::MaterialDescriptor::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct SubmeshResource {
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        Renderer::MaterialHandle material;
    };

    struct StaticMeshResource {
        bool alive = false;
        Renderer::Aabb localBounds;
        bool hasBounds = false;
        std::vector<SubmeshResource> submeshes;
    };

    struct MeshInstanceResource {
        bool alive = false;
        Renderer::StaticMeshHandle mesh;
        Renderer::RenderVisibility visibility;
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{1.0f};
        glm::mat4 explicitTransform{1.0f};
        bool usesExplicitTransform = false;
        Renderer::MaterialHandle materialOverride;
        bool hasMaterialOverride = false;
        Renderer::RenderGroupHandle renderGroup;
    };

    struct TerrainTileResource {
        bool alive = false;
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        Renderer::MaterialHandle material;
        Renderer::RenderVisibility visibility{Renderer::RenderLayer::Terrain, Renderer::VisibilityFlags::Visible, 0.0f};
        Renderer::Aabb worldBounds;
        bool hasBounds = false;
        Renderer::RenderGroupHandle renderGroup;
    };

    std::vector<RenderGroupResource> g_renderGroups;
    std::vector<LightResource> g_lights;
    std::vector<MaterialResource> g_materials;
    std::vector<StaticMeshResource> g_meshes;
    std::vector<MeshInstanceResource> g_instances;
    std::vector<TerrainTileResource> g_terrainTiles;
    std::vector<Renderer::PosColorVertex> g_debugLineVertices;

    bgfx::ProgramHandle g_meshProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle g_debugLineProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_baseColorSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_normalSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_metallicSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_roughnessSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_metallicRoughnessSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_occlusionSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_emissiveSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_baseColorFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_emissiveFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_materialParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_textureFlags = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_packedTextureChannels = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_alphaParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_lightDirection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_sunColorIntensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_cameraPosition = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_pbrParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_environmentParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_forwardLightPositionRange = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_forwardLightDirectionType = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_forwardLightColorIntensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_forwardLightSpotParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_fogColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_fogParams = BGFX_INVALID_HANDLE;

    Renderer::TextureHandle g_whiteTexture;
    Renderer::TextureHandle g_flatNormalTexture;
    Renderer::TextureHandle g_blackTexture;
    Renderer::AtmosphereSettings g_atmosphere;
    Renderer::DebugDrawSettings g_debugDrawSettings;
    Renderer::DebugDrawStats g_debugDrawStats;

    bool isValidStaticMesh(Renderer::StaticMeshHandle handle)
    {
        return handle.id < g_meshes.size() && g_meshes[handle.id].alive;
    }

    bool isValidMaterial(Renderer::MaterialHandle handle)
    {
        return handle.id < g_materials.size() && g_materials[handle.id].alive;
    }

    bool isValidMeshInstance(Renderer::MeshInstanceHandle handle)
    {
        return handle.id < g_instances.size() && g_instances[handle.id].alive;
    }

    bool isValidTerrain(Renderer::TerrainHandle handle)
    {
        return handle.id < g_terrainTiles.size() && g_terrainTiles[handle.id].alive;
    }

    bool isValidRenderGroup(Renderer::RenderGroupHandle handle)
    {
        return handle.id < g_renderGroups.size() && g_renderGroups[handle.id].alive;
    }

    bool isValidLight(Renderer::LightHandle handle)
    {
        return handle.id < g_lights.size() && g_lights[handle.id].alive;
    }

    uint32_t layerBits(Renderer::RenderLayer layer)
    {
        return static_cast<uint32_t>(layer);
    }

    uint32_t visibilityBits(Renderer::VisibilityFlags flags)
    {
        return static_cast<uint32_t>(flags);
    }

    bool passesLayerAndFlags(const Renderer::RenderVisibility& visibility, uint32_t layerMask)
    {
        return (visibilityBits(visibility.flags) & visibilityBits(Renderer::VisibilityFlags::Visible)) != 0 &&
            (layerBits(visibility.layer) & layerMask) != 0;
    }

    Renderer::RenderGroupDrawStats* findGroupStats(
        Renderer::SceneDrawStats& stats,
        Renderer::RenderGroupHandle group)
    {
        if (!isValidRenderGroup(group)) {
            return nullptr;
        }

        const auto existing = std::find_if(
            stats.renderGroups.begin(),
            stats.renderGroups.end(),
            [group](const Renderer::RenderGroupDrawStats& entry) {
                return entry.group.id == group.id;
            }
        );
        if (existing != stats.renderGroups.end()) {
            return &(*existing);
        }

        const RenderGroupResource& resource = g_renderGroups[group.id];
        Renderer::RenderGroupDrawStats entry;
        entry.group = group;
        entry.name = resource.name;
        entry.hasChunkCoord = resource.hasChunkCoord;
        entry.chunkX = resource.chunkX;
        entry.chunkZ = resource.chunkZ;
        stats.renderGroups.push_back(std::move(entry));
        return &stats.renderGroups.back();
    }

    bool isFiniteVec3(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool isValidAabb(const Renderer::Aabb& bounds)
    {
        return isFiniteVec3(bounds.min) &&
            isFiniteVec3(bounds.max) &&
            bounds.min.x <= bounds.max.x &&
            bounds.min.y <= bounds.max.y &&
            bounds.min.z <= bounds.max.z;
    }

    Renderer::Aabb emptyAabb()
    {
        const float maxFloat = std::numeric_limits<float>::max();
        return {{maxFloat, maxFloat, maxFloat}, {-maxFloat, -maxFloat, -maxFloat}};
    }

    void includePoint(Renderer::Aabb& bounds, const glm::vec3& point)
    {
        bounds.min = glm::min(bounds.min, point);
        bounds.max = glm::max(bounds.max, point);
    }

    void includeAabb(Renderer::Aabb& bounds, const Renderer::Aabb& other)
    {
        includePoint(bounds, other.min);
        includePoint(bounds, other.max);
    }

    glm::vec3 centerOf(const Renderer::Aabb& bounds)
    {
        return (bounds.min + bounds.max) * 0.5f;
    }

    bool exceedsMaxDrawDistance(
        const Renderer::RenderVisibility& visibility,
        const Renderer::Aabb& worldBounds,
        const glm::vec3& cameraPosition,
        bool enabled)
    {
        if (!enabled || visibility.maxDrawDistance <= 0.0f || !isValidAabb(worldBounds)) {
            return false;
        }

        const float maxDistanceSquared = visibility.maxDrawDistance * visibility.maxDrawDistance;
        const glm::vec3 offset = cameraPosition - centerOf(worldBounds);
        return glm::dot(offset, offset) > maxDistanceSquared;
    }

    Renderer::Aabb transformAabb(const Renderer::Aabb& bounds, const glm::mat4& transform)
    {
        Renderer::Aabb transformed = emptyAabb();
        for (uint32_t corner = 0; corner < 8; ++corner) {
            const glm::vec3 point{
                (corner & 1) ? bounds.max.x : bounds.min.x,
                (corner & 2) ? bounds.max.y : bounds.min.y,
                (corner & 4) ? bounds.max.z : bounds.min.z,
            };
            const glm::vec4 transformedPoint = transform * glm::vec4{point, 1.0f};
            includePoint(transformed, glm::vec3{transformedPoint});
        }
        return transformed;
    }

    glm::vec4 matrixRow(const glm::mat4& matrix, uint32_t row)
    {
        return {matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]};
    }

    glm::vec4 normalizePlane(const glm::vec4& plane)
    {
        const float length = glm::length(glm::vec3{plane});
        if (length <= 0.0f || !std::isfinite(length)) {
            return plane;
        }
        return plane / length;
    }

    Renderer::Frustum makeFrustum(const glm::mat4& viewProjection)
    {
        const glm::vec4 row0 = matrixRow(viewProjection, 0);
        const glm::vec4 row1 = matrixRow(viewProjection, 1);
        const glm::vec4 row2 = matrixRow(viewProjection, 2);
        const glm::vec4 row3 = matrixRow(viewProjection, 3);

        Renderer::Frustum frustum;
        frustum.planes[0] = normalizePlane(row3 + row0);
        frustum.planes[1] = normalizePlane(row3 - row0);
        frustum.planes[2] = normalizePlane(row3 + row1);
        frustum.planes[3] = normalizePlane(row3 - row1);
        frustum.planes[4] = normalizePlane(row3 + row2);
        frustum.planes[5] = normalizePlane(row3 - row2);
        return frustum;
    }

    bool intersects(const Renderer::Frustum& frustum, const Renderer::Aabb& bounds)
    {
        if (!isValidAabb(bounds)) {
            return true;
        }

        for (const glm::vec4& plane : frustum.planes) {
            const glm::vec3 positive{
                plane.x >= 0.0f ? bounds.max.x : bounds.min.x,
                plane.y >= 0.0f ? bounds.max.y : bounds.min.y,
                plane.z >= 0.0f ? bounds.max.z : bounds.min.z,
            };
            if (glm::dot(glm::vec3{plane}, positive) + plane.w < 0.0f) {
                return false;
            }
        }

        return true;
    }

    glm::mat4 composeTransform(const MeshInstanceResource& instance)
    {
        if (instance.usesExplicitTransform) {
            return instance.explicitTransform;
        }

        glm::mat4 transform{1.0f};
        transform = glm::translate(transform, instance.position);
        transform = glm::rotate(transform, instance.rotation.x, {1.0f, 0.0f, 0.0f});
        transform = glm::rotate(transform, instance.rotation.y, {0.0f, 1.0f, 0.0f});
        transform = glm::rotate(transform, instance.rotation.z, {0.0f, 0.0f, 1.0f});
        transform = glm::scale(transform, instance.scale);
        return transform;
    }

    bool isActiveLight(const LightResource& light)
    {
        return light.alive &&
            light.descriptor.enabled &&
            light.descriptor.intensity > 0.0f &&
            isFiniteVec3(light.descriptor.color);
    }

    Renderer::LightHandle addLight(LightResource light)
    {
        light.alive = true;
        for (uint32_t index = 0; index < g_lights.size(); ++index) {
            if (!g_lights[index].alive) {
                g_lights[index] = std::move(light);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_lights.size());
        g_lights.push_back(std::move(light));
        return {id};
    }

    LightResource makeLightResource(Renderer::LightDescriptor descriptor)
    {
        if (!isFiniteVec3(descriptor.direction) || glm::length(descriptor.direction) <= 0.0f) {
            descriptor.direction = {0.0f, -1.0f, 0.0f};
        } else {
            descriptor.direction = glm::normalize(descriptor.direction);
        }
        if (!isFiniteVec3(descriptor.position)) {
            descriptor.position = {0.0f, 0.0f, 0.0f};
        }
        if (!isFiniteVec3(descriptor.color)) {
            descriptor.color = {1.0f, 1.0f, 1.0f};
        }
        descriptor.intensity = std::max(descriptor.intensity, 0.0f);
        descriptor.range = std::max(descriptor.range, 0.0f);
        descriptor.innerConeAngle = std::clamp(descriptor.innerConeAngle, 0.0f, 3.14159265f);
        descriptor.outerConeAngle = std::clamp(descriptor.outerConeAngle, descriptor.innerConeAngle, 3.14159265f);

        LightResource resource;
        resource.descriptor = std::move(descriptor);
        return resource;
    }

    Renderer::MaterialHandle addMaterial(MaterialResource material)
    {
        material.alive = true;
        for (uint32_t index = 0; index < g_materials.size(); ++index) {
            if (!g_materials[index].alive) {
                g_materials[index] = std::move(material);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_materials.size());
        g_materials.push_back(std::move(material));
        return {id};
    }

    MaterialResource makeMaterialResource(const Renderer::MaterialDescriptor& descriptor)
    {
        MaterialResource material;
        material.name = descriptor.name;
        material.baseColorFactor = descriptor.baseColorFactor;
        material.metallicFactor = descriptor.metallicFactor;
        material.roughnessFactor = descriptor.roughnessFactor;
        material.normalScale = descriptor.normalScale;
        material.baseColorTexture = Renderer::isValid(descriptor.baseColorTexture) ? descriptor.baseColorTexture : g_whiteTexture;
        material.normalTexture = Renderer::isValid(descriptor.normalTexture) ? descriptor.normalTexture : g_flatNormalTexture;
        material.metallicTexture = Renderer::isValid(descriptor.metallicTexture) ? descriptor.metallicTexture : g_blackTexture;
        material.roughnessTexture = Renderer::isValid(descriptor.roughnessTexture) ? descriptor.roughnessTexture : g_whiteTexture;
        material.metallicRoughnessTexture = Renderer::isValid(descriptor.metallicRoughnessTexture)
            ? descriptor.metallicRoughnessTexture
            : g_whiteTexture;
        material.occlusionTexture = Renderer::isValid(descriptor.occlusionTexture) ? descriptor.occlusionTexture : g_whiteTexture;
        material.occlusionStrength = descriptor.occlusionStrength;
        material.emissiveTexture = Renderer::isValid(descriptor.emissiveTexture) ? descriptor.emissiveTexture : g_blackTexture;
        material.emissiveFactor = descriptor.emissiveFactor;
        material.packedMetallicChannel = descriptor.metallicRoughnessMetallicChannel;
        material.packedRoughnessChannel = descriptor.metallicRoughnessRoughnessChannel;
        material.alphaMode = descriptor.alphaMode;
        material.alphaCutoff = descriptor.alphaCutoff;
        material.doubleSided = descriptor.doubleSided;
        return material;
    }

    Renderer::StaticMeshHandle addMesh(StaticMeshResource mesh)
    {
        mesh.alive = true;
        for (uint32_t index = 0; index < g_meshes.size(); ++index) {
            if (!g_meshes[index].alive) {
                g_meshes[index] = std::move(mesh);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_meshes.size());
        g_meshes.push_back(std::move(mesh));
        return {id};
    }

    StaticMeshResource makeStaticMeshResource(const Renderer::StaticMeshDescriptor& descriptor)
    {
        StaticMeshResource mesh;
        mesh.submeshes.reserve(descriptor.submeshes.size());
        mesh.localBounds = emptyAabb();

        for (const Renderer::StaticSubmeshDescriptor& sourceSubmesh : descriptor.submeshes) {
            if (sourceSubmesh.vertices.empty() || sourceSubmesh.indices.empty()) {
                continue;
            }

            for (const Renderer::MeshVertex& vertex : sourceSubmesh.vertices) {
                includePoint(mesh.localBounds, {vertex.px, vertex.py, vertex.pz});
                mesh.hasBounds = true;
            }

            SubmeshResource submesh;
            submesh.vertexBuffer = bgfx::createVertexBuffer(
                bgfx::copy(
                    sourceSubmesh.vertices.data(),
                    static_cast<uint32_t>(sourceSubmesh.vertices.size() * sizeof(Renderer::MeshVertex))),
                Renderer::MeshVertex::layout
            );
            submesh.indexBuffer = bgfx::createIndexBuffer(
                bgfx::copy(
                    sourceSubmesh.indices.data(),
                    static_cast<uint32_t>(sourceSubmesh.indices.size() * sizeof(uint32_t))),
                BGFX_BUFFER_INDEX32
            );
            submesh.indexCount = static_cast<uint32_t>(sourceSubmesh.indices.size());
            submesh.material = isValidMaterial(sourceSubmesh.material) ? sourceSubmesh.material : Renderer::MaterialHandle{0};

            if (bgfx::isValid(submesh.vertexBuffer) && bgfx::isValid(submesh.indexBuffer)) {
                mesh.submeshes.push_back(submesh);
            } else {
                if (bgfx::isValid(submesh.vertexBuffer)) {
                    bgfx::destroy(submesh.vertexBuffer);
                }
                if (bgfx::isValid(submesh.indexBuffer)) {
                    bgfx::destroy(submesh.indexBuffer);
                }
            }
        }

        return mesh;
    }

    Renderer::TextureHandle loadMaterialTexture(
        const std::filesystem::path& meshDirectory,
        const std::filesystem::path& texturePath,
        Renderer::TextureHandle fallback)
    {
        if (texturePath.empty()) {
            return fallback;
        }

        const std::filesystem::path resolvedTexturePath =
            texturePath.is_absolute() ? texturePath : meshDirectory / texturePath;
        const Renderer::TextureHandle texture = Renderer::loadTexture(resolvedTexturePath);
        return Renderer::isValid(texture) ? texture : fallback;
    }

    float channelIndex(Renderer::MaterialDescriptor::TextureChannel channel)
    {
        return static_cast<float>(static_cast<uint32_t>(channel));
    }

    Renderer::MaterialRenderPass renderPassFor(Renderer::MaterialDescriptor::AlphaMode alphaMode)
    {
        switch (alphaMode) {
            case Renderer::MaterialDescriptor::AlphaMode::Mask:
                return Renderer::MaterialRenderPass::AlphaMask;
            case Renderer::MaterialDescriptor::AlphaMode::Blend:
                return Renderer::MaterialRenderPass::AlphaBlend;
            case Renderer::MaterialDescriptor::AlphaMode::Opaque:
            default:
                break;
        }
        return Renderer::MaterialRenderPass::Opaque;
    }

    bool isDepthWritingPass(Renderer::MaterialRenderPass pass)
    {
        return pass == Renderer::MaterialRenderPass::Opaque ||
            pass == Renderer::MaterialRenderPass::AlphaMask;
    }

    uint64_t renderStateFor(const MaterialResource& material, Renderer::MaterialRenderPass pass)
    {
        uint64_t state = BGFX_STATE_WRITE_RGB |
            BGFX_STATE_WRITE_A |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_MSAA;

        if (isDepthWritingPass(pass)) {
            state |= BGFX_STATE_WRITE_Z;
        }

        if (pass == Renderer::MaterialRenderPass::AlphaBlend) {
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }

        if (!material.doubleSided) {
            state |= BGFX_STATE_CULL_CW;
        }

        return state;
    }

    float alphaModeValue(Renderer::MaterialRenderPass pass)
    {
        return pass == Renderer::MaterialRenderPass::AlphaMask ? 1.0f : 0.0f;
    }

    float lightTypeValue(Renderer::LightType type)
    {
        switch (type) {
            case Renderer::LightType::Directional:
                return 0.0f;
            case Renderer::LightType::Point:
                return 1.0f;
            case Renderer::LightType::Spot:
                return 2.0f;
            default:
                break;
        }
        return 1.0f;
    }

    struct SceneUniforms {
        glm::vec4 lightDirection{};
        glm::vec4 sunColorIntensity{};
        glm::vec4 cameraPosition{};
        glm::vec4 pbrParams{};
        glm::vec4 environmentParams{};
        std::array<glm::vec4, Renderer::MaxForwardLights> forwardLightPositionRange{};
        std::array<glm::vec4, Renderer::MaxForwardLights> forwardLightDirectionType{};
        std::array<glm::vec4, Renderer::MaxForwardLights> forwardLightColorIntensity{};
        std::array<glm::vec4, Renderer::MaxForwardLights> forwardLightSpotParams{};
        glm::vec4 fogParams{};
    };

    struct VisibleMeshDrawItem {
        Renderer::MeshInstanceHandle instance;
        Renderer::StaticMeshHandle mesh;
        uint32_t submeshIndex = 0;
        Renderer::MaterialHandle material;
        Renderer::RenderGroupHandle renderGroup;
        glm::mat4 transform{1.0f};
        Renderer::Aabb worldBounds;
        Renderer::MaterialRenderPass pass = Renderer::MaterialRenderPass::Opaque;
        float cameraDistanceSquared = 0.0f;
    };

    void setMaterialAndSceneUniforms(
        const MaterialResource& material,
        const SceneUniforms& uniforms,
        Renderer::MaterialRenderPass pass)
    {
        const Renderer::TextureHandle baseColorTexture = Renderer::isValid(material.baseColorTexture) ? material.baseColorTexture : g_whiteTexture;
        const Renderer::TextureHandle normalTexture = Renderer::isValid(material.normalTexture) ? material.normalTexture : g_flatNormalTexture;
        const Renderer::TextureHandle metallicTexture = Renderer::isValid(material.metallicTexture) ? material.metallicTexture : g_blackTexture;
        const Renderer::TextureHandle roughnessTexture = Renderer::isValid(material.roughnessTexture) ? material.roughnessTexture : g_whiteTexture;
        const Renderer::TextureHandle metallicRoughnessTexture = Renderer::isValid(material.metallicRoughnessTexture)
            ? material.metallicRoughnessTexture
            : g_whiteTexture;
        const Renderer::TextureHandle occlusionTexture = Renderer::isValid(material.occlusionTexture) ? material.occlusionTexture : g_whiteTexture;
        const Renderer::TextureHandle emissiveTexture = Renderer::isValid(material.emissiveTexture) ? material.emissiveTexture : g_blackTexture;
        const glm::vec4 materialParams{
            material.metallicFactor,
            material.roughnessFactor,
            metallicTexture.id != g_blackTexture.id ? 1.0f : 0.0f,
            roughnessTexture.id != g_whiteTexture.id ? 1.0f : 0.0f,
        };
        const glm::vec4 textureFlags{
            normalTexture.id != g_flatNormalTexture.id ? 1.0f : 0.0f,
            metallicRoughnessTexture.id != g_whiteTexture.id ? 1.0f : 0.0f,
            occlusionTexture.id != g_whiteTexture.id ? 1.0f : 0.0f,
            emissiveTexture.id != g_blackTexture.id ? 1.0f : 0.0f,
        };
        const glm::vec4 packedTextureChannels{
            channelIndex(material.packedRoughnessChannel),
            channelIndex(material.packedMetallicChannel),
            material.normalScale,
            material.occlusionStrength,
        };
        const glm::vec4 emissiveFactor{material.emissiveFactor, 1.0f};
        const glm::vec4 alphaParams{
            alphaModeValue(pass),
            material.alphaCutoff,
            0.0f,
            0.0f,
        };

        bgfx::setUniform(g_baseColorFactor, &material.baseColorFactor[0]);
        bgfx::setUniform(g_emissiveFactor, &emissiveFactor[0]);
        bgfx::setUniform(g_materialParams, &materialParams[0]);
        bgfx::setUniform(g_textureFlags, &textureFlags[0]);
        bgfx::setUniform(g_packedTextureChannels, &packedTextureChannels[0]);
        bgfx::setUniform(g_alphaParams, &alphaParams[0]);
        bgfx::setUniform(g_lightDirection, &uniforms.lightDirection[0]);
        bgfx::setUniform(g_sunColorIntensity, &uniforms.sunColorIntensity[0]);
        bgfx::setUniform(g_cameraPosition, &uniforms.cameraPosition[0]);
        bgfx::setUniform(g_pbrParams, &uniforms.pbrParams[0]);
        bgfx::setUniform(g_environmentParams, &uniforms.environmentParams[0]);
        bgfx::setUniform(
            g_forwardLightPositionRange,
            uniforms.forwardLightPositionRange.data(),
            Renderer::MaxForwardLights);
        bgfx::setUniform(
            g_forwardLightDirectionType,
            uniforms.forwardLightDirectionType.data(),
            Renderer::MaxForwardLights);
        bgfx::setUniform(
            g_forwardLightColorIntensity,
            uniforms.forwardLightColorIntensity.data(),
            Renderer::MaxForwardLights);
        bgfx::setUniform(
            g_forwardLightSpotParams,
            uniforms.forwardLightSpotParams.data(),
            Renderer::MaxForwardLights);
        bgfx::setUniform(g_fogColor, &g_atmosphere.fogColor[0]);
        bgfx::setUniform(g_fogParams, &uniforms.fogParams[0]);
        bgfx::setTexture(0, g_baseColorSampler, getNativeTexture(baseColorTexture));
        bgfx::setTexture(1, g_normalSampler, getNativeTexture(normalTexture));
        bgfx::setTexture(2, g_metallicSampler, getNativeTexture(metallicTexture));
        bgfx::setTexture(3, g_roughnessSampler, getNativeTexture(roughnessTexture));
        bgfx::setTexture(4, g_metallicRoughnessSampler, getNativeTexture(metallicRoughnessTexture));
        bgfx::setTexture(5, g_occlusionSampler, getNativeTexture(occlusionTexture));
        bgfx::setTexture(6, g_emissiveSampler, getNativeTexture(emissiveTexture));
    }

    bool sameBatch(const VisibleMeshDrawItem& lhs, const VisibleMeshDrawItem& rhs)
    {
        return lhs.mesh.id == rhs.mesh.id &&
            lhs.submeshIndex == rhs.submeshIndex &&
            lhs.material.id == rhs.material.id;
    }

    void sortDepthWritingDrawItems(std::vector<VisibleMeshDrawItem>& drawItems)
    {
        std::sort(
            drawItems.begin(),
            drawItems.end(),
            [](const VisibleMeshDrawItem& lhs, const VisibleMeshDrawItem& rhs) {
                if (lhs.mesh.id != rhs.mesh.id) {
                    return lhs.mesh.id < rhs.mesh.id;
                }
                if (lhs.submeshIndex != rhs.submeshIndex) {
                    return lhs.submeshIndex < rhs.submeshIndex;
                }
                if (lhs.material.id != rhs.material.id) {
                    return lhs.material.id < rhs.material.id;
                }
                return lhs.instance.id < rhs.instance.id;
            }
        );
    }

    void sortAlphaBlendDrawItems(std::vector<VisibleMeshDrawItem>& drawItems)
    {
        std::sort(
            drawItems.begin(),
            drawItems.end(),
            [](const VisibleMeshDrawItem& lhs, const VisibleMeshDrawItem& rhs) {
                if (lhs.cameraDistanceSquared != rhs.cameraDistanceSquared) {
                    return lhs.cameraDistanceSquared > rhs.cameraDistanceSquared;
                }
                return lhs.instance.id < rhs.instance.id;
            }
        );
    }

    void addVisiblePassStats(Renderer::SceneDrawStats& stats, Renderer::MaterialRenderPass pass, uint32_t count)
    {
        switch (pass) {
            case Renderer::MaterialRenderPass::AlphaMask:
                stats.visibleAlphaMaskMeshDrawItems += count;
                break;
            case Renderer::MaterialRenderPass::AlphaBlend:
                stats.visibleAlphaBlendMeshDrawItems += count;
                break;
            case Renderer::MaterialRenderPass::Opaque:
            default:
                stats.visibleOpaqueMeshDrawItems += count;
                break;
        }
    }

    void addSubmittedPassStats(Renderer::SceneDrawStats& stats, Renderer::MaterialRenderPass pass)
    {
        switch (pass) {
            case Renderer::MaterialRenderPass::AlphaMask:
                ++stats.submittedAlphaMaskMeshDrawItems;
                break;
            case Renderer::MaterialRenderPass::AlphaBlend:
                ++stats.submittedAlphaBlendMeshDrawItems;
                break;
            case Renderer::MaterialRenderPass::Opaque:
            default:
                ++stats.submittedOpaqueMeshDrawItems;
                break;
        }
    }

    void addBatchPassStats(Renderer::SceneDrawStats& stats, Renderer::MaterialRenderPass pass)
    {
        ++stats.meshBatchCount;
        switch (pass) {
            case Renderer::MaterialRenderPass::AlphaMask:
                ++stats.alphaMaskMeshBatchCount;
                break;
            case Renderer::MaterialRenderPass::AlphaBlend:
                ++stats.alphaBlendMeshBatchCount;
                break;
            case Renderer::MaterialRenderPass::Opaque:
            default:
                ++stats.opaqueMeshBatchCount;
                break;
        }
    }

    void submitMeshDrawItems(
        const std::vector<VisibleMeshDrawItem>& drawItems,
        const SceneUniforms& sceneUniforms,
        bgfx::ViewId viewId,
        Renderer::SceneDrawStats& stats)
    {
        uint32_t currentBatchSize = 0;
        VisibleMeshDrawItem previousItem;
        bool hasPreviousItem = false;

        for (const VisibleMeshDrawItem& item : drawItems) {
            if (!isValidStaticMesh(item.mesh) || item.submeshIndex >= g_meshes[item.mesh.id].submeshes.size() || !isValidMaterial(item.material)) {
                continue;
            }

            if (!hasPreviousItem || !sameBatch(previousItem, item)) {
                if (currentBatchSize > stats.largestMeshBatchSize) {
                    stats.largestMeshBatchSize = currentBatchSize;
                }
                currentBatchSize = 0;
                addBatchPassStats(stats, item.pass);
                hasPreviousItem = true;
            }
            previousItem = item;
            ++currentBatchSize;

            const SubmeshResource& submesh = g_meshes[item.mesh.id].submeshes[item.submeshIndex];
            if (!bgfx::isValid(submesh.vertexBuffer) || !bgfx::isValid(submesh.indexBuffer)) {
                continue;
            }

            const MaterialResource& material = g_materials[item.material.id];
            bgfx::setTransform(&item.transform[0][0]);
            setMaterialAndSceneUniforms(material, sceneUniforms, item.pass);
            bgfx::setVertexBuffer(0, submesh.vertexBuffer);
            bgfx::setIndexBuffer(submesh.indexBuffer, 0, submesh.indexCount);
            bgfx::setState(renderStateFor(material, item.pass));
            bgfx::submit(viewId, g_meshProgram);
            ++stats.submittedMeshDrawItems;
            addSubmittedPassStats(stats, item.pass);
        }

        if (currentBatchSize > stats.largestMeshBatchSize) {
            stats.largestMeshBatchSize = currentBatchSize;
        }
    }

    void pushDebugVertex(const glm::vec3& position, uint32_t abgr)
    {
#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
        g_debugLineVertices.push_back({
            position.x,
            position.y,
            position.z,
            abgr,
        });
#else
        (void)position;
        (void)abgr;
#endif
    }
}

namespace Renderer {
    bool initSceneRenderer()
    {
        g_whiteTexture = createSolidTexture(255, 255, 255, 255);
        g_flatNormalTexture = createSolidTexture(128, 128, 255, 255);
        g_blackTexture = createSolidTexture(0, 0, 0, 255);

        bgfx::ShaderHandle vsh = loadShader("vs_mesh.bin");
        bgfx::ShaderHandle fsh = loadShader("fs_mesh.bin");
        if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
            return false;
        }

        g_meshProgram = bgfx::createProgram(vsh, fsh, true);
        if (!bgfx::isValid(g_meshProgram)) {
            return false;
        }

#if MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
        bgfx::ShaderHandle debugVsh = loadShader("vs_debug_line.bin");
        bgfx::ShaderHandle debugFsh = loadShader("fs_debug_line.bin");
        if (!bgfx::isValid(debugVsh) || !bgfx::isValid(debugFsh)) {
            if (bgfx::isValid(debugVsh)) {
                bgfx::destroy(debugVsh);
            }
            if (bgfx::isValid(debugFsh)) {
                bgfx::destroy(debugFsh);
            }
            return false;
        }
        g_debugLineProgram = bgfx::createProgram(debugVsh, debugFsh, true);
        if (!bgfx::isValid(g_debugLineProgram)) {
            return false;
        }
#endif

        g_baseColorSampler = bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler);
        g_normalSampler = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);
        g_metallicSampler = bgfx::createUniform("s_metallicMap", bgfx::UniformType::Sampler);
        g_roughnessSampler = bgfx::createUniform("s_roughnessMap", bgfx::UniformType::Sampler);
        g_metallicRoughnessSampler = bgfx::createUniform("s_metallicRoughnessMap", bgfx::UniformType::Sampler);
        g_occlusionSampler = bgfx::createUniform("s_occlusionMap", bgfx::UniformType::Sampler);
        g_emissiveSampler = bgfx::createUniform("s_emissiveMap", bgfx::UniformType::Sampler);
        g_baseColorFactor = bgfx::createUniform("u_baseColorFactor", bgfx::UniformType::Vec4);
        g_emissiveFactor = bgfx::createUniform("u_emissiveFactor", bgfx::UniformType::Vec4);
        g_materialParams = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
        g_textureFlags = bgfx::createUniform("u_textureFlags", bgfx::UniformType::Vec4);
        g_packedTextureChannels = bgfx::createUniform("u_packedTextureChannels", bgfx::UniformType::Vec4);
        g_alphaParams = bgfx::createUniform("u_alphaParams", bgfx::UniformType::Vec4);
        g_lightDirection = bgfx::createUniform("u_lightDirection", bgfx::UniformType::Vec4);
        g_sunColorIntensity = bgfx::createUniform("u_sunColorIntensity", bgfx::UniformType::Vec4);
        g_cameraPosition = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
        g_pbrParams = bgfx::createUniform("u_pbrParams", bgfx::UniformType::Vec4);
        g_environmentParams = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
        g_forwardLightPositionRange = bgfx::createUniform(
            "u_forwardLightPositionRange",
            bgfx::UniformType::Vec4,
            MaxForwardLights);
        g_forwardLightDirectionType = bgfx::createUniform(
            "u_forwardLightDirectionType",
            bgfx::UniformType::Vec4,
            MaxForwardLights);
        g_forwardLightColorIntensity = bgfx::createUniform(
            "u_forwardLightColorIntensity",
            bgfx::UniformType::Vec4,
            MaxForwardLights);
        g_forwardLightSpotParams = bgfx::createUniform(
            "u_forwardLightSpotParams",
            bgfx::UniformType::Vec4,
            MaxForwardLights);
        g_fogColor = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
        g_fogParams = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);

        MaterialResource defaultMaterial;
        defaultMaterial.name = "default";
        defaultMaterial.baseColorTexture = g_whiteTexture;
        defaultMaterial.normalTexture = g_flatNormalTexture;
        defaultMaterial.metallicTexture = g_blackTexture;
        defaultMaterial.roughnessTexture = g_whiteTexture;
        defaultMaterial.metallicRoughnessTexture = g_whiteTexture;
        defaultMaterial.occlusionTexture = g_whiteTexture;
        defaultMaterial.emissiveTexture = g_blackTexture;
        addMaterial(defaultMaterial);

        return true;
    }

    MaterialHandle createMaterial(const MaterialDescriptor& descriptor)
    {
        return addMaterial(makeMaterialResource(descriptor));
    }

    void destroyMaterial(MaterialHandle material)
    {
        if (!isValidMaterial(material) || material.id == 0) {
            return;
        }

        for (MeshInstanceResource& instance : g_instances) {
            if (instance.alive && instance.hasMaterialOverride && instance.materialOverride.id == material.id) {
                instance.materialOverride = {};
                instance.hasMaterialOverride = false;
            }
        }

        for (TerrainTileResource& terrain : g_terrainTiles) {
            if (terrain.alive && terrain.material.id == material.id) {
                terrain.material = MaterialHandle{0};
            }
        }

        for (StaticMeshResource& mesh : g_meshes) {
            if (!mesh.alive) {
                continue;
            }
            for (SubmeshResource& submesh : mesh.submeshes) {
                if (submesh.material.id == material.id) {
                    submesh.material = MaterialHandle{0};
                }
            }
        }

        g_materials[material.id] = {};
    }

    void setMaterialDescriptor(MaterialHandle material, const MaterialDescriptor& descriptor)
    {
        if (!isValidMaterial(material)) {
            return;
        }

        MaterialResource resource = makeMaterialResource(descriptor);
        resource.alive = true;
        g_materials[material.id] = std::move(resource);
    }

    MaterialDiagnostics materialDiagnostics(MaterialHandle material)
    {
        MaterialDiagnostics diagnostics;
        if (!isValidMaterial(material)) {
            return diagnostics;
        }

        const MaterialResource& resource = g_materials[material.id];
        diagnostics.valid = true;
        diagnostics.name = resource.name;
        diagnostics.hasBaseColorTexture = Renderer::isValid(resource.baseColorTexture) && resource.baseColorTexture.id != g_whiteTexture.id;
        diagnostics.hasNormalTexture = Renderer::isValid(resource.normalTexture) && resource.normalTexture.id != g_flatNormalTexture.id;
        diagnostics.hasMetallicTexture = Renderer::isValid(resource.metallicTexture) && resource.metallicTexture.id != g_blackTexture.id;
        diagnostics.hasRoughnessTexture = Renderer::isValid(resource.roughnessTexture) && resource.roughnessTexture.id != g_whiteTexture.id;
        diagnostics.hasPackedMetallicRoughnessTexture =
            Renderer::isValid(resource.metallicRoughnessTexture) && resource.metallicRoughnessTexture.id != g_whiteTexture.id;
        diagnostics.hasOcclusionTexture = Renderer::isValid(resource.occlusionTexture) && resource.occlusionTexture.id != g_whiteTexture.id;
        diagnostics.hasEmissiveTexture = Renderer::isValid(resource.emissiveTexture) && resource.emissiveTexture.id != g_blackTexture.id;
        diagnostics.packedMetallicChannel = resource.packedMetallicChannel;
        diagnostics.packedRoughnessChannel = resource.packedRoughnessChannel;
        diagnostics.alphaMode = resource.alphaMode;
        diagnostics.alphaCutoff = resource.alphaCutoff;
        diagnostics.doubleSided = resource.doubleSided;
        diagnostics.renderPass = renderPassFor(resource.alphaMode);
        return diagnostics;
    }

    void setAtmosphereSettings(const AtmosphereSettings& settings)
    {
        g_atmosphere = settings;
        if (glm::length(g_atmosphere.sunDirection) <= 0.0f) {
            g_atmosphere.sunDirection = {-0.35f, -0.85f, -0.25f};
        }
        g_atmosphere.fogDensity = std::max(g_atmosphere.fogDensity, 0.0f);
        g_atmosphere.sunIntensity = std::max(g_atmosphere.sunIntensity, 0.0f);
        g_atmosphere.exposure = std::max(g_atmosphere.exposure, 0.0f);
        g_atmosphere.ambientIntensity = std::max(g_atmosphere.ambientIntensity, 0.0f);
        if (!isFiniteVec3(g_atmosphere.environmentDiffuseColor)) {
            g_atmosphere.environmentDiffuseColor = {1.0f, 1.0f, 1.0f};
        }
        g_atmosphere.environmentDiffuseColor = glm::max(g_atmosphere.environmentDiffuseColor, glm::vec3{0.0f});
        g_atmosphere.environmentDiffuseIntensity = std::max(g_atmosphere.environmentDiffuseIntensity, 0.0f);
    }

    const AtmosphereSettings& atmosphereSettings()
    {
        return g_atmosphere;
    }

    RenderGroupHandle createRenderGroup(const RenderGroupDescriptor& descriptor)
    {
        RenderGroupResource group;
        group.alive = true;
        group.name = descriptor.name;
        group.hasChunkCoord = descriptor.hasChunkCoord;
        group.chunkX = descriptor.chunkX;
        group.chunkZ = descriptor.chunkZ;

        for (uint32_t index = 0; index < g_renderGroups.size(); ++index) {
            if (!g_renderGroups[index].alive) {
                g_renderGroups[index] = std::move(group);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_renderGroups.size());
        g_renderGroups.push_back(std::move(group));
        return {id};
    }

    void destroyRenderGroup(RenderGroupHandle group)
    {
        if (!isValidRenderGroup(group)) {
            return;
        }

        for (MeshInstanceResource& instance : g_instances) {
            if (instance.alive && instance.renderGroup.id == group.id) {
                instance.renderGroup = {};
            }
        }

        for (TerrainTileResource& terrain : g_terrainTiles) {
            if (terrain.alive && terrain.renderGroup.id == group.id) {
                terrain.renderGroup = {};
            }
        }

        g_renderGroups[group.id] = {};
    }

    LightHandle createLight(const LightDescriptor& descriptor)
    {
        return addLight(makeLightResource(descriptor));
    }

    void destroyLight(LightHandle light)
    {
        if (!isValidLight(light)) {
            return;
        }

        g_lights[light.id] = {};
    }

    void setLightDescriptor(LightHandle light, const LightDescriptor& descriptor)
    {
        if (!isValidLight(light)) {
            return;
        }

        LightResource resource = makeLightResource(descriptor);
        resource.alive = true;
        g_lights[light.id] = std::move(resource);
    }

    LightDiagnostics lightDiagnostics(LightHandle light)
    {
        LightDiagnostics diagnostics;
        if (!isValidLight(light)) {
            return diagnostics;
        }

        diagnostics.valid = true;
        diagnostics.descriptor = g_lights[light.id].descriptor;
        diagnostics.active = isActiveLight(g_lights[light.id]);

        uint32_t activeIndex = 0;
        for (uint32_t index = 0; index < g_lights.size(); ++index) {
            if (!isActiveLight(g_lights[index])) {
                continue;
            }
            if (index == light.id) {
                diagnostics.inForwardBudget = activeIndex < MaxForwardLights;
                break;
            }
            ++activeIndex;
        }

        return diagnostics;
    }

    void shutdownSceneRenderer()
    {
        for (StaticMeshResource& mesh : g_meshes) {
            if (!mesh.alive) {
                continue;
            }

            for (SubmeshResource& submesh : mesh.submeshes) {
                if (bgfx::isValid(submesh.vertexBuffer)) {
                    bgfx::destroy(submesh.vertexBuffer);
                    submesh.vertexBuffer = BGFX_INVALID_HANDLE;
                }
                if (bgfx::isValid(submesh.indexBuffer)) {
                    bgfx::destroy(submesh.indexBuffer);
                    submesh.indexBuffer = BGFX_INVALID_HANDLE;
                }
            }
        }
        g_meshes.clear();
        g_materials.clear();
        g_instances.clear();
        g_lights.clear();
        g_renderGroups.clear();

        for (TerrainTileResource& terrain : g_terrainTiles) {
            if (bgfx::isValid(terrain.vertexBuffer)) {
                bgfx::destroy(terrain.vertexBuffer);
                terrain.vertexBuffer = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(terrain.indexBuffer)) {
                bgfx::destroy(terrain.indexBuffer);
                terrain.indexBuffer = BGFX_INVALID_HANDLE;
            }
        }
        g_terrainTiles.clear();
        g_debugLineVertices.clear();

        const std::array uniforms = {
            g_baseColorSampler,
            g_normalSampler,
            g_metallicSampler,
            g_roughnessSampler,
            g_metallicRoughnessSampler,
            g_occlusionSampler,
            g_emissiveSampler,
            g_baseColorFactor,
            g_emissiveFactor,
            g_materialParams,
            g_textureFlags,
            g_packedTextureChannels,
            g_alphaParams,
            g_lightDirection,
            g_sunColorIntensity,
            g_cameraPosition,
            g_pbrParams,
            g_environmentParams,
            g_forwardLightPositionRange,
            g_forwardLightDirectionType,
            g_forwardLightColorIntensity,
            g_forwardLightSpotParams,
            g_fogColor,
            g_fogParams,
        };
        for (bgfx::UniformHandle uniform : uniforms) {
            if (bgfx::isValid(uniform)) {
                bgfx::destroy(uniform);
            }
        }

        if (bgfx::isValid(g_meshProgram)) {
            bgfx::destroy(g_meshProgram);
            g_meshProgram = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_debugLineProgram)) {
            bgfx::destroy(g_debugLineProgram);
            g_debugLineProgram = BGFX_INVALID_HANDLE;
        }

        destroyTextures();
    }

    void setDebugDrawSettings(const DebugDrawSettings& settings)
    {
        g_debugDrawSettings = settings;
    }

    const DebugDrawSettings& debugDrawSettings()
    {
        return g_debugDrawSettings;
    }

    StaticMeshHandle loadStaticMesh(const std::filesystem::path& path)
    {
        const Assets::Assimp::ImportResult importResult = Assets::Assimp::importStaticMesh(path);
        if (!importResult.success) {
            SDL_Log("Failed to import static mesh %s: %s", path.string().c_str(), importResult.error.c_str());
            return {};
        }

        const std::filesystem::path meshDirectory = path.parent_path();
        std::vector<MaterialHandle> materialHandles;
        materialHandles.reserve(importResult.mesh.materials.size());

        for (const Assets::Assimp::ImportedMaterial& importedMaterial : importResult.mesh.materials) {
            MaterialResource material;
            material.baseColorFactor = importedMaterial.baseColorFactor;
            material.metallicFactor = importedMaterial.metallicFactor;
            material.roughnessFactor = importedMaterial.roughnessFactor;
            material.baseColorTexture = loadMaterialTexture(meshDirectory, importedMaterial.baseColorTexture, g_whiteTexture);
            material.normalTexture = loadMaterialTexture(meshDirectory, importedMaterial.normalTexture, g_flatNormalTexture);
            material.metallicTexture = loadMaterialTexture(meshDirectory, importedMaterial.metallicTexture, g_blackTexture);
            material.roughnessTexture = loadMaterialTexture(meshDirectory, importedMaterial.roughnessTexture, g_whiteTexture);
            materialHandles.push_back(addMaterial(material));
        }

        StaticMeshDescriptor descriptor;
        descriptor.name = path.string();
        descriptor.submeshes.reserve(importResult.mesh.submeshes.size());
        for (const Assets::Assimp::ImportedSubmesh& importedSubmesh : importResult.mesh.submeshes) {
            StaticSubmeshDescriptor submesh;
            submesh.vertices.reserve(importedSubmesh.vertices.size());
            submesh.indices = importedSubmesh.indices;
            submesh.material = importedSubmesh.materialIndex < materialHandles.size()
                ? materialHandles[importedSubmesh.materialIndex]
                : MaterialHandle{0};
            for (const Assets::Assimp::ImportedVertex& importedVertex : importedSubmesh.vertices) {
                submesh.vertices.push_back({
                    importedVertex.position.x,
                    importedVertex.position.y,
                    importedVertex.position.z,
                    importedVertex.normal.x,
                    importedVertex.normal.y,
                    importedVertex.normal.z,
                    importedVertex.tangent.x,
                    importedVertex.tangent.y,
                    importedVertex.tangent.z,
                    1.0f,
                    importedVertex.uv.x,
                    importedVertex.uv.y,
                    importedVertex.uv.x,
                    importedVertex.uv.y,
                    0xffffffff,
                });
            }

            descriptor.submeshes.push_back(std::move(submesh));
        }

        return createStaticMesh(descriptor);
    }

    StaticMeshHandle createStaticMesh(const StaticMeshDescriptor& descriptor)
    {
        StaticMeshResource mesh = makeStaticMeshResource(descriptor);
        if (mesh.submeshes.empty()) {
            return {};
        }

        return addMesh(std::move(mesh));
    }

    StaticMeshHandle createTexturedCubeMesh()
    {
        const MeshVertex vertices[] = {
            {-1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0xffffffff},
            { 1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0xffffffff},
            {-1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0xffffffff},
            { 1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0xffffffff},
            {-1.0f,  1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0xffffffff},
            { 1.0f,  1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0xffffffff},
            {-1.0f, -1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0xffffffff},
            { 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0xffffffff},
        };

        const uint32_t indices[] = {
            0, 2, 1, 1, 2, 3,
            4, 5, 6, 5, 7, 6,
            4, 6, 0, 0, 6, 2,
            1, 3, 5, 5, 3, 7,
            4, 0, 5, 5, 0, 1,
            2, 6, 3, 3, 6, 7,
        };

        SubmeshResource submesh;
        submesh.vertexBuffer = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), MeshVertex::layout);
        submesh.indexBuffer = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)), BGFX_BUFFER_INDEX32);
        submesh.indexCount = static_cast<uint32_t>(std::size(indices));
        submesh.material = MaterialHandle{0};

        StaticMeshResource mesh;
        mesh.localBounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        mesh.hasBounds = true;
        mesh.submeshes.push_back(submesh);
        return addMesh(std::move(mesh));
    }

    void destroyStaticMesh(StaticMeshHandle mesh)
    {
        if (!isValidStaticMesh(mesh)) {
            return;
        }

        for (MeshInstanceResource& instance : g_instances) {
            if (instance.alive && instance.mesh.id == mesh.id) {
                instance = {};
            }
        }

        StaticMeshResource& resource = g_meshes[mesh.id];
        for (SubmeshResource& submesh : resource.submeshes) {
            if (bgfx::isValid(submesh.vertexBuffer)) {
                bgfx::destroy(submesh.vertexBuffer);
            }
            if (bgfx::isValid(submesh.indexBuffer)) {
                bgfx::destroy(submesh.indexBuffer);
            }
        }
        resource = {};
    }

    MeshInstanceHandle createInstance(StaticMeshHandle mesh)
    {
        if (!isValidStaticMesh(mesh)) {
            return {};
        }

        for (uint32_t index = 0; index < g_instances.size(); ++index) {
            if (!g_instances[index].alive) {
                g_instances[index] = {};
                g_instances[index].alive = true;
                g_instances[index].mesh = mesh;
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_instances.size());
        MeshInstanceResource instance;
        instance.alive = true;
        instance.mesh = mesh;
        g_instances.push_back(instance);
        return {id};
    }

    void destroyInstance(MeshInstanceHandle instance)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }

        g_instances[instance.id] = {};
    }

    TerrainHandle createTerrainTile(
        const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices,
        MaterialHandle material)
    {
        if (vertices.empty() || indices.empty()) {
            return {};
        }

        TerrainTileResource terrain;
        terrain.alive = true;
        terrain.worldBounds = emptyAabb();
        for (const MeshVertex& vertex : vertices) {
            includePoint(terrain.worldBounds, {vertex.px, vertex.py, vertex.pz});
            terrain.hasBounds = true;
        }
        terrain.vertexBuffer = bgfx::createVertexBuffer(
            bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex))),
            MeshVertex::layout
        );
        terrain.indexBuffer = bgfx::createIndexBuffer(
            bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t))),
            BGFX_BUFFER_INDEX32
        );
        terrain.indexCount = static_cast<uint32_t>(indices.size());
        terrain.material = isValidMaterial(material) ? material : MaterialHandle{0};

        for (uint32_t index = 0; index < g_terrainTiles.size(); ++index) {
            if (!g_terrainTiles[index].alive) {
                g_terrainTiles[index] = terrain;
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_terrainTiles.size());
        g_terrainTiles.push_back(terrain);
        return {id};
    }

    void destroyTerrainTile(TerrainHandle terrain)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }

        TerrainTileResource& resource = g_terrainTiles[terrain.id];
        if (bgfx::isValid(resource.vertexBuffer)) {
            bgfx::destroy(resource.vertexBuffer);
        }
        if (bgfx::isValid(resource.indexBuffer)) {
            bgfx::destroy(resource.indexBuffer);
        }
        resource = {};
    }

    void setInstancePosition(MeshInstanceHandle instance, const glm::vec3& position)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].position = position;
        g_instances[instance.id].usesExplicitTransform = false;
    }

    void setInstanceRotation(MeshInstanceHandle instance, const glm::vec3& eulerRadians)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].rotation = eulerRadians;
        g_instances[instance.id].usesExplicitTransform = false;
    }

    void setInstanceScale(MeshInstanceHandle instance, const glm::vec3& scale)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].scale = scale;
        g_instances[instance.id].usesExplicitTransform = false;
    }

    void setInstanceTransform(MeshInstanceHandle instance, const glm::mat4& transform)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].explicitTransform = transform;
        g_instances[instance.id].usesExplicitTransform = true;
    }

    void setInstanceMaterial(MeshInstanceHandle instance, MaterialHandle material)
    {
        if (!isValidMeshInstance(instance) || !isValidMaterial(material)) {
            return;
        }
        g_instances[instance.id].materialOverride = material;
        g_instances[instance.id].hasMaterialOverride = true;
    }

    void clearInstanceMaterial(MeshInstanceHandle instance)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].materialOverride = {};
        g_instances[instance.id].hasMaterialOverride = false;
    }

    void setTerrainMaterial(TerrainHandle terrain, MaterialHandle material)
    {
        if (!isValidTerrain(terrain) || !isValidMaterial(material)) {
            return;
        }
        g_terrainTiles[terrain.id].material = material;
    }

    void setInstanceRenderLayer(MeshInstanceHandle instance, RenderLayer layer)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].visibility.layer = layer;
    }

    void setInstanceVisibilityFlags(MeshInstanceHandle instance, VisibilityFlags flags)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].visibility.flags = flags;
    }

    void setInstanceMaxDrawDistance(MeshInstanceHandle instance, float maxDrawDistance)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].visibility.maxDrawDistance = maxDrawDistance;
    }

    void setInstanceRenderGroup(MeshInstanceHandle instance, RenderGroupHandle group)
    {
        if (!isValidMeshInstance(instance) || !isValidRenderGroup(group)) {
            return;
        }
        g_instances[instance.id].renderGroup = group;
    }

    void clearInstanceRenderGroup(MeshInstanceHandle instance)
    {
        if (!isValidMeshInstance(instance)) {
            return;
        }
        g_instances[instance.id].renderGroup = {};
    }

    void setTerrainRenderLayer(TerrainHandle terrain, RenderLayer layer)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }
        g_terrainTiles[terrain.id].visibility.layer = layer;
    }

    void setTerrainVisibilityFlags(TerrainHandle terrain, VisibilityFlags flags)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }
        g_terrainTiles[terrain.id].visibility.flags = flags;
    }

    void setTerrainMaxDrawDistance(TerrainHandle terrain, float maxDrawDistance)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }
        g_terrainTiles[terrain.id].visibility.maxDrawDistance = maxDrawDistance;
    }

    void setTerrainRenderGroup(TerrainHandle terrain, RenderGroupHandle group)
    {
        if (!isValidTerrain(terrain) || !isValidRenderGroup(group)) {
            return;
        }
        g_terrainTiles[terrain.id].renderGroup = group;
    }

    void clearTerrainRenderGroup(TerrainHandle terrain)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }
        g_terrainTiles[terrain.id].renderGroup = {};
    }

    SceneDrawStats drawScene(const RenderView& view)
    {
        SceneDrawStats stats;
        if (!bgfx::isValid(g_meshProgram)) {
            return stats;
        }

        const Frustum frustum = makeFrustum(view.viewProjection);
        const glm::vec3 sunDirection = glm::normalize(g_atmosphere.sunDirection);
        SceneUniforms sceneUniforms;
        sceneUniforms.lightDirection = glm::vec4{sunDirection, 0.0f};
        sceneUniforms.sunColorIntensity = glm::vec4{
            g_atmosphere.sunColor.r,
            g_atmosphere.sunColor.g,
            g_atmosphere.sunColor.b,
            g_atmosphere.sunIntensity,
        };
        sceneUniforms.cameraPosition = glm::vec4{view.cameraPosition, 1.0f};
        sceneUniforms.pbrParams = glm::vec4{
            g_atmosphere.exposure,
            g_atmosphere.ambientIntensity,
            0.0f,
            0.0f,
        };
        sceneUniforms.environmentParams = glm::vec4{
            g_atmosphere.environmentDiffuseColor,
            g_atmosphere.environmentEnabled ? std::max(g_atmosphere.environmentDiffuseIntensity, 0.0f) : 0.0f,
        };
        sceneUniforms.fogParams = glm::vec4{
            g_atmosphere.fogEnabled ? std::max(g_atmosphere.fogDensity, 0.0f) : 0.0f,
            g_atmosphere.fogEnabled ? 1.0f : 0.0f,
            0.0f,
            0.0f,
        };

        for (const LightResource& light : g_lights) {
            if (!light.alive) {
                continue;
            }
            ++stats.liveLightCount;
            if (!isActiveLight(light)) {
                ++stats.disabledLightCount;
                continue;
            }
            ++stats.activeLightCount;
            if (stats.submittedForwardLightCount >= MaxForwardLights) {
                ++stats.overBudgetLightCount;
                continue;
            }

            const uint32_t packedIndex = stats.submittedForwardLightCount;
            const Renderer::LightDescriptor& descriptor = light.descriptor;
            sceneUniforms.forwardLightPositionRange[packedIndex] = glm::vec4{
                descriptor.position,
                descriptor.range,
            };
            sceneUniforms.forwardLightDirectionType[packedIndex] = glm::vec4{
                descriptor.direction,
                lightTypeValue(descriptor.type),
            };
            sceneUniforms.forwardLightColorIntensity[packedIndex] = glm::vec4{
                descriptor.color,
                descriptor.intensity,
            };
            sceneUniforms.forwardLightSpotParams[packedIndex] = glm::vec4{
                std::cos(descriptor.innerConeAngle),
                std::cos(descriptor.outerConeAngle),
                0.0f,
                0.0f,
            };
            ++stats.submittedForwardLightCount;
        }
        sceneUniforms.pbrParams.z = static_cast<float>(stats.submittedForwardLightCount);

        const glm::mat4 identity{1.0f};
        std::vector<VisibleMeshDrawItem> opaqueMeshDrawItems;
        std::vector<VisibleMeshDrawItem> alphaMaskMeshDrawItems;
        std::vector<VisibleMeshDrawItem> alphaBlendMeshDrawItems;

        for (const TerrainTileResource& terrain : g_terrainTiles) {
            if (!terrain.alive ||
                !bgfx::isValid(terrain.vertexBuffer) ||
                !bgfx::isValid(terrain.indexBuffer)) {
                continue;
            }
            ++stats.liveTerrainTiles;
            Renderer::RenderGroupDrawStats* groupStats = findGroupStats(stats, terrain.renderGroup);
            if (groupStats) {
                ++groupStats->liveTerrainTiles;
            }
            if (!passesLayerAndFlags(terrain.visibility, view.layerMask)) {
                ++stats.layerOrFlagCulledTerrainTiles;
                if (groupStats) {
                    ++groupStats->layerOrFlagCulledTerrainTiles;
                }
                continue;
            }
            if (terrain.hasBounds && !intersects(frustum, terrain.worldBounds)) {
                ++stats.frustumCulledTerrainTiles;
                if (groupStats) {
                    ++groupStats->frustumCulledTerrainTiles;
                }
                continue;
            }
            if (terrain.hasBounds && exceedsMaxDrawDistance(
                terrain.visibility,
                terrain.worldBounds,
                view.cameraPosition,
                view.enableDistanceCulling)) {
                ++stats.distanceCulledTerrainTiles;
                if (groupStats) {
                    ++groupStats->distanceCulledTerrainTiles;
                }
                continue;
            }
            ++stats.visibleTerrainTiles;
            if (groupStats) {
                ++groupStats->visibleTerrainTiles;
            }

            const MaterialResource& material = isValidMaterial(terrain.material)
                ? g_materials[terrain.material.id]
                : g_materials[0];

            bgfx::setTransform(&identity[0][0]);
            setMaterialAndSceneUniforms(material, sceneUniforms, MaterialRenderPass::Opaque);
            bgfx::setVertexBuffer(0, terrain.vertexBuffer);
            bgfx::setIndexBuffer(terrain.indexBuffer, 0, terrain.indexCount);
            bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
            bgfx::submit(view.viewId, g_meshProgram);
            ++stats.submittedTerrainTiles;
            if (groupStats) {
                ++groupStats->submittedTerrainTiles;
            }
        }

        for (const MeshInstanceResource& instance : g_instances) {
            if (!instance.alive || !isValidStaticMesh(instance.mesh)) {
                continue;
            }
            ++stats.liveMeshInstances;
            Renderer::RenderGroupDrawStats* groupStats = findGroupStats(stats, instance.renderGroup);
            if (groupStats) {
                ++groupStats->liveMeshInstances;
            }
            if (!passesLayerAndFlags(instance.visibility, view.layerMask)) {
                ++stats.layerOrFlagCulledMeshInstances;
                if (groupStats) {
                    ++groupStats->layerOrFlagCulledMeshInstances;
                }
                continue;
            }

            const glm::mat4 transform = composeTransform(instance);
            const StaticMeshResource& mesh = g_meshes[instance.mesh.id];
            const Aabb worldBounds = mesh.hasBounds ? transformAabb(mesh.localBounds, transform) : Aabb{};
            if (mesh.hasBounds && !intersects(frustum, worldBounds)) {
                ++stats.frustumCulledMeshInstances;
                if (groupStats) {
                    ++groupStats->frustumCulledMeshInstances;
                }
                continue;
            }
            if (mesh.hasBounds && exceedsMaxDrawDistance(
                instance.visibility,
                worldBounds,
                view.cameraPosition,
                view.enableDistanceCulling)) {
                ++stats.distanceCulledMeshInstances;
                if (groupStats) {
                    ++groupStats->distanceCulledMeshInstances;
                }
                continue;
            }
            ++stats.visibleMeshInstances;
            if (groupStats) {
                ++groupStats->visibleMeshInstances;
            }

            bool submittedInstance = false;
            for (uint32_t submeshIndex = 0; submeshIndex < mesh.submeshes.size(); ++submeshIndex) {
                const SubmeshResource& submesh = mesh.submeshes[submeshIndex];
                if (!bgfx::isValid(submesh.vertexBuffer) ||
                    !bgfx::isValid(submesh.indexBuffer) ||
                    !isValidMaterial(submesh.material)) {
                    continue;
                }

                const MaterialHandle materialHandle = instance.hasMaterialOverride && isValidMaterial(instance.materialOverride)
                    ? instance.materialOverride
                    : submesh.material;
                if (!isValidMaterial(materialHandle)) {
                    continue;
                }

                const MaterialResource& material = g_materials[materialHandle.id];
                const MaterialRenderPass pass = renderPassFor(material.alphaMode);
                const glm::vec3 sortCenter = mesh.hasBounds ? centerOf(worldBounds) : glm::vec3{transform[3]};
                const float cameraDistanceSquared = glm::dot(sortCenter - view.cameraPosition, sortCenter - view.cameraPosition);
                VisibleMeshDrawItem item{
                    MeshInstanceHandle{static_cast<uint32_t>(&instance - g_instances.data())},
                    instance.mesh,
                    submeshIndex,
                    materialHandle,
                    instance.renderGroup,
                    transform,
                    worldBounds,
                    pass,
                    cameraDistanceSquared,
                };
                switch (pass) {
                    case MaterialRenderPass::AlphaMask:
                        alphaMaskMeshDrawItems.push_back(item);
                        break;
                    case MaterialRenderPass::AlphaBlend:
                        alphaBlendMeshDrawItems.push_back(item);
                        break;
                    case MaterialRenderPass::Opaque:
                    default:
                        opaqueMeshDrawItems.push_back(item);
                        break;
                }
                submittedInstance = true;
            }

            if (submittedInstance) {
                ++stats.submittedMeshInstances;
                if (groupStats) {
                    ++groupStats->submittedMeshInstances;
                }
            }
        }

        sortDepthWritingDrawItems(opaqueMeshDrawItems);
        sortDepthWritingDrawItems(alphaMaskMeshDrawItems);
        sortAlphaBlendDrawItems(alphaBlendMeshDrawItems);

        addVisiblePassStats(stats, MaterialRenderPass::Opaque, static_cast<uint32_t>(opaqueMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::AlphaMask, static_cast<uint32_t>(alphaMaskMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::AlphaBlend, static_cast<uint32_t>(alphaBlendMeshDrawItems.size()));
        stats.visibleMeshDrawItems = stats.visibleOpaqueMeshDrawItems +
            stats.visibleAlphaMaskMeshDrawItems +
            stats.visibleAlphaBlendMeshDrawItems;

        submitMeshDrawItems(opaqueMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitMeshDrawItems(alphaMaskMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitMeshDrawItems(alphaBlendMeshDrawItems, sceneUniforms, view.viewId, stats);

        return stats;
    }

    void clearDebugPrimitives()
    {
        g_debugLineVertices.clear();
        g_debugDrawStats = {};
    }

    void addDebugLine(const glm::vec3& a, const glm::vec3& b, uint32_t abgr)
    {
        ++g_debugDrawStats.generatedLines;
        if (!isFiniteVec3(a) || !isFiniteVec3(b)) {
            ++g_debugDrawStats.clippedLines;
            return;
        }
        if (g_debugLineVertices.size() / 2 >= g_debugDrawSettings.maxDebugLines) {
            ++g_debugDrawStats.clippedLines;
            return;
        }
        pushDebugVertex(a, abgr);
        pushDebugVertex(b, abgr);
        ++g_debugDrawStats.submittedLines;
    }

    void addDebugLines(std::span<const DebugLinePrimitive> lines)
    {
        for (const DebugLinePrimitive& line : lines) {
            addDebugLine(line.a, line.b, line.abgr);
        }
    }

    void addDebugAabb(const Aabb& bounds, uint32_t abgr)
    {
        if (!isValidAabb(bounds)) {
            return;
        }

        const glm::vec3 corners[] = {
            {bounds.min.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.min.y, bounds.max.z},
            {bounds.min.x, bounds.min.y, bounds.max.z},
            {bounds.min.x, bounds.max.y, bounds.min.z},
            {bounds.max.x, bounds.max.y, bounds.min.z},
            {bounds.max.x, bounds.max.y, bounds.max.z},
            {bounds.min.x, bounds.max.y, bounds.max.z},
        };
        const uint32_t edges[][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (const auto& edge : edges) {
            addDebugLine(corners[edge[0]], corners[edge[1]], abgr);
        }
    }

    void addDebugXZRect(float minX, float minZ, float maxX, float maxZ, float y, uint32_t abgr)
    {
        if (!std::isfinite(minX) || !std::isfinite(minZ) ||
            !std::isfinite(maxX) || !std::isfinite(maxZ) || !std::isfinite(y) ||
            minX > maxX || minZ > maxZ) {
            return;
        }

        const glm::vec3 a{minX, y, minZ};
        const glm::vec3 b{maxX, y, minZ};
        const glm::vec3 c{maxX, y, maxZ};
        const glm::vec3 d{minX, y, maxZ};
        addDebugLine(a, b, abgr);
        addDebugLine(b, c, abgr);
        addDebugLine(c, d, abgr);
        addDebugLine(d, a, abgr);
    }

    void addDebugFrustum(const glm::mat4& inverseViewProjection, uint32_t abgr)
    {
        const float nearZ = bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f;
        const glm::vec4 ndcCorners[] = {
            {-1.0f, -1.0f, nearZ, 1.0f},
            { 1.0f, -1.0f, nearZ, 1.0f},
            { 1.0f,  1.0f, nearZ, 1.0f},
            {-1.0f,  1.0f, nearZ, 1.0f},
            {-1.0f, -1.0f, 1.0f, 1.0f},
            { 1.0f, -1.0f, 1.0f, 1.0f},
            { 1.0f,  1.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 1.0f, 1.0f},
        };

        glm::vec3 corners[8]{};
        for (uint32_t index = 0; index < 8; ++index) {
            const glm::vec4 world = inverseViewProjection * ndcCorners[index];
            if (std::abs(world.w) <= 0.00001f || !std::isfinite(world.w)) {
                return;
            }
            corners[index] = glm::vec3{world} / world.w;
            if (!isFiniteVec3(corners[index])) {
                return;
            }
        }

        const uint32_t edges[][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (const auto& edge : edges) {
            addDebugLine(corners[edge[0]], corners[edge[1]], abgr);
        }
    }

    DebugDrawStats& debugDrawStats()
    {
        return g_debugDrawStats;
    }

    void drawDebugPrimitives(const RenderView& view)
    {
#if !MANUAL_ENGINE_ENABLE_DEBUG_TOOLS
        (void)view;
        return;
#else
        g_debugDrawStats.lastFramePrimitiveBufferSize = static_cast<uint32_t>(g_debugLineVertices.size());
        if (!g_debugDrawSettings.enabled ||
            (view.layerMask & static_cast<uint32_t>(RenderLayer::Debug)) == 0 ||
            g_debugLineVertices.empty() ||
            !bgfx::isValid(g_debugLineProgram)) {
            return;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(g_debugLineVertices.size());
        if (!bgfx::getAvailTransientVertexBuffer(vertexCount, PosColorVertex::layout)) {
            return;
        }

        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, PosColorVertex::layout);
        std::memcpy(vertexBuffer.data, g_debugLineVertices.data(), vertexCount * sizeof(PosColorVertex));

        bgfx::setVertexBuffer(0, &vertexBuffer, 0, vertexCount);
        bgfx::setState(
            BGFX_STATE_WRITE_RGB |
            BGFX_STATE_WRITE_A |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_PT_LINES
        );
        bgfx::submit(view.viewId, g_debugLineProgram);
#endif
    }
}
