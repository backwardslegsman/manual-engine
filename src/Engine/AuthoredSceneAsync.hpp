#pragma once

#include <filesystem>
#include <string>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/AuthoredScene.hpp"

namespace Engine {
    // Worker-job results are plain data only. They must be consumed on the main
    // thread before touching AssetCache, Renderer handles, or live scene state.
    struct AuthoredSceneCacheReadJobResult {
        AuthoredSceneCacheReadResult result;
        float readMs = 0.0f;
    };

    struct AuthoredSceneCacheWriteJobResult {
        AuthoredSceneCacheWriteResult result;
        float writeMs = 0.0f;
    };

    struct AuthoredSceneImportJobResult {
        bool success = false;
        bool cancelled = false;
        std::string message;
        std::filesystem::path path;
        AuthoredSceneCachePayload payload;
        float importMs = 0.0f;
    };

    // Enqueues derived-cache I/O and source import/partition work. These jobs
    // are worker-safe and do not create renderer resources.
    AsyncJobHandle enqueueAuthoredSceneCacheRead(
        AsyncWorkQueue& queue,
        AuthoredSceneCacheManifest manifest);

    AsyncJobHandle enqueueAuthoredSceneCacheWrite(
        AsyncWorkQueue& queue,
        AuthoredSceneCacheManifest manifest,
        AuthoredSceneCachePayload payload);

    AsyncJobHandle enqueueAuthoredSceneImportAndPartition(
        AsyncWorkQueue& queue,
        std::filesystem::path path,
        AuthoredScenePartitionSettings partitionSettings);
}
