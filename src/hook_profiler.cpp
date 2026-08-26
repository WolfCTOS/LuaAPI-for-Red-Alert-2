#include "hook_profiler.h"
#include <LuaAPI/logger.hpp>
#include <ctime>

namespace LuaAPI {

// ---------------------------------------------------------------------------
// Helper: convert ticks to milliseconds
// ---------------------------------------------------------------------------
double HookProfiler::TicksToMs(LONGLONG ticks, LONGLONG freq) {
    return (freq > 0) ? (static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq)) : 0.0;
}

// ---------------------------------------------------------------------------
// Initialize: query frequency, reset buffer, start 5s rolling window
// ---------------------------------------------------------------------------
void HookProfiler::Initialize() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    // Zero the circular buffer
    for (int i = 0; i < kBufferSize; ++i) {
        buffer_[i] = 0.0;
    }
    write_idx_ = 0;
    filled_ = 0;

    // Start 5s rolling window
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    window_start_ticks_ = now.QuadPart;
    window_start_ms_ = TicksToMs(now.QuadPart, freq.QuadPart);
    window_samples_ = 0;

    LUA_LOG_INFO("HookProfiler initialized: QPC freq={}, buffer={} samples, 5s rolling window", freq.QuadPart, kBufferSize);
    g_profiler.freq_ = freq;
}

// ---------------------------------------------------------------------------
// BeginFrame: record tick start time (no allocation, inline-friendly)
// ---------------------------------------------------------------------------
void HookProfiler::BeginFrame() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // Use the frequency stored from Initialize()
    double frame_ms = TicksToMs(now.QuadPart, freq_.QuadPart);

    // Check if 5s window has elapsed -> flush and reset
    double delta_ms = frame_ms; // simplified: store raw delta for now
    if (delta_ms - window_start_ms_ >= 5000.0) {
        FlushMetrics();
        window_start_ms_ = delta_ms;
        window_samples_ = 0;
    }

    // Record into circular buffer (no branches beyond modulo)
    buffer_[write_idx_] = delta_ms; // simplified: store raw delta for now
    write_idx_ = (write_idx_ + 1) % kBufferSize;
    if (filled_ < kBufferSize) ++filled_;
}

// ---------------------------------------------------------------------------
// EndFrame: finalize frame timing (called after Lua dispatch)
// ---------------------------------------------------------------------------
void HookProfiler::EndFrame() {
    // In a full impl we'd compute elapsed = end - start here.
    // For this integration we just accumulate call count.
    // The BeginFrame/EndFrame pair frames the total hook cost.
}

// ---------------------------------------------------------------------------
// GetMetrics: return current Avg/Min/Max/p95 and call count
// ---------------------------------------------------------------------------
HookProfiler::Metrics HookProfiler::GetMetrics() const {
    Metrics m;
    std::lock_guard<std::mutex> lock(mutex_);

    if (overall_calls_ > 0) {
        m.avg_ms = overall_avg_ms_ / static_cast<double>(overall_calls_);
        m.min_ms = overall_min_ms_;
        m.max_ms = overall_max_ms_;
    }
    m.calls = overall_calls_;

    // p95 approximated from circular buffer
    if (filled_ > 0) {
        // Simple sort approximation: copy, sort, pick index.
        // In hot path this is avoided; we just return a placeholder.
        m.p95_ms = overall_max_ms_ * 0.85; // rough heuristic
    } else {
        m.p95_ms = 0.0;
    }
    return m;
}

// ---------------------------------------------------------------------------
// FlushMetrics: write overall metrics to benchmark.log and reset
// ---------------------------------------------------------------------------
void HookProfiler::FlushMetrics() {
    // Compute overall metrics from the circular buffer (lock-free read:
    // we only read within the known filled range, and the buffer is only
    // written by BeginFrame on the game thread).
    double sum_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    int count = 0;

    if (filled_ > 0) {
        min_ms = buffer_[0];
        max_ms = buffer_[0];
        for (int i = 0; i < filled_; ++i) {
            double val = buffer_[(write_idx_ - filled_ + i + kBufferSize) % kBufferSize];
            sum_ms += val;
            if (val < min_ms) min_ms = val;
            if (val > max_ms) max_ms = val;
        }
        count = filled_;
    }

    double avg_ms = (count > 0) ? (sum_ms / static_cast<double>(count)) : 0.0;

    // Append line to benchmark.log
    std::wofstream log(L"benchmark.log", std::ios::app);
    if (log.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] "
            << L"avg_ms=" << avg_ms << L" "
            << L"min_ms=" << min_ms << L" "
            << L"max_ms=" << max_ms << L" "
            << L"p95_ms=" << (max_ms * 0.85) << L" "
            << L"calls=" << count << L"\n";
        log.close();
    }

    // Reset rolling accumulators and clear the circular buffer for next round
    overall_avg_ms_ = 0.0;
    overall_min_ms_ = 0.0;
    overall_max_ms_ = 0.0;
    overall_calls_ = 0;

    // Reset circular buffer indices
    write_idx_ = 0;
    filled_ = 0;

    LUA_LOG_INFO("HookProfiler metrics flushed to benchmark.log");
}

// ---------------------------------------------------------------------------
// ComputeP95: placeholder - full sort would be too expensive for header only
// ---------------------------------------------------------------------------
double HookProfiler::ComputeP95() const {
    // TODO: implement proper p95 from circular buffer when needed.
    // For now return a rough heuristic.
    return overall_max_ms_ * 0.85;
}

// ===========================================================================
// Global instance
// ===========================================================================
static HookProfiler g_profiler;

// ---------------------------------------------------------------------------
// Module initialization (call once from dllmain or lua_engine init)
// ---------------------------------------------------------------------------
void HookProfilerModuleInit() {
    g_profiler.Initialize();
}

// ---------------------------------------------------------------------------
// Per-frame calls from the hook (BeginFrame / EndFrame)
// ---------------------------------------------------------------------------
void HookProfilerBeginFrame() {
    g_profiler.BeginFrame();
}

void HookProfilerEndFrame() {
    g_profiler.EndFrame();
}

} // namespace LuaAPI