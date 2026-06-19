#include "Engine/AnimatedModelAsync.hpp"

#include <any>
#include <chrono>
#include <utility>

#include "Assets/Assimp/Importer.hpp"

namespace {
    template <typename Function>
    float measureMilliseconds(Function&& function)
    {
        const auto start = std::chrono::steady_clock::now();
        function();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(end - start).count();
    }

}

namespace Engine {
    AsyncJobHandle enqueueAnimatedModelCacheRead(
        AsyncWorkQueue& queue,
        AnimatedModelCacheManifest manifest)
    {
        return queue.submit("animated model cache read", [manifest = std::move(manifest)](std::stop_token stopToken) -> std::any {
            AnimatedModelCacheReadJobResult result;
            if (stopToken.stop_requested()) {
                result.result.status = AnimatedModelCacheStatus::Cancelled;
                result.result.message = "Animated model cache read cancelled.";
                return result;
            }

            result.readMs = measureMilliseconds([&]() {
                result.result = AnimatedModelCache::read(manifest);
            });
            if (stopToken.stop_requested()) {
                result.result.status = AnimatedModelCacheStatus::Cancelled;
                result.result.message = "Animated model cache read cancelled.";
                result.result.payload.reset();
            }
            return result;
        });
    }

    AsyncJobHandle enqueueAnimatedModelCacheWrite(
        AsyncWorkQueue& queue,
        AnimatedModelCacheManifest manifest,
        AnimatedModelCachePayload payload)
    {
        return queue.submit(
            "animated model cache write",
            [manifest = std::move(manifest), payload = std::move(payload)](std::stop_token stopToken) mutable -> std::any {
                AnimatedModelCacheWriteJobResult result;
                if (stopToken.stop_requested()) {
                    result.result.status = AnimatedModelCacheStatus::Cancelled;
                    result.result.message = "Animated model cache write cancelled.";
                    return result;
                }

                result.writeMs = measureMilliseconds([&]() {
                    result.result = AnimatedModelCache::write(manifest, payload);
                });
                if (stopToken.stop_requested()) {
                    result.result.status = AnimatedModelCacheStatus::Cancelled;
                    result.result.message = "Animated model cache write cancelled.";
                }
                return result;
            });
    }

    AsyncJobHandle enqueueAnimatedModelImportAndPreparePayload(
        AsyncWorkQueue& queue,
        std::filesystem::path path)
    {
        return queue.submit(
            "animated model import",
            [path = std::move(path)](std::stop_token stopToken) mutable -> std::any {
                AnimatedModelImportJobResult result;
                result.path = path;
                if (stopToken.stop_requested()) {
                    result.cancelled = true;
                    result.message = "Animated model import cancelled.";
                    return result;
                }

                Assets::Assimp::ImportedScene imported;
                result.importMs = measureMilliseconds([&]() {
                    imported = Assets::Assimp::importScene(path);
                });
                if (stopToken.stop_requested()) {
                    result.cancelled = true;
                    result.message = "Animated model import cancelled.";
                    return result;
                }
                result.payload.scene = std::move(imported);
                if (!result.payload.scene.success) {
                    result.message = result.payload.scene.error;
                    return result;
                }
                if (!Assets::Assimp::containsSkeletalOrAnimationData(result.payload.scene)) {
                    result.message = "Imported asset is not an animated model.";
                    return result;
                }

                result.success = true;
                result.message = "Animated model import completed.";
                return result;
            });
    }
}
