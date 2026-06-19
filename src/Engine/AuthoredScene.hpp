#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/FrameBudget.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct AuthoredSceneLoadResult;

    struct AuthoredSceneBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        bool valid = false;
    };

    struct AuthoredSceneLoadSettings {
        bool loadTextures = true;
        Renderer::RenderLayer renderLayer = Renderer::RenderLayer::Props;
        float maxDrawDistance = 0.0f;
    };

    struct AuthoredSceneSectorId {
        uint32_t value = UINT32_MAX;
    };

    inline bool operator==(AuthoredSceneSectorId lhs, AuthoredSceneSectorId rhs)
    {
        return lhs.value == rhs.value;
    }

    struct AuthoredSceneSectorManifest {
        AuthoredSceneSectorId id;
        std::string name;
        AuthoredSceneBounds bounds;
        std::vector<uint32_t> nodeIndices;
        std::vector<uint32_t> meshIndices;
        std::vector<uint32_t> materialIndices;
        std::vector<uint32_t> textureIndices;
        std::vector<uint32_t> lightIndices;
        uint32_t primitiveCount = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    struct AuthoredScenePartitionSettings {
        float sectorSize = 25.0f;
        bool enabled = true;
    };

    struct AuthoredScenePartition {
        AuthoredSceneBounds bounds;
        std::vector<AuthoredSceneSectorManifest> sectors;
        bool usedFallbackRootSector = false;
        std::vector<std::string> warnings;
    };

    enum class AuthoredSceneCachePolicy {
        Disabled,
        ReadOnly,
        GenerateOnMiss,
        Refresh,
    };

    struct AuthoredSceneCacheSettings {
        std::filesystem::path rootPath = "generated/authored_scene_cache";
        uint32_t formatVersion = 2;
        std::string importerVersion = "assimp_authored_scene_format_aware_a13";
        std::string materialPipelineVersion = "gltf_materials_a7";
        std::string texturePolicyVersion = "texture_descriptors_a5";
        std::string vertexFormatVersion = "mesh_vertex_a4";
        std::string partitionVersion = "xz_grid_a10";
        AuthoredSceneCachePolicy policy = AuthoredSceneCachePolicy::Disabled;
    };

    struct AuthoredSceneCacheManifest {
        AuthoredSceneCacheSettings settings;
        AuthoredScenePartitionSettings partition;
        std::filesystem::path sourcePath;
        std::string sourceHash;
        std::string identityHash;
    };

    enum class AuthoredSceneCacheStatus {
        Hit,
        Miss,
        Stale,
        Corrupt,
        WriteSuccess,
        WriteFailed,
        Cancelled,
    };

    struct AuthoredSceneCacheStats {
        uint32_t hits = 0;
        uint32_t misses = 0;
        uint32_t stale = 0;
        uint32_t corrupt = 0;
        uint32_t writes = 0;
        std::filesystem::path lastPath;
        std::string lastMessage;
    };

    struct AuthoredSceneCacheOperationResult {
        AuthoredSceneCacheStatus status = AuthoredSceneCacheStatus::Miss;
        std::filesystem::path path;
        std::string message;
    };

    struct AuthoredSceneCachePayload {
        Assets::Assimp::ImportedScene scene;
        AuthoredScenePartition partition;
    };

    struct AuthoredSceneCacheReadResult : AuthoredSceneCacheOperationResult {
        std::optional<AuthoredSceneCachePayload> payload;
    };

    using AuthoredSceneCacheWriteResult = AuthoredSceneCacheOperationResult;

    class AuthoredSceneCache {
    public:
        explicit AuthoredSceneCache(AuthoredSceneCacheManifest manifest);

        static AuthoredSceneCacheManifest buildManifest(
            AuthoredSceneCacheSettings settings,
            const std::filesystem::path& sourcePath,
            const AuthoredScenePartitionSettings& partition);
        static std::string hashFile(const std::filesystem::path& path);
        static std::filesystem::path cacheRoot(const AuthoredSceneCacheManifest& manifest);
        static AuthoredSceneCacheReadResult read(const AuthoredSceneCacheManifest& manifest);
        static AuthoredSceneCacheWriteResult write(
            const AuthoredSceneCacheManifest& manifest,
            const AuthoredSceneCachePayload& payload);

        const AuthoredSceneCacheManifest& manifest() const;
        const AuthoredSceneCacheStats& stats() const;
        void clearStats();
        AuthoredSceneCacheReadResult read();
        AuthoredSceneCacheWriteResult write(const AuthoredSceneCachePayload& payload);
        void recordReadResult(const AuthoredSceneCacheOperationResult& result);
        void recordWriteResult(const AuthoredSceneCacheOperationResult& result);

    private:
        AuthoredSceneCacheManifest manifest_;
        AuthoredSceneCacheStats stats_;
    };

    struct AuthoredSceneStreamingSettings {
        AuthoredSceneLoadSettings load;
        AuthoredScenePartitionSettings partition;
        AuthoredSceneCacheSettings cache;
        glm::vec3 initialCameraPosition{0.0f};
        float loadRadius = 45.0f;
        float unloadRadius = 70.0f;
        uint32_t maxLoadedSectorCount = 0;
        uint32_t maxSectorLoadCommitsPerFrame = 1;
        uint32_t maxSectorUnloadCommitsPerFrame = 2;
        float maxMainThreadStreamingWorkMs = 2.0f;
        bool loadInitialSectorsImmediately = true;
    };

    struct AuthoredSceneDiagnostics {
        Assets::Assimp::ImportedSceneSourceFormat sourceFormat = Assets::Assimp::ImportedSceneSourceFormat::Unknown;
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedPrimitiveCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t importedTextureCount = 0;
        uint32_t importedLightCount = 0;
        uint32_t importedSkinCount = 0;
        uint32_t importedJointCount = 0;
        uint32_t importedAnimationCount = 0;
        uint32_t importedAnimationChannelCount = 0;
        uint32_t createdMeshCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t createdInstanceCount = 0;
        uint32_t createdLightCount = 0;
        uint32_t disabledZeroIntensityLightCount = 0;
        uint32_t skippedUnsupportedLightCount = 0;
        uint32_t skippedOverBudgetLightCount = 0;
        uint32_t missingLightTransformCount = 0;
        uint32_t activeAuthoredLightCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t fallbackTextureCount = 0;
        uint64_t textureEstimatedBytes = 0;
        uint32_t textureSrgbFallbackCount = 0;
        uint32_t fallbackMaterialCount = 0;
        uint32_t invalidMeshReferenceCount = 0;
        uint32_t invalidMaterialReferenceCount = 0;
        uint32_t mappedPackedMetallicRoughnessCount = 0;
        uint32_t deferredAlphaMaterialCount = 0;
        uint32_t deferredDoubleSidedMaterialCount = 0;
        uint32_t deferredOcclusionTextureCount = 0;
        uint32_t deferredEmissiveTextureCount = 0;
        uint32_t retainedTexcoord1PrimitiveCount = 0;
        uint32_t retainedVertexColorPrimitiveCount = 0;
        uint32_t totalSectorCount = 0;
        uint32_t loadedSectorCount = 0;
        uint32_t pendingLoadSectorCount = 0;
        uint32_t pendingUnloadSectorCount = 0;
        uint32_t failedSectorCount = 0;
        uint32_t sharedMaterialReferenceCount = 0;
        uint32_t sharedTextureReferenceCount = 0;
        uint64_t sectorEstimatedBytes = 0;
        std::string lastStreamingWarning;
        AuthoredSceneCacheStatus cacheStatus = AuthoredSceneCacheStatus::Miss;
        std::filesystem::path cachePath;
        std::string cacheIdentityHash;
        uint32_t cacheReadCount = 0;
        uint32_t cacheWriteCount = 0;
        uint32_t cacheMissCount = 0;
        uint32_t cacheStaleCount = 0;
        uint32_t cacheCorruptCount = 0;
        bool loadedFromCache = false;
        std::string cacheMessage;
        std::string asyncPhase;
        uint32_t asyncJobsQueued = 0;
        uint32_t asyncJobsCompleted = 0;
        uint32_t asyncJobsFailed = 0;
        uint32_t asyncPendingJobs = 0;
        float asyncCacheReadMs = 0.0f;
        float asyncImportMs = 0.0f;
        float asyncCacheWriteMs = 0.0f;
        std::string asyncMessage;
        bool boundsValid = false;
        std::vector<std::string> warnings;
    };

    struct AuthoredSceneDiagnosticsSummary {
        Assets::Assimp::ImportedSceneSourceFormat sourceFormat = Assets::Assimp::ImportedSceneSourceFormat::Unknown;
        std::string sourceFormatName;
        uint32_t importedNodeCount = 0;
        uint32_t importedMeshCount = 0;
        uint32_t importedPrimitiveCount = 0;
        uint32_t importedMaterialCount = 0;
        uint32_t importedTextureCount = 0;
        uint32_t importedLightCount = 0;
        uint32_t importedSkinCount = 0;
        uint32_t importedJointCount = 0;
        uint32_t importedAnimationCount = 0;
        uint32_t importedAnimationChannelCount = 0;
        uint32_t createdMeshCount = 0;
        uint32_t createdMaterialCount = 0;
        uint32_t createdInstanceCount = 0;
        uint32_t createdLightCount = 0;
        uint32_t textureLoadSuccessCount = 0;
        uint32_t textureLoadFailureCount = 0;
        uint32_t fallbackTextureCount = 0;
        uint64_t textureEstimatedBytes = 0;
        uint32_t disabledZeroIntensityLightCount = 0;
        uint32_t skippedOverBudgetLightCount = 0;
        uint32_t activeAuthoredLightCount = 0;
        uint32_t totalSectorCount = 0;
        uint32_t loadedSectorCount = 0;
        uint32_t pendingLoadSectorCount = 0;
        uint32_t pendingUnloadSectorCount = 0;
        uint32_t failedSectorCount = 0;
        uint64_t sectorEstimatedBytes = 0;
        AuthoredSceneCacheStatus cacheStatus = AuthoredSceneCacheStatus::Miss;
        bool loadedFromCache = false;
        uint32_t cacheReadCount = 0;
        uint32_t cacheWriteCount = 0;
        uint32_t cacheMissCount = 0;
        uint32_t cacheStaleCount = 0;
        uint32_t cacheCorruptCount = 0;
        std::string cacheIdentityHash;
        std::filesystem::path cachePath;
        std::string cacheMessage;
        std::string asyncPhase;
        uint32_t asyncJobsQueued = 0;
        uint32_t asyncJobsCompleted = 0;
        uint32_t asyncJobsFailed = 0;
        uint32_t asyncPendingJobs = 0;
        float asyncCacheReadMs = 0.0f;
        float asyncImportMs = 0.0f;
        float asyncCacheWriteMs = 0.0f;
        std::string asyncMessage;
        bool boundsValid = false;
        uint32_t warningCount = 0;
        std::string lastWarning;
    };

    const char* cacheStatusName(AuthoredSceneCacheStatus status);
    AuthoredSceneDiagnosticsSummary summarizeAuthoredSceneDiagnostics(const AuthoredSceneDiagnostics& diagnostics);
    std::string authoredSceneDiagnosticsSummaryText(const AuthoredSceneDiagnostics& diagnostics);
    std::string authoredSceneDiagnosticsSummaryYaml(const AuthoredSceneDiagnostics& diagnostics);

    struct AuthoredSceneInstance {
        uint32_t nodeIndex = UINT32_MAX;
        uint32_t meshIndex = UINT32_MAX;
        Renderer::MeshInstanceHandle handle;
        glm::mat4 transform{1.0f};
    };

    class AuthoredScene {
    public:
        AuthoredScene() = default;
        ~AuthoredScene();

        AuthoredScene(const AuthoredScene&) = delete;
        AuthoredScene& operator=(const AuthoredScene&) = delete;
        AuthoredScene(AuthoredScene&& other) noexcept;
        AuthoredScene& operator=(AuthoredScene&& other) noexcept;

        void shutdown();
        bool loaded() const;
        const AuthoredSceneBounds& bounds() const;
        const AuthoredSceneDiagnostics& diagnostics() const;
        const std::vector<AuthoredSceneInstance>& instances() const;

    private:
        friend struct AuthoredSceneLoadResult;
        friend AuthoredSceneLoadResult loadAuthoredScene(
            const std::filesystem::path& path,
            AssetCache& assetCache,
            const AuthoredSceneLoadSettings& settings);

        void moveFrom(AuthoredScene&& other) noexcept;

        AssetCache* assetCache_ = nullptr;
        bool loaded_ = false;
        AuthoredSceneBounds bounds_;
        AuthoredSceneDiagnostics diagnostics_;
        std::vector<Renderer::StaticMeshHandle> meshes_;
        std::vector<Renderer::MaterialHandle> materials_;
        std::vector<Renderer::LightHandle> lights_;
        std::vector<AuthoredSceneInstance> instances_;
        std::vector<CachedTexture> textures_;
    };

    struct AuthoredSceneLoadResult {
        bool success = false;
        std::string message;
        AuthoredScene scene;
    };

    class PartitionedAuthoredScene {
    public:
        PartitionedAuthoredScene() = default;
        ~PartitionedAuthoredScene();

        PartitionedAuthoredScene(const PartitionedAuthoredScene&) = delete;
        PartitionedAuthoredScene& operator=(const PartitionedAuthoredScene&) = delete;
        PartitionedAuthoredScene(PartitionedAuthoredScene&& other) noexcept;
        PartitionedAuthoredScene& operator=(PartitionedAuthoredScene&& other) noexcept;

        void updateStreaming(const glm::vec3& cameraPosition, MainThreadWorkQueue& mainThreadWork);
        void shutdown();
        bool loaded() const;
        const AuthoredSceneBounds& bounds() const;
        const AuthoredSceneDiagnostics& diagnostics() const;
        const AuthoredScenePartition& partition() const;
        void setAsyncDiagnostics(
            std::string phase,
            uint32_t queued,
            uint32_t completed,
            uint32_t failed,
            uint32_t pending,
            float cacheReadMs,
            float importMs,
            float cacheWriteMs,
            std::string message);

    private:
        friend struct PartitionedAuthoredSceneLoadResult;
        friend PartitionedAuthoredSceneLoadResult loadPartitionedAuthoredScene(
            const std::filesystem::path& path,
            AssetCache& assetCache,
            const AuthoredSceneStreamingSettings& settings);
        friend PartitionedAuthoredSceneLoadResult createPartitionedAuthoredSceneFromPayload(
            const std::filesystem::path& path,
            AuthoredSceneCachePayload payload,
            AssetCache& assetCache,
            const AuthoredSceneStreamingSettings& settings);

        enum class SectorState {
            Unloaded,
            PendingLoad,
            Loaded,
            PendingUnload,
            Failed,
        };

        struct MaterialRecord {
            Renderer::MaterialHandle handle;
            uint32_t refCount = 0;
            std::vector<CachedTexture> textures;
        };

        struct SectorRuntime {
            SectorState state = SectorState::Unloaded;
            Renderer::RenderGroupHandle renderGroup;
            std::vector<Renderer::StaticMeshHandle> meshes;
            std::vector<Renderer::MeshInstanceHandle> instances;
            std::vector<Renderer::LightHandle> lights;
            std::vector<uint32_t> materialRefs;
            std::vector<uint32_t> textureRefs;
        };

        void moveFrom(PartitionedAuthoredScene&& other) noexcept;
        void enqueueSectorLoad(AuthoredSceneSectorId sector, MainThreadWorkQueue& mainThreadWork);
        void enqueueSectorUnload(AuthoredSceneSectorId sector, MainThreadWorkQueue& mainThreadWork);
        void commitSectorLoad(AuthoredSceneSectorId sector);
        void commitSectorUnload(AuthoredSceneSectorId sector);
        void refreshDiagnostics();
        void setStreamingWarning(std::string warning);

        AssetCache* assetCache_ = nullptr;
        AuthoredSceneStreamingSettings settings_;
        bool loaded_ = false;
        std::filesystem::path sourcePath_;
        AuthoredSceneBounds bounds_;
        AuthoredSceneDiagnostics diagnostics_;
        AuthoredScenePartition partition_;
        std::vector<SectorRuntime> sectorRuntime_;
        std::vector<MaterialRecord> materials_;
        std::unordered_set<uint32_t> queuedLoadSectors_;
        std::unordered_set<uint32_t> queuedUnloadSectors_;
        struct ImportedStorage;
        std::shared_ptr<ImportedStorage> imported_;
    };

    struct PartitionedAuthoredSceneLoadResult {
        bool success = false;
        std::string message;
        PartitionedAuthoredScene scene;
    };

    AuthoredSceneLoadResult loadAuthoredScene(
        const std::filesystem::path& path,
        AssetCache& assetCache,
        const AuthoredSceneLoadSettings& settings = {});

    AuthoredScenePartition partitionAuthoredScene(
        const std::filesystem::path& path,
        const AuthoredScenePartitionSettings& settings = {});

    AuthoredScenePartition partitionImportedAuthoredScene(
        const Assets::Assimp::ImportedScene& imported,
        const AuthoredScenePartitionSettings& settings = {});

    PartitionedAuthoredSceneLoadResult loadPartitionedAuthoredScene(
        const std::filesystem::path& path,
        AssetCache& assetCache,
        const AuthoredSceneStreamingSettings& settings = {});

    PartitionedAuthoredSceneLoadResult createPartitionedAuthoredSceneFromPayload(
        const std::filesystem::path& path,
        AuthoredSceneCachePayload payload,
        AssetCache& assetCache,
        const AuthoredSceneStreamingSettings& settings = {});
}
