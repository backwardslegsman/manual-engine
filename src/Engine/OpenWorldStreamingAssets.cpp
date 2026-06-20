#include "Engine/OpenWorldStreamingAssets.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace Engine {
    namespace {
        constexpr uint64_t HashOffset = 1469598103934665603ull;
        constexpr uint64_t HashPrime = 1099511628211ull;

        void hashByte(uint64_t& hash, uint8_t byte)
        {
            hash ^= static_cast<uint64_t>(byte);
            hash *= HashPrime;
        }

        void hashUint64(uint64_t& hash, uint64_t value)
        {
            for (uint32_t shift = 0; shift < 64; shift += 8) {
                hashByte(hash, static_cast<uint8_t>((value >> shift) & 0xffu));
            }
        }

        void hashString(uint64_t& hash, const std::string& value)
        {
            for (const unsigned char byte : value) {
                hashByte(hash, byte);
            }
            hashByte(hash, 0xffu);
        }

        uint64_t hashImportSettings(const AssetImportSettingsKey& settings)
        {
            uint64_t hash = HashOffset;
            hashString(hash, settings.pipeline);
            hashString(hash, settings.version);
            hashString(hash, settings.optionsHash);
            return hash;
        }

        uint64_t hashDescriptorSource(const StreamingAssetDependencyDescriptor& descriptor)
        {
            uint64_t hash = HashOffset;
            hashUint64(hash, descriptor.asset.value);
            hashUint64(hash, static_cast<uint64_t>(descriptor.type));
            hashString(hash, descriptor.sourcePath.lexically_normal().generic_string());
            hashUint64(hash, hashImportSettings(descriptor.importSettings));
            if (descriptor.textureDescriptor) {
                const Renderer::TextureDescriptor& texture = *descriptor.textureDescriptor;
                hashUint64(hash, static_cast<uint64_t>(texture.slot));
                hashUint64(hash, static_cast<uint64_t>(texture.colorSpace));
                hashUint64(hash, static_cast<uint64_t>(texture.wrapU));
                hashUint64(hash, static_cast<uint64_t>(texture.wrapV));
                hashUint64(hash, static_cast<uint64_t>(texture.minFilter));
                hashUint64(hash, static_cast<uint64_t>(texture.magFilter));
                hashUint64(hash, static_cast<uint64_t>(texture.mipFilter));
                hashUint64(hash, texture.generateMips ? 1ull : 0ull);
                hashString(hash, texture.debugName);
            }
            return hash;
        }

        StreamingWorldBounds defaultAssetBounds()
        {
            return {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        }

        bool validStaticMesh(CachedStaticMesh mesh)
        {
            return mesh.id != UINT32_MAX && mesh.handle.id != UINT32_MAX;
        }

        bool validTexture(CachedTexture texture)
        {
            return texture.id != UINT32_MAX && Renderer::isValid(texture.handle);
        }

        StreamingMetadataPayload metadataPayloadFor(const StreamingAssetDependencyDescriptor& descriptor)
        {
            std::ostringstream label;
            label << "asset:" << descriptor.asset.value << ":";
            label << descriptor.sourcePath.generic_string();
            return {label.str()};
        }

        std::string importSettingsSuffix(const AssetImportSettingsKey& settings)
        {
            std::ostringstream stream;
            stream << settings.pipeline << ":" << settings.version << ":" << settings.optionsHash;
            return stream.str();
        }

        StreamingChunkKey assetDependencyKey(const StreamingAssetDependencyDescriptor& descriptor)
        {
            StreamingChunkKey key;
            key.kind = StreamingChunkKeyKind::AssetDependency;
            key.asset = descriptor.asset;
            key.stableId = importSettingsSuffix(descriptor.importSettings);
            return key;
        }
    }

    OpenWorldStreamingAssetResidency::OpenWorldStreamingAssetResidency(
        StreamingAssetCacheCallbacks callbacks)
        : callbacks_(std::move(callbacks))
    {
    }

    bool OpenWorldStreamingAssetResidency::registerDependency(
        const StreamingAssetDependencyDescriptor& descriptor)
    {
        if (!isValid(descriptor.asset)) {
            diagnostics_.lastMessage = "Cannot register asset dependency without valid AssetId.";
            return false;
        }

        const StreamingChunkKey key = assetDependencyKey(descriptor);
        for (StreamingAssetDependencyDescriptor& existing : descriptors_) {
            if (assetDependencyKey(existing) == key) {
                existing = descriptor;
                return true;
            }
        }
        descriptors_.push_back(descriptor);
        diagnostics_.manifestRecordCount = static_cast<uint32_t>(descriptors_.size());
        return true;
    }

    StreamingPromotionCallbacks OpenWorldStreamingAssetResidency::promotionCallbacks()
    {
        StreamingPromotionCallbacks callbacks;
        callbacks.promote = [this](
            const StreamingPromotionRequest& request,
            const StreamingCachedPayload& payload) {
            return promote(request, payload);
        };
        callbacks.demote = [this](
            const StreamingDemotionRequest& request,
            StreamingRuntimeToken token) {
            return demote(request, token);
        };
        return callbacks;
    }

    const StreamingAssetDependencyDiagnostics& OpenWorldStreamingAssetResidency::diagnostics() const
    {
        return diagnostics_;
    }

    void OpenWorldStreamingAssetResidency::clear()
    {
        for (LiveRecord& record : live_) {
            if (validStaticMesh(record.mesh) && callbacks_.releaseStaticMesh) {
                callbacks_.releaseStaticMesh(record.mesh);
            }
            if (validTexture(record.texture) && callbacks_.releaseTexture) {
                callbacks_.releaseTexture(record.texture);
            }
        }
        live_.clear();
        descriptors_.clear();
        diagnostics_ = {};
    }

    StreamingPromotionResult OpenWorldStreamingAssetResidency::promote(
        const StreamingPromotionRequest& request,
        const StreamingCachedPayload& payload)
    {
        StreamingPromotionResult result;
        result.status = StreamingPromotionStatus::UnsupportedPayload;
        result.message = "Unsupported asset dependency payload.";

        if (request.payload != StreamingPayloadKind::AssetDependency ||
            request.key.kind != StreamingChunkKeyKind::AssetDependency) {
            ++diagnostics_.promotionFailureCount;
            diagnostics_.lastMessage = result.message;
            return result;
        }

        const StreamingMetadataPayload* metadata = std::get_if<StreamingMetadataPayload>(&payload);
        if (!metadata) {
            ++diagnostics_.promotionFailureCount;
            result.message = "Asset dependency promotion requires cached metadata payload.";
            diagnostics_.lastMessage = result.message;
            return result;
        }
        ++diagnostics_.metadataCacheHitCount;

        if (LiveRecord* existing = findByKey(request.key)) {
            ++existing->refCount;
            ++diagnostics_.sharedReferenceCount;
            result.status = StreamingPromotionStatus::Success;
            result.liveToken = {existing->token};
            result.liveResources.meshHandles = diagnostics_.liveStaticMeshCount;
            result.liveResources.textureHandles = diagnostics_.liveTextureCount;
            result.liveResources.assetDependencies = static_cast<uint32_t>(live_.size());
            result.message = "Reused live asset dependency.";
            return result;
        }

        const StreamingAssetDependencyDescriptor* descriptor = findDescriptor(request.key);
        if (!descriptor) {
            result.message = "Asset dependency descriptor was not registered with residency adapter.";
            diagnostics_.lastMessage = result.message;
            ++diagnostics_.promotionFailureCount;
            return result;
        }

        LiveRecord record;
        record.token = nextToken_++;
        record.key = request.key;
        record.type = descriptor->type;
        record.sourcePath = descriptor->sourcePath;
        record.textureDescriptor = descriptor->textureDescriptor.value_or(Renderer::TextureDescriptor{});
        record.refCount = 1;
        record.optional = !descriptor->required;

        switch (descriptor->type) {
            case AssetType::StaticMesh:
                if (!callbacks_.acquireStaticMesh) {
                    result.message = "Static mesh asset streaming has no acquire callback.";
                    ++diagnostics_.promotionFailureCount;
                    diagnostics_.lastMessage = result.message;
                    return result;
                }
                record.mesh = callbacks_.acquireStaticMesh(descriptor->sourcePath);
                if (!validStaticMesh(record.mesh)) {
                    if (descriptor->required) {
                        ++diagnostics_.missingRequiredCount;
                        ++diagnostics_.promotionFailureCount;
                        result.status = StreamingPromotionStatus::CallbackFailed;
                        result.message = "Required static mesh asset failed to load.";
                        diagnostics_.lastMessage = result.message;
                        return result;
                    }
                    ++diagnostics_.missingOptionalCount;
                } else {
                    ++diagnostics_.liveStaticMeshCount;
                }
                break;
            case AssetType::Texture:
                if (!callbacks_.acquireTexture) {
                    result.message = "Texture asset streaming has no acquire callback.";
                    ++diagnostics_.promotionFailureCount;
                    diagnostics_.lastMessage = result.message;
                    return result;
                }
                record.texture = callbacks_.acquireTexture(
                    descriptor->sourcePath,
                    descriptor->textureDescriptor.value_or(Renderer::TextureDescriptor{}));
                if (!validTexture(record.texture)) {
                    if (descriptor->required) {
                        ++diagnostics_.missingRequiredCount;
                        ++diagnostics_.promotionFailureCount;
                        result.status = StreamingPromotionStatus::CallbackFailed;
                        result.message = "Required texture asset failed to load.";
                        diagnostics_.lastMessage = result.message;
                        return result;
                    }
                    ++diagnostics_.missingOptionalCount;
                } else {
                    ++diagnostics_.liveTextureCount;
                }
                break;
            default:
                ++diagnostics_.unsupportedTypeCount;
                ++diagnostics_.promotionFailureCount;
                result.message = "Asset type is not supported for live streaming promotion.";
                diagnostics_.lastMessage = result.message;
                return result;
        }

        live_.push_back(record);
        result.status = StreamingPromotionStatus::Success;
        result.liveToken = {record.token};
        result.liveResources.meshHandles = diagnostics_.liveStaticMeshCount;
        result.liveResources.textureHandles = diagnostics_.liveTextureCount;
        result.liveResources.assetDependencies = static_cast<uint32_t>(live_.size());
        result.message = "Promoted asset dependency.";
        (void)metadata;
        return result;
    }

    StreamingPromotionResult OpenWorldStreamingAssetResidency::demote(
        const StreamingDemotionRequest&,
        StreamingRuntimeToken token)
    {
        StreamingPromotionResult result;
        result.status = StreamingPromotionStatus::Stale;
        result.message = "Unknown asset dependency runtime token.";

        LiveRecord* record = findByToken(token);
        if (!record) {
            return result;
        }

        if (record->refCount > 1) {
            --record->refCount;
            result.status = StreamingPromotionStatus::Success;
            result.liveToken = token;
            result.message = "Released one shared asset dependency reference.";
            return result;
        }

        const auto start = std::chrono::steady_clock::now();
        if (validStaticMesh(record->mesh) && callbacks_.releaseStaticMesh) {
            callbacks_.releaseStaticMesh(record->mesh);
            if (diagnostics_.liveStaticMeshCount > 0) {
                --diagnostics_.liveStaticMeshCount;
            }
        }
        if (validTexture(record->texture) && callbacks_.releaseTexture) {
            callbacks_.releaseTexture(record->texture);
            if (diagnostics_.liveTextureCount > 0) {
                --diagnostics_.liveTextureCount;
            }
        }
        const auto end = std::chrono::steady_clock::now();
        diagnostics_.releaseLatencyMicroseconds +=
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());

        live_.erase(std::remove_if(
                        live_.begin(),
                        live_.end(),
                        [&](const LiveRecord& candidate) { return candidate.token == token.value; }),
            live_.end());

        result.status = StreamingPromotionStatus::Success;
        result.message = "Released asset dependency.";
        result.liveResources.assetDependencies = static_cast<uint32_t>(live_.size());
        result.liveResources.meshHandles = diagnostics_.liveStaticMeshCount;
        result.liveResources.textureHandles = diagnostics_.liveTextureCount;
        return result;
    }

    OpenWorldStreamingAssetResidency::LiveRecord* OpenWorldStreamingAssetResidency::findByKey(
        const StreamingChunkKey& key)
    {
        for (LiveRecord& record : live_) {
            if (record.key == key) {
                return &record;
            }
        }
        return nullptr;
    }

    const StreamingAssetDependencyDescriptor* OpenWorldStreamingAssetResidency::findDescriptor(
        const StreamingChunkKey& key) const
    {
        for (const StreamingAssetDependencyDescriptor& descriptor : descriptors_) {
            if (assetDependencyKey(descriptor) == key) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    OpenWorldStreamingAssetResidency::LiveRecord* OpenWorldStreamingAssetResidency::findByToken(
        StreamingRuntimeToken token)
    {
        for (LiveRecord& record : live_) {
            if (record.token == token.value) {
                return &record;
            }
        }
        return nullptr;
    }

    StreamingAssetDependencyBuildResult makeAssetDependencyStreamingRecord(
        const StreamingAssetDependencyDescriptor& descriptor,
        const StreamingWorldBounds& bounds)
    {
        StreamingAssetDependencyBuildResult result;
        if (!isValid(descriptor.asset)) {
            result.message = "Asset dependency requires a valid AssetId.";
            return result;
        }

        const StreamingWorldBounds recordBounds = isValid(bounds) ? bounds : defaultAssetBounds();
        result.record = makeAssetStreamingManifestRecord(
            descriptor.asset,
            recordBounds,
            hashDescriptorSource(descriptor),
            hashImportSettings(descriptor.importSettings),
            descriptor.estimatedBytes,
            descriptor.debugName.empty() ? descriptor.sourcePath.filename().generic_string() : descriptor.debugName);
        result.record.key = assetDependencyKey(descriptor);
        result.record.dirtyFlags = StreamingDirtyFlags::None;

        const uint64_t manifestHash = hashStreamingManifestRecord(result.record);
        result.readDescriptor = {
            result.record.key,
            StreamingPayloadKind::AssetDependency,
            manifestHash,
            fakeStreamingReadDescriptor(
                StreamingReadStatus::Hit,
                StreamingCachedPayload{metadataPayloadFor(descriptor)},
                descriptor.estimatedBytes,
                "Asset dependency metadata cached.")};
        result.success = true;
        result.message = "Built asset dependency streaming record.";
        return result;
    }

    std::optional<StreamingAssetDependencyDescriptor> assetDependencyDescriptorFromRegistry(
        const AssetRegistry& registry,
        AssetId asset,
        bool required,
        std::optional<Renderer::TextureDescriptor> textureDescriptor,
        uint64_t estimatedBytes,
        std::string debugName)
    {
        const std::optional<AssetMetadata> metadata = registry.metadata(asset);
        if (!metadata) {
            return std::nullopt;
        }

        StreamingAssetDependencyDescriptor descriptor;
        descriptor.asset = metadata->id;
        descriptor.type = metadata->type;
        descriptor.sourcePath = metadata->canonicalPath.empty() ? metadata->sourcePath : metadata->canonicalPath;
        descriptor.importSettings = metadata->settings;
        descriptor.textureDescriptor = std::move(textureDescriptor);
        descriptor.estimatedBytes = estimatedBytes;
        descriptor.required = required;
        descriptor.debugName = debugName.empty() ? metadata->sourcePath.filename().generic_string() : std::move(debugName);
        return descriptor;
    }

    void addAssetDependencyStreamingRecord(
        StreamingChunkManifest& manifest,
        StreamingReadDescriptorTable& descriptors,
        const StreamingAssetDependencyBuildResult& dependency)
    {
        if (!dependency.success) {
            return;
        }
        manifest.records.push_back(dependency.record);
        setStreamingReadDescriptor(
            descriptors,
            dependency.readDescriptor.key,
            dependency.readDescriptor.payload,
            dependency.readDescriptor.manifestRecordHash,
            dependency.readDescriptor.descriptor);
    }

    void mergeAssetDependencyDiagnostics(
        OpenWorldStreamingDiagnostics& streaming,
        const StreamingAssetDependencyDiagnostics& assets)
    {
        streaming.assetDependencyManifestCount += assets.manifestRecordCount;
        streaming.assetMetadataCacheHitCount += assets.metadataCacheHitCount;
        streaming.liveAssetMeshCount += assets.liveStaticMeshCount;
        streaming.liveAssetTextureCount += assets.liveTextureCount;
        streaming.missingAssetDependencyCount += assets.missingRequiredCount + assets.missingOptionalCount;
        streaming.unsupportedAssetDependencyCount += assets.unsupportedTypeCount;
        streaming.sharedAssetReferenceCount += assets.sharedReferenceCount;
        streaming.assetReleaseLatencyMicroseconds += assets.releaseLatencyMicroseconds;
        streaming.liveResources.meshHandles += assets.liveStaticMeshCount;
        streaming.liveResources.textureHandles += assets.liveTextureCount;
        streaming.liveResources.assetDependencies += assets.liveStaticMeshCount + assets.liveTextureCount;
    }

    StreamingAssetCacheCallbacks assetCacheStreamingCallbacks(AssetCache& cache)
    {
        StreamingAssetCacheCallbacks callbacks;
        callbacks.acquireStaticMesh = [&cache](const std::filesystem::path& path) {
            return cache.acquireStaticMesh(path);
        };
        callbacks.releaseStaticMesh = [&cache](CachedStaticMesh mesh) {
            cache.release(mesh);
        };
        callbacks.acquireTexture = [&cache](const std::filesystem::path& path, const Renderer::TextureDescriptor& descriptor) {
            return cache.acquireTexture(path, descriptor);
        };
        callbacks.releaseTexture = [&cache](CachedTexture texture) {
            cache.release(texture);
        };
        return callbacks;
    }

    StreamingPromotionCallbacks makeAssetStreamingPromotionCallbacks(
        OpenWorldStreamingAssetResidency& residency)
    {
        return residency.promotionCallbacks();
    }
}
