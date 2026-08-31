# 📊 LuaAPI Performance & Overhead Benchmark Report

> **Target:** `gamemd.exe` (Yuri's Revenge 1.001)  
> **Tool:** Intel PresentMon (Hardware ETW Capture / D3D9)  
> **Date:** August 26, 2026  
> **Test Environment:** Windows 10 x64, 60 Hz display refresh, `cnc-ddraw` (D3D9)  
> **Tested Build:** LuaAPI `v1.0.0`, engine revision `2cdb26b`

---

## 🎯 Test Methodology

The benchmark measures the observed runtime performance of LuaAPI and its C++ hooks using **Intel PresentMon** at the DirectX driver level through Event Tracing for Windows (ETW).

The results below describe this specific test environment and workload. They should not be interpreted as a proof that LuaAPI has zero overhead under every possible workload or hardware configuration.

1. **Test Scenario:** Identical 8-player Skirmish map (*Heck Freezes Over*) with active combat against AI.
2. **Warmup Discard:** First 5 seconds of every run are discarded to reduce loading-phase effects.
3. **Alt-Tab Filter:** Single window-switch stalls (>200 ms) are excluded from the gameplay measurement.

---

## 📈 Empirical Benchmark Results

### 1. Comparative Overhead Suite — 65-Second Active Skirmish

| Configuration | Samples | Avg FPS | 1% Low FPS | 95th Percentile Frame Time | Max Frame Time | Observed Avg-FPS Delta |
|:---|---:|---:|---:|---:|---:|---:|
| **1. Vanilla RA2: YR** (Baseline) | 3,203 | **60.05** | **54.40** | 17.56 ms | 22.92 ms | — |
| **2. Clean LuaAPI** (Hook `0x55D360`, 0 Mods) | 3,030 | **60.05** | **55.13** | 17.51 ms | 18.84 ms | **0.00 FPS (0.00%)** |
| **3. Modded LuaAPI** (Active Mods + Events) | 3,112 | **60.04** | **55.11** | 17.31 ms | 18.62 ms | **-0.01 FPS (< 0.02%)** |

The measured average-FPS difference between the vanilla baseline and the tested Modded LuaAPI run was **-0.01 FPS**. Within this benchmark, that difference is very small relative to normal frame-time variation and should not be treated as a universal performance guarantee.

### 2. High-Stress Endurance Run — 5 Minutes / 7 Brutal AIs

| Metric | Measured Value | Interpretation |
|:---|:---:|:---|
| **Total Duration** | **299.87 seconds** | Approximately five minutes of continuous gameplay |
| **Total Recorded Frames** | **17,993 frames** | Approximately 60 FPS average over the run |
| **1% Low FPS** | **56.24 FPS** | High lower-percentile performance during the recorded workload |
| **Minimum Observed FPS** | **47.35 FPS** | Lowest observed instantaneous/measurement-window FPS value |
| **`0xC0000005` Crashes** | **0** | No access-violation crash occurred during this run |

The endurance run demonstrates stability under the tested workload. It does not establish the absence of crashes or memory issues under all possible maps, mods, hardware, or runtime conditions.

---

## 🔍 What the Results Support

1. **Very small observed runtime overhead:**  
   In the comparative test, the Modded LuaAPI configuration measured **0.01 FPS lower average FPS** than the vanilla baseline. This is the observed result for this workload, not a universal upper bound on LuaAPI overhead.

2. **No obvious frame-pacing degradation in this test:**  
   The reported 1% lows and percentile frame-time values did not show a large performance regression between the tested configurations.

3. **Stable five-minute stress run:**  
   The tested endurance run completed approximately 18,000 frames without an observed `0xC0000005` crash.

### What This Benchmark Does Not Prove

This report does **not** independently prove:

- that the MainLoop hook itself costs less than a specific number of microseconds;
- that Lua garbage collection can never cause stuttering;
- that LuaAPI introduces zero overhead on every workload;
- that LuaAPI is crash-free under all engine states;
- that the tested results will reproduce identically on different hardware or configurations.

A direct claim about hook execution cost requires a dedicated high-resolution timing measurement (for example, QPC instrumentation around the hook/trampoline), while GC behavior requires targeted allocation and stress tests.

---

## 🔁 Reproduction

The benchmark tooling is available in:

- `tools/run_benchmark.ps1`
- `tools/benchmark_analyzer.py`

For meaningful comparisons, use the same map, game configuration, hardware, display refresh rate, renderer, and measurement procedure for all configurations.

---

*Report captured via Intel PresentMon ETW CLI. Measurements and conclusions apply to the tested build and workload described above.*
