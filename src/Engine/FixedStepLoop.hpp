#pragma once

#include <chrono>

namespace Engine {
    // Fixed-timestep helper for simulation code. The app still owns platform
    // event polling, rendering submission, and presentation.
    class FixedStepLoop {
    public:
        using Clock = std::chrono::steady_clock;

        // fixedDeltaSeconds defaults to 60 Hz. maxFrameDeltaSeconds limits how
        // much stalled wall-clock time can enter the accumulator in one frame.
        explicit FixedStepLoop(float fixedDeltaSeconds = 1.0f / 60.0f, float maxFrameDeltaSeconds = 0.25f);

        // Samples the clock and updates frame/accumulator state once per app frame.
        void beginFrame();

        // Returns true while at least one fixed simulation tick is pending.
        bool shouldRunFixedUpdate() const;

        // Consumes exactly one pending fixed simulation tick.
        void consumeFixedUpdate();

        float fixedDeltaSeconds() const;
        float frameDeltaSeconds() const;
        float elapsedSeconds() const;

    private:
        Clock::time_point previousFrameTime_;
        float fixedDeltaSeconds_ = 1.0f / 60.0f;
        float maxFrameDeltaSeconds_ = 0.25f;
        float accumulatedSeconds_ = 0.0f;
        float frameDeltaSeconds_ = 0.0f;
        float elapsedSeconds_ = 0.0f;
    };
}
