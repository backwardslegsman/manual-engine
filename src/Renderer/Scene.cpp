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
        std::string name;
        Renderer::Aabb localBounds;
        bool hasBounds = false;
        std::vector<SubmeshResource> submeshes;
    };

    struct SkinnedMeshResource {
        bool alive = false;
        std::string name;
        Renderer::Aabb localBounds;
        bool hasBounds = false;
        std::vector<SubmeshResource> submeshes;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t jointCount = 0;
        uint32_t maxInfluenceCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t normalizedWeightVertexCount = 0;
        uint32_t validMaterialReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
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

    struct SkinnedMeshInstanceResource {
        bool alive = false;
        Renderer::SkinnedMeshHandle mesh;
        Renderer::RenderVisibility visibility;
        glm::mat4 transform{1.0f};
        Renderer::RenderGroupHandle renderGroup;
        std::vector<glm::mat4> jointMatrices;
        uint32_t submittedJointCount = 0;
        uint32_t truncatedJointCount = 0;
    };

    struct TerrainTileResource {
        bool alive = false;
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        Renderer::MaterialHandle material;
        Renderer::TerrainMaterialSetHandle materialSet;
        Renderer::RenderVisibility visibility{Renderer::RenderLayer::Terrain, Renderer::VisibilityFlags::Visible, 0.0f};
        Renderer::Aabb worldBounds;
        bool hasBounds = false;
        Renderer::RenderGroupHandle renderGroup;
    };

    struct TerrainMaterialSetResource {
        bool alive = false;
        Renderer::TerrainMaterialSetDescriptor descriptor;
        Renderer::TerrainMaterialSetDiagnostics diagnostics;
    };

    std::vector<RenderGroupResource> g_renderGroups;
    std::vector<LightResource> g_lights;
    std::vector<MaterialResource> g_materials;
    std::vector<StaticMeshResource> g_meshes;
    std::vector<SkinnedMeshResource> g_skinnedMeshes;
    std::vector<MeshInstanceResource> g_instances;
    std::vector<SkinnedMeshInstanceResource> g_skinnedInstances;
    std::vector<TerrainTileResource> g_terrainTiles;
    std::vector<TerrainMaterialSetResource> g_terrainMaterialSets;
    std::vector<Renderer::PosColorVertex> g_debugLineVertices;

    bgfx::ProgramHandle g_meshProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle g_terrainProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle g_skinnedMeshProgram = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle g_jointPalette = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, Renderer::MaxTerrainMaterialLayers> g_terrainBaseColorSamplers{};
    std::array<bgfx::UniformHandle, Renderer::MaxTerrainMaterialLayers> g_terrainNormalSamplers{};
    std::array<bgfx::UniformHandle, Renderer::MaxTerrainMaterialLayers> g_terrainMetallicRoughnessSamplers{};
    bgfx::UniformHandle g_terrainLayerBaseColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_terrainLayerParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_terrainLayerTiling = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_terrainRuleParams0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_terrainRuleHeightSlope = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_terrainRuleWorldXz0 = BGFX_INVALID_HANDLE;

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

    bool isValidSkinnedMesh(Renderer::SkinnedMeshHandle handle)
    {
        return handle.id < g_skinnedMeshes.size() && g_skinnedMeshes[handle.id].alive;
    }

    bool isValidMeshInstance(Renderer::MeshInstanceHandle handle)
    {
        return handle.id < g_instances.size() && g_instances[handle.id].alive;
    }

    bool isValidSkinnedMeshInstance(Renderer::SkinnedMeshInstanceHandle handle)
    {
        return handle.id < g_skinnedInstances.size() && g_skinnedInstances[handle.id].alive;
    }

    bool isValidTerrain(Renderer::TerrainHandle handle)
    {
        return handle.id < g_terrainTiles.size() && g_terrainTiles[handle.id].alive;
    }

    bool isValidTerrainMaterialSet(Renderer::TerrainMaterialSetHandle handle)
    {
        return handle.id < g_terrainMaterialSets.size() && g_terrainMaterialSets[handle.id].alive;
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

    bool isFiniteVec2(const glm::vec2& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool isFiniteVec4(const glm::vec4& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
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

    TerrainMaterialSetResource makeTerrainMaterialSetResource(const Renderer::TerrainMaterialSetDescriptor& descriptor)
    {
        TerrainMaterialSetResource resource;
        resource.alive = true;
        resource.descriptor.name = descriptor.name;
        resource.descriptor.fallbackLayerIndex = std::min<uint32_t>(
            descriptor.fallbackLayerIndex,
            Renderer::MaxTerrainMaterialLayers - 1u);

        resource.diagnostics.valid = true;
        resource.diagnostics.name = descriptor.name;
        resource.diagnostics.truncatedLayerCount = descriptor.layers.size() > Renderer::MaxTerrainMaterialLayers
            ? static_cast<uint32_t>(descriptor.layers.size() - Renderer::MaxTerrainMaterialLayers)
            : 0u;
        resource.diagnostics.truncatedRuleCount = descriptor.rules.size() > Renderer::MaxTerrainMaterialRules
            ? static_cast<uint32_t>(descriptor.rules.size() - Renderer::MaxTerrainMaterialRules)
            : 0u;

        const uint32_t layerCount = std::min<uint32_t>(
            static_cast<uint32_t>(descriptor.layers.size()),
            Renderer::MaxTerrainMaterialLayers);
        resource.descriptor.layers.reserve(layerCount);
        for (uint32_t index = 0; index < layerCount; ++index) {
            Renderer::TerrainMaterialLayerDescriptor layer = descriptor.layers[index];
            if (!isFiniteVec4(layer.baseColorFactor)) {
                layer.baseColorFactor = glm::vec4{1.0f};
            }
            if (!isFiniteVec2(layer.tilingScale) || layer.tilingScale.x == 0.0f || layer.tilingScale.y == 0.0f) {
                layer.tilingScale = glm::vec2{1.0f};
            }
            layer.normalScale = std::isfinite(layer.normalScale) ? layer.normalScale : 1.0f;
            layer.metallicFactor = std::clamp(std::isfinite(layer.metallicFactor) ? layer.metallicFactor : 0.0f, 0.0f, 1.0f);
            layer.roughnessFactor = std::clamp(std::isfinite(layer.roughnessFactor) ? layer.roughnessFactor : 1.0f, 0.04f, 1.0f);
            if (!Renderer::isValid(layer.baseColorTexture)) {
                layer.baseColorTexture = g_whiteTexture;
                ++resource.diagnostics.missingTextureFallbackCount;
            }
            if (!Renderer::isValid(layer.normalTexture)) {
                layer.normalTexture = g_flatNormalTexture;
                ++resource.diagnostics.missingTextureFallbackCount;
            }
            if (!Renderer::isValid(layer.metallicRoughnessTexture)) {
                layer.metallicRoughnessTexture = g_whiteTexture;
                ++resource.diagnostics.missingTextureFallbackCount;
            }
            resource.descriptor.layers.push_back(layer);
        }

        if (resource.descriptor.layers.empty()) {
            Renderer::TerrainMaterialLayerDescriptor fallback;
            fallback.name = "default";
            fallback.baseColorTexture = g_whiteTexture;
            fallback.normalTexture = g_flatNormalTexture;
            fallback.metallicRoughnessTexture = g_whiteTexture;
            resource.descriptor.layers.push_back(fallback);
            resource.descriptor.fallbackLayerIndex = 0;
        } else if (resource.descriptor.fallbackLayerIndex >= resource.descriptor.layers.size()) {
            resource.descriptor.fallbackLayerIndex = 0;
        }

        const uint32_t ruleCount = std::min<uint32_t>(
            static_cast<uint32_t>(descriptor.rules.size()),
            Renderer::MaxTerrainMaterialRules);
        resource.descriptor.rules.reserve(ruleCount);
        for (uint32_t index = 0; index < ruleCount; ++index) {
            Renderer::TerrainMaterialRuleDescriptor rule = descriptor.rules[index];
            if (rule.layerIndex >= resource.descriptor.layers.size()) {
                ++resource.diagnostics.invalidRuleCount;
                continue;
            }
            rule.weight = std::max(std::isfinite(rule.weight) ? rule.weight : 1.0f, 0.0f);
            rule.blendFalloff = std::max(std::isfinite(rule.blendFalloff) ? rule.blendFalloff : 0.0f, 0.0f);
            if (rule.useHeightRange && (!std::isfinite(rule.minHeight) || !std::isfinite(rule.maxHeight) || rule.minHeight > rule.maxHeight)) {
                ++resource.diagnostics.invalidRuleCount;
                continue;
            }
            if (rule.useSlopeRange && (!std::isfinite(rule.minSlopeDegrees) || !std::isfinite(rule.maxSlopeDegrees) || rule.minSlopeDegrees > rule.maxSlopeDegrees)) {
                ++resource.diagnostics.invalidRuleCount;
                continue;
            }
            if (rule.useWorldXRange && (!std::isfinite(rule.minWorldX) || !std::isfinite(rule.maxWorldX) || rule.minWorldX > rule.maxWorldX)) {
                ++resource.diagnostics.invalidRuleCount;
                continue;
            }
            if (rule.useWorldZRange && (!std::isfinite(rule.minWorldZ) || !std::isfinite(rule.maxWorldZ) || rule.minWorldZ > rule.maxWorldZ)) {
                ++resource.diagnostics.invalidRuleCount;
                continue;
            }
            resource.descriptor.rules.push_back(rule);
        }

        resource.diagnostics.layerCount = static_cast<uint32_t>(resource.descriptor.layers.size());
        std::stable_sort(
            resource.descriptor.rules.begin(),
            resource.descriptor.rules.end(),
            [](const Renderer::TerrainMaterialRuleDescriptor& lhs, const Renderer::TerrainMaterialRuleDescriptor& rhs) {
                return lhs.priority > rhs.priority;
            });
        resource.diagnostics.ruleCount = static_cast<uint32_t>(resource.descriptor.rules.size());
        return resource;
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

    Renderer::SkinnedMeshHandle addSkinnedMesh(SkinnedMeshResource mesh)
    {
        mesh.alive = true;
        for (uint32_t index = 0; index < g_skinnedMeshes.size(); ++index) {
            if (!g_skinnedMeshes[index].alive) {
                g_skinnedMeshes[index] = std::move(mesh);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_skinnedMeshes.size());
        g_skinnedMeshes.push_back(std::move(mesh));
        return {id};
    }

    StaticMeshResource makeStaticMeshResource(const Renderer::StaticMeshDescriptor& descriptor)
    {
        StaticMeshResource mesh;
        mesh.name = descriptor.name;
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

    SkinnedMeshResource makeSkinnedMeshResource(const Renderer::SkinnedMeshDescriptor& descriptor)
    {
        SkinnedMeshResource mesh;
        mesh.name = descriptor.name;
        mesh.submeshes.reserve(descriptor.submeshes.size());
        mesh.localBounds = emptyAabb();
        mesh.jointCount = descriptor.jointCount;
        mesh.maxInfluenceCount = descriptor.maxInfluencesPerVertex;
        mesh.truncatedInfluenceVertexCount = descriptor.truncatedInfluenceVertexCount;
        mesh.zeroWeightVertexCount = descriptor.zeroWeightVertexCount;
        mesh.normalizedWeightVertexCount = descriptor.normalizedWeightVertexCount;

        for (const Renderer::SkinnedSubmeshDescriptor& sourceSubmesh : descriptor.submeshes) {
            if (sourceSubmesh.vertices.empty() || sourceSubmesh.indices.empty()) {
                continue;
            }

            for (const Renderer::SkinnedMeshVertex& vertex : sourceSubmesh.vertices) {
                includePoint(mesh.localBounds, {vertex.px, vertex.py, vertex.pz});
                mesh.hasBounds = true;
            }

            SubmeshResource submesh;
            submesh.vertexBuffer = bgfx::createVertexBuffer(
                bgfx::copy(
                    sourceSubmesh.vertices.data(),
                    static_cast<uint32_t>(sourceSubmesh.vertices.size() * sizeof(Renderer::SkinnedMeshVertex))),
                Renderer::SkinnedMeshVertex::layout
            );
            submesh.indexBuffer = bgfx::createIndexBuffer(
                bgfx::copy(
                    sourceSubmesh.indices.data(),
                    static_cast<uint32_t>(sourceSubmesh.indices.size() * sizeof(uint32_t))),
                BGFX_BUFFER_INDEX32
            );
            submesh.indexCount = static_cast<uint32_t>(sourceSubmesh.indices.size());
            if (isValidMaterial(sourceSubmesh.material)) {
                submesh.material = sourceSubmesh.material;
                ++mesh.validMaterialReferenceCount;
            } else {
                submesh.material = Renderer::MaterialHandle{0};
                ++mesh.invalidMaterialReferenceCount;
            }

            if (bgfx::isValid(submesh.vertexBuffer) && bgfx::isValid(submesh.indexBuffer)) {
                mesh.vertexCount += static_cast<uint32_t>(sourceSubmesh.vertices.size());
                mesh.indexCount += static_cast<uint32_t>(sourceSubmesh.indices.size());
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

    struct VisibleSkinnedMeshDrawItem {
        Renderer::SkinnedMeshInstanceHandle instance;
        Renderer::SkinnedMeshHandle mesh;
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

    void setTerrainMaterialSetAndSceneUniforms(
        const TerrainMaterialSetResource& materialSet,
        const SceneUniforms& uniforms)
    {
        std::array<glm::vec4, Renderer::MaxTerrainMaterialLayers> layerBaseColors{};
        std::array<glm::vec4, Renderer::MaxTerrainMaterialLayers> layerParams{};
        std::array<glm::vec4, Renderer::MaxTerrainMaterialLayers> layerTilings{};
        std::array<glm::vec4, Renderer::MaxTerrainMaterialRules> ruleParams{};
        std::array<glm::vec4, Renderer::MaxTerrainMaterialRules> ruleHeightSlope{};
        std::array<glm::vec4, Renderer::MaxTerrainMaterialRules> ruleWorldXz{};

        const uint32_t layerCount = std::min<uint32_t>(
            static_cast<uint32_t>(materialSet.descriptor.layers.size()),
            Renderer::MaxTerrainMaterialLayers);
        for (uint32_t index = 0; index < Renderer::MaxTerrainMaterialLayers; ++index) {
            Renderer::TextureHandle baseColorTexture = g_whiteTexture;
            Renderer::TextureHandle normalTexture = g_flatNormalTexture;
            Renderer::TextureHandle metallicRoughnessTexture = g_whiteTexture;
            if (index < layerCount) {
                const Renderer::TerrainMaterialLayerDescriptor& layer = materialSet.descriptor.layers[index];
                baseColorTexture = Renderer::isValid(layer.baseColorTexture) ? layer.baseColorTexture : g_whiteTexture;
                normalTexture = Renderer::isValid(layer.normalTexture) ? layer.normalTexture : g_flatNormalTexture;
                metallicRoughnessTexture = Renderer::isValid(layer.metallicRoughnessTexture)
                    ? layer.metallicRoughnessTexture
                    : g_whiteTexture;
                layerBaseColors[index] = layer.baseColorFactor;
                layerParams[index] = glm::vec4{
                    std::clamp(layer.metallicFactor, 0.0f, 1.0f),
                    std::clamp(layer.roughnessFactor, 0.04f, 1.0f),
                    layer.normalScale,
                    (layer.enabled ? 1.0f : 0.0f) +
                        (normalTexture.id != g_flatNormalTexture.id ? 2.0f : 0.0f) +
                        (metallicRoughnessTexture.id != g_whiteTexture.id ? 4.0f : 0.0f),
                };
                layerTilings[index] = glm::vec4{layer.tilingScale, 0.0f, 0.0f};
            } else {
                layerBaseColors[index] = glm::vec4{1.0f};
                layerParams[index] = glm::vec4{0.0f, 1.0f, 1.0f, 0.0f};
                layerTilings[index] = glm::vec4{1.0f, 1.0f, 0.0f, 0.0f};
            }
            bgfx::setTexture(index, g_terrainBaseColorSamplers[index], getNativeTexture(baseColorTexture));
            bgfx::setTexture(4 + index, g_terrainNormalSamplers[index], getNativeTexture(normalTexture));
            bgfx::setTexture(
                8 + index,
                g_terrainMetallicRoughnessSamplers[index],
                getNativeTexture(metallicRoughnessTexture));
        }

        const uint32_t ruleCount = std::min<uint32_t>(
            static_cast<uint32_t>(materialSet.descriptor.rules.size()),
            Renderer::MaxTerrainMaterialRules);
        for (uint32_t index = 0; index < Renderer::MaxTerrainMaterialRules; ++index) {
            if (index < ruleCount) {
                const Renderer::TerrainMaterialRuleDescriptor& rule = materialSet.descriptor.rules[index];
                const float flags =
                    (rule.useHeightRange ? 1.0f : 0.0f) +
                    (rule.useSlopeRange ? 2.0f : 0.0f) +
                    (rule.useWorldXRange ? 4.0f : 0.0f) +
                    (rule.useWorldZRange ? 8.0f : 0.0f);
                ruleParams[index] = glm::vec4{
                    static_cast<float>(rule.layerIndex),
                    rule.weight,
                    rule.blendFalloff,
                    flags,
                };
                ruleHeightSlope[index] = glm::vec4{
                    rule.minHeight,
                    rule.maxHeight,
                    rule.minSlopeDegrees,
                    rule.maxSlopeDegrees,
                };
                ruleWorldXz[index] = glm::vec4{
                    rule.minWorldX,
                    rule.maxWorldX,
                    rule.minWorldZ,
                    rule.maxWorldZ,
                };
            } else {
                ruleParams[index] = glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f};
            }
        }

        const glm::vec4 alphaParams{0.0f, 0.5f, 0.0f, 0.0f};
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
        bgfx::setUniform(g_terrainLayerBaseColor, layerBaseColors.data(), Renderer::MaxTerrainMaterialLayers);
        bgfx::setUniform(g_terrainLayerParams, layerParams.data(), Renderer::MaxTerrainMaterialLayers);
        bgfx::setUniform(g_terrainLayerTiling, layerTilings.data(), Renderer::MaxTerrainMaterialLayers);
        bgfx::setUniform(g_terrainRuleParams0, ruleParams.data(), Renderer::MaxTerrainMaterialRules);
        bgfx::setUniform(g_terrainRuleHeightSlope, ruleHeightSlope.data(), Renderer::MaxTerrainMaterialRules);
        bgfx::setUniform(g_terrainRuleWorldXz0, ruleWorldXz.data(), Renderer::MaxTerrainMaterialRules);
    }

    bool sameBatch(const VisibleMeshDrawItem& lhs, const VisibleMeshDrawItem& rhs)
    {
        return lhs.mesh.id == rhs.mesh.id &&
            lhs.submeshIndex == rhs.submeshIndex &&
            lhs.material.id == rhs.material.id;
    }

    bool sameBatch(const VisibleSkinnedMeshDrawItem& lhs, const VisibleSkinnedMeshDrawItem& rhs)
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

    void sortDepthWritingDrawItems(std::vector<VisibleSkinnedMeshDrawItem>& drawItems)
    {
        std::sort(
            drawItems.begin(),
            drawItems.end(),
            [](const VisibleSkinnedMeshDrawItem& lhs, const VisibleSkinnedMeshDrawItem& rhs) {
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

    void sortAlphaBlendDrawItems(std::vector<VisibleSkinnedMeshDrawItem>& drawItems)
    {
        std::sort(
            drawItems.begin(),
            drawItems.end(),
            [](const VisibleSkinnedMeshDrawItem& lhs, const VisibleSkinnedMeshDrawItem& rhs) {
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

    void submitSkinnedMeshDrawItems(
        const std::vector<VisibleSkinnedMeshDrawItem>& drawItems,
        const SceneUniforms& sceneUniforms,
        bgfx::ViewId viewId,
        Renderer::SceneDrawStats& stats)
    {
        uint32_t currentBatchSize = 0;
        VisibleSkinnedMeshDrawItem previousItem;
        bool hasPreviousItem = false;

        for (const VisibleSkinnedMeshDrawItem& item : drawItems) {
            if (!isValidSkinnedMesh(item.mesh) ||
                !isValidSkinnedMeshInstance(item.instance) ||
                item.submeshIndex >= g_skinnedMeshes[item.mesh.id].submeshes.size() ||
                !isValidMaterial(item.material)) {
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

            const SubmeshResource& submesh = g_skinnedMeshes[item.mesh.id].submeshes[item.submeshIndex];
            if (!bgfx::isValid(submesh.vertexBuffer) || !bgfx::isValid(submesh.indexBuffer)) {
                continue;
            }

            SkinnedMeshInstanceResource& instance = g_skinnedInstances[item.instance.id];
            const uint32_t submittedJointCount = std::min(
                static_cast<uint32_t>(instance.jointMatrices.size()),
                Renderer::MaxSkinnedJointsPerMesh);
            instance.submittedJointCount = submittedJointCount;
            instance.truncatedJointCount = instance.jointMatrices.size() > Renderer::MaxSkinnedJointsPerMesh
                ? static_cast<uint32_t>(instance.jointMatrices.size() - Renderer::MaxSkinnedJointsPerMesh)
                : 0;

            std::array<glm::mat4, Renderer::MaxSkinnedJointsPerMesh> jointPalette;
            jointPalette.fill(glm::mat4{1.0f});
            for (uint32_t jointIndex = 0; jointIndex < submittedJointCount; ++jointIndex) {
                jointPalette[jointIndex] = instance.jointMatrices[jointIndex];
            }

            const MaterialResource& material = g_materials[item.material.id];
            bgfx::setTransform(&item.transform[0][0]);
            setMaterialAndSceneUniforms(material, sceneUniforms, item.pass);
            bgfx::setUniform(g_jointPalette, jointPalette.data(), Renderer::MaxSkinnedJointsPerMesh);
            bgfx::setVertexBuffer(0, submesh.vertexBuffer);
            bgfx::setIndexBuffer(submesh.indexBuffer, 0, submesh.indexCount);
            bgfx::setState(renderStateFor(material, item.pass));
            bgfx::submit(viewId, g_skinnedMeshProgram);
            ++stats.submittedMeshDrawItems;
            ++stats.submittedSkinnedMeshDrawItems;
            stats.submittedSkinnedJointPaletteCount += submittedJointCount;
            stats.truncatedSkinnedJointPaletteCount += instance.truncatedJointCount;
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

        bgfx::ShaderHandle terrainVsh = loadShader("vs_terrain.bin");
        bgfx::ShaderHandle terrainFsh = loadShader("fs_terrain.bin");
        if (bgfx::isValid(terrainVsh) && bgfx::isValid(terrainFsh)) {
            g_terrainProgram = bgfx::createProgram(terrainVsh, terrainFsh, true);
        } else {
            if (bgfx::isValid(terrainVsh)) {
                bgfx::destroy(terrainVsh);
            }
            if (bgfx::isValid(terrainFsh)) {
                bgfx::destroy(terrainFsh);
            }
        }

        bgfx::ShaderHandle skinnedVsh = loadShader("vs_skinned_mesh.bin");
        bgfx::ShaderHandle skinnedFsh = loadShader("fs_mesh.bin");
        if (!bgfx::isValid(skinnedVsh) || !bgfx::isValid(skinnedFsh)) {
            if (bgfx::isValid(skinnedVsh)) {
                bgfx::destroy(skinnedVsh);
            }
            if (bgfx::isValid(skinnedFsh)) {
                bgfx::destroy(skinnedFsh);
            }
            return false;
        }

        g_skinnedMeshProgram = bgfx::createProgram(skinnedVsh, skinnedFsh, true);
        if (!bgfx::isValid(g_skinnedMeshProgram)) {
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
        g_jointPalette = bgfx::createUniform(
            "u_jointPalette",
            bgfx::UniformType::Mat4,
            MaxSkinnedJointsPerMesh);
        for (uint32_t index = 0; index < MaxTerrainMaterialLayers; ++index) {
            const std::string suffix = std::to_string(index);
            g_terrainBaseColorSamplers[index] =
                bgfx::createUniform(("s_terrainBaseColor" + suffix).c_str(), bgfx::UniformType::Sampler);
            g_terrainNormalSamplers[index] =
                bgfx::createUniform(("s_terrainNormal" + suffix).c_str(), bgfx::UniformType::Sampler);
            g_terrainMetallicRoughnessSamplers[index] =
                bgfx::createUniform(("s_terrainMetallicRoughness" + suffix).c_str(), bgfx::UniformType::Sampler);
        }
        g_terrainLayerBaseColor = bgfx::createUniform(
            "u_terrainLayerBaseColor",
            bgfx::UniformType::Vec4,
            MaxTerrainMaterialLayers);
        g_terrainLayerParams = bgfx::createUniform(
            "u_terrainLayerParams",
            bgfx::UniformType::Vec4,
            MaxTerrainMaterialLayers);
        g_terrainLayerTiling = bgfx::createUniform(
            "u_terrainLayerTiling",
            bgfx::UniformType::Vec4,
            MaxTerrainMaterialLayers);
        g_terrainRuleParams0 = bgfx::createUniform(
            "u_terrainRuleParams0",
            bgfx::UniformType::Vec4,
            MaxTerrainMaterialRules);
        g_terrainRuleHeightSlope = bgfx::createUniform(
            "u_terrainRuleHeightSlope",
            bgfx::UniformType::Vec4,
            MaxTerrainMaterialRules);
        g_terrainRuleWorldXz0 = bgfx::createUniform(
            "u_terrainRuleWorldXz0",
            bgfx::UniformType::Vec4,
            MaxTerrainMaterialRules);

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

        for (SkinnedMeshResource& mesh : g_skinnedMeshes) {
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

    TerrainMaterialSetHandle createTerrainMaterialSet(const TerrainMaterialSetDescriptor& descriptor)
    {
        TerrainMaterialSetResource resource = makeTerrainMaterialSetResource(descriptor);
        for (uint32_t index = 0; index < g_terrainMaterialSets.size(); ++index) {
            if (!g_terrainMaterialSets[index].alive) {
                g_terrainMaterialSets[index] = std::move(resource);
                return {index};
            }
        }

        const uint32_t id = static_cast<uint32_t>(g_terrainMaterialSets.size());
        g_terrainMaterialSets.push_back(std::move(resource));
        return {id};
    }

    void destroyTerrainMaterialSet(TerrainMaterialSetHandle materialSet)
    {
        if (!isValidTerrainMaterialSet(materialSet)) {
            return;
        }

        for (TerrainTileResource& terrain : g_terrainTiles) {
            if (terrain.alive && terrain.materialSet.id == materialSet.id) {
                terrain.materialSet = {};
            }
        }
        g_terrainMaterialSets[materialSet.id] = {};
    }

    void setTerrainMaterialSetDescriptor(TerrainMaterialSetHandle materialSet, const TerrainMaterialSetDescriptor& descriptor)
    {
        if (!isValidTerrainMaterialSet(materialSet)) {
            return;
        }
        g_terrainMaterialSets[materialSet.id] = makeTerrainMaterialSetResource(descriptor);
    }

    TerrainMaterialSetDiagnostics terrainMaterialSetDiagnostics(TerrainMaterialSetHandle materialSet)
    {
        if (!isValidTerrainMaterialSet(materialSet)) {
            return {};
        }
        return g_terrainMaterialSets[materialSet.id].diagnostics;
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
        for (SkinnedMeshResource& mesh : g_skinnedMeshes) {
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
        g_skinnedMeshes.clear();
        g_materials.clear();
        g_instances.clear();
        g_skinnedInstances.clear();
        g_lights.clear();
        g_renderGroups.clear();
        g_terrainMaterialSets.clear();

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

        std::vector<bgfx::UniformHandle> uniforms = {
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
            g_jointPalette,
            g_terrainLayerBaseColor,
            g_terrainLayerParams,
            g_terrainLayerTiling,
            g_terrainRuleParams0,
            g_terrainRuleHeightSlope,
            g_terrainRuleWorldXz0,
        };
        for (uint32_t index = 0; index < MaxTerrainMaterialLayers; ++index) {
            uniforms.push_back(g_terrainBaseColorSamplers[index]);
            uniforms.push_back(g_terrainNormalSamplers[index]);
            uniforms.push_back(g_terrainMetallicRoughnessSamplers[index]);
        }
        for (bgfx::UniformHandle uniform : uniforms) {
            if (bgfx::isValid(uniform)) {
                bgfx::destroy(uniform);
            }
        }

        if (bgfx::isValid(g_meshProgram)) {
            bgfx::destroy(g_meshProgram);
            g_meshProgram = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_terrainProgram)) {
            bgfx::destroy(g_terrainProgram);
            g_terrainProgram = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_skinnedMeshProgram)) {
            bgfx::destroy(g_skinnedMeshProgram);
            g_skinnedMeshProgram = BGFX_INVALID_HANDLE;
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

    SkinnedMeshHandle createSkinnedMesh(const SkinnedMeshDescriptor& descriptor)
    {
        SkinnedMeshResource mesh = makeSkinnedMeshResource(descriptor);
        if (mesh.submeshes.empty()) {
            return {};
        }

        return addSkinnedMesh(std::move(mesh));
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

    void destroySkinnedMesh(SkinnedMeshHandle mesh)
    {
        if (!isValidSkinnedMesh(mesh)) {
            return;
        }

        for (SkinnedMeshInstanceResource& instance : g_skinnedInstances) {
            if (instance.alive && instance.mesh.id == mesh.id) {
                instance = {};
            }
        }

        SkinnedMeshResource& resource = g_skinnedMeshes[mesh.id];
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

    SkinnedMeshDiagnostics skinnedMeshDiagnostics(SkinnedMeshHandle mesh)
    {
        SkinnedMeshDiagnostics diagnostics;
        if (!isValidSkinnedMesh(mesh)) {
            return diagnostics;
        }

        const SkinnedMeshResource& resource = g_skinnedMeshes[mesh.id];
        diagnostics.valid = true;
        diagnostics.name = resource.name;
        diagnostics.submeshCount = static_cast<uint32_t>(resource.submeshes.size());
        diagnostics.vertexCount = resource.vertexCount;
        diagnostics.indexCount = resource.indexCount;
        diagnostics.jointCount = resource.jointCount;
        diagnostics.maxInfluenceCount = resource.maxInfluenceCount;
        diagnostics.truncatedInfluenceVertexCount = resource.truncatedInfluenceVertexCount;
        diagnostics.zeroWeightVertexCount = resource.zeroWeightVertexCount;
        diagnostics.normalizedWeightVertexCount = resource.normalizedWeightVertexCount;
        diagnostics.validMaterialReferenceCount = resource.validMaterialReferenceCount;
        diagnostics.invalidMaterialReferenceCount = resource.invalidMaterialReferenceCount;
        return diagnostics;
    }

    SkinnedMeshInstanceHandle createSkinnedInstance(SkinnedMeshHandle mesh)
    {
        if (!isValidSkinnedMesh(mesh)) {
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
        SkinnedMeshInstanceResource instance;
        instance.alive = true;
        instance.mesh = mesh;
        instance.visibility = {RenderLayer::Props, VisibilityFlags::Visible, 0.0f};
        g_skinnedInstances.push_back(std::move(instance));
        return {index};
    }

    void destroySkinnedInstance(SkinnedMeshInstanceHandle instance)
    {
        if (isValidSkinnedMeshInstance(instance)) {
            g_skinnedInstances[instance.id] = {};
        }
    }

    void setSkinnedInstanceTransform(SkinnedMeshInstanceHandle instance, const glm::mat4& transform)
    {
        if (isValidSkinnedMeshInstance(instance)) {
            g_skinnedInstances[instance.id].transform = transform;
        }
    }

    void setSkinnedInstanceRenderLayer(SkinnedMeshInstanceHandle instance, RenderLayer layer)
    {
        if (isValidSkinnedMeshInstance(instance)) {
            g_skinnedInstances[instance.id].visibility.layer = layer;
        }
    }

    void setSkinnedInstanceMaxDrawDistance(SkinnedMeshInstanceHandle instance, float maxDrawDistance)
    {
        if (isValidSkinnedMeshInstance(instance)) {
            g_skinnedInstances[instance.id].visibility.maxDrawDistance = std::max(maxDrawDistance, 0.0f);
        }
    }

    void setSkinnedInstanceRenderGroup(SkinnedMeshInstanceHandle instance, RenderGroupHandle group)
    {
        if (isValidSkinnedMeshInstance(instance) && isValidRenderGroup(group)) {
            g_skinnedInstances[instance.id].renderGroup = group;
        }
    }

    void clearSkinnedInstanceRenderGroup(SkinnedMeshInstanceHandle instance)
    {
        if (isValidSkinnedMeshInstance(instance)) {
            g_skinnedInstances[instance.id].renderGroup = {};
        }
    }

    void setSkinnedInstanceJointMatrices(SkinnedMeshInstanceHandle instance, std::span<const glm::mat4> matrices)
    {
        if (!isValidSkinnedMeshInstance(instance)) {
            return;
        }

        SkinnedMeshInstanceResource& resource = g_skinnedInstances[instance.id];
        resource.jointMatrices.assign(matrices.begin(), matrices.end());
        resource.submittedJointCount = std::min(
            static_cast<uint32_t>(resource.jointMatrices.size()),
            MaxSkinnedJointsPerMesh);
        resource.truncatedJointCount = resource.jointMatrices.size() > MaxSkinnedJointsPerMesh
            ? static_cast<uint32_t>(resource.jointMatrices.size() - MaxSkinnedJointsPerMesh)
            : 0;
    }

    SkinnedInstanceDiagnostics skinnedInstanceDiagnostics(SkinnedMeshInstanceHandle instance)
    {
        SkinnedInstanceDiagnostics diagnostics;
        if (!isValidSkinnedMeshInstance(instance)) {
            return diagnostics;
        }

        const SkinnedMeshInstanceResource& resource = g_skinnedInstances[instance.id];
        diagnostics.valid = true;
        diagnostics.mesh = resource.mesh;
        diagnostics.visibility = resource.visibility;
        diagnostics.renderGroup = resource.renderGroup;
        diagnostics.submittedJointCount = resource.submittedJointCount;
        diagnostics.truncatedJointCount = resource.truncatedJointCount;
        diagnostics.boundsValid = isValidSkinnedMesh(resource.mesh) && g_skinnedMeshes[resource.mesh.id].hasBounds;
        return diagnostics;
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

    void setTerrainMaterialSet(TerrainHandle terrain, TerrainMaterialSetHandle materialSet)
    {
        if (!isValidTerrain(terrain) || !isValidTerrainMaterialSet(materialSet)) {
            return;
        }
        g_terrainTiles[terrain.id].materialSet = materialSet;
    }

    void clearTerrainMaterialSet(TerrainHandle terrain)
    {
        if (!isValidTerrain(terrain)) {
            return;
        }
        g_terrainTiles[terrain.id].materialSet = {};
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
        std::vector<VisibleSkinnedMeshDrawItem> opaqueSkinnedMeshDrawItems;
        std::vector<VisibleSkinnedMeshDrawItem> alphaMaskSkinnedMeshDrawItems;
        std::vector<VisibleSkinnedMeshDrawItem> alphaBlendSkinnedMeshDrawItems;

        for (const TerrainTileResource& terrain : g_terrainTiles) {
            if (!terrain.alive ||
                !bgfx::isValid(terrain.vertexBuffer) ||
                !bgfx::isValid(terrain.indexBuffer)) {
                continue;
            }
            ++stats.liveTerrainTiles;
            if (isValidTerrainMaterialSet(terrain.materialSet)) {
                ++stats.assignedLayeredTerrainTiles;
            }
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

            bgfx::setTransform(&identity[0][0]);
            bgfx::setVertexBuffer(0, terrain.vertexBuffer);
            bgfx::setIndexBuffer(terrain.indexBuffer, 0, terrain.indexCount);
            bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
            if (isValidTerrainMaterialSet(terrain.materialSet) && bgfx::isValid(g_terrainProgram)) {
                setTerrainMaterialSetAndSceneUniforms(g_terrainMaterialSets[terrain.materialSet.id], sceneUniforms);
                bgfx::submit(view.viewId, g_terrainProgram);
                ++stats.submittedLayeredTerrainTiles;
                if (groupStats) {
                    ++groupStats->submittedLayeredTerrainTiles;
                }
            } else {
                const MaterialResource& material = isValidMaterial(terrain.material)
                    ? g_materials[terrain.material.id]
                    : g_materials[0];
                setMaterialAndSceneUniforms(material, sceneUniforms, MaterialRenderPass::Opaque);
                bgfx::submit(view.viewId, g_meshProgram);
                ++stats.fallbackTerrainTiles;
                if (groupStats) {
                    ++groupStats->fallbackTerrainTiles;
                }
            }
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

        for (const SkinnedMeshInstanceResource& instance : g_skinnedInstances) {
            if (!instance.alive || !isValidSkinnedMesh(instance.mesh)) {
                continue;
            }
            ++stats.liveSkinnedMeshInstances;
            Renderer::RenderGroupDrawStats* groupStats = findGroupStats(stats, instance.renderGroup);
            if (groupStats) {
                ++groupStats->liveSkinnedMeshInstances;
            }
            if (!passesLayerAndFlags(instance.visibility, view.layerMask)) {
                ++stats.layerOrFlagCulledSkinnedMeshInstances;
                if (groupStats) {
                    ++groupStats->layerOrFlagCulledSkinnedMeshInstances;
                }
                continue;
            }

            const SkinnedMeshResource& mesh = g_skinnedMeshes[instance.mesh.id];
            const Aabb worldBounds = mesh.hasBounds ? transformAabb(mesh.localBounds, instance.transform) : Aabb{};
            if (mesh.hasBounds && !intersects(frustum, worldBounds)) {
                ++stats.frustumCulledSkinnedMeshInstances;
                if (groupStats) {
                    ++groupStats->frustumCulledSkinnedMeshInstances;
                }
                continue;
            }
            if (mesh.hasBounds && exceedsMaxDrawDistance(
                instance.visibility,
                worldBounds,
                view.cameraPosition,
                view.enableDistanceCulling)) {
                ++stats.distanceCulledSkinnedMeshInstances;
                if (groupStats) {
                    ++groupStats->distanceCulledSkinnedMeshInstances;
                }
                continue;
            }
            ++stats.visibleSkinnedMeshInstances;
            if (groupStats) {
                ++groupStats->visibleSkinnedMeshInstances;
            }

            bool submittedInstance = false;
            for (uint32_t submeshIndex = 0; submeshIndex < mesh.submeshes.size(); ++submeshIndex) {
                const SubmeshResource& submesh = mesh.submeshes[submeshIndex];
                if (!bgfx::isValid(submesh.vertexBuffer) ||
                    !bgfx::isValid(submesh.indexBuffer) ||
                    !isValidMaterial(submesh.material)) {
                    continue;
                }

                const MaterialHandle materialHandle = submesh.material;
                const MaterialResource& material = g_materials[materialHandle.id];
                const MaterialRenderPass pass = renderPassFor(material.alphaMode);
                const glm::vec3 sortCenter = mesh.hasBounds ? centerOf(worldBounds) : glm::vec3{instance.transform[3]};
                const float cameraDistanceSquared = glm::dot(sortCenter - view.cameraPosition, sortCenter - view.cameraPosition);
                VisibleSkinnedMeshDrawItem item{
                    SkinnedMeshInstanceHandle{static_cast<uint32_t>(&instance - g_skinnedInstances.data())},
                    instance.mesh,
                    submeshIndex,
                    materialHandle,
                    instance.renderGroup,
                    instance.transform,
                    worldBounds,
                    pass,
                    cameraDistanceSquared,
                };
                switch (pass) {
                    case MaterialRenderPass::AlphaMask:
                        alphaMaskSkinnedMeshDrawItems.push_back(item);
                        break;
                    case MaterialRenderPass::AlphaBlend:
                        alphaBlendSkinnedMeshDrawItems.push_back(item);
                        break;
                    case MaterialRenderPass::Opaque:
                    default:
                        opaqueSkinnedMeshDrawItems.push_back(item);
                        break;
                }
                submittedInstance = true;
            }

            if (submittedInstance) {
                ++stats.submittedSkinnedMeshInstances;
                if (groupStats) {
                    ++groupStats->submittedSkinnedMeshInstances;
                }
            }
        }

        sortDepthWritingDrawItems(opaqueMeshDrawItems);
        sortDepthWritingDrawItems(alphaMaskMeshDrawItems);
        sortAlphaBlendDrawItems(alphaBlendMeshDrawItems);
        sortDepthWritingDrawItems(opaqueSkinnedMeshDrawItems);
        sortDepthWritingDrawItems(alphaMaskSkinnedMeshDrawItems);
        sortAlphaBlendDrawItems(alphaBlendSkinnedMeshDrawItems);

        addVisiblePassStats(stats, MaterialRenderPass::Opaque, static_cast<uint32_t>(opaqueMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::AlphaMask, static_cast<uint32_t>(alphaMaskMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::AlphaBlend, static_cast<uint32_t>(alphaBlendMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::Opaque, static_cast<uint32_t>(opaqueSkinnedMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::AlphaMask, static_cast<uint32_t>(alphaMaskSkinnedMeshDrawItems.size()));
        addVisiblePassStats(stats, MaterialRenderPass::AlphaBlend, static_cast<uint32_t>(alphaBlendSkinnedMeshDrawItems.size()));
        stats.visibleSkinnedMeshDrawItems = static_cast<uint32_t>(
            opaqueSkinnedMeshDrawItems.size() +
            alphaMaskSkinnedMeshDrawItems.size() +
            alphaBlendSkinnedMeshDrawItems.size());
        stats.visibleMeshDrawItems = stats.visibleOpaqueMeshDrawItems +
            stats.visibleAlphaMaskMeshDrawItems +
            stats.visibleAlphaBlendMeshDrawItems;

        submitMeshDrawItems(opaqueMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitMeshDrawItems(alphaMaskMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitMeshDrawItems(alphaBlendMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitSkinnedMeshDrawItems(opaqueSkinnedMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitSkinnedMeshDrawItems(alphaMaskSkinnedMeshDrawItems, sceneUniforms, view.viewId, stats);
        submitSkinnedMeshDrawItems(alphaBlendSkinnedMeshDrawItems, sceneUniforms, view.viewId, stats);

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
