#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace Engine {
    struct FrameBudgetSettings {
        float maxMainThreadWorkMs = 2.0f;
        bool enabled = true;
    };

    // Categories are diagnostic and scheduling labels for cooperative
    // main-thread work. They do not imply thread ownership by themselves.
    enum class BudgetCategory : uint32_t {
        StreamingCommit = 0,
        NavigationCommit,
        DerivedRebuild,
        Debug,
        General,
        Count,
    };

    enum class BudgetPriority : uint32_t {
        Critical = 0,
        High,
        Normal,
        Low,
        Count,
    };

    constexpr uint32_t BudgetCategoryCount = static_cast<uint32_t>(BudgetCategory::Count);
    constexpr uint32_t BudgetPriorityCount = static_cast<uint32_t>(BudgetPriority::Count);

    struct FrameBudgetStats {
        float budgetMs = 0.0f;
        float usedMs = 0.0f;
        float overrunMs = 0.0f;
        uint32_t itemsRun = 0;
        uint32_t itemsDeferred = 0;
        std::array<float, BudgetCategoryCount> perCategoryMs{};
        float slowestItemMs = 0.0f;
        BudgetCategory slowestItemCategory = BudgetCategory::General;
        std::string slowestItemLabel;
    };

    class FrameBudget {
    public:
        void beginFrame(FrameBudgetSettings settings = {});
        bool hasTime() const;

        // Runs one cooperative work item and charges its elapsed time to the
        // current frame budget. Critical items are allowed to overrun and are
        // reported through stats; normal work is skipped once the budget is
        // exhausted.
        bool run(
            BudgetCategory category,
            std::string_view label,
            BudgetPriority priority,
            const std::function<void()>& callback);
        bool run(BudgetCategory category, std::string_view label, const std::function<void()>& callback);

        void addDeferred(uint32_t count = 1);
        const FrameBudgetStats& stats() const;
        const FrameBudgetSettings& settings() const;

    private:
        using Clock = std::chrono::steady_clock;

        FrameBudgetSettings settings_;
        FrameBudgetStats stats_;
        Clock::time_point frameStart_{};
    };

    struct MainThreadWorkItem {
        BudgetCategory category = BudgetCategory::General;
        BudgetPriority priority = BudgetPriority::Normal;
        std::string label;
        std::function<void()> callback;
    };

    class MainThreadWorkQueue {
    public:
        // The queue is intentionally main-thread only. Worker jobs should
        // return plain data; the App/owning service enqueues the live mutation
        // phase here.
        void enqueue(MainThreadWorkItem item);
        void drain(FrameBudget& budget);
        void clear();
        uint32_t pendingCount(std::optional<BudgetCategory> category = std::nullopt) const;
        const FrameBudgetStats& stats() const;

    private:
        std::array<std::deque<MainThreadWorkItem>, BudgetPriorityCount> queues_;
        FrameBudgetStats lastDrainStats_{};
    };
}
