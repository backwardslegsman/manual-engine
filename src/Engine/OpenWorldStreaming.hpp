#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/AssetRegistry.hpp"
#include "Engine/ChunkTypes.hpp"
#include "Engine/FrameBudget.hpp"
#include "Engine/TerrainImport.hpp"

namespace Engine {
    struct NavigationCacheManifest;
    struct NavigationCacheSettings;
    struct SceneSerializationSettings;
    struct SceneSerializedScene;
    struct TerrainDerivedCacheManifest;

    enum class StreamingResidencyState : uint32_t {
        ColdOnDisk,
        ReadQueued,
        CachedCpu,
        PromoteQueued,
        LiveActive,
        DemoteQueued,
        WriteQueued,
        Failed,
        Count,
    };

    enum class StreamingDirtyFlags : uint32_t {
        None = 0,
        SourceDirty = 1u << 0,
        DerivedDirty = 1u << 1,
        RuntimeDirty = 1u << 2,
        SaveDirty = 1u << 3,
    };

    enum class StreamingTransitionLane : uint32_t {
        DiskReadDecode,
        DerivedGeneration,
        AssetPreload,
        MainThreadPromote,
        MainThreadDemote,
        DiskCacheWrite,
        ManifestValidation,
        Count,
    };

    enum class StreamingPayloadKind : uint32_t {
        TerrainChunk,
        TerrainRenderLod,
        NavigationTile,
        PhysicsCollider,
        SceneChunk,
        AssetDependency,
        Unknown,
        Count,
    };

    inline constexpr uint32_t StreamingResidencyStateCount =
        static_cast<uint32_t>(StreamingResidencyState::Count);
    inline constexpr uint32_t StreamingTransitionLaneCount =
        static_cast<uint32_t>(StreamingTransitionLane::Count);
    inline constexpr uint32_t StreamingPayloadKindCount =
        static_cast<uint32_t>(StreamingPayloadKind::Count);

    enum class StreamingHaloProfile : uint32_t {
        Default,
        FarMetadata,
        CacheOnly,
        LowDetailLive,
        StandardLive,
        HighDetailLive,
        Behavior,
        Animation,
        AudioParticle,
        Editing,
        Count,
    };

    inline constexpr uint32_t StreamingHaloProfileCount =
        static_cast<uint32_t>(StreamingHaloProfile::Count);

    enum class StreamingChunkKeyKind : uint32_t {
        None,
        TerrainSourceChunk,
        SceneChunk,
        AssetDependency,
    };

    enum class StreamingReadDescriptorKind : uint32_t {
        None,
        TerrainChunkCache,
        TerrainLodMeshCache,
        NavigationTileCache,
        TerrainPhysicsColliderCache,
        SceneChunkBinary,
        MetadataOnly,
        Unsupported,
        Fake,
    };

    enum class StreamingReadStatus : uint32_t {
        Hit,
        Miss,
        Stale,
        Corrupt,
        Cancelled,
        Unsupported,
        Failed,
    };

    enum class StreamingPromotionStatus : uint32_t {
        Success,
        Deferred,
        MissingCachedPayload,
        UnsupportedPayload,
        CallbackFailed,
        Cancelled,
        Stale,
    };

    enum class StreamingDerivedGenerationPolicy : uint32_t {
        ReadOnly,
        GenerateOnMiss,
        Refresh,
    };

    [[nodiscard]] constexpr StreamingDirtyFlags operator|(
        StreamingDirtyFlags lhs,
        StreamingDirtyFlags rhs)
    {
        return static_cast<StreamingDirtyFlags>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr StreamingDirtyFlags operator&(
        StreamingDirtyFlags lhs,
        StreamingDirtyFlags rhs)
    {
        return static_cast<StreamingDirtyFlags>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    constexpr StreamingDirtyFlags& operator|=(
        StreamingDirtyFlags& lhs,
        StreamingDirtyFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    [[nodiscard]] constexpr bool hasAny(StreamingDirtyFlags flags, StreamingDirtyFlags test)
    {
        return (static_cast<uint32_t>(flags & test)) != 0;
    }

    struct StreamingLaneDiagnostics {
        uint32_t queuedCount = 0;
        uint32_t activeJobCount = 0;
        uint32_t completedCount = 0;
        uint32_t cancelledCount = 0;
        uint32_t staleCount = 0;
        uint32_t failedCount = 0;
        uint64_t elapsedMicroseconds = 0;
        uint64_t bytesRead = 0;
        uint64_t bytesWritten = 0;
    };

    struct StreamingPayloadCacheDiagnostics {
        uint32_t hits = 0;
        uint32_t misses = 0;
        uint32_t stale = 0;
        uint32_t corrupt = 0;
        uint32_t writes = 0;
    };

    struct StreamingLiveResourceCounts {
        uint32_t terrainRenderHandles = 0;
        uint32_t meshHandles = 0;
        uint32_t materialHandles = 0;
        uint32_t textureHandles = 0;
        uint32_t navigationTiles = 0;
        uint32_t physicsBodies = 0;
        uint32_t physicsColliders = 0;
        uint32_t sceneActors = 0;
        uint32_t sceneComponents = 0;
        uint32_t assetDependencies = 0;
    };

    struct StreamingLastFailure {
        bool hasFailure = false;
        StreamingTransitionLane lane = StreamingTransitionLane::DiskReadDecode;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingResidencyState state = StreamingResidencyState::Failed;
        std::string chunkId;
        std::string status;
        std::string message;
    };

    struct StreamingWorldBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    struct StreamingChunkKey {
        StreamingChunkKeyKind kind = StreamingChunkKeyKind::None;
        TerrainSourceChunkId terrainChunk;
        AssetId asset;
        std::string stableId;
        std::string variantId;
    };

    [[nodiscard]] bool operator==(const StreamingChunkKey& lhs, const StreamingChunkKey& rhs);
    [[nodiscard]] bool operator!=(const StreamingChunkKey& lhs, const StreamingChunkKey& rhs);
    [[nodiscard]] bool isValid(const StreamingChunkKey& key);
    [[nodiscard]] std::string stableStreamingChunkKeyString(const StreamingChunkKey& key);

    struct StreamingChunkManifestRecord {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingWorldBounds bounds;
        uint64_t sourceHash = 0;
        uint64_t settingsHash = 0;
        uint64_t estimatedBytes = 0;
        std::array<bool, StreamingPayloadKindCount> availablePayloads{};
        StreamingDirtyFlags dirtyFlags = StreamingDirtyFlags::None;
        StreamingHaloProfile haloProfile = StreamingHaloProfile::Default;
        uint32_t detailLevel = 0;
        int32_t priorityBias = 0;
        std::string debugName;
    };

    struct StreamingChunkManifest {
        std::vector<StreamingChunkManifestRecord> records;
    };

    struct StreamingPayloadResidencyPolicy {
        float activeRadius = 96.0f;
        float cacheRadius = 160.0f;
        float hysteresis = 16.0f;
        uint32_t maxTransitionsPerFrame = 64;
        bool liveAllowed = true;
        bool predictiveLiveAllowed = false;
    };

    struct StreamingHaloPlannerSettings {
        std::array<StreamingPayloadResidencyPolicy, StreamingPayloadKindCount> payloadPolicies{};
        std::array<
            std::array<StreamingPayloadResidencyPolicy, StreamingHaloProfileCount>,
            StreamingPayloadKindCount> profilePolicies{};
    };

    struct StreamingFocusInput {
        glm::vec3 position{0.0f};
        std::optional<glm::vec3> velocity;
        float predictionSeconds = 0.0f;
        float maxPredictionDistance = 0.0f;
        std::vector<glm::vec3> goalFocusPoints;
    };

    struct StreamingChunkResidencyInput {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
    };

    struct StreamingChunkResidencyDecision {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingResidencyState current = StreamingResidencyState::ColdOnDisk;
        StreamingResidencyState desired = StreamingResidencyState::ColdOnDisk;
        float distanceToFocus = 0.0f;
        bool transitionCandidate = false;
        bool transitionLimited = false;
        bool hysteresisRetained = false;
        StreamingHaloProfile haloProfile = StreamingHaloProfile::Default;
        uint32_t detailLevel = 0;
        int32_t priorityBias = 0;
        bool predictiveCandidate = false;
        bool activeFocusCandidate = false;
    };

    struct OpenWorldStreamingDiagnostics {
        std::array<uint32_t, StreamingResidencyStateCount> desiredChunksByState{};
        std::array<uint32_t, StreamingResidencyStateCount> actualChunksByState{};
        std::array<uint32_t, StreamingPayloadKindCount> desiredChunksByPayload{};
        std::array<uint32_t, StreamingHaloProfileCount> desiredChunksByProfile{};
        std::array<uint32_t, StreamingHaloProfileCount> transitionLimitedByProfile{};
        std::array<uint32_t, StreamingTransitionLaneCount> transitionCountThisFrame{};
        std::array<StreamingLaneDiagnostics, StreamingTransitionLaneCount> lanes{};
        std::array<StreamingPayloadCacheDiagnostics, StreamingPayloadKindCount> payloads{};
        StreamingLiveResourceCounts liveResources;
        StreamingLastFailure lastFailure;
        bool hasLastFocus = false;
        glm::vec3 lastFocus{0.0f};
        uint32_t manifestRecordCount = 0;
        uint32_t manifestRecordsConsidered = 0;
        uint32_t manifestRecordsSkipped = 0;
        uint32_t transitionCandidateCount = 0;
        uint32_t transitionLimitedCount = 0;
        uint32_t hysteresisRetainedCount = 0;
        uint32_t invalidBoundsCount = 0;
        uint32_t pendingReadCount = 0;
        uint32_t cachedCpuPayloadCount = 0;
        uint32_t staleReadCompletionCount = 0;
        uint32_t unsupportedReadCount = 0;
        uint32_t pendingPromoteCount = 0;
        uint32_t pendingDemoteCount = 0;
        uint32_t stalePromotionCompletionCount = 0;
        uint32_t failedPromotionCount = 0;
        uint32_t failedDemotionCount = 0;
        uint32_t livePayloadCount = 0;
        uint32_t bakeChunkCount = 0;
        uint32_t bakePayloadWriteCount = 0;
        uint32_t generationQueuedCount = 0;
        uint32_t generationCompletedCount = 0;
        uint32_t generationFailedCount = 0;
        uint32_t cacheInvalidationCount = 0;
        uint32_t assetDependencyManifestCount = 0;
        uint32_t assetMetadataCacheHitCount = 0;
        uint32_t liveAssetMeshCount = 0;
        uint32_t liveAssetTextureCount = 0;
        uint32_t missingAssetDependencyCount = 0;
        uint32_t unsupportedAssetDependencyCount = 0;
        uint32_t sharedAssetReferenceCount = 0;
        uint64_t assetReleaseLatencyMicroseconds = 0;
        uint32_t sceneChunkManifestCount = 0;
        uint32_t cachedSceneChunkPayloadCount = 0;
        uint32_t promotedSceneChunkCount = 0;
        uint32_t demotedSceneChunkCount = 0;
        uint32_t sceneChunkActorsCreated = 0;
        uint32_t sceneChunkComponentsCreated = 0;
        uint32_t sceneChunkActorsDestroyed = 0;
        uint32_t sceneChunkDuplicateStableIdCount = 0;
        uint32_t sceneChunkInvalidParentCount = 0;
        uint32_t sceneChunkInvalidComponentCount = 0;
        uint32_t sceneChunkUnsupportedOwnershipCount = 0;
        uint32_t mainThreadPromoteItemsRun = 0;
        uint32_t mainThreadPromoteItemsDeferred = 0;
        uint32_t mainThreadDemoteItemsRun = 0;
        uint32_t mainThreadDemoteItemsDeferred = 0;
        uint64_t estimatedResidentBytes = 0;
        uint32_t hysteresisChurnCount = 0;
        uint32_t evictionBlockedCount = 0;
        uint32_t variantRecordCount = 0;
        uint32_t activeFocusCandidateCount = 0;
        uint32_t predictiveCandidateCount = 0;
        uint32_t predictivePrefetchCount = 0;
        uint32_t prefetchRetainedCount = 0;
        uint32_t highDetailCandidateCount = 0;
    };

    struct StreamingHaloPlan {
        std::vector<StreamingChunkResidencyDecision> decisions;
        OpenWorldStreamingDiagnostics diagnostics;
    };

    struct StreamingTerrainChunkPayload {
        TerrainSourceChunkId chunkId;
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        uint32_t resolution = 0;
        std::vector<float> heights;
        std::vector<std::string> warnings;
    };

    struct StreamingNavigationTilePayload {
        ChunkCoord coord;
        StreamingWorldBounds bounds;
        std::vector<uint8_t> detourTileData;
    };

    struct StreamingTerrainLodMeshPayload {
        TerrainSourceChunkId chunkId;
        uint32_t lodIndex = 0;
        uint32_t renderResolution = 0;
        StreamingWorldBounds bounds;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    struct StreamingTerrainPhysicsColliderPayload {
        TerrainSourceChunkId chunkId;
        ChunkCoord coord;
        StreamingWorldBounds bounds;
        uint32_t sourceResolution = 0;
        uint32_t colliderResolution = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    struct StreamingMetadataPayload {
        std::string label;
    };

    struct StreamingSceneChunkPayload {
        std::string stableChunkId;
        uint32_t actorCount = 0;
        uint32_t componentCount = 0;
        std::vector<AssetId> assetDependencies;
        std::shared_ptr<const SceneSerializedScene> scene;
    };

    using StreamingCachedPayload = std::variant<
        std::monostate,
        StreamingTerrainChunkPayload,
        StreamingTerrainLodMeshPayload,
        StreamingNavigationTilePayload,
        StreamingTerrainPhysicsColliderPayload,
        StreamingSceneChunkPayload,
        StreamingMetadataPayload>;

    struct StreamingRuntimeToken {
        uint64_t value = 0;
    };

    struct StreamingPromotionSettings {
        uint32_t maxPromotesQueuedPerUpdate = 32;
        uint32_t maxDemotesQueuedPerUpdate = 32;
        bool queueLabelsEnabled = true;
        BudgetCategory budgetCategory = BudgetCategory::StreamingCommit;
        BudgetPriority promotePriority = BudgetPriority::Normal;
        BudgetPriority demotePriority = BudgetPriority::Low;
    };

    struct StreamingPromotionRequest {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        uint64_t manifestRecordHash = 0;
        uint64_t requestGeneration = 0;
        StreamingResidencyState targetState = StreamingResidencyState::LiveActive;
        std::string debugName;
    };

    struct StreamingDemotionRequest {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        uint64_t manifestRecordHash = 0;
        uint64_t requestGeneration = 0;
        StreamingResidencyState targetState = StreamingResidencyState::CachedCpu;
        std::string debugName;
    };

    struct StreamingPromotionResult {
        StreamingPromotionStatus status = StreamingPromotionStatus::Success;
        StreamingRuntimeToken liveToken;
        StreamingLiveResourceCounts liveResources;
        uint64_t estimatedLiveBytes = 0;
        uint64_t elapsedMicroseconds = 0;
        std::string message;
    };

    struct StreamingPromotionCallbacks {
        std::function<StreamingPromotionResult(
            const StreamingPromotionRequest&,
            const StreamingCachedPayload&)> promote;
        std::function<StreamingPromotionResult(
            const StreamingDemotionRequest&,
            StreamingRuntimeToken)> demote;
    };

    struct StreamingReadDescriptor {
        StreamingReadDescriptorKind kind = StreamingReadDescriptorKind::None;
        std::shared_ptr<const TerrainDerivedCacheManifest> terrainChunkManifest;
        std::shared_ptr<const NavigationCacheSettings> navigationSettings;
        std::shared_ptr<const NavigationCacheManifest> navigationManifest;
        ChunkCoord navigationCoord;
        std::filesystem::path sceneChunkPath;
        std::string sceneChunkStableId;
        std::vector<AssetId> sceneChunkAssetDependencies;
        StreamingReadStatus fakeStatus = StreamingReadStatus::Hit;
        uint64_t fakeBytes = 0;
        std::string fakeMessage;
        std::optional<StreamingCachedPayload> fakePayload;
    };

    struct StreamingReadDescriptorEntry {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        uint64_t manifestRecordHash = 0;
        StreamingReadDescriptor descriptor;
    };

    struct StreamingReadDescriptorTable {
        std::vector<StreamingReadDescriptorEntry> entries;
    };

    struct StreamingCacheHaloSettings {
        uint32_t maxReadJobsQueuedPerUpdate = 32;
        uint32_t maxCompletedJobsMergedPerUpdate = 64;
        bool queueLabelsEnabled = true;
        bool releaseCachedPayloadsOutsideCacheHalo = true;
        bool metadataOnlyAssetsBecomeCached = true;
    };

    struct StreamingReadRequest {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        uint64_t manifestRecordHash = 0;
        uint64_t requestGeneration = 0;
        StreamingTransitionLane lane = StreamingTransitionLane::DiskReadDecode;
        StreamingReadDescriptor descriptor;
    };

    struct StreamingReadJobResult {
        StreamingReadRequest request;
        StreamingReadStatus status = StreamingReadStatus::Failed;
        uint64_t bytesRead = 0;
        uint64_t elapsedMicroseconds = 0;
        std::string message;
        std::optional<StreamingCachedPayload> payload;
    };

    struct StreamingGenerationSettings {
        StreamingDerivedGenerationPolicy policy = StreamingDerivedGenerationPolicy::ReadOnly;
        uint32_t maxGenerationJobsQueuedPerUpdate = 16;
        uint32_t maxCompletedJobsMergedPerUpdate = 32;
        bool queueLabelsEnabled = true;
    };

    struct StreamingGenerationRequest {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        uint64_t manifestRecordHash = 0;
        uint64_t requestGeneration = 0;
        StreamingTransitionLane lane = StreamingTransitionLane::DerivedGeneration;
        StreamingReadDescriptor descriptor;
    };

    using StreamingGenerationCallback = std::function<StreamingReadJobResult(
        const StreamingGenerationRequest&,
        std::stop_token)>;

    struct StreamingGenerationDescriptor {
        StreamingReadDescriptor descriptor;
        StreamingGenerationCallback generate;
    };

    struct StreamingGenerationDescriptorEntry {
        StreamingChunkKey key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        uint64_t manifestRecordHash = 0;
        StreamingGenerationDescriptor descriptor;
    };

    struct StreamingGenerationDescriptorTable {
        std::vector<StreamingGenerationDescriptorEntry> entries;
    };

    struct StreamingCacheHaloDebugRecord {
        std::string key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
        std::string message;
    };

    struct StreamingCacheHaloSnapshot {
        std::vector<StreamingChunkResidencyInput> residency;
        OpenWorldStreamingDiagnostics diagnostics;
        std::vector<StreamingCacheHaloDebugRecord> records;
    };

    struct StreamingLiveHaloDebugRecord {
        std::string key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
        StreamingRuntimeToken liveToken;
        std::string message;
    };

    struct StreamingLiveHaloSnapshot {
        std::vector<StreamingChunkResidencyInput> residency;
        OpenWorldStreamingDiagnostics diagnostics;
        std::vector<StreamingLiveHaloDebugRecord> records;
    };

    struct StreamingGenerationHaloDebugRecord {
        std::string key;
        StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
        StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
        std::string message;
    };

    struct StreamingGenerationHaloSnapshot {
        std::vector<StreamingChunkResidencyInput> residency;
        OpenWorldStreamingDiagnostics diagnostics;
        std::vector<StreamingGenerationHaloDebugRecord> records;
    };

    class OpenWorldStreamingCacheHalo {
    public:
        explicit OpenWorldStreamingCacheHalo(StreamingCacheHaloSettings settings = {});

        void update(
            AsyncWorkQueue& queue,
            const StreamingHaloPlan& plan,
            const StreamingReadDescriptorTable& descriptors);
        void pollCompleted(AsyncWorkQueue& queue);
        void mergeCompleted(const std::vector<AsyncCompletedJob>& completedJobs);
        void clear();

        [[nodiscard]] StreamingCacheHaloSnapshot snapshot() const;
        [[nodiscard]] OpenWorldStreamingDiagnostics diagnostics() const;
        [[nodiscard]] std::optional<StreamingCachedPayload> cachedPayload(
            const StreamingChunkKey& key,
            StreamingPayloadKind payload) const;
        void storeCachedPayload(
            const StreamingChunkKey& key,
            StreamingPayloadKind payload,
            StreamingCachedPayload cachedPayload,
            uint64_t manifestRecordHash = 0);

    private:
        struct Record {
            StreamingChunkKey key;
            StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
            StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
            uint64_t manifestRecordHash = 0;
            uint64_t generation = 0;
            AsyncJobHandle pendingRead;
            std::optional<StreamingCachedPayload> cachedPayload;
            std::string message;
        };

        [[nodiscard]] Record* findRecord(const StreamingChunkKey& key, StreamingPayloadKind payload);
        [[nodiscard]] const Record* findRecord(const StreamingChunkKey& key, StreamingPayloadKind payload) const;
        [[nodiscard]] Record& ensureRecord(const StreamingChunkKey& key, StreamingPayloadKind payload);
        void cancelPending(AsyncWorkQueue& queue, Record& record);
        bool enqueueRead(
            AsyncWorkQueue& queue,
            Record& record,
            const StreamingReadDescriptorEntry& descriptor);
        void rebuildDiagnostics();

        StreamingCacheHaloSettings settings_;
        std::vector<Record> records_;
        OpenWorldStreamingDiagnostics diagnostics_;
    };

    class OpenWorldStreamingDerivedGenerationHalo {
    public:
        explicit OpenWorldStreamingDerivedGenerationHalo(StreamingGenerationSettings settings = {});

        void update(
            AsyncWorkQueue& queue,
            const StreamingHaloPlan& plan,
            const StreamingCacheHaloSnapshot& cacheSnapshot,
            const StreamingGenerationDescriptorTable& descriptors);
        void pollCompleted(AsyncWorkQueue& queue);
        void mergeCompleted(const std::vector<AsyncCompletedJob>& completedJobs);
        void clear();

        [[nodiscard]] StreamingGenerationHaloSnapshot snapshot() const;
        [[nodiscard]] OpenWorldStreamingDiagnostics diagnostics() const;
        [[nodiscard]] std::optional<StreamingCachedPayload> cachedPayload(
            const StreamingChunkKey& key,
            StreamingPayloadKind payload) const;

    private:
        struct Record {
            StreamingChunkKey key;
            StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
            StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
            uint64_t manifestRecordHash = 0;
            uint64_t generation = 0;
            AsyncJobHandle pendingGeneration;
            std::optional<StreamingCachedPayload> cachedPayload;
            std::string message;
        };

        [[nodiscard]] Record* findRecord(const StreamingChunkKey& key, StreamingPayloadKind payload);
        [[nodiscard]] const Record* findRecord(const StreamingChunkKey& key, StreamingPayloadKind payload) const;
        [[nodiscard]] Record& ensureRecord(const StreamingChunkKey& key, StreamingPayloadKind payload);
        bool enqueueGeneration(
            AsyncWorkQueue& queue,
            Record& record,
            const StreamingGenerationDescriptorEntry& descriptor);
        void rebuildDiagnostics();

        StreamingGenerationSettings settings_;
        std::vector<Record> records_;
        OpenWorldStreamingDiagnostics diagnostics_;
    };

    class OpenWorldStreamingLiveHalo {
    public:
        explicit OpenWorldStreamingLiveHalo(StreamingPromotionSettings settings = {});

        void update(
            MainThreadWorkQueue& queue,
            const StreamingHaloPlan& plan,
            const OpenWorldStreamingCacheHalo& cache,
            StreamingPromotionCallbacks callbacks);
        void clear();

        [[nodiscard]] StreamingLiveHaloSnapshot snapshot() const;
        [[nodiscard]] OpenWorldStreamingDiagnostics diagnostics() const;

    private:
        struct Record {
            StreamingChunkKey key;
            StreamingPayloadKind payload = StreamingPayloadKind::Unknown;
            StreamingResidencyState state = StreamingResidencyState::ColdOnDisk;
            uint64_t manifestRecordHash = 0;
            uint64_t generation = 0;
            StreamingRuntimeToken liveToken;
            StreamingLiveResourceCounts liveResources;
            uint64_t estimatedLiveBytes = 0;
            StreamingResidencyState queuedTarget = StreamingResidencyState::ColdOnDisk;
            std::string message;
        };

        [[nodiscard]] Record* findRecord(const StreamingChunkKey& key, StreamingPayloadKind payload);
        [[nodiscard]] const Record* findRecord(const StreamingChunkKey& key, StreamingPayloadKind payload) const;
        [[nodiscard]] Record& ensureRecord(const StreamingChunkKey& key, StreamingPayloadKind payload);
        void enqueuePromote(
            MainThreadWorkQueue& queue,
            Record& record,
            const StreamingCachedPayload& payload,
            StreamingPromotionCallbacks callbacks);
        void enqueueDemote(
            MainThreadWorkQueue& queue,
            Record& record,
            StreamingResidencyState targetState,
            StreamingPromotionCallbacks callbacks);
        void applyPromoteResult(
            const StreamingPromotionRequest& request,
            const StreamingPromotionResult& result);
        void applyDemoteResult(
            const StreamingDemotionRequest& request,
            const StreamingPromotionResult& result);
        void rebuildDiagnostics();

        StreamingPromotionSettings settings_;
        std::vector<Record> records_;
        OpenWorldStreamingDiagnostics diagnostics_;
    };

    [[nodiscard]] const char* streamingResidencyStateName(StreamingResidencyState state);
    [[nodiscard]] const char* streamingTransitionLaneName(StreamingTransitionLane lane);
    [[nodiscard]] const char* streamingPayloadKindName(StreamingPayloadKind kind);
    [[nodiscard]] const char* streamingHaloProfileName(StreamingHaloProfile profile);
    [[nodiscard]] const char* streamingReadStatusName(StreamingReadStatus status);
    [[nodiscard]] const char* streamingPromotionStatusName(StreamingPromotionStatus status);
    [[nodiscard]] OpenWorldStreamingDiagnostics makeEmptyOpenWorldStreamingDiagnostics();
    [[nodiscard]] StreamingHaloPlannerSettings defaultStreamingHaloPlannerSettings();
    [[nodiscard]] bool isValid(const StreamingWorldBounds& bounds);
    [[nodiscard]] float streamingBoundsDistanceXZ(const StreamingWorldBounds& bounds, const glm::vec3& focus);
    [[nodiscard]] uint64_t hashStreamingManifestRecord(const StreamingChunkManifestRecord& record);
    void setStreamingReadDescriptor(
        StreamingReadDescriptorTable& table,
        const StreamingChunkKey& key,
        StreamingPayloadKind payload,
        uint64_t manifestRecordHash,
        StreamingReadDescriptor descriptor);
    [[nodiscard]] const StreamingReadDescriptorEntry* findStreamingReadDescriptor(
        const StreamingReadDescriptorTable& table,
        const StreamingChunkKey& key,
        StreamingPayloadKind payload);
    void setStreamingGenerationDescriptor(
        StreamingGenerationDescriptorTable& table,
        StreamingChunkKey key,
        StreamingPayloadKind payload,
        uint64_t manifestRecordHash,
        StreamingGenerationDescriptor descriptor);
    [[nodiscard]] const StreamingGenerationDescriptorEntry* findStreamingGenerationDescriptor(
        const StreamingGenerationDescriptorTable& table,
        const StreamingChunkKey& key,
        StreamingPayloadKind payload);
    [[nodiscard]] StreamingReadDescriptor terrainChunkCacheReadDescriptor(
        const TerrainDerivedCacheManifest& manifest);
    [[nodiscard]] StreamingReadDescriptor terrainLodMeshCacheReadDescriptor(
        const TerrainDerivedCacheManifest& manifest);
    [[nodiscard]] StreamingReadDescriptor terrainPhysicsColliderCacheReadDescriptor(
        const TerrainDerivedCacheManifest& manifest);
    [[nodiscard]] StreamingReadDescriptor navigationTileCacheReadDescriptor(
        const NavigationCacheSettings& settings,
        const NavigationCacheManifest& manifest,
        ChunkCoord coord);
    [[nodiscard]] StreamingReadDescriptor sceneChunkBinaryReadDescriptor(
        std::filesystem::path path,
        std::string stableChunkId = {},
        std::vector<AssetId> assetDependencies = {});
    [[nodiscard]] StreamingReadDescriptor metadataOnlyStreamingReadDescriptor(std::string label = {});
    [[nodiscard]] StreamingReadDescriptor unsupportedStreamingReadDescriptor(std::string message = {});
    [[nodiscard]] StreamingReadDescriptor fakeStreamingReadDescriptor(
        StreamingReadStatus status,
        std::optional<StreamingCachedPayload> payload = std::nullopt,
        uint64_t bytes = 0,
        std::string message = {});
    [[nodiscard]] StreamingChunkManifestRecord makeTerrainStreamingManifestRecord(
        TerrainSourceChunkId id,
        const StreamingWorldBounds& bounds,
        uint64_t sourceHash,
        uint64_t settingsHash,
        uint64_t estimatedBytes,
        StreamingPayloadKind payload,
        std::string debugName = {});
    [[nodiscard]] StreamingChunkManifestRecord makeSceneStreamingManifestRecord(
        std::string stableChunkId,
        const StreamingWorldBounds& bounds,
        uint64_t sourceHash,
        uint64_t settingsHash,
        uint64_t estimatedBytes,
        StreamingPayloadKind payload,
        std::string debugName = {});
    [[nodiscard]] StreamingChunkManifestRecord makeAssetStreamingManifestRecord(
        AssetId asset,
        const StreamingWorldBounds& bounds,
        uint64_t sourceHash,
        uint64_t settingsHash,
        uint64_t estimatedBytes,
        std::string debugName = {});
    [[nodiscard]] StreamingHaloPlan planStreamingHalo(
        const StreamingChunkManifest& manifest,
        const StreamingFocusInput& focus,
        const std::vector<StreamingChunkResidencyInput>& currentResidency,
        const StreamingHaloPlannerSettings& settings);
    [[nodiscard]] StreamingHaloPlan planStreamingHalo(
        const StreamingChunkManifest& manifest,
        const glm::vec3& focus,
        const std::vector<StreamingChunkResidencyInput>& currentResidency,
        const StreamingHaloPlannerSettings& settings);
}
