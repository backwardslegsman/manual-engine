#pragma once

#include <cstdint>
#include <vector>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AnimationPose.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct AnimatedSkinnedVertexPackingStats {
        uint32_t maxInfluenceCount = 0;
        uint32_t truncatedInfluenceVertexCount = 0;
        uint32_t zeroWeightVertexCount = 0;
        uint32_t normalizedWeightVertexCount = 0;
        uint32_t overBudgetJointCount = 0;
    };

    [[nodiscard]] Renderer::SkinnedMeshVertex animatedImportedSceneVertexToSkinnedMeshVertex(
        const Assets::Assimp::ImportedSceneVertex& vertex,
        AnimatedSkinnedVertexPackingStats& stats);

    [[nodiscard]] uint32_t animatedSkinIndexForMesh(
        const Assets::Assimp::ImportedScene& imported,
        uint32_t meshIndex);

    [[nodiscard]] std::vector<glm::mat4> animatedBindPosePalette(
        const Assets::Assimp::ImportedScene& imported);

    [[nodiscard]] std::vector<glm::mat4> animatedPosePalette(
        const AnimatedSkeletonPose& pose);

    [[nodiscard]] AnimatedSkeletonPose sampleImportedSceneBindPose(
        const Assets::Assimp::ImportedScene& imported,
        bool loaded);

    [[nodiscard]] AnimatedSkeletonPose sampleImportedSceneClip(
        const Assets::Assimp::ImportedScene& imported,
        bool loaded,
        uint32_t clipIndex,
        float timeSeconds,
        const AnimationSampleSettings& settings = {});

    void advanceImportedScenePlayback(
        const Assets::Assimp::ImportedScene& imported,
        AnimationPlaybackState& state,
        float deltaSeconds);
}
