#include "Engine/AuthoredSceneAsync.hpp"

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
    AsyncJobHandle enqueueAuthoredSceneCacheRead(
        AsyncWorkQueue& queue,
        AuthoredSceneCacheManifest manifest)
    {
        return queue.submit("authored scene cache read", [manifest = std::move(manifest)](std::stop_token stopToken) -> std::any {
            AuthoredSceneCacheReadJobResult result;
            if (stopToken.stop_requested()) {
                result.result.status = AuthoredSceneCacheStatus::Cancelled;
                result.result.message = "Authored scene cache read cancelled.";
                return result;
            }

            result.readMs = measureMilliseconds([&]() {
                result.result = AuthoredSceneCache::read(manifest);
            });
            if (stopToken.stop_requested()) {
                result.result.status = AuthoredSceneCacheStatus::Cancelled;
                result.result.message = "Authored scene cache read cancelled.";
                result.result.payload.reset();
            }
            return result;
        });
    }

    AsyncJobHandle enqueueAuthoredSceneCacheWrite(
        AsyncWorkQueue& queue,
        AuthoredSceneCacheManifest manifest,
        AuthoredSceneCachePayload payload)
    {
        return queue.submit(
            "authored scene cache write",
            [manifest = std::move(manifest), payload = std::move(payload)](std::stop_token stopToken) mutable -> std::any {
                AuthoredSceneCacheWriteJobResult result;
                if (stopToken.stop_requested()) {
                    result.result.status = AuthoredSceneCacheStatus::Cancelled;
                    result.result.message = "Authored scene cache write cancelled.";
                    return result;
                }

                result.writeMs = measureMilliseconds([&]() {
                    result.result = AuthoredSceneCache::write(manifest, payload);
                });
                if (stopToken.stop_requested()) {
                    result.result.status = AuthoredSceneCacheStatus::Cancelled;
                    result.result.message = "Authored scene cache write cancelled.";
                }
                return result;
            });
    }

    AsyncJobHandle enqueueAuthoredSceneImportAndPartition(
        AsyncWorkQueue& queue,
        std::filesystem::path path,
        AuthoredScenePartitionSettings partitionSettings)
    {
        return queue.submit(
            "authored scene import",
            [path = std::move(path), partitionSettings](std::stop_token stopToken) mutable -> std::any {
                AuthoredSceneImportJobResult result;
                result.path = path;
                if (stopToken.stop_requested()) {
                    result.cancelled = true;
                    result.message = "Authored scene import cancelled.";
                    return result;
                }

                Assets::Assimp::ImportedScene imported;
                result.importMs = measureMilliseconds([&]() {
                    imported = Assets::Assimp::importScene(path);
                });
                if (stopToken.stop_requested()) {
                    result.cancelled = true;
                    result.message = "Authored scene import cancelled.";
                    return result;
                }
                if (!imported.success) {
                    result.success = false;
                    result.message = imported.error;
                    result.payload.scene = std::move(imported);
                    return result;
                }

                result.payload.scene = std::move(imported);
                result.payload.partition = partitionImportedAuthoredScene(result.payload.scene, partitionSettings);
                result.success = true;
                result.message = "Authored scene import and partition completed.";
                return result;
            });
    }
}
