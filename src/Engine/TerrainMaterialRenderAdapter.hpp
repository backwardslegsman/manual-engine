#pragma once

#include <string>
#include <vector>

#include "Engine/AssetCache.hpp"
#include "Engine/TerrainMaterialMetadata.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct TerrainMaterialRenderBinding {
        Renderer::TerrainMaterialSetHandle materialSet;
        std::vector<CachedTexture> textures;
    };

    struct TerrainMaterialRenderDiagnostics {
        bool success = false;
        uint32_t layerCount = 0;
        uint32_t ruleCount = 0;
        uint32_t truncatedLayerCount = 0;
        uint32_t truncatedRuleCount = 0;
        uint32_t missingTextureFallbackCount = 0;
        uint32_t unsupportedTextureRoleCount = 0;
        uint32_t invalidMetadataErrorCount = 0;
        std::vector<std::string> warnings;
    };

    struct TerrainMaterialRenderCreateResult {
        bool success = false;
        TerrainMaterialRenderBinding binding;
        Renderer::TerrainMaterialSetDescriptor descriptor;
        TerrainMaterialRenderDiagnostics diagnostics;
    };

    [[nodiscard]] Renderer::TerrainMaterialSetDescriptor terrainMaterialSetDescriptorFromMetadata(
        AssetCache& cache,
        const TerrainMaterialSet& materialSet,
        TerrainMaterialRenderBinding& binding,
        TerrainMaterialRenderDiagnostics& diagnostics);

    [[nodiscard]] TerrainMaterialRenderCreateResult createTerrainMaterialResources(
        AssetCache& cache,
        const TerrainMaterialSet& materialSet);

    void releaseTerrainMaterialResources(
        AssetCache& cache,
        TerrainMaterialRenderBinding& binding);
}
