#include "Engine/TerrainMaterialMetadata.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <utility>

namespace Engine {
    namespace {
        [[nodiscard]] bool validRange(const TerrainMaterialRange& range)
        {
            return std::isfinite(range.min) && std::isfinite(range.max) && range.min <= range.max;
        }

        [[nodiscard]] bool validHeights(const TerrainChunkData& chunk)
        {
            return chunk.resolution >= 2 &&
                chunk.size > 0.0f &&
                chunk.heights.size() == static_cast<size_t>(chunk.resolution) * chunk.resolution &&
                std::ranges::all_of(chunk.heights, [](float height) { return std::isfinite(height); });
        }

        [[nodiscard]] float heightAt(const TerrainChunkData& chunk, uint32_t x, uint32_t z)
        {
            return chunk.heights[static_cast<size_t>(z) * chunk.resolution + x];
        }

        [[nodiscard]] float sampleHeight(const TerrainChunkData& chunk, float worldX, float worldZ)
        {
            const float localX = std::clamp(worldX - chunk.origin.x, 0.0f, chunk.size);
            const float localZ = std::clamp(worldZ - chunk.origin.z, 0.0f, chunk.size);
            const float normalizedX = localX / chunk.size * static_cast<float>(chunk.resolution - 1u);
            const float normalizedZ = localZ / chunk.size * static_cast<float>(chunk.resolution - 1u);
            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), chunk.resolution - 1u);
            const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), chunk.resolution - 1u);
            const uint32_t x1 = std::min(x0 + 1u, chunk.resolution - 1u);
            const uint32_t z1 = std::min(z0 + 1u, chunk.resolution - 1u);
            const float tx = normalizedX - static_cast<float>(x0);
            const float tz = normalizedZ - static_cast<float>(z0);
            const float h00 = heightAt(chunk, x0, z0);
            const float h10 = heightAt(chunk, x1, z0);
            const float h01 = heightAt(chunk, x0, z1);
            const float h11 = heightAt(chunk, x1, z1);
            const float hx0 = h00 + (h10 - h00) * tx;
            const float hx1 = h01 + (h11 - h01) * tx;
            return hx0 + (hx1 - hx0) * tz;
        }

        [[nodiscard]] float sampleSlopeDegrees(const TerrainChunkData& chunk, float worldX, float worldZ)
        {
            const float spacing = chunk.size / static_cast<float>(chunk.resolution - 1u);
            const float left = sampleHeight(chunk, worldX - spacing, worldZ);
            const float right = sampleHeight(chunk, worldX + spacing, worldZ);
            const float down = sampleHeight(chunk, worldX, worldZ - spacing);
            const float up = sampleHeight(chunk, worldX, worldZ + spacing);
            const glm::vec3 normal = glm::normalize(glm::vec3{left - right, 2.0f * spacing, down - up});
            return glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)));
        }

        [[nodiscard]] float rangeWeight(float value, const std::optional<TerrainMaterialRange>& range, float falloff)
        {
            if (!range.has_value()) {
                return 1.0f;
            }
            if (value >= range->min && value <= range->max) {
                return 1.0f;
            }
            if (falloff <= 0.0f) {
                return 0.0f;
            }
            const float distance = value < range->min ? range->min - value : value - range->max;
            if (distance >= falloff) {
                return 0.0f;
            }
            return 1.0f - distance / falloff;
        }

        [[nodiscard]] bool containsLayer(const TerrainMaterialSet& materialSet, TerrainMaterialLayerId layerId)
        {
            return std::ranges::any_of(materialSet.layers, [&](const TerrainMaterialLayer& layer) {
                return layer.id == layerId;
            });
        }

        [[nodiscard]] const TerrainMaterialLayer* fallbackLayer(const TerrainMaterialSet& materialSet)
        {
            const auto found = std::ranges::find_if(materialSet.layers, [](const TerrainMaterialLayer& layer) {
                return layer.fallback;
            });
            return found == materialSet.layers.end() ? nullptr : &*found;
        }

        void addCoverageWeight(
            std::vector<TerrainMaterialCoverage>& coverage,
            TerrainMaterialLayerId layer,
            float weight,
            bool matched)
        {
            if (weight <= 0.0f || !std::isfinite(weight)) {
                return;
            }
            auto found = std::ranges::find_if(coverage, [&](const TerrainMaterialCoverage& entry) {
                return entry.layer == layer;
            });
            if (found == coverage.end()) {
                coverage.push_back({layer, weight, matched ? 1u : 0u});
            } else {
                found->weight += weight;
                found->matchedSampleCount += matched ? 1u : 0u;
            }
        }

        [[nodiscard]] std::vector<const TerrainMaterialRule*> sortedRules(const TerrainMaterialSet& materialSet)
        {
            std::vector<const TerrainMaterialRule*> rules;
            rules.reserve(materialSet.rules.size());
            for (const TerrainMaterialRule& rule : materialSet.rules) {
                rules.push_back(&rule);
            }
            std::stable_sort(rules.begin(), rules.end(), [](const TerrainMaterialRule* lhs, const TerrainMaterialRule* rhs) {
                return lhs->priority > rhs->priority;
            });
            return rules;
        }

        [[nodiscard]] uint32_t evaluationResolution(
            const TerrainChunkData& chunk,
            const TerrainMaterialEvaluationSettings& settings)
        {
            if (settings.sampleResolution > 0) {
                return std::max(settings.sampleResolution, 1u);
            }
            return std::max(1u, std::min(chunk.resolution, std::max(settings.maxSampleResolution, 1u)));
        }

        [[nodiscard]] std::filesystem::path texturePathForRegistration(const TerrainMaterialTextureSlot& texture)
        {
            if (!texture.sourcePath.empty()) {
                return texture.sourcePath;
            }
            if (isValid(texture.assetId)) {
                return std::filesystem::path{"generated/terrain_material_texture_"} /
                    (std::to_string(texture.assetId.value) + ".texture");
            }
            return {};
        }
    }

    TerrainMaterialValidationDiagnostics validateTerrainMaterialSet(const TerrainMaterialSet& materialSet)
    {
        TerrainMaterialValidationDiagnostics diagnostics;
        diagnostics.layerCount = static_cast<uint32_t>(materialSet.layers.size());
        diagnostics.ruleCount = static_cast<uint32_t>(materialSet.rules.size());

        if (materialSet.layers.empty()) {
            diagnostics.errors.push_back("terrain material set has no layers");
        }
        if (materialSet.rules.empty()) {
            diagnostics.errors.push_back("terrain material set has no rules");
        }

        std::unordered_set<uint64_t> layerIds;
        for (const TerrainMaterialLayer& layer : materialSet.layers) {
            if (!isValid(layer.id)) {
                diagnostics.errors.push_back("terrain material layer has invalid ID");
            } else if (!layerIds.insert(layer.id.value).second) {
                diagnostics.errors.push_back("terrain material layer ID is duplicated");
            }
            diagnostics.fallbackLayerCount += layer.fallback ? 1u : 0u;
            diagnostics.textureReferenceCount += static_cast<uint32_t>(layer.textures.size());
            if (layer.tilingScale.x <= 0.0f || layer.tilingScale.y <= 0.0f) {
                diagnostics.errors.push_back("terrain material layer has invalid tiling scale");
            }
        }
        if (diagnostics.fallbackLayerCount == 0) {
            diagnostics.errors.push_back("terrain material set has no fallback layer");
        } else if (diagnostics.fallbackLayerCount > 1) {
            diagnostics.errors.push_back("terrain material set has multiple fallback layers");
        }

        std::unordered_set<uint64_t> ruleIds;
        for (const TerrainMaterialRule& rule : materialSet.rules) {
            if (!isValid(rule.id)) {
                diagnostics.errors.push_back("terrain material rule has invalid ID");
            } else if (!ruleIds.insert(rule.id.value).second) {
                diagnostics.errors.push_back("terrain material rule ID is duplicated");
            }
            if (!containsLayer(materialSet, rule.layer)) {
                diagnostics.errors.push_back("terrain material rule references a missing layer");
            }
            if (!std::isfinite(rule.weight) || rule.weight < 0.0f) {
                diagnostics.errors.push_back("terrain material rule has invalid weight");
            }
            if (!std::isfinite(rule.blendFalloff) || rule.blendFalloff < 0.0f) {
                diagnostics.errors.push_back("terrain material rule has invalid blend falloff");
            }
            if ((rule.heightRange && !validRange(*rule.heightRange)) ||
                (rule.slopeRangeDegrees && !validRange(*rule.slopeRangeDegrees)) ||
                (rule.worldXRange && !validRange(*rule.worldXRange)) ||
                (rule.worldZRange && !validRange(*rule.worldZRange))) {
                diagnostics.errors.push_back("terrain material rule has invalid range");
            }
        }

        diagnostics.valid = diagnostics.errors.empty();
        return diagnostics;
    }

    TerrainMaterialDependencyResult registerTerrainMaterialSetDependencies(
        AssetRegistry& registry,
        AssetHandle materialSetAsset,
        const TerrainMaterialSet& materialSet)
    {
        TerrainMaterialDependencyResult result;
        if (!registry.contains(materialSetAsset)) {
            result.warnings.push_back("terrain material set asset handle is invalid");
            return result;
        }

        std::vector<std::filesystem::path> registeredPaths;
        for (const TerrainMaterialLayer& layer : materialSet.layers) {
            for (const TerrainMaterialTextureSlot& texture : layer.textures) {
                const std::filesystem::path path = texturePathForRegistration(texture);
                if (path.empty()) {
                    result.warnings.push_back("terrain material texture slot has no source path or asset ID");
                    continue;
                }
                const std::string normalized = path.lexically_normal().generic_string();
                if (std::ranges::find_if(registeredPaths, [&](const std::filesystem::path& existing) {
                    return existing.lexically_normal().generic_string() == normalized;
                }) != registeredPaths.end()) {
                    continue;
                }
                registeredPaths.push_back(path);

                AssetDescriptor descriptor;
                descriptor.sourcePath = path;
                descriptor.type = AssetType::Texture;
                descriptor.settings = {"terrain_material_texture", "1", "metadata"};
                descriptor.explicitId = texture.assetId;
                const AssetHandle textureAsset = registry.registerAsset(descriptor);
                if (!registry.contains(textureAsset)) {
                    result.warnings.push_back("terrain material texture dependency failed to register");
                    continue;
                }
                ++result.registeredTextureCount;
                if (registry.addDependency(materialSetAsset, textureAsset)) {
                    ++result.dependencyCount;
                }
            }
        }
        return result;
    }

    TerrainMaterialEvaluationResult evaluateTerrainMaterialCoverage(
        const TerrainChunkData& chunk,
        const TerrainMaterialSet& materialSet,
        const TerrainMaterialEvaluationSettings& settings)
    {
        TerrainMaterialEvaluationResult result;
        result.chunkId = chunk.id;
        result.coord = chunk.coord;

        const TerrainMaterialValidationDiagnostics validation = validateTerrainMaterialSet(materialSet);
        if (!validation.valid) {
            result.warnings = validation.errors;
            return result;
        }
        if (!validHeights(chunk)) {
            result.warnings.push_back("terrain chunk data is invalid");
            return result;
        }

        const TerrainMaterialLayer* fallback = fallbackLayer(materialSet);
        if (!fallback) {
            result.warnings.push_back("terrain material set has no fallback layer");
            return result;
        }

        const std::vector<const TerrainMaterialRule*> rules = sortedRules(materialSet);
        const uint32_t resolution = evaluationResolution(chunk, settings);
        result.sampleResolution = resolution;
        result.sampleCount = resolution * resolution;

        std::vector<TerrainMaterialCoverage> totalCoverage;
        const float step = resolution > 1 ? chunk.size / static_cast<float>(resolution - 1u) : 0.0f;
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = resolution > 1 ? chunk.origin.x + static_cast<float>(x) * step : chunk.origin.x + chunk.size * 0.5f;
                const float worldZ = resolution > 1 ? chunk.origin.z + static_cast<float>(z) * step : chunk.origin.z + chunk.size * 0.5f;
                const float height = sampleHeight(chunk, worldX, worldZ);
                const float slope = sampleSlopeDegrees(chunk, worldX, worldZ);

                std::vector<TerrainMaterialCoverage> sampleWeights;
                for (const TerrainMaterialRule* rule : rules) {
                    float ruleWeight = rule->weight;
                    ruleWeight *= rangeWeight(height, rule->heightRange, rule->blendFalloff);
                    ruleWeight *= rangeWeight(slope, rule->slopeRangeDegrees, rule->blendFalloff);
                    ruleWeight *= rangeWeight(worldX, rule->worldXRange, rule->blendFalloff);
                    ruleWeight *= rangeWeight(worldZ, rule->worldZRange, rule->blendFalloff);
                    addCoverageWeight(sampleWeights, rule->layer, ruleWeight, true);
                }
                if (sampleWeights.empty()) {
                    addCoverageWeight(sampleWeights, fallback->id, 1.0f, true);
                }

                const float totalWeight = std::accumulate(
                    sampleWeights.begin(),
                    sampleWeights.end(),
                    0.0f,
                    [](float sum, const TerrainMaterialCoverage& coverage) { return sum + coverage.weight; });
                if (totalWeight > 0.0f) {
                    for (TerrainMaterialCoverage& coverage : sampleWeights) {
                        coverage.weight /= totalWeight;
                    }
                }

                for (const TerrainMaterialCoverage& coverage : sampleWeights) {
                    addCoverageWeight(totalCoverage, coverage.layer, coverage.weight, coverage.matchedSampleCount > 0);
                }

                if (settings.keepSamples) {
                    TerrainMaterialSample sample;
                    sample.position = {worldX, height, worldZ};
                    sample.slopeDegrees = slope;
                    sample.weights = std::move(sampleWeights);
                    result.samples.push_back(std::move(sample));
                }
            }
        }

        for (TerrainMaterialCoverage& coverage : totalCoverage) {
            coverage.weight /= static_cast<float>(std::max(result.sampleCount, 1u));
        }
        std::stable_sort(totalCoverage.begin(), totalCoverage.end(), [](const TerrainMaterialCoverage& lhs, const TerrainMaterialCoverage& rhs) {
            return lhs.layer.value < rhs.layer.value;
        });
        result.coverage = std::move(totalCoverage);
        result.success = true;
        return result;
    }

    TerrainMaterialEvaluationResult evaluateTerrainMaterialCoverage(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        const TerrainMaterialSet& materialSet,
        const TerrainMaterialEvaluationSettings& settings)
    {
        const std::optional<TerrainChunkData> data = dataset.chunk(chunk);
        if (!data.has_value()) {
            TerrainMaterialEvaluationResult result;
            result.warnings.push_back("terrain dataset chunk handle is invalid");
            return result;
        }
        return evaluateTerrainMaterialCoverage(*data, materialSet, settings);
    }
}
