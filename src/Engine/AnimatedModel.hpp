#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/AnimatedModelCache.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct AnimatedModelBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        bool valid = false;
    };

    struct AnimatedModelLoadSettings {
        bool loadTextures = true;
        Renderer::RenderLayer renderLayer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
        bool createBindPoseInstances = true;
        bool createSkinnedMeshes = true;
        bool createSkinnedInstances = false;
        AnimatedModelCacheSettings cache;
    };

    struct AnimatedModelDiagnostics {
        Assets::Assimp::ImportedSceneSourceFormat sourceFormat = Assets::Assimp::ImportedSceneSourceFormat::Unknown;
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedPrimitiveCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t importedTextureCount = 0;
        uint32_t importedJointCount = 0;
        uint32_t importedSkinCount = 0;
        uint32_t importedAnimationCount = 0;
        uint32_t importedAnimationChannelCount = 0;
        uint32_t createdMeshCount = 0;
        uint32_t createdSkinnedMeshCount = 0;
        uint32_t createdSkinnedInstanceCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t createdInstanceCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t fallbackTextureCount = 0;
        uint64_t textureEstimatedBytes = 0;
        uint32_t textureSrgbFallbackCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t overBudgetJointCount = 0;
        AnimatedModelCacheStatus cacheStatus = AnimatedModelCacheStatus::Miss;
        std::filesystem::path cachePath;
        std::string cacheIdentityHash;
        std::string cacheMessage;
        bool loadedFromCache = false;
        uint32_t cacheReadCount = 0;
        uint32_t cacheWriteCount = 0;
        uint32_t cacheMissCount = 0;
        uint32_t cacheStaleCount = 0;
        uint32_t cacheCorruptCount = 0;
        float cacheReadMs = 0.0f;
        float cacheWriteMs = 0.0f;
        float sourceImportMs = 0.0f;
        std::string asyncPhase;
        uint32_t asyncJobsQueued = 0;
        uint32_t asyncJobsCompleted = 0;
        uint32_t asyncJobsFailed = 0;
        uint32_t asyncPendingJobs = 0;
        std::string asyncMessage;
        bool boundsValid = false;
        std::vector<std::string> warnings;
    };

    struct AnimatedModelDiagnosticsSummary {
        Assets::Assimp::ImportedSceneSourceFormat sourceFormat = Assets::Assimp::ImportedSceneSourceFormat::Unknown;
        std::string sourceFormatName;
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedPrimitiveCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t importedTextureCount = 0;
        uint32_t importedJointCount = 0;
        uint32_t importedSkinCount = 0;
        uint32_t importedAnimationCount = 0;
        uint32_t importedAnimationChannelCount = 0;
        uint32_t createdMeshCount = 0;
        uint32_t createdSkinnedMeshCount = 0;
        uint32_t createdInstanceCount = 0;
        uint32_t createdSkinnedInstanceCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t fallbackTextureCount = 0;
        uint64_t textureEstimatedBytes = 0;
        uint32_t textureSrgbFallbackCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t overBudgetJointCount = 0;
        AnimatedModelCacheStatus cacheStatus = AnimatedModelCacheStatus::Miss;
        bool loadedFromCache = false;
        uint32_t cacheReadCount = 0;
        uint32_t cacheWriteCount = 0;
        uint32_t cacheMissCount = 0;
        uint32_t cacheStaleCount = 0;
        uint32_t cacheCorruptCount = 0;
        std::filesystem::path cachePath;
        std::string cacheIdentityHash;
        std::string cacheMessage;
        float cacheReadMs = 0.0f;
        float cacheWriteMs = 0.0f;
        float sourceImportMs = 0.0f;
        std::string asyncPhase;
        uint32_t asyncJobsQueued = 0;
        uint32_t asyncJobsCompleted = 0;
        uint32_t asyncJobsFailed = 0;
        uint32_t asyncPendingJobs = 0;
        std::string asyncMessage;
        bool boundsValid = false;
        uint32_t warningCount = 0;
        std::string lastWarning;
    };

    AnimatedModelDiagnosticsSummary summarizeAnimatedModelDiagnostics(const AnimatedModelDiagnostics& diagnostics);
    std::string animatedModelDiagnosticsSummaryText(const AnimatedModelDiagnostics& diagnostics);
    std::string animatedModelDiagnosticsSummaryYaml(const AnimatedModelDiagnostics& diagnostics);

    struct AnimatedModelInstance {
        uint32_t nodeIndex = UINT32_MAX;
        uint32_t meshIndex = UINT32_MAX;
        Renderer::MeshInstanceHandle handle;
        glm::mat4 transform{1.0f};
    };

    struct AnimatedModelSkinnedInstance {
        uint32_t nodeIndex = UINT32_MAX;
        uint32_t meshIndex = UINT32_MAX;
        Renderer::SkinnedMeshInstanceHandle handle;
        glm::mat4 transform{1.0f};
    };

    struct AnimatedPoseDiagnostics {
        bool valid = false;
        uint32_t sampledClipIndex = UINT32_MAX;
        float sampledTimeSeconds = 0.0f;
        uint32_t jointCount = 0;
        uint32_t sourceJointCount = 0;
        uint32_t targetJointCount = 0;
        uint32_t missingJointNodeCount = 0;
        uint32_t missingChannelTargetCount = 0;
        uint32_t unsupportedInterpolationCount = 0;
        uint32_t mismatchedJointCount = 0;
        float blendWeight = 0.0f;
        bool blendWeightClamped = false;
        std::vector<std::string> warnings;
    };

    struct AnimatedJointPose {
        glm::mat4 localTransform{1.0f};
        glm::mat4 modelTransform{1.0f};
        glm::mat4 inverseBindMatrix{1.0f};
        glm::mat4 finalSkinningMatrix{1.0f};
    };

    struct AnimatedSkeletonPose {
        std::vector<AnimatedJointPose> joints;
        AnimatedPoseDiagnostics diagnostics;

        const glm::mat4* jointModelTransform(uint32_t jointIndex) const;
        const glm::mat4* finalSkinningMatrix(uint32_t jointIndex) const;
    };

    struct AnimationPlaybackState {
        uint32_t clipIndex = 0;
        float timeSeconds = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
    };

    struct AnimationSampleSettings {
        bool loop = true;
        std::optional<glm::mat4> rootTransform;
    };

    constexpr float DefaultAnimationCrossfadeSeconds = 0.25f;

    struct AnimationCrossfadeState {
        AnimationPlaybackState source;
        AnimationPlaybackState target;
        float durationSeconds = DefaultAnimationCrossfadeSeconds;
        float elapsedSeconds = 0.0f;
        uint32_t targetClipIndex = UINT32_MAX;
        bool active = false;
    };

    class AnimatedModel {
    public:
        AnimatedModel() = default;
        ~AnimatedModel();

        AnimatedModel(const AnimatedModel&) = delete;
        AnimatedModel& operator=(const AnimatedModel&) = delete;
        AnimatedModel(AnimatedModel&& other) noexcept;
        AnimatedModel& operator=(AnimatedModel&& other) noexcept;

        bool loaded() const;
        const AnimatedModelBounds& bounds() const;
        const AnimatedModelDiagnostics& diagnostics() const;
        const Assets::Assimp::ImportedScene& importedScene() const;
        uint32_t jointCount() const;
        uint32_t skinCount() const;
        uint32_t animationClipCount() const;
        uint32_t clipCount() const;
        float clipDuration(uint32_t clipIndex) const;
        uint32_t bindPoseInstanceCount() const;
        uint32_t skinnedMeshCount() const;
        uint32_t skinnedInstanceCount() const;
        std::optional<AnimatedModelInstance> bindPoseInstance(uint32_t index) const;
        std::optional<AnimatedModelSkinnedInstance> skinnedInstance(uint32_t index) const;
        AnimatedSkeletonPose bindPose() const;
        AnimatedSkeletonPose sampleClip(
            uint32_t clipIndex,
            float timeSeconds,
            const AnimationSampleSettings& settings = {}) const;
        void advancePlayback(AnimationPlaybackState& state, float deltaSeconds) const;
        bool updateSkinnedPose(uint32_t instanceIndex, const AnimatedSkeletonPose& pose);

        void shutdown();

    private:
        friend struct AnimatedModelLoadResult;
        friend AnimatedModelLoadResult loadAnimatedModel(
            const std::filesystem::path& path,
            AssetCache& assetCache,
            const AnimatedModelLoadSettings& settings);
        friend AnimatedModelLoadResult createAnimatedModelFromPayload(
            const std::filesystem::path& path,
            AnimatedModelCachePayload payload,
            AssetCache& assetCache,
            const AnimatedModelLoadSettings& settings);

        void moveFrom(AnimatedModel&& other) noexcept;

        bool loaded_ = false;
        AssetCache* assetCache_ = nullptr;
        Assets::Assimp::ImportedScene imported_;
        AnimatedModelBounds bounds_;
        AnimatedModelDiagnostics diagnostics_;
        std::vector<CachedTexture> textures_;
        std::vector<Renderer::MaterialHandle> materials_;
        std::vector<Renderer::StaticMeshHandle> meshes_;
        std::vector<Renderer::SkinnedMeshHandle> skinnedMeshes_;
        std::vector<AnimatedModelInstance> instances_;
        std::vector<AnimatedModelSkinnedInstance> skinnedInstances_;
    };

    struct AnimatedModelLoadResult {
        bool success = false;
        std::string message;
        AnimatedModel model;
    };

    AnimatedModelLoadResult loadAnimatedModel(
        const std::filesystem::path& path,
        AssetCache& assetCache,
        const AnimatedModelLoadSettings& settings = {});

    AnimatedModelLoadResult createAnimatedModelFromPayload(
        const std::filesystem::path& path,
        AnimatedModelCachePayload payload,
        AssetCache& assetCache,
        const AnimatedModelLoadSettings& settings = {});

    AnimatedSkeletonPose blendSkeletonPoses(
        const AnimatedSkeletonPose& a,
        const AnimatedSkeletonPose& b,
        float weight);

    AnimationCrossfadeState beginCrossfade(
        const AnimationPlaybackState& currentPlayback,
        uint32_t targetClipIndex,
        float durationSeconds = DefaultAnimationCrossfadeSeconds);

    AnimatedSkeletonPose advanceCrossfade(
        const AnimatedModel& model,
        AnimationCrossfadeState& state,
        AnimationPlaybackState& currentPlayback,
        float deltaSeconds);
}
