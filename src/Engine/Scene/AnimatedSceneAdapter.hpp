#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AnimatedModel.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct SceneSkeletonHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    struct SceneAnimatorHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    [[nodiscard]] constexpr bool isValid(SceneSkeletonHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool isValid(SceneAnimatorHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(SceneSkeletonHandle lhs, SceneSkeletonHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneSkeletonHandle lhs, SceneSkeletonHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr bool operator==(SceneAnimatorHandle lhs, SceneAnimatorHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneAnimatorHandle lhs, SceneAnimatorHandle rhs)
    {
        return !(lhs == rhs);
    }

    struct SceneAnimatedNodeBinding {
        uint32_t importedNodeIndex = UINT32_MAX;
        SceneActorHandle actor;
    };

    struct SceneSkeletonBinding {
        uint32_t importedSkinIndex = UINT32_MAX;
        SceneSkeletonHandle skeleton;
        std::vector<uint32_t> importedJointIndices;
        std::vector<SceneActorHandle> jointActors;
    };

    struct SceneAnimatedMeshBinding {
        uint32_t importedNodeIndex = UINT32_MAX;
        uint32_t importedMeshIndex = UINT32_MAX;
        uint32_t importedSkinIndex = UINT32_MAX;
        SceneSkinnedMeshComponentHandle component;
    };

    struct SceneAnimatedResourceBinding {
        std::vector<Renderer::SkinnedMeshHandle> skinnedMeshes;
        std::vector<Renderer::StaticMeshHandle> bindPoseMeshes;
        std::vector<Renderer::MaterialHandle> materials;
        std::vector<CachedTexture> textures;
    };

    struct SceneAnimatedAdapterSettings {
        bool loadTextures = true;
        bool createSkinnedMeshes = true;
        bool createBindPoseMeshes = false;
        Renderer::RenderLayer renderLayer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
        uint32_t defaultClipIndex = 0;
        bool playOnStart = true;
        bool loop = true;
        float playbackSpeed = 1.0f;
        std::string materialNamePrefix = "SceneAnimated";
        std::string textureDebugNamePrefix = "SceneAnimated";
    };

    struct SceneAnimatedAdapterDiagnostics {
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t importedSkinCount = 0;
        uint32_t importedJointCount = 0;
        uint32_t importedAnimationCount = 0;
        uint32_t createdActorCount = 0;
        uint32_t createdSkeletonCount = 0;
        uint32_t createdSkinnedMeshCount = 0;
        uint32_t createdSkinnedComponentCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t createdBindPoseMeshCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t textureFallbackCount = 0;
        uint32_t textureSrgbFallbackCount = 0;
        uint64_t textureEstimatedBytes = 0;
        uint32_t invalidNodeReferenceCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidSkinReferenceCount = 0;
        uint32_t invalidJointReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t overBudgetJointCount = 0;
        uint32_t sampledPoseCount = 0;
        std::vector<std::string> warnings;
    };

    struct SceneAnimatorState {
        uint32_t clipIndex = 0;
        float timeSeconds = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
        AnimationCrossfadeState crossfade;
    };

    struct SceneAnimatedAdapterResult {
        bool success = false;
        std::string message;
        std::vector<SceneAnimatedNodeBinding> nodes;
        std::vector<SceneSkeletonBinding> skeletons;
        std::vector<SceneAnimatedMeshBinding> skinnedMeshes;
        SceneAnimatorHandle animator;
        SceneAnimatedResourceBinding resources;
        SceneAnimatedAdapterDiagnostics diagnostics;
    };

    class SceneAnimatedModelAdapter {
    public:
        SceneAnimatedModelAdapter(Scene& scene, SceneRenderBridge& renderBridge);
        ~SceneAnimatedModelAdapter();

        SceneAnimatedModelAdapter(const SceneAnimatedModelAdapter&) = delete;
        SceneAnimatedModelAdapter& operator=(const SceneAnimatedModelAdapter&) = delete;

        [[nodiscard]] SceneAnimatedAdapterResult adaptImportedScene(
            const Assets::Assimp::ImportedScene& importedScene,
            const std::filesystem::path& sourcePath,
            AssetCache& assetCache,
            const SceneAnimatedAdapterSettings& settings = {});

        bool updateAnimator(SceneAnimatorHandle animator, float deltaSeconds);
        void updateAll(float deltaSeconds);

        [[nodiscard]] std::optional<SceneAnimatorState> animatorState(SceneAnimatorHandle animator) const;
        bool setAnimatorState(SceneAnimatorHandle animator, const SceneAnimatorState& state);
        bool beginCrossfade(SceneAnimatorHandle animator, uint32_t targetClipIndex, float durationSeconds);
        bool setAnimatorEnabled(SceneAnimatorHandle animator, bool enabled);
        [[nodiscard]] std::optional<AnimatedSkeletonPose> lastPose(SceneAnimatorHandle animator) const;

        [[nodiscard]] SceneSystemHandle registerVariableAnimationSystem();
        bool unregisterVariableAnimationSystem();

        void releaseResources(SceneAnimatedResourceBinding& resources, AssetCache& assetCache);
        [[nodiscard]] SceneAnimatedAdapterDiagnostics diagnostics() const;

    private:
        struct SkeletonRecord {
            uint32_t generation = 0;
            bool occupied = false;
            uint32_t importedSkinIndex = UINT32_MAX;
            std::vector<uint32_t> importedJointIndices;
            std::vector<SceneActorHandle> jointActors;
        };

        struct SkinnedRecord {
            SceneSkinnedMeshComponentHandle component;
            SceneSkinnedMeshComponentDescriptor descriptor;
            uint32_t importedNodeIndex = UINT32_MAX;
            uint32_t importedMeshIndex = UINT32_MAX;
            uint32_t importedSkinIndex = UINT32_MAX;
        };

        struct AnimatorRecord {
            uint32_t generation = 0;
            bool occupied = false;
            bool enabled = true;
            Assets::Assimp::ImportedScene imported;
            SceneAnimatorState state;
            AnimatedSkeletonPose lastPose;
            std::vector<uint32_t> skinnedRecordIndices;
        };

        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        [[nodiscard]] SkeletonRecord* record(SceneSkeletonHandle handle);
        [[nodiscard]] const SkeletonRecord* record(SceneSkeletonHandle handle) const;
        [[nodiscard]] AnimatorRecord* record(SceneAnimatorHandle handle);
        [[nodiscard]] const AnimatorRecord* record(SceneAnimatorHandle handle) const;
        [[nodiscard]] SceneSkeletonHandle allocateSkeleton();
        [[nodiscard]] SceneAnimatorHandle allocateAnimator();

        [[nodiscard]] AnimatedSkeletonPose sampleAnimatorPose(AnimatorRecord& record, float deltaSeconds);
        void applyPoseToSkinnedRecords(AnimatorRecord& record, const AnimatedSkeletonPose& pose);

        Scene& scene_;
        SceneRenderBridge& renderBridge_;
        std::vector<SkeletonRecord> skeletons_;
        std::vector<AnimatorRecord> animators_;
        std::vector<SkinnedRecord> skinnedRecords_;
        std::optional<SceneSystemHandle> animationSystem_;
        SceneAnimatedAdapterDiagnostics diagnostics_;
    };
}
