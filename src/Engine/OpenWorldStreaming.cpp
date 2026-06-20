#include "Engine/OpenWorldStreaming.hpp"

#include <any>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

#include "Engine/NavigationCache.hpp"
#include "Engine/TerrainDerivedCache.hpp"

namespace Engine {
    namespace {
        constexpr uint64_t FnvaOffset = 1469598103934665603ull;
        constexpr uint64_t FnvaPrime = 1099511628211ull;

        [[nodiscard]] bool finiteVec3(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        void hashBytes(uint64_t& hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= FnvaPrime;
            }
        }

        void hashUint64(uint64_t& hash, uint64_t value)
        {
            hashBytes(hash, &value, sizeof(value));
        }

        void hashInt32(uint64_t& hash, int32_t value)
        {
            hashBytes(hash, &value, sizeof(value));
        }

        void hashFloat(uint64_t& hash, float value)
        {
            hashBytes(hash, &value, sizeof(value));
        }

        void hashString(uint64_t& hash, const std::string& value)
        {
            hashBytes(hash, value.data(), value.size());
            constexpr uint8_t terminator = 0;
            hashBytes(hash, &terminator, sizeof(terminator));
        }

        [[nodiscard]] int desiredPriority(StreamingResidencyState state)
        {
            switch (state) {
                case StreamingResidencyState::LiveActive:
                    return 0;
                case StreamingResidencyState::CachedCpu:
                    return 1;
                case StreamingResidencyState::ColdOnDisk:
                    return 2;
                default:
                    return 3;
            }
        }

        [[nodiscard]] StreamingResidencyState normalizedResidency(StreamingResidencyState state)
        {
            switch (state) {
                case StreamingResidencyState::LiveActive:
                case StreamingResidencyState::PromoteQueued:
                    return StreamingResidencyState::LiveActive;
                case StreamingResidencyState::CachedCpu:
                case StreamingResidencyState::ReadQueued:
                case StreamingResidencyState::DemoteQueued:
                case StreamingResidencyState::WriteQueued:
                    return StreamingResidencyState::CachedCpu;
                case StreamingResidencyState::ColdOnDisk:
                case StreamingResidencyState::Failed:
                case StreamingResidencyState::Count:
                    break;
            }
            return StreamingResidencyState::ColdOnDisk;
        }

        [[nodiscard]] StreamingResidencyState baseDesiredResidency(
            float distance,
            const StreamingPayloadResidencyPolicy& policy)
        {
            if (distance <= policy.activeRadius) {
                return StreamingResidencyState::LiveActive;
            }
            if (distance <= policy.cacheRadius) {
                return StreamingResidencyState::CachedCpu;
            }
            return StreamingResidencyState::ColdOnDisk;
        }

        [[nodiscard]] const StreamingChunkResidencyInput* findCurrentResidency(
            const std::vector<StreamingChunkResidencyInput>& currentResidency,
            const StreamingChunkManifestRecord& record)
        {
            for (const StreamingChunkResidencyInput& input : currentResidency) {
                if (input.payload == record.payload && input.key == record.key) {
                    return &input;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool decisionLess(
            const StreamingChunkResidencyDecision& lhs,
            const StreamingChunkResidencyDecision& rhs)
        {
            if (lhs.payload != rhs.payload) {
                return static_cast<uint32_t>(lhs.payload) < static_cast<uint32_t>(rhs.payload);
            }
            const int lhsPriority = desiredPriority(lhs.desired);
            const int rhsPriority = desiredPriority(rhs.desired);
            if (lhsPriority != rhsPriority) {
                return lhsPriority < rhsPriority;
            }
            if (lhs.distanceToFocus != rhs.distanceToFocus) {
                return lhs.distanceToFocus < rhs.distanceToFocus;
            }
            return stableStreamingChunkKeyString(lhs.key) < stableStreamingChunkKeyString(rhs.key);
        }

        [[nodiscard]] uint32_t payloadIndex(StreamingPayloadKind payload)
        {
            return static_cast<uint32_t>(payload);
        }

        [[nodiscard]] uint32_t stateIndex(StreamingResidencyState state)
        {
            return static_cast<uint32_t>(state);
        }

        [[nodiscard]] bool isQueuedOrCachedDesired(StreamingResidencyState desired)
        {
            return desired == StreamingResidencyState::CachedCpu ||
                desired == StreamingResidencyState::LiveActive ||
                desired == StreamingResidencyState::PromoteQueued;
        }

        [[nodiscard]] StreamingWorldBounds streamingBoundsFromRenderer(const Renderer::Aabb& bounds)
        {
            return {bounds.min, bounds.max};
        }

        [[nodiscard]] StreamingTerrainChunkPayload streamingPayloadFromTerrain(
            const TerrainCachedChunkPayload& payload)
        {
            StreamingTerrainChunkPayload result;
            result.chunkId = payload.chunkId;
            result.origin = payload.origin;
            result.size = payload.size;
            result.resolution = payload.resolution;
            result.heights = payload.heights;
            result.warnings = payload.warnings;
            return result;
        }

        [[nodiscard]] StreamingTerrainLodMeshPayload streamingPayloadFromLod(
            const TerrainCachedLodMeshPayload& payload)
        {
            StreamingTerrainLodMeshPayload result;
            result.chunkId = payload.chunkId;
            result.lodIndex = payload.lodIndex;
            result.renderResolution = payload.renderResolution;
            result.bounds = {payload.bounds.min, payload.bounds.max};
            result.vertexCount = static_cast<uint32_t>(payload.vertices.size());
            result.indexCount = static_cast<uint32_t>(payload.indices.size());
            return result;
        }

        [[nodiscard]] StreamingTerrainPhysicsColliderPayload streamingPayloadFromPhysics(
            const TerrainPhysicsColliderPayload& payload)
        {
            StreamingTerrainPhysicsColliderPayload result;
            result.chunkId = payload.chunkId;
            result.coord = payload.coord;
            result.bounds = {payload.bounds.min, payload.bounds.max};
            result.sourceResolution = payload.sourceResolution;
            result.colliderResolution = payload.colliderResolution;
            result.vertexCount = static_cast<uint32_t>(payload.vertices.size());
            result.indexCount = static_cast<uint32_t>(payload.indices.size());
            return result;
        }

        [[nodiscard]] StreamingNavigationTilePayload streamingPayloadFromNavigation(
            const NavigationTileCacheData& tile)
        {
            StreamingNavigationTilePayload result;
            result.coord = tile.coord;
            result.bounds = streamingBoundsFromRenderer(tile.bounds);
            result.detourTileData = tile.detourTileData;
            return result;
        }

        [[nodiscard]] StreamingReadJobResult executeReadRequest(
            const StreamingReadRequest& request,
            std::stop_token stopToken)
        {
            const auto start = std::chrono::steady_clock::now();
            StreamingReadJobResult result;
            result.request = request;

            const auto finish = [&]() {
                const auto end = std::chrono::steady_clock::now();
                result.elapsedMicroseconds = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                return result;
            };

            if (stopToken.stop_requested()) {
                result.status = StreamingReadStatus::Cancelled;
                result.message = "Streaming read cancelled.";
                return finish();
            }

            switch (request.descriptor.kind) {
                case StreamingReadDescriptorKind::TerrainChunkCache: {
                    if (!request.descriptor.terrainChunkManifest) {
                        result.status = StreamingReadStatus::Failed;
                        result.message = "Missing terrain chunk cache manifest.";
                        return finish();
                    }
                    const TerrainDerivedCacheChunkReadResult read =
                        TerrainDerivedCache::readChunk(*request.descriptor.terrainChunkManifest);
                    result.bytesRead = read.bytes;
                    result.message = read.message;
                    switch (read.status) {
                        case TerrainDerivedCacheStatus::Hit:
                            result.status = read.payload ? StreamingReadStatus::Hit : StreamingReadStatus::Corrupt;
                            if (read.payload) {
                                result.payload = streamingPayloadFromTerrain(*read.payload);
                            }
                            break;
                        case TerrainDerivedCacheStatus::Miss:
                            result.status = StreamingReadStatus::Miss;
                            break;
                        case TerrainDerivedCacheStatus::Stale:
                            result.status = StreamingReadStatus::Stale;
                            break;
                        case TerrainDerivedCacheStatus::Corrupt:
                            result.status = StreamingReadStatus::Corrupt;
                            break;
                        case TerrainDerivedCacheStatus::Cancelled:
                            result.status = StreamingReadStatus::Cancelled;
                            break;
                        case TerrainDerivedCacheStatus::WriteSuccess:
                        case TerrainDerivedCacheStatus::WriteFailed:
                            result.status = StreamingReadStatus::Failed;
                            break;
                    }
                    return finish();
                }
                case StreamingReadDescriptorKind::TerrainLodMeshCache: {
                    if (!request.descriptor.terrainChunkManifest) {
                        result.status = StreamingReadStatus::Failed;
                        result.message = "Missing terrain LOD cache manifest.";
                        return finish();
                    }
                    const TerrainDerivedCacheLodMeshReadResult read =
                        TerrainDerivedCache::readLodMesh(*request.descriptor.terrainChunkManifest);
                    result.bytesRead = read.bytes;
                    result.message = read.message;
                    switch (read.status) {
                        case TerrainDerivedCacheStatus::Hit:
                            result.status = read.payload ? StreamingReadStatus::Hit : StreamingReadStatus::Corrupt;
                            if (read.payload) {
                                result.payload = streamingPayloadFromLod(*read.payload);
                            }
                            break;
                        case TerrainDerivedCacheStatus::Miss:
                            result.status = StreamingReadStatus::Miss;
                            break;
                        case TerrainDerivedCacheStatus::Stale:
                            result.status = StreamingReadStatus::Stale;
                            break;
                        case TerrainDerivedCacheStatus::Corrupt:
                            result.status = StreamingReadStatus::Corrupt;
                            break;
                        case TerrainDerivedCacheStatus::Cancelled:
                            result.status = StreamingReadStatus::Cancelled;
                            break;
                        case TerrainDerivedCacheStatus::WriteSuccess:
                        case TerrainDerivedCacheStatus::WriteFailed:
                            result.status = StreamingReadStatus::Failed;
                            break;
                    }
                    return finish();
                }
                case StreamingReadDescriptorKind::NavigationTileCache: {
                    if (!request.descriptor.navigationSettings || !request.descriptor.navigationManifest) {
                        result.status = StreamingReadStatus::Failed;
                        result.message = "Missing navigation tile cache descriptor.";
                        return finish();
                    }
                    const NavigationCacheTileReadResult read = NavigationCache::readTileCache(
                        *request.descriptor.navigationSettings,
                        *request.descriptor.navigationManifest,
                        request.descriptor.navigationCoord);
                    result.message = read.message;
                    result.bytesRead = read.tile ? static_cast<uint64_t>(read.tile->detourTileData.size()) : 0;
                    switch (read.status) {
                        case NavigationCacheOperationStatus::Hit:
                            result.status = read.tile ? StreamingReadStatus::Hit : StreamingReadStatus::Corrupt;
                            if (read.tile) {
                                result.payload = streamingPayloadFromNavigation(*read.tile);
                            }
                            break;
                        case NavigationCacheOperationStatus::Miss:
                            result.status = StreamingReadStatus::Miss;
                            break;
                        case NavigationCacheOperationStatus::Stale:
                            result.status = StreamingReadStatus::Stale;
                            break;
                        case NavigationCacheOperationStatus::Corrupt:
                            result.status = StreamingReadStatus::Corrupt;
                            break;
                        case NavigationCacheOperationStatus::Cancelled:
                            result.status = StreamingReadStatus::Cancelled;
                            break;
                        case NavigationCacheOperationStatus::WriteSuccess:
                        case NavigationCacheOperationStatus::WriteFailed:
                            result.status = StreamingReadStatus::Failed;
                            break;
                    }
                    return finish();
                }
                case StreamingReadDescriptorKind::TerrainPhysicsColliderCache: {
                    if (!request.descriptor.terrainChunkManifest) {
                        result.status = StreamingReadStatus::Failed;
                        result.message = "Missing terrain physics collider cache manifest.";
                        return finish();
                    }
                    const TerrainDerivedCachePhysicsColliderReadResult read =
                        TerrainDerivedCache::readPhysicsCollider(*request.descriptor.terrainChunkManifest);
                    result.bytesRead = read.bytes;
                    result.message = read.message;
                    switch (read.status) {
                        case TerrainDerivedCacheStatus::Hit:
                            result.status = read.payload ? StreamingReadStatus::Hit : StreamingReadStatus::Corrupt;
                            if (read.payload) {
                                result.payload = streamingPayloadFromPhysics(*read.payload);
                            }
                            break;
                        case TerrainDerivedCacheStatus::Miss:
                            result.status = StreamingReadStatus::Miss;
                            break;
                        case TerrainDerivedCacheStatus::Stale:
                            result.status = StreamingReadStatus::Stale;
                            break;
                        case TerrainDerivedCacheStatus::Corrupt:
                            result.status = StreamingReadStatus::Corrupt;
                            break;
                        case TerrainDerivedCacheStatus::Cancelled:
                            result.status = StreamingReadStatus::Cancelled;
                            break;
                        case TerrainDerivedCacheStatus::WriteSuccess:
                        case TerrainDerivedCacheStatus::WriteFailed:
                            result.status = StreamingReadStatus::Failed;
                            break;
                    }
                    return finish();
                }
                case StreamingReadDescriptorKind::MetadataOnly:
                    result.status = StreamingReadStatus::Hit;
                    result.payload = StreamingMetadataPayload{request.descriptor.fakeMessage};
                    result.message = request.descriptor.fakeMessage.empty()
                        ? "Metadata-only streaming payload cached."
                        : request.descriptor.fakeMessage;
                    return finish();
                case StreamingReadDescriptorKind::Fake:
                    result.status = request.descriptor.fakeStatus;
                    result.bytesRead = request.descriptor.fakeBytes;
                    result.message = request.descriptor.fakeMessage;
                    result.payload = request.descriptor.fakePayload;
                    return finish();
                case StreamingReadDescriptorKind::Unsupported:
                    result.status = StreamingReadStatus::Unsupported;
                    result.message = request.descriptor.fakeMessage.empty()
                        ? "Streaming payload has no S2 read/decode implementation."
                        : request.descriptor.fakeMessage;
                    return finish();
                case StreamingReadDescriptorKind::None:
                    break;
            }

            result.status = StreamingReadStatus::Failed;
            result.message = "Missing streaming read descriptor.";
            return finish();
        }

        [[nodiscard]] bool isHit(StreamingReadStatus status)
        {
            return status == StreamingReadStatus::Hit;
        }

        void addLiveResourceCounts(StreamingLiveResourceCounts& lhs, const StreamingLiveResourceCounts& rhs)
        {
            lhs.terrainRenderHandles += rhs.terrainRenderHandles;
            lhs.meshHandles += rhs.meshHandles;
            lhs.materialHandles += rhs.materialHandles;
            lhs.textureHandles += rhs.textureHandles;
            lhs.navigationTiles += rhs.navigationTiles;
            lhs.physicsBodies += rhs.physicsBodies;
            lhs.physicsColliders += rhs.physicsColliders;
            lhs.sceneActors += rhs.sceneActors;
            lhs.sceneComponents += rhs.sceneComponents;
            lhs.assetDependencies += rhs.assetDependencies;
        }
    }

    const char* streamingResidencyStateName(StreamingResidencyState state)
    {
        switch (state) {
            case StreamingResidencyState::ColdOnDisk:
                return "ColdOnDisk";
            case StreamingResidencyState::ReadQueued:
                return "ReadQueued";
            case StreamingResidencyState::CachedCpu:
                return "CachedCpu";
            case StreamingResidencyState::PromoteQueued:
                return "PromoteQueued";
            case StreamingResidencyState::LiveActive:
                return "LiveActive";
            case StreamingResidencyState::DemoteQueued:
                return "DemoteQueued";
            case StreamingResidencyState::WriteQueued:
                return "WriteQueued";
            case StreamingResidencyState::Failed:
                return "Failed";
            case StreamingResidencyState::Count:
                break;
        }
        return "Unknown";
    }

    const char* streamingTransitionLaneName(StreamingTransitionLane lane)
    {
        switch (lane) {
            case StreamingTransitionLane::DiskReadDecode:
                return "DiskReadDecode";
            case StreamingTransitionLane::DerivedGeneration:
                return "DerivedGeneration";
            case StreamingTransitionLane::AssetPreload:
                return "AssetPreload";
            case StreamingTransitionLane::MainThreadPromote:
                return "MainThreadPromote";
            case StreamingTransitionLane::MainThreadDemote:
                return "MainThreadDemote";
            case StreamingTransitionLane::DiskCacheWrite:
                return "DiskCacheWrite";
            case StreamingTransitionLane::ManifestValidation:
                return "ManifestValidation";
            case StreamingTransitionLane::Count:
                break;
        }
        return "Unknown";
    }

    const char* streamingPayloadKindName(StreamingPayloadKind kind)
    {
        switch (kind) {
            case StreamingPayloadKind::TerrainChunk:
                return "TerrainChunk";
            case StreamingPayloadKind::TerrainRenderLod:
                return "TerrainRenderLod";
            case StreamingPayloadKind::NavigationTile:
                return "NavigationTile";
            case StreamingPayloadKind::PhysicsCollider:
                return "PhysicsCollider";
            case StreamingPayloadKind::SceneChunk:
                return "SceneChunk";
            case StreamingPayloadKind::AssetDependency:
                return "AssetDependency";
            case StreamingPayloadKind::Unknown:
                return "Unknown";
            case StreamingPayloadKind::Count:
                break;
        }
        return "Unknown";
    }

    const char* streamingReadStatusName(StreamingReadStatus status)
    {
        switch (status) {
            case StreamingReadStatus::Hit:
                return "Hit";
            case StreamingReadStatus::Miss:
                return "Miss";
            case StreamingReadStatus::Stale:
                return "Stale";
            case StreamingReadStatus::Corrupt:
                return "Corrupt";
            case StreamingReadStatus::Cancelled:
                return "Cancelled";
            case StreamingReadStatus::Unsupported:
                return "Unsupported";
            case StreamingReadStatus::Failed:
                return "Failed";
        }
        return "Unknown";
    }

    const char* streamingPromotionStatusName(StreamingPromotionStatus status)
    {
        switch (status) {
            case StreamingPromotionStatus::Success:
                return "Success";
            case StreamingPromotionStatus::Deferred:
                return "Deferred";
            case StreamingPromotionStatus::MissingCachedPayload:
                return "MissingCachedPayload";
            case StreamingPromotionStatus::UnsupportedPayload:
                return "UnsupportedPayload";
            case StreamingPromotionStatus::CallbackFailed:
                return "CallbackFailed";
            case StreamingPromotionStatus::Cancelled:
                return "Cancelled";
            case StreamingPromotionStatus::Stale:
                return "Stale";
        }
        return "Unknown";
    }

    OpenWorldStreamingDiagnostics makeEmptyOpenWorldStreamingDiagnostics()
    {
        return {};
    }

    bool operator==(const StreamingChunkKey& lhs, const StreamingChunkKey& rhs)
    {
        return lhs.kind == rhs.kind &&
            lhs.terrainChunk == rhs.terrainChunk &&
            lhs.asset == rhs.asset &&
            lhs.stableId == rhs.stableId;
    }

    bool operator!=(const StreamingChunkKey& lhs, const StreamingChunkKey& rhs)
    {
        return !(lhs == rhs);
    }

    bool isValid(const StreamingChunkKey& key)
    {
        switch (key.kind) {
            case StreamingChunkKeyKind::TerrainSourceChunk:
                return isValid(key.terrainChunk.source);
            case StreamingChunkKeyKind::SceneChunk:
                return !key.stableId.empty();
            case StreamingChunkKeyKind::AssetDependency:
                return isValid(key.asset);
            case StreamingChunkKeyKind::None:
                break;
        }
        return false;
    }

    std::string stableStreamingChunkKeyString(const StreamingChunkKey& key)
    {
        char buffer[128]{};
        switch (key.kind) {
            case StreamingChunkKeyKind::TerrainSourceChunk:
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "terrain:%llu:%d:%d",
                    static_cast<unsigned long long>(key.terrainChunk.source.value),
                    key.terrainChunk.coord.x,
                    key.terrainChunk.coord.z);
                return buffer;
            case StreamingChunkKeyKind::SceneChunk:
                return "scene:" + key.stableId;
            case StreamingChunkKeyKind::AssetDependency:
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "asset:%llu",
                    static_cast<unsigned long long>(key.asset.value));
                return key.stableId.empty() ? std::string{buffer} : std::string{buffer} + ":" + key.stableId;
            case StreamingChunkKeyKind::None:
                break;
        }
        return "none";
    }

    StreamingHaloPlannerSettings defaultStreamingHaloPlannerSettings()
    {
        StreamingHaloPlannerSettings settings;
        for (StreamingPayloadResidencyPolicy& policy : settings.payloadPolicies) {
            policy = {};
        }
        return settings;
    }

    bool isValid(const StreamingWorldBounds& bounds)
    {
        return finiteVec3(bounds.min) &&
            finiteVec3(bounds.max) &&
            bounds.min.x <= bounds.max.x &&
            bounds.min.y <= bounds.max.y &&
            bounds.min.z <= bounds.max.z;
    }

    float streamingBoundsDistanceXZ(const StreamingWorldBounds& bounds, const glm::vec3& focus)
    {
        if (!isValid(bounds) || !finiteVec3(focus)) {
            return std::numeric_limits<float>::infinity();
        }

        const float dx = focus.x < bounds.min.x
            ? bounds.min.x - focus.x
            : (focus.x > bounds.max.x ? focus.x - bounds.max.x : 0.0f);
        const float dz = focus.z < bounds.min.z
            ? bounds.min.z - focus.z
            : (focus.z > bounds.max.z ? focus.z - bounds.max.z : 0.0f);
        return std::sqrt(dx * dx + dz * dz);
    }

    uint64_t hashStreamingManifestRecord(const StreamingChunkManifestRecord& record)
    {
        uint64_t hash = FnvaOffset;
        hashUint64(hash, static_cast<uint64_t>(record.key.kind));
        hashUint64(hash, record.key.terrainChunk.source.value);
        hashInt32(hash, record.key.terrainChunk.coord.x);
        hashInt32(hash, record.key.terrainChunk.coord.z);
        hashUint64(hash, record.key.asset.value);
        hashString(hash, record.key.stableId);
        hashUint64(hash, static_cast<uint64_t>(record.payload));
        hashFloat(hash, record.bounds.min.x);
        hashFloat(hash, record.bounds.min.y);
        hashFloat(hash, record.bounds.min.z);
        hashFloat(hash, record.bounds.max.x);
        hashFloat(hash, record.bounds.max.y);
        hashFloat(hash, record.bounds.max.z);
        hashUint64(hash, record.sourceHash);
        hashUint64(hash, record.settingsHash);
        hashUint64(hash, record.estimatedBytes);
        hashUint64(hash, static_cast<uint64_t>(static_cast<uint32_t>(record.dirtyFlags)));
        for (bool available : record.availablePayloads) {
            hashUint64(hash, available ? 1ull : 0ull);
        }
        hashString(hash, record.debugName);
        return hash;
    }

    void setStreamingReadDescriptor(
        StreamingReadDescriptorTable& table,
        const StreamingChunkKey& key,
        StreamingPayloadKind payload,
        uint64_t manifestRecordHash,
        StreamingReadDescriptor descriptor)
    {
        for (StreamingReadDescriptorEntry& entry : table.entries) {
            if (entry.key == key && entry.payload == payload) {
                entry.manifestRecordHash = manifestRecordHash;
                entry.descriptor = std::move(descriptor);
                return;
            }
        }
        table.entries.push_back({key, payload, manifestRecordHash, std::move(descriptor)});
    }

    const StreamingReadDescriptorEntry* findStreamingReadDescriptor(
        const StreamingReadDescriptorTable& table,
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        for (const StreamingReadDescriptorEntry& entry : table.entries) {
            if (entry.key == key && entry.payload == payload) {
                return &entry;
            }
        }
        return nullptr;
    }

    void setStreamingGenerationDescriptor(
        StreamingGenerationDescriptorTable& table,
        StreamingChunkKey key,
        StreamingPayloadKind payload,
        uint64_t manifestRecordHash,
        StreamingGenerationDescriptor descriptor)
    {
        for (StreamingGenerationDescriptorEntry& entry : table.entries) {
            if (entry.key == key && entry.payload == payload) {
                entry.manifestRecordHash = manifestRecordHash;
                entry.descriptor = std::move(descriptor);
                return;
            }
        }
        table.entries.push_back({std::move(key), payload, manifestRecordHash, std::move(descriptor)});
    }

    const StreamingGenerationDescriptorEntry* findStreamingGenerationDescriptor(
        const StreamingGenerationDescriptorTable& table,
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        for (const StreamingGenerationDescriptorEntry& entry : table.entries) {
            if (entry.key == key && entry.payload == payload) {
                return &entry;
            }
        }
        return nullptr;
    }

    StreamingReadDescriptor terrainChunkCacheReadDescriptor(
        const TerrainDerivedCacheManifest& manifest)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::TerrainChunkCache;
        descriptor.terrainChunkManifest = std::make_shared<TerrainDerivedCacheManifest>(manifest);
        return descriptor;
    }

    StreamingReadDescriptor terrainLodMeshCacheReadDescriptor(
        const TerrainDerivedCacheManifest& manifest)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::TerrainLodMeshCache;
        descriptor.terrainChunkManifest = std::make_shared<TerrainDerivedCacheManifest>(manifest);
        return descriptor;
    }

    StreamingReadDescriptor terrainPhysicsColliderCacheReadDescriptor(
        const TerrainDerivedCacheManifest& manifest)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::TerrainPhysicsColliderCache;
        descriptor.terrainChunkManifest = std::make_shared<TerrainDerivedCacheManifest>(manifest);
        return descriptor;
    }

    StreamingReadDescriptor navigationTileCacheReadDescriptor(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        ChunkCoord coord)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::NavigationTileCache;
        descriptor.navigationSettings = std::make_shared<NavigationCacheSettings>(settings);
        descriptor.navigationManifest = std::make_shared<NavigationCacheManifest>(manifest);
        descriptor.navigationCoord = coord;
        return descriptor;
    }

    StreamingReadDescriptor metadataOnlyStreamingReadDescriptor(std::string label)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::MetadataOnly;
        descriptor.fakeMessage = std::move(label);
        return descriptor;
    }

    StreamingReadDescriptor unsupportedStreamingReadDescriptor(std::string message)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::Unsupported;
        descriptor.fakeMessage = std::move(message);
        return descriptor;
    }

    StreamingReadDescriptor fakeStreamingReadDescriptor(
        StreamingReadStatus status,
        std::optional<StreamingCachedPayload> payload,
        uint64_t bytes,
        std::string message)
    {
        StreamingReadDescriptor descriptor;
        descriptor.kind = StreamingReadDescriptorKind::Fake;
        descriptor.fakeStatus = status;
        descriptor.fakeBytes = bytes;
        descriptor.fakeMessage = std::move(message);
        descriptor.fakePayload = std::move(payload);
        return descriptor;
    }

    StreamingChunkManifestRecord makeTerrainStreamingManifestRecord(
        TerrainSourceChunkId id,
        const StreamingWorldBounds& bounds,
        uint64_t sourceHash,
        uint64_t settingsHash,
        uint64_t estimatedBytes,
        StreamingPayloadKind payload,
        std::string debugName)
    {
        StreamingChunkManifestRecord record;
        record.key.kind = StreamingChunkKeyKind::TerrainSourceChunk;
        record.key.terrainChunk = id;
        record.payload = payload;
        record.bounds = bounds;
        record.sourceHash = sourceHash;
        record.settingsHash = settingsHash;
        record.estimatedBytes = estimatedBytes;
        record.debugName = std::move(debugName);
        if (payloadIndex(payload) < StreamingPayloadKindCount) {
            record.availablePayloads[payloadIndex(payload)] = true;
        }
        return record;
    }

    StreamingChunkManifestRecord makeSceneStreamingManifestRecord(
        std::string stableChunkId,
        const StreamingWorldBounds& bounds,
        uint64_t sourceHash,
        uint64_t settingsHash,
        uint64_t estimatedBytes,
        StreamingPayloadKind payload,
        std::string debugName)
    {
        StreamingChunkManifestRecord record;
        record.key.kind = StreamingChunkKeyKind::SceneChunk;
        record.key.stableId = std::move(stableChunkId);
        record.payload = payload;
        record.bounds = bounds;
        record.sourceHash = sourceHash;
        record.settingsHash = settingsHash;
        record.estimatedBytes = estimatedBytes;
        record.debugName = std::move(debugName);
        if (payloadIndex(payload) < StreamingPayloadKindCount) {
            record.availablePayloads[payloadIndex(payload)] = true;
        }
        return record;
    }

    StreamingChunkManifestRecord makeAssetStreamingManifestRecord(
        AssetId asset,
        const StreamingWorldBounds& bounds,
        uint64_t sourceHash,
        uint64_t settingsHash,
        uint64_t estimatedBytes,
        std::string debugName)
    {
        StreamingChunkManifestRecord record;
        record.key.kind = StreamingChunkKeyKind::AssetDependency;
        record.key.asset = asset;
        record.payload = StreamingPayloadKind::AssetDependency;
        record.bounds = bounds;
        record.sourceHash = sourceHash;
        record.settingsHash = settingsHash;
        record.estimatedBytes = estimatedBytes;
        record.debugName = std::move(debugName);
        record.availablePayloads[payloadIndex(StreamingPayloadKind::AssetDependency)] = true;
        return record;
    }

    StreamingHaloPlan planStreamingHalo(
        const StreamingChunkManifest& manifest,
        const glm::vec3& focus,
        const std::vector<StreamingChunkResidencyInput>& currentResidency,
        const StreamingHaloPlannerSettings& settings)
    {
        StreamingHaloPlan plan;
        OpenWorldStreamingDiagnostics& diagnostics = plan.diagnostics;
        diagnostics.manifestRecordCount = static_cast<uint32_t>(manifest.records.size());
        diagnostics.hasLastFocus = finiteVec3(focus);
        diagnostics.lastFocus = focus;

        if (!finiteVec3(focus)) {
            diagnostics.manifestRecordsSkipped = static_cast<uint32_t>(manifest.records.size());
            diagnostics.invalidBoundsCount = static_cast<uint32_t>(manifest.records.size());
            return plan;
        }

        for (const StreamingChunkManifestRecord& record : manifest.records) {
            if (payloadIndex(record.payload) >= StreamingPayloadKindCount ||
                record.payload == StreamingPayloadKind::Unknown ||
                !Engine::isValid(record.key) ||
                !Engine::isValid(record.bounds)) {
                ++diagnostics.manifestRecordsSkipped;
                if (!Engine::isValid(record.bounds)) {
                    ++diagnostics.invalidBoundsCount;
                }
                continue;
            }

            ++diagnostics.manifestRecordsConsidered;
            const StreamingChunkResidencyInput* currentInput = findCurrentResidency(currentResidency, record);
            const StreamingResidencyState rawCurrent = currentInput
                ? currentInput->state
                : StreamingResidencyState::ColdOnDisk;
            const StreamingResidencyState current = normalizedResidency(rawCurrent);
            const uint32_t currentIndex = static_cast<uint32_t>(rawCurrent);
            if (currentIndex < StreamingResidencyStateCount) {
                ++diagnostics.actualChunksByState[currentIndex];
            }

            const StreamingPayloadResidencyPolicy& policy = settings.payloadPolicies[payloadIndex(record.payload)];
            const float distance = streamingBoundsDistanceXZ(record.bounds, focus);
            const StreamingResidencyState baseDesired = baseDesiredResidency(distance, policy);
            StreamingResidencyState desired = baseDesired;
            bool hysteresisRetained = false;
            if (current == StreamingResidencyState::LiveActive &&
                distance <= policy.activeRadius + policy.hysteresis) {
                desired = StreamingResidencyState::LiveActive;
                hysteresisRetained = baseDesired != desired;
            } else if (current == StreamingResidencyState::CachedCpu &&
                distance > policy.activeRadius &&
                distance <= policy.cacheRadius + policy.hysteresis) {
                desired = StreamingResidencyState::CachedCpu;
                hysteresisRetained = baseDesired != desired;
            }

            StreamingChunkResidencyDecision decision;
            decision.key = record.key;
            decision.payload = record.payload;
            decision.current = current;
            decision.desired = desired;
            decision.distanceToFocus = distance;
            decision.transitionCandidate = current != desired;
            decision.hysteresisRetained = hysteresisRetained;
            if (hysteresisRetained) {
                ++diagnostics.hysteresisRetainedCount;
                ++diagnostics.hysteresisChurnCount;
            }
            if (decision.transitionCandidate) {
                ++diagnostics.transitionCandidateCount;
            }
            plan.decisions.push_back(std::move(decision));
        }

        std::sort(plan.decisions.begin(), plan.decisions.end(), decisionLess);

        std::array<uint32_t, StreamingPayloadKindCount> transitionsByPayload{};
        for (StreamingChunkResidencyDecision& decision : plan.decisions) {
            const uint32_t index = payloadIndex(decision.payload);
            if (decision.transitionCandidate) {
                const uint32_t maxTransitions = settings.payloadPolicies[index].maxTransitionsPerFrame;
                if (transitionsByPayload[index] >= maxTransitions) {
                    decision.transitionLimited = true;
                    decision.desired = decision.current;
                    ++diagnostics.transitionLimitedCount;
                    ++diagnostics.evictionBlockedCount;
                } else {
                    ++transitionsByPayload[index];
                }
            }

            const uint32_t desiredIndex = static_cast<uint32_t>(decision.desired);
            if (desiredIndex < StreamingResidencyStateCount) {
                ++diagnostics.desiredChunksByState[desiredIndex];
            }
            if (index < StreamingPayloadKindCount) {
                ++diagnostics.desiredChunksByPayload[index];
            }
        }

        return plan;
    }

    OpenWorldStreamingCacheHalo::OpenWorldStreamingCacheHalo(StreamingCacheHaloSettings settings)
        : settings_(settings)
    {
        rebuildDiagnostics();
    }

    void OpenWorldStreamingCacheHalo::update(
        AsyncWorkQueue& queue,
        const StreamingHaloPlan& plan,
        const StreamingReadDescriptorTable& descriptors)
    {
        uint32_t queuedThisUpdate = 0;
        for (const StreamingChunkResidencyDecision& decision : plan.decisions) {
            Record& record = ensureRecord(decision.key, decision.payload);
            if (!isQueuedOrCachedDesired(decision.desired)) {
                cancelPending(queue, record);
                record.state = StreamingResidencyState::ColdOnDisk;
                record.message.clear();
                if (settings_.releaseCachedPayloadsOutsideCacheHalo) {
                    record.cachedPayload.reset();
                    record.manifestRecordHash = 0;
                }
                continue;
            }

            if (record.state == StreamingResidencyState::CachedCpu &&
                record.cachedPayload.has_value()) {
                continue;
            }
            if (record.state == StreamingResidencyState::ReadQueued) {
                continue;
            }

            const StreamingReadDescriptorEntry* descriptor =
                findStreamingReadDescriptor(descriptors, decision.key, decision.payload);
            if (!descriptor &&
                decision.payload == StreamingPayloadKind::AssetDependency &&
                settings_.metadataOnlyAssetsBecomeCached) {
                record.state = StreamingResidencyState::CachedCpu;
                record.cachedPayload = StreamingMetadataPayload{stableStreamingChunkKeyString(decision.key)};
                record.message = "Metadata-only asset dependency cached.";
                continue;
            }
            if (!descriptor) {
                ++diagnostics_.unsupportedReadCount;
                ++diagnostics_.payloads[payloadIndex(decision.payload)].corrupt;
                diagnostics_.lastFailure = {
                    true,
                    StreamingTransitionLane::DiskReadDecode,
                    decision.payload,
                    StreamingResidencyState::Failed,
                    stableStreamingChunkKeyString(decision.key),
                    streamingReadStatusName(StreamingReadStatus::Unsupported),
                    "Missing streaming read descriptor.",
                };
                record.state = StreamingResidencyState::Failed;
                record.message = "Missing streaming read descriptor.";
                continue;
            }

            if (queuedThisUpdate >= settings_.maxReadJobsQueuedPerUpdate) {
                continue;
            }
            if (enqueueRead(queue, record, *descriptor)) {
                ++queuedThisUpdate;
            }
        }
        rebuildDiagnostics();
    }

    void OpenWorldStreamingCacheHalo::pollCompleted(AsyncWorkQueue& queue)
    {
        mergeCompleted(queue.pollCompleted());
    }

    void OpenWorldStreamingCacheHalo::mergeCompleted(const std::vector<AsyncCompletedJob>& completedJobs)
    {
        uint32_t merged = 0;
        for (const AsyncCompletedJob& completed : completedJobs) {
            if (merged >= settings_.maxCompletedJobsMergedPerUpdate) {
                break;
            }

            Record* recordByHandle = nullptr;
            for (Record& record : records_) {
                if (record.pendingRead == completed.handle) {
                    recordByHandle = &record;
                    break;
                }
            }
            if (!recordByHandle) {
                continue;
            }

            ++merged;
            StreamingReadJobResult read;
            bool hasRead = false;
            if (completed.result.type() == typeid(StreamingReadJobResult)) {
                read = std::any_cast<StreamingReadJobResult>(completed.result);
                hasRead = true;
            } else if (completed.result.type() == typeid(std::exception_ptr)) {
                read.request.key = recordByHandle->key;
                read.request.payload = recordByHandle->payload;
                read.request.requestGeneration = recordByHandle->generation;
                read.status = StreamingReadStatus::Failed;
                read.message = "Streaming read job threw an exception.";
                hasRead = true;
            }
            if (!hasRead) {
                read.request.key = recordByHandle->key;
                read.request.payload = recordByHandle->payload;
                read.request.requestGeneration = recordByHandle->generation;
                read.status = StreamingReadStatus::Failed;
                read.message = "Streaming read job returned an unexpected result type.";
            }
            if (completed.cancelled) {
                read.status = StreamingReadStatus::Cancelled;
                if (read.message.empty()) {
                    read.message = "Streaming read job cancelled.";
                }
            }

            Record* record = findRecord(read.request.key, read.request.payload);
            if (!record || record->generation != read.request.requestGeneration ||
                record->pendingRead != completed.handle) {
                ++diagnostics_.staleReadCompletionCount;
                ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)].staleCount;
                if (recordByHandle && recordByHandle->pendingRead == completed.handle) {
                    recordByHandle->pendingRead = {};
                }
                continue;
            }

            record->pendingRead = {};
            record->message = read.message;

            StreamingLaneDiagnostics& lane =
                diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)];
            ++lane.completedCount;
            lane.elapsedMicroseconds += read.elapsedMicroseconds;
            lane.bytesRead += read.bytesRead;
            switch (read.status) {
                case StreamingReadStatus::Hit:
                    ++diagnostics_.payloads[payloadIndex(record->payload)].hits;
                    record->state = StreamingResidencyState::CachedCpu;
                    record->cachedPayload = read.payload.value_or(StreamingMetadataPayload{});
                    record->manifestRecordHash = read.request.manifestRecordHash;
                    break;
                case StreamingReadStatus::Miss:
                    ++diagnostics_.payloads[payloadIndex(record->payload)].misses;
                    record->state = StreamingResidencyState::Failed;
                    break;
                case StreamingReadStatus::Stale:
                    ++diagnostics_.payloads[payloadIndex(record->payload)].stale;
                    record->state = StreamingResidencyState::Failed;
                    break;
                case StreamingReadStatus::Corrupt:
                    ++diagnostics_.payloads[payloadIndex(record->payload)].corrupt;
                    record->state = StreamingResidencyState::Failed;
                    break;
                case StreamingReadStatus::Cancelled:
                    ++lane.cancelledCount;
                    record->state = StreamingResidencyState::ColdOnDisk;
                    break;
                case StreamingReadStatus::Unsupported:
                    ++diagnostics_.unsupportedReadCount;
                    ++diagnostics_.payloads[payloadIndex(record->payload)].corrupt;
                    record->state = StreamingResidencyState::Failed;
                    break;
                case StreamingReadStatus::Failed:
                    ++lane.failedCount;
                    record->state = StreamingResidencyState::Failed;
                    break;
            }

            if (!isHit(read.status)) {
                diagnostics_.lastFailure = {
                    true,
                    StreamingTransitionLane::DiskReadDecode,
                    record->payload,
                    record->state,
                    stableStreamingChunkKeyString(record->key),
                    streamingReadStatusName(read.status),
                    read.message,
                };
            }
        }
        rebuildDiagnostics();
    }

    void OpenWorldStreamingCacheHalo::clear()
    {
        records_.clear();
        diagnostics_ = {};
        rebuildDiagnostics();
    }

    StreamingCacheHaloSnapshot OpenWorldStreamingCacheHalo::snapshot() const
    {
        StreamingCacheHaloSnapshot snapshot;
        snapshot.diagnostics = diagnostics_;
        snapshot.residency.reserve(records_.size());
        snapshot.records.reserve(records_.size());
        for (const Record& record : records_) {
            snapshot.residency.push_back({record.key, record.payload, record.state});
            snapshot.records.push_back({
                stableStreamingChunkKeyString(record.key),
                record.payload,
                record.state,
                record.message,
            });
        }
        return snapshot;
    }

    OpenWorldStreamingDiagnostics OpenWorldStreamingCacheHalo::diagnostics() const
    {
        return diagnostics_;
    }

    std::optional<StreamingCachedPayload> OpenWorldStreamingCacheHalo::cachedPayload(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload) const
    {
        const Record* record = findRecord(key, payload);
        if (!record || record->state != StreamingResidencyState::CachedCpu) {
            return std::nullopt;
        }
        return record->cachedPayload;
    }

    OpenWorldStreamingCacheHalo::Record* OpenWorldStreamingCacheHalo::findRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        for (Record& record : records_) {
            if (record.key == key && record.payload == payload) {
                return &record;
            }
        }
        return nullptr;
    }

    const OpenWorldStreamingCacheHalo::Record* OpenWorldStreamingCacheHalo::findRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload) const
    {
        for (const Record& record : records_) {
            if (record.key == key && record.payload == payload) {
                return &record;
            }
        }
        return nullptr;
    }

    OpenWorldStreamingCacheHalo::Record& OpenWorldStreamingCacheHalo::ensureRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        if (Record* record = findRecord(key, payload)) {
            return *record;
        }
        records_.push_back({key, payload});
        return records_.back();
    }

    void OpenWorldStreamingCacheHalo::cancelPending(AsyncWorkQueue& queue, Record& record)
    {
        if (record.state != StreamingResidencyState::ReadQueued ||
            record.pendingRead.id == UINT64_MAX) {
            return;
        }
        queue.cancel(record.pendingRead);
        ++record.generation;
        ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)].cancelledCount;
    }

    bool OpenWorldStreamingCacheHalo::enqueueRead(
        AsyncWorkQueue& queue,
        Record& record,
        const StreamingReadDescriptorEntry& descriptor)
    {
        ++record.generation;
        StreamingReadRequest request;
        request.key = record.key;
        request.payload = record.payload;
        request.manifestRecordHash = descriptor.manifestRecordHash;
        request.requestGeneration = record.generation;
        request.descriptor = descriptor.descriptor;

        std::string label = "streaming read";
        if (settings_.queueLabelsEnabled) {
            label += " ";
            label += streamingPayloadKindName(record.payload);
            label += " ";
            label += stableStreamingChunkKeyString(record.key);
        }

        AsyncJobHandle handle = queue.submit(std::move(label), [request = std::move(request)](std::stop_token stopToken) -> std::any {
            return executeReadRequest(request, stopToken);
        });
        if (handle.id == UINT64_MAX) {
            record.state = StreamingResidencyState::Failed;
            record.message = "Failed to enqueue streaming read job.";
            diagnostics_.lastFailure = {
                true,
                StreamingTransitionLane::DiskReadDecode,
                record.payload,
                record.state,
                stableStreamingChunkKeyString(record.key),
                streamingReadStatusName(StreamingReadStatus::Failed),
                record.message,
            };
            return false;
        }

        record.pendingRead = handle;
        record.state = StreamingResidencyState::ReadQueued;
        record.cachedPayload.reset();
        record.message.clear();
        ++diagnostics_.transitionCountThisFrame[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)];
        ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)].queuedCount;
        return true;
    }

    void OpenWorldStreamingCacheHalo::rebuildDiagnostics()
    {
        for (uint32_t& count : diagnostics_.actualChunksByState) {
            count = 0;
        }
        diagnostics_.pendingReadCount = 0;
        diagnostics_.cachedCpuPayloadCount = 0;
        diagnostics_.estimatedResidentBytes = 0;
        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)].activeJobCount = 0;

        for (const Record& record : records_) {
            const uint32_t index = stateIndex(record.state);
            if (index < StreamingResidencyStateCount) {
                ++diagnostics_.actualChunksByState[index];
            }
            if (record.state == StreamingResidencyState::ReadQueued) {
                ++diagnostics_.pendingReadCount;
            }
            if (record.cachedPayload.has_value()) {
                ++diagnostics_.cachedCpuPayloadCount;
                diagnostics_.estimatedResidentBytes += std::visit([](const auto& payload) -> uint64_t {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<T, StreamingTerrainChunkPayload>) {
                        return static_cast<uint64_t>(payload.heights.size() * sizeof(float));
                    } else if constexpr (std::is_same_v<T, StreamingNavigationTilePayload>) {
                        return static_cast<uint64_t>(payload.detourTileData.size());
                    } else {
                        return 0;
                    }
                }, *record.cachedPayload);
            }
        }
        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DiskReadDecode)].activeJobCount =
            diagnostics_.pendingReadCount;
    }

    OpenWorldStreamingDerivedGenerationHalo::OpenWorldStreamingDerivedGenerationHalo(
        StreamingGenerationSettings settings)
        : settings_(settings)
    {
        rebuildDiagnostics();
    }

    void OpenWorldStreamingDerivedGenerationHalo::update(
        AsyncWorkQueue& queue,
        const StreamingHaloPlan& plan,
        const StreamingCacheHaloSnapshot& cacheSnapshot,
        const StreamingGenerationDescriptorTable& descriptors)
    {
        if (settings_.policy == StreamingDerivedGenerationPolicy::ReadOnly) {
            rebuildDiagnostics();
            return;
        }

        uint32_t queuedThisUpdate = 0;
        for (const StreamingChunkResidencyDecision& decision : plan.decisions) {
            if (!isQueuedOrCachedDesired(decision.desired)) {
                if (Record* record = findRecord(decision.key, decision.payload)) {
                    record->state = StreamingResidencyState::ColdOnDisk;
                    record->cachedPayload.reset();
                    record->message.clear();
                }
                continue;
            }

            const bool alreadyCached = std::any_of(
                cacheSnapshot.residency.begin(),
                cacheSnapshot.residency.end(),
                [&](const StreamingChunkResidencyInput& input) {
                    return input.key == decision.key &&
                        input.payload == decision.payload &&
                        input.state == StreamingResidencyState::CachedCpu;
                });
            if (alreadyCached && settings_.policy != StreamingDerivedGenerationPolicy::Refresh) {
                continue;
            }

            Record& record = ensureRecord(decision.key, decision.payload);
            if (record.state == StreamingResidencyState::ReadQueued ||
                (record.cachedPayload.has_value() && settings_.policy != StreamingDerivedGenerationPolicy::Refresh)) {
                continue;
            }

            if (queuedThisUpdate >= settings_.maxGenerationJobsQueuedPerUpdate) {
                continue;
            }

            const StreamingGenerationDescriptorEntry* descriptor =
                findStreamingGenerationDescriptor(descriptors, decision.key, decision.payload);
            if (!descriptor || !descriptor->descriptor.generate) {
                ++diagnostics_.generationFailedCount;
                diagnostics_.lastFailure = {
                    true,
                    StreamingTransitionLane::DerivedGeneration,
                    decision.payload,
                    StreamingResidencyState::Failed,
                    stableStreamingChunkKeyString(decision.key),
                    streamingReadStatusName(StreamingReadStatus::Unsupported),
                    "Missing streaming generation descriptor.",
                };
                record.state = StreamingResidencyState::Failed;
                record.message = "Missing streaming generation descriptor.";
                continue;
            }

            if (enqueueGeneration(queue, record, *descriptor)) {
                ++queuedThisUpdate;
            }
        }
        rebuildDiagnostics();
    }

    void OpenWorldStreamingDerivedGenerationHalo::pollCompleted(AsyncWorkQueue& queue)
    {
        mergeCompleted(queue.pollCompleted());
    }

    void OpenWorldStreamingDerivedGenerationHalo::mergeCompleted(const std::vector<AsyncCompletedJob>& completedJobs)
    {
        uint32_t merged = 0;
        for (const AsyncCompletedJob& completed : completedJobs) {
            if (merged >= settings_.maxCompletedJobsMergedPerUpdate) {
                break;
            }

            Record* recordByHandle = nullptr;
            for (Record& record : records_) {
                if (record.pendingGeneration == completed.handle) {
                    recordByHandle = &record;
                    break;
                }
            }
            if (!recordByHandle) {
                continue;
            }

            ++merged;
            StreamingReadJobResult generated;
            if (completed.result.type() == typeid(StreamingReadJobResult)) {
                generated = std::any_cast<StreamingReadJobResult>(completed.result);
            } else {
                generated.request.key = recordByHandle->key;
                generated.request.payload = recordByHandle->payload;
                generated.request.requestGeneration = recordByHandle->generation;
                generated.status = StreamingReadStatus::Failed;
                generated.message = "Streaming generation job returned an unexpected result type.";
            }
            if (completed.cancelled) {
                generated.status = StreamingReadStatus::Cancelled;
                if (generated.message.empty()) {
                    generated.message = "Streaming generation job cancelled.";
                }
            }

            Record* record = findRecord(generated.request.key, generated.request.payload);
            if (!record || record->generation != generated.request.requestGeneration ||
                record->pendingGeneration != completed.handle) {
                ++diagnostics_.staleReadCompletionCount;
                ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DerivedGeneration)].staleCount;
                if (recordByHandle && recordByHandle->pendingGeneration == completed.handle) {
                    recordByHandle->pendingGeneration = {};
                }
                continue;
            }

            record->pendingGeneration = {};
            record->message = generated.message;
            StreamingLaneDiagnostics& lane =
                diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DerivedGeneration)];
            ++lane.completedCount;
            lane.elapsedMicroseconds += generated.elapsedMicroseconds;
            lane.bytesWritten += generated.bytesRead;

            if (generated.status == StreamingReadStatus::Hit && generated.payload.has_value()) {
                ++diagnostics_.generationCompletedCount;
                ++diagnostics_.payloads[payloadIndex(record->payload)].writes;
                record->state = StreamingResidencyState::CachedCpu;
                record->cachedPayload = generated.payload;
                record->manifestRecordHash = generated.request.manifestRecordHash;
            } else {
                ++diagnostics_.generationFailedCount;
                ++lane.failedCount;
                record->state = generated.status == StreamingReadStatus::Cancelled
                    ? StreamingResidencyState::ColdOnDisk
                    : StreamingResidencyState::Failed;
                diagnostics_.lastFailure = {
                    true,
                    StreamingTransitionLane::DerivedGeneration,
                    record->payload,
                    record->state,
                    stableStreamingChunkKeyString(record->key),
                    streamingReadStatusName(generated.status),
                    generated.message,
                };
            }
        }
        rebuildDiagnostics();
    }

    void OpenWorldStreamingDerivedGenerationHalo::clear()
    {
        records_.clear();
        diagnostics_ = {};
        rebuildDiagnostics();
    }

    StreamingGenerationHaloSnapshot OpenWorldStreamingDerivedGenerationHalo::snapshot() const
    {
        StreamingGenerationHaloSnapshot snapshot;
        snapshot.diagnostics = diagnostics_;
        snapshot.residency.reserve(records_.size());
        snapshot.records.reserve(records_.size());
        for (const Record& record : records_) {
            snapshot.residency.push_back({record.key, record.payload, record.state});
            snapshot.records.push_back({
                stableStreamingChunkKeyString(record.key),
                record.payload,
                record.state,
                record.message,
            });
        }
        return snapshot;
    }

    OpenWorldStreamingDiagnostics OpenWorldStreamingDerivedGenerationHalo::diagnostics() const
    {
        return diagnostics_;
    }

    std::optional<StreamingCachedPayload> OpenWorldStreamingDerivedGenerationHalo::cachedPayload(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload) const
    {
        const Record* record = findRecord(key, payload);
        if (!record || record->state != StreamingResidencyState::CachedCpu) {
            return std::nullopt;
        }
        return record->cachedPayload;
    }

    OpenWorldStreamingDerivedGenerationHalo::Record* OpenWorldStreamingDerivedGenerationHalo::findRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        for (Record& record : records_) {
            if (record.key == key && record.payload == payload) {
                return &record;
            }
        }
        return nullptr;
    }

    const OpenWorldStreamingDerivedGenerationHalo::Record* OpenWorldStreamingDerivedGenerationHalo::findRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload) const
    {
        for (const Record& record : records_) {
            if (record.key == key && record.payload == payload) {
                return &record;
            }
        }
        return nullptr;
    }

    OpenWorldStreamingDerivedGenerationHalo::Record& OpenWorldStreamingDerivedGenerationHalo::ensureRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        if (Record* record = findRecord(key, payload)) {
            return *record;
        }
        records_.push_back({key, payload});
        return records_.back();
    }

    bool OpenWorldStreamingDerivedGenerationHalo::enqueueGeneration(
        AsyncWorkQueue& queue,
        Record& record,
        const StreamingGenerationDescriptorEntry& descriptor)
    {
        ++record.generation;
        StreamingGenerationRequest request;
        request.key = record.key;
        request.payload = record.payload;
        request.manifestRecordHash = descriptor.manifestRecordHash;
        request.requestGeneration = record.generation;
        request.descriptor = descriptor.descriptor.descriptor;

        std::string label = "streaming generate";
        if (settings_.queueLabelsEnabled) {
            label += " ";
            label += streamingPayloadKindName(record.payload);
            label += " ";
            label += stableStreamingChunkKeyString(record.key);
        }

        StreamingGenerationCallback callback = descriptor.descriptor.generate;
        AsyncJobHandle handle = queue.submit(std::move(label), [request, callback = std::move(callback)](std::stop_token stopToken) -> std::any {
            StreamingReadRequest echo;
            echo.key = request.key;
            echo.payload = request.payload;
            echo.manifestRecordHash = request.manifestRecordHash;
            echo.requestGeneration = request.requestGeneration;
            echo.lane = StreamingTransitionLane::DerivedGeneration;
            echo.descriptor = request.descriptor;
            try {
                StreamingReadJobResult result = callback(request, stopToken);
                result.request = echo;
                return result;
            } catch (...) {
                StreamingReadJobResult result;
                result.request = echo;
                result.status = StreamingReadStatus::Failed;
                result.message = "Streaming generation job threw an exception.";
                return result;
            }
        });
        if (handle.id == UINT64_MAX) {
            record.state = StreamingResidencyState::Failed;
            record.message = "Failed to enqueue streaming generation job.";
            diagnostics_.lastFailure = {
                true,
                StreamingTransitionLane::DerivedGeneration,
                record.payload,
                record.state,
                stableStreamingChunkKeyString(record.key),
                streamingReadStatusName(StreamingReadStatus::Failed),
                record.message,
            };
            return false;
        }

        record.pendingGeneration = handle;
        record.state = StreamingResidencyState::ReadQueued;
        record.cachedPayload.reset();
        record.message.clear();
        ++diagnostics_.generationQueuedCount;
        ++diagnostics_.transitionCountThisFrame[static_cast<uint32_t>(StreamingTransitionLane::DerivedGeneration)];
        ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DerivedGeneration)].queuedCount;
        return true;
    }

    void OpenWorldStreamingDerivedGenerationHalo::rebuildDiagnostics()
    {
        for (uint32_t& count : diagnostics_.actualChunksByState) {
            count = 0;
        }
        diagnostics_.cachedCpuPayloadCount = 0;
        diagnostics_.estimatedResidentBytes = 0;
        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DerivedGeneration)].activeJobCount = 0;

        for (const Record& record : records_) {
            const uint32_t index = stateIndex(record.state);
            if (index < StreamingResidencyStateCount) {
                ++diagnostics_.actualChunksByState[index];
            }
            if (record.state == StreamingResidencyState::ReadQueued) {
                ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::DerivedGeneration)].activeJobCount;
            }
            if (record.cachedPayload.has_value()) {
                ++diagnostics_.cachedCpuPayloadCount;
            }
        }
    }

    OpenWorldStreamingLiveHalo::OpenWorldStreamingLiveHalo(StreamingPromotionSettings settings)
        : settings_(settings)
    {
        rebuildDiagnostics();
    }

    void OpenWorldStreamingLiveHalo::update(
        MainThreadWorkQueue& queue,
        const StreamingHaloPlan& plan,
        const OpenWorldStreamingCacheHalo& cache,
        StreamingPromotionCallbacks callbacks)
    {
        uint32_t promotesQueued = 0;
        uint32_t demotesQueued = 0;

        for (const StreamingChunkResidencyDecision& decision : plan.decisions) {
            Record* existing = findRecord(decision.key, decision.payload);
            if (decision.desired == StreamingResidencyState::LiveActive) {
                Record& record = ensureRecord(decision.key, decision.payload);
                if (record.state == StreamingResidencyState::LiveActive ||
                    record.state == StreamingResidencyState::PromoteQueued) {
                    continue;
                }
                if (record.state == StreamingResidencyState::DemoteQueued) {
                    ++record.generation;
                    record.state = StreamingResidencyState::LiveActive;
                    record.queuedTarget = StreamingResidencyState::LiveActive;
                    record.message = "Queued demotion invalidated by live residency.";
                    continue;
                }

                if (promotesQueued >= settings_.maxPromotesQueuedPerUpdate) {
                    ++diagnostics_.mainThreadPromoteItemsDeferred;
                    continue;
                }

                const std::optional<StreamingCachedPayload> payload =
                    cache.cachedPayload(decision.key, decision.payload);
                if (!payload) {
                    record.state = StreamingResidencyState::Failed;
                    record.message = "Missing cached payload for live promotion.";
                    ++diagnostics_.failedPromotionCount;
                    diagnostics_.lastFailure = {
                        true,
                        StreamingTransitionLane::MainThreadPromote,
                        decision.payload,
                        record.state,
                        stableStreamingChunkKeyString(decision.key),
                        streamingPromotionStatusName(StreamingPromotionStatus::MissingCachedPayload),
                        record.message,
                    };
                    continue;
                }

                enqueuePromote(queue, record, *payload, callbacks);
                ++promotesQueued;
                continue;
            }

            if (!existing) {
                continue;
            }
            if (existing->state == StreamingResidencyState::PromoteQueued) {
                ++existing->generation;
                existing->state = decision.desired == StreamingResidencyState::ColdOnDisk
                    ? StreamingResidencyState::ColdOnDisk
                    : StreamingResidencyState::CachedCpu;
                existing->queuedTarget = existing->state;
                existing->message = "Queued promotion invalidated by non-live residency.";
                continue;
            }
            if (existing->state != StreamingResidencyState::LiveActive ||
                existing->state == StreamingResidencyState::DemoteQueued) {
                continue;
            }
            if (demotesQueued >= settings_.maxDemotesQueuedPerUpdate) {
                ++diagnostics_.mainThreadDemoteItemsDeferred;
                continue;
            }
            enqueueDemote(queue, *existing, decision.desired, callbacks);
            ++demotesQueued;
        }

        rebuildDiagnostics();
    }

    void OpenWorldStreamingLiveHalo::clear()
    {
        records_.clear();
        diagnostics_ = {};
        rebuildDiagnostics();
    }

    StreamingLiveHaloSnapshot OpenWorldStreamingLiveHalo::snapshot() const
    {
        StreamingLiveHaloSnapshot snapshot;
        snapshot.diagnostics = diagnostics_;
        snapshot.residency.reserve(records_.size());
        snapshot.records.reserve(records_.size());
        for (const Record& record : records_) {
            snapshot.residency.push_back({record.key, record.payload, record.state});
            snapshot.records.push_back({
                stableStreamingChunkKeyString(record.key),
                record.payload,
                record.state,
                record.liveToken,
                record.message,
            });
        }
        return snapshot;
    }

    OpenWorldStreamingDiagnostics OpenWorldStreamingLiveHalo::diagnostics() const
    {
        return diagnostics_;
    }

    OpenWorldStreamingLiveHalo::Record* OpenWorldStreamingLiveHalo::findRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        for (Record& record : records_) {
            if (record.key == key && record.payload == payload) {
                return &record;
            }
        }
        return nullptr;
    }

    const OpenWorldStreamingLiveHalo::Record* OpenWorldStreamingLiveHalo::findRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload) const
    {
        for (const Record& record : records_) {
            if (record.key == key && record.payload == payload) {
                return &record;
            }
        }
        return nullptr;
    }

    OpenWorldStreamingLiveHalo::Record& OpenWorldStreamingLiveHalo::ensureRecord(
        const StreamingChunkKey& key,
        StreamingPayloadKind payload)
    {
        if (Record* record = findRecord(key, payload)) {
            return *record;
        }
        records_.push_back({key, payload});
        return records_.back();
    }

    void OpenWorldStreamingLiveHalo::enqueuePromote(
        MainThreadWorkQueue& queue,
        Record& record,
        const StreamingCachedPayload& payload,
        StreamingPromotionCallbacks callbacks)
    {
        ++record.generation;
        record.state = StreamingResidencyState::PromoteQueued;
        record.queuedTarget = StreamingResidencyState::LiveActive;
        record.message.clear();

        StreamingPromotionRequest request;
        request.key = record.key;
        request.payload = record.payload;
        request.manifestRecordHash = record.manifestRecordHash;
        request.requestGeneration = record.generation;
        request.targetState = StreamingResidencyState::LiveActive;
        request.debugName = stableStreamingChunkKeyString(record.key);

        std::string label = "streaming promote";
        if (settings_.queueLabelsEnabled) {
            label += " ";
            label += streamingPayloadKindName(record.payload);
            label += " ";
            label += request.debugName;
        }

        queue.enqueue({
            settings_.budgetCategory,
            settings_.promotePriority,
            std::move(label),
            [this, request, payload, callbacks = std::move(callbacks)]() mutable {
                const auto start = std::chrono::steady_clock::now();
                StreamingPromotionResult result;
                const Record* current = findRecord(request.key, request.payload);
                if (!current || current->generation != request.requestGeneration ||
                    current->state != StreamingResidencyState::PromoteQueued) {
                    result.status = StreamingPromotionStatus::Stale;
                    result.message = "Stale streaming promotion discarded before callback.";
                    applyPromoteResult(request, result);
                    return;
                }
                try {
                    if (!callbacks.promote) {
                        result.status = StreamingPromotionStatus::UnsupportedPayload;
                        result.message = "Missing streaming promotion callback.";
                    } else {
                        result = callbacks.promote(request, payload);
                    }
                } catch (...) {
                    result.status = StreamingPromotionStatus::CallbackFailed;
                    result.message = "Streaming promotion callback threw an exception.";
                }
                if (result.elapsedMicroseconds == 0) {
                    const auto end = std::chrono::steady_clock::now();
                    result.elapsedMicroseconds = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                }
                applyPromoteResult(request, result);
            },
        });

        ++diagnostics_.transitionCountThisFrame[static_cast<uint32_t>(StreamingTransitionLane::MainThreadPromote)];
        ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadPromote)].queuedCount;
    }

    void OpenWorldStreamingLiveHalo::enqueueDemote(
        MainThreadWorkQueue& queue,
        Record& record,
        StreamingResidencyState targetState,
        StreamingPromotionCallbacks callbacks)
    {
        ++record.generation;
        record.state = StreamingResidencyState::DemoteQueued;
        record.queuedTarget = targetState == StreamingResidencyState::ColdOnDisk
            ? StreamingResidencyState::ColdOnDisk
            : StreamingResidencyState::CachedCpu;
        record.message.clear();

        StreamingDemotionRequest request;
        request.key = record.key;
        request.payload = record.payload;
        request.manifestRecordHash = record.manifestRecordHash;
        request.requestGeneration = record.generation;
        request.targetState = record.queuedTarget;
        request.debugName = stableStreamingChunkKeyString(record.key);
        const StreamingRuntimeToken token = record.liveToken;

        std::string label = "streaming demote";
        if (settings_.queueLabelsEnabled) {
            label += " ";
            label += streamingPayloadKindName(record.payload);
            label += " ";
            label += request.debugName;
        }

        queue.enqueue({
            settings_.budgetCategory,
            settings_.demotePriority,
            std::move(label),
            [this, request, token, callbacks = std::move(callbacks)]() mutable {
                const auto start = std::chrono::steady_clock::now();
                StreamingPromotionResult result;
                const Record* current = findRecord(request.key, request.payload);
                if (!current || current->generation != request.requestGeneration ||
                    current->state != StreamingResidencyState::DemoteQueued) {
                    result.status = StreamingPromotionStatus::Stale;
                    result.message = "Stale streaming demotion discarded before callback.";
                    applyDemoteResult(request, result);
                    return;
                }
                try {
                    if (!callbacks.demote) {
                        result.status = StreamingPromotionStatus::UnsupportedPayload;
                        result.message = "Missing streaming demotion callback.";
                    } else {
                        result = callbacks.demote(request, token);
                    }
                } catch (...) {
                    result.status = StreamingPromotionStatus::CallbackFailed;
                    result.message = "Streaming demotion callback threw an exception.";
                }
                if (result.elapsedMicroseconds == 0) {
                    const auto end = std::chrono::steady_clock::now();
                    result.elapsedMicroseconds = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                }
                applyDemoteResult(request, result);
            },
        });

        ++diagnostics_.transitionCountThisFrame[static_cast<uint32_t>(StreamingTransitionLane::MainThreadDemote)];
        ++diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadDemote)].queuedCount;
    }

    void OpenWorldStreamingLiveHalo::applyPromoteResult(
        const StreamingPromotionRequest& request,
        const StreamingPromotionResult& result)
    {
        Record* record = findRecord(request.key, request.payload);
        StreamingLaneDiagnostics& lane =
            diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadPromote)];
        if (!record || record->generation != request.requestGeneration ||
            record->state != StreamingResidencyState::PromoteQueued) {
            ++diagnostics_.stalePromotionCompletionCount;
            ++lane.staleCount;
            rebuildDiagnostics();
            return;
        }

        ++lane.completedCount;
        lane.elapsedMicroseconds += result.elapsedMicroseconds;
        record->message = result.message;

        switch (result.status) {
            case StreamingPromotionStatus::Success:
                record->state = StreamingResidencyState::LiveActive;
                record->liveToken = result.liveToken;
                record->liveResources = result.liveResources;
                record->estimatedLiveBytes = result.estimatedLiveBytes;
                ++diagnostics_.mainThreadPromoteItemsRun;
                break;
            case StreamingPromotionStatus::Deferred:
                record->state = StreamingResidencyState::CachedCpu;
                ++diagnostics_.mainThreadPromoteItemsDeferred;
                break;
            case StreamingPromotionStatus::Cancelled:
                record->state = StreamingResidencyState::CachedCpu;
                ++lane.cancelledCount;
                break;
            case StreamingPromotionStatus::MissingCachedPayload:
            case StreamingPromotionStatus::UnsupportedPayload:
            case StreamingPromotionStatus::CallbackFailed:
            case StreamingPromotionStatus::Stale:
                record->state = StreamingResidencyState::Failed;
                ++diagnostics_.failedPromotionCount;
                ++lane.failedCount;
                diagnostics_.lastFailure = {
                    true,
                    StreamingTransitionLane::MainThreadPromote,
                    request.payload,
                    record->state,
                    stableStreamingChunkKeyString(request.key),
                    streamingPromotionStatusName(result.status),
                    result.message,
                };
                break;
        }

        rebuildDiagnostics();
    }

    void OpenWorldStreamingLiveHalo::applyDemoteResult(
        const StreamingDemotionRequest& request,
        const StreamingPromotionResult& result)
    {
        Record* record = findRecord(request.key, request.payload);
        StreamingLaneDiagnostics& lane =
            diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadDemote)];
        if (!record || record->generation != request.requestGeneration ||
            record->state != StreamingResidencyState::DemoteQueued) {
            ++diagnostics_.stalePromotionCompletionCount;
            ++lane.staleCount;
            rebuildDiagnostics();
            return;
        }

        ++lane.completedCount;
        lane.elapsedMicroseconds += result.elapsedMicroseconds;
        record->message = result.message;

        switch (result.status) {
            case StreamingPromotionStatus::Success:
                record->state = request.targetState;
                record->liveToken = {};
                record->liveResources = {};
                record->estimatedLiveBytes = 0;
                ++diagnostics_.mainThreadDemoteItemsRun;
                break;
            case StreamingPromotionStatus::Deferred:
                record->state = StreamingResidencyState::LiveActive;
                ++diagnostics_.mainThreadDemoteItemsDeferred;
                break;
            case StreamingPromotionStatus::Cancelled:
                record->state = StreamingResidencyState::LiveActive;
                ++lane.cancelledCount;
                break;
            case StreamingPromotionStatus::MissingCachedPayload:
            case StreamingPromotionStatus::UnsupportedPayload:
            case StreamingPromotionStatus::CallbackFailed:
            case StreamingPromotionStatus::Stale:
                record->state = StreamingResidencyState::LiveActive;
                ++diagnostics_.failedDemotionCount;
                ++lane.failedCount;
                diagnostics_.lastFailure = {
                    true,
                    StreamingTransitionLane::MainThreadDemote,
                    request.payload,
                    record->state,
                    stableStreamingChunkKeyString(request.key),
                    streamingPromotionStatusName(result.status),
                    result.message,
                };
                break;
        }

        rebuildDiagnostics();
    }

    void OpenWorldStreamingLiveHalo::rebuildDiagnostics()
    {
        for (uint32_t& count : diagnostics_.actualChunksByState) {
            count = 0;
        }
        diagnostics_.pendingPromoteCount = 0;
        diagnostics_.pendingDemoteCount = 0;
        diagnostics_.livePayloadCount = 0;
        diagnostics_.liveResources = {};
        diagnostics_.estimatedResidentBytes = 0;
        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadPromote)].activeJobCount = 0;
        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadDemote)].activeJobCount = 0;

        for (const Record& record : records_) {
            const uint32_t index = stateIndex(record.state);
            if (index < StreamingResidencyStateCount) {
                ++diagnostics_.actualChunksByState[index];
            }
            if (record.state == StreamingResidencyState::PromoteQueued) {
                ++diagnostics_.pendingPromoteCount;
            }
            if (record.state == StreamingResidencyState::DemoteQueued) {
                ++diagnostics_.pendingDemoteCount;
            }
            if (record.state == StreamingResidencyState::LiveActive) {
                ++diagnostics_.livePayloadCount;
                addLiveResourceCounts(diagnostics_.liveResources, record.liveResources);
                diagnostics_.estimatedResidentBytes += record.estimatedLiveBytes;
            }
        }

        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadPromote)].activeJobCount =
            diagnostics_.pendingPromoteCount;
        diagnostics_.lanes[static_cast<uint32_t>(StreamingTransitionLane::MainThreadDemote)].activeJobCount =
            diagnostics_.pendingDemoteCount;
    }
}
