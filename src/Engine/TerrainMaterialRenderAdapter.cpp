#include "Engine/TerrainMaterialRenderAdapter.hpp"

#include <algorithm>

namespace Engine {
    namespace {
        Renderer::TextureColorSpace toRendererColorSpace(TerrainMaterialTextureColorSpace value)
        {
            switch (value) {
                case TerrainMaterialTextureColorSpace::Srgb:
                    return Renderer::TextureColorSpace::Srgb;
                case TerrainMaterialTextureColorSpace::Linear:
                default:
                    return Renderer::TextureColorSpace::Linear;
            }
        }

        Renderer::TextureWrap toRendererWrap(TerrainMaterialTextureWrap value)
        {
            switch (value) {
                case TerrainMaterialTextureWrap::ClampToEdge:
                    return Renderer::TextureWrap::ClampToEdge;
                case TerrainMaterialTextureWrap::MirroredRepeat:
                    return Renderer::TextureWrap::MirroredRepeat;
                case TerrainMaterialTextureWrap::Repeat:
                default:
                    return Renderer::TextureWrap::Repeat;
            }
        }

        Renderer::TextureFilter toRendererFilter(TerrainMaterialTextureFilter value)
        {
            switch (value) {
                case TerrainMaterialTextureFilter::Nearest:
                    return Renderer::TextureFilter::Nearest;
                case TerrainMaterialTextureFilter::Linear:
                default:
                    return Renderer::TextureFilter::Linear;
            }
        }

        Renderer::TextureDescriptor textureDescriptorFor(const TerrainMaterialTextureSlot& slot)
        {
            Renderer::TextureDescriptor descriptor;
            descriptor.colorSpace = toRendererColorSpace(slot.colorSpace);
            descriptor.wrapU = toRendererWrap(slot.wrapU);
            descriptor.wrapV = toRendererWrap(slot.wrapV);
            descriptor.minFilter = toRendererFilter(slot.minFilter);
            descriptor.magFilter = toRendererFilter(slot.magFilter);
            descriptor.generateMips = true;
            descriptor.debugName = slot.sourcePath.string();
            return descriptor;
        }

        CachedTexture acquireFallbackTexture(
            AssetCache& cache,
            TerrainMaterialTextureRole role)
        {
            switch (role) {
                case TerrainMaterialTextureRole::Normal:
                    return cache.acquireSolidTexture(128, 128, 255, 255);
                case TerrainMaterialTextureRole::Metallic:
                case TerrainMaterialTextureRole::Occlusion:
                case TerrainMaterialTextureRole::Emissive:
                case TerrainMaterialTextureRole::Mask:
                    return cache.acquireSolidTexture(0, 0, 0, 255);
                case TerrainMaterialTextureRole::Roughness:
                case TerrainMaterialTextureRole::MetallicRoughness:
                case TerrainMaterialTextureRole::BaseColor:
                default:
                    return cache.acquireSolidTexture(255, 255, 255, 255);
            }
        }

        Renderer::TextureHandle acquireTextureForSlot(
            AssetCache& cache,
            const TerrainMaterialTextureSlot& slot,
            TerrainMaterialRenderBinding& binding,
            TerrainMaterialRenderDiagnostics& diagnostics)
        {
            CachedTexture texture;
            if (!slot.sourcePath.empty()) {
                texture = cache.acquireTexture(slot.sourcePath, textureDescriptorFor(slot));
            }
            if (texture.handle.id == UINT32_MAX) {
                texture = acquireFallbackTexture(cache, slot.role);
                ++diagnostics.missingTextureFallbackCount;
                diagnostics.warnings.push_back("Terrain material texture '" + slot.sourcePath.string() + "' used fallback texture.");
            }
            if (texture.handle.id != UINT32_MAX) {
                binding.textures.push_back(texture);
            }
            return texture.handle;
        }

        float minOrZero(const std::optional<TerrainMaterialRange>& range)
        {
            return range ? range->min : 0.0f;
        }

        float maxOrZero(const std::optional<TerrainMaterialRange>& range)
        {
            return range ? range->max : 0.0f;
        }
    }

    Renderer::TerrainMaterialSetDescriptor terrainMaterialSetDescriptorFromMetadata(
        AssetCache& cache,
        const TerrainMaterialSet& materialSet,
        TerrainMaterialRenderBinding& binding,
        TerrainMaterialRenderDiagnostics& diagnostics)
    {
        diagnostics = {};
        const TerrainMaterialValidationDiagnostics validation = validateTerrainMaterialSet(materialSet);
        diagnostics.invalidMetadataErrorCount = static_cast<uint32_t>(validation.errors.size());
        for (const std::string& warning : validation.warnings) {
            diagnostics.warnings.push_back(warning);
        }
        for (const std::string& error : validation.errors) {
            diagnostics.warnings.push_back(error);
        }

        Renderer::TerrainMaterialSetDescriptor descriptor;
        descriptor.name = materialSet.debugName;
        diagnostics.truncatedLayerCount =
            materialSet.layers.size() > Renderer::MaxTerrainMaterialLayers
                ? static_cast<uint32_t>(materialSet.layers.size() - Renderer::MaxTerrainMaterialLayers)
                : 0u;
        diagnostics.truncatedRuleCount =
            materialSet.rules.size() > Renderer::MaxTerrainMaterialRules
                ? static_cast<uint32_t>(materialSet.rules.size() - Renderer::MaxTerrainMaterialRules)
                : 0u;

        const uint32_t layerCount = std::min<uint32_t>(
            static_cast<uint32_t>(materialSet.layers.size()),
            Renderer::MaxTerrainMaterialLayers);
        descriptor.layers.reserve(layerCount);
        for (uint32_t index = 0; index < layerCount; ++index) {
            const TerrainMaterialLayer& layer = materialSet.layers[index];
            if (layer.fallback) {
                descriptor.fallbackLayerIndex = index;
            }

            Renderer::TerrainMaterialLayerDescriptor renderLayer;
            renderLayer.name = layer.debugName;
            renderLayer.baseColorFactor = layer.baseColorFactor;
            renderLayer.normalScale = layer.normalScale;
            renderLayer.metallicFactor = layer.metallicFactor;
            renderLayer.roughnessFactor = layer.roughnessFactor;
            renderLayer.tilingScale = layer.tilingScale;
            if (layer.projection != TerrainMaterialProjectionMode::WorldPlanar &&
                layer.projection != TerrainMaterialProjectionMode::Uv) {
                diagnostics.warnings.push_back("Terrain material layer '" + layer.debugName + "' uses unsupported projection metadata.");
            }

            for (const TerrainMaterialTextureSlot& slot : layer.textures) {
                switch (slot.role) {
                    case TerrainMaterialTextureRole::BaseColor:
                        renderLayer.baseColorTexture = acquireTextureForSlot(cache, slot, binding, diagnostics);
                        break;
                    case TerrainMaterialTextureRole::Normal:
                        renderLayer.normalTexture = acquireTextureForSlot(cache, slot, binding, diagnostics);
                        break;
                    case TerrainMaterialTextureRole::MetallicRoughness:
                        renderLayer.metallicRoughnessTexture = acquireTextureForSlot(cache, slot, binding, diagnostics);
                        break;
                    case TerrainMaterialTextureRole::Metallic:
                    case TerrainMaterialTextureRole::Roughness:
                    case TerrainMaterialTextureRole::Occlusion:
                    case TerrainMaterialTextureRole::Emissive:
                    case TerrainMaterialTextureRole::Mask:
                        ++diagnostics.unsupportedTextureRoleCount;
                        diagnostics.warnings.push_back("Terrain material layer '" + layer.debugName + "' has unsupported texture role metadata.");
                        break;
                }
            }

            if (renderLayer.baseColorTexture.id == UINT32_MAX) {
                CachedTexture fallback = acquireFallbackTexture(cache, TerrainMaterialTextureRole::BaseColor);
                renderLayer.baseColorTexture = fallback.handle;
                binding.textures.push_back(fallback);
                ++diagnostics.missingTextureFallbackCount;
            }
            if (renderLayer.normalTexture.id == UINT32_MAX) {
                CachedTexture fallback = acquireFallbackTexture(cache, TerrainMaterialTextureRole::Normal);
                renderLayer.normalTexture = fallback.handle;
                binding.textures.push_back(fallback);
                ++diagnostics.missingTextureFallbackCount;
            }
            if (renderLayer.metallicRoughnessTexture.id == UINT32_MAX) {
                CachedTexture fallback = acquireFallbackTexture(cache, TerrainMaterialTextureRole::MetallicRoughness);
                renderLayer.metallicRoughnessTexture = fallback.handle;
                binding.textures.push_back(fallback);
                ++diagnostics.missingTextureFallbackCount;
            }

            descriptor.layers.push_back(renderLayer);
        }

        descriptor.rules.reserve(std::min<uint32_t>(
            static_cast<uint32_t>(materialSet.rules.size()),
            Renderer::MaxTerrainMaterialRules));
        for (const TerrainMaterialRule& rule : materialSet.rules) {
            if (descriptor.rules.size() >= Renderer::MaxTerrainMaterialRules) {
                break;
            }
            auto layer = std::find_if(
                materialSet.layers.begin(),
                materialSet.layers.begin() + static_cast<std::ptrdiff_t>(layerCount),
                [rule](const TerrainMaterialLayer& candidate) {
                    return candidate.id == rule.layer;
                });
            if (layer == materialSet.layers.begin() + static_cast<std::ptrdiff_t>(layerCount)) {
                continue;
            }

            Renderer::TerrainMaterialRuleDescriptor renderRule;
            renderRule.name = rule.debugName;
            renderRule.layerIndex = static_cast<uint32_t>(std::distance(materialSet.layers.begin(), layer));
            renderRule.priority = rule.priority;
            renderRule.weight = rule.weight;
            renderRule.blendFalloff = rule.blendFalloff;
            renderRule.useHeightRange = rule.heightRange.has_value();
            renderRule.minHeight = minOrZero(rule.heightRange);
            renderRule.maxHeight = maxOrZero(rule.heightRange);
            renderRule.useSlopeRange = rule.slopeRangeDegrees.has_value();
            renderRule.minSlopeDegrees = minOrZero(rule.slopeRangeDegrees);
            renderRule.maxSlopeDegrees = maxOrZero(rule.slopeRangeDegrees);
            renderRule.useWorldXRange = rule.worldXRange.has_value();
            renderRule.minWorldX = minOrZero(rule.worldXRange);
            renderRule.maxWorldX = maxOrZero(rule.worldXRange);
            renderRule.useWorldZRange = rule.worldZRange.has_value();
            renderRule.minWorldZ = minOrZero(rule.worldZRange);
            renderRule.maxWorldZ = maxOrZero(rule.worldZRange);
            descriptor.rules.push_back(renderRule);
        }

        diagnostics.layerCount = static_cast<uint32_t>(descriptor.layers.size());
        diagnostics.ruleCount = static_cast<uint32_t>(descriptor.rules.size());
        diagnostics.success = diagnostics.layerCount > 0 && diagnostics.invalidMetadataErrorCount == 0;
        return descriptor;
    }

    TerrainMaterialRenderCreateResult createTerrainMaterialResources(
        AssetCache& cache,
        const TerrainMaterialSet& materialSet)
    {
        TerrainMaterialRenderCreateResult result;
        result.descriptor = terrainMaterialSetDescriptorFromMetadata(
            cache,
            materialSet,
            result.binding,
            result.diagnostics);
        if (!result.diagnostics.success) {
            releaseTerrainMaterialResources(cache, result.binding);
            return result;
        }

        result.binding.materialSet = Renderer::createTerrainMaterialSet(result.descriptor);
        const Renderer::TerrainMaterialSetDiagnostics rendererDiagnostics =
            Renderer::terrainMaterialSetDiagnostics(result.binding.materialSet);
        result.diagnostics.truncatedLayerCount += rendererDiagnostics.truncatedLayerCount;
        result.diagnostics.truncatedRuleCount += rendererDiagnostics.truncatedRuleCount;
        result.diagnostics.missingTextureFallbackCount += rendererDiagnostics.missingTextureFallbackCount;
        result.success = rendererDiagnostics.valid;
        result.diagnostics.success = result.success;
        if (!result.success) {
            releaseTerrainMaterialResources(cache, result.binding);
        }
        return result;
    }

    void releaseTerrainMaterialResources(
        AssetCache& cache,
        TerrainMaterialRenderBinding& binding)
    {
        Renderer::destroyTerrainMaterialSet(binding.materialSet);
        binding.materialSet = {};
        for (CachedTexture texture : binding.textures) {
            cache.release(texture);
        }
        binding.textures.clear();
    }
}
