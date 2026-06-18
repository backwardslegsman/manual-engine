#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "Assets/Assimp/Importer.hpp"

namespace Engine {
    enum class AnimatedModelCachePolicy {
        Disabled,
        ReadOnly,
        GenerateOnMiss,
        Refresh,
    };

    struct AnimatedModelCacheSettings {
        std::filesystem::path rootPath = "generated/animated_model_cache";
        uint32_t formatVersion = 1;
        std::string importerVersion = "assimp_animation_b9";
        std::string materialPipelineVersion = "gltf_materials_a7";
        std::string texturePolicyVersion = "texture_descriptors_a5";
        std::string staticVertexFormatVersion = "mesh_vertex_a4";
        std::string skinnedVertexFormatVersion = "skinned_mesh_vertex_b5";
        std::string animationPipelineVersion = "cpu_pose_b7";
        AnimatedModelCachePolicy policy = AnimatedModelCachePolicy::Disabled;
    };

    struct AnimatedModelCacheManifest {
        AnimatedModelCacheSettings settings;
        std::filesystem::path sourcePath;
        std::string sourceHash;
        Assets::Assimp::ImportedSceneSourceFormat sourceFormat = Assets::Assimp::ImportedSceneSourceFormat::Unknown;
        std::string identityHash;
    };

    enum class AnimatedModelCacheStatus {
        Hit,
        Miss,
        Stale,
        Corrupt,
        WriteSuccess,
        WriteFailed,
        Cancelled,
    };

    struct AnimatedModelCacheOperationResult {
        AnimatedModelCacheStatus status = AnimatedModelCacheStatus::Miss;
        std::filesystem::path path;
        std::string message;
    };

    struct AnimatedModelCachePayload {
        Assets::Assimp::ImportedScene scene;
    };

    struct AnimatedModelCacheReadResult : AnimatedModelCacheOperationResult {
        std::optional<AnimatedModelCachePayload> payload;
    };

    using AnimatedModelCacheWriteResult = AnimatedModelCacheOperationResult;

    class AnimatedModelCache {
    public:
        static AnimatedModelCacheManifest buildManifest(
            AnimatedModelCacheSettings settings,
            const std::filesystem::path& sourcePath);
        static std::string hashFile(const std::filesystem::path& path);
        static std::filesystem::path cacheRoot(const AnimatedModelCacheManifest& manifest);
        static AnimatedModelCacheReadResult read(const AnimatedModelCacheManifest& manifest);
        static AnimatedModelCacheWriteResult write(
            const AnimatedModelCacheManifest& manifest,
            const AnimatedModelCachePayload& payload);
    };

    const char* animatedModelCacheStatusName(AnimatedModelCacheStatus status);
}
