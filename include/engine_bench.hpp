#pragma once
// Shared core of the on-board benchmarks (pico/main_pico.cpp, daisy/main_daisy.cpp): the scenarios,
// the synthetic input and the timing loop. Each board supplies its own microsecond clock and printing.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "demonic_engine.hpp"

namespace bench {

constexpr size_t kBlock        = 64;    // the desktop app's block size
constexpr int    kBlocksPerRun = 200;

// Real-time budget of one block: it must be processed before the next one arrives.
constexpr float budgetUs(float sampleRate) { return 1.0e6f * kBlock / sampleRate; }

// One block holding exactly one cycle of a sine, so repeating it is seamless.
inline void fillInput(float* in) {
    for (size_t i = 0; i < kBlock; ++i)
        in[i] = 0.5f * sinf(6.2831853f * static_cast<float>(i) / static_cast<float>(kBlock));
}

inline void minimal(DemonicEngine& e) {
    e.setSpectralFreeze(false);
    e.setPitchShift(0.0f);
    e.setSubOctaveBlend(0.0f);
    e.setRobotBlend(0.0f);
    e.setWavefold(0.0f);
    e.setDecimation(1.0f);
    e.setDelayMix(0.0f);
    e.setReverbMix(0.0f);
    e.setDrive(0.0f);
    e.setMix(1.0f);
}

struct Scenario {
    const char* name;
    void (*configure)(DemonicEngine&);
};

inline const Scenario kScenarios[] = {
    {"Baseline (optional effects off)",  [](DemonicEngine& e) { minimal(e); }},
    {"Baseline + pitch shift -5 st",     [](DemonicEngine& e) { minimal(e); e.setPitchShift(-500.0f); }},
    {"Preset 0: DEMONIC GROWL",          [](DemonicEngine& e) { minimal(e); e.loadPreset(0); }},
    {"Preset 1: MEGATRON",               [](DemonicEngine& e) { minimal(e); e.loadPreset(1); }},
    {"Preset 2: CYBER DEMON",            [](DemonicEngine& e) { minimal(e); e.loadPreset(2); }},
    {"Worst case: MEGATRON + pitch + freeze", [](DemonicEngine& e) {
        minimal(e); e.loadPreset(1); e.setPitchShift(-500.0f); e.setSpectralFreeze(true); }},
};

inline volatile float sink;   // consuming the output stops the compiler from optimising the work away

// Average microseconds per block. nowUs() is the board's microsecond clock; a 32-bit counter is
// fine, because the subtraction is unsigned and one run takes well under a second.
template <typename NowUs>
float measureUsPerBlock(DemonicEngine& engine, const float* in, float* out, NowUs nowUs) {
    for (int i = 0; i < 20; ++i) engine.processBlock(in, out, kBlock);   // warm up
    const auto t0 = nowUs();
    for (int i = 0; i < kBlocksPerRun; ++i) {
        engine.processBlock(in, out, kBlock);
        sink = out[kBlock - 1];
    }
    const auto elapsed = nowUs() - t0;
    return static_cast<float>(elapsed) / static_cast<float>(kBlocksPerRun);
}

inline const char* verdict(float pct) {
    if (pct < 50.0f)  return "OK";
    if (pct < 80.0f)  return "TIGHT";
    if (pct < 100.0f) return "VERY TIGHT";
    return "TOO SLOW";
}

} // namespace bench
