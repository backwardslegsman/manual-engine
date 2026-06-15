#include "Engine/FrameBudget.hpp"

#include <algorithm>

namespace Engine {
    namespace {
        uint32_t priorityIndex(BudgetPriority priority)
        {
            return std::min(static_cast<uint32_t>(priority), BudgetPriorityCount - 1);
        }

        uint32_t categoryIndex(BudgetCategory category)
        {
            return std::min(static_cast<uint32_t>(category), BudgetCategoryCount - 1);
        }
    }

    void FrameBudget::beginFrame(FrameBudgetSettings settings)
    {
        settings.maxMainThreadWorkMs = std::max(settings.maxMainThreadWorkMs, 0.0f);
        settings_ = settings;
        stats_ = {};
        stats_.budgetMs = settings_.enabled ? settings_.maxMainThreadWorkMs : 0.0f;
        frameStart_ = Clock::now();
    }

    bool FrameBudget::hasTime() const
    {
        return !settings_.enabled || stats_.usedMs < settings_.maxMainThreadWorkMs;
    }

    bool FrameBudget::run(
        BudgetCategory category,
        std::string_view label,
        BudgetPriority priority,
        const std::function<void()>& callback)
    {
        if (!callback) {
            return true;
        }
        if (settings_.enabled && priority != BudgetPriority::Critical && !hasTime()) {
            ++stats_.itemsDeferred;
            return false;
        }

        const auto start = Clock::now();
        callback();
        const auto end = Clock::now();
        const float elapsedMs = std::chrono::duration<float, std::milli>(end - start).count();
        stats_.usedMs += elapsedMs;
        stats_.perCategoryMs[categoryIndex(category)] += elapsedMs;
        if (elapsedMs > stats_.slowestItemMs) {
            stats_.slowestItemMs = elapsedMs;
            stats_.slowestItemCategory = category;
            stats_.slowestItemLabel = std::string(label);
        }
        ++stats_.itemsRun;
        if (settings_.enabled && stats_.usedMs > settings_.maxMainThreadWorkMs) {
            stats_.overrunMs = stats_.usedMs - settings_.maxMainThreadWorkMs;
        }
        return true;
    }

    bool FrameBudget::run(BudgetCategory category, std::string_view label, const std::function<void()>& callback)
    {
        return run(category, label, BudgetPriority::Normal, callback);
    }

    void FrameBudget::addDeferred(uint32_t count)
    {
        stats_.itemsDeferred += count;
    }

    const FrameBudgetStats& FrameBudget::stats() const
    {
        return stats_;
    }

    const FrameBudgetSettings& FrameBudget::settings() const
    {
        return settings_;
    }

    void MainThreadWorkQueue::enqueue(MainThreadWorkItem item)
    {
        if (!item.callback) {
            return;
        }
        queues_[priorityIndex(item.priority)].push_back(std::move(item));
    }

    void MainThreadWorkQueue::drain(FrameBudget& budget)
    {
        for (std::deque<MainThreadWorkItem>& queue : queues_) {
            while (!queue.empty()) {
                MainThreadWorkItem item = std::move(queue.front());
                queue.pop_front();
                if (!budget.run(item.category, item.label, item.priority, item.callback)) {
                    queue.push_front(std::move(item));
                    const uint32_t pending = pendingCount();
                    budget.addDeferred(pending > 0 ? pending - 1 : 0);
                    lastDrainStats_ = budget.stats();
                    return;
                }
            }
        }
        lastDrainStats_ = budget.stats();
    }

    void MainThreadWorkQueue::clear()
    {
        for (std::deque<MainThreadWorkItem>& queue : queues_) {
            queue.clear();
        }
        lastDrainStats_ = {};
    }

    uint32_t MainThreadWorkQueue::pendingCount(std::optional<BudgetCategory> category) const
    {
        uint32_t count = 0;
        for (const std::deque<MainThreadWorkItem>& queue : queues_) {
            for (const MainThreadWorkItem& item : queue) {
                if (!category || item.category == *category) {
                    ++count;
                }
            }
        }
        return count;
    }

    const FrameBudgetStats& MainThreadWorkQueue::stats() const
    {
        return lastDrainStats_;
    }
}
