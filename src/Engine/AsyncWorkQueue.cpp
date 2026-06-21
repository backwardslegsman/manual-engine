#include "Engine/AsyncWorkQueue.hpp"

#include <algorithm>
#include <exception>

namespace Engine {
    AsyncWorkQueue::AsyncWorkQueue(uint32_t workerCount)
    {
        const uint32_t count = std::max(workerCount, 1u);
        workers_.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            workers_.emplace_back([this](std::stop_token stopToken) {
                workerMain(stopToken);
            });
        }
    }

    AsyncWorkQueue::~AsyncWorkQueue()
    {
        shutdown();
    }

    AsyncJobHandle AsyncWorkQueue::submit(std::string label, Job job)
    {
        if (!job) {
            return {};
        }

        {
            std::lock_guard lock(mutex_);
            if (shuttingDown_) {
                return {};
            }

            const AsyncJobHandle handle{nextJobId_++};
            pending_.push(QueuedJob{handle, std::move(label), std::move(job)});
            wakeWorkers_.notify_one();
            return handle;
        }
    }

    void AsyncWorkQueue::cancel(AsyncJobHandle handle)
    {
        if (handle.id == UINT64_MAX) {
            return;
        }

        std::lock_guard lock(mutex_);
        cancelled_.insert(handle.id);
        wakeWorkers_.notify_all();
    }

    std::vector<AsyncCompletedJob> AsyncWorkQueue::pollCompleted()
    {
        std::lock_guard lock(mutex_);
        std::vector<AsyncCompletedJob> result;
        result.swap(completed_);
        for (const AsyncCompletedJob& completed : result) {
            cancelled_.erase(completed.handle.id);
        }
        return result;
    }

    void AsyncWorkQueue::shutdown()
    {
        {
            std::lock_guard lock(mutex_);
            if (shuttingDown_) {
                return;
            }
            shuttingDown_ = true;
        }

        wakeWorkers_.notify_all();
        for (std::jthread& worker : workers_) {
            worker.request_stop();
        }
        workers_.clear();

        std::lock_guard lock(mutex_);
        while (!pending_.empty()) {
            pending_.pop();
        }
        completed_.clear();
        cancelled_.clear();
    }

    void AsyncWorkQueue::restart(uint32_t workerCount)
    {
        shutdown();
        {
            std::lock_guard lock(mutex_);
            shuttingDown_ = false;
        }
        const uint32_t count = std::max(workerCount, 1u);
        workers_.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            workers_.emplace_back([this](std::stop_token stopToken) {
                workerMain(stopToken);
            });
        }
    }

    uint32_t AsyncWorkQueue::pendingCount() const
    {
        std::lock_guard lock(mutex_);
        return static_cast<uint32_t>(pending_.size());
    }

    uint32_t AsyncWorkQueue::workerCount() const
    {
        return static_cast<uint32_t>(workers_.size());
    }

    uint32_t AsyncWorkQueue::defaultWorkerCount()
    {
        const uint32_t hardware = std::thread::hardware_concurrency();
        return hardware > 1 ? hardware - 1 : 1;
    }

    void AsyncWorkQueue::workerMain(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested()) {
            QueuedJob queued;
            {
                std::unique_lock lock(mutex_);
                wakeWorkers_.wait(lock, stopToken, [&] {
                    return shuttingDown_ || !pending_.empty();
                });
                if (stopToken.stop_requested() || shuttingDown_) {
                    return;
                }
                queued = std::move(pending_.front());
                pending_.pop();
            }

            AsyncCompletedJob completed;
            completed.handle = queued.handle;
            completed.label = std::move(queued.label);
            completed.cancelled = stopToken.stop_requested();
            if (!completed.cancelled) {
                try {
                    completed.result = queued.job(stopToken);
                } catch (...) {
                    completed.result = std::current_exception();
                }
            }

            {
                std::lock_guard lock(mutex_);
                completed.cancelled = completed.cancelled || isCancelledLocked(completed.handle);
                completed_.push_back(std::move(completed));
            }
        }
    }

    bool AsyncWorkQueue::isCancelledLocked(AsyncJobHandle handle) const
    {
        return cancelled_.contains(handle.id);
    }
}
