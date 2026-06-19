#include "Engine/Scene/AnimatedSceneAdapter.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

#include "Engine/AnimatedModelPose.hpp"
#include "Engine/ImportedSceneResources.hpp"

namespace {
    bool rendererHandleValid(Renderer::SkinnedMeshHandle handle)
    {
        return handle.id != UINT32_MAX;
    }

    bool rendererHandleValid(Renderer::StaticMeshHandle handle)
    {
        return handle.id != UINT32_MAX;
    }

    bool rendererHandleValid(Renderer::MaterialHandle handle)
    {
        return handle.id != UINT32_MAX;
    }

    bool isFinite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool isFinite(const glm::quat& value)
    {
        return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    std::optional<Engine::SceneTransform> decomposeTransform(const glm::mat4& matrix)
    {
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 translation{0.0f};
        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
            return std::nullopt;
        }

        rotation = glm::normalize(rotation);
        if (!isFinite(translation) || !isFinite(scale) || !isFinite(rotation)) {
            return std::nullopt;
        }

        return Engine::SceneTransform{translation, rotation, scale};
    }

    Renderer::MaterialDescriptor fallbackMaterialDescriptor(const Engine::SceneAnimatedAdapterSettings& settings)
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = settings.materialNamePrefix.empty()
            ? "SceneAnimatedFallback"
            : settings.materialNamePrefix + ".Fallback";
        descriptor.baseColorFactor = {1.0f, 0.0f, 1.0f, 1.0f};
        descriptor.roughnessFactor = 1.0f;
        return descriptor;
    }

    Renderer::MaterialHandle ensureFallbackMaterial(
        std::optional<Renderer::MaterialHandle>& fallbackMaterial,
        Engine::SceneAnimatedResourceBinding& resources,
        Engine::SceneAnimatedAdapterDiagnostics& diagnostics,
        const Engine::SceneAnimatedAdapterSettings& settings)
    {
        if (fallbackMaterial.has_value()) {
            return *fallbackMaterial;
        }

        const Renderer::MaterialHandle material = Renderer::createMaterial(fallbackMaterialDescriptor(settings));
        resources.materials.push_back(material);
        fallbackMaterial = material;
        if (rendererHandleValid(material)) {
            ++diagnostics.createdMaterialCount;
        } else {
            diagnostics.warnings.push_back("Renderer rejected animated scene fallback material.");
        }
        return material;
    }

    Engine::AnimationPlaybackState toPlaybackState(const Engine::SceneAnimatorState& state)
    {
        Engine::AnimationPlaybackState playback;
        playback.clipIndex = state.clipIndex;
        playback.timeSeconds = state.timeSeconds;
        playback.speed = state.speed;
        playback.loop = state.loop;
        playback.playing = state.playing;
        return playback;
    }

    void fromPlaybackState(const Engine::AnimationPlaybackState& playback, Engine::SceneAnimatorState& state)
    {
        state.clipIndex = playback.clipIndex;
        state.timeSeconds = playback.timeSeconds;
        state.speed = playback.speed;
        state.loop = playback.loop;
        state.playing = playback.playing;
    }

}

namespace Engine {
    SceneAnimatedModelAdapter::SceneAnimatedModelAdapter(Scene& scene, SceneRenderBridge& renderBridge)
        : scene_(scene)
        , renderBridge_(renderBridge)
    {
    }

    SceneAnimatedModelAdapter::~SceneAnimatedModelAdapter()
    {
        unregisterVariableAnimationSystem();
    }

    uint32_t SceneAnimatedModelAdapter::nextGeneration(uint32_t generation) const
    {
        return generation == UINT32_MAX ? 1u : generation + 1u;
    }

    SceneAnimatedModelAdapter::SkeletonRecord* SceneAnimatedModelAdapter::record(SceneSkeletonHandle handle)
    {
        if (!isValid(handle) || handle.index >= skeletons_.size()) {
            return nullptr;
        }
        SkeletonRecord& candidate = skeletons_[handle.index];
        return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
    }

    const SceneAnimatedModelAdapter::SkeletonRecord* SceneAnimatedModelAdapter::record(SceneSkeletonHandle handle) const
    {
        if (!isValid(handle) || handle.index >= skeletons_.size()) {
            return nullptr;
        }
        const SkeletonRecord& candidate = skeletons_[handle.index];
        return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
    }

    SceneAnimatedModelAdapter::AnimatorRecord* SceneAnimatedModelAdapter::record(SceneAnimatorHandle handle)
    {
        if (!isValid(handle) || handle.index >= animators_.size()) {
            return nullptr;
        }
        AnimatorRecord& candidate = animators_[handle.index];
        return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
    }

    const SceneAnimatedModelAdapter::AnimatorRecord* SceneAnimatedModelAdapter::record(SceneAnimatorHandle handle) const
    {
        if (!isValid(handle) || handle.index >= animators_.size()) {
            return nullptr;
        }
        const AnimatorRecord& candidate = animators_[handle.index];
        return candidate.occupied && candidate.generation == handle.generation ? &candidate : nullptr;
    }

    SceneSkeletonHandle SceneAnimatedModelAdapter::allocateSkeleton()
    {
        for (uint32_t index = 0; index < skeletons_.size(); ++index) {
            if (!skeletons_[index].occupied) {
                SkeletonRecord& record = skeletons_[index];
                record = {};
                record.occupied = true;
                record.generation = nextGeneration(record.generation);
                return {index, record.generation};
            }
        }

        SkeletonRecord record;
        record.occupied = true;
        record.generation = 1;
        skeletons_.push_back(record);
        return {static_cast<uint32_t>(skeletons_.size() - 1), record.generation};
    }

    SceneAnimatorHandle SceneAnimatedModelAdapter::allocateAnimator()
    {
        for (uint32_t index = 0; index < animators_.size(); ++index) {
            if (!animators_[index].occupied) {
                AnimatorRecord& record = animators_[index];
                record = {};
                record.occupied = true;
                record.generation = nextGeneration(record.generation);
                return {index, record.generation};
            }
        }

        AnimatorRecord record;
        record.occupied = true;
        record.generation = 1;
        animators_.push_back(std::move(record));
        return {static_cast<uint32_t>(animators_.size() - 1), 1};
    }

    SceneAnimatedAdapterResult SceneAnimatedModelAdapter::adaptImportedScene(
        const Assets::Assimp::ImportedScene& importedScene,
        const std::filesystem::path& sourcePath,
        AssetCache& assetCache,
        const SceneAnimatedAdapterSettings& settings)
    {
        SceneAnimatedAdapterResult result;
        result.diagnostics.importedNodeCount = static_cast<uint32_t>(importedScene.nodes.size());
        result.diagnostics.importedMeshCount = static_cast<uint32_t>(importedScene.meshes.size());
        result.diagnostics.importedMaterialCount = static_cast<uint32_t>(importedScene.materials.size());
        result.diagnostics.importedSkinCount = static_cast<uint32_t>(importedScene.skins.size());
        result.diagnostics.importedJointCount = static_cast<uint32_t>(importedScene.joints.size());
        result.diagnostics.importedAnimationCount = static_cast<uint32_t>(importedScene.animations.size());
        result.diagnostics.warnings = importedScene.diagnostics.warnings;

        if (!importedScene.success) {
            result.message = importedScene.error.empty() ? "Imported scene was not successful." : importedScene.error;
            diagnostics_ = result.diagnostics;
            return result;
        }
        if (!Assets::Assimp::containsSkeletalOrAnimationData(importedScene) ||
            importedScene.skins.empty() ||
            importedScene.joints.empty() ||
            importedScene.animations.empty()) {
            result.message = "Imported scene does not contain skins, joints, and animations.";
            diagnostics_ = result.diagnostics;
            return result;
        }

        ImportedSceneTextureSet textureSet;
        ImportedSceneMaterialMappingSettings materialSettings;
        materialSettings.materialNamePrefix = settings.materialNamePrefix;
        materialSettings.textureDebugNamePrefix = settings.textureDebugNamePrefix;
        materialSettings.loadTextures = settings.loadTextures;
        ImportedSceneTextureLoadStats textureStats = acquireImportedSceneMaterialTextures(
            sourcePath,
            importedScene.materials,
            assetCache,
            materialSettings,
            textureSet);
        result.diagnostics.textureLoadSuccessCount = textureStats.successCount;
        result.diagnostics.textureLoadFailureCount = textureStats.failureCount;
        result.diagnostics.textureFallbackCount = textureStats.fallbackCount;
        result.diagnostics.textureSrgbFallbackCount = textureStats.srgbFallbackCount;
        result.diagnostics.textureEstimatedBytes = textureStats.estimatedBytes;
        result.diagnostics.warnings.insert(
            result.diagnostics.warnings.end(),
            textureStats.warnings.begin(),
            textureStats.warnings.end());
        result.resources.textures = std::move(textureStats.acquiredTextures);

        result.resources.materials.reserve(importedScene.materials.size());
        for (uint32_t materialIndex = 0; materialIndex < importedScene.materials.size(); ++materialIndex) {
            const Renderer::MaterialDescriptor descriptor = importedSceneMaterialDescriptor(
                importedScene.materials[materialIndex],
                textureSet,
                materialIndex,
                settings.materialNamePrefix);
            const Renderer::MaterialHandle material = Renderer::createMaterial(descriptor);
            result.resources.materials.push_back(material);
            if (rendererHandleValid(material)) {
                ++result.diagnostics.createdMaterialCount;
            } else {
                result.diagnostics.warnings.push_back("Renderer rejected animated scene material " + std::to_string(materialIndex));
            }
        }

        result.nodes.reserve(importedScene.nodes.size());
        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            const SceneActorHandle actor = scene_.createActor();
            result.nodes.push_back({nodeIndex, actor});
            if (scene_.contains(actor)) {
                ++result.diagnostics.createdActorCount;
                if (std::optional<SceneTransform> transform = decomposeTransform(importedScene.nodes[nodeIndex].localTransform)) {
                    scene_.setLocalTransform(actor, *transform);
                } else {
                    ++result.diagnostics.invalidNodeReferenceCount;
                    result.diagnostics.warnings.push_back("Failed to decompose animated scene node transform " + std::to_string(nodeIndex));
                }
            }
        }

        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            const uint32_t parentIndex = importedScene.nodes[nodeIndex].parentIndex;
            if (parentIndex == UINT32_MAX) {
                continue;
            }
            if (parentIndex >= result.nodes.size()) {
                ++result.diagnostics.invalidNodeReferenceCount;
                continue;
            }
            scene_.attachChild(result.nodes[nodeIndex].actor, result.nodes[parentIndex].actor, false);
        }

        result.skeletons.reserve(importedScene.skins.size());
        for (uint32_t skinIndex = 0; skinIndex < importedScene.skins.size(); ++skinIndex) {
            const SceneSkeletonHandle handle = allocateSkeleton();
            SkeletonRecord* skeleton = record(handle);
            skeleton->importedSkinIndex = skinIndex;
            const Assets::Assimp::ImportedSceneSkin& importedSkin = importedScene.skins[skinIndex];
            for (uint32_t jointIndex : importedSkin.jointIndices) {
                skeleton->importedJointIndices.push_back(jointIndex);
                if (jointIndex < importedScene.joints.size() &&
                    importedScene.joints[jointIndex].nodeIndex &&
                    *importedScene.joints[jointIndex].nodeIndex < result.nodes.size()) {
                    skeleton->jointActors.push_back(result.nodes[*importedScene.joints[jointIndex].nodeIndex].actor);
                } else {
                    ++result.diagnostics.invalidJointReferenceCount;
                    skeleton->jointActors.push_back({});
                }
            }
            result.skeletons.push_back({skinIndex, handle, skeleton->importedJointIndices, skeleton->jointActors});
            ++result.diagnostics.createdSkeletonCount;
        }

        std::optional<Renderer::MaterialHandle> fallbackMaterial;
        result.resources.skinnedMeshes.resize(importedScene.meshes.size());
        if (settings.createSkinnedMeshes) {
            for (uint32_t meshIndex = 0; meshIndex < importedScene.meshes.size(); ++meshIndex) {
                const Assets::Assimp::ImportedSceneMesh& importedMesh = importedScene.meshes[meshIndex];
                if (importedMesh.primitives.empty()) {
                    continue;
                }

                Renderer::SkinnedMeshDescriptor descriptor;
                descriptor.name = importedMesh.name;
                descriptor.jointCount = static_cast<uint32_t>(importedScene.joints.size());
                descriptor.submeshes.reserve(importedMesh.primitives.size());
                const uint32_t skinIndex = animatedSkinIndexForMesh(importedScene, meshIndex);
                if (skinIndex == UINT32_MAX) {
                    ++result.diagnostics.invalidSkinReferenceCount;
                }

                AnimatedSkinnedVertexPackingStats packingStats;
                for (const Assets::Assimp::ImportedScenePrimitive& primitive : importedMesh.primitives) {
                    Renderer::SkinnedSubmeshDescriptor submesh;
                    submesh.vertices.reserve(primitive.vertices.size());
                    submesh.indices = primitive.indices;
                    submesh.skinIndex = skinIndex;
                    if (primitive.materialIndex < result.resources.materials.size() &&
                        rendererHandleValid(result.resources.materials[primitive.materialIndex])) {
                        submesh.material = result.resources.materials[primitive.materialIndex];
                    } else {
                        ++result.diagnostics.invalidMaterialReferenceCount;
                        submesh.material = ensureFallbackMaterial(fallbackMaterial, result.resources, result.diagnostics, settings);
                    }
                    for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                        submesh.vertices.push_back(animatedImportedSceneVertexToSkinnedMeshVertex(vertex, packingStats));
                    }
                    descriptor.submeshes.push_back(std::move(submesh));
                }

                descriptor.maxInfluencesPerVertex = packingStats.maxInfluenceCount;
                descriptor.truncatedInfluenceVertexCount = packingStats.truncatedInfluenceVertexCount;
                descriptor.zeroWeightVertexCount = packingStats.zeroWeightVertexCount;
                descriptor.normalizedWeightVertexCount = packingStats.normalizedWeightVertexCount;
                result.diagnostics.truncatedInfluenceVertexCount += packingStats.truncatedInfluenceVertexCount;
                result.diagnostics.zeroWeightVertexCount += packingStats.zeroWeightVertexCount;
                result.diagnostics.overBudgetJointCount += packingStats.overBudgetJointCount;

                const Renderer::SkinnedMeshHandle mesh = Renderer::createSkinnedMesh(descriptor);
                result.resources.skinnedMeshes[meshIndex] = mesh;
                if (rendererHandleValid(mesh)) {
                    ++result.diagnostics.createdSkinnedMeshCount;
                } else {
                    result.diagnostics.warnings.push_back("Renderer rejected animated scene skinned mesh " + std::to_string(meshIndex));
                }
            }
        }

        const std::vector<glm::mat4> bindPosePalette = animatedBindPosePalette(importedScene);
        SceneAnimatorHandle animatorHandle = allocateAnimator();
        AnimatorRecord* animator = record(animatorHandle);
        animator->imported = importedScene;
        animator->state.clipIndex = settings.defaultClipIndex < importedScene.animations.size() ? settings.defaultClipIndex : 0;
        animator->state.playing = settings.playOnStart;
        animator->state.loop = settings.loop;
        animator->state.speed = settings.playbackSpeed;

        uint32_t rootNodeIndex = 0;
        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            if (importedScene.nodes[nodeIndex].parentIndex == UINT32_MAX) {
                rootNodeIndex = nodeIndex;
                break;
            }
        }

        const auto attachSkinnedComponent =
            [&](uint32_t nodeIndex, uint32_t meshIndex, uint32_t skinIndex) -> bool {
                const uint32_t componentNodeIndex = nodeIndex;
                if (meshIndex >= result.resources.skinnedMeshes.size()) {
                    ++result.diagnostics.invalidMeshReferenceCount;
                    return false;
                }
                if (!rendererHandleValid(result.resources.skinnedMeshes[meshIndex])) {
                    ++result.diagnostics.invalidMeshReferenceCount;
                    result.diagnostics.warnings.push_back(
                        "Animated skinned mesh resource slot " + std::to_string(meshIndex) + " does not contain a valid renderer handle.");
                    return false;
                }
                if (componentNodeIndex >= result.nodes.size()) {
                    ++result.diagnostics.invalidNodeReferenceCount;
                    return false;
                }
                if (!scene_.contains(result.nodes[componentNodeIndex].actor)) {
                    ++result.diagnostics.invalidNodeReferenceCount;
                    result.diagnostics.warnings.push_back(
                        "Animated skinned mesh node " + std::to_string(componentNodeIndex) + " does not contain a live scene actor.");
                    return false;
                }

                SceneSkinnedMeshComponentDescriptor descriptor;
                descriptor.actor = result.nodes[componentNodeIndex].actor;
                descriptor.mesh = result.resources.skinnedMeshes[meshIndex];
                descriptor.layer = settings.renderLayer;
                descriptor.maxDrawDistance = settings.maxDrawDistance;
                descriptor.jointMatrices = bindPosePalette;
                descriptor.enabled = true;
                const SceneSkinnedMeshComponentHandle component = renderBridge_.attachSkinnedMesh(descriptor);
                if (!isValid(component)) {
                    ++result.diagnostics.invalidNodeReferenceCount;
                    result.diagnostics.warnings.push_back(
                        "Scene render bridge rejected skinned component for node " + std::to_string(componentNodeIndex) +
                        " and mesh " + std::to_string(meshIndex) + ".");
                    return false;
                }

                SkinnedRecord record;
                record.component = component;
                record.descriptor = descriptor;
                record.importedNodeIndex = componentNodeIndex;
                record.importedMeshIndex = meshIndex;
                record.importedSkinIndex = skinIndex;
                skinnedRecords_.push_back(record);
                animator->skinnedRecordIndices.push_back(static_cast<uint32_t>(skinnedRecords_.size() - 1));
                result.skinnedMeshes.push_back({componentNodeIndex, meshIndex, record.importedSkinIndex, component});
                ++result.diagnostics.createdSkinnedComponentCount;
                return true;
            };

        for (uint32_t nodeIndex = 0; nodeIndex < importedScene.nodes.size(); ++nodeIndex) {
            const Assets::Assimp::ImportedSceneNode& node = importedScene.nodes[nodeIndex];
            for (uint32_t meshIndex : node.meshIndices) {
                attachSkinnedComponent(nodeIndex, meshIndex, animatedSkinIndexForMesh(importedScene, meshIndex));
            }
        }

        if (result.diagnostics.createdSkinnedComponentCount == 0 && !result.nodes.empty()) {
            for (uint32_t meshIndex = 0; meshIndex < result.resources.skinnedMeshes.size(); ++meshIndex) {
                if (rendererHandleValid(result.resources.skinnedMeshes[meshIndex])) {
                    attachSkinnedComponent(
                        rootNodeIndex,
                        meshIndex,
                        animatedSkinIndexForMesh(importedScene, meshIndex));
                }
            }
            if (result.diagnostics.createdSkinnedComponentCount > 0) {
                result.diagnostics.warnings.push_back(
                    "Animated adapter used root-node fallback skinned mesh bindings because the import had no node mesh bindings.");
            }
        }

        result.animator = animatorHandle;
        updateAnimator(animatorHandle, 0.0f);
        result.diagnostics.sampledPoseCount = diagnostics_.sampledPoseCount;
        result.success = true;
        result.message = "Animated scene adapted.";
        diagnostics_ = result.diagnostics;
        diagnostics_.sampledPoseCount += record(animatorHandle)->lastPose.diagnostics.valid ? 1u : 0u;
        return result;
    }

    AnimatedSkeletonPose SceneAnimatedModelAdapter::sampleAnimatorPose(AnimatorRecord& record, float deltaSeconds)
    {
        if (!record.enabled) {
            return record.lastPose;
        }

        if (record.state.crossfade.active) {
            AnimationPlaybackState source = record.state.crossfade.source;
            AnimationPlaybackState target = record.state.crossfade.target;
            advanceImportedScenePlayback(record.imported, source, deltaSeconds);
            advanceImportedScenePlayback(record.imported, target, deltaSeconds);
            record.state.crossfade.source = source;
            record.state.crossfade.target = target;
            record.state.crossfade.elapsedSeconds += std::max(deltaSeconds, 0.0f);
            const float alpha = record.state.crossfade.durationSeconds > 0.0f
                ? std::min(record.state.crossfade.elapsedSeconds / record.state.crossfade.durationSeconds, 1.0f)
                : 1.0f;
            AnimationSampleSettings sourceSettings;
            sourceSettings.loop = source.loop;
            AnimationSampleSettings targetSettings;
            targetSettings.loop = target.loop;
            AnimatedSkeletonPose sourcePose = sampleImportedSceneClip(record.imported, true, source.clipIndex, source.timeSeconds, sourceSettings);
            AnimatedSkeletonPose targetPose = sampleImportedSceneClip(record.imported, true, target.clipIndex, target.timeSeconds, targetSettings);
            if (alpha >= 1.0f) {
                record.state.crossfade = {};
                fromPlaybackState(target, record.state);
                return targetPose;
            }
            return blendSkeletonPoses(sourcePose, targetPose, alpha);
        }

        AnimationPlaybackState playback = toPlaybackState(record.state);
        advanceImportedScenePlayback(record.imported, playback, deltaSeconds);
        fromPlaybackState(playback, record.state);
        AnimationSampleSettings settings;
        settings.loop = record.state.loop;
        return sampleImportedSceneClip(record.imported, true, record.state.clipIndex, record.state.timeSeconds, settings);
    }

    void SceneAnimatedModelAdapter::applyPoseToSkinnedRecords(AnimatorRecord& record, const AnimatedSkeletonPose& pose)
    {
        if (!pose.diagnostics.valid) {
            diagnostics_.warnings.insert(
                diagnostics_.warnings.end(),
                pose.diagnostics.warnings.begin(),
                pose.diagnostics.warnings.end());
            return;
        }

        for (uint32_t skinnedIndex : record.skinnedRecordIndices) {
            if (skinnedIndex >= skinnedRecords_.size()) {
                continue;
            }
            SkinnedRecord& skinned = skinnedRecords_[skinnedIndex];
            std::vector<glm::mat4> palette;
            palette.reserve(pose.joints.size());
            const std::optional<glm::mat4> ownerWorld = scene_.worldMatrix(skinned.descriptor.actor);
            const glm::mat4 inverseOwnerWorld = ownerWorld.has_value()
                ? glm::inverse(*ownerWorld)
                : glm::mat4{1.0f};
            for (const AnimatedJointPose& joint : pose.joints) {
                palette.push_back(inverseOwnerWorld * joint.finalSkinningMatrix);
            }
            skinned.descriptor.jointMatrices = palette;
            renderBridge_.setSkinnedMeshDescriptor(skinned.component, skinned.descriptor);
        }
    }

    bool SceneAnimatedModelAdapter::updateAnimator(SceneAnimatorHandle animator, float deltaSeconds)
    {
        AnimatorRecord* animatorRecord = record(animator);
        if (animatorRecord == nullptr) {
            return false;
        }
        if (!animatorRecord->enabled) {
            return true;
        }

        AnimatedSkeletonPose pose = sampleAnimatorPose(*animatorRecord, deltaSeconds);
        animatorRecord->lastPose = pose;
        applyPoseToSkinnedRecords(*animatorRecord, pose);
        if (pose.diagnostics.valid) {
            ++diagnostics_.sampledPoseCount;
        }
        return pose.diagnostics.valid;
    }

    void SceneAnimatedModelAdapter::updateAll(float deltaSeconds)
    {
        for (uint32_t index = 0; index < animators_.size(); ++index) {
            AnimatorRecord& animator = animators_[index];
            if (animator.occupied) {
                updateAnimator({index, animator.generation}, deltaSeconds);
            }
        }
    }

    std::optional<SceneAnimatorState> SceneAnimatedModelAdapter::animatorState(SceneAnimatorHandle animator) const
    {
        const AnimatorRecord* animatorRecord = record(animator);
        if (animatorRecord == nullptr) {
            return std::nullopt;
        }
        return animatorRecord->state;
    }

    bool SceneAnimatedModelAdapter::setAnimatorState(SceneAnimatorHandle animator, const SceneAnimatorState& state)
    {
        AnimatorRecord* animatorRecord = record(animator);
        if (animatorRecord == nullptr || state.clipIndex >= animatorRecord->imported.animations.size()) {
            return false;
        }
        animatorRecord->state = state;
        return updateAnimator(animator, 0.0f);
    }

    bool SceneAnimatedModelAdapter::beginCrossfade(SceneAnimatorHandle animator, uint32_t targetClipIndex, float durationSeconds)
    {
        AnimatorRecord* animatorRecord = record(animator);
        if (animatorRecord == nullptr || targetClipIndex >= animatorRecord->imported.animations.size()) {
            return false;
        }
        AnimationPlaybackState playback = toPlaybackState(animatorRecord->state);
        animatorRecord->state.crossfade = Engine::beginCrossfade(playback, targetClipIndex, durationSeconds);
        return true;
    }

    bool SceneAnimatedModelAdapter::setAnimatorEnabled(SceneAnimatorHandle animator, bool enabled)
    {
        AnimatorRecord* animatorRecord = record(animator);
        if (animatorRecord == nullptr) {
            return false;
        }
        animatorRecord->enabled = enabled;
        return true;
    }

    std::optional<AnimatedSkeletonPose> SceneAnimatedModelAdapter::lastPose(SceneAnimatorHandle animator) const
    {
        const AnimatorRecord* animatorRecord = record(animator);
        if (animatorRecord == nullptr) {
            return std::nullopt;
        }
        return animatorRecord->lastPose;
    }

    SceneSystemHandle SceneAnimatedModelAdapter::registerVariableAnimationSystem()
    {
        if (animationSystem_.has_value() && scene_.contains(*animationSystem_)) {
            return *animationSystem_;
        }

        SceneSystemDescriptor descriptor;
        descriptor.name = "SceneAnimatedModelAdapter";
        descriptor.phases = {SceneTickPhase::VariableAnimation};
        descriptor.onTick = [this](Scene&, const SceneTickContext& context) {
            updateAll(context.deltaSeconds);
        };
        const SceneSystemHandle handle = scene_.registerSystem(std::move(descriptor));
        if (isValid(handle)) {
            animationSystem_ = handle;
        }
        return handle;
    }

    bool SceneAnimatedModelAdapter::unregisterVariableAnimationSystem()
    {
        if (!animationSystem_.has_value()) {
            return false;
        }
        const bool removed = scene_.unregisterSystem(*animationSystem_);
        animationSystem_.reset();
        return removed;
    }

    void SceneAnimatedModelAdapter::releaseResources(SceneAnimatedResourceBinding& resources, AssetCache& assetCache)
    {
        for (Renderer::SkinnedMeshHandle& mesh : resources.skinnedMeshes) {
            if (rendererHandleValid(mesh)) {
                Renderer::destroySkinnedMesh(mesh);
                mesh = {};
            }
        }
        for (Renderer::StaticMeshHandle& mesh : resources.bindPoseMeshes) {
            if (rendererHandleValid(mesh)) {
                Renderer::destroyStaticMesh(mesh);
                mesh = {};
            }
        }
        for (Renderer::MaterialHandle& material : resources.materials) {
            if (rendererHandleValid(material)) {
                Renderer::destroyMaterial(material);
                material = {};
            }
        }
        for (const CachedTexture& texture : resources.textures) {
            assetCache.release(texture);
        }
        resources.textures.clear();
    }

    SceneAnimatedAdapterDiagnostics SceneAnimatedModelAdapter::diagnostics() const
    {
        return diagnostics_;
    }
}
