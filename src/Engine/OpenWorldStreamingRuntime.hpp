#pragma once

#include <filesystem>
#include <string>

#include "Engine/OpenWorldStreaming.hpp"
#include "Engine/OpenWorldStreamingBake.hpp"

namespace Engine {
    inline constexpr const char* OpenWorldStreamingRuntimeVersion =
        "open_world_streaming_runtime_s8_v1";

    enum class OpenWorldStreamingBuildStatus {
        ReusedSavedBuild,
        RebuiltSavedBuild,
        Failed,
    };

    struct OpenWorldStreamingRuntimeSettings {
        std::filesystem::path savedBuildManifestPath =
            "generated/open_world_streaming/modern_default/manifest.yaml";
        OpenWorldStreamingBakeSettings bake;
        StreamingHaloPlannerSettings planner = defaultStreamingHaloPlannerSettings();
        StreamingCacheHaloSettings cache;
        StreamingGenerationSettings generation;
        StreamingPromotionSettings promotion;
        bool validatePayloadFiles = true;
        bool rebuildWhenStale = true;
    };

    struct OpenWorldStreamingBuildResult {
        OpenWorldStreamingBuildStatus status = OpenWorldStreamingBuildStatus::Failed;
        bool success = false;
        bool reusedSavedBuild = false;
        bool rebuilt = false;
        std::string fingerprint;
        std::string sourceHash;
        std::string reason;
        std::string message;
        OpenWorldStreamingBakeDiagnostics bakeDiagnostics;
    };

    struct OpenWorldStreamingRuntimeSnapshot {
        StreamingHaloPlan lastPlan;
        StreamingCacheHaloSnapshot cache;
        StreamingGenerationHaloSnapshot generation;
        StreamingLiveHaloSnapshot live;
        OpenWorldStreamingDiagnostics diagnostics;
        OpenWorldStreamingBuildResult build;
    };

    class OpenWorldStreamingRuntime {
    public:
        OpenWorldStreamingRuntime() = default;
        explicit OpenWorldStreamingRuntime(OpenWorldStreamingRuntimeSettings settings);

        [[nodiscard]] const OpenWorldStreamingBuildResult& initializeFromSavedBuild();
        void update(
            const StreamingFocusInput& focus,
            AsyncWorkQueue& asyncQueue,
            MainThreadWorkQueue& mainThreadQueue,
            StreamingPromotionCallbacks callbacks);
        void update(
            const glm::vec3& focus,
            AsyncWorkQueue& asyncQueue,
            MainThreadWorkQueue& mainThreadQueue,
            StreamingPromotionCallbacks callbacks);
        void pollCompleted(const std::vector<AsyncCompletedJob>& completedJobs);
        void shutdown();

        [[nodiscard]] bool initialized() const;
        [[nodiscard]] const StreamingChunkManifest& manifest() const;
        [[nodiscard]] const StreamingReadDescriptorTable& readDescriptors() const;
        [[nodiscard]] const StreamingGenerationDescriptorTable& generationDescriptors() const;
        [[nodiscard]] const OpenWorldStreamingBuildResult& buildResult() const;
        [[nodiscard]] OpenWorldStreamingRuntimeSnapshot snapshot() const;
        [[nodiscard]] OpenWorldStreamingDiagnostics diagnostics() const;

    private:
        void rebuildMergedDiagnostics();
        [[nodiscard]] std::vector<StreamingChunkResidencyInput> currentResidency() const;

        OpenWorldStreamingRuntimeSettings settings_;
        StreamingChunkManifest manifest_;
        StreamingReadDescriptorTable readDescriptors_;
        StreamingGenerationDescriptorTable generationDescriptors_;
        OpenWorldStreamingCacheHalo cache_;
        OpenWorldStreamingDerivedGenerationHalo generation_;
        OpenWorldStreamingLiveHalo live_;
        StreamingHaloPlan lastPlan_;
        OpenWorldStreamingDiagnostics diagnostics_;
        OpenWorldStreamingBuildResult build_;
        bool initialized_ = false;
    };

    [[nodiscard]] std::string openWorldStreamingRuntimeFingerprint(
        const OpenWorldStreamingRuntimeSettings& settings);
}
