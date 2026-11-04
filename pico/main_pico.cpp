// Raspberry Pi Pico (RP2040) benchmark for DemonicEngine.
//
// Step one of the hardware port is not audio I/O, it is answering one question: can the RP2040 run
// the engine in real time at all? The Cortex-M0+ cores have no floating-point unit, and the engine
// does float maths (sin, tanh, ...) on every sample.
//
// This program runs the engine on synthetic input, measures how long each 64-sample block takes, and
// prints the result over USB serial. Nothing here touches a microphone, DAC, or display.
//
// The budget: at 44.1 kHz a 64-sample block lasts 64 / 44100 = 1451 microseconds. processBlock must
// finish in LESS than that, with room to spare for the audio driver, the display, and the buttons.
// A reading above 100 % means the engine cannot keep up and glitches would be unavoidable.
//
// Build and flash instructions: see pico/CMakeLists.txt.
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "demonic_engine.hpp"

namespace {

constexpr float  kSampleRate   = 44100.0f;
constexpr size_t kBlock        = 64;
constexpr int    kBlocksPerRun = 200;
constexpr float  kBudgetUs     = 1.0e6f * kBlock / kSampleRate;   // ~1451 us

// ~190 KB: far too big for the stack, so it must be a global.
DemonicEngine engine(kSampleRate);

float inBlock[kBlock];
float outBlock[kBlock];
volatile float sink;   // consuming the output stops the compiler from optimising the work away

// One 64-sample block holding exactly one cycle of a sine (689 Hz), so repeating it is seamless.
void fillInput() {
    for (size_t i = 0; i < kBlock; ++i)
        inBlock[i] = 0.5f * sinf(6.2831853f * static_cast<float>(i) / static_cast<float>(kBlock));
}

void minimal(DemonicEngine& e) {
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

const Scenario kScenarios[] = {
    {"Baseline (optional effects off)",  [](DemonicEngine& e) { minimal(e); }},
    {"Baseline + pitch shift -5 st",     [](DemonicEngine& e) { minimal(e); e.setPitchShift(-500.0f); }},
    {"Preset 0: DEMONIC GROWL",          [](DemonicEngine& e) { minimal(e); e.loadPreset(0); }},
    {"Preset 1: MEGATRON",               [](DemonicEngine& e) { minimal(e); e.loadPreset(1); }},
    {"Preset 2: CYBER DEMON",            [](DemonicEngine& e) { minimal(e); e.loadPreset(2); }},
    {"Worst case: MEGATRON + pitch + freeze", [](DemonicEngine& e) {
        minimal(e); e.loadPreset(1); e.setPitchShift(-500.0f); e.setSpectralFreeze(true); }},
};

float measureUsPerBlock() {
    for (int i = 0; i < 20; ++i) engine.processBlock(inBlock, outBlock, kBlock);   // warm up
    const uint64_t t0 = time_us_64();
    for (int i = 0; i < kBlocksPerRun; ++i) {
        engine.processBlock(inBlock, outBlock, kBlock);
        sink = outBlock[kBlock - 1];
    }
    const uint64_t elapsed = time_us_64() - t0;
    return static_cast<float>(elapsed) / static_cast<float>(kBlocksPerRun);
}

const char* verdict(float pct) {
    if (pct < 50.0f)  return "OK";
    if (pct < 80.0f)  return "TIGHT";
    if (pct < 100.0f) return "VERY TIGHT";
    return "TOO SLOW";
}

}  // namespace

int main() {
    stdio_init_all();
    fillInput();
    sleep_ms(2000);   // give a serial terminal time to attach after reset

    while (true) {
        const uint32_t mhz = clock_get_hz(clk_sys) / 1000000u;
        const float cyclesPerSample = static_cast<float>(mhz) * 1.0e6f / kSampleRate;
        printf("\n=== DemonicEngine on RP2040 | clk_sys %u MHz | engine %u bytes ===\n",
               static_cast<unsigned>(mhz), static_cast<unsigned>(sizeof(DemonicEngine)));
        printf("Real-time budget per %u-sample block: %.0f us (about %.0f CPU cycles per sample)\n",
               static_cast<unsigned>(kBlock), kBudgetUs, cyclesPerSample);

        for (const Scenario& s : kScenarios) {
            s.configure(engine);
            const float us = measureUsPerBlock();
            const float pct = 100.0f * us / kBudgetUs;
            printf("%-40s %8.1f us/block  %6.1f %% of budget  %s\n", s.name, us, pct, verdict(pct));
        }
        sleep_ms(3000);
    }
}
