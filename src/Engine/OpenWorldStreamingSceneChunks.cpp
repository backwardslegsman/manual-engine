#include "Engine/OpenWorldStreamingSceneChunks.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <utility>

namespace Engine {
    namespace {
        [[nodiscard]] bool isSceneChunkKey(const StreamingChunkKey& key)
        {
            return key.kind == StreamingChunkKeyKind::SceneChunk && !key.stableId.empty();
        }

        [[nodiscard]] bool containsActor(
            const std::vector<SceneActorHandle>& actors,
            SceneActorHandle actor)
        {
            return std::ranges::find(actors, actor) != actors.end();
        }
    }

    OpenWorldStreamingSceneChunkResidency::OpenWorldStreamingSceneChunkResidency(
        Scene& scene,
        const ReflectionRegistry& registry,
        StreamingSceneChunkResidencySettings settings)
        : scene_(scene)
        , registry_(registry)
        , settings_(settings)
    {
    }

    bool OpenWorldStreamingSceneChunkResidency::registerChunk(
        const StreamingSceneChunkDescriptor& descriptor)
    {
        if (descriptor.stableChunkId.empty() || !isValid(descriptor.bounds)) {
            diagnostics_.lastMessage = "Invalid scene chunk descriptor.";
            return false;
        }

        for (StreamingSceneChunkDescriptor& existing : descriptors_) {
            if (existing.stableChunkId == descriptor.stableChunkId) {
                existing = descriptor;
                return true;
            }
        }
        descriptors_.push_back(descriptor);
        diagnostics_.manifestRecordCount = static_cast<uint32_t>(descriptors_.size());
        return true;
    }

    bool OpenWorldStreamingSceneChunkResidency::claimMigratoryActor(
        SceneObjectId actor,
        std::string stableChunkId)
    {
        if (!isValid(actor) || stableChunkId.empty()) {
            return false;
        }
        for (LiveActorClaim& claim : claims_) {
            if (claim.id == actor) {
                claim.stableChunkId = std::move(stableChunkId);
                return true;
            }
        }
        claims_.push_back({actor, std::move(stableChunkId)});
        return true;
    }

    StreamingPromotionCallbacks OpenWorldStreamingSceneChunkResidency::promotionCallbacks()
    {
        return makeSceneChunkStreamingPromotionCallbacks(*this);
    }

    const StreamingSceneChunkDiagnostics& OpenWorldStreamingSceneChunkResidency::diagnostics() const
    {
        return diagnostics_;
    }

    void OpenWorldStreamingSceneChunkResidency::clear()
    {
        for (auto it = live_.rbegin(); it != live_.rend(); ++it) {
            releaseBinding(*it);
        }
        live_.clear();
        claims_.clear();
        if (settings_.flushDestroyedActorsOnDemote) {
            scene_.flushDestroyedActors();
        }
    }

    StreamingPromotionResult OpenWorldStreamingSceneChunkResidency::promote(
        const StreamingPromotionRequest& request,
        const StreamingCachedPayload& payload)
    {
        StreamingPromotionResult result;
        if (!isSceneChunkKey(request.key) || request.payload != StreamingPayloadKind::SceneChunk) {
            result.status = StreamingPromotionStatus::UnsupportedPayload;
            result.message = "Promotion request is not a scene chunk.";
            ++diagnostics_.unsupportedOwnershipCount;
            diagnostics_.lastMessage = result.message;
            return result;
        }
        const StreamingSceneChunkDescriptor* descriptor = findDescriptor(request.key);
        if (!descriptor) {
            result.status = StreamingPromotionStatus::UnsupportedPayload;
            result.message = "Scene chunk descriptor was not registered.";
            diagnostics_.lastMessage = result.message;
            return result;
        }
        if (const LiveBinding* existing = findBinding(request.key.stableId)) {
            result.status = StreamingPromotionStatus::Success;
            result.liveToken = existing->token;
            result.liveResources.sceneActors = static_cast<uint32_t>(existing->createdActors.size());
            result.liveResources.sceneComponents = existing->createdComponentCount;
            result.message = "Scene chunk is already live.";
            return result;
        }

        const auto* chunk = std::get_if<StreamingSceneChunkPayload>(&payload);
        if (!chunk || !chunk->scene) {
            result.status = StreamingPromotionStatus::MissingCachedPayload;
            result.message = "Scene chunk cached payload is missing.";
            diagnostics_.lastMessage = result.message;
            return result;
        }

        const SceneSerializationDiagnostics validation =
            validateSerializedScene(*chunk->scene, registry_, SceneSerializationSettings{});
        if (!validation.errors.empty()) {
            result.status = StreamingPromotionStatus::CallbackFailed;
            result.message = validation.errors.front();
            diagnostics_.lastMessage = result.message;
            return result;
        }

        LiveBinding binding;
        binding.token = StreamingRuntimeToken{nextToken_++};
        binding.stableChunkId = request.key.stableId;
        binding.ownership = descriptor->ownership;

        std::map<uint64_t, SceneActorHandle> actorById;
        std::vector<SceneSerializedActorRecord> actors = chunk->scene->actors;
        std::ranges::sort(actors, [](const auto& lhs, const auto& rhs) {
            return lhs.order < rhs.order;
        });

        for (const SceneSerializedActorRecord& actor : actors) {
            const SceneActorHandle existing = findActorByStableId(actor.id);
            if (isValid(existing)) {
                if (descriptor->ownership == StreamingSceneChunkActorOwnership::ChunkOwnedStatic) {
                    ++diagnostics_.duplicateStableIdCount;
                    result.status = StreamingPromotionStatus::CallbackFailed;
                    result.message = "Chunk-owned scene actor stable ID is already live.";
                    releaseBinding(binding);
                    diagnostics_.lastMessage = result.message;
                    return result;
                }
                if (descriptor->ownership == StreamingSceneChunkActorOwnership::Migratory &&
                    actorClaimedByOtherChunk(actor.id, request.key.stableId)) {
                    ++diagnostics_.duplicateStableIdCount;
                    result.status = StreamingPromotionStatus::CallbackFailed;
                    result.message = "Migratory scene actor is already claimed by another live chunk.";
                    releaseBinding(binding);
                    diagnostics_.lastMessage = result.message;
                    return result;
                }
                if (descriptor->ownership == StreamingSceneChunkActorOwnership::Migratory) {
                    claimMigratoryActor(actor.id, request.key.stableId);
                }
                actorById[actor.id.value] = existing;
                continue;
            }

            if (descriptor->ownership == StreamingSceneChunkActorOwnership::Global) {
                ++diagnostics_.unsupportedOwnershipCount;
                result.status = StreamingPromotionStatus::CallbackFailed;
                result.message = "Global scene chunk actor is not already live.";
                releaseBinding(binding);
                diagnostics_.lastMessage = result.message;
                return result;
            }

            const SceneActorHandle handle = scene_.createActor(actor.id);
            if (!scene_.setLocalTransform(handle, actor.localTransform)) {
                result.status = StreamingPromotionStatus::CallbackFailed;
                result.message = "Failed to restore scene chunk actor transform.";
                releaseBinding(binding);
                diagnostics_.lastMessage = result.message;
                return result;
            }
            if (descriptor->ownership == StreamingSceneChunkActorOwnership::Migratory) {
                claimMigratoryActor(actor.id, request.key.stableId);
            }
            binding.createdActors.push_back(handle);
            actorById[actor.id.value] = handle;
        }

        for (const SceneSerializedActorRecord& actor : actors) {
            if (!actor.parent) {
                continue;
            }
            const auto childIt = actorById.find(actor.id.value);
            const auto parentIt = actorById.find(actor.parent->value);
            if (childIt == actorById.end() || parentIt == actorById.end()) {
                ++diagnostics_.invalidParentCount;
                result.status = StreamingPromotionStatus::CallbackFailed;
                result.message = "Scene chunk actor references a missing parent.";
                releaseBinding(binding);
                diagnostics_.lastMessage = result.message;
                return result;
            }
            if (!containsActor(binding.createdActors, childIt->second)) {
                continue;
            }
            if (scene_.attachChild(childIt->second, parentIt->second, false) !=
                SceneTransformUpdateResult::Success) {
                ++diagnostics_.invalidParentCount;
                result.status = StreamingPromotionStatus::CallbackFailed;
                result.message = "Failed to restore scene chunk parent link.";
                releaseBinding(binding);
                diagnostics_.lastMessage = result.message;
                return result;
            }
        }

        for (const SceneSerializedComponentRecord& component : chunk->scene->components) {
            const auto ownerIt = actorById.find(component.owner.value);
            if (ownerIt == actorById.end() || !isValid(component.type)) {
                ++diagnostics_.invalidComponentCount;
                result.status = StreamingPromotionStatus::CallbackFailed;
                result.message = "Scene chunk component references an invalid owner or type.";
                releaseBinding(binding);
                diagnostics_.lastMessage = result.message;
                return result;
            }
            if (!containsActor(binding.createdActors, ownerIt->second)) {
                continue;
            }
            if (!isValid(scene_.attachComponent(ownerIt->second, component.type))) {
                ++diagnostics_.invalidComponentCount;
                result.status = StreamingPromotionStatus::CallbackFailed;
                result.message = "Failed to restore scene chunk component.";
                releaseBinding(binding);
                diagnostics_.lastMessage = result.message;
                return result;
            }
            ++binding.createdComponentCount;
        }

        result.status = StreamingPromotionStatus::Success;
        result.liveToken = binding.token;
        result.liveResources.sceneActors = static_cast<uint32_t>(binding.createdActors.size());
        result.liveResources.sceneComponents = binding.createdComponentCount;
        result.message = "Scene chunk promoted.";
        diagnostics_.actorsCreated += result.liveResources.sceneActors;
        diagnostics_.componentsCreated += result.liveResources.sceneComponents;
        ++diagnostics_.promotedChunkCount;
        diagnostics_.lastMessage = result.message;
        live_.push_back(std::move(binding));
        return result;
    }

    StreamingPromotionResult OpenWorldStreamingSceneChunkResidency::demote(
        const StreamingDemotionRequest&,
        StreamingRuntimeToken token)
    {
        StreamingPromotionResult result;
        LiveBinding* binding = findBinding(token);
        if (!binding) {
            result.status = StreamingPromotionStatus::Stale;
            result.message = "Scene chunk binding is no longer live.";
            diagnostics_.lastMessage = result.message;
            return result;
        }

        const uint32_t actorCount = static_cast<uint32_t>(binding->createdActors.size());
        releaseBinding(*binding);
        live_.erase(std::remove_if(live_.begin(), live_.end(), [token](const LiveBinding& live) {
            return live.token.value == token.value;
        }), live_.end());
        if (settings_.flushDestroyedActorsOnDemote) {
            scene_.flushDestroyedActors();
        }

        result.status = StreamingPromotionStatus::Success;
        result.message = "Scene chunk demoted.";
        diagnostics_.actorsDestroyed += actorCount;
        ++diagnostics_.demotedChunkCount;
        diagnostics_.lastMessage = result.message;
        return result;
    }

    const StreamingSceneChunkDescriptor* OpenWorldStreamingSceneChunkResidency::findDescriptor(
        const StreamingChunkKey& key) const
    {
        for (const StreamingSceneChunkDescriptor& descriptor : descriptors_) {
            if (descriptor.stableChunkId == key.stableId) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    SceneActorHandle OpenWorldStreamingSceneChunkResidency::findActorByStableId(SceneObjectId id) const
    {
        SceneActorHandle result;
        scene_.forEachActor([&](SceneActorHandle actor) {
            if (isValid(result)) {
                return;
            }
            const std::optional<SceneObjectId> stable = scene_.stableId(actor);
            if (stable && *stable == id) {
                result = actor;
            }
        });
        return result;
    }

    OpenWorldStreamingSceneChunkResidency::LiveBinding*
        OpenWorldStreamingSceneChunkResidency::findBinding(StreamingRuntimeToken token)
    {
        for (LiveBinding& binding : live_) {
            if (binding.token.value == token.value) {
                return &binding;
            }
        }
        return nullptr;
    }

    const OpenWorldStreamingSceneChunkResidency::LiveBinding*
        OpenWorldStreamingSceneChunkResidency::findBinding(const std::string& stableChunkId) const
    {
        for (const LiveBinding& binding : live_) {
            if (binding.stableChunkId == stableChunkId) {
                return &binding;
            }
        }
        return nullptr;
    }

    bool OpenWorldStreamingSceneChunkResidency::actorClaimedByOtherChunk(
        SceneObjectId id,
        const std::string& stableChunkId) const
    {
        for (const LiveActorClaim& claim : claims_) {
            if (claim.id == id && claim.stableChunkId != stableChunkId) {
                return true;
            }
        }
        return false;
    }

    void OpenWorldStreamingSceneChunkResidency::releaseBinding(LiveBinding& binding)
    {
        for (auto it = binding.createdActors.rbegin(); it != binding.createdActors.rend(); ++it) {
            scene_.destroyActor(*it);
        }
        claims_.erase(std::remove_if(claims_.begin(), claims_.end(), [&](const LiveActorClaim& claim) {
            return claim.stableChunkId == binding.stableChunkId;
        }), claims_.end());
        binding.createdActors.clear();
        binding.createdComponentCount = 0;
    }

    StreamingSceneChunkBuildResult makeSceneChunkStreamingRecord(
        const StreamingSceneChunkDescriptor& descriptor,
        const std::filesystem::path& sceneBinaryPath)
    {
        StreamingSceneChunkBuildResult result;
        if (descriptor.stableChunkId.empty() || sceneBinaryPath.empty() || !isValid(descriptor.bounds)) {
            result.message = "Invalid scene chunk streaming descriptor.";
            return result;
        }

        result.record = makeSceneStreamingManifestRecord(
            descriptor.stableChunkId,
            descriptor.bounds,
            descriptor.sourceHash,
            descriptor.settingsHash,
            descriptor.estimatedBytes,
            StreamingPayloadKind::SceneChunk,
            descriptor.debugName);
        result.readDescriptor.key = result.record.key;
        result.readDescriptor.payload = StreamingPayloadKind::SceneChunk;
        result.readDescriptor.manifestRecordHash = hashStreamingManifestRecord(result.record);
        result.readDescriptor.descriptor = sceneChunkBinaryReadDescriptor(
            sceneBinaryPath,
            descriptor.stableChunkId,
            descriptor.assetDependencies);
        result.success = true;
        return result;
    }

    void addSceneChunkStreamingRecord(
        StreamingChunkManifest& manifest,
        StreamingReadDescriptorTable& descriptors,
        const StreamingSceneChunkBuildResult& chunk)
    {
        if (!chunk.success) {
            return;
        }
        manifest.records.push_back(chunk.record);
        setStreamingReadDescriptor(
            descriptors,
            chunk.readDescriptor.key,
            chunk.readDescriptor.payload,
            chunk.readDescriptor.manifestRecordHash,
            chunk.readDescriptor.descriptor);
    }

    void mergeSceneChunkStreamingDiagnostics(
        OpenWorldStreamingDiagnostics& streaming,
        const StreamingSceneChunkDiagnostics& sceneChunks)
    {
        streaming.sceneChunkManifestCount += sceneChunks.manifestRecordCount;
        streaming.cachedSceneChunkPayloadCount += sceneChunks.cachedPayloadCount;
        streaming.promotedSceneChunkCount += sceneChunks.promotedChunkCount;
        streaming.demotedSceneChunkCount += sceneChunks.demotedChunkCount;
        streaming.sceneChunkActorsCreated += sceneChunks.actorsCreated;
        streaming.sceneChunkComponentsCreated += sceneChunks.componentsCreated;
        streaming.sceneChunkActorsDestroyed += sceneChunks.actorsDestroyed;
        streaming.sceneChunkDuplicateStableIdCount += sceneChunks.duplicateStableIdCount;
        streaming.sceneChunkInvalidParentCount += sceneChunks.invalidParentCount;
        streaming.sceneChunkInvalidComponentCount += sceneChunks.invalidComponentCount;
        streaming.sceneChunkUnsupportedOwnershipCount += sceneChunks.unsupportedOwnershipCount;
        streaming.liveResources.sceneActors += sceneChunks.actorsCreated;
        streaming.liveResources.sceneComponents += sceneChunks.componentsCreated;
    }

    StreamingPromotionCallbacks makeSceneChunkStreamingPromotionCallbacks(
        OpenWorldStreamingSceneChunkResidency& residency)
    {
        StreamingPromotionCallbacks callbacks;
        callbacks.promote = [&](const StreamingPromotionRequest& request, const StreamingCachedPayload& payload) {
            return residency.promote(request, payload);
        };
        callbacks.demote = [&](const StreamingDemotionRequest& request, StreamingRuntimeToken token) {
            return residency.demote(request, token);
        };
        return callbacks;
    }
}
