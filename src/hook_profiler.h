#pragma once

#include <windows.h>
#include <vector>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <chrono>

namespace LuaAPI {

class HookProfiler {
public:
    HookProfiler() = default;

    // Call once at startup. Resets metrics and starts the 5s rolling window.
    void Initialize();

    // Call at the very start of Hooked_MainLoop before the original loop.
    // No allocations; just records a sample if the window has elapsed.
    void BeginFrame();

    // Call at the very end of Hooked_MainLoop after Lua dispatch.
    // No allocations; just records a sample if the window has elapsed.
    void EndFrame();

    // Query current metrics (thread-safe; cheap).
    struct Metrics {
        double avg_ms = 0.0;
        double min_ms = 0.0;
        double max_ms = 0.0;
        double p95_ms = 0.0;
        int64_t calls = 0;
    };
    Metrics GetMetrics() const;

    // Flush metrics to benchmark.log and reset rolling window.
    void FlushMetrics();

private:
    // Circular buffer of frame times (ms). 2048 samples ~ 85 seconds at 60 FPS.
    static constexpr int kBufferSize = 2048;
    double buffer_[kBufferSize] = { 0.0 };
    int write_idx_ = 0;
    int filled_ = 0;

    // QPC frequency (cached for conversions). Set once in Initialize().
    LARGE_INTEGER freq_ = { 0 };

    // 5-second rolling window tracking
    INT64 window_start_ticks_ = 0;
    double window_start_ms_ = 0.0;
    int window_samples_ = 0;

    // Overall metrics (updated on flush)
    mutable std::mutex mutex_;
    double overall_avg_ms_ = 0.0;
    double overall_min_ms_ = 0.0;
    double overall_max_ms_ = 0.0;
    int64_t overall_calls_ = 0;

    // Helper: compute p95 from the circular buffer (sorted approximation).
    double ComputeP95() const;

    // Helper: convert ticks to ms.
    static double TicksToMs(LARGE_INTEGER ticks, LARGE_INTEGER freq);
};

} // namespace LuaAPI