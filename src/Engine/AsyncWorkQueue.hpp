#pragma once

#include <any>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Engine {
    struct AsyncJobHandle {
        uint64_t id = UINT64_MAX;
    };

    inline bool operator==(AsyncJobHandle lhs, AsyncJobHandle rhs)
    {
        return lhs.id == rhs.id;
    }

    struct AsyncCompletedJob {
        AsyncJobHandle handle;
        std::any result;
        bool cancelled = false;
        std::string label;
    };

    // Small worker queue for CPU-only generated data. Jobs must not touch live
    // Scene, Renderer/bgfx, TerrainDataset, NavigationSystem, or physics state.
    // Return plain data and commit it on the main thread.
    class AsyncWorkQueue {
    public:
        using Job = std::function<std::any(std::stop_token)>;

        explicit AsyncWorkQueue(uint32_t workerCount = defaultWorkerCount());
        ~AsyncWorkQueue();

        AsyncWorkQueue(const AsyncWorkQueue&) = delete;
        AsyncWorkQueue& operator=(const AsyncWorkQueue&) = delete;
        AsyncWorkQueue(AsyncWorkQueue&&) = delete;
        AsyncWorkQueue& operator=(AsyncWorkQueue&&) = delete;

        AsyncJobHandle submit(std::string label, Job job);
        void cancel(AsyncJobHandle handle);
        std::vector<AsyncCompletedJob> pollCompleted();
        void shutdown();
        void restart(uint32_t workerCount = defaultWorkerCount());

        uint32_t pendingCount() const;
        uint32_t workerCount() const;

        static uint32_t defaultWorkerCount();

    private:
        struct QueuedJob {
            AsyncJobHandle handle;
            std::string label;
            Job job;
        };

        void workerMain(std::stop_token stopToken);
        bool isCancelledLocked(AsyncJobHandle handle) const;

        mutable std::mutex mutex_;
        std::condition_variable_any wakeWorkers_;
        std::queue<QueuedJob> pending_;
        std::vector<AsyncCompletedJob> completed_;
        std::unordered_set<uint64_t> cancelled_;
        std::vector<std::jthread> workers_;
        uint64_t nextJobId_ = 1;
        bool shuttingDown_ = false;
    };
}
