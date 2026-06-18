#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Engine/AssetCache.hpp"
#include "Engine/AnimatedModel.hpp"
#include "Engine/AuthoredScene.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

namespace {
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

    SDL_Window* createHiddenWindow()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
            return nullptr;
        }

        SDL_Window* window = SDL_CreateWindow(
            "ManualEngine Authored Scene Smoke",
            640,
            480,
            SDL_WINDOW_HIDDEN
        );
        if (!window) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
            SDL_Quit();
        }
        return window;
    }

    Renderer::MeshVertex vertex(float x, float y, float z, float u, float v, uint32_t abgr = 0xffffffff)
    {
        return {
            x, y, z,
            0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            u, v,
            u, v,
            abgr,
        };
    }

    Renderer::StaticMeshHandle createQuad(Renderer::MaterialHandle material)
    {
        Renderer::StaticMeshDescriptor descriptor;
        descriptor.name = "smoke.quad";
        Renderer::StaticSubmeshDescriptor submesh;
        submesh.material = material;
        submesh.vertices = {
            vertex(-0.5f, 0.5f, 0.0f, 0.0f, 0.0f),
            vertex(0.5f, 0.5f, 0.0f, 1.0f, 0.0f),
            vertex(-0.5f, -0.5f, 0.0f, 0.0f, 1.0f),
            vertex(0.5f, -0.5f, 0.0f, 1.0f, 1.0f),
        };
        submesh.indices = {0, 2, 1, 1, 2, 3};
        descriptor.submeshes.push_back(std::move(submesh));
        return Renderer::createStaticMesh(descriptor);
    }

    std::filesystem::path authoredReportPath(std::string_view filename)
    {
        const std::filesystem::path directory = "generated/authored_scene_reports";
        std::filesystem::create_directories(directory);
        return directory / filename;
    }

    template <typename Function>
    auto measureMilliseconds(Function&& function, float& milliseconds)
    {
        const auto start = std::chrono::steady_clock::now();
        auto result = function();
        const auto end = std::chrono::steady_clock::now();
        milliseconds = std::chrono::duration<float, std::milli>(end - start).count();
        return result;
    }
}

int main()
{
    SDL_Window* window = createHiddenWindow();
    if (!window) {
        return 1;
    }

    if (Renderer::init_bgfx(window) != 0) {
        return 1;
    }

    Renderer::configureVertexLayouts();
    if (!Renderer::initSceneRenderer()) {
        std::cerr << "Renderer scene initialization failed\n";
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Engine::AssetCache assetCache;
    Engine::AuthoredSceneStreamingSettings settings;
    settings.load.loadTextures = false;
    settings.partition.sectorSize = 25.0f;
    settings.initialCameraPosition = {10.0f, 2.0f, 0.0f};
    settings.loadRadius = 50.0f;
    float loadMs = 0.0f;
    Engine::PartitionedAuthoredSceneLoadResult load = measureMilliseconds([&]() {
        return Engine::loadPartitionedAuthoredScene(fixturePath(), assetCache, settings);
    }, loadMs);
    if (!load.success) {
        std::cerr << "Partitioned authored scene load failed: " << load.message << '\n';
        Renderer::shutdownSceneRenderer();
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Engine::AnimatedModelLoadSettings animatedSettings;
    animatedSettings.loadTextures = false;
    animatedSettings.createBindPoseInstances = false;
    animatedSettings.createSkinnedInstances = true;
    animatedSettings.renderLayer = Renderer::RenderLayer::Props;
    Engine::AnimatedModelLoadResult animatedLoad = Engine::loadAnimatedModel(
        skinnedFixturePath(),
        assetCache,
        animatedSettings);
    if (!animatedLoad.success) {
        std::cerr << "Animated model load failed: " << animatedLoad.message << '\n';
        load.scene.shutdown();
        Renderer::shutdownSceneRenderer();
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (const std::optional<Engine::AnimatedModelSkinnedInstance> skinnedInstance = animatedLoad.model.skinnedInstance(0)) {
        Renderer::setSkinnedInstanceTransform(
            skinnedInstance->handle,
            glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 1.5f, 0.0f}));
    }

    Renderer::AtmosphereSettings atmosphere = Renderer::atmosphereSettings();
    atmosphere.exposure = 1.25f;
    atmosphere.ambientIntensity = 0.12f;
    atmosphere.environmentDiffuseColor = {0.8f, 0.9f, 1.0f};
    atmosphere.environmentDiffuseIntensity = 0.75f;
    atmosphere.environmentEnabled = true;
    Renderer::setAtmosphereSettings(atmosphere);
    const Renderer::AtmosphereSettings appliedAtmosphere = Renderer::atmosphereSettings();

    Renderer::LightDescriptor pointLightDescriptor;
    pointLightDescriptor.name = "smoke.point";
    pointLightDescriptor.type = Renderer::LightType::Point;
    pointLightDescriptor.color = {1.0f, 0.7f, 0.45f};
    pointLightDescriptor.intensity = 8.0f;
    pointLightDescriptor.position = {10.0f, 2.0f, 2.5f};
    pointLightDescriptor.range = 12.0f;
    Renderer::LightHandle pointLight = Renderer::createLight(pointLightDescriptor);

    Renderer::LightDescriptor spotLightDescriptor;
    spotLightDescriptor.name = "smoke.spot";
    spotLightDescriptor.type = Renderer::LightType::Spot;
    spotLightDescriptor.color = {0.45f, 0.65f, 1.0f};
    spotLightDescriptor.intensity = 5.0f;
    spotLightDescriptor.position = {11.25f, 2.0f, 3.0f};
    spotLightDescriptor.direction = {0.0f, 0.0f, -1.0f};
    spotLightDescriptor.range = 10.0f;
    spotLightDescriptor.innerConeAngle = 0.2f;
    spotLightDescriptor.outerConeAngle = 0.7f;
    Renderer::LightHandle spotLight = Renderer::createLight(spotLightDescriptor);

    Renderer::LightDescriptor directionalLightDescriptor;
    directionalLightDescriptor.name = "smoke.directional";
    directionalLightDescriptor.type = Renderer::LightType::Directional;
    directionalLightDescriptor.color = {0.8f, 0.9f, 1.0f};
    directionalLightDescriptor.intensity = 1.0f;
    directionalLightDescriptor.direction = {-0.3f, -0.8f, -0.4f};
    Renderer::LightHandle directionalLight = Renderer::createLight(directionalLightDescriptor);

    Renderer::TextureHandle pbrBaseColorTexture = Renderer::createSolidTexture(220, 210, 190, 255);
    Renderer::TextureHandle pbrNormalTexture = Renderer::createSolidTexture(128, 128, 255, 255);
    Renderer::TextureHandle pbrMetallicRoughnessTexture = Renderer::createSolidTexture(255, 96, 230, 255);
    Renderer::TextureHandle pbrOcclusionTexture = Renderer::createSolidTexture(180, 180, 180, 255);
    Renderer::TextureHandle pbrEmissiveTexture = Renderer::createSolidTexture(24, 48, 96, 255);

    Renderer::MaterialDescriptor opaqueDescriptor;
    opaqueDescriptor.name = "smoke.opaque";
    opaqueDescriptor.baseColorFactor = {1.0f, 0.2f, 0.2f, 1.0f};
    opaqueDescriptor.roughnessFactor = 0.95f;
    Renderer::MaterialHandle opaqueMaterial = Renderer::createMaterial(opaqueDescriptor);

    Renderer::MaterialDescriptor maskDescriptor;
    maskDescriptor.name = "smoke.mask";
    maskDescriptor.baseColorFactor = {0.2f, 1.0f, 0.2f, 0.75f};
    maskDescriptor.baseColorTexture = pbrBaseColorTexture;
    maskDescriptor.normalTexture = pbrNormalTexture;
    maskDescriptor.normalScale = 0.8f;
    maskDescriptor.metallicRoughnessTexture = pbrMetallicRoughnessTexture;
    maskDescriptor.occlusionTexture = pbrOcclusionTexture;
    maskDescriptor.occlusionStrength = 0.5f;
    maskDescriptor.emissiveTexture = pbrEmissiveTexture;
    maskDescriptor.emissiveFactor = {0.2f, 0.35f, 0.5f};
    maskDescriptor.alphaMode = Renderer::MaterialDescriptor::AlphaMode::Mask;
    maskDescriptor.alphaCutoff = 0.5f;
    Renderer::MaterialHandle maskMaterial = Renderer::createMaterial(maskDescriptor);

    Renderer::MaterialDescriptor blendDescriptor;
    blendDescriptor.name = "smoke.blend";
    blendDescriptor.baseColorFactor = {0.2f, 0.2f, 1.0f, 0.5f};
    blendDescriptor.metallicFactor = 1.0f;
    blendDescriptor.roughnessFactor = 0.18f;
    blendDescriptor.alphaMode = Renderer::MaterialDescriptor::AlphaMode::Blend;
    blendDescriptor.doubleSided = true;
    Renderer::MaterialHandle blendMaterial = Renderer::createMaterial(blendDescriptor);

    Renderer::StaticMeshHandle opaqueMesh = createQuad(opaqueMaterial);
    Renderer::StaticMeshHandle maskMesh = createQuad(maskMaterial);
    Renderer::StaticMeshHandle blendMesh = createQuad(blendMaterial);
    Renderer::MeshInstanceHandle opaqueInstance = Renderer::createInstance(opaqueMesh);
    Renderer::MeshInstanceHandle maskInstance = Renderer::createInstance(maskMesh);
    Renderer::MeshInstanceHandle blendInstance = Renderer::createInstance(blendMesh);
    Renderer::setInstancePosition(opaqueInstance, {8.75f, 2.0f, 0.0f});
    Renderer::setInstancePosition(maskInstance, {10.0f, 2.0f, 0.0f});
    Renderer::setInstancePosition(blendInstance, {11.25f, 2.0f, 0.0f});

    const glm::mat4 view = glm::lookAt(
        glm::vec3{10.0f, 2.0f, 4.0f},
        glm::vec3{10.0f, 2.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f}
    );
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 640.0f / 480.0f, 0.1f, 100.0f);

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, 640, 480);
    bgfx::setViewTransform(0, &view[0][0], &projection[0][0]);

    const Renderer::RenderView renderView{
        0,
        view,
        projection,
        projection * view,
        {10.0f, 2.0f, 4.0f},
        640,
        480,
        static_cast<uint32_t>(Renderer::RenderLayer::All),
        true,
    };
    float drawMs = 0.0f;
    const Renderer::SceneDrawStats stats = measureMilliseconds([&]() {
        return Renderer::drawScene(renderView);
    }, drawMs);
    bgfx::frame();

    Engine::AnimatedSkeletonPose sampledPose = animatedLoad.model.sampleClip(0, 0.5f);
    const bool updatedSkinnedPose = animatedLoad.model.updateSkinnedPose(0, sampledPose);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, 640, 480);
    bgfx::setViewTransform(0, &view[0][0], &projection[0][0]);
    float sampledDrawMs = 0.0f;
    const Renderer::SceneDrawStats sampledStats = measureMilliseconds([&]() {
        return Renderer::drawScene(renderView);
    }, sampledDrawMs);
    bgfx::frame();

    Engine::AnimationPlaybackState smokePlayback;
    smokePlayback.clipIndex = 0;
    smokePlayback.loop = true;
    smokePlayback.playing = true;
    uint32_t playbackSubmittedFrames = 0;
    float playbackPoseMs = 0.0f;
    float playbackUploadMs = 0.0f;
    for (uint32_t frame = 0; frame < 3; ++frame) {
        animatedLoad.model.advancePlayback(smokePlayback, 0.25f);
        const auto poseStart = std::chrono::steady_clock::now();
        Engine::AnimatedSkeletonPose playbackPose =
            animatedLoad.model.sampleClip(smokePlayback.clipIndex, smokePlayback.timeSeconds);
        playbackPoseMs += std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - poseStart).count();
        if (playbackPose.diagnostics.valid) {
            const auto uploadStart = std::chrono::steady_clock::now();
            animatedLoad.model.updateSkinnedPose(0, playbackPose);
            playbackUploadMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - uploadStart).count();
        }
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, 640, 480);
        bgfx::setViewTransform(0, &view[0][0], &projection[0][0]);
        const Renderer::SceneDrawStats playbackStats = Renderer::drawScene(renderView);
        if (playbackStats.submittedSkinnedMeshDrawItems > 0 &&
            playbackStats.submittedSkinnedJointPaletteCount >= 2) {
            ++playbackSubmittedFrames;
        }
        bgfx::frame();
    }

    Engine::AnimationCrossfadeState smokeCrossfade = Engine::beginCrossfade(smokePlayback, 1, 0.5f);
    uint32_t crossfadeSubmittedFrames = 0;
    float crossfadePoseMs = 0.0f;
    float crossfadeUploadMs = 0.0f;
    for (uint32_t frame = 0; frame < 3; ++frame) {
        const auto poseStart = std::chrono::steady_clock::now();
        Engine::AnimatedSkeletonPose crossfadePose =
            Engine::advanceCrossfade(animatedLoad.model, smokeCrossfade, smokePlayback, 0.25f);
        crossfadePoseMs += std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - poseStart).count();
        if (crossfadePose.diagnostics.valid) {
            const auto uploadStart = std::chrono::steady_clock::now();
            animatedLoad.model.updateSkinnedPose(0, crossfadePose);
            crossfadeUploadMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - uploadStart).count();
        }
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, 640, 480);
        bgfx::setViewTransform(0, &view[0][0], &projection[0][0]);
        const Renderer::SceneDrawStats crossfadeStats = Renderer::drawScene(renderView);
        if (crossfadeStats.submittedSkinnedMeshDrawItems > 0 &&
            crossfadeStats.submittedSkinnedJointPaletteCount >= 2) {
            ++crossfadeSubmittedFrames;
        }
        bgfx::frame();
    }

    const Engine::AuthoredSceneDiagnosticsSummary authoredSummary =
        Engine::summarizeAuthoredSceneDiagnostics(load.scene.diagnostics());
    const Engine::AnimatedModelDiagnosticsSummary animatedSummary =
        Engine::summarizeAnimatedModelDiagnostics(animatedLoad.model.diagnostics());
    const Renderer::MaterialDiagnostics blendDiagnostics = Renderer::materialDiagnostics(blendMaterial);
    const Renderer::TextureInfo baseTextureInfo = Renderer::textureInfo(pbrBaseColorTexture);
    const bool submitted = stats.submittedMeshDrawItems > 0;
    const bool submittedAllPasses = stats.submittedOpaqueMeshDrawItems > 0 &&
        stats.submittedAlphaMaskMeshDrawItems > 0 &&
        stats.submittedAlphaBlendMeshDrawItems > 0;
    const bool blendDiagnosticsValid = blendDiagnostics.renderPass == Renderer::MaterialRenderPass::AlphaBlend &&
        blendDiagnostics.doubleSided;
    const bool atmosphereValid = appliedAtmosphere.exposure == 1.25f &&
        appliedAtmosphere.ambientIntensity == 0.12f &&
        appliedAtmosphere.environmentDiffuseIntensity == 0.75f &&
        appliedAtmosphere.environmentEnabled;
    const Renderer::LightDiagnostics pointDiagnostics = Renderer::lightDiagnostics(pointLight);
    const Renderer::LightDiagnostics spotDiagnostics = Renderer::lightDiagnostics(spotLight);
    const bool lightDiagnosticsValid = pointDiagnostics.valid &&
        pointDiagnostics.active &&
        pointDiagnostics.inForwardBudget &&
        spotDiagnostics.valid &&
        spotDiagnostics.descriptor.type == Renderer::LightType::Spot;
    const bool submittedLights = stats.liveLightCount >= 3 &&
        stats.submittedForwardLightCount >= 3 &&
        stats.overBudgetLightCount == 0;
    const bool textureDiagnosticsValid = baseTextureInfo.valid &&
        baseTextureInfo.width > 0 &&
        baseTextureInfo.height > 0 &&
        baseTextureInfo.estimatedBytes > 0;
    const bool submittedSkinned = stats.submittedSkinnedMeshDrawItems > 0 &&
        stats.submittedSkinnedJointPaletteCount >= 2 &&
        sampledStats.submittedSkinnedMeshDrawItems > 0 &&
        sampledStats.submittedSkinnedJointPaletteCount >= 2 &&
        sampledStats.truncatedSkinnedJointPaletteCount == 0 &&
        updatedSkinnedPose &&
        playbackSubmittedFrames == 3 &&
        crossfadeSubmittedFrames == 3 &&
        !smokeCrossfade.active &&
        smokePlayback.clipIndex == 1;

    std::ofstream report(authoredReportPath("authored_scene_smoke_profile.txt"));
    report << "Authored Scene Smoke Profile\n";
    report << "load_ms: " << loadMs << '\n';
    report << "draw_ms: " << drawMs << '\n';
    report << "sampled_draw_ms: " << sampledDrawMs << '\n';
    report << "imported_nodes: " << authoredSummary.importedNodeCount << '\n';
    report << "imported_primitives: " << authoredSummary.importedPrimitiveCount << '\n';
    report << "loaded_sectors: " << authoredSummary.loadedSectorCount << '\n';
    report << "total_sectors: " << authoredSummary.totalSectorCount << '\n';
    report << "texture_bytes: " << authoredSummary.textureEstimatedBytes << '\n';
    report << "submitted_draw_items: " << stats.submittedMeshDrawItems << '\n';
    report << "submitted_opaque: " << stats.submittedOpaqueMeshDrawItems << '\n';
    report << "submitted_alpha_mask: " << stats.submittedAlphaMaskMeshDrawItems << '\n';
    report << "submitted_alpha_blend: " << stats.submittedAlphaBlendMeshDrawItems << '\n';
    report << "submitted_lights: " << stats.submittedForwardLightCount << '\n';
    report << "submitted_skinned_draw_items: " << stats.submittedSkinnedMeshDrawItems << '\n';
    report << "sampled_submitted_skinned_draw_items: " << sampledStats.submittedSkinnedMeshDrawItems << '\n';
    report << "submitted_skinned_joint_palette_count: " << stats.submittedSkinnedJointPaletteCount << '\n';
    report << "playback_submitted_skinned_frames: " << playbackSubmittedFrames << '\n';
    report << "crossfade_submitted_skinned_frames: " << crossfadeSubmittedFrames << '\n';
    report << "animated_source_format: " << animatedSummary.sourceFormatName << '\n';
    report << "animated_joints: " << animatedSummary.importedJointCount << '\n';
    report << "animated_skins: " << animatedSummary.importedSkinCount << '\n';
    report << "animated_clips: " << animatedSummary.importedAnimationCount << '\n';
    report << "animated_created_skinned_meshes: " << animatedSummary.createdSkinnedMeshCount << '\n';
    report << "animated_created_skinned_instances: " << animatedSummary.createdSkinnedInstanceCount << '\n';
    report << "animated_warning_count: " << animatedSummary.warningCount << '\n';
    report << "animated_playback_pose_ms: " << playbackPoseMs << '\n';
    report << "animated_playback_upload_ms: " << playbackUploadMs << '\n';
    report << "animated_crossfade_pose_ms: " << crossfadePoseMs << '\n';
    report << "animated_crossfade_upload_ms: " << crossfadeUploadMs << '\n';
    report << "animated_summary: " << Engine::animatedModelDiagnosticsSummaryText(animatedLoad.model.diagnostics()) << '\n';

    Renderer::destroyInstance(opaqueInstance);
    Renderer::destroyInstance(maskInstance);
    Renderer::destroyInstance(blendInstance);
    Renderer::destroyStaticMesh(opaqueMesh);
    Renderer::destroyStaticMesh(maskMesh);
    Renderer::destroyStaticMesh(blendMesh);
    Renderer::destroyMaterial(opaqueMaterial);
    Renderer::destroyMaterial(maskMaterial);
    Renderer::destroyMaterial(blendMaterial);
    Renderer::destroyLight(pointLight);
    Renderer::destroyLight(spotLight);
    Renderer::destroyLight(directionalLight);
    Renderer::destroyTexture(pbrBaseColorTexture);
    Renderer::destroyTexture(pbrNormalTexture);
    Renderer::destroyTexture(pbrMetallicRoughnessTexture);
    Renderer::destroyTexture(pbrOcclusionTexture);
    Renderer::destroyTexture(pbrEmissiveTexture);
    animatedLoad.model.shutdown();
    load.scene.shutdown();
    assetCache.shutdown();
    Renderer::shutdownSceneRenderer();
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!submitted) {
        std::cerr << "Authored scene smoke did not submit any mesh draw items\n";
        return 1;
    }
    if (!submittedAllPasses) {
        std::cerr << "Authored scene smoke did not submit all mesh render passes: opaque="
            << stats.submittedOpaqueMeshDrawItems
            << " mask=" << stats.submittedAlphaMaskMeshDrawItems
            << " blend=" << stats.submittedAlphaBlendMeshDrawItems
            << '\n';
        return 1;
    }
    if (!blendDiagnosticsValid) {
        std::cerr << "Authored scene smoke did not preserve alpha-blend double-sided diagnostics\n";
        return 1;
    }
    if (!atmosphereValid) {
        std::cerr << "Renderer atmosphere PBR controls were not preserved\n";
        return 1;
    }
    if (!lightDiagnosticsValid) {
        std::cerr << "Renderer light diagnostics were not valid\n";
        return 1;
    }
    if (!textureDiagnosticsValid) {
        std::cerr << "Renderer texture diagnostics were not valid\n";
        return 1;
    }
    if (!submittedLights) {
        std::cerr << "Renderer did not submit expected forward lights: live="
            << stats.liveLightCount
            << " submitted=" << stats.submittedForwardLightCount
            << " overBudget=" << stats.overBudgetLightCount
            << '\n';
        return 1;
    }
    if (!submittedSkinned) {
        std::cerr << "Renderer did not submit expected skinned mesh draw items: bindPose="
            << stats.submittedSkinnedMeshDrawItems
            << " sampled=" << sampledStats.submittedSkinnedMeshDrawItems
            << " palette=" << sampledStats.submittedSkinnedJointPaletteCount
            << " truncated=" << sampledStats.truncatedSkinnedJointPaletteCount
            << " updated=" << updatedSkinnedPose
            << '\n';
        return 1;
    }

    std::cout << "Authored scene renderer smoke passed\n";
    return 0;
}
