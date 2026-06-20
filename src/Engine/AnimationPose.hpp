#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Engine {
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

    AnimatedSkeletonPose blendSkeletonPoses(
        const AnimatedSkeletonPose& a,
        const AnimatedSkeletonPose& b,
        float weight);

    AnimationCrossfadeState beginCrossfade(
        const AnimationPlaybackState& currentPlayback,
        uint32_t targetClipIndex,
        float durationSeconds = DefaultAnimationCrossfadeSeconds);
}
