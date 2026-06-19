#include "Engine/AnimatedModel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/ImportedSceneResources.hpp"

namespace {
    Engine::AnimatedModelBounds convertBounds(const Assets::Assimp::ImportedSceneBounds& bounds)
    {
        return {bounds.min, bounds.max, bounds.valid};
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

    struct SkinnedVertexPackingStats {
        uint32_t maxInfluenceCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t normalizedWeightVertexCount = 0;
        uint32_t overBudgetJointCount = 0;
    };

    Renderer::SkinnedMeshVertex convertSkinnedVertex(
        const Assets::Assimp::ImportedSceneVertex& vertex,
        SkinnedVertexPackingStats& stats)
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
            skinned.joints[index] = static_cast<uint8_t>(packed[index].jointIndex);
            skinned.weights[index] = std::max(packed[index].weight, 0.0f) / weightSum;
        }

        return skinned;
    }

    uint32_t findSkinIndexForMesh(const Assets::Assimp::ImportedScene& imported, uint32_t meshIndex)
    {
        for (uint32_t skinIndex = 0; skinIndex < imported.skins.size(); ++skinIndex) {
            const Assets::Assimp::ImportedSceneSkin& skin = imported.skins[skinIndex];
            if (std::find(skin.meshIndices.begin(), skin.meshIndices.end(), meshIndex) != skin.meshIndices.end()) {
                return skinIndex;
            }
        }
        return UINT32_MAX;
    }

    std::vector<glm::mat4> makeBindPosePalette(const Assets::Assimp::ImportedScene& imported)
    {
        std::vector<glm::mat4> palette;
        palette.reserve(imported.joints.size());
        for (const Assets::Assimp::ImportedSceneJoint& joint : imported.joints) {
            palette.push_back(joint.worldBindTransform * joint.inverseBindMatrix);
        }
        return palette;
    }

    std::vector<glm::mat4> makePosePalette(const Engine::AnimatedSkeletonPose& pose)
    {
        std::vector<glm::mat4> palette;
        palette.reserve(pose.joints.size());
        for (const Engine::AnimatedJointPose& joint : pose.joints) {
            palette.push_back(joint.finalSkinningMatrix);
        }
        return palette;
    }

    void appendImportDiagnostics(
        const Assets::Assimp::ImportedScene& imported,
        Engine::AnimatedModelDiagnostics& diagnostics)
    {
        diagnostics.sourceFormat = imported.sourceFormat;
        diagnostics.importedNodeCount = imported.diagnostics.nodeCount;
        diagnostics.importedMeshCount = imported.diagnostics.meshCount;
        diagnostics.importedPrimitiveCount = imported.diagnostics.primitiveCount;
        diagnostics.importedMaterialCount = imported.diagnostics.materialCount;
        diagnostics.importedTextureCount = imported.diagnostics.textureCount;
        diagnostics.importedJointCount = imported.diagnostics.jointCount;
        diagnostics.importedSkinCount = imported.diagnostics.skinCount;
        diagnostics.importedAnimationCount = imported.diagnostics.animationCount;
        diagnostics.importedAnimationChannelCount = imported.diagnostics.animationChannelCount;
        diagnostics.boundsValid = imported.bounds.valid;
        diagnostics.warnings = imported.diagnostics.warnings;
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

    template <typename Function>
    float measureMilliseconds(Function&& function)
    {
        const auto start = std::chrono::steady_clock::now();
        function();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(end - start).count();
    }

    std::string quotedYamlString(const std::string& value)
    {
        std::string output = "\"";
        for (char character : value) {
            if (character == '\\' || character == '"') {
                output.push_back('\\');
            }
            output.push_back(character);
        }
        output.push_back('"');
        return output;
    }
}

namespace Engine {
    AnimatedModelDiagnosticsSummary summarizeAnimatedModelDiagnostics(const AnimatedModelDiagnostics& diagnostics)
    {
        AnimatedModelDiagnosticsSummary summary;
        summary.sourceFormat = diagnostics.sourceFormat;
        summary.sourceFormatName = Assets::Assimp::sourceFormatName(diagnostics.sourceFormat);
        summary.importedNodeCount = diagnostics.importedNodeCount;
        summary.importedMeshCount = diagnostics.importedMeshCount;
        summary.importedPrimitiveCount = diagnostics.importedPrimitiveCount;
        summary.importedMaterialCount = diagnostics.importedMaterialCount;
        summary.importedTextureCount = diagnostics.importedTextureCount;
        summary.importedJointCount = diagnostics.importedJointCount;
        summary.importedSkinCount = diagnostics.importedSkinCount;
        summary.importedAnimationCount = diagnostics.importedAnimationCount;
        summary.importedAnimationChannelCount = diagnostics.importedAnimationChannelCount;
        summary.createdMeshCount = diagnostics.createdMeshCount;
        summary.createdSkinnedMeshCount = diagnostics.createdSkinnedMeshCount;
        summary.createdInstanceCount = diagnostics.createdInstanceCount;
        summary.createdSkinnedInstanceCount = diagnostics.createdSkinnedInstanceCount;
        summary.createdMaterialCount = diagnostics.createdMaterialCount;
        summary.textureLoadSuccessCount = diagnostics.textureLoadSuccessCount;
        summary.textureLoadFailureCount = diagnostics.textureLoadFailureCount;
        summary.fallbackTextureCount = diagnostics.fallbackTextureCount;
        summary.textureEstimatedBytes = diagnostics.textureEstimatedBytes;
        summary.textureSrgbFallbackCount = diagnostics.textureSrgbFallbackCount;
        summary.invalidMeshReferenceCount = diagnostics.invalidMeshReferenceCount;
        summary.invalidMaterialReferenceCount = diagnostics.invalidMaterialReferenceCount;
        summary.truncatedInfluenceVertexCount = diagnostics.truncatedInfluenceVertexCount;
        summary.zeroWeightVertexCount = diagnostics.zeroWeightVertexCount;
        summary.overBudgetJointCount = diagnostics.overBudgetJointCount;
        summary.cacheStatus = diagnostics.cacheStatus;
        summary.loadedFromCache = diagnostics.loadedFromCache;
        summary.cacheReadCount = diagnostics.cacheReadCount;
        summary.cacheWriteCount = diagnostics.cacheWriteCount;
        summary.cacheMissCount = diagnostics.cacheMissCount;
        summary.cacheStaleCount = diagnostics.cacheStaleCount;
        summary.cacheCorruptCount = diagnostics.cacheCorruptCount;
        summary.cachePath = diagnostics.cachePath;
        summary.cacheIdentityHash = diagnostics.cacheIdentityHash;
        summary.cacheMessage = diagnostics.cacheMessage;
        summary.cacheReadMs = diagnostics.cacheReadMs;
        summary.cacheWriteMs = diagnostics.cacheWriteMs;
        summary.sourceImportMs = diagnostics.sourceImportMs;
        summary.asyncPhase = diagnostics.asyncPhase;
        summary.asyncJobsQueued = diagnostics.asyncJobsQueued;
        summary.asyncJobsCompleted = diagnostics.asyncJobsCompleted;
        summary.asyncJobsFailed = diagnostics.asyncJobsFailed;
        summary.asyncPendingJobs = diagnostics.asyncPendingJobs;
        summary.asyncMessage = diagnostics.asyncMessage;
        summary.boundsValid = diagnostics.boundsValid;
        summary.warningCount = static_cast<uint32_t>(diagnostics.warnings.size());
        if (!diagnostics.warnings.empty()) {
            summary.lastWarning = diagnostics.warnings.back();
        }
        return summary;
    }

    std::string animatedModelDiagnosticsSummaryText(const AnimatedModelDiagnostics& diagnostics)
    {
        const AnimatedModelDiagnosticsSummary summary = summarizeAnimatedModelDiagnostics(diagnostics);
        std::ostringstream output;
        output
            << "sourceFormat=" << summary.sourceFormatName
            << "; imported nodes=" << summary.importedNodeCount
            << " meshes=" << summary.importedMeshCount
            << " primitives=" << summary.importedPrimitiveCount
            << " materials=" << summary.importedMaterialCount
            << " textures=" << summary.importedTextureCount
            << " joints=" << summary.importedJointCount
            << " skins=" << summary.importedSkinCount
            << " animations=" << summary.importedAnimationCount
            << " channels=" << summary.importedAnimationChannelCount
            << "; created meshes=" << summary.createdMeshCount
            << " skinnedMeshes=" << summary.createdSkinnedMeshCount
            << " instances=" << summary.createdInstanceCount
            << " skinnedInstances=" << summary.createdSkinnedInstanceCount
            << " materials=" << summary.createdMaterialCount
            << "; textures ok=" << summary.textureLoadSuccessCount
            << " failed=" << summary.textureLoadFailureCount
            << " fallback=" << summary.fallbackTextureCount
            << " bytes=" << summary.textureEstimatedBytes
            << "; influences truncated=" << summary.truncatedInfluenceVertexCount
            << " zeroWeight=" << summary.zeroWeightVertexCount
            << " overBudgetJoints=" << summary.overBudgetJointCount
            << "; cache=" << animatedModelCacheStatusName(summary.cacheStatus)
            << " loadedFromCache=" << (summary.loadedFromCache ? "true" : "false")
            << " reads=" << summary.cacheReadCount
            << " writes=" << summary.cacheWriteCount
            << "; async=" << (summary.asyncPhase.empty() ? "n/a" : summary.asyncPhase)
            << " queued=" << summary.asyncJobsQueued
            << " completed=" << summary.asyncJobsCompleted
            << " failed=" << summary.asyncJobsFailed
            << " pending=" << summary.asyncPendingJobs
            << " importMs=" << std::fixed << std::setprecision(3) << summary.sourceImportMs
            << " cacheReadMs=" << summary.cacheReadMs
            << " cacheWriteMs=" << summary.cacheWriteMs
            << "; boundsValid=" << (summary.boundsValid ? "true" : "false")
            << "; warnings=" << summary.warningCount;
        if (!summary.lastWarning.empty()) {
            output << "; lastWarning=" << summary.lastWarning;
        }
        return output.str();
    }

    std::string animatedModelDiagnosticsSummaryYaml(const AnimatedModelDiagnostics& diagnostics)
    {
        const AnimatedModelDiagnosticsSummary summary = summarizeAnimatedModelDiagnostics(diagnostics);
        std::ostringstream output;
        output << "animated_model:\n";
        output << "  source_format: " << quotedYamlString(summary.sourceFormatName) << '\n';
        output << "  imported:\n";
        output << "    nodes: " << summary.importedNodeCount << '\n';
        output << "    meshes: " << summary.importedMeshCount << '\n';
        output << "    primitives: " << summary.importedPrimitiveCount << '\n';
        output << "    materials: " << summary.importedMaterialCount << '\n';
        output << "    textures: " << summary.importedTextureCount << '\n';
        output << "    joints: " << summary.importedJointCount << '\n';
        output << "    skins: " << summary.importedSkinCount << '\n';
        output << "    animations: " << summary.importedAnimationCount << '\n';
        output << "    animation_channels: " << summary.importedAnimationChannelCount << '\n';
        output << "  created:\n";
        output << "    meshes: " << summary.createdMeshCount << '\n';
        output << "    skinned_meshes: " << summary.createdSkinnedMeshCount << '\n';
        output << "    instances: " << summary.createdInstanceCount << '\n';
        output << "    skinned_instances: " << summary.createdSkinnedInstanceCount << '\n';
        output << "    materials: " << summary.createdMaterialCount << '\n';
        output << "  textures:\n";
        output << "    loaded: " << summary.textureLoadSuccessCount << '\n';
        output << "    failed: " << summary.textureLoadFailureCount << '\n';
        output << "    fallback: " << summary.fallbackTextureCount << '\n';
        output << "    estimated_bytes: " << summary.textureEstimatedBytes << '\n';
        output << "    srgb_fallback: " << summary.textureSrgbFallbackCount << '\n';
        output << "  influences:\n";
        output << "    truncated_vertices: " << summary.truncatedInfluenceVertexCount << '\n';
        output << "    zero_weight_vertices: " << summary.zeroWeightVertexCount << '\n';
        output << "    over_budget_joints: " << summary.overBudgetJointCount << '\n';
        output << "  references:\n";
        output << "    invalid_meshes: " << summary.invalidMeshReferenceCount << '\n';
        output << "    invalid_materials: " << summary.invalidMaterialReferenceCount << '\n';
        output << "  cache:\n";
        output << "    status: " << animatedModelCacheStatusName(summary.cacheStatus) << '\n';
        output << "    loaded_from_cache: " << (summary.loadedFromCache ? "true" : "false") << '\n';
        output << "    reads: " << summary.cacheReadCount << '\n';
        output << "    writes: " << summary.cacheWriteCount << '\n';
        output << "    misses: " << summary.cacheMissCount << '\n';
        output << "    stale: " << summary.cacheStaleCount << '\n';
        output << "    corrupt: " << summary.cacheCorruptCount << '\n';
        output << "    identity: " << quotedYamlString(summary.cacheIdentityHash) << '\n';
        output << "    path: " << quotedYamlString(summary.cachePath.generic_string()) << '\n';
        output << "    message: " << quotedYamlString(summary.cacheMessage) << '\n';
        output << "    read_ms: " << summary.cacheReadMs << '\n';
        output << "    write_ms: " << summary.cacheWriteMs << '\n';
        output << "    source_import_ms: " << summary.sourceImportMs << '\n';
        output << "  async:\n";
        output << "    phase: " << quotedYamlString(summary.asyncPhase) << '\n';
        output << "    queued: " << summary.asyncJobsQueued << '\n';
        output << "    completed: " << summary.asyncJobsCompleted << '\n';
        output << "    failed: " << summary.asyncJobsFailed << '\n';
        output << "    pending: " << summary.asyncPendingJobs << '\n';
        output << "    message: " << quotedYamlString(summary.asyncMessage) << '\n';
        output << "  bounds_valid: " << (summary.boundsValid ? "true" : "false") << '\n';
        output << "  warnings:\n";
        output << "    count: " << summary.warningCount << '\n';
        output << "    last: " << quotedYamlString(summary.lastWarning) << '\n';
        return output.str();
    }

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

    AnimatedSkeletonPose advanceCrossfade(
        const AnimatedModel& model,
        AnimationCrossfadeState& state,
        AnimationPlaybackState& currentPlayback,
        float deltaSeconds)
    {
        if (!state.active) {
            return model.sampleClip(currentPlayback.clipIndex, currentPlayback.timeSeconds, {currentPlayback.loop, std::nullopt});
        }

        state.source.playing = currentPlayback.playing;
        state.source.loop = currentPlayback.loop;
        state.source.speed = currentPlayback.speed;
        state.target.playing = currentPlayback.playing;
        state.target.loop = currentPlayback.loop;
        state.target.speed = currentPlayback.speed;

        if (currentPlayback.playing) {
            model.advancePlayback(state.source, deltaSeconds);
            model.advancePlayback(state.target, deltaSeconds);
            state.elapsedSeconds = std::min(state.elapsedSeconds + std::max(deltaSeconds, 0.0f), state.durationSeconds);
        }

        AnimationSampleSettings settings;
        settings.loop = currentPlayback.loop;
        AnimatedSkeletonPose sourcePose = model.sampleClip(state.source.clipIndex, state.source.timeSeconds, settings);
        AnimatedSkeletonPose targetPose = model.sampleClip(state.target.clipIndex, state.target.timeSeconds, settings);
        if (!sourcePose.diagnostics.valid || !targetPose.diagnostics.valid) {
            AnimatedSkeletonPose invalid;
            invalid.diagnostics.sourceJointCount = static_cast<uint32_t>(sourcePose.joints.size());
            invalid.diagnostics.targetJointCount = static_cast<uint32_t>(targetPose.joints.size());
            invalid.diagnostics.warnings.push_back("Animation crossfade failed to sample one or both clips.");
            state.active = false;
            return invalid;
        }

        const float weight = state.durationSeconds > 0.0f
            ? std::clamp(state.elapsedSeconds / state.durationSeconds, 0.0f, 1.0f)
            : 1.0f;
        AnimatedSkeletonPose blended = blendSkeletonPoses(sourcePose, targetPose, weight);
        if (blended.diagnostics.valid && state.elapsedSeconds >= state.durationSeconds) {
            currentPlayback = state.target;
            currentPlayback.clipIndex = state.targetClipIndex;
            state.active = false;
            return targetPose;
        }

        return blended;
    }

    AnimatedModel::~AnimatedModel()
    {
        shutdown();
    }

    AnimatedModel::AnimatedModel(AnimatedModel&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    AnimatedModel& AnimatedModel::operator=(AnimatedModel&& other) noexcept
    {
        if (this != &other) {
            shutdown();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void AnimatedModel::moveFrom(AnimatedModel&& other) noexcept
    {
        loaded_ = other.loaded_;
        assetCache_ = other.assetCache_;
        imported_ = std::move(other.imported_);
        bounds_ = other.bounds_;
        diagnostics_ = std::move(other.diagnostics_);
        textures_ = std::move(other.textures_);
        materials_ = std::move(other.materials_);
        meshes_ = std::move(other.meshes_);
        skinnedMeshes_ = std::move(other.skinnedMeshes_);
        instances_ = std::move(other.instances_);
        skinnedInstances_ = std::move(other.skinnedInstances_);

        other.loaded_ = false;
        other.assetCache_ = nullptr;
        other.textures_.clear();
        other.materials_.clear();
        other.meshes_.clear();
        other.skinnedMeshes_.clear();
        other.instances_.clear();
        other.skinnedInstances_.clear();
    }

    bool AnimatedModel::loaded() const
    {
        return loaded_;
    }

    const AnimatedModelBounds& AnimatedModel::bounds() const
    {
        return bounds_;
    }

    const AnimatedModelDiagnostics& AnimatedModel::diagnostics() const
    {
        return diagnostics_;
    }

    const Assets::Assimp::ImportedScene& AnimatedModel::importedScene() const
    {
        return imported_;
    }

    uint32_t AnimatedModel::jointCount() const
    {
        return static_cast<uint32_t>(imported_.joints.size());
    }

    uint32_t AnimatedModel::skinCount() const
    {
        return static_cast<uint32_t>(imported_.skins.size());
    }

    uint32_t AnimatedModel::animationClipCount() const
    {
        return static_cast<uint32_t>(imported_.animations.size());
    }

    uint32_t AnimatedModel::clipCount() const
    {
        return animationClipCount();
    }

    float AnimatedModel::clipDuration(uint32_t clipIndex) const
    {
        if (clipIndex >= imported_.animations.size()) {
            return 0.0f;
        }
        return imported_.animations[clipIndex].durationSeconds;
    }

    uint32_t AnimatedModel::bindPoseInstanceCount() const
    {
        return static_cast<uint32_t>(instances_.size());
    }

    uint32_t AnimatedModel::skinnedMeshCount() const
    {
        return static_cast<uint32_t>(skinnedMeshes_.size());
    }

    uint32_t AnimatedModel::skinnedInstanceCount() const
    {
        return static_cast<uint32_t>(skinnedInstances_.size());
    }

    std::optional<AnimatedModelInstance> AnimatedModel::bindPoseInstance(uint32_t index) const
    {
        if (index >= instances_.size()) {
            return std::nullopt;
        }
        return instances_[index];
    }

    std::optional<AnimatedModelSkinnedInstance> AnimatedModel::skinnedInstance(uint32_t index) const
    {
        if (index >= skinnedInstances_.size()) {
            return std::nullopt;
        }
        return skinnedInstances_[index];
    }

    AnimatedSkeletonPose AnimatedModel::bindPose() const
    {
        AnimatedSkeletonPose pose;
        pose.diagnostics.valid = loaded_;
        pose.diagnostics.jointCount = static_cast<uint32_t>(imported_.joints.size());
        if (!loaded_) {
            pose.diagnostics.warnings.push_back("Animated model is not loaded.");
            return pose;
        }

        pose.joints.reserve(imported_.joints.size());
        for (const Assets::Assimp::ImportedSceneJoint& joint : imported_.joints) {
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

    AnimatedSkeletonPose AnimatedModel::sampleClip(
        uint32_t clipIndex,
        float timeSeconds,
        const AnimationSampleSettings& settings) const
    {
        AnimatedSkeletonPose pose;
        pose.diagnostics.sampledClipIndex = clipIndex;
        pose.diagnostics.jointCount = static_cast<uint32_t>(imported_.joints.size());
        if (!loaded_) {
            pose.diagnostics.warnings.push_back("Animated model is not loaded.");
            return pose;
        }
        if (clipIndex >= imported_.animations.size()) {
            pose.diagnostics.warnings.push_back("Animation clip index is invalid.");
            return pose;
        }

        const Assets::Assimp::ImportedSceneAnimationClip& clip = imported_.animations[clipIndex];
        if (clip.channels.empty() || clip.durationSeconds <= 0.0f) {
            pose.diagnostics.warnings.push_back("Animation clip is empty.");
            return pose;
        }

        const float sampleTime = normalizeSampleTime(timeSeconds, clip.durationSeconds, settings.loop);
        pose.diagnostics.sampledTimeSeconds = sampleTime;

        std::vector<NodeTransformComponents> components;
        components.reserve(imported_.nodes.size());
        std::vector<glm::mat4> localTransforms;
        localTransforms.reserve(imported_.nodes.size());
        for (const Assets::Assimp::ImportedSceneNode& node : imported_.nodes) {
            components.push_back(decomposeNodeTransform(node.localTransform));
            localTransforms.push_back(node.localTransform);
        }

        for (const Assets::Assimp::ImportedSceneAnimationChannel& channel : clip.channels) {
            if (!channel.targetNodeIndex || *channel.targetNodeIndex >= components.size()) {
                ++pose.diagnostics.missingChannelTargetCount;
                continue;
            }
            NodeTransformComponents& target = components[*channel.targetNodeIndex];
            target.translation = sampleVec3Keys(channel.translationKeys, sampleTime, target.translation, pose.diagnostics);
            target.rotation = sampleQuatKeys(channel.rotationKeys, sampleTime, target.rotation, pose.diagnostics);
            target.scale = sampleVec3Keys(channel.scaleKeys, sampleTime, target.scale, pose.diagnostics);
            localTransforms[*channel.targetNodeIndex] = composeNodeTransform(target);
        }

        std::vector<glm::mat4> modelTransforms;
        computeNodeModelTransforms(imported_, localTransforms, modelTransforms, settings.rootTransform);

        pose.joints.reserve(imported_.joints.size());
        for (const Assets::Assimp::ImportedSceneJoint& joint : imported_.joints) {
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

    void AnimatedModel::advancePlayback(AnimationPlaybackState& state, float deltaSeconds) const
    {
        if (!state.playing || state.clipIndex >= imported_.animations.size()) {
            return;
        }

        const float duration = imported_.animations[state.clipIndex].durationSeconds;
        state.timeSeconds += deltaSeconds * state.speed;
        state.timeSeconds = normalizeSampleTime(state.timeSeconds, duration, state.loop);
    }

    bool AnimatedModel::updateSkinnedPose(uint32_t instanceIndex, const AnimatedSkeletonPose& pose)
    {
        if (instanceIndex >= skinnedInstances_.size() || !pose.diagnostics.valid) {
            return false;
        }

        const std::vector<glm::mat4> palette = makePosePalette(pose);
        Renderer::setSkinnedInstanceJointMatrices(skinnedInstances_[instanceIndex].handle, palette);
        return true;
    }

    void AnimatedModel::shutdown()
    {
        for (AnimatedModelInstance& instance : instances_) {
            Renderer::destroyInstance(instance.handle);
        }
        instances_.clear();

        for (AnimatedModelSkinnedInstance& instance : skinnedInstances_) {
            Renderer::destroySkinnedInstance(instance.handle);
        }
        skinnedInstances_.clear();

        for (Renderer::StaticMeshHandle mesh : meshes_) {
            Renderer::destroyStaticMesh(mesh);
        }
        meshes_.clear();

        for (Renderer::SkinnedMeshHandle mesh : skinnedMeshes_) {
            Renderer::destroySkinnedMesh(mesh);
        }
        skinnedMeshes_.clear();

        for (Renderer::MaterialHandle material : materials_) {
            Renderer::destroyMaterial(material);
        }
        materials_.clear();

        if (assetCache_) {
            for (CachedTexture texture : textures_) {
                assetCache_->release(texture);
            }
        }
        textures_.clear();

        loaded_ = false;
        assetCache_ = nullptr;
    }

    AnimatedModelLoadResult createAnimatedModelFromPayload(
        const std::filesystem::path& path,
        AnimatedModelCachePayload payload,
        AssetCache& assetCache,
        const AnimatedModelLoadSettings& settings)
    {
        AnimatedModelLoadResult result;
        result.model.assetCache_ = &assetCache;

        Assets::Assimp::ImportedScene imported = std::move(payload.scene);
        if (!imported.success) {
            result.message = imported.error;
            return result;
        }

        appendImportDiagnostics(imported, result.model.diagnostics_);
        result.model.bounds_ = convertBounds(imported.bounds);
        if (!Assets::Assimp::containsSkeletalOrAnimationData(imported)) {
            result.message = "Imported asset is not an animated model.";
            result.model.diagnostics_.warnings.push_back(result.message);
            return result;
        }

        ImportedSceneTextureSet textures;
        ImportedSceneMaterialMappingSettings materialMapping;
        materialMapping.materialNamePrefix = "animated.material";
        materialMapping.textureDebugNamePrefix = "animated";
        materialMapping.loadTextures = settings.loadTextures;
        ImportedSceneTextureLoadStats textureStats = acquireImportedSceneMaterialTextures(
            path,
            imported.materials,
            assetCache,
            materialMapping,
            textures);
        result.model.diagnostics_.textureLoadSuccessCount += textureStats.successCount;
        result.model.diagnostics_.textureLoadFailureCount += textureStats.failureCount;
        result.model.diagnostics_.fallbackTextureCount += textureStats.fallbackCount;
        result.model.diagnostics_.textureEstimatedBytes += textureStats.estimatedBytes;
        result.model.diagnostics_.textureSrgbFallbackCount += textureStats.srgbFallbackCount;
        result.model.diagnostics_.warnings.insert(
            result.model.diagnostics_.warnings.end(),
            textureStats.warnings.begin(),
            textureStats.warnings.end());
        result.model.textures_ = std::move(textureStats.acquiredTextures);

        result.model.materials_.reserve(imported.materials.size());
        for (uint32_t materialIndex = 0; materialIndex < imported.materials.size(); ++materialIndex) {
            Renderer::MaterialHandle handle = Renderer::createMaterial(importedSceneMaterialDescriptor(
                imported.materials[materialIndex],
                textures,
                materialIndex,
                "animated.material"));
            if (handle.id == UINT32_MAX) {
                result.message = "Failed to create renderer material for animated model.";
                result.model.shutdown();
                return result;
            }
            result.model.materials_.push_back(handle);
            ++result.model.diagnostics_.createdMaterialCount;
        }

        result.model.meshes_.reserve(imported.meshes.size());
        for (uint32_t meshIndex = 0; meshIndex < imported.meshes.size(); ++meshIndex) {
            const Assets::Assimp::ImportedSceneMesh& importedMesh = imported.meshes[meshIndex];
            if (importedMesh.primitives.empty()) {
                result.model.diagnostics_.warnings.push_back(
                    "Animated model mesh has no renderable primitives and was skipped: " + importedMesh.name);
                result.model.meshes_.push_back({});
                continue;
            }
            Renderer::StaticMeshDescriptor descriptor;
            descriptor.name = importedMesh.name;
            descriptor.submeshes.reserve(importedMesh.primitives.size());

            for (const Assets::Assimp::ImportedScenePrimitive& primitive : importedMesh.primitives) {
                Renderer::StaticSubmeshDescriptor submesh;
                submesh.vertices.reserve(primitive.vertices.size());
                submesh.indices = primitive.indices;
                if (primitive.materialIndex < result.model.materials_.size()) {
                    submesh.material = result.model.materials_[primitive.materialIndex];
                } else {
                    ++result.model.diagnostics_.invalidMaterialReferenceCount;
                }
                for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                    submesh.vertices.push_back(importedSceneVertexToMeshVertex(vertex));
                }
                descriptor.submeshes.push_back(std::move(submesh));
            }

            Renderer::StaticMeshHandle mesh = Renderer::createStaticMesh(descriptor);
            if (mesh.id == UINT32_MAX) {
                result.message = "Failed to create bind-pose renderer mesh for animated model.";
                result.model.shutdown();
                return result;
            }
            result.model.meshes_.push_back(mesh);
            ++result.model.diagnostics_.createdMeshCount;
        }

        if (settings.createSkinnedMeshes) {
            if (imported.joints.size() > Renderer::MaxSkinnedJointsPerMesh) {
                result.model.diagnostics_.overBudgetJointCount =
                    static_cast<uint32_t>(imported.joints.size() - Renderer::MaxSkinnedJointsPerMesh);
                result.model.diagnostics_.warnings.push_back(
                    "Animated model exceeds the renderer skinned joint metadata budget.");
            }

            result.model.skinnedMeshes_.reserve(imported.meshes.size());
            for (uint32_t meshIndex = 0; meshIndex < imported.meshes.size(); ++meshIndex) {
                const Assets::Assimp::ImportedSceneMesh& importedMesh = imported.meshes[meshIndex];
                if (importedMesh.primitives.empty()) {
                    result.model.skinnedMeshes_.push_back({});
                    continue;
                }
                Renderer::SkinnedMeshDescriptor descriptor;
                descriptor.name = importedMesh.name;
                descriptor.jointCount = static_cast<uint32_t>(imported.joints.size());
                descriptor.submeshes.reserve(importedMesh.primitives.size());
                const uint32_t skinIndex = findSkinIndexForMesh(imported, meshIndex);

                SkinnedVertexPackingStats packingStats;
                for (const Assets::Assimp::ImportedScenePrimitive& primitive : importedMesh.primitives) {
                    Renderer::SkinnedSubmeshDescriptor submesh;
                    submesh.vertices.reserve(primitive.vertices.size());
                    submesh.indices = primitive.indices;
                    submesh.skinIndex = skinIndex;
                    if (primitive.materialIndex < result.model.materials_.size()) {
                        submesh.material = result.model.materials_[primitive.materialIndex];
                    } else {
                        ++result.model.diagnostics_.invalidMaterialReferenceCount;
                    }
                    for (const Assets::Assimp::ImportedSceneVertex& vertex : primitive.vertices) {
                        submesh.vertices.push_back(convertSkinnedVertex(vertex, packingStats));
                    }
                    descriptor.submeshes.push_back(std::move(submesh));
                }

                descriptor.maxInfluencesPerVertex = packingStats.maxInfluenceCount;
                descriptor.truncatedInfluenceVertexCount = packingStats.truncatedInfluenceVertexCount;
                descriptor.zeroWeightVertexCount = packingStats.zeroWeightVertexCount;
                descriptor.normalizedWeightVertexCount = packingStats.normalizedWeightVertexCount;
                result.model.diagnostics_.truncatedInfluenceVertexCount += packingStats.truncatedInfluenceVertexCount;
                result.model.diagnostics_.zeroWeightVertexCount += packingStats.zeroWeightVertexCount;
                result.model.diagnostics_.overBudgetJointCount += packingStats.overBudgetJointCount;

                Renderer::SkinnedMeshHandle mesh = Renderer::createSkinnedMesh(descriptor);
                if (mesh.id == UINT32_MAX) {
                    result.message = "Failed to create renderer skinned mesh for animated model.";
                    result.model.shutdown();
                    return result;
                }
                result.model.skinnedMeshes_.push_back(mesh);
                ++result.model.diagnostics_.createdSkinnedMeshCount;
            }
        }

        if (settings.createBindPoseInstances) {
            for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
                const Assets::Assimp::ImportedSceneNode& node = imported.nodes[nodeIndex];
                for (uint32_t meshIndex : node.meshIndices) {
                    if (meshIndex >= result.model.meshes_.size() ||
                        result.model.meshes_[meshIndex].id == UINT32_MAX) {
                        ++result.model.diagnostics_.invalidMeshReferenceCount;
                        continue;
                    }
                    Renderer::MeshInstanceHandle instance = Renderer::createInstance(result.model.meshes_[meshIndex]);
                    if (instance.id == UINT32_MAX) {
                        result.message = "Failed to create bind-pose renderer instance for animated model.";
                        result.model.shutdown();
                        return result;
                    }
                    Renderer::setInstanceTransform(instance, node.worldTransform);
                    Renderer::setInstanceRenderLayer(instance, settings.renderLayer);
                    Renderer::setInstanceMaxDrawDistance(instance, settings.maxDrawDistance);
                    result.model.instances_.push_back({nodeIndex, meshIndex, instance, node.worldTransform});
                    ++result.model.diagnostics_.createdInstanceCount;
                }
            }
        }

        if (settings.createSkinnedInstances && settings.createSkinnedMeshes) {
            const std::vector<glm::mat4> bindPosePalette = makeBindPosePalette(imported);
            for (uint32_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
                const Assets::Assimp::ImportedSceneNode& node = imported.nodes[nodeIndex];
                for (uint32_t meshIndex : node.meshIndices) {
                    if (meshIndex >= result.model.skinnedMeshes_.size() ||
                        result.model.skinnedMeshes_[meshIndex].id == UINT32_MAX) {
                        ++result.model.diagnostics_.invalidMeshReferenceCount;
                        continue;
                    }
                    Renderer::SkinnedMeshInstanceHandle instance =
                        Renderer::createSkinnedInstance(result.model.skinnedMeshes_[meshIndex]);
                    if (instance.id == UINT32_MAX) {
                        result.message = "Failed to create skinned renderer instance for animated model.";
                        result.model.shutdown();
                        return result;
                    }
                    Renderer::setSkinnedInstanceTransform(instance, node.worldTransform);
                    Renderer::setSkinnedInstanceRenderLayer(instance, settings.renderLayer);
                    Renderer::setSkinnedInstanceMaxDrawDistance(instance, settings.maxDrawDistance);
                    Renderer::setSkinnedInstanceJointMatrices(instance, bindPosePalette);
                    result.model.skinnedInstances_.push_back({nodeIndex, meshIndex, instance, node.worldTransform});
                    ++result.model.diagnostics_.createdSkinnedInstanceCount;
                }
            }
        }

        result.model.imported_ = std::move(imported);
        result.model.loaded_ = true;
        result.success = true;
        if (result.model.diagnostics_.createdMeshCount == 0 &&
            result.model.diagnostics_.createdSkinnedMeshCount == 0) {
            result.model.diagnostics_.warnings.push_back(
                "Animated model loaded without renderable mesh resources.");
            result.message = "Animated model loaded without renderable mesh resources.";
        } else {
            result.message = "Animated model loaded.";
        }
        return result;
    }

    AnimatedModelLoadResult loadAnimatedModel(
        const std::filesystem::path& path,
        AssetCache& assetCache,
        const AnimatedModelLoadSettings& settings)
    {
        AnimatedModelCacheManifest cacheManifest;
        AnimatedModelCachePayload payload;
        AnimatedModelDiagnostics cacheDiagnostics;
        bool payloadReady = false;
        bool loadedFromCache = false;

        if (settings.cache.policy != AnimatedModelCachePolicy::Disabled) {
            cacheManifest = AnimatedModelCache::buildManifest(settings.cache, path);
            cacheDiagnostics.cachePath = AnimatedModelCache::cacheRoot(cacheManifest);
            cacheDiagnostics.cacheIdentityHash = cacheManifest.identityHash;

            if (settings.cache.policy != AnimatedModelCachePolicy::Refresh) {
                AnimatedModelCacheReadResult cacheRead;
                cacheDiagnostics.cacheReadMs = measureMilliseconds([&]() {
                    cacheRead = AnimatedModelCache::read(cacheManifest);
                });
                ++cacheDiagnostics.cacheReadCount;
                cacheDiagnostics.cacheStatus = cacheRead.status;
                cacheDiagnostics.cacheMessage = cacheRead.message;
                switch (cacheRead.status) {
                    case AnimatedModelCacheStatus::Hit:
                        if (cacheRead.payload) {
                            payload = std::move(*cacheRead.payload);
                            payloadReady = true;
                            loadedFromCache = true;
                        }
                        break;
                    case AnimatedModelCacheStatus::Stale:
                        ++cacheDiagnostics.cacheStaleCount;
                        break;
                    case AnimatedModelCacheStatus::Corrupt:
                        ++cacheDiagnostics.cacheCorruptCount;
                        break;
                    case AnimatedModelCacheStatus::Miss:
                    default:
                        ++cacheDiagnostics.cacheMissCount;
                        break;
                }
            } else {
                cacheDiagnostics.cacheStatus = AnimatedModelCacheStatus::Stale;
                cacheDiagnostics.cacheMessage = "Animated model cache refresh requested.";
                ++cacheDiagnostics.cacheStaleCount;
            }
        }

        if (!payloadReady) {
            Assets::Assimp::ImportedScene imported;
            cacheDiagnostics.sourceImportMs = measureMilliseconds([&]() {
                imported = Assets::Assimp::importScene(path);
            });
            payload.scene = std::move(imported);
            payloadReady = true;
        }

        AnimatedModelLoadResult result = createAnimatedModelFromPayload(path, std::move(payload), assetCache, settings);
        result.model.diagnostics_.cacheStatus = cacheDiagnostics.cacheStatus;
        result.model.diagnostics_.cachePath = cacheDiagnostics.cachePath;
        result.model.diagnostics_.cacheIdentityHash = cacheDiagnostics.cacheIdentityHash;
        result.model.diagnostics_.cacheMessage = cacheDiagnostics.cacheMessage;
        result.model.diagnostics_.loadedFromCache = loadedFromCache;
        result.model.diagnostics_.cacheReadCount = cacheDiagnostics.cacheReadCount;
        result.model.diagnostics_.cacheMissCount = cacheDiagnostics.cacheMissCount;
        result.model.diagnostics_.cacheStaleCount = cacheDiagnostics.cacheStaleCount;
        result.model.diagnostics_.cacheCorruptCount = cacheDiagnostics.cacheCorruptCount;
        result.model.diagnostics_.cacheReadMs = cacheDiagnostics.cacheReadMs;
        result.model.diagnostics_.sourceImportMs = cacheDiagnostics.sourceImportMs;

        if (!result.success) {
            return result;
        }

        if (settings.cache.policy == AnimatedModelCachePolicy::GenerateOnMiss ||
            settings.cache.policy == AnimatedModelCachePolicy::Refresh) {
            if (cacheManifest.identityHash.empty()) {
                cacheManifest = AnimatedModelCache::buildManifest(settings.cache, path);
                result.model.diagnostics_.cachePath = AnimatedModelCache::cacheRoot(cacheManifest);
                result.model.diagnostics_.cacheIdentityHash = cacheManifest.identityHash;
            }

            AnimatedModelCacheWriteResult cacheWrite;
            AnimatedModelCachePayload writePayload;
            writePayload.scene = result.model.imported_;
            result.model.diagnostics_.cacheWriteMs = measureMilliseconds([&]() {
                cacheWrite = AnimatedModelCache::write(cacheManifest, writePayload);
            });
            if (cacheWrite.status == AnimatedModelCacheStatus::WriteSuccess) {
                ++result.model.diagnostics_.cacheWriteCount;
                result.model.diagnostics_.cacheStatus = loadedFromCache
                    ? AnimatedModelCacheStatus::Hit
                    : AnimatedModelCacheStatus::WriteSuccess;
            } else {
                result.model.diagnostics_.cacheStatus = cacheWrite.status;
                result.model.diagnostics_.warnings.push_back("Animated model cache write failed: " + cacheWrite.message);
            }
            result.model.diagnostics_.cacheMessage = cacheWrite.message;
        } else if (loadedFromCache) {
            result.model.diagnostics_.cacheStatus = AnimatedModelCacheStatus::Hit;
        }

        return result;
    }
}
