#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AnimatedModel.hpp"
#include "Engine/AnimatedModelAsync.hpp"
#include "Engine/AnimatedModelCache.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/AuthoredScene.hpp"
#include "Engine/AuthoredSceneAsync.hpp"
#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/FrameBudget.hpp"

namespace TestRenderer {
    void reset();
    uint32_t liveMeshCount();
    uint32_t liveSkinnedMeshCount();
    uint32_t liveMaterialCount();
    uint32_t liveTextureCount();
    uint32_t liveInstanceCount();
    uint32_t liveSkinnedInstanceCount();
    uint32_t liveLightCount();
    uint32_t liveRenderGroupCount();
    glm::mat4 instanceTransform(Renderer::MeshInstanceHandle handle);
    glm::mat4 skinnedInstanceTransform(Renderer::SkinnedMeshInstanceHandle handle);
    std::vector<glm::mat4> skinnedInstanceJointMatrices(Renderer::SkinnedMeshInstanceHandle handle);
    Renderer::MaterialDescriptor firstMaterialDescriptor();
    Renderer::MaterialDiagnostics firstMaterialDiagnostics();
    Renderer::StaticMeshDescriptor firstMeshDescriptor();
    Renderer::SkinnedMeshDescriptor firstSkinnedMeshDescriptor();
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

    struct ProfileSample {
        std::string label;
        float milliseconds = 0.0f;
    };

    struct ProfileReport {
        std::vector<ProfileSample> samples;
        std::vector<std::pair<std::string, std::string>> facts;

        template <typename Function>
        decltype(auto) measure(std::string label, Function&& function)
        {
            const auto start = std::chrono::steady_clock::now();
            if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
                function();
                const auto end = std::chrono::steady_clock::now();
                samples.push_back({std::move(label), std::chrono::duration<float, std::milli>(end - start).count()});
            } else {
                auto result = function();
                const auto end = std::chrono::steady_clock::now();
                samples.push_back({std::move(label), std::chrono::duration<float, std::milli>(end - start).count()});
                return result;
            }
        }

        void fact(std::string key, std::string value)
        {
            facts.push_back({std::move(key), std::move(value)});
        }
    };

    void writeProfileReport(const std::filesystem::path& path, const ProfileReport& report)
    {
        std::ofstream output(path);
        output << "Authored Scene Profile\n";
        output << "samples:\n";
        for (const ProfileSample& sample : report.samples) {
            output << "  - label: \"" << sample.label << "\"\n";
            output << "    milliseconds: " << sample.milliseconds << '\n';
        }
        output << "facts:\n";
        for (const auto& [key, value] : report.facts) {
            output << "  " << key << ": \"" << value << "\"\n";
        }
    }

    void expectNoLiveRendererResources(TestContext& ctx, std::string_view label)
    {
        ctx.expect(TestRenderer::liveMeshCount() == 0, std::string{label} + " leaked meshes");
        ctx.expect(TestRenderer::liveSkinnedMeshCount() == 0, std::string{label} + " leaked skinned meshes");
        ctx.expect(TestRenderer::liveMaterialCount() == 0, std::string{label} + " leaked materials");
        ctx.expect(TestRenderer::liveTextureCount() == 0, std::string{label} + " leaked textures");
        ctx.expect(TestRenderer::liveInstanceCount() == 0, std::string{label} + " leaked instances");
        ctx.expect(TestRenderer::liveSkinnedInstanceCount() == 0, std::string{label} + " leaked skinned instances");
        ctx.expect(TestRenderer::liveLightCount() == 0, std::string{label} + " leaked lights");
        ctx.expect(TestRenderer::liveRenderGroupCount() == 0, std::string{label} + " leaked render groups");
    }

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

    std::filesystem::path skinnedFixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/skinned_animation_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../tests/assets/fixtures/skinned_animation_fixture.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::filesystem::path sponzaPath()
    {
        const std::filesystem::path sourceRelative = "assets/main_sponza/NewSponza_Main_glTF_003.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../assets/main_sponza/NewSponza_Main_glTF_003.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::filesystem::path sponzaFbxPath()
    {
        const std::filesystem::path sourceRelative = "assets/main_sponza/NewSponza_Main_Yup_003.fbx";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../assets/main_sponza/NewSponza_Main_Yup_003.fbx";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::vector<std::filesystem::path> kayKitFbxAnimationPaths()
    {
        return {
            "assets/KayKit_Adventurers_2.0_FREE/Animations/fbx/Rig_Medium/Rig_Medium_General.fbx",
            "assets/KayKit_Adventurers_2.0_FREE/Animations/fbx/Rig_Medium/Rig_Medium_MovementBasic.fbx",
        };
    }

    std::filesystem::path authoredReportPath(std::string_view filename)
    {
        const std::filesystem::path directory = "generated/authored_scene_reports";
        std::filesystem::create_directories(directory);
        return directory / filename;
    }

    bool heavyOptionalAssetTestsEnabled()
    {
        const char* value = std::getenv("MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS");
        if (!value) {
            return false;
        }
        const std::string_view text{value};
        return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
    }

    glm::vec3 translationOf(const glm::mat4& transform)
    {
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs)
    {
        return nearlyEqual(lhs.x, rhs.x) && nearlyEqual(lhs.y, rhs.y) && nearlyEqual(lhs.z, rhs.z);
    }

    bool nearlyEqual(const glm::vec2& lhs, const glm::vec2& rhs)
    {
        return nearlyEqual(lhs.x, rhs.x) && nearlyEqual(lhs.y, rhs.y);
    }

    bool nearlyEqual(const glm::mat4& lhs, const glm::mat4& rhs)
    {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!nearlyEqual(lhs[column][row], rhs[column][row])) {
                    return false;
                }
            }
        }
        return true;
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

    Engine::AnimatedModelCacheSettings animatedCacheSettings(std::string_view name, Engine::AnimatedModelCachePolicy policy)
    {
        Engine::AnimatedModelCacheSettings settings;
        settings.rootPath = std::filesystem::temp_directory_path() / ("manual_engine_animated_model_cache_" + std::string{name});
        settings.policy = policy;
        std::filesystem::remove_all(settings.rootPath);
        return settings;
    }

    template <typename Result>
    std::optional<Result> waitForAsyncResult(Engine::AsyncWorkQueue& queue, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            for (Engine::AsyncCompletedJob& completed : queue.pollCompleted()) {
                if (Result* result = std::any_cast<Result>(&completed.result)) {
                    return std::move(*result);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return std::nullopt;
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
        ctx.expect(read.payload->scene.sourceFormat == imported.sourceFormat, "cache source format did not round trip");
        ctx.expect(
            read.payload->scene.diagnostics.sourceFormat == imported.diagnostics.sourceFormat,
            "cache diagnostic source format did not round trip");
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

    void authoredSceneAsyncJobsRoundTripCacheAndImport(TestContext& ctx)
    {
        const std::filesystem::path fixture = fixtureWithTwoMeshSectors();
        Engine::AuthoredScenePartitionSettings partitionSettings;
        partitionSettings.sectorSize = 25.0f;
        const Engine::AuthoredSceneCacheSettings cache =
            cacheSettings("async_jobs", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
        const Engine::AuthoredSceneCacheManifest manifest =
            Engine::AuthoredSceneCache::buildManifest(cache, fixture, partitionSettings);

        Engine::AsyncWorkQueue queue{1};
        Engine::enqueueAuthoredSceneImportAndPartition(queue, fixture, partitionSettings);
        std::optional<Engine::AuthoredSceneImportJobResult> import =
            waitForAsyncResult<Engine::AuthoredSceneImportJobResult>(queue, std::chrono::seconds(10));
        ctx.expect(import.has_value(), "async import job did not complete");
        if (!import) {
            queue.shutdown();
            return;
        }
        ctx.expect(import->success, "async import failed: " + import->message);
        ctx.expect(import->payload.scene.success, "async import did not return an imported scene payload");
        ctx.expect(import->payload.partition.sectors.size() == 2, "async import partition sector count was wrong");
        ctx.expect(import->importMs >= 0.0f, "async import timing was invalid");

        Engine::enqueueAuthoredSceneCacheWrite(queue, manifest, import->payload);
        std::optional<Engine::AuthoredSceneCacheWriteJobResult> write =
            waitForAsyncResult<Engine::AuthoredSceneCacheWriteJobResult>(queue, std::chrono::seconds(10));
        ctx.expect(write.has_value(), "async cache write job did not complete");
        if (!write) {
            queue.shutdown();
            return;
        }
        ctx.expect(
            write->result.status == Engine::AuthoredSceneCacheStatus::WriteSuccess,
            "async cache write failed: " + write->result.message);

        Engine::enqueueAuthoredSceneCacheRead(queue, manifest);
        std::optional<Engine::AuthoredSceneCacheReadJobResult> read =
            waitForAsyncResult<Engine::AuthoredSceneCacheReadJobResult>(queue, std::chrono::seconds(10));
        ctx.expect(read.has_value(), "async cache read job did not complete");
        if (!read) {
            queue.shutdown();
            return;
        }
        ctx.expect(read->result.status == Engine::AuthoredSceneCacheStatus::Hit, "async cache read did not hit");
        ctx.expect(read->result.payload.has_value(), "async cache hit did not return payload");
        ctx.expect(
            read->result.payload && read->result.payload->partition.sectors.size() == import->payload.partition.sectors.size(),
            "async cache read partition did not match written payload");
        queue.shutdown();
    }

    void authoredSceneAsyncPayloadCommitsThroughPartitionedOwner(TestContext& ctx)
    {
        const std::filesystem::path fixture = fixtureWithTwoMeshSectors();
        Engine::AuthoredScenePartitionSettings partitionSettings;
        partitionSettings.sectorSize = 25.0f;

        Engine::AsyncWorkQueue queue{1};
        Engine::enqueueAuthoredSceneImportAndPartition(queue, fixture, partitionSettings);
        std::optional<Engine::AuthoredSceneImportJobResult> import =
            waitForAsyncResult<Engine::AuthoredSceneImportJobResult>(queue, std::chrono::seconds(10));
        queue.shutdown();
        ctx.expect(import.has_value(), "async import job did not complete for commit test");
        if (!import) {
            return;
        }
        ctx.expect(import->success, "async import failed for commit test: " + import->message);
        if (!import->success) {
            return;
        }

        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition = partitionSettings;
        settings.initialCameraPosition = {10.0f, 2.0f, 0.0f};
        settings.loadRadius = 20.0f;
        settings.unloadRadius = 35.0f;
        settings.loadInitialSectorsImmediately = false;

        Engine::PartitionedAuthoredSceneLoadResult result =
            Engine::createPartitionedAuthoredSceneFromPayload(
                fixture,
                std::move(import->payload),
                assetCache,
                settings);
        ctx.expect(result.success, "async payload commit failed: " + result.message);
        if (!result.success) {
            return;
        }
        ctx.expect(result.scene.loaded(), "partitioned owner did not report loaded after async payload commit");
        ctx.expect(result.scene.diagnostics().loadedSectorCount == 0, "async payload commit loaded sectors immediately");

        Engine::MainThreadWorkQueue work;
        Engine::FrameBudget budget;
        result.scene.updateStreaming(settings.initialCameraPosition, work);
        budget.beginFrame({5.0f, true});
        work.drain(budget);
        ctx.expect(result.scene.diagnostics().loadedSectorCount == 1, "async payload streaming commit did not load one sector");
        ctx.expect(TestRenderer::liveInstanceCount() == 1, "async payload streaming commit created wrong instance count");

        result.scene.setAsyncDiagnostics("committed", 1, 1, 0, 0, 1.0f, import->importMs, 0.0f, "test async commit");
        ctx.expect(result.scene.diagnostics().asyncPhase == "committed", "async diagnostics phase was not stored");
        ctx.expect(result.scene.diagnostics().asyncJobsCompleted == 1, "async diagnostics completed count was not stored");

        result.scene.shutdown();
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "async payload scene shutdown leaked instances");
        ctx.expect(TestRenderer::liveRenderGroupCount() == 0, "async payload scene shutdown leaked render groups");
    }

    void authoredSceneDiagnosticsSummaryAndProfileReport(TestContext& ctx)
    {
        TestRenderer::reset();
        ProfileReport profile;
        const std::filesystem::path fixture = fixtureWithTwoMeshSectors();
        Engine::AuthoredScenePartitionSettings partitionSettings;
        partitionSettings.sectorSize = 25.0f;

        Assets::Assimp::ImportedScene imported = profile.measure("source import", [&]() {
            return Assets::Assimp::importScene(fixture);
        });
        ctx.expect(imported.success, "profile import failed: " + imported.error);
        if (!imported.success) {
            return;
        }

        Engine::AuthoredScenePartition partition = profile.measure("partition", [&]() {
            return Engine::partitionImportedAuthoredScene(imported, partitionSettings);
        });
        ctx.expect(partition.sectors.size() == 2, "profile partition sector count was wrong");

        Engine::AuthoredSceneCachePayload payload;
        payload.scene = imported;
        payload.partition = partition;
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition = partitionSettings;
        settings.initialCameraPosition = {10.0f, 2.0f, 0.0f};
        settings.loadRadius = 20.0f;
        settings.unloadRadius = 35.0f;
        settings.loadInitialSectorsImmediately = false;
        settings.cache = cacheSettings("profile_report", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
        const Engine::AuthoredSceneCacheManifest manifest =
            Engine::AuthoredSceneCache::buildManifest(settings.cache, fixture, settings.partition);

        Engine::AuthoredSceneCacheWriteResult write = profile.measure("cache write", [&]() {
            return Engine::AuthoredSceneCache::write(manifest, payload);
        });
        ctx.expect(write.status == Engine::AuthoredSceneCacheStatus::WriteSuccess, "profile cache write failed");

        Engine::AuthoredSceneCacheReadResult read = profile.measure("cache read", [&]() {
            return Engine::AuthoredSceneCache::read(manifest);
        });
        ctx.expect(read.status == Engine::AuthoredSceneCacheStatus::Hit && read.payload.has_value(), "profile cache read did not hit");

        Engine::AssetCache assetCache;
        Engine::PartitionedAuthoredSceneLoadResult load = profile.measure("payload commit", [&]() {
            return Engine::createPartitionedAuthoredSceneFromPayload(fixture, payload, assetCache, settings);
        });
        ctx.expect(load.success, "profile payload commit failed: " + load.message);
        if (!load.success) {
            return;
        }

        Engine::MainThreadWorkQueue work;
        Engine::FrameBudget budget;
        profile.measure("initial sector commit", [&]() {
            load.scene.updateStreaming(settings.initialCameraPosition, work);
            budget.beginFrame({5.0f, true});
            work.drain(budget);
        });
        ctx.expect(load.scene.diagnostics().loadedSectorCount == 1, "profile initial sector commit did not load one sector");

        load.scene.setAsyncDiagnostics("committed", 3, 3, 0, 0, 1.0f, 2.0f, 3.0f, "profile diagnostics");
        const Engine::AuthoredSceneDiagnosticsSummary summary =
            Engine::summarizeAuthoredSceneDiagnostics(load.scene.diagnostics());
        ctx.expect(summary.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Gltf, "summary source format was not glTF");
        ctx.expect(summary.sourceFormatName == "gltf", "summary source format name was wrong");
        ctx.expect(summary.importedNodeCount > 0, "summary missed imported node count");
        ctx.expect(summary.createdInstanceCount == 1, "summary created instance count was wrong");
        ctx.expect(summary.totalSectorCount == 2, "summary sector count was wrong");
        ctx.expect(summary.asyncPhase == "committed", "summary async phase was wrong");
        ctx.expect(summary.asyncImportMs == 2.0f, "summary async import timing was wrong");
        const std::string text = Engine::authoredSceneDiagnosticsSummaryText(load.scene.diagnostics());
        const std::string yaml = Engine::authoredSceneDiagnosticsSummaryYaml(load.scene.diagnostics());
        ctx.expect(text.find("imported nodes=") != std::string::npos, "summary text missed imported counts");
        ctx.expect(yaml.find("authored_scene:") != std::string::npos, "summary yaml missed root key");

        profile.fact("summary", text);
        profile.fact("summary_yaml_bytes", std::to_string(yaml.size()));
        profile.fact("texture_bytes", std::to_string(summary.textureEstimatedBytes));
        profile.fact("loaded_sectors", std::to_string(summary.loadedSectorCount));
        profile.fact("warnings", std::to_string(summary.warningCount));

        profile.measure("shutdown", [&]() {
            load.scene.shutdown();
        });
        expectNoLiveRendererResources(ctx, "profile authored scene");
        writeProfileReport(authoredReportPath("authored_scene_profile.txt"), profile);
    }

    void authoredSceneRepeatedLoadShutdownLeavesNoResources(TestContext& ctx)
    {
        for (uint32_t iteration = 0; iteration < 3; ++iteration) {
            TestRenderer::reset();
            Engine::AssetCache eagerCache;
            Engine::AuthoredSceneLoadSettings eagerSettings;
            eagerSettings.loadTextures = false;
            Engine::AuthoredSceneLoadResult eager =
                Engine::loadAuthoredScene(fixturePath(), eagerCache, eagerSettings);
            ctx.expect(eager.success, "repeated eager load failed: " + eager.message);
            if (eager.success) {
                eager.scene.shutdown();
            }
            expectNoLiveRendererResources(ctx, "repeated eager load");

            TestRenderer::reset();
            Engine::AssetCache streamingCache;
            Engine::AuthoredSceneStreamingSettings streamingSettings;
            streamingSettings.load.loadTextures = false;
            streamingSettings.partition.sectorSize = 25.0f;
            streamingSettings.initialCameraPosition = {45.0f, 2.0f, 0.0f};
            streamingSettings.loadRadius = 100.0f;
            Engine::PartitionedAuthoredSceneLoadResult streaming =
                Engine::loadPartitionedAuthoredScene(fixtureWithTwoMeshSectors(), streamingCache, streamingSettings);
            ctx.expect(streaming.success, "repeated partitioned load failed: " + streaming.message);
            if (streaming.success) {
                streaming.scene.shutdown();
            }
            expectNoLiveRendererResources(ctx, "repeated partitioned load");
        }
    }

    void authoredSceneAsyncPendingShutdownLeavesNoResources(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AsyncWorkQueue queue{1};
        Engine::AsyncJobHandle blocker = queue.submit("test blocker", [](std::stop_token stopToken) -> std::any {
            while (!stopToken.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return std::any{};
        });
        Engine::AsyncJobHandle pendingImport =
            Engine::enqueueAuthoredSceneImportAndPartition(queue, fixturePath(), {});
        queue.cancel(pendingImport);
        queue.cancel(blocker);
        queue.shutdown();
        expectNoLiveRendererResources(ctx, "pending async shutdown");
    }

    void authoredSceneMissingPathDoesNotCreateResources(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneLoadSettings settings;
        settings.loadTextures = false;
        Engine::AuthoredSceneLoadResult load =
            Engine::loadAuthoredScene("tests/assets/fixtures/missing_authored_scene.gltf", assetCache, settings);
        ctx.expect(!load.success, "missing authored path unexpectedly loaded");
        expectNoLiveRendererResources(ctx, "missing authored path");
    }

    void authoredSceneRejectsSkinnedAnimationRuntime(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;

        Engine::AuthoredSceneLoadSettings eagerSettings;
        eagerSettings.loadTextures = false;
        Engine::AuthoredSceneLoadResult eager =
            Engine::loadAuthoredScene(skinnedFixturePath(), assetCache, eagerSettings);
        ctx.expect(!eager.success, "static authored scene unexpectedly loaded skinned animation fixture");
        ctx.expect(
            eager.message.find("animated model runtime") != std::string::npos,
            "static authored scene rejection message did not mention animated model runtime");
        ctx.expect(eager.scene.diagnostics().importedSkinCount == 1, "eager rejection did not retain skin diagnostic count");
        ctx.expect(eager.scene.diagnostics().importedJointCount == 2, "eager rejection did not retain joint diagnostic count");
        ctx.expect(eager.scene.diagnostics().importedAnimationCount == 2, "eager rejection did not retain animation diagnostic count");
        expectNoLiveRendererResources(ctx, "eager skinned rejection");

        Engine::AuthoredSceneStreamingSettings streamingSettings;
        streamingSettings.load.loadTextures = false;
        streamingSettings.cache = cacheSettings("skinned_reject", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
        Engine::PartitionedAuthoredSceneLoadResult streaming =
            Engine::loadPartitionedAuthoredScene(skinnedFixturePath(), assetCache, streamingSettings);
        ctx.expect(!streaming.success, "partitioned authored scene unexpectedly loaded skinned animation fixture");
        ctx.expect(
            streaming.message.find("animated model runtime") != std::string::npos,
            "partitioned rejection message did not mention animated model runtime");
        ctx.expect(streaming.scene.diagnostics().cacheWriteCount == 0, "partitioned rejection should not write static authored cache");
        expectNoLiveRendererResources(ctx, "partitioned skinned rejection");

        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(skinnedFixturePath());
        ctx.expect(imported.success, "skinned fixture import failed before cache write rejection: " + imported.error);
        if (imported.success) {
            Engine::AuthoredSceneCachePayload payload;
            payload.scene = imported;
            payload.partition = Engine::partitionImportedAuthoredScene(imported, {});
            Engine::AuthoredSceneCacheSettings cache = cacheSettings("skinned_cache_write", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
            const Engine::AuthoredSceneCacheManifest manifest =
                Engine::AuthoredSceneCache::buildManifest(cache, skinnedFixturePath(), {});
            Engine::AuthoredSceneCacheWriteResult write = Engine::AuthoredSceneCache::write(manifest, payload);
            ctx.expect(write.status == Engine::AuthoredSceneCacheStatus::WriteFailed, "skinned static cache write did not fail");
            ctx.expect(
                write.message.find("animated model runtime") != std::string::npos,
                "skinned static cache write failure message did not mention animated model runtime");
        }
    }

    void authoredSceneAsyncSkinnedImportCommitsFailStaticRuntime(TestContext& ctx)
    {
        Engine::AsyncWorkQueue queue{1};
        Engine::enqueueAuthoredSceneImportAndPartition(queue, skinnedFixturePath(), {});
        std::optional<Engine::AuthoredSceneImportJobResult> import =
            waitForAsyncResult<Engine::AuthoredSceneImportJobResult>(queue, std::chrono::seconds(10));
        queue.shutdown();
        ctx.expect(import.has_value(), "async skinned import job did not complete");
        if (!import) {
            return;
        }
        ctx.expect(import->success, "async skinned import should succeed at CPU boundary: " + import->message);
        ctx.expect(import->payload.scene.diagnostics.skinCount == 1, "async skinned import lost skin diagnostics");
        ctx.expect(import->payload.scene.diagnostics.animationCount == 2, "async skinned import lost animation diagnostics");

        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        Engine::PartitionedAuthoredSceneLoadResult commit =
            Engine::createPartitionedAuthoredSceneFromPayload(
                skinnedFixturePath(),
                std::move(import->payload),
                assetCache,
                settings);
        ctx.expect(!commit.success, "static payload commit unexpectedly accepted skinned animation payload");
        ctx.expect(
            commit.message.find("animated model runtime") != std::string::npos,
            "static payload commit rejection message did not mention animated model runtime");
        expectNoLiveRendererResources(ctx, "async skinned static commit rejection");
    }

    void animatedModelLoadsAndOwnsBindPoseResources(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;

        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model load failed: " + result.message);
        if (!result.success) {
            return;
        }

        const Engine::AnimatedModelDiagnostics& diagnostics = result.model.diagnostics();
        ctx.expect(result.model.loaded(), "animated model did not report loaded");
        ctx.expect(result.model.bounds().valid, "animated model bounds were invalid");
        ctx.expect(result.model.jointCount() == 2, "animated model joint accessor count was wrong");
        ctx.expect(result.model.skinCount() == 1, "animated model skin accessor count was wrong");
        ctx.expect(result.model.animationClipCount() == 2, "animated model animation accessor count was wrong");
        ctx.expect(diagnostics.importedJointCount == 2, "animated model imported joint diagnostic was wrong");
        ctx.expect(diagnostics.importedSkinCount == 1, "animated model imported skin diagnostic was wrong");
        ctx.expect(diagnostics.importedAnimationCount == 2, "animated model imported animation diagnostic was wrong");
        ctx.expect(diagnostics.importedAnimationChannelCount == 4, "animated model imported channel diagnostic was wrong");
        ctx.expect(diagnostics.createdMaterialCount == 1, "animated model material count was wrong");
        ctx.expect(diagnostics.createdMeshCount == 1, "animated model mesh count was wrong");
        ctx.expect(diagnostics.createdSkinnedMeshCount == 1, "animated model skinned mesh count was wrong");
        ctx.expect(diagnostics.createdInstanceCount == 1, "animated model bind-pose instance count was wrong");
        ctx.expect(diagnostics.truncatedInfluenceVertexCount == 0, "fixture should not truncate influences");
        ctx.expect(diagnostics.zeroWeightVertexCount == 0, "fixture should not report zero-weight vertices");
        ctx.expect(TestRenderer::liveMaterialCount() == 1, "stub renderer material was not live for animated model");
        ctx.expect(TestRenderer::liveMeshCount() == 1, "stub renderer mesh was not live for animated model");
        ctx.expect(TestRenderer::liveSkinnedMeshCount() == 1, "stub renderer skinned mesh was not live for animated model");
        ctx.expect(TestRenderer::liveInstanceCount() == 1, "stub renderer instance was not live for animated model");
        ctx.expect(result.model.skinnedMeshCount() == 1, "animated model skinned mesh accessor count was wrong");

        const Assets::Assimp::ImportedScene& imported = result.model.importedScene();
        ctx.expect(!imported.meshes.empty() && !imported.meshes.front().primitives.empty(), "animated model lost imported mesh payload");
        if (!imported.meshes.empty() && !imported.meshes.front().primitives.empty()) {
            const Assets::Assimp::ImportedScenePrimitive& primitive = imported.meshes.front().primitives.front();
            ctx.expect(!primitive.vertices.empty() && !primitive.vertices.front().influences.empty(),
                "animated model lost CPU vertex influence payload");
        }
        ctx.expect(!imported.animations.empty() && !imported.animations.front().channels.empty(),
            "animated model lost CPU animation clip payload");

        const Renderer::SkinnedMeshDescriptor skinnedDescriptor = TestRenderer::firstSkinnedMeshDescriptor();
        ctx.expect(skinnedDescriptor.jointCount == 2, "skinned descriptor joint count was wrong");
        ctx.expect(skinnedDescriptor.maxInfluencesPerVertex == 1, "skinned descriptor max influence count was wrong");
        ctx.expect(skinnedDescriptor.submeshes.size() == 1, "skinned descriptor submesh count was wrong");
        if (!skinnedDescriptor.submeshes.empty()) {
            const Renderer::SkinnedSubmeshDescriptor& submesh = skinnedDescriptor.submeshes.front();
            ctx.expect(submesh.vertices.size() == 3, "skinned descriptor vertex count was wrong");
            ctx.expect(submesh.indices.size() == 3, "skinned descriptor index count was wrong");
            if (submesh.vertices.size() >= 3) {
                ctx.expect(submesh.vertices[0].joints[0] == 0 && nearlyEqual(submesh.vertices[0].weights[0], 1.0f),
                    "skinned descriptor vertex 0 influence was wrong");
                ctx.expect(submesh.vertices[2].joints[0] == 1 && nearlyEqual(submesh.vertices[2].weights[0], 1.0f),
                    "skinned descriptor vertex 2 influence was wrong");
            }
        }

        const std::optional<Engine::AnimatedModelInstance> instance = result.model.bindPoseInstance(0);
        ctx.expect(instance.has_value(), "animated model did not expose bind-pose instance record");
        if (instance) {
            ctx.expect(instance->nodeIndex < imported.nodes.size(), "bind-pose instance node index was invalid");
            const glm::mat4 rendererTransform = TestRenderer::instanceTransform(instance->handle);
            ctx.expect(nearlyEqual(translationOf(rendererTransform), translationOf(imported.nodes[instance->nodeIndex].worldTransform)),
                "bind-pose instance transform did not match imported node world transform");
        }

        result.model.shutdown();
        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model load shutdown");
    }

    void animatedModelLoadRejectsNonAnimatedAndMissingAssets(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;

        Engine::AnimatedModelLoadResult staticLoad = Engine::loadAnimatedModel(fixturePath(), assetCache, settings);
        ctx.expect(!staticLoad.success, "static authored fixture unexpectedly loaded as animated model");
        ctx.expect(
            staticLoad.message.find("not an animated model") != std::string::npos,
            "static fixture rejection did not identify non-animated model");
        expectNoLiveRendererResources(ctx, "animated model static rejection");

        Engine::AnimatedModelLoadResult missingLoad =
            Engine::loadAnimatedModel("tests/assets/fixtures/missing_skinned_animation_fixture.gltf", assetCache, settings);
        ctx.expect(!missingLoad.success, "missing animated model path unexpectedly loaded");
        expectNoLiveRendererResources(ctx, "animated model missing path");
    }

    void animatedModelCanSkipBindPoseInstances(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        settings.createBindPoseInstances = false;

        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model no-instance load failed: " + result.message);
        if (!result.success) {
            return;
        }
        ctx.expect(result.model.diagnostics().createdMeshCount == 1, "animated model no-instance mesh count was wrong");
        ctx.expect(result.model.diagnostics().createdSkinnedMeshCount == 1, "animated model no-instance skinned mesh count was wrong");
        ctx.expect(result.model.diagnostics().createdInstanceCount == 0, "animated model created bind-pose instances despite setting");
        ctx.expect(result.model.bindPoseInstanceCount() == 0, "animated model bind-pose instance accessor was wrong");
        ctx.expect(TestRenderer::liveMeshCount() == 1, "animated model no-instance mesh was not live");
        ctx.expect(TestRenderer::liveSkinnedMeshCount() == 1, "animated model no-instance skinned mesh was not live");
        ctx.expect(TestRenderer::liveInstanceCount() == 0, "animated model no-instance path created renderer instances");

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model no-instance shutdown");
    }

    void animatedModelCanSkipSkinnedMeshes(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        settings.createSkinnedMeshes = false;

        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model no-skinned-mesh load failed: " + result.message);
        if (!result.success) {
            return;
        }
        ctx.expect(result.model.diagnostics().createdMeshCount == 1, "animated model no-skinned-mesh static preview count was wrong");
        ctx.expect(result.model.diagnostics().createdSkinnedMeshCount == 0, "animated model created skinned meshes despite setting");
        ctx.expect(result.model.skinnedMeshCount() == 0, "animated model skinned mesh accessor was wrong when disabled");
        ctx.expect(TestRenderer::liveMeshCount() == 1, "animated model no-skinned-mesh static mesh was not live");
        ctx.expect(TestRenderer::liveSkinnedMeshCount() == 0, "animated model no-skinned-mesh path created renderer skinned meshes");

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model no-skinned-mesh shutdown");
    }

    void animatedModelCreatesSkinnedInstancesAndUpdatesPose(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        settings.createSkinnedInstances = true;

        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model skinned-instance load failed: " + result.message);
        if (!result.success) {
            return;
        }

        ctx.expect(result.model.diagnostics().createdSkinnedInstanceCount == 1, "animated model skinned-instance diagnostic count was wrong");
        ctx.expect(result.model.skinnedInstanceCount() == 1, "animated model skinned instance accessor count was wrong");
        ctx.expect(TestRenderer::liveSkinnedInstanceCount() == 1, "stub renderer skinned instance was not live");
        const std::optional<Engine::AnimatedModelSkinnedInstance> instance = result.model.skinnedInstance(0);
        ctx.expect(instance.has_value(), "animated model did not expose skinned instance record");
        if (instance) {
            const std::vector<glm::mat4> bindPalette = TestRenderer::skinnedInstanceJointMatrices(instance->handle);
            ctx.expect(bindPalette.size() == 2, "bind-pose skinned instance palette size was wrong");
            const glm::mat4 rendererTransform = TestRenderer::skinnedInstanceTransform(instance->handle);
            const Assets::Assimp::ImportedScene& imported = result.model.importedScene();
            ctx.expect(instance->nodeIndex < imported.nodes.size(), "skinned instance node index was invalid");
            if (instance->nodeIndex < imported.nodes.size()) {
                ctx.expect(nearlyEqual(translationOf(rendererTransform), translationOf(imported.nodes[instance->nodeIndex].worldTransform)),
                    "skinned instance transform did not match imported node world transform");
            }

            Engine::AnimatedSkeletonPose sampled = result.model.sampleClip(0, 0.5f);
            ctx.expect(sampled.diagnostics.valid, "sampled pose for skinned instance update was invalid");
            const bool updated = result.model.updateSkinnedPose(0, sampled);
            ctx.expect(updated, "animated model did not update skinned pose");
            const std::vector<glm::mat4> sampledPalette = TestRenderer::skinnedInstanceJointMatrices(instance->handle);
            ctx.expect(sampledPalette.size() == 2, "sampled skinned instance palette size was wrong");
            if (bindPalette.size() == 2 && sampledPalette.size() == 2) {
                ctx.expect(!nearlyEqual(bindPalette[0], sampledPalette[0]) || !nearlyEqual(bindPalette[1], sampledPalette[1]),
                    "sampled skinned pose did not change the uploaded palette");
            }
        }

        Renderer::SkinnedInstanceDiagnostics diagnostics = instance
            ? Renderer::skinnedInstanceDiagnostics(instance->handle)
            : Renderer::SkinnedInstanceDiagnostics{};
        ctx.expect(diagnostics.valid, "skinned instance diagnostics were invalid");
        ctx.expect(diagnostics.submittedJointCount == 2, "skinned instance diagnostics joint count was wrong");
        ctx.expect(diagnostics.truncatedJointCount == 0, "skinned fixture should not truncate joint palette");

        result.model.shutdown();
        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model skinned-instance shutdown");
    }

    void animatedModelPlaybackLoopUpdatesSkinnedPalettes(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        settings.createBindPoseInstances = false;
        settings.createSkinnedInstances = true;

        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model playback loop load failed: " + result.message);
        if (!result.success) {
            return;
        }

        Engine::AnimationPlaybackState playback;
        playback.clipIndex = 0;
        playback.timeSeconds = 0.0f;
        playback.speed = 1.0f;
        playback.loop = true;
        playback.playing = true;

        const std::optional<Engine::AnimatedModelSkinnedInstance> instance = result.model.skinnedInstance(0);
        ctx.expect(instance.has_value(), "playback loop test did not expose skinned instance");
        std::vector<glm::mat4> previousPalette = instance
            ? TestRenderer::skinnedInstanceJointMatrices(instance->handle)
            : std::vector<glm::mat4>{};

        uint32_t successfulUpdates = 0;
        for (uint32_t frame = 0; frame < 3; ++frame) {
            result.model.advancePlayback(playback, 0.25f);
            Engine::AnimatedSkeletonPose pose = result.model.sampleClip(playback.clipIndex, playback.timeSeconds);
            ctx.expect(pose.diagnostics.valid, "playback loop sampled invalid pose");
            if (result.model.updateSkinnedPose(0, pose)) {
                ++successfulUpdates;
            }
        }
        ctx.expect(successfulUpdates == 3, "playback loop did not update every sampled pose");
        if (instance) {
            const std::vector<glm::mat4> currentPalette = TestRenderer::skinnedInstanceJointMatrices(instance->handle);
            ctx.expect(currentPalette.size() == 2, "playback loop palette size was wrong");
            if (previousPalette.size() == 2 && currentPalette.size() == 2) {
                ctx.expect(!nearlyEqual(previousPalette[0], currentPalette[0]) || !nearlyEqual(previousPalette[1], currentPalette[1]),
                    "playback loop did not change uploaded palette");
            }
        }

        playback.clipIndex = 99;
        if (playback.clipIndex >= result.model.clipCount()) {
            playback.clipIndex = 0;
            playback.timeSeconds = 0.0f;
        }
        ctx.expect(playback.clipIndex == 0 && nearlyEqual(playback.timeSeconds, 0.0f),
            "playback loop did not clamp invalid clip index");

        playback.playing = false;
        playback.timeSeconds = 0.35f;
        result.model.advancePlayback(playback, 0.5f);
        ctx.expect(nearlyEqual(playback.timeSeconds, 0.35f), "paused playback loop advanced time");

        result.model.shutdown();
        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model playback loop shutdown");
    }

    void animatedModelCacheRoundTripsPayload(TestContext& ctx)
    {
        Engine::AnimatedModelCacheSettings settings =
            animatedCacheSettings("roundtrip", Engine::AnimatedModelCachePolicy::GenerateOnMiss);
        Engine::AnimatedModelCacheManifest manifest =
            Engine::AnimatedModelCache::buildManifest(settings, skinnedFixturePath());
        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(skinnedFixturePath());
        ctx.expect(imported.success, "skinned fixture import failed before animated cache write");

        Engine::AnimatedModelCachePayload payload;
        payload.scene = imported;
        Engine::AnimatedModelCacheWriteResult write = Engine::AnimatedModelCache::write(manifest, payload);
        ctx.expect(write.status == Engine::AnimatedModelCacheStatus::WriteSuccess, "animated cache write failed: " + write.message);

        Engine::AnimatedModelCacheReadResult read = Engine::AnimatedModelCache::read(manifest);
        ctx.expect(read.status == Engine::AnimatedModelCacheStatus::Hit, "animated cache read did not hit: " + read.message);
        ctx.expect(read.payload.has_value(), "animated cache hit did not return payload");
        if (read.payload) {
            const Assets::Assimp::ImportedScene& scene = read.payload->scene;
            ctx.expect(scene.sourceFormat == imported.sourceFormat, "animated cache source format did not round-trip");
            ctx.expect(scene.nodes.size() == imported.nodes.size(), "animated cache node count did not round-trip");
            ctx.expect(scene.meshes.size() == imported.meshes.size(), "animated cache mesh count did not round-trip");
            ctx.expect(scene.joints.size() == imported.joints.size(), "animated cache joint count did not round-trip");
            ctx.expect(scene.skins.size() == imported.skins.size(), "animated cache skin count did not round-trip");
            ctx.expect(scene.animations.size() == imported.animations.size(), "animated cache clip count did not round-trip");
            ctx.expect(!scene.meshes.empty() && !scene.meshes[0].primitives.empty(), "animated cache mesh primitives missing");
            ctx.expect(!scene.animations.empty() && scene.animations[0].channels.size() == imported.animations[0].channels.size(),
                "animated cache animation channels did not round-trip");
        }
    }

    void animatedModelCacheHandlesMissStaleAndCorrupt(TestContext& ctx)
    {
        Engine::AnimatedModelCacheSettings settings =
            animatedCacheSettings("miss_stale_corrupt", Engine::AnimatedModelCachePolicy::GenerateOnMiss);
        Engine::AnimatedModelCacheManifest manifest =
            Engine::AnimatedModelCache::buildManifest(settings, skinnedFixturePath());
        Engine::AnimatedModelCacheReadResult miss = Engine::AnimatedModelCache::read(manifest);
        ctx.expect(miss.status == Engine::AnimatedModelCacheStatus::Miss, "missing animated cache was not a miss");

        Assets::Assimp::ImportedScene imported = Assets::Assimp::importScene(skinnedFixturePath());
        Engine::AnimatedModelCachePayload payload;
        payload.scene = std::move(imported);
        Engine::AnimatedModelCacheWriteResult write = Engine::AnimatedModelCache::write(manifest, payload);
        ctx.expect(write.status == Engine::AnimatedModelCacheStatus::WriteSuccess, "animated cache setup write failed");

        Engine::AnimatedModelCacheManifest staleManifest = manifest;
        staleManifest.sourceHash = "different";
        Engine::AnimatedModelCacheReadResult stale = Engine::AnimatedModelCache::read(staleManifest);
        ctx.expect(stale.status == Engine::AnimatedModelCacheStatus::Stale, "animated cache identity mismatch was not stale");

        {
            std::ofstream corrupt(Engine::AnimatedModelCache::cacheRoot(manifest) / "model.yaml", std::ios::trunc);
            corrupt << "not: [valid";
        }
        Engine::AnimatedModelCacheReadResult corrupt = Engine::AnimatedModelCache::read(manifest);
        ctx.expect(corrupt.status == Engine::AnimatedModelCacheStatus::Corrupt, "truncated animated cache was not corrupt");
    }

    void animatedModelLoadsFromCacheAndCommitsPayload(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        settings.createBindPoseInstances = false;
        settings.createSkinnedInstances = true;
        settings.cache = animatedCacheSettings("load_from_cache", Engine::AnimatedModelCachePolicy::GenerateOnMiss);

        Engine::AnimatedModelLoadResult generated = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(generated.success, "animated model generate-on-miss load failed: " + generated.message);
        ctx.expect(generated.model.diagnostics().cacheWriteCount == 1, "animated cache was not written on generate-on-miss");
        generated.model.shutdown();
        assetCache.shutdown();
        expectNoLiveRendererResources(ctx, "animated model cache generate");

        TestRenderer::reset();
        Engine::AssetCache cachedAssetCache;
        settings.cache.policy = Engine::AnimatedModelCachePolicy::ReadOnly;
        Engine::AnimatedModelLoadResult cached = Engine::loadAnimatedModel(skinnedFixturePath(), cachedAssetCache, settings);
        ctx.expect(cached.success, "animated model cache-hit load failed: " + cached.message);
        ctx.expect(cached.model.diagnostics().loadedFromCache, "animated model did not report cache hit");
        ctx.expect(cached.model.diagnostics().createdSkinnedMeshCount == 1, "cached animated model skinned mesh count was wrong");
        ctx.expect(cached.model.skinnedInstanceCount() == 1, "cached animated model skinned instance count was wrong");
        cached.model.shutdown();
        cachedAssetCache.shutdown();
        expectNoLiveRendererResources(ctx, "animated model cache hit");
    }

    void animatedModelAsyncJobsProduceCommittablePayloads(TestContext& ctx)
    {
        Engine::AsyncWorkQueue queue(1);
        Engine::AnimatedModelCacheSettings settings =
            animatedCacheSettings("async_jobs", Engine::AnimatedModelCachePolicy::GenerateOnMiss);
        Engine::AnimatedModelCacheManifest manifest =
            Engine::AnimatedModelCache::buildManifest(settings, skinnedFixturePath());

        Engine::enqueueAnimatedModelImportAndPreparePayload(queue, skinnedFixturePath());
        std::optional<Engine::AnimatedModelImportJobResult> imported =
            waitForAsyncResult<Engine::AnimatedModelImportJobResult>(queue, std::chrono::seconds(10));
        ctx.expect(imported.has_value(), "animated async import job did not complete");
        ctx.expect(imported && imported->success, "animated async import job failed");

        Engine::AnimatedModelCachePayload payloadForCommit = imported ? imported->payload : Engine::AnimatedModelCachePayload{};
        Engine::AnimatedModelCachePayload payloadForWrite = payloadForCommit;
        Engine::enqueueAnimatedModelCacheWrite(queue, manifest, std::move(payloadForWrite));
        std::optional<Engine::AnimatedModelCacheWriteJobResult> write =
            waitForAsyncResult<Engine::AnimatedModelCacheWriteJobResult>(queue, std::chrono::seconds(10));
        ctx.expect(write.has_value(), "animated async cache write job did not complete");
        ctx.expect(write && write->result.status == Engine::AnimatedModelCacheStatus::WriteSuccess,
            "animated async cache write job failed");

        Engine::enqueueAnimatedModelCacheRead(queue, manifest);
        std::optional<Engine::AnimatedModelCacheReadJobResult> read =
            waitForAsyncResult<Engine::AnimatedModelCacheReadJobResult>(queue, std::chrono::seconds(10));
        ctx.expect(read.has_value(), "animated async cache read job did not complete");
        ctx.expect(read && read->result.status == Engine::AnimatedModelCacheStatus::Hit && read->result.payload,
            "animated async cache read did not hit");

        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings loadSettings;
        loadSettings.loadTextures = false;
        loadSettings.createBindPoseInstances = false;
        loadSettings.createSkinnedInstances = true;
        Engine::AnimatedModelLoadResult committed = Engine::createAnimatedModelFromPayload(
            skinnedFixturePath(),
            std::move(payloadForCommit),
            assetCache,
            loadSettings);
        ctx.expect(committed.success, "animated async payload could not be committed: " + committed.message);
        ctx.expect(committed.model.skinnedInstanceCount() == 1, "animated async payload committed wrong instance count");
        committed.model.shutdown();
        assetCache.shutdown();
        queue.shutdown();
        expectNoLiveRendererResources(ctx, "animated async payload commit");
    }

    void animatedModelDiagnosticsSummaryAndProfileReport(TestContext& ctx)
    {
        TestRenderer::reset();
        ProfileReport profile;

        Assets::Assimp::ImportedScene imported = profile.measure("animated source import", [&]() {
            return Assets::Assimp::importScene(skinnedFixturePath());
        });
        ctx.expect(imported.success, "animated profile source import failed");

        Engine::AnimatedModelCacheSettings cache =
            animatedCacheSettings("profile", Engine::AnimatedModelCachePolicy::GenerateOnMiss);
        Engine::AnimatedModelCacheManifest manifest =
            Engine::AnimatedModelCache::buildManifest(cache, skinnedFixturePath());
        Engine::AnimatedModelCachePayload payload;
        payload.scene = imported;

        Engine::AnimatedModelCacheWriteResult write = profile.measure("animated cache write", [&]() {
            return Engine::AnimatedModelCache::write(manifest, payload);
        });
        ctx.expect(write.status == Engine::AnimatedModelCacheStatus::WriteSuccess, "animated profile cache write failed");

        Engine::AnimatedModelCacheReadResult read = profile.measure("animated cache read", [&]() {
            return Engine::AnimatedModelCache::read(manifest);
        });
        ctx.expect(read.status == Engine::AnimatedModelCacheStatus::Hit && read.payload, "animated profile cache read failed");
        if (!read.payload) {
            return;
        }

        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        settings.createBindPoseInstances = false;
        settings.createSkinnedInstances = true;
        Engine::AnimatedModelLoadResult load = profile.measure("animated payload commit", [&]() {
            return Engine::createAnimatedModelFromPayload(
                skinnedFixturePath(),
                std::move(*read.payload),
                assetCache,
                settings);
        });
        ctx.expect(load.success, "animated profile payload commit failed: " + load.message);

        Engine::AnimatedSkeletonPose bindPose = profile.measure("bind pose sample", [&]() {
            return load.model.bindPose();
        });
        ctx.expect(bindPose.diagnostics.valid, "animated profile bind pose was invalid");
        Engine::AnimatedSkeletonPose sampled = profile.measure("clip sample", [&]() {
            return load.model.sampleClip(0, 0.5f);
        });
        ctx.expect(sampled.diagnostics.valid, "animated profile clip sample was invalid");

        Engine::AnimationPlaybackState playback;
        playback.clipIndex = 0;
        Engine::AnimationCrossfadeState crossfade = Engine::beginCrossfade(playback, 1, 0.25f);
        Engine::AnimatedSkeletonPose blended = profile.measure("crossfade sample", [&]() {
            return Engine::advanceCrossfade(load.model, crossfade, playback, 0.125f);
        });
        ctx.expect(blended.diagnostics.valid, "animated profile crossfade sample was invalid");

        const bool uploaded = profile.measure("pose upload", [&]() {
            return load.model.updateSkinnedPose(0, sampled);
        });
        ctx.expect(uploaded, "animated profile pose upload failed");

        const Engine::AnimatedModelDiagnosticsSummary summary =
            Engine::summarizeAnimatedModelDiagnostics(load.model.diagnostics());
        ctx.expect(summary.sourceFormatName == "gltf", "animated summary source format was wrong");
        ctx.expect(summary.importedJointCount == 2, "animated summary joint count was wrong");
        ctx.expect(summary.importedSkinCount == 1, "animated summary skin count was wrong");
        ctx.expect(summary.importedAnimationCount == 2, "animated summary clip count was wrong");
        ctx.expect(summary.createdSkinnedMeshCount == 1, "animated summary skinned mesh count was wrong");
        ctx.expect(summary.createdSkinnedInstanceCount == 1, "animated summary skinned instance count was wrong");
        ctx.expect(summary.boundsValid, "animated summary bounds were invalid");

        const std::string text = Engine::animatedModelDiagnosticsSummaryText(load.model.diagnostics());
        const std::string yaml = Engine::animatedModelDiagnosticsSummaryYaml(load.model.diagnostics());
        ctx.expect(text.find("sourceFormat=gltf") != std::string::npos, "animated summary text omitted source format");
        ctx.expect(text.find("skinnedMeshes=1") != std::string::npos, "animated summary text omitted skinned meshes");
        ctx.expect(yaml.find("animated_model:") != std::string::npos, "animated summary YAML omitted root");
        ctx.expect(yaml.find("skinned_meshes: 1") != std::string::npos, "animated summary YAML omitted skinned meshes");

        profile.fact("summary", text);
        profile.fact("summary_yaml_size", std::to_string(yaml.size()));
        profile.fact("clip_count", std::to_string(load.model.clipCount()));
        profile.fact("joint_count", std::to_string(load.model.jointCount()));
        profile.fact("skinned_instances", std::to_string(load.model.skinnedInstanceCount()));
        profile.measure("animated shutdown", [&]() {
            load.model.shutdown();
            assetCache.shutdown();
        });
        writeProfileReport(authoredReportPath("animated_model_profile.txt"), profile);
        expectNoLiveRendererResources(ctx, "animated profile");
    }

    void animatedModelRepeatedLoadShutdownLeavesNoResources(TestContext& ctx)
    {
        for (uint32_t iteration = 0; iteration < 3; ++iteration) {
            TestRenderer::reset();
            Engine::AssetCache assetCache;
            Engine::AnimatedModelLoadSettings settings;
            settings.loadTextures = false;
            settings.createBindPoseInstances = false;
            settings.createSkinnedInstances = true;
            Engine::AnimatedModelLoadResult load = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
            ctx.expect(load.success, "repeated animated uncached load failed");
            if (load.success) {
                Engine::AnimatedSkeletonPose pose = load.model.sampleClip(0, 0.25f);
                ctx.expect(pose.diagnostics.valid, "repeated animated pose sample failed");
                ctx.expect(load.model.updateSkinnedPose(0, pose), "repeated animated pose upload failed");
            }
            load.model.shutdown();
            load.model.shutdown();
            assetCache.shutdown();
            expectNoLiveRendererResources(ctx, "repeated animated uncached load");
        }

        Engine::AnimatedModelCacheSettings cache =
            animatedCacheSettings("repeated_cache", Engine::AnimatedModelCachePolicy::GenerateOnMiss);
        {
            TestRenderer::reset();
            Engine::AssetCache assetCache;
            Engine::AnimatedModelLoadSettings settings;
            settings.loadTextures = false;
            settings.createBindPoseInstances = false;
            settings.createSkinnedInstances = true;
            settings.cache = cache;
            Engine::AnimatedModelLoadResult generated = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
            ctx.expect(generated.success, "repeated animated cache seed failed");
            generated.model.shutdown();
            assetCache.shutdown();
            expectNoLiveRendererResources(ctx, "repeated animated cache seed");
        }

        for (uint32_t iteration = 0; iteration < 2; ++iteration) {
            TestRenderer::reset();
            Engine::AssetCache assetCache;
            Engine::AnimatedModelLoadSettings settings;
            settings.loadTextures = false;
            settings.createBindPoseInstances = false;
            settings.createSkinnedInstances = true;
            settings.cache = cache;
            settings.cache.policy = Engine::AnimatedModelCachePolicy::ReadOnly;
            Engine::AnimatedModelLoadResult load = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
            ctx.expect(load.success, "repeated animated cache-hit load failed");
            ctx.expect(load.model.diagnostics().loadedFromCache, "repeated animated load did not use cache");
            load.model.shutdown();
            assetCache.shutdown();
            expectNoLiveRendererResources(ctx, "repeated animated cache-hit load");
        }
    }

    void animatedModelAsyncPendingShutdownLeavesNoResources(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AsyncWorkQueue queue(1);
        Engine::AsyncJobHandle handle = Engine::enqueueAnimatedModelImportAndPreparePayload(queue, skinnedFixturePath());
        if (handle.id != UINT64_MAX) {
            queue.cancel(handle);
        }
        queue.shutdown();
        expectNoLiveRendererResources(ctx, "animated pending async shutdown");
    }

    void rendererSkinnedMeshDiagnosticsAndLifetime(TestContext& ctx)
    {
        TestRenderer::reset();

        Renderer::MaterialHandle material = Renderer::createMaterial({});
        Renderer::SkinnedMeshVertex vertex0{};
        vertex0.weights[0] = 1.0f;
        Renderer::SkinnedMeshVertex vertex1{};
        vertex1.px = 1.0f;
        vertex1.joints[0] = 1;
        vertex1.weights[0] = 1.0f;
        Renderer::SkinnedMeshVertex vertex2{};
        vertex2.py = 1.0f;

        Renderer::SkinnedSubmeshDescriptor submesh;
        submesh.material = material;
        submesh.vertices = {vertex0, vertex1, vertex2};
        submesh.indices = {0, 1, 2};
        submesh.skinIndex = 0;

        Renderer::SkinnedMeshDescriptor descriptor;
        descriptor.name = "diagnostic-skinned-mesh";
        descriptor.submeshes.push_back(submesh);
        descriptor.jointCount = 2;
        descriptor.maxInfluencesPerVertex = 4;
        descriptor.truncatedInfluenceVertexCount = 1;
        descriptor.zeroWeightVertexCount = 1;
        descriptor.normalizedWeightVertexCount = 1;

        Renderer::SkinnedMeshHandle empty = Renderer::createSkinnedMesh({});
        ctx.expect(empty.id == UINT32_MAX, "empty skinned descriptor unexpectedly created a handle");
        ctx.expect(!Renderer::skinnedMeshDiagnostics({}).valid, "invalid skinned mesh diagnostics unexpectedly reported valid");

        Renderer::SkinnedMeshHandle mesh = Renderer::createSkinnedMesh(descriptor);
        ctx.expect(mesh.id != UINT32_MAX, "valid skinned descriptor did not create a handle");
        Renderer::SkinnedMeshInstanceHandle instance = Renderer::createSkinnedInstance(mesh);
        ctx.expect(instance.id != UINT32_MAX, "valid skinned mesh did not create skinned instance");

        std::vector<glm::mat4> palette(Renderer::MaxSkinnedJointsPerMesh + 2, glm::mat4{1.0f});
        Renderer::setSkinnedInstanceJointMatrices(instance, palette);
        Renderer::SkinnedInstanceDiagnostics instanceDiagnostics = Renderer::skinnedInstanceDiagnostics(instance);
        ctx.expect(instanceDiagnostics.valid, "skinned instance diagnostics were invalid");
        ctx.expect(instanceDiagnostics.submittedJointCount == Renderer::MaxSkinnedJointsPerMesh,
            "skinned instance diagnostics submitted joint count was wrong");
        ctx.expect(instanceDiagnostics.truncatedJointCount == 2, "skinned instance diagnostics truncation count was wrong");

        Renderer::SkinnedMeshDiagnostics diagnostics = Renderer::skinnedMeshDiagnostics(mesh);
        ctx.expect(diagnostics.valid, "skinned mesh diagnostics were invalid");
        ctx.expect(diagnostics.name == descriptor.name, "skinned mesh diagnostics name was wrong");
        ctx.expect(diagnostics.submeshCount == 1, "skinned mesh diagnostics submesh count was wrong");
        ctx.expect(diagnostics.vertexCount == 3, "skinned mesh diagnostics vertex count was wrong");
        ctx.expect(diagnostics.indexCount == 3, "skinned mesh diagnostics index count was wrong");
        ctx.expect(diagnostics.jointCount == 2, "skinned mesh diagnostics joint count was wrong");
        ctx.expect(diagnostics.maxInfluenceCount == 4, "skinned mesh diagnostics max influence count was wrong");
        ctx.expect(diagnostics.truncatedInfluenceVertexCount == 1, "skinned mesh diagnostics truncation count was wrong");
        ctx.expect(diagnostics.zeroWeightVertexCount == 1, "skinned mesh diagnostics zero-weight count was wrong");
        ctx.expect(diagnostics.normalizedWeightVertexCount == 1, "skinned mesh diagnostics normalization count was wrong");
        ctx.expect(diagnostics.validMaterialReferenceCount == 1, "skinned mesh diagnostics material validity count was wrong");

        Renderer::destroySkinnedInstance(instance);
        Renderer::destroySkinnedInstance(instance);
        Renderer::destroySkinnedMesh(mesh);
        Renderer::destroySkinnedMesh(mesh);
        Renderer::destroyMaterial(material);
        expectNoLiveRendererResources(ctx, "renderer skinned mesh diagnostics");
    }

    void animatedModelEvaluatesBindPoseAndClipSamples(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model pose test load failed: " + result.message);
        if (!result.success) {
            return;
        }

        Engine::AnimatedSkeletonPose bindPose = result.model.bindPose();
        ctx.expect(bindPose.diagnostics.valid, "bind pose diagnostics were invalid");
        ctx.expect(bindPose.joints.size() == 2, "bind pose joint count was wrong");
        if (bindPose.joints.size() == 2) {
            ctx.expect(nearlyEqual(translationOf(bindPose.joints[0].modelTransform), {0.0f, 0.0f, 0.0f}),
                "bind pose root transform was wrong");
            ctx.expect(nearlyEqual(translationOf(bindPose.joints[1].modelTransform), {0.0f, 1.0f, 0.0f}),
                "bind pose child transform was wrong");
            const glm::mat4 expectedFinal = bindPose.joints[1].modelTransform * bindPose.joints[1].inverseBindMatrix;
            ctx.expect(nearlyEqual(bindPose.joints[1].finalSkinningMatrix, expectedFinal),
                "bind pose final skinning matrix was not model * inverseBind");
            ctx.expect(bindPose.jointModelTransform(0) != nullptr, "bind pose joint model transform accessor failed");
            ctx.expect(bindPose.finalSkinningMatrix(0) != nullptr, "bind pose final matrix accessor failed");
            ctx.expect(bindPose.jointModelTransform(99) == nullptr, "bind pose invalid model transform accessor did not return null");
        }

        Engine::AnimationSampleSettings nonLooping;
        nonLooping.loop = false;
        Engine::AnimatedSkeletonPose start = result.model.sampleClip(0, 0.0f, nonLooping);
        Engine::AnimatedSkeletonPose middle = result.model.sampleClip(0, 0.5f, nonLooping);
        Engine::AnimatedSkeletonPose end = result.model.sampleClip(0, 1.0f, nonLooping);
        ctx.expect(start.diagnostics.valid, "start sample diagnostics were invalid");
        ctx.expect(middle.diagnostics.valid, "middle sample diagnostics were invalid");
        ctx.expect(end.diagnostics.valid, "end sample diagnostics were invalid");
        if (middle.joints.size() == 2 && end.joints.size() == 2) {
            ctx.expect(nearlyEqual(translationOf(middle.joints[0].modelTransform), {0.0f, 0.25f, 0.0f}),
                "middle sample root translation was wrong");
            ctx.expect(nearlyEqual(translationOf(end.joints[0].modelTransform), {0.0f, 0.5f, 0.0f}),
                "end sample root translation was wrong");
            ctx.expect(nearlyEqual(translationOf(middle.joints[1].modelTransform), {0.0f, 1.25f, 0.0f}),
                "middle sample child model transform did not combine hierarchy");
            const glm::vec3 rotatedX = glm::vec3{middle.joints[1].modelTransform * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}};
            ctx.expect(nearlyEqual(rotatedX.x, 0.7071f, 0.001f) && nearlyEqual(rotatedX.y, 0.7071f, 0.001f),
                "middle sample rotation was not halfway around Z");
        }

        Engine::AnimatedSkeletonPose looped = result.model.sampleClip(0, 1.25f);
        ctx.expect(nearlyEqual(looped.diagnostics.sampledTimeSeconds, 0.25f), "looped sample time did not wrap");
        Engine::AnimatedSkeletonPose clamped = result.model.sampleClip(0, 2.0f, nonLooping);
        ctx.expect(nearlyEqual(clamped.diagnostics.sampledTimeSeconds, 1.0f), "non-looped sample time did not clamp");

        Engine::AnimatedSkeletonPose invalid = result.model.sampleClip(99, 0.0f);
        ctx.expect(!invalid.diagnostics.valid, "invalid clip sample unexpectedly reported valid");
        ctx.expect(!invalid.diagnostics.warnings.empty(), "invalid clip sample did not report warning");

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model pose test shutdown");
    }

    void animatedModelAdvancesPlaybackState(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model playback test load failed: " + result.message);
        if (!result.success) {
            return;
        }

        ctx.expect(result.model.clipCount() == 2, "animated model clip count was wrong");
        ctx.expect(nearlyEqual(result.model.clipDuration(0), 1.0f), "animated model clip duration was wrong");

        Engine::AnimationPlaybackState state;
        state.clipIndex = 0;
        state.timeSeconds = 0.75f;
        state.speed = 1.0f;
        state.loop = true;
        state.playing = true;
        result.model.advancePlayback(state, 0.5f);
        ctx.expect(nearlyEqual(state.timeSeconds, 0.25f), "looping playback did not wrap");

        state.timeSeconds = 0.75f;
        state.speed = 2.0f;
        state.loop = false;
        result.model.advancePlayback(state, 0.5f);
        ctx.expect(nearlyEqual(state.timeSeconds, 1.0f), "non-looping playback did not clamp");

        state.timeSeconds = 0.2f;
        state.playing = false;
        result.model.advancePlayback(state, 0.5f);
        ctx.expect(nearlyEqual(state.timeSeconds, 0.2f), "paused playback advanced time");

        state.clipIndex = 99;
        state.playing = true;
        result.model.advancePlayback(state, 0.5f);
        ctx.expect(nearlyEqual(state.timeSeconds, 0.2f), "invalid clip playback advanced time");

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model playback test shutdown");
    }

    void animatedModelBlendsSkeletonPoses(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model blend test load failed: " + result.message);
        if (!result.success) {
            return;
        }

        Engine::AnimationSampleSettings nonLooping;
        nonLooping.loop = false;
        Engine::AnimatedSkeletonPose source = result.model.sampleClip(0, 0.5f, nonLooping);
        Engine::AnimatedSkeletonPose target = result.model.sampleClip(1, 0.5f, nonLooping);
        ctx.expect(source.diagnostics.valid, "source blend pose was invalid");
        ctx.expect(target.diagnostics.valid, "target blend pose was invalid");

        Engine::AnimatedSkeletonPose weightZero = Engine::blendSkeletonPoses(source, target, 0.0f);
        Engine::AnimatedSkeletonPose weightHalf = Engine::blendSkeletonPoses(source, target, 0.5f);
        Engine::AnimatedSkeletonPose weightOne = Engine::blendSkeletonPoses(source, target, 1.0f);
        ctx.expect(weightZero.diagnostics.valid, "zero-weight blended pose was invalid");
        ctx.expect(weightHalf.diagnostics.valid, "half-weight blended pose was invalid");
        ctx.expect(weightOne.diagnostics.valid, "one-weight blended pose was invalid");
        if (weightZero.joints.size() == 2 && weightHalf.joints.size() == 2 && weightOne.joints.size() == 2) {
            ctx.expect(nearlyEqual(weightZero.joints[0].modelTransform, source.joints[0].modelTransform),
                "zero-weight blend did not preserve source root model transform");
            ctx.expect(nearlyEqual(weightOne.joints[0].modelTransform, target.joints[0].modelTransform),
                "one-weight blend did not preserve target root model transform");
            ctx.expect(!nearlyEqual(weightHalf.joints[1].modelTransform, source.joints[1].modelTransform),
                "half-weight blend unexpectedly matched source child transform");
            ctx.expect(!nearlyEqual(weightHalf.joints[1].modelTransform, target.joints[1].modelTransform),
                "half-weight blend unexpectedly matched target child transform");
            ctx.expect(nearlyEqual(
                weightHalf.joints[1].finalSkinningMatrix,
                weightHalf.joints[1].modelTransform * weightHalf.joints[1].inverseBindMatrix),
                "blended final skinning matrix was not recomputed from model and inverse bind");
        }

        Engine::AnimatedSkeletonPose clamped = Engine::blendSkeletonPoses(source, target, 2.0f);
        ctx.expect(clamped.diagnostics.valid, "clamped blended pose was invalid");
        ctx.expect(clamped.diagnostics.blendWeightClamped, "out-of-range blend weight was not diagnosed");
        ctx.expect(nearlyEqual(clamped.diagnostics.blendWeight, 1.0f), "clamped blend weight was wrong");

        Engine::AnimatedSkeletonPose invalid = Engine::blendSkeletonPoses(result.model.sampleClip(99, 0.0f), target, 0.5f);
        ctx.expect(!invalid.diagnostics.valid, "invalid-source blend unexpectedly reported valid");
        ctx.expect(!invalid.diagnostics.warnings.empty(), "invalid-source blend did not report a warning");

        Engine::AnimatedSkeletonPose mismatched = source;
        mismatched.joints.pop_back();
        Engine::AnimatedSkeletonPose mismatchBlend = Engine::blendSkeletonPoses(mismatched, target, 0.5f);
        ctx.expect(!mismatchBlend.diagnostics.valid, "mismatched-joint blend unexpectedly reported valid");
        ctx.expect(mismatchBlend.diagnostics.mismatchedJointCount > 0, "mismatched-joint blend did not diagnose the mismatch");

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model blend test shutdown");
    }

    void animatedModelCrossfadesBetweenClips(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model crossfade test load failed: " + result.message);
        if (!result.success) {
            return;
        }

        Engine::AnimationPlaybackState playback;
        playback.clipIndex = 0;
        playback.timeSeconds = 0.0f;
        playback.loop = false;
        playback.playing = true;
        Engine::AnimationCrossfadeState crossfade = Engine::beginCrossfade(playback, 1, 0.5f);
        ctx.expect(crossfade.active, "crossfade did not become active for a different target clip");

        Engine::AnimatedSkeletonPose start = Engine::advanceCrossfade(result.model, crossfade, playback, 0.0f);
        ctx.expect(start.diagnostics.valid, "crossfade start pose was invalid");
        ctx.expect(crossfade.active, "crossfade completed too early at start");
        ctx.expect(nearlyEqual(start.diagnostics.blendWeight, 0.0f), "crossfade start weight was wrong");

        Engine::AnimatedSkeletonPose midpoint = Engine::advanceCrossfade(result.model, crossfade, playback, 0.25f);
        ctx.expect(midpoint.diagnostics.valid, "crossfade midpoint pose was invalid");
        ctx.expect(crossfade.active, "crossfade completed too early at midpoint");
        ctx.expect(nearlyEqual(midpoint.diagnostics.blendWeight, 0.5f), "crossfade midpoint weight was wrong");
        Engine::AnimationSampleSettings nonLooping;
        nonLooping.loop = false;
        Engine::AnimatedSkeletonPose sourceMid = result.model.sampleClip(0, 0.25f, nonLooping);
        Engine::AnimatedSkeletonPose targetMid = result.model.sampleClip(1, 0.25f, nonLooping);
        if (midpoint.joints.size() == 2 && sourceMid.joints.size() == 2 && targetMid.joints.size() == 2) {
            ctx.expect(!nearlyEqual(midpoint.joints[1].modelTransform, sourceMid.joints[1].modelTransform),
                "crossfade midpoint unexpectedly matched source");
            ctx.expect(!nearlyEqual(midpoint.joints[1].modelTransform, targetMid.joints[1].modelTransform),
                "crossfade midpoint unexpectedly matched target");
        }

        Engine::AnimatedSkeletonPose end = Engine::advanceCrossfade(result.model, crossfade, playback, 0.25f);
        ctx.expect(end.diagnostics.valid, "crossfade completion pose was invalid");
        ctx.expect(!crossfade.active, "crossfade did not complete after duration");
        ctx.expect(playback.clipIndex == 1, "crossfade completion did not swap active playback clip");

        playback.clipIndex = 0;
        playback.timeSeconds = 0.0f;
        playback.playing = false;
        Engine::AnimationCrossfadeState paused = Engine::beginCrossfade(playback, 1, 0.5f);
        Engine::AnimatedSkeletonPose pausedPose = Engine::advanceCrossfade(result.model, paused, playback, 0.25f);
        ctx.expect(pausedPose.diagnostics.valid, "paused crossfade pose was invalid");
        ctx.expect(paused.active, "paused crossfade completed unexpectedly");
        ctx.expect(nearlyEqual(paused.elapsedSeconds, 0.0f), "paused crossfade advanced elapsed time");

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model crossfade test shutdown");
    }

    void animatedModelReportsInterpolationFallbackDiagnostics(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::AssetCache assetCache;
        Engine::AnimatedModelLoadSettings settings;
        settings.loadTextures = false;
        Engine::AnimatedModelLoadResult result = Engine::loadAnimatedModel(skinnedFixturePath(), assetCache, settings);
        ctx.expect(result.success, "animated model interpolation test load failed: " + result.message);
        if (!result.success) {
            return;
        }

        Assets::Assimp::ImportedScene& imported = const_cast<Assets::Assimp::ImportedScene&>(result.model.importedScene());
        ctx.expect(!imported.animations.empty() && imported.animations.front().channels.size() >= 2,
            "interpolation test fixture did not have expected channels");
        if (!imported.animations.empty() && imported.animations.front().channels.size() >= 2) {
            auto& rotationKeys = imported.animations.front().channels[0].rotationKeys;
            auto& translationKeys = imported.animations.front().channels[1].translationKeys;
            if (!rotationKeys.empty()) {
                rotationKeys.front().interpolation = Assets::Assimp::ImportedSceneAnimationInterpolation::Step;
            }
            if (!translationKeys.empty()) {
                translationKeys.front().interpolation = Assets::Assimp::ImportedSceneAnimationInterpolation::CubicSpline;
            }
            Engine::AnimationSampleSettings settingsNoLoop;
            settingsNoLoop.loop = false;
            Engine::AnimatedSkeletonPose pose = result.model.sampleClip(0, 0.5f, settingsNoLoop);
            ctx.expect(pose.diagnostics.unsupportedInterpolationCount > 0, "cubic interpolation fallback was not diagnosed");
            if (pose.joints.size() == 2) {
                const glm::vec3 rotatedX = glm::vec3{pose.joints[1].modelTransform * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}};
                ctx.expect(nearlyEqual(rotatedX.x, 1.0f, 0.001f) && nearlyEqual(rotatedX.y, 0.0f, 0.001f),
                    "step rotation interpolation did not hold previous key");
            }
        }

        result.model.shutdown();
        expectNoLiveRendererResources(ctx, "animated model interpolation test shutdown");
    }

    void optionalSponzaAuthoredSceneStabilityReport(TestContext& ctx)
    {
        if (!heavyOptionalAssetTestsEnabled()) {
            std::cout << "[authored_scene] Heavy optional asset tests disabled; set MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1 to run Sponza stability report\n";
            return;
        }

        const std::filesystem::path path = sponzaPath();
        if (!std::filesystem::exists(path)) {
            std::cout << "[authored_scene] Sponza fixture not present; skipping optional stability report\n";
            return;
        }

        ProfileReport profile;
        Assets::Assimp::ImportedScene imported = profile.measure("sponza source import", [&]() {
            return Assets::Assimp::importScene(path);
        });
        ctx.expect(imported.success, "Sponza import failed: " + imported.error);
        if (!imported.success) {
            return;
        }
        ctx.expect(imported.diagnostics.nodeCount == 155, "Sponza node count differed from expected 155");
        ctx.expect(imported.diagnostics.meshNodeCount == 115, "Sponza mesh node count differed from expected 115");
        ctx.expect(imported.diagnostics.primitiveCount == 405, "Sponza primitive count differed from expected 405");
        ctx.expect(imported.diagnostics.materialCount == 28, "Sponza material count differed from expected 28");
        ctx.expect(imported.diagnostics.textureCount > 0, "Sponza texture count was zero");
        ctx.expect(imported.diagnostics.lightCount > 0, "Sponza light count was zero");
        ctx.expect(imported.diagnostics.alphaMaterialCount > 0, "Sponza alpha usage was not detected");
        ctx.expect(imported.diagnostics.doubleSidedMaterialCount > 0, "Sponza double-sided usage was not detected");
        ctx.expect(imported.diagnostics.texcoord1PrimitiveCount > 0, "Sponza TEXCOORD_1 usage was not detected");
        ctx.expect(imported.diagnostics.vertexColorPrimitiveCount > 0, "Sponza COLOR_0 usage was not detected");

        Engine::AuthoredScenePartitionSettings partitionSettings;
        partitionSettings.sectorSize = 25.0f;
        Engine::AuthoredScenePartition partition = profile.measure("sponza partition", [&]() {
            return Engine::partitionImportedAuthoredScene(imported, partitionSettings);
        });
        uint32_t largestSectorPrimitiveCount = 0;
        for (const Engine::AuthoredSceneSectorManifest& sector : partition.sectors) {
            largestSectorPrimitiveCount = std::max(largestSectorPrimitiveCount, sector.primitiveCount);
        }

        Engine::AuthoredSceneCacheSettings cache = cacheSettings("sponza_report", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
        const Engine::AuthoredSceneCacheManifest manifest =
            Engine::AuthoredSceneCache::buildManifest(cache, path, partitionSettings);
        Engine::AuthoredSceneCachePayload payload;
        payload.scene = imported;
        payload.partition = partition;
        Engine::AuthoredSceneCacheWriteResult write = profile.measure("sponza cache write", [&]() {
            return Engine::AuthoredSceneCache::write(manifest, payload);
        });
        Engine::AuthoredSceneCacheReadResult read = profile.measure("sponza cache read", [&]() {
            return Engine::AuthoredSceneCache::read(manifest);
        });

        profile.fact("nodes", std::to_string(imported.diagnostics.nodeCount));
        profile.fact("mesh_nodes", std::to_string(imported.diagnostics.meshNodeCount));
        profile.fact("primitives", std::to_string(imported.diagnostics.primitiveCount));
        profile.fact("materials", std::to_string(imported.diagnostics.materialCount));
        profile.fact("textures", std::to_string(imported.diagnostics.textureCount));
        profile.fact("lights", std::to_string(imported.diagnostics.lightCount));
        profile.fact("sectors", std::to_string(partition.sectors.size()));
        profile.fact("largest_sector_primitives", std::to_string(largestSectorPrimitiveCount));
        profile.fact("warnings", std::to_string(imported.diagnostics.warnings.size()));
        profile.fact("cache_write", Engine::cacheStatusName(write.status));
        profile.fact("cache_read", Engine::cacheStatusName(read.status));
        writeProfileReport(authoredReportPath("authored_scene_sponza_profile.txt"), profile);
    }

    void optionalSponzaFbxAuthoredSceneValidation(TestContext& ctx)
    {
        if (!heavyOptionalAssetTestsEnabled()) {
            std::cout << "[authored_scene] Heavy optional asset tests disabled; set MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1 to run Sponza FBX validation\n";
            return;
        }

        const std::filesystem::path path = sponzaFbxPath();
        if (!std::filesystem::exists(path)) {
            std::cout << "[authored_scene] Sponza FBX fixture not present; skipping optional validation\n";
            return;
        }

        TestRenderer::reset();
        ProfileReport profile;
        Assets::Assimp::ImportedScene imported = profile.measure("sponza fbx source import", [&]() {
            return Assets::Assimp::importScene(path);
        });
        ctx.expect(imported.success, "Sponza FBX import failed: " + imported.error);
        if (!imported.success) {
            return;
        }
        ctx.expect(imported.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx, "Sponza FBX source format was not FBX");
        ctx.expect(imported.diagnostics.nodeCount > 0, "Sponza FBX node count was zero");
        ctx.expect(imported.diagnostics.meshCount > 0, "Sponza FBX mesh count was zero");
        ctx.expect(imported.diagnostics.primitiveCount > 0, "Sponza FBX primitive count was zero");
        ctx.expect(imported.diagnostics.materialCount > 0, "Sponza FBX material count was zero");
        ctx.expect(imported.bounds.valid, "Sponza FBX bounds were invalid");

        Engine::AuthoredScenePartitionSettings partitionSettings;
        partitionSettings.sectorSize = 25.0f;
        Engine::AuthoredScenePartition partition = profile.measure("sponza fbx partition", [&]() {
            return Engine::partitionImportedAuthoredScene(imported, partitionSettings);
        });
        ctx.expect(!partition.sectors.empty(), "Sponza FBX partition produced no sectors");
        uint32_t largestSectorPrimitiveCount = 0;
        for (const Engine::AuthoredSceneSectorManifest& sector : partition.sectors) {
            largestSectorPrimitiveCount = std::max(largestSectorPrimitiveCount, sector.primitiveCount);
        }

        Engine::AuthoredSceneCacheSettings cache = cacheSettings("sponza_fbx_report", Engine::AuthoredSceneCachePolicy::GenerateOnMiss);
        const Engine::AuthoredSceneCacheManifest manifest =
            Engine::AuthoredSceneCache::buildManifest(cache, path, partitionSettings);
        Engine::AuthoredSceneCachePayload payload;
        payload.scene = imported;
        payload.partition = partition;
        Engine::AuthoredSceneCacheWriteResult write = profile.measure("sponza fbx cache write", [&]() {
            return Engine::AuthoredSceneCache::write(manifest, payload);
        });
        Engine::AuthoredSceneCacheReadResult read = profile.measure("sponza fbx cache read", [&]() {
            return Engine::AuthoredSceneCache::read(manifest);
        });
        ctx.expect(write.status == Engine::AuthoredSceneCacheStatus::WriteSuccess, "Sponza FBX cache write failed");
        ctx.expect(read.status == Engine::AuthoredSceneCacheStatus::Hit && read.payload.has_value(), "Sponza FBX cache read did not hit");
        if (read.payload) {
            ctx.expect(read.payload->scene.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx,
                "Sponza FBX cache payload lost source format");
        }

        Engine::AuthoredSceneStreamingSettings settings;
        settings.load.loadTextures = false;
        settings.partition = partitionSettings;
        settings.loadInitialSectorsImmediately = false;
        settings.loadRadius = 45.0f;
        settings.unloadRadius = 80.0f;
        if (imported.bounds.valid) {
            settings.initialCameraPosition = (imported.bounds.min + imported.bounds.max) * 0.5f;
        }

        Engine::AssetCache assetCache;
        Engine::PartitionedAuthoredSceneLoadResult load = profile.measure("sponza fbx payload commit", [&]() {
            return Engine::createPartitionedAuthoredSceneFromPayload(path, payload, assetCache, settings);
        });
        ctx.expect(load.success, "Sponza FBX payload commit failed: " + load.message);
        if (!load.success) {
            return;
        }
        ctx.expect(load.scene.diagnostics().sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx,
            "Sponza FBX runtime diagnostics lost source format");
        ctx.expect(load.scene.diagnostics().totalSectorCount == partition.sectors.size(), "Sponza FBX runtime sector count was wrong");

        Engine::MainThreadWorkQueue work;
        Engine::FrameBudget budget;
        profile.measure("sponza fbx initial sector commit", [&]() {
            load.scene.updateStreaming(settings.initialCameraPosition, work);
            budget.beginFrame({5.0f, true});
            work.drain(budget);
        });
        ctx.expect(load.scene.diagnostics().loadedSectorCount > 0, "Sponza FBX runtime did not load any sectors");

        const Engine::AuthoredSceneDiagnosticsSummary summary =
            Engine::summarizeAuthoredSceneDiagnostics(load.scene.diagnostics());
        ctx.expect(summary.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx, "Sponza FBX summary source format was wrong");

        profile.fact("source_format", summary.sourceFormatName);
        profile.fact("nodes", std::to_string(imported.diagnostics.nodeCount));
        profile.fact("mesh_nodes", std::to_string(imported.diagnostics.meshNodeCount));
        profile.fact("primitives", std::to_string(imported.diagnostics.primitiveCount));
        profile.fact("materials", std::to_string(imported.diagnostics.materialCount));
        profile.fact("textures", std::to_string(imported.diagnostics.textureCount));
        profile.fact("embedded_textures", std::to_string(imported.diagnostics.embeddedTextureCount));
        profile.fact("missing_pbr_materials", std::to_string(imported.diagnostics.missingPbrMaterialCount));
        profile.fact("sectors", std::to_string(partition.sectors.size()));
        profile.fact("largest_sector_primitives", std::to_string(largestSectorPrimitiveCount));
        profile.fact("cache_write", Engine::cacheStatusName(write.status));
        profile.fact("cache_read", Engine::cacheStatusName(read.status));
        profile.fact("warnings", std::to_string(imported.diagnostics.warnings.size()));

        profile.measure("sponza fbx shutdown", [&]() {
            load.scene.shutdown();
        });
        expectNoLiveRendererResources(ctx, "Sponza FBX authored scene");
        writeProfileReport(authoredReportPath("authored_scene_sponza_fbx_profile.txt"), profile);
    }

    void optionalKayKitFbxAnimatedModelRuntimeValidation(TestContext& ctx)
    {
        if (!heavyOptionalAssetTestsEnabled()) {
            std::cout << "[authored_scene] Heavy optional asset tests disabled; set MANUAL_ENGINE_RUN_HEAVY_ASSET_TESTS=1 to run KayKit FBX runtime validation\n";
            return;
        }

        uint32_t validatedCount = 0;
        for (const std::filesystem::path& path : kayKitFbxAnimationPaths()) {
            if (!std::filesystem::exists(path)) {
                std::cout << "[authored_scene] KayKit FBX fixture not present; skipping " << path.generic_string() << '\n';
                continue;
            }

            ++validatedCount;
            TestRenderer::reset();
            ProfileReport profile;
            Engine::AssetCache assetCache;
            Engine::AnimatedModelLoadSettings settings;
            settings.loadTextures = false;
            settings.createBindPoseInstances = false;
            settings.createSkinnedMeshes = true;
            settings.createSkinnedInstances = true;

            Engine::AnimatedModelLoadResult load = profile.measure("kaykit fbx animated model load", [&]() {
                return Engine::loadAnimatedModel(path, assetCache, settings);
            });
            ctx.expect(load.success, "KayKit FBX animated model load failed: " + path.generic_string() + " " + load.message);
            if (!load.success) {
                expectNoLiveRendererResources(ctx, "KayKit FBX failed animated model load");
                continue;
            }

            const Engine::AnimatedModelDiagnostics& diagnostics = load.model.diagnostics();
            ctx.expect(diagnostics.sourceFormat == Assets::Assimp::ImportedSceneSourceFormat::Fbx,
                "KayKit animated model diagnostics lost FBX source format");
            ctx.expect(diagnostics.importedJointCount > 0 || diagnostics.importedAnimationCount > 0,
                "KayKit animated model did not retain joints or animations");
            if (diagnostics.importedAnimationCount > 0) {
                ctx.expect(load.model.clipCount() > 0, "KayKit animated model clip accessor was empty");
                const float duration = load.model.clipDuration(0);
                Engine::AnimationSampleSettings sampleSettings;
                sampleSettings.loop = false;
                const float midpoint = duration > 0.0f ? duration * 0.5f : 0.0f;
                Engine::AnimatedSkeletonPose start = load.model.sampleClip(0, 0.0f, sampleSettings);
                Engine::AnimatedSkeletonPose middle = load.model.sampleClip(0, midpoint, sampleSettings);
                Engine::AnimatedSkeletonPose end = load.model.sampleClip(0, duration, sampleSettings);
                ctx.expect(start.diagnostics.valid, "KayKit FBX start pose was invalid");
                ctx.expect(middle.diagnostics.valid, "KayKit FBX midpoint pose was invalid");
                ctx.expect(end.diagnostics.valid, "KayKit FBX end pose was invalid");
                if (load.model.skinnedInstanceCount() > 0 && middle.diagnostics.valid) {
                    bool uploaded = true;
                    for (uint32_t instanceIndex = 0; instanceIndex < load.model.skinnedInstanceCount(); ++instanceIndex) {
                        uploaded = load.model.updateSkinnedPose(instanceIndex, middle) && uploaded;
                    }
                    ctx.expect(uploaded, "KayKit FBX sampled pose did not upload to all skinned instances");
                }
            }
            if (diagnostics.createdSkinnedMeshCount == 0 && diagnostics.createdMeshCount == 0) {
                ctx.expect(!diagnostics.warnings.empty(), "KayKit animation-only FBX load did not report diagnostics");
            }

            const Engine::AnimatedModelDiagnosticsSummary summary =
                Engine::summarizeAnimatedModelDiagnostics(diagnostics);
            profile.fact("path", path.generic_string());
            profile.fact("message", load.message);
            profile.fact("summary", Engine::animatedModelDiagnosticsSummaryText(diagnostics));
            profile.fact("summary_yaml_size", std::to_string(Engine::animatedModelDiagnosticsSummaryYaml(diagnostics).size()));
            profile.fact("source_format", summary.sourceFormatName);
            profile.fact("joints", std::to_string(diagnostics.importedJointCount));
            profile.fact("skins", std::to_string(diagnostics.importedSkinCount));
            profile.fact("animations", std::to_string(diagnostics.importedAnimationCount));
            profile.fact("channels", std::to_string(diagnostics.importedAnimationChannelCount));
            profile.fact("created_meshes", std::to_string(diagnostics.createdMeshCount));
            profile.fact("created_skinned_meshes", std::to_string(diagnostics.createdSkinnedMeshCount));
            profile.fact("created_skinned_instances", std::to_string(diagnostics.createdSkinnedInstanceCount));
            profile.fact("warnings", std::to_string(diagnostics.warnings.size()));

            profile.measure("kaykit fbx animated model shutdown", [&]() {
                load.model.shutdown();
                load.model.shutdown();
                assetCache.shutdown();
            });
            expectNoLiveRendererResources(ctx, "KayKit FBX animated model");
            writeProfileReport(authoredReportPath("authored_scene_kaykit_fbx_" + path.stem().string() + "_runtime_profile.txt"), profile);
        }

        if (validatedCount == 0) {
            std::cout << "[authored_scene] KayKit FBX animation fixtures not present; optional runtime validation skipped\n";
        }
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
        {"AuthoredSceneAsyncJobsRoundTripCacheAndImport", authoredSceneAsyncJobsRoundTripCacheAndImport},
        {"AuthoredSceneAsyncPayloadCommitsThroughPartitionedOwner", authoredSceneAsyncPayloadCommitsThroughPartitionedOwner},
        {"AuthoredSceneDiagnosticsSummaryAndProfileReport", authoredSceneDiagnosticsSummaryAndProfileReport},
        {"AuthoredSceneRepeatedLoadShutdownLeavesNoResources", authoredSceneRepeatedLoadShutdownLeavesNoResources},
        {"AuthoredSceneAsyncPendingShutdownLeavesNoResources", authoredSceneAsyncPendingShutdownLeavesNoResources},
        {"AuthoredSceneMissingPathDoesNotCreateResources", authoredSceneMissingPathDoesNotCreateResources},
        {"AuthoredSceneRejectsSkinnedAnimationRuntime", authoredSceneRejectsSkinnedAnimationRuntime},
        {"AuthoredSceneAsyncSkinnedImportCommitsFailStaticRuntime", authoredSceneAsyncSkinnedImportCommitsFailStaticRuntime},
        {"AnimatedModelLoadsAndOwnsBindPoseResources", animatedModelLoadsAndOwnsBindPoseResources},
        {"AnimatedModelLoadRejectsNonAnimatedAndMissingAssets", animatedModelLoadRejectsNonAnimatedAndMissingAssets},
        {"AnimatedModelCanSkipBindPoseInstances", animatedModelCanSkipBindPoseInstances},
        {"AnimatedModelCanSkipSkinnedMeshes", animatedModelCanSkipSkinnedMeshes},
        {"AnimatedModelCreatesSkinnedInstancesAndUpdatesPose", animatedModelCreatesSkinnedInstancesAndUpdatesPose},
        {"AnimatedModelPlaybackLoopUpdatesSkinnedPalettes", animatedModelPlaybackLoopUpdatesSkinnedPalettes},
        {"AnimatedModelCacheRoundTripsPayload", animatedModelCacheRoundTripsPayload},
        {"AnimatedModelCacheHandlesMissStaleAndCorrupt", animatedModelCacheHandlesMissStaleAndCorrupt},
        {"AnimatedModelLoadsFromCacheAndCommitsPayload", animatedModelLoadsFromCacheAndCommitsPayload},
        {"AnimatedModelAsyncJobsProduceCommittablePayloads", animatedModelAsyncJobsProduceCommittablePayloads},
        {"AnimatedModelDiagnosticsSummaryAndProfileReport", animatedModelDiagnosticsSummaryAndProfileReport},
        {"AnimatedModelRepeatedLoadShutdownLeavesNoResources", animatedModelRepeatedLoadShutdownLeavesNoResources},
        {"AnimatedModelAsyncPendingShutdownLeavesNoResources", animatedModelAsyncPendingShutdownLeavesNoResources},
        {"RendererSkinnedMeshDiagnosticsAndLifetime", rendererSkinnedMeshDiagnosticsAndLifetime},
        {"AnimatedModelEvaluatesBindPoseAndClipSamples", animatedModelEvaluatesBindPoseAndClipSamples},
        {"AnimatedModelAdvancesPlaybackState", animatedModelAdvancesPlaybackState},
        {"AnimatedModelBlendsSkeletonPoses", animatedModelBlendsSkeletonPoses},
        {"AnimatedModelCrossfadesBetweenClips", animatedModelCrossfadesBetweenClips},
        {"AnimatedModelReportsInterpolationFallbackDiagnostics", animatedModelReportsInterpolationFallbackDiagnostics},
        {"OptionalSponzaAuthoredSceneStabilityReport", optionalSponzaAuthoredSceneStabilityReport},
        {"OptionalSponzaFbxAuthoredSceneValidation", optionalSponzaFbxAuthoredSceneValidation},
        {"OptionalKayKitFbxAnimatedModelRuntimeValidation", optionalKayKitFbxAnimatedModelRuntimeValidation},
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
