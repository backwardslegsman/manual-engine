#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Engine/OpenWorldStreaming.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/SceneSerialization.hpp"

namespace Engine {
    inline constexpr const char* OpenWorldStreamingSceneChunksVersion =
        "open_world_streaming_scene_chunks_s6_v1";

    enum class StreamingSceneChunkActorOwnership : uint32_t {
        ChunkOwnedStatic,
        Global,
        Migratory,
    };

    struct StreamingSceneChunkDescriptor {
        std::string stableChunkId;
        StreamingWorldBounds bounds;
        uint64_t sourceHash = 0;
        uint64_t settingsHash = 0;
        uint64_t estimatedBytes = 0;
        StreamingSceneChunkActorOwnership ownership = StreamingSceneChunkActorOwnership::ChunkOwnedStatic;
        std::string debugName;
        std::vector<AssetId> assetDependencies;
    };

    struct StreamingSceneChunkBuildResult {
        bool success = false;
        StreamingChunkManifestRecord record;
        StreamingReadDescriptorEntry readDescriptor;
        std::string message;
    };

    struct StreamingSceneChunkDiagnostics {
        uint32_t manifestRecordCount = 0;
        uint32_t cachedPayloadCount = 0;
        uint32_t promotedChunkCount = 0;
        uint32_t demotedChunkCount = 0;
        uint32_t actorsCreated = 0;
        uint32_t componentsCreated = 0;
        uint32_t actorsDestroyed = 0;
        uint32_t duplicateStableIdCount = 0;
        uint32_t invalidParentCount = 0;
        uint32_t invalidComponentCount = 0;
        uint32_t unsupportedOwnershipCount = 0;
        std::string lastMessage;
    };

    struct StreamingSceneChunkResidencySettings {
        bool flushDestroyedActorsOnDemote = true;
    };

    class OpenWorldStreamingSceneChunkResidency {
    public:
        OpenWorldStreamingSceneChunkResidency(
            Scene& scene,
            const ReflectionRegistry& registry,
            StreamingSceneChunkResidencySettings settings = {});

        bool registerChunk(const StreamingSceneChunkDescriptor& descriptor);
        bool claimMigratoryActor(SceneObjectId actor, std::string stableChunkId);
        [[nodiscard]] StreamingPromotionCallbacks promotionCallbacks();
        [[nodiscard]] const StreamingSceneChunkDiagnostics& diagnostics() const;
        void clear();

    private:
        friend StreamingPromotionCallbacks makeSceneChunkStreamingPromotionCallbacks(
            OpenWorldStreamingSceneChunkResidency& residency);

        struct LiveActorClaim {
            SceneObjectId id;
            std::string stableChunkId;
        };

        struct LiveBinding {
            StreamingRuntimeToken token;
            std::string stableChunkId;
            StreamingSceneChunkActorOwnership ownership = StreamingSceneChunkActorOwnership::ChunkOwnedStatic;
            std::vector<SceneActorHandle> createdActors;
            uint32_t createdComponentCount = 0;
        };

        [[nodiscard]] StreamingPromotionResult promote(
            const StreamingPromotionRequest& request,
            const StreamingCachedPayload& payload);
        [[nodiscard]] StreamingPromotionResult demote(
            const StreamingDemotionRequest& request,
            StreamingRuntimeToken token);
        [[nodiscard]] const StreamingSceneChunkDescriptor* findDescriptor(const StreamingChunkKey& key) const;
        [[nodiscard]] SceneActorHandle findActorByStableId(SceneObjectId id) const;
        [[nodiscard]] LiveBinding* findBinding(StreamingRuntimeToken token);
        [[nodiscard]] const LiveBinding* findBinding(const std::string& stableChunkId) const;
        [[nodiscard]] bool actorClaimedByOtherChunk(SceneObjectId id, const std::string& stableChunkId) const;
        void releaseBinding(LiveBinding& binding);

        Scene& scene_;
        const ReflectionRegistry& registry_;
        StreamingSceneChunkResidencySettings settings_;
        std::vector<StreamingSceneChunkDescriptor> descriptors_;
        std::vector<LiveActorClaim> claims_;
        std::vector<LiveBinding> live_;
        uint64_t nextToken_ = 1;
        StreamingSceneChunkDiagnostics diagnostics_;
    };

    [[nodiscard]] StreamingSceneChunkBuildResult makeSceneChunkStreamingRecord(
        const StreamingSceneChunkDescriptor& descriptor,
        const std::filesystem::path& sceneBinaryPath);
    void addSceneChunkStreamingRecord(
        StreamingChunkManifest& manifest,
        StreamingReadDescriptorTable& descriptors,
        const StreamingSceneChunkBuildResult& chunk);
    void mergeSceneChunkStreamingDiagnostics(
        OpenWorldStreamingDiagnostics& streaming,
        const StreamingSceneChunkDiagnostics& sceneChunks);
    [[nodiscard]] StreamingPromotionCallbacks makeSceneChunkStreamingPromotionCallbacks(
        OpenWorldStreamingSceneChunkResidency& residency);
}
