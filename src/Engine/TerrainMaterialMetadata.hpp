#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/AssetRegistry.hpp"
#include "Engine/TerrainDataset.hpp"

namespace Engine {
    struct TerrainMaterialLayerId {
        uint64_t value = 0;
    };

    struct TerrainMaterialRuleId {
        uint64_t value = 0;
    };

    enum class TerrainMaterialTextureRole {
        BaseColor,
        Normal,
        Metallic,
        Roughness,
        MetallicRoughness,
        Occlusion,
        Emissive,
        Mask,
    };

    enum class TerrainMaterialTextureColorSpace {
        Linear,
        Srgb,
    };

    enum class TerrainMaterialTextureWrap {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    enum class TerrainMaterialTextureFilter {
        Nearest,
        Linear,
    };

    enum class TerrainMaterialProjectionMode {
        Uv,
        WorldPlanar,
        Triplanar,
    };

    struct TerrainMaterialTextureSlot {
        TerrainMaterialTextureRole role = TerrainMaterialTextureRole::BaseColor;
        std::filesystem::path sourcePath;
        AssetId assetId;
        TerrainMaterialTextureColorSpace colorSpace = TerrainMaterialTextureColorSpace::Linear;
        TerrainMaterialTextureWrap wrapU = TerrainMaterialTextureWrap::Repeat;
        TerrainMaterialTextureWrap wrapV = TerrainMaterialTextureWrap::Repeat;
        TerrainMaterialTextureFilter minFilter = TerrainMaterialTextureFilter::Linear;
        TerrainMaterialTextureFilter magFilter = TerrainMaterialTextureFilter::Linear;
    };

    struct TerrainMaterialLayer {
        TerrainMaterialLayerId id;
        std::string debugName;
        std::vector<TerrainMaterialTextureSlot> textures;
        glm::vec4 baseColorFactor{1.0f};
        float normalScale = 1.0f;
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float occlusionStrength = 1.0f;
        glm::vec3 emissiveFactor{0.0f};
        glm::vec2 tilingScale{1.0f};
        TerrainMaterialProjectionMode projection = TerrainMaterialProjectionMode::WorldPlanar;
        bool fallback = false;
    };

    struct TerrainMaterialRange {
        float min = 0.0f;
        float max = 0.0f;
    };

    struct TerrainMaterialRule {
        TerrainMaterialRuleId id;
        TerrainMaterialLayerId layer;
        int32_t priority = 0;
        float weight = 1.0f;
        float blendFalloff = 0.0f;
        std::optional<TerrainMaterialRange> heightRange;
        std::optional<TerrainMaterialRange> slopeRangeDegrees;
        std::optional<TerrainMaterialRange> worldXRange;
        std::optional<TerrainMaterialRange> worldZRange;
        std::string biomeTag;
        std::string debugName;
    };

    struct TerrainMaterialSet {
        AssetId id;
        std::string debugName;
        std::vector<TerrainMaterialLayer> layers;
        std::vector<TerrainMaterialRule> rules;
    };

    struct TerrainMaterialValidationDiagnostics {
        bool valid = false;
        uint32_t layerCount = 0;
        uint32_t ruleCount = 0;
        uint32_t fallbackLayerCount = 0;
        uint32_t textureReferenceCount = 0;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    struct TerrainMaterialDependencyResult {
        uint32_t registeredTextureCount = 0;
        uint32_t dependencyCount = 0;
        std::vector<std::string> warnings;
    };

    struct TerrainMaterialEvaluationSettings {
        uint32_t sampleResolution = 0;
        uint32_t maxSampleResolution = 33;
        bool keepSamples = false;
    };

    struct TerrainMaterialCoverage {
        TerrainMaterialLayerId layer;
        float weight = 0.0f;
        uint32_t matchedSampleCount = 0;
    };

    struct TerrainMaterialSample {
        glm::vec3 position{0.0f};
        float slopeDegrees = 0.0f;
        std::vector<TerrainMaterialCoverage> weights;
    };

    struct TerrainMaterialEvaluationResult {
        bool success = false;
        TerrainSourceChunkId chunkId;
        TerrainSourceChunkCoord coord;
        uint32_t sampleResolution = 0;
        uint32_t sampleCount = 0;
        std::vector<TerrainMaterialCoverage> coverage;
        std::vector<TerrainMaterialSample> samples;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] constexpr bool isValid(TerrainMaterialLayerId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool isValid(TerrainMaterialRuleId id)
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool operator==(TerrainMaterialLayerId lhs, TerrainMaterialLayerId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(TerrainMaterialLayerId lhs, TerrainMaterialLayerId rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(TerrainMaterialRuleId lhs, TerrainMaterialRuleId rhs)
    {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool operator!=(TerrainMaterialRuleId lhs, TerrainMaterialRuleId rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] TerrainMaterialValidationDiagnostics validateTerrainMaterialSet(
        const TerrainMaterialSet& materialSet);
    [[nodiscard]] TerrainMaterialDependencyResult registerTerrainMaterialSetDependencies(
        AssetRegistry& registry,
        AssetHandle materialSetAsset,
        const TerrainMaterialSet& materialSet);
    [[nodiscard]] TerrainMaterialEvaluationResult evaluateTerrainMaterialCoverage(
        const TerrainChunkData& chunk,
        const TerrainMaterialSet& materialSet,
        const TerrainMaterialEvaluationSettings& settings = {});
    [[nodiscard]] TerrainMaterialEvaluationResult evaluateTerrainMaterialCoverage(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        const TerrainMaterialSet& materialSet,
        const TerrainMaterialEvaluationSettings& settings = {});
}
