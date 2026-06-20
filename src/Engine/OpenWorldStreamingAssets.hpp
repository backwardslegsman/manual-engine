#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Engine/AssetCache.hpp"
#include "Engine/OpenWorldStreaming.hpp"
#include "Renderer/Texture.hpp"

namespace Engine {
    inline constexpr const char* OpenWorldStreamingAssetsVersion = "open_world_streaming_assets_s5_v1";

    struct StreamingAssetDependencyDescriptor {
        AssetId asset;
        AssetType type = AssetType::Unknown;
        std::filesystem::path sourcePath;
        AssetImportSettingsKey importSettings;
        std::optional<Renderer::TextureDescriptor> textureDescriptor;
        uint64_t estimatedBytes = 0;
        bool required = true;
        std::string debugName;
    };

    struct StreamingAssetDependencyBuildResult {
        bool success = false;
        StreamingChunkManifestRecord record;
        StreamingReadDescriptorEntry readDescriptor;
        std::string message;
    };

    struct StreamingAssetDependencyDiagnostics {
        uint32_t manifestRecordCount = 0;
        uint32_t metadataCacheHitCount = 0;
        uint32_t liveStaticMeshCount = 0;
        uint32_t liveTextureCount = 0;
        uint32_t missingRequiredCount = 0;
        uint32_t missingOptionalCount = 0;
        uint32_t unsupportedTypeCount = 0;
        uint32_t promotionFailureCount = 0;
        uint32_t sharedReferenceCount = 0;
        uint64_t releaseLatencyMicroseconds = 0;
        std::string lastMessage;
    };

    struct StreamingAssetCacheCallbacks {
        std::function<CachedStaticMesh(const std::filesystem::path&)> acquireStaticMesh;
        std::function<void(CachedStaticMesh)> releaseStaticMesh;
        std::function<CachedTexture(const std::filesystem::path&, const Renderer::TextureDescriptor&)> acquireTexture;
        std::function<void(CachedTexture)> releaseTexture;
    };

    class OpenWorldStreamingAssetResidency {
    public:
        explicit OpenWorldStreamingAssetResidency(StreamingAssetCacheCallbacks callbacks);

        bool registerDependency(const StreamingAssetDependencyDescriptor& descriptor);
        [[nodiscard]] StreamingPromotionCallbacks promotionCallbacks();
        [[nodiscard]] const StreamingAssetDependencyDiagnostics& diagnostics() const;
        void clear();

    private:
        struct LiveRecord {
            uint64_t token = 0;
            StreamingChunkKey key;
            AssetType type = AssetType::Unknown;
            std::filesystem::path sourcePath;
            Renderer::TextureDescriptor textureDescriptor;
            CachedStaticMesh mesh;
            CachedTexture texture;
            uint32_t refCount = 0;
            bool optional = false;
        };

        [[nodiscard]] StreamingPromotionResult promote(
            const StreamingPromotionRequest& request,
            const StreamingCachedPayload& payload);
        [[nodiscard]] StreamingPromotionResult demote(
            const StreamingDemotionRequest& request,
            StreamingRuntimeToken token);
        [[nodiscard]] const StreamingAssetDependencyDescriptor* findDescriptor(const StreamingChunkKey& key) const;
        [[nodiscard]] LiveRecord* findByKey(const StreamingChunkKey& key);
        [[nodiscard]] LiveRecord* findByToken(StreamingRuntimeToken token);

        StreamingAssetCacheCallbacks callbacks_;
        std::vector<StreamingAssetDependencyDescriptor> descriptors_;
        std::vector<LiveRecord> live_;
        uint64_t nextToken_ = 1;
        StreamingAssetDependencyDiagnostics diagnostics_;
    };

    [[nodiscard]] StreamingAssetDependencyBuildResult makeAssetDependencyStreamingRecord(
        const StreamingAssetDependencyDescriptor& descriptor,
        const StreamingWorldBounds& bounds = {});
    [[nodiscard]] std::optional<StreamingAssetDependencyDescriptor> assetDependencyDescriptorFromRegistry(
        const AssetRegistry& registry,
        AssetId asset,
        bool required = true,
        std::optional<Renderer::TextureDescriptor> textureDescriptor = std::nullopt,
        uint64_t estimatedBytes = 0,
        std::string debugName = {});
    void addAssetDependencyStreamingRecord(
        StreamingChunkManifest& manifest,
        StreamingReadDescriptorTable& descriptors,
        const StreamingAssetDependencyBuildResult& dependency);
    void mergeAssetDependencyDiagnostics(
        OpenWorldStreamingDiagnostics& streaming,
        const StreamingAssetDependencyDiagnostics& assets);
    [[nodiscard]] StreamingAssetCacheCallbacks assetCacheStreamingCallbacks(AssetCache& cache);
    [[nodiscard]] StreamingPromotionCallbacks makeAssetStreamingPromotionCallbacks(
        OpenWorldStreamingAssetResidency& residency);
}
