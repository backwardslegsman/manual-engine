#pragma once

#include <filesystem>
#include <string>

#include "Engine/AnimatedModelCache.hpp"
#include "Engine/AsyncWorkQueue.hpp"

namespace Engine {
    // Worker-job results are plain data only. Commit returned payloads on the
    // main thread before touching AssetCache, Renderer handles, or live model state.
    struct AnimatedModelCacheReadJobResult {
        AnimatedModelCacheReadResult result;
        float readMs = 0.0f;
    };

    struct AnimatedModelCacheWriteJobResult {
        AnimatedModelCacheWriteResult result;
        float writeMs = 0.0f;
    };

    struct AnimatedModelImportJobResult {
        bool success = false;
        bool cancelled = false;
        std::string message;
        std::filesystem::path path;
        AnimatedModelCachePayload payload;
        float importMs = 0.0f;
    };

    // Enqueues derived-cache I/O and source import work. These jobs are
    // worker-safe and do not create renderer resources or mutate AnimatedModel.
    AsyncJobHandle enqueueAnimatedModelCacheRead(
        AsyncWorkQueue& queue,
        AnimatedModelCacheManifest manifest);

    AsyncJobHandle enqueueAnimatedModelCacheWrite(
        AsyncWorkQueue& queue,
        AnimatedModelCacheManifest manifest,
        AnimatedModelCachePayload payload);

    AsyncJobHandle enqueueAnimatedModelImportAndPreparePayload(
        AsyncWorkQueue& queue,
        std::filesystem::path path);
}
