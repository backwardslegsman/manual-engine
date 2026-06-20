#include "Engine/AnimatedModelPose.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
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

    struct NodeTransformComponents {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    NodeTransformComponents decomposeNodeTransform(const glm::mat4& transform)
    {
        NodeTransformComponents components;
        components.translation = glm::vec3{transform[3]};

        const glm::vec3 column0{transform[0]};
        const glm::vec3 column1{transform[1]};
        const glm::vec3 column2{transform[2]};
        components.scale = {
            glm::length(column0),
            glm::length(column1),
            glm::length(column2),
        };

        glm::mat3 rotationMatrix{1.0f};
        rotationMatrix[0] = components.scale.x > 0.0f ? column0 / components.scale.x : glm::vec3{1.0f, 0.0f, 0.0f};
        rotationMatrix[1] = components.scale.y > 0.0f ? column1 / components.scale.y : glm::vec3{0.0f, 1.0f, 0.0f};
        rotationMatrix[2] = components.scale.z > 0.0f ? column2 / components.scale.z : glm::vec3{0.0f, 0.0f, 1.0f};
        components.rotation = glm::normalize(glm::quat_cast(rotationMatrix));
        return components;
    }

    glm::mat4 composeNodeTransform(const NodeTransformComponents& components)
    {
        glm::mat4 transform{1.0f};
        transform = glm::translate(transform, components.translation);
        transform *= glm::mat4_cast(glm::normalize(components.rotation));
        transform = glm::scale(transform, components.scale);
        return transform;
    }

    float normalizeSampleTime(float timeSeconds, float durationSeconds, bool loop)
    {
        if (durationSeconds <= 0.0f) {
            return 0.0f;
        }
        if (loop) {
            float wrapped = std::fmod(timeSeconds, durationSeconds);
            if (wrapped < 0.0f) {
                wrapped += durationSeconds;
            }
            return wrapped;
        }
        return std::clamp(timeSeconds, 0.0f, durationSeconds);
    }

    Assets::Assimp::ImportedSceneAnimationInterpolation interpolationFor(
        Assets::Assimp::ImportedSceneAnimationInterpolation interpolation,
        Engine::AnimatedPoseDiagnostics& diagnostics)
    {
        if (interpolation == Assets::Assimp::ImportedSceneAnimationInterpolation::CubicSpline) {
            ++diagnostics.unsupportedInterpolationCount;
            diagnostics.warnings.push_back("Cubic spline animation interpolation is deferred; linear fallback was used.");
            return Assets::Assimp::ImportedSceneAnimationInterpolation::Linear;
        }
        if (interpolation == Assets::Assimp::ImportedSceneAnimationInterpolation::Unknown) {
            return Assets::Assimp::ImportedSceneAnimationInterpolation::Linear;
        }
        return interpolation;
    }

    glm::vec3 sampleVec3Keys(
        const std::vector<Assets::Assimp::ImportedSceneVec3Key>& keys,
        float timeSeconds,
        const glm::vec3& fallback,
        Engine::AnimatedPoseDiagnostics& diagnostics)
    {
        if (keys.empty()) {
            return fallback;
        }
        if (timeSeconds <= keys.front().timeSeconds) {
            return keys.front().value;
        }
        if (timeSeconds >= keys.back().timeSeconds) {
            return keys.back().value;
        }

        for (uint32_t index = 0; index + 1 < keys.size(); ++index) {
            const auto& a = keys[index];
            const auto& b = keys[index + 1];
            if (timeSeconds < a.timeSeconds || timeSeconds > b.timeSeconds) {
                continue;
            }
            const auto interpolation = interpolationFor(a.interpolation, diagnostics);
            if (interpolation == Assets::Assimp::ImportedSceneAnimationInterpolation::Step) {
                return a.value;
            }
            const float span = std::max(b.timeSeconds - a.timeSeconds, 0.000001f);
            const float t = std::clamp((timeSeconds - a.timeSeconds) / span, 0.0f, 1.0f);
            return glm::mix(a.value, b.value, t);
        }
        return keys.back().value;
    }

    glm::quat sampleQuatKeys(
        const std::vector<Assets::Assimp::ImportedSceneQuatKey>& keys,
        float timeSeconds,
        const glm::quat& fallback,
        Engine::AnimatedPoseDiagnostics& diagnostics)
    {
        if (keys.empty()) {
            return fallback;
        }
        if (timeSeconds <= keys.front().timeSeconds) {
            return glm::normalize(keys.front().value);
        }
        if (timeSeconds >= keys.back().timeSeconds) {
            return glm::normalize(keys.back().value);
        }

        for (uint32_t index = 0; index + 1 < keys.size(); ++index) {
            const auto& a = keys[index];
            const auto& b = keys[index + 1];
            if (timeSeconds < a.timeSeconds || timeSeconds > b.timeSeconds) {
                continue;
            }
            const auto interpolation = interpolationFor(a.interpolation, diagnostics);
            if (interpolation == Assets::Assimp::ImportedSceneAnimationInterpolation::Step) {
                return glm::normalize(a.value);
            }
            const float span = std::max(b.timeSeconds - a.timeSeconds, 0.000001f);
            const float t = std::clamp((timeSeconds - a.timeSeconds) / span, 0.0f, 1.0f);
            return glm::normalize(glm::slerp(glm::normalize(a.value), glm::normalize(b.value), t));
        }
        return glm::normalize(keys.back().value);
    }

    void computeNodeModelTransforms(
        const Assets::Assimp::ImportedScene& imported,
        const std::vector<glm::mat4>& localTransforms,
        std::vector<glm::mat4>& modelTransforms,
        const std::optional<glm::mat4>& rootTransform)
    {
        modelTransforms.assign(localTransforms.size(), glm::mat4{1.0f});
        for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
            const Assets::Assimp::ImportedSceneNode& node = imported.nodes[nodeIndex];
            const glm::mat4 parent = node.parentIndex != UINT32_MAX && node.parentIndex < modelTransforms.size()
                ? modelTransforms[node.parentIndex]
                : rootTransform.value_or(glm::mat4{1.0f});
            modelTransforms[nodeIndex] = parent * localTransforms[nodeIndex];
        }
    }
}

namespace Engine {
    const glm::mat4* AnimatedSkeletonPose::jointModelTransform(uint32_t jointIndex) const
    {
        if (jointIndex >= joints.size()) {
            return nullptr;
        }
        return &joints[jointIndex].modelTransform;
    }

    const glm::mat4* AnimatedSkeletonPose::finalSkinningMatrix(uint32_t jointIndex) const
    {
        if (jointIndex >= joints.size()) {
            return nullptr;
        }
        return &joints[jointIndex].finalSkinningMatrix;
    }

    Renderer::SkinnedMeshVertex animatedImportedSceneVertexToSkinnedMeshVertex(
        const Assets::Assimp::ImportedSceneVertex& vertex,
        AnimatedSkinnedVertexPackingStats& stats)
    {
        Renderer::SkinnedMeshVertex skinned;
        skinned.px = vertex.position.x;
        skinned.py = vertex.position.y;
        skinned.pz = vertex.position.z;
        skinned.nx = vertex.normal.x;
        skinned.ny = vertex.normal.y;
        skinned.nz = vertex.normal.z;
        skinned.tx = vertex.tangent.x;
        skinned.ty = vertex.tangent.y;
        skinned.tz = vertex.tangent.z;
        skinned.tw = vertex.tangent.w;
        skinned.u = vertex.texcoord0.x;
        skinned.v = vertex.texcoord0.y;
        const glm::vec2 texcoord1 = vertex.hasTexcoord1 ? vertex.texcoord1 : vertex.texcoord0;
        skinned.u1 = texcoord1.x;
        skinned.v1 = texcoord1.y;
        skinned.abgr = packColorAbgr(vertex.hasColor0 ? vertex.color0 : glm::vec4{1.0f});

        std::vector<Assets::Assimp::ImportedSceneVertexInfluence> influences = vertex.influences;
        std::sort(influences.begin(), influences.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.weight > rhs.weight;
        });

        stats.maxInfluenceCount = std::max(stats.maxInfluenceCount, static_cast<uint32_t>(influences.size()));
        if (influences.size() > Renderer::MaxSkinnedInfluencesPerVertex) {
            ++stats.truncatedInfluenceVertexCount;
            influences.resize(Renderer::MaxSkinnedInfluencesPerVertex);
        }

        std::array<Assets::Assimp::ImportedSceneVertexInfluence, Renderer::MaxSkinnedInfluencesPerVertex> packed{};
        uint32_t packedCount = 0;
        for (const auto& influence : influences) {
            if (packedCount >= Renderer::MaxSkinnedInfluencesPerVertex) {
                break;
            }
            if (influence.jointIndex >= Renderer::MaxSkinnedJointsPerMesh) {
                ++stats.overBudgetJointCount;
                continue;
            }
            packed[packedCount++] = influence;
        }

        float weightSum = 0.0f;
        for (uint32_t index = 0; index < packedCount; ++index) {
            weightSum += std::max(packed[index].weight, 0.0f);
        }

        if (packedCount == 0 || weightSum <= 0.0f) {
            ++stats.zeroWeightVertexCount;
            return skinned;
        }

        if (std::abs(weightSum - 1.0f) > 0.0001f) {
            ++stats.normalizedWeightVertexCount;
        }

        for (uint32_t index = 0; index < packedCount; ++index) {
            skinned.joints[index] = static_cast<float>(packed[index].jointIndex);
            skinned.weights[index] = std::max(packed[index].weight, 0.0f) / weightSum;
        }

        return skinned;
    }

    uint32_t animatedSkinIndexForMesh(const Assets::Assimp::ImportedScene& imported, uint32_t meshIndex)
    {
        for (uint32_t skinIndex = 0; skinIndex < imported.skins.size(); ++skinIndex) {
            const Assets::Assimp::ImportedSceneSkin& skin = imported.skins[skinIndex];
            if (std::find(skin.meshIndices.begin(), skin.meshIndices.end(), meshIndex) != skin.meshIndices.end()) {
                return skinIndex;
            }
        }
        return UINT32_MAX;
    }

    std::vector<glm::mat4> animatedBindPosePalette(const Assets::Assimp::ImportedScene& imported)
    {
        std::vector<glm::mat4> palette;
        palette.reserve(imported.joints.size());
        for (const Assets::Assimp::ImportedSceneJoint& joint : imported.joints) {
            palette.push_back(joint.worldBindTransform * joint.inverseBindMatrix);
        }
        return palette;
    }

    std::vector<glm::mat4> animatedPosePalette(const AnimatedSkeletonPose& pose)
    {
        std::vector<glm::mat4> palette;
        palette.reserve(pose.joints.size());
        for (const AnimatedJointPose& joint : pose.joints) {
            palette.push_back(joint.finalSkinningMatrix);
        }
        return palette;
    }

    AnimatedSkeletonPose blendSkeletonPoses(
        const AnimatedSkeletonPose& a,
        const AnimatedSkeletonPose& b,
        float weight)
    {
        AnimatedSkeletonPose blended;
        blended.diagnostics.sourceJointCount = static_cast<uint32_t>(a.joints.size());
        blended.diagnostics.targetJointCount = static_cast<uint32_t>(b.joints.size());
        blended.diagnostics.jointCount = blended.diagnostics.sourceJointCount;

        if (!a.diagnostics.valid || !b.diagnostics.valid) {
            blended.diagnostics.warnings.push_back("Cannot blend invalid skeleton poses.");
            return blended;
        }
        if (a.joints.size() != b.joints.size()) {
            blended.diagnostics.mismatchedJointCount =
                static_cast<uint32_t>(std::max(a.joints.size(), b.joints.size()) - std::min(a.joints.size(), b.joints.size()));
            blended.diagnostics.warnings.push_back("Cannot blend skeleton poses with different joint counts.");
            return blended;
        }

        const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
        blended.diagnostics.blendWeight = clampedWeight;
        if (clampedWeight != weight) {
            blended.diagnostics.blendWeightClamped = true;
            blended.diagnostics.warnings.push_back("Animation blend weight was clamped to [0, 1].");
        }

        blended.joints.reserve(a.joints.size());
        for (uint32_t jointIndex = 0; jointIndex < a.joints.size(); ++jointIndex) {
            const AnimatedJointPose& source = a.joints[jointIndex];
            const AnimatedJointPose& target = b.joints[jointIndex];
            const NodeTransformComponents sourceLocal = decomposeNodeTransform(source.localTransform);
            const NodeTransformComponents targetLocal = decomposeNodeTransform(target.localTransform);
            const NodeTransformComponents sourceModel = decomposeNodeTransform(source.modelTransform);
            const NodeTransformComponents targetModel = decomposeNodeTransform(target.modelTransform);

            NodeTransformComponents blendedLocal;
            blendedLocal.translation = glm::mix(sourceLocal.translation, targetLocal.translation, clampedWeight);
            blendedLocal.rotation = glm::normalize(glm::slerp(sourceLocal.rotation, targetLocal.rotation, clampedWeight));
            blendedLocal.scale = glm::mix(sourceLocal.scale, targetLocal.scale, clampedWeight);

            NodeTransformComponents blendedModel;
            blendedModel.translation = glm::mix(sourceModel.translation, targetModel.translation, clampedWeight);
            blendedModel.rotation = glm::normalize(glm::slerp(sourceModel.rotation, targetModel.rotation, clampedWeight));
            blendedModel.scale = glm::mix(sourceModel.scale, targetModel.scale, clampedWeight);

            AnimatedJointPose jointPose;
            jointPose.localTransform = composeNodeTransform(blendedLocal);
            jointPose.modelTransform = composeNodeTransform(blendedModel);
            jointPose.inverseBindMatrix = source.inverseBindMatrix;
            jointPose.finalSkinningMatrix = jointPose.modelTransform * jointPose.inverseBindMatrix;
            blended.joints.push_back(jointPose);
        }

        blended.diagnostics.valid = true;
        return blended;
    }

    AnimationCrossfadeState beginCrossfade(
        const AnimationPlaybackState& currentPlayback,
        uint32_t targetClipIndex,
        float durationSeconds)
    {
        AnimationCrossfadeState state;
        state.source = currentPlayback;
        state.target = currentPlayback;
        state.target.clipIndex = targetClipIndex;
        state.target.timeSeconds = 0.0f;
        state.targetClipIndex = targetClipIndex;
        state.durationSeconds = std::max(durationSeconds, 0.0001f);
        state.elapsedSeconds = 0.0f;
        state.active = currentPlayback.clipIndex != targetClipIndex;
        return state;
    }

    AnimatedSkeletonPose sampleImportedSceneBindPose(
        const Assets::Assimp::ImportedScene& imported,
        bool loaded)
    {
        AnimatedSkeletonPose pose;
        pose.diagnostics.valid = loaded;
        pose.diagnostics.jointCount = static_cast<uint32_t>(imported.joints.size());
        if (!loaded) {
            pose.diagnostics.warnings.push_back("Animated model is not loaded.");
            return pose;
        }

        pose.joints.reserve(imported.joints.size());
        for (const Assets::Assimp::ImportedSceneJoint& joint : imported.joints) {
            AnimatedJointPose jointPose;
            jointPose.localTransform = joint.localBindTransform;
            jointPose.modelTransform = joint.worldBindTransform;
            jointPose.inverseBindMatrix = joint.inverseBindMatrix;
            jointPose.finalSkinningMatrix = jointPose.modelTransform * jointPose.inverseBindMatrix;
            if (!joint.nodeIndex) {
                ++pose.diagnostics.missingJointNodeCount;
            }
            pose.joints.push_back(jointPose);
        }
        if (pose.diagnostics.missingJointNodeCount > 0) {
            pose.diagnostics.valid = false;
            pose.diagnostics.warnings.push_back("One or more joints are missing node associations.");
        }
        return pose;
    }

    AnimatedSkeletonPose sampleImportedSceneClip(
        const Assets::Assimp::ImportedScene& imported,
        bool loaded,
        uint32_t clipIndex,
        float timeSeconds,
        const AnimationSampleSettings& settings)
    {
        AnimatedSkeletonPose pose;
        pose.diagnostics.sampledClipIndex = clipIndex;
        pose.diagnostics.jointCount = static_cast<uint32_t>(imported.joints.size());
        if (!loaded) {
            pose.diagnostics.warnings.push_back("Animated model is not loaded.");
            return pose;
        }
        if (clipIndex >= imported.animations.size()) {
            pose.diagnostics.warnings.push_back("Animation clip index is invalid.");
            return pose;
        }

        const Assets::Assimp::ImportedSceneAnimationClip& clip = imported.animations[clipIndex];
        const float sampleTime = normalizeSampleTime(timeSeconds, clip.durationSeconds, settings.loop);
        pose.diagnostics.sampledTimeSeconds = sampleTime;

        std::vector<glm::mat4> localTransforms;
        localTransforms.reserve(imported.nodes.size());
        for (const Assets::Assimp::ImportedSceneNode& node : imported.nodes) {
            localTransforms.push_back(node.localTransform);
        }

        for (const Assets::Assimp::ImportedSceneAnimationChannel& channel : clip.channels) {
            if (!channel.targetNodeIndex || *channel.targetNodeIndex >= localTransforms.size()) {
                ++pose.diagnostics.missingChannelTargetCount;
                continue;
            }
            NodeTransformComponents components = decomposeNodeTransform(localTransforms[*channel.targetNodeIndex]);
            components.translation = sampleVec3Keys(channel.translationKeys, sampleTime, components.translation, pose.diagnostics);
            components.rotation = sampleQuatKeys(channel.rotationKeys, sampleTime, components.rotation, pose.diagnostics);
            components.scale = sampleVec3Keys(channel.scaleKeys, sampleTime, components.scale, pose.diagnostics);
            localTransforms[*channel.targetNodeIndex] = composeNodeTransform(components);
        }

        std::vector<glm::mat4> modelTransforms;
        computeNodeModelTransforms(imported, localTransforms, modelTransforms, settings.rootTransform);

        pose.joints.reserve(imported.joints.size());
        for (const Assets::Assimp::ImportedSceneJoint& joint : imported.joints) {
            AnimatedJointPose jointPose;
            jointPose.localTransform = joint.localBindTransform;
            jointPose.modelTransform = joint.worldBindTransform;
            jointPose.inverseBindMatrix = joint.inverseBindMatrix;
            if (joint.nodeIndex && *joint.nodeIndex < modelTransforms.size()) {
                jointPose.localTransform = localTransforms[*joint.nodeIndex];
                jointPose.modelTransform = modelTransforms[*joint.nodeIndex];
            } else {
                ++pose.diagnostics.missingJointNodeCount;
            }
            jointPose.finalSkinningMatrix = jointPose.modelTransform * jointPose.inverseBindMatrix;
            pose.joints.push_back(jointPose);
        }

        pose.diagnostics.valid = pose.diagnostics.missingJointNodeCount == 0 &&
            pose.diagnostics.missingChannelTargetCount == 0;
        if (pose.diagnostics.missingJointNodeCount > 0) {
            pose.diagnostics.warnings.push_back("One or more joints are missing node associations.");
        }
        if (pose.diagnostics.missingChannelTargetCount > 0) {
            pose.diagnostics.warnings.push_back("One or more animation channels target missing nodes.");
        }
        return pose;
    }

    void advanceImportedScenePlayback(
        const Assets::Assimp::ImportedScene& imported,
        AnimationPlaybackState& state,
        float deltaSeconds)
    {
        if (!state.playing || state.clipIndex >= imported.animations.size()) {
            return;
        }

        const float duration = imported.animations[state.clipIndex].durationSeconds;
        state.timeSeconds += deltaSeconds * state.speed;
        state.timeSeconds = normalizeSampleTime(state.timeSeconds, duration, state.loop);
    }
}
