#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Engine/AssetCache.hpp"
#include "Engine/TerrainMaterialMetadata.hpp"
#include "Engine/TerrainMaterialRenderAdapter.hpp"
#include "Engine/TerrainSampleMaterials.hpp"
#include "Renderer/Scene.hpp"

#ifndef MANUAL_ENGINE_SOURCE_DIR
#define MANUAL_ENGINE_SOURCE_DIR "."
#endif

namespace TestRenderer {
    void reset();
    uint32_t liveTerrainMaterialSetCount();
    Renderer::TerrainMaterialSetDescriptor terrainMaterialSetDescriptor(Renderer::TerrainMaterialSetHandle handle);
    Renderer::TerrainMaterialSetHandle terrainMaterialSetForTerrain(Renderer::TerrainHandle handle);
}

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

    Renderer::MeshVertex vertex(float x, float z)
    {
        Renderer::MeshVertex result{};
        result.px = x;
        result.py = 0.0f;
        result.pz = z;
        result.nx = 0.0f;
        result.ny = 1.0f;
        result.nz = 0.0f;
        result.tx = 1.0f;
        result.tw = 1.0f;
        result.u = x;
        result.v = z;
        result.abgr = 0xffffffff;
        return result;
    }

    std::vector<Renderer::MeshVertex> quadVertices()
    {
        return {
            vertex(0.0f, 0.0f),
            vertex(1.0f, 0.0f),
            vertex(1.0f, 1.0f),
            vertex(0.0f, 1.0f),
        };
    }

    Renderer::TerrainMaterialLayerDescriptor renderLayer(std::string name)
    {
        Renderer::TerrainMaterialLayerDescriptor layer;
        layer.name = std::move(name);
        layer.baseColorFactor = glm::vec4{0.3f, 0.6f, 0.25f, 1.0f};
        layer.tilingScale = glm::vec2{0.1f};
        return layer;
    }

    Renderer::TerrainMaterialRuleDescriptor renderRule(uint32_t layerIndex)
    {
        Renderer::TerrainMaterialRuleDescriptor rule;
        rule.name = "slope";
        rule.layerIndex = layerIndex;
        rule.priority = 10;
        rule.weight = 1.0f;
        rule.useSlopeRange = true;
        rule.minSlopeDegrees = 25.0f;
        rule.maxSlopeDegrees = 90.0f;
        return rule;
    }

    Engine::TerrainMaterialLayer metadataLayer(uint64_t id, std::string name, bool fallback = false)
    {
        Engine::TerrainMaterialLayer layer;
        layer.id = {id};
        layer.debugName = std::move(name);
        layer.fallback = fallback;
        layer.baseColorFactor = fallback ? glm::vec4{0.2f, 0.5f, 0.2f, 1.0f} : glm::vec4{0.45f, 0.42f, 0.38f, 1.0f};
        layer.tilingScale = glm::vec2{0.08f};
        return layer;
    }

    Engine::TerrainMaterialSet metadataSet()
    {
        Engine::TerrainMaterialSet set;
        set.id = {7001};
        set.debugName = "test.layered";
        set.layers.push_back(metadataLayer(1, "grass", true));
        Engine::TerrainMaterialLayer rock = metadataLayer(2, "rock");
        Engine::TerrainMaterialTextureSlot unsupported;
        unsupported.role = Engine::TerrainMaterialTextureRole::Mask;
        unsupported.sourcePath = "missing_mask.png";
        rock.textures.push_back(unsupported);
        set.layers.push_back(rock);
        Engine::TerrainMaterialRule rule;
        rule.id = {101};
        rule.layer = {2};
        rule.priority = 20;
        rule.weight = 1.0f;
        rule.slopeRangeDegrees = Engine::TerrainMaterialRange{30.0f, 90.0f};
        set.rules.push_back(rule);
        return set;
    }

    void rendererTerrainMaterialSetLifecycle(TestContext& ctx)
    {
        TestRenderer::reset();
        Renderer::TerrainMaterialSetDescriptor descriptor;
        descriptor.name = "oversized";
        for (uint32_t i = 0; i < Renderer::MaxTerrainMaterialLayers + 1; ++i) {
            descriptor.layers.push_back(renderLayer("layer" + std::to_string(i)));
        }
        for (uint32_t i = 0; i < Renderer::MaxTerrainMaterialRules + 1; ++i) {
            descriptor.rules.push_back(renderRule(0));
        }

        const Renderer::TerrainMaterialSetHandle handle = Renderer::createTerrainMaterialSet(descriptor);
        const Renderer::TerrainMaterialSetDiagnostics diagnostics = Renderer::terrainMaterialSetDiagnostics(handle);
        ctx.expect(diagnostics.valid, "created terrain material set was invalid");
        ctx.expect(diagnostics.truncatedLayerCount == 1, "oversized layer list did not report truncation");
        ctx.expect(diagnostics.truncatedRuleCount == 1, "oversized rule list did not report truncation");
        Renderer::destroyTerrainMaterialSet(handle);
        ctx.expect(TestRenderer::liveTerrainMaterialSetCount() == 0, "destroy did not release material-set handle");
    }

    void terrainTileFallbackAndLayeredStats(TestContext& ctx)
    {
        TestRenderer::reset();
        Renderer::MaterialHandle fallback = Renderer::createMaterial({});
        Renderer::TerrainHandle terrain = Renderer::createTerrainTile(quadVertices(), {0, 1, 2, 0, 2, 3}, fallback);
        Renderer::SceneDrawStats fallbackStats = Renderer::drawScene({});
        ctx.expect(fallbackStats.fallbackTerrainTiles == 1, "terrain without material set did not use fallback path");

        Renderer::TerrainMaterialSetDescriptor descriptor;
        descriptor.layers.push_back(renderLayer("grass"));
        descriptor.rules.push_back(renderRule(0));
        Renderer::TerrainMaterialSetHandle materialSet = Renderer::createTerrainMaterialSet(descriptor);
        Renderer::setTerrainMaterialSet(terrain, materialSet);
        ctx.expect(TestRenderer::terrainMaterialSetForTerrain(terrain).id == materialSet.id, "terrain material set was not assigned");
        Renderer::SceneDrawStats layeredStats = Renderer::drawScene({});
        ctx.expect(layeredStats.assignedLayeredTerrainTiles == 1, "assigned layered terrain was not counted");
        ctx.expect(layeredStats.submittedLayeredTerrainTiles == 1, "layered terrain submission was not counted");
        ctx.expect(layeredStats.fallbackTerrainTiles == 0, "layered terrain also counted as fallback");

        Renderer::clearTerrainMaterialSet(terrain);
        Renderer::SceneDrawStats clearedStats = Renderer::drawScene({});
        ctx.expect(clearedStats.fallbackTerrainTiles == 1, "cleared layered terrain did not return to fallback");
    }

    void adapterMapsMetadataAndReleases(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache cache;
        Engine::TerrainMaterialRenderCreateResult result =
            Engine::createTerrainMaterialResources(cache, metadataSet());
        ctx.expect(result.success, "adapter failed to create renderer terrain material set");
        ctx.expect(result.diagnostics.layerCount == 2, "adapter did not preserve expected layer count");
        ctx.expect(result.diagnostics.ruleCount == 1, "adapter did not preserve expected rule count");
        ctx.expect(result.diagnostics.unsupportedTextureRoleCount == 1, "unsupported texture role was not diagnosed");
        ctx.expect(TestRenderer::liveTerrainMaterialSetCount() == 1, "renderer material set was not created");

        const Renderer::TerrainMaterialSetDescriptor descriptor =
            TestRenderer::terrainMaterialSetDescriptor(result.binding.materialSet);
        ctx.expect(descriptor.layers.size() == 2, "renderer descriptor layer count mismatch");
        ctx.expect(descriptor.rules.size() == 1, "renderer descriptor rule count mismatch");
        ctx.expect(descriptor.fallbackLayerIndex == 0, "fallback layer was not mapped");

        Engine::releaseTerrainMaterialResources(cache, result.binding);
        ctx.expect(TestRenderer::liveTerrainMaterialSetCount() == 0, "adapter release did not destroy renderer material set");
        Engine::releaseTerrainMaterialResources(cache, result.binding);
        ctx.expect(TestRenderer::liveTerrainMaterialSetCount() == 0, "adapter release was not idempotent");
    }

    void shaderSourcesExist(TestContext& ctx)
    {
        const std::filesystem::path sourceRoot = MANUAL_ENGINE_SOURCE_DIR;
        ctx.expect(std::filesystem::exists(sourceRoot / "assets/shaders/src/vs_terrain.vs"), "terrain vertex shader source is missing");
        ctx.expect(std::filesystem::exists(sourceRoot / "assets/shaders/src/fs_terrain.fs"), "terrain fragment shader source is missing");
    }

    void sampleProceduralMaterialSetIsValid(TestContext& ctx)
    {
        const std::vector<Engine::TerrainSampleBiomeMaterialInput> inputs = {
            {"grass", {48, 132, 62, 255}},
            {"dry", {174, 126, 80, 255}},
        };

        const Engine::TerrainMaterialSet materialSet =
            Engine::makeSampleProceduralTerrainMaterialSet(inputs, {72, 168, 210, 255});
        const Engine::TerrainMaterialValidationDiagnostics diagnostics =
            Engine::validateTerrainMaterialSet(materialSet);

        ctx.expect(diagnostics.valid, "sample procedural terrain material set was invalid");
        ctx.expect(materialSet.layers.size() == 4, "sample material set layer count changed");
        ctx.expect(materialSet.rules.size() == 3, "sample material set rule count changed");
        ctx.expect(materialSet.layers.front().fallback, "sample material set has no fallback first layer");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"RendererTerrainMaterialSetLifecycle", rendererTerrainMaterialSetLifecycle},
        {"TerrainTileFallbackAndLayeredStats", terrainTileFallbackAndLayeredStats},
        {"AdapterMapsMetadataAndReleases", adapterMapsMetadataAndReleases},
        {"ShaderSourcesExist", shaderSourcesExist},
        {"SampleProceduralMaterialSetIsValid", sampleProceduralMaterialSetIsValid},
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

    std::cout << "Terrain layered rendering tests passed (" << tests.size() << ")\n";
    return 0;
}
