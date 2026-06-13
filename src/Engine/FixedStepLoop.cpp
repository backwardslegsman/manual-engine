#include "Engine/FixedStepLoop.hpp"

#include <algorithm>

namespace Engine {
    FixedStepLoop::FixedStepLoop(float fixedDeltaSeconds, float maxFrameDeltaSeconds)
        : previousFrameTime_(Clock::now()),
          fixedDeltaSeconds_(fixedDeltaSeconds > 0.0f ? fixedDeltaSeconds : 1.0f / 60.0f),
          maxFrameDeltaSeconds_(maxFrameDeltaSeconds > 0.0f ? maxFrameDeltaSeconds : 0.25f)
    {
    }

    void FixedStepLoop::beginFrame()
    {
        const Clock::time_point currentFrameTime = Clock::now();
        const std::chrono::duration<float> delta = currentFrameTime - previousFrameTime_;
        previousFrameTime_ = currentFrameTime;

        frameDeltaSeconds_ = std::clamp(delta.count(), 0.0f, maxFrameDeltaSeconds_);
        elapsedSeconds_ += frameDeltaSeconds_;
        accumulatedSeconds_ += frameDeltaSeconds_;
    }

    bool FixedStepLoop::shouldRunFixedUpdate() const
    {
        return accumulatedSeconds_ >= fixedDeltaSeconds_;
    }

    void FixedStepLoop::consumeFixedUpdate()
    {
        if (accumulatedSeconds_ >= fixedDeltaSeconds_) {
            accumulatedSeconds_ -= fixedDeltaSeconds_;
        }
    }

    float FixedStepLoop::fixedDeltaSeconds() const
    {
        return fixedDeltaSeconds_;
    }

    float FixedStepLoop::frameDeltaSeconds() const
    {
        return frameDeltaSeconds_;
    }

    float FixedStepLoop::elapsedSeconds() const
    {
        return elapsedSeconds_;
    }
}
