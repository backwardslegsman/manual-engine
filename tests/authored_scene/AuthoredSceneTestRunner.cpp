#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/AuthoredScene.hpp"
#include "Engine/FrameBudget.hpp"

namespace TestRenderer {
    void reset();
    uint32_t liveMeshCount();
    uint32_t liveMaterialCount();
    uint32_t liveTextureCount();
    uint32_t liveInstanceCount();
    uint32_t liveLightCount();
    uint32_t liveRenderGroupCount();
    glm::mat4 instanceTransform(Renderer::MeshInstanceHandle handle);
    Renderer::MaterialDescriptor firstMaterialDescriptor();
    Renderer::MaterialDiagnostics firstMaterialDiagnostics();
    Renderer::StaticMeshDescriptor firstMeshDescriptor();
    Renderer::TextureDescriptor textureDescriptor(Renderer::TextureHandle handle);
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

    std::filesystem::path fixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    glm::vec3 translationOf(const glm::mat4& transform)
    {
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    bool nearlyEqual(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 0.0001f;
    }

    bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs)
    {
        return nearlyEqual(lhs.x, rhs.x) && nearlyEqual(lhs.y, rhs.y) && nearlyEqual(lhs.z, rhs.z);
    }

    bool nearlyEqual(const glm::vec2& lhs, const glm::vec2& rhs)
    {
        return nearlyEqual(lhs.x, rhs.x) && nearlyEqual(lhs.y, rhs.y);
    }

    uint32_t packColorAbgr(const glm::vec4& color)
    {
        const auto channel = [](float value) {
            return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        const uint32_t r = channel(color.r);
        const uint32_t g = channel(color.g);
        const uint32_t b = channel(color.b);
        const uint32_t a = channel(color.a);
        return (a << 24u) | (b << 16u) | (g << 8u) | r;
    }

    void replaceAll(std::string& text, std::string_view from, std::string_view to)
    {
        size_t offset = 0;
        while ((offset = text.find(from, offset)) != std::string::npos) {
            text.replace(offset, from.size(), to);
            offset += to.size();
        }
    }

    void writeTinyBmp(const std::filesystem::path& path)
    {
        const unsigned char bytes[] = {
            0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff,
            0xff, 0xff, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0b,
            0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
            0xff, 0xff,
        };

        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }

    std::filesystem::path fixtureWithExistingTextures()
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "manual_engine_authored_scene_texture_test";
        std::filesystem::create_directories(directory);

        std::ifstream input(fixturePath());
        std::string fixture((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        replaceAll(fixture, "textures/base_color.png", "base_color.bmp");
        replaceAll(fixture, "textures/metallic_roughness.png", "metallic_roughness.bmp");
        replaceAll(fixture, "textures/normal.png", "normal.bmp");
        replaceAll(fixture, "textures/occlusion.png", "occlusion.bmp");
        replaceAll(fixture, "textures/emissive.png", "emissive.bmp");

        const std::filesystem::path outputPath = directory / "authored_scene_fixture.gltf";
        std::ofstream output(outputPath);
        output << fixture;

        const std::vector<std::filesystem::path> textureFiles = {
            directory / "base_color.bmp",
            directory / "metallic_roughness.bmp",
            directory / "normal.bmp",
            directory / "occlusion.bmp",
            directory / "emissive.bmp",
        };
        for (const std::filesystem::path& path : textureFiles) {
            writeTinyBmp(path);
        }

        return outputPath;
    }

    std::filesystem::path fixtureWithOverBudgetLights()
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "manual_engine_authored_scene_light_budget_test";
        std::filesystem::create_directories(directory);

        std::ifstream input(fixturePath());
        std::string fixture((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        replaceAll(fixture, "\"children\": [1, 2, 3, 4, 5]", "\"children\": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22]");

        std::string extraNodes;
        std::string extraLights;
        for (uint32_t index = 0; index < 17; ++index) {
            const uint32_t lightIndex = 4 + index;
            extraNodes +=
                ",\n"
                "    {\n"
                "      \"name\": \"BudgetLight" + std::to_string(index) + "\",\n"
                "      \"translation\": [" + std::to_string(static_cast<float>(index)) + ", 4.0, 5.0],\n"
                "      \"extensions\": {\n"
                "        \"KHR_lights_punctual\": {\n"
                "          \"light\": " + std::to_string(lightIndex) + "\n"
                "        }\n"
                "      }\n"
                "    }";
            extraLights +=
                ",\n"
                "        {\n"
                "          \"name\": \"BudgetLight" + std::to_string(index) + "\",\n"
                "          \"type\": \"point\",\n"
                "          \"color\": [1.0, 1.0, 1.0],\n"
                "          \"intensity\": 1.0,\n"
                "          \"range\": 6.0\n"
                "        }";
        }

        replaceAll(fixture, "\n  ],\n  \"meshes\":", extraNodes + "\n  ],\n  \"meshes\":");
        replaceAll(fixture, "\n      ]\n    }\n  },\n  \"buffers\":", extraLights + "\n      ]\n    }\n  },\n  \"buffers\":");

        const std::filesystem::path outputPath = directory / "authored_scene_light_budget_fixture.gltf";
        std::ofstream output(outputPath);
        output << fixture;
        return outputPath;
    }

    std::filesystem::path fixtureWithTwoMeshSectors()
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "manual_engine_authored_scene_sector_test";
        std::filesystem::create_directories(directory);

        std::ifstream input(fixturePath());
        std::string fixture((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        replaceAll(fixture, "\"children\": [1, 2, 3, 4, 5]", "\"children\": [1, 2, 3, 4, 5, 6]");
        replaceAll(fixture,
            "\n  ],\n  \"meshes\":",
            ",\n"
            "    {\n"
            "      \"name\": \"FarMeshChild\",\n"
            "      \"translation\": [80.0, 2.0, 0.0],\n"
            "      \"mesh\": 0\n"
            "    }\n"
            "  ],\n"
            "  \"meshes\":");

        const std::filesystem::path outputPath = directory / "authored_scene_two_sector_fixture.gltf";
        std::ofstream output(outputPath);
        output << fixture;
        return outputPath;
    }

    Engine::AuthoredSceneCacheSettings cacheSettings(std::string_view name, Engine::AuthoredSceneCachePolicy policy)
    {
        Engine::AuthoredSceneCacheSettings settings;
        settings.rootPath = std::filesystem::temp_directory_path() / ("manual_engine_authored_scene_cache_" + std::string{name});
        settings.policy = policy;
        std::filesystem::remove_all(settings.rootPath);
        return settings;
    }

    void expectAuthoredTextureDescriptor(
        TestContext& ctx,
        Renderer::TextureHandle handle,
        Renderer::TextureSlot slot,
        Renderer::TextureColorSpace colorSpace,
        std::string_view label)
    {
        const Renderer::TextureDescriptor descriptor = TestRenderer::textureDescriptor(handle);
        ctx.expect(descriptor.slot == slot, std::string{label} + " texture slot descriptor was wrong");
        ctx.expect(descriptor.colorSpace == colorSpace, std::string{label} + " texture color-space descriptor was wrong");
        ctx.expect(descriptor.wrapU == Renderer::TextureWrap::Repeat, std::string{label} + " wrap U descriptor was not repeat");
        ctx.expect(descriptor.wrapV == Renderer::TextureWrap::Repeat, std::string{label} + " wrap V descriptor was not repeat");
        ctx.expect(descriptor.generateMips, std::string{label} + " descriptor did not request mipmaps");
    }

    void assetCacheUsesTextureDescriptorKeys(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache cache;

        Renderer::TextureDescriptor srgb;
        srgb.slot = Renderer::TextureSlot::BaseColor;
        srgb.colorSpace = Renderer::TextureColorSpace::Srgb;
        srgb.generateMips = true;

        Renderer::TextureDescriptor linear = srgb;
        linear.colorSpace = Renderer::TextureColorSpace::Linear;

        const Engine::CachedTexture first = cache.acquireTexture("same_texture_path", srgb);
        const Engine::CachedTexture reused = cache.acquireTexture("same_texture_path", srgb);
        const Engine::CachedTexture separate = cache.acquireTexture("same_texture_path", linear);

        ctx.expect(Renderer::isValid(first.handle), "first descriptor texture was not valid");
        ctx.expect(first.id == reused.id && first.handle.id == reused.handle.id, "identical descriptor texture was not reused");
        ctx.expect(separate.id != first.id && separate.handle.id != first.handle.id, "different descriptor texture reused the same cache entry");
        ctx.expect(TestRenderer::liveTextureCount() == 2, "descriptor cache should have two live texture resources");

        cache.release(first);
        cache.release(reused);
        cache.release(separate);
        ctx.expect(TestRenderer::liveTextureCount() == 0, "descriptor cache release did not destroy textures");
    }

    void authoredSceneLoadsAndOwnsResources(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneLoadSettings settings;
        settings.loadTextures = false;

        Engine::AuthoredSceneLoadResult result = Engine::loadAuthoredScene(fixturePath(), assetCache, settings);
        ctx.expect(result.success, "authored scene load failed: " + result.message);
        if (!result.success) {
            return;
        }

        const Engine::AuthoredSceneDiagnostics& diagnostics = result.scene.diagnostics();
        ctx.expect(result.scene.loaded(), "scene did not report loaded");
        ctx.expect(result.scene.bounds().valid, "scene bounds were invalid");
        ctx.expect(diagnostics.createdMaterialCount == 1, "created material count was wrong");
        ctx.expect(diagnostics.createdMeshCount == 1, "created mesh count was wrong");
        ctx.expect(diagnostics.createdInstanceCount == 1, "created instance count was wrong");
        ctx.expect(diagnostics.importedLightCount == 4, "imported light count was wrong");
        ctx.expect(diagnostics.createdLightCount == 4, "created light count was wrong");
        ctx.expect(diagnostics.activeAuthoredLightCount == 3, "active authored light count was wrong");
        ctx.expect(diagnostics.disabledZeroIntensityLightCount == 1, "zero-intensity light diagnostic was wrong");
        ctx.expect(diagnostics.skippedUnsupportedLightCount == 0, "fixture should not skip unsupported lights");
        ctx.expect(diagnostics.skippedOverBudgetLightCount == 0, "fixture should not skip over-budget lights");
        ctx.expect(diagnostics.mappedPackedMetallicRoughnessCount == 1, "packed metallic-roughness mapping diagnostic missing");
        ctx.expect(diagnostics.deferredAlphaMaterialCount == 1, "alpha deferred diagnostic missing");
        ctx.expect(diagnostics.deferredDoubleSidedMaterialCount == 1, "double-sided deferred diagnostic missing");
        ctx.expect(diagnostics.deferredOcclusionTextureCount == 1, "occlusion deferred diagnostic missing");
        ctx.expect(diagnostics.deferredEmissiveTextureCount == 1, "emissive deferred diagnostic missing");
        ctx.expect(diagnostics.retainedTexcoord1PrimitiveCount == 1, "texcoord1 retained diagnostic missing");
        ctx.expect(diagnostics.retainedVertexColorPrimitiveCount == 1, "vertex color retained diagnostic missing");
        ctx.expect(TestRenderer::liveMaterialCount() == 1, "stub renderer material was not live");
        ctx.expect(TestRenderer::liveMeshCount() == 1, "stub renderer mesh was not live");
        ctx.expect(TestRenderer::liveInstanceCount() == 1, "stub renderer instance was not live");
        ctx.expect(TestRenderer::liveLightCount() == 4, "stub renderer lights were not live");

        const Renderer::MaterialDescriptor material = TestRenderer::firstMaterialDescriptor();
        ctx.expect(material.name == "FixtureBlendDoubleSided", "renderer material name did not map");
        ctx.expect(material.alphaMode == Renderer::MaterialDescriptor::AlphaMode::Blend, "renderer alpha mode did not map");
        ctx.expect(material.doubleSided, "renderer double-sided flag did not map");
        const Renderer::MaterialDiagnostics materialDiagnostics = TestRenderer::firstMaterialDiagnostics();
        ctx.expect(materialDiagnostics.renderPass == Renderer::MaterialRenderPass::AlphaBlend, "renderer material render pass was not alpha blend");
        ctx.expect(materialDiagnostics.doubleSided, "renderer material diagnostics did not preserve double-sided intent");
        ctx.expect(material.metallicRoughnessMetallicChannel == Renderer::MaterialDescriptor::TextureChannel::B,
            "renderer packed metallic channel was not glTF B");
        ctx.expect(material.metallicRoughnessRoughnessChannel == Renderer::MaterialDescriptor::TextureChannel::G,
            "renderer packed roughness channel was not glTF G");
        ctx.expect(nearlyEqual(material.normalScale, 0.75f) || nearlyEqual(material.normalScale, 1.0f),
            "renderer normal scale was neither mapped nor defaulted");
        ctx.expect(nearlyEqual(material.occlusionStrength, 0.6f) || nearlyEqual(material.occlusionStrength, 1.0f),
            "renderer occlusion strength was neither mapped nor defaulted");
        ctx.expect(nearlyEqual(material.emissiveFactor, {0.1f, 0.2f, 0.3f}), "renderer emissive factor did not map");

        const Renderer::StaticMeshDescriptor mesh = TestRenderer::firstMeshDescriptor();
        ctx.expect(mesh.submeshes.size() == 1, "renderer mesh descriptor was not captured");
        if (!mesh.submeshes.empty() && !mesh.submeshes.front().vertices.empty()) {
            const Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(fixturePath());
            ctx.expect(imported.success, "fixture import for vertex comparison failed");
            const Renderer::MeshVertex& vertex = mesh.submeshes.front().vertices.front();
            if (imported.success && !imported.meshes.empty() && !imported.meshes.front().primitives.empty() &&
                !imported.meshes.front().primitives.front().vertices.empty()) {
                const Assets::Assimp::ImportedSceneVertex& importedVertex =
                    imported.meshes.front().primitives.front().vertices.front();
                ctx.expect(nearlyEqual(vertex.tw, importedVertex.tangent.w), "renderer tangent handedness was not preserved");
                ctx.expect(nearlyEqual(glm::vec2{vertex.u1, vertex.v1}, importedVertex.texcoord1),
                    "renderer texcoord1 was not preserved");
                ctx.expect(vertex.abgr == packColorAbgr(importedVertex.color0), "renderer color0 was not preserved");
            }
        }

        ctx.expect(result.scene.instances().size() == 1, "scene instance record count was wrong");
        if (!result.scene.instances().empty()) {
            const Renderer::MeshInstanceHandle handle = result.scene.instances().front().handle;
            ctx.expect(nearlyEqual(translationOf(TestRenderer::instanceTransform(handle)), {10.0f, 2.0f, 0.0f}),
                "instance transform did not preserve imported node world transform");
        }

        result.scene.shutdown();
        ctx.expect(!result.scene.loaded(), "scene still reported loaded after shutdown");
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "instance was not destroyed by shutdown");
        ctx.expect(TestRenderer::liveLightCount() == 0, "lights were not destroyed by shutdown");
        ctx.expect(TestRenderer::liveMeshCount() == 0, "mesh was not destroyed by shutdown");
        ctx.expect(TestRenderer::liveMaterialCount() == 0, "material was not destroyed by shutdown");

        result.scene.shutdown();
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "second shutdown recreated or leaked instances");
    }

    void authoredSceneMapsTextureDescriptors(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneLoadSettings settings;
        settings.loadTextures = true;

        Engine::AuthoredSceneLoadResult result = Engine::loadAuthoredScene(fixtureWithExistingTextures(), assetCache, settings);
        ctx.expect(result.success, "authored scene texture load failed: " + result.message);
        if (!result.success) {
            return;
        }

        const Engine::AuthoredSceneDiagnostics& diagnostics = result.scene.diagnostics();
        ctx.expect(
            diagnostics.textureLoadSuccessCount == 5,
            "authored scene texture success count was wrong: " + std::to_string(diagnostics.textureLoadSuccessCount));
        ctx.expect(diagnostics.textureLoadFailureCount == 0, "authored scene texture failure count was wrong");
        ctx.expect(diagnostics.textureEstimatedBytes > 0, "authored scene texture bytes estimate was missing");
        ctx.expect(
            TestRenderer::liveTextureCount() == 5,
            "stub renderer did not load expected authored textures: " + std::to_string(TestRenderer::liveTextureCount()));

        const Renderer::MaterialDescriptor material = TestRenderer::firstMaterialDescriptor();
        expectAuthoredTextureDescriptor(ctx, material.baseColorTexture, Renderer::TextureSlot::BaseColor, Renderer::TextureColorSpace::Srgb, "base color");
        expectAuthoredTextureDescriptor(ctx, material.normalTexture, Renderer::TextureSlot::Normal, Renderer::TextureColorSpace::Linear, "normal");
        expectAuthoredTextureDescriptor(
            ctx,
            material.metallicRoughnessTexture,
            Renderer::TextureSlot::MetallicRoughness,
            Renderer::TextureColorSpace::Linear,
            "metallic-roughness");
        expectAuthoredTextureDescriptor(ctx, material.occlusionTexture, Renderer::TextureSlot::Occlusion, Renderer::TextureColorSpace::Linear, "occlusion");
        expectAuthoredTextureDescriptor(ctx, material.emissiveTexture, Renderer::TextureSlot::Emissive, Renderer::TextureColorSpace::Srgb, "emissive");

        result.scene.shutdown();
        ctx.expect(TestRenderer::liveTextureCount() == 0, "authored scene shutdown did not release loaded textures");
    }

    void authoredSceneSkipsOverBudgetLights(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneLoadSettings settings;
        settings.loadTextures = false;

        Engine::AuthoredSceneLoadResult result = Engine::loadAuthoredScene(fixtureWithOverBudgetLights(), assetCache, settings);
        ctx.expect(result.success, "over-budget authored scene load failed: " + result.message);
        if (!result.success) {
            return;
        }

        const Engine::AuthoredSceneDiagnostics& diagnostics = result.scene.diagnostics();
        ctx.expect(diagnostics.importedLightCount == 21, "over-budget fixture imported light count was wrong");
        ctx.expect(diagnostics.createdLightCount == Renderer::MaxForwardLights + 1, "over-budget fixture created wrong light count");
        ctx.expect(diagnostics.activeAuthoredLightCount == Renderer::MaxForwardLights, "active authored light budget was wrong");
        ctx.expect(diagnostics.disabledZeroIntensityLightCount == 1, "zero-intensity light should still be created disabled");
        ctx.expect(diagnostics.skippedOverBudgetLightCount == 4, "over-budget light skip count was wrong");
        ctx.expect(TestRenderer::liveLightCount() == Renderer::MaxForwardLights + 1, "stub renderer live light count was wrong");

        result.scene.shutdown();
        ctx.expect(TestRenderer::liveLightCount() == 0, "over-budget scene shutdown did not release lights");
    }

    void authoredScenePartitionIsDeterministic(TestContext& ctx)
    {
        const Engine::AuthoredScenePartition partition = Engine::partitionAuthoredScene(fixtureWithTwoMeshSectors(), {25.0f, true});
        ctx.expect(partition.sectors.size() == 2, "two-sector fixture did not partition into two sectors");
        ctx.expect(!partition.usedFallbackRootSector, "valid partition unexpectedly used root fallback");

        uint32_t assignedNodes = 0;
        uint32_t primitiveCount = 0;
        for (const Engine::AuthoredSceneSectorManifest& sector : partition.sectors) {
            ctx.expect(sector.bounds.valid, "sector bounds were invalid");
            ctx.expect(!sector.nodeIndices.empty(), "sector had no node references");
            ctx.expect(!sector.meshIndices.empty(), "sector had no mesh references");
            ctx.expect(!sector.materialIndices.empty(), "sector had no material references");
            ctx.expect(!sector.textureIndices.empty(), "sector had no texture references");
            assignedNodes += static_cast<uint32_t>(sector.nodeIndices.size());
            primitiveCount += sector.primitiveCount;
        }
        ctx.expect(assignedNodes == 2, "mesh nodes were not assigned exactly once");
        ctx.expect(primitiveCount == 2, "sector primitive counts were wrong");

        const Engine::AuthoredScenePartition fallback = Engine::partitionAuthoredScene(fixturePath(), {25.0f, false});
        ctx.expect(fallback.sectors.size() == 1, "disabled partition did not produce a root sector");
        ctx.expect(fallback.usedFallbackRootSector, "disabled partition did not report root fallback");
    }

    void partitionedAuthoredSceneStreamsSectors(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition.sectorSize = 25.0f;
        settings.initialCameraPosition = {10.0f, 2.0f, 0.0f};
        settings.loadRadius = 20.0f;
        settings.unloadRadius = 35.0f;
        settings.maxSectorLoadCommitsPerFrame = 1;
        settings.maxSectorUnloadCommitsPerFrame = 1;

        Engine::PartitionedAuthoredSceneLoadResult result =
            Engine::loadPartitionedAuthoredScene(fixtureWithTwoMeshSectors(), assetCache, settings);
        ctx.expect(result.success, "partitioned authored scene load failed: " + result.message);
        if (!result.success) {
            return;
        }

        ctx.expect(result.scene.partition().sectors.size() == 2, "streaming scene did not keep two sector manifests");
        ctx.expect(result.scene.diagnostics().loadedSectorCount == 1, "initial streaming load did not load exactly one sector");
        ctx.expect(TestRenderer::liveInstanceCount() == 1, "initial streaming load created wrong instance count");
        ctx.expect(TestRenderer::liveRenderGroupCount() == 1, "initial streaming load created wrong render group count");

        Engine::MainThreadWorkQueue queue;
        result.scene.updateStreaming({90.0f, 2.0f, 0.0f}, queue);
        Engine::FrameBudget budget;
        budget.beginFrame({100.0f, true});
        queue.drain(budget);

        ctx.expect(result.scene.diagnostics().loadedSectorCount == 1, "streaming update did not converge to one loaded sector");
        ctx.expect(result.scene.diagnostics().pendingLoadSectorCount == 0, "streaming load work stayed pending after drain");
        ctx.expect(result.scene.diagnostics().pendingUnloadSectorCount == 0, "streaming unload work stayed pending after drain");
        ctx.expect(TestRenderer::liveInstanceCount() == 1, "streaming update should leave one live sector instance");
        ctx.expect(TestRenderer::liveRenderGroupCount() == 1, "streaming update should leave one live render group");

        result.scene.shutdown();
        result.scene.shutdown();
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "partitioned scene shutdown leaked instances");
        ctx.expect(TestRenderer::liveRenderGroupCount() == 0, "partitioned scene shutdown leaked render groups");
        ctx.expect(TestRenderer::liveMeshCount() == 0, "partitioned scene shutdown leaked meshes");
        ctx.expect(TestRenderer::liveMaterialCount() == 0, "partitioned scene shutdown leaked materials");
    }

    void partitionedAuthoredSceneSharesMaterials(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition.sectorSize = 25.0f;
        settings.initialCameraPosition = {45.0f, 2.0f, 0.0f};
        settings.loadRadius = 100.0f;

        Engine::PartitionedAuthoredSceneLoadResult result =
            Engine::loadPartitionedAuthoredScene(fixtureWithTwoMeshSectors(), assetCache, settings);
        ctx.expect(result.success, "partitioned shared-material scene load failed: " + result.message);
        if (!result.success) {
            return;
        }

        ctx.expect(result.scene.diagnostics().loadedSectorCount == 2, "shared-material test did not load both sectors");
        ctx.expect(TestRenderer::liveInstanceCount() == 2, "shared-material test did not create both instances");
        ctx.expect(TestRenderer::liveMaterialCount() == 1, "shared material was not reused across sectors");
        ctx.expect(result.scene.diagnostics().sharedMaterialReferenceCount == 1, "shared material reference diagnostic was wrong");

        result.scene.shutdown();
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "shared-material scene shutdown leaked instances");
        ctx.expect(TestRenderer::liveRenderGroupCount() == 0, "shared-material scene shutdown leaked render groups");
        ctx.expect(TestRenderer::liveMaterialCount() == 0, "shared-material scene shutdown leaked material");
    }

    void authoredSceneCacheRoundTripsPayload(TestContext& ctx)
    {
        const std::filesystem::path fixture = fixtureWithTwoMeshSectors();
        Engine::AuthoredSceneCacheSettings settings = cacheSettings("roundtrip", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
        Engine::AuthoredScenePartitionSettings partitionSettings;
        partitionSettings.sectorSize = 25.0f;

        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(fixture);
        ctx.expect(imported.success, "fixture import for cache round trip failed");
        if (!imported.success) {
            return;
        }

        Engine::AuthoredSceneCachePayload payload;
        payload.scene = imported;
        payload.partition = Engine::partitionAuthoredScene(fixture, partitionSettings);
        const Engine::AuthoredSceneCacheManifest manifest =
            Engine::AuthoredSceneCache::buildManifest(settings, fixture, partitionSettings);

        Engine::AuthoredSceneCacheWriteResult write = Engine::AuthoredSceneCache::write(manifest, payload);
        ctx.expect(write.status == Engine::AuthoredSceneCacheStatus::WriteSuccess, "cache write failed: " + write.message);
        Engine::AuthoredSceneCacheReadResult read = Engine::AuthoredSceneCache::read(manifest);
        ctx.expect(read.status == Engine::AuthoredSceneCacheStatus::Hit, "cache read did not hit: " + read.message);
        ctx.expect(read.payload.has_value(), "cache hit did not include payload");
        if (!read.payload) {
            return;
        }

        ctx.expect(read.payload->scene.nodes.size() == imported.nodes.size(), "cache node count did not round trip");
        ctx.expect(read.payload->scene.meshes.size() == imported.meshes.size(), "cache mesh count did not round trip");
        ctx.expect(read.payload->scene.materials.size() == imported.materials.size(), "cache material count did not round trip");
        ctx.expect(read.payload->scene.textures.size() == imported.textures.size(), "cache texture count did not round trip");
        ctx.expect(read.payload->scene.lights.size() == imported.lights.size(), "cache light count did not round trip");
        ctx.expect(read.payload->partition.sectors.size() == payload.partition.sectors.size(), "cache sector count did not round trip");
        ctx.expect(!read.payload->scene.meshes.empty() &&
                !read.payload->scene.meshes.front().primitives.empty() &&
                !read.payload->scene.meshes.front().primitives.front().vertices.empty(),
            "cache mesh vertex payload was empty");
    }

    void partitionedAuthoredSceneUsesCachePolicies(TestContext& ctx)
    {
        const std::filesystem::path fixture = fixtureWithTwoMeshSectors();
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition.sectorSize = 25.0f;
        settings.initialCameraPosition = {45.0f, 2.0f, 0.0f};
        settings.loadRadius = 100.0f;
        settings.cache = cacheSettings("policies", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);

        TestRenderer::reset();
        Engine::AssetCache firstCache;
        Engine::PartitionedAuthoredSceneLoadResult generated =
            Engine::loadPartitionedAuthoredScene(fixture, firstCache, settings);
        ctx.expect(generated.success, "GenerateOnMiss cache scene load failed: " + generated.message);
        ctx.expect(generated.scene.diagnostics().cacheWriteCount == 1, "GenerateOnMiss did not write cache");
        ctx.expect(!generated.scene.diagnostics().loadedFromCache, "first GenerateOnMiss load should not be from cache");
        generated.scene.shutdown();

        TestRenderer::reset();
        Engine::AssetCache secondCache;
        settings.cache.policy = Engine::AuthoredSceneCachePolicy::ReadOnly;
        Engine::PartitionedAuthoredSceneLoadResult cached =
            Engine::loadPartitionedAuthoredScene(fixture, secondCache, settings);
        ctx.expect(cached.success, "ReadOnly cache hit scene load failed: " + cached.message);
        ctx.expect(cached.scene.diagnostics().loadedFromCache, "ReadOnly load did not use existing cache");
        ctx.expect(cached.scene.diagnostics().cacheStatus == Engine::AuthoredSceneCacheStatus::Hit, "ReadOnly cache status was not hit");
        ctx.expect(cached.scene.diagnostics().loadedSectorCount == 2, "cache hit did not load expected sectors");
        cached.scene.shutdown();

        TestRenderer::reset();
        Engine::AssetCache refreshCache;
        settings.cache.policy = Engine::AuthoredSceneCachePolicy::Refresh;
        Engine::PartitionedAuthoredSceneLoadResult refreshed =
            Engine::loadPartitionedAuthoredScene(fixture, refreshCache, settings);
        ctx.expect(refreshed.success, "Refresh cache scene load failed: " + refreshed.message);
        ctx.expect(refreshed.scene.diagnostics().cacheWriteCount == 1, "Refresh did not rewrite cache");
        ctx.expect(!refreshed.scene.diagnostics().loadedFromCache, "Refresh should load from source before writing cache");
        refreshed.scene.shutdown();
    }

    void authoredSceneCacheHandlesMissStaleAndCorrupt(TestContext& ctx)
    {
        const std::filesystem::path fixture = fixtureWithTwoMeshSectors();
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition.sectorSize = 25.0f;
        settings.loadRadius = 100.0f;
        settings.cache = cacheSettings("miss_stale_corrupt", Engine::AuthoredSceneCachePolicy::ReadOnly);

        Engine::AssetCache missCache;
        Engine::PartitionedAuthoredSceneLoadResult miss =
            Engine::loadPartitionedAuthoredScene(fixture, missCache, settings);
        ctx.expect(miss.success, "ReadOnly miss should fall back to source import");
        ctx.expect(!miss.scene.diagnostics().loadedFromCache, "ReadOnly miss incorrectly loaded from cache");
        ctx.expect(miss.scene.diagnostics().cacheMissCount == 1, "ReadOnly miss diagnostic was wrong");
        ctx.expect(miss.scene.diagnostics().cacheWriteCount == 0, "ReadOnly miss wrote cache");
        miss.scene.shutdown();

        settings.cache.policy = Engine::AuthoredSceneCachePolicy::GenerateOnMiss;
        Engine::AssetCache writeCache;
        Engine::PartitionedAuthoredSceneLoadResult write =
            Engine::loadPartitionedAuthoredScene(fixture, writeCache, settings);
        ctx.expect(write.success, "cache generation before stale/corrupt tests failed");
        const std::filesystem::path cacheRoot = write.scene.diagnostics().cachePath.parent_path();
        write.scene.shutdown();

        const Engine::AuthoredSceneCacheManifest originalManifest =
            Engine::AuthoredSceneCache::buildManifest(settings.cache, fixture, settings.partition);
        Engine::AuthoredSceneCacheManifest staleManifest = originalManifest;
        staleManifest.sourceHash = "different";
        staleManifest.identityHash = originalManifest.identityHash;
        Engine::AuthoredSceneCacheReadResult stale = Engine::AuthoredSceneCache::read(staleManifest);
        ctx.expect(stale.status == Engine::AuthoredSceneCacheStatus::Stale, "cache hash mismatch was not stale");

        {
            std::ofstream corrupt(cacheRoot / "mesh_0.bin", std::ios::binary | std::ios::trunc);
            corrupt << "bad";
        }
        Engine::AuthoredSceneCacheReadResult corruptRead = Engine::AuthoredSceneCache::read(originalManifest);
        ctx.expect(corruptRead.status == Engine::AuthoredSceneCacheStatus::Corrupt, "truncated mesh payload was not corrupt");

        Engine::AuthoredSceneCacheManifest differentPartition =
            Engine::AuthoredSceneCache::buildManifest(settings.cache, fixture, {50.0f, true});
        ctx.expect(
            differentPartition.identityHash != originalManifest.identityHash,
            "different partition settings did not produce different cache identity");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"AuthoredSceneLoadsAndOwnsResources", authoredSceneLoadsAndOwnsResources},
        {"AssetCacheUsesTextureDescriptorKeys", assetCacheUsesTextureDescriptorKeys},
        {"AuthoredSceneMapsTextureDescriptors", authoredSceneMapsTextureDescriptors},
        {"AuthoredSceneSkipsOverBudgetLights", authoredSceneSkipsOverBudgetLights},
        {"AuthoredScenePartitionIsDeterministic", authoredScenePartitionIsDeterministic},
        {"PartitionedAuthoredSceneStreamsSectors", partitionedAuthoredSceneStreamsSectors},
        {"PartitionedAuthoredSceneSharesMaterials", partitionedAuthoredSceneSharesMaterials},
        {"AuthoredSceneCacheRoundTripsPayload", authoredSceneCacheRoundTripsPayload},
        {"PartitionedAuthoredSceneUsesCachePolicies", partitionedAuthoredSceneUsesCachePolicies},
        {"AuthoredSceneCacheHandlesMissStaleAndCorrupt", authoredSceneCacheHandlesMissStaleAndCorrupt},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
        std::cout << "[authored_scene] " << name << '\n';
    }

    if (failures.empty()) {
        std::cout << "Authored scene tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Authored scene tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  [" << failure.testName << "] " << failure.message << '\n';
    }
    return 1;
}
