#pragma once

#include <filesystem>
#include <string>

#include "Engine/AnimatedModelCache.hpp"
#include "Engine/AsyncWorkQueue.hpp"

namespace Engine {
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
