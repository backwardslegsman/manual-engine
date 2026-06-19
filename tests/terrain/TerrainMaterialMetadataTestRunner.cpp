#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/AssetRegistry.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainMaterialMetadata.hpp"

namespace {
    struct TestFailure {
        std::string testName;
        std::string message;
    };

    struct TestContext {
        std::string name;
        std::vector<TestFailure>& failures;

        void expect(bool condition, std::string message)
        {
            if (!condition) {
                failures.push_back({name, std::move(message)});
            }
        }
    };

    bool near(float lhs, float rhs, float epsilon = 0.001f)
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    Engine::TerrainMaterialLayer layer(uint64_t id, std::string name, bool fallback = false)
    {
        Engine::TerrainMaterialLayer result;
        result.id = {id};
        result.debugName = std::move(name);
        result.fallback = fallback;
        return result;
    }

    Engine::TerrainMaterialRule rule(
        uint64_t id,
        uint64_t layerId,
        std::optional<Engine::TerrainMaterialRange> height = std::nullopt,
        std::optional<Engine::TerrainMaterialRange> slope = std::nullopt,
        std::optional<Engine::TerrainMaterialRange> worldX = std::nullopt,
        int32_t priority = 0,
        float weight = 1.0f,
        float falloff = 0.0f)
    {
        Engine::TerrainMaterialRule result;
        result.id = {id};
        result.layer = {layerId};
        result.priority = priority;
        result.weight = weight;
        result.blendFalloff = falloff;
        result.heightRange = height;
        result.slopeRangeDegrees = slope;
        result.worldXRange = worldX;
        return result;
    }

    Engine::TerrainMaterialSet basicSet()
    {
        Engine::TerrainMaterialSet set;
        set.id = {5001};
        set.debugName = "basic";
        set.layers = {
            layer(1, "grass", true),
            layer(2, "rock"),
            layer(3, "snow"),
        };
        set.rules = {
            rule(101, 2, Engine::TerrainMaterialRange{1.5f, 10.0f}),
        };
        return set;
    }

    Engine::TerrainChunkData chunk(std::vector<float> heights, uint32_t resolution = 3)
    {
        Engine::TerrainChunkData data;
        data.id = {{6001}, {0, 0}};
        data.coord = {0, 0};
        data.origin = {0.0f, 0.0f, 0.0f};
        data.size = 2.0f;
        data.resolution = resolution;
        data.heights = std::move(heights);
        data.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return data;
    }

    std::optional<Engine::TerrainMaterialCoverage> coverageFor(
        const Engine::TerrainMaterialEvaluationResult& result,
        uint64_t layerId)
    {
        for (const Engine::TerrainMaterialCoverage& coverage : result.coverage) {
            if (coverage.layer == Engine::TerrainMaterialLayerId{layerId}) {
                return coverage;
            }
        }
        return std::nullopt;
    }

    void validMaterialSetValidates(TestContext& ctx)
    {
        const Engine::TerrainMaterialValidationDiagnostics diagnostics =
            Engine::validateTerrainMaterialSet(basicSet());
        ctx.expect(diagnostics.valid, "valid material set failed validation");
        ctx.expect(diagnostics.layerCount == 3, "layer count was wrong");
        ctx.expect(diagnostics.ruleCount == 1, "rule count was wrong");
        ctx.expect(diagnostics.fallbackLayerCount == 1, "fallback layer count was wrong");
    }

    void invalidMaterialSetIsDiagnosed(TestContext& ctx)
    {
        Engine::TerrainMaterialSet set = basicSet();
        set.layers.push_back(layer(1, "duplicate"));
        set.layers[0].fallback = false;
        set.layers[1].tilingScale = {-1.0f, 1.0f};
        set.rules.push_back(rule(101, 99));
        set.rules.push_back(rule(102, 2, Engine::TerrainMaterialRange{3.0f, 1.0f}));
        set.rules.push_back(rule(103, 2, std::nullopt, std::nullopt, std::nullopt, 0, -1.0f));
        const Engine::TerrainMaterialValidationDiagnostics diagnostics =
            Engine::validateTerrainMaterialSet(set);
        ctx.expect(!diagnostics.valid, "invalid material set unexpectedly validated");
        ctx.expect(diagnostics.errors.size() >= 5, "invalid material set did not report enough errors");
    }

    void textureDependenciesRegisterAndDeduplicate(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        Engine::AssetDescriptor materialSetAsset;
        materialSetAsset.type = Engine::AssetType::TerrainMaterialSet;
        materialSetAsset.settings = {"terrain_material_set", "1", "test"};
        materialSetAsset.explicitId = {7001};
        const Engine::AssetHandle materialSetHandle = registry.registerAsset(materialSetAsset);

        Engine::TerrainMaterialSet set = basicSet();
        Engine::TerrainMaterialTextureSlot base;
        base.role = Engine::TerrainMaterialTextureRole::BaseColor;
        base.sourcePath = "assets/terrain/grass_base.png";
        base.colorSpace = Engine::TerrainMaterialTextureColorSpace::Srgb;
        Engine::TerrainMaterialTextureSlot normal;
        normal.role = Engine::TerrainMaterialTextureRole::Normal;
        normal.sourcePath = "assets/terrain/grass_base.png";
        Engine::TerrainMaterialTextureSlot roughness;
        roughness.role = Engine::TerrainMaterialTextureRole::Roughness;
        roughness.sourcePath = "assets/terrain/grass_roughness.png";
        set.layers[0].textures = {base, normal, roughness};

        const Engine::TerrainMaterialDependencyResult result =
            Engine::registerTerrainMaterialSetDependencies(registry, materialSetHandle, set);
        ctx.expect(result.registeredTextureCount == 2, "texture registration did not deduplicate paths");
        ctx.expect(result.dependencyCount == 2, "texture dependency count was wrong");
        ctx.expect(registry.dependencies(materialSetHandle).size() == 2, "registry dependency list size was wrong");
    }

    void flatTerrainUsesFallbackAndHeightBand(TestContext& ctx)
    {
        const Engine::TerrainChunkData flatLow = chunk(std::vector<float>(9, 0.0f));
        const Engine::TerrainMaterialEvaluationResult fallback =
            Engine::evaluateTerrainMaterialCoverage(flatLow, basicSet(), {3, 33, false});
        ctx.expect(fallback.success, "fallback evaluation failed");
        const std::optional<Engine::TerrainMaterialCoverage> grass = coverageFor(fallback, 1);
        ctx.expect(grass.has_value() && near(grass->weight, 1.0f), "low flat terrain did not use fallback");

        const Engine::TerrainChunkData flatHigh = chunk(std::vector<float>(9, 2.0f));
        const Engine::TerrainMaterialEvaluationResult height =
            Engine::evaluateTerrainMaterialCoverage(flatHigh, basicSet(), {3, 33, false});
        const std::optional<Engine::TerrainMaterialCoverage> rock = coverageFor(height, 2);
        ctx.expect(height.success && rock.has_value() && near(rock->weight, 1.0f), "height band did not select rock");
    }

    void slopedTerrainUsesSlopeRule(TestContext& ctx)
    {
        Engine::TerrainMaterialSet set;
        set.layers = {
            layer(1, "grass", true),
            layer(2, "cliff"),
        };
        set.rules = {
            rule(201, 2, std::nullopt, Engine::TerrainMaterialRange{20.0f, 90.0f}),
        };
        const Engine::TerrainChunkData sloped = chunk({
            0.0f, 2.0f, 4.0f,
            0.0f, 2.0f, 4.0f,
            0.0f, 2.0f, 4.0f,
        });
        Engine::TerrainMaterialEvaluationSettings settings;
        settings.sampleResolution = 3;
        settings.keepSamples = true;
        const Engine::TerrainMaterialEvaluationResult result =
            Engine::evaluateTerrainMaterialCoverage(sloped, set, settings);
        const std::optional<Engine::TerrainMaterialCoverage> cliff = coverageFor(result, 2);
        ctx.expect(result.success && cliff.has_value() && cliff->weight > 0.99f, "slope rule did not select cliff");
        ctx.expect(!result.samples.empty() && result.samples.front().slopeDegrees > 20.0f, "sample slope was not computed");
    }

    void worldPositionBandsAreDeterministic(TestContext& ctx)
    {
        Engine::TerrainMaterialSet set;
        set.layers = {
            layer(1, "west", true),
            layer(2, "east"),
        };
        set.rules = {
            rule(301, 2, std::nullopt, std::nullopt, Engine::TerrainMaterialRange{1.0f, 2.0f}),
        };
        const Engine::TerrainMaterialEvaluationResult result =
            Engine::evaluateTerrainMaterialCoverage(chunk(std::vector<float>(9, 0.0f)), set, {3, 33, false});
        const std::optional<Engine::TerrainMaterialCoverage> east = coverageFor(result, 2);
        const std::optional<Engine::TerrainMaterialCoverage> west = coverageFor(result, 1);
        ctx.expect(result.success && east.has_value() && west.has_value(), "world-position coverage missing layers");
        ctx.expect(near(east->weight, 2.0f / 3.0f), "world-position east coverage was wrong");
        ctx.expect(near(west->weight, 1.0f / 3.0f), "world-position fallback coverage was wrong");
    }

    void priorityAndFalloffNormalizeWeights(TestContext& ctx)
    {
        Engine::TerrainMaterialSet set;
        set.layers = {
            layer(1, "fallback", true),
            layer(2, "sand"),
            layer(3, "grass"),
        };
        set.rules = {
            rule(401, 2, Engine::TerrainMaterialRange{0.0f, 1.0f}, std::nullopt, std::nullopt, 10, 1.0f, 1.0f),
            rule(402, 3, Engine::TerrainMaterialRange{1.0f, 2.0f}, std::nullopt, std::nullopt, 5, 1.0f, 1.0f),
        };
        const Engine::TerrainMaterialEvaluationResult result =
            Engine::evaluateTerrainMaterialCoverage(chunk(std::vector<float>(9, 1.5f)), set, {3, 33, false});
        const std::optional<Engine::TerrainMaterialCoverage> sand = coverageFor(result, 2);
        const std::optional<Engine::TerrainMaterialCoverage> grass = coverageFor(result, 3);
        ctx.expect(result.success && sand.has_value() && grass.has_value(), "falloff coverage missing layers");
        ctx.expect(near(sand->weight + grass->weight, 1.0f), "falloff weights were not normalized");
        ctx.expect(near(sand->weight, 1.0f / 3.0f), "sand falloff contribution was wrong");
        ctx.expect(near(grass->weight, 2.0f / 3.0f), "grass falloff contribution was wrong");
    }

    void datasetWrapperRejectsInvalidHandles(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainMaterialEvaluationResult result =
            Engine::evaluateTerrainMaterialCoverage(dataset, {}, basicSet(), {3, 33, false});
        ctx.expect(!result.success, "invalid dataset handle unexpectedly evaluated");
        ctx.expect(!result.warnings.empty(), "invalid dataset handle did not report warning");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"ValidMaterialSetValidates", validMaterialSetValidates},
        {"InvalidMaterialSetIsDiagnosed", invalidMaterialSetIsDiagnosed},
        {"TextureDependenciesRegisterAndDeduplicate", textureDependenciesRegisterAndDeduplicate},
        {"FlatTerrainUsesFallbackAndHeightBand", flatTerrainUsesFallbackAndHeightBand},
        {"SlopedTerrainUsesSlopeRule", slopedTerrainUsesSlopeRule},
        {"WorldPositionBandsAreDeterministic", worldPositionBandsAreDeterministic},
        {"PriorityAndFalloffNormalizeWeights", priorityAndFalloffNormalizeWeights},
        {"DatasetWrapperRejectsInvalidHandles", datasetWrapperRejectsInvalidHandles},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Terrain material metadata tests passed (" << tests.size() << " tests)\n";
    return 0;
}
