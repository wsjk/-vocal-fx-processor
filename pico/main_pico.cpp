// RP2350 / RP2040 benchmark for DemonicEngine (built for the WeAct RP2350B Core by default).
//
// Step one of the hardware port is not audio I/O, it is answering one question: can the chip run
// the engine in real time at all? The engine does float maths (sin, tanh, ...) on every sample. The
// RP2350's Cortex-M33 cores have a single-precision FPU; the RP2040's Cortex-M0+ cores do not.
//
// This program runs the engine on synthetic input, measures how long each 64-sample block takes, and
// prints the result over USB serial. Nothing here touches a microphone, DAC, or display.
//
// The budget: at 44.1 kHz a 64-sample block lasts 64 / 44100 = 1451 microseconds. processBlock must
// finish in LESS than that, with room to spare for the audio driver, the display, and the buttons.
// A reading above 100 % means the engine cannot keep up and glitches would be unavoidable.
//
// Build and flash instructions: see pico/CMakeLists.txt. The scenarios and timing loop are shared with
// the Daisy Seed benchmark (include/engine_bench.hpp).
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "engine_bench.hpp"

#if PICO_RP2350
static const char* const kChip = "RP2350";
#else
static const char* const kChip = "RP2040";
#endif

namespace {

constexpr float kSampleRate = 44100.0f;
constexpr float kBudgetUs   = bench::budgetUs(kSampleRate);   // ~1451 us

// ~190 KB: far too big for the stack, so it must be a global.
DemonicEngine engine(kSampleRate);

float inBlock[bench::kBlock];
float outBlock[bench::kBlock];

}  // namespace

int main() {
    stdio_init_all();
    bench::fillInput(inBlock);
    sleep_ms(2000);   // give a serial terminal time to attach after reset

    while (true) {
        const uint32_t mhz = clock_get_hz(clk_sys) / 1000000u;
        const float cyclesPerSample = static_cast<float>(mhz) * 1.0e6f / kSampleRate;
        printf("\n=== vocal-fx %s | DemonicEngine on %s | clk_sys %u MHz | engine %u bytes ===\n",
               VFX_VERSION, kChip, static_cast<unsigned>(mhz), static_cast<unsigned>(sizeof(DemonicEngine)));
        printf("Real-time budget per %u-sample block: %.0f us (about %.0f CPU cycles per sample)\n",
               static_cast<unsigned>(bench::kBlock), kBudgetUs, cyclesPerSample);

        for (const bench::Scenario& s : bench::kScenarios) {
            s.configure(engine);
            const float us = bench::measureUsPerBlock(engine, inBlock, outBlock, [] { return time_us_64(); });
            const float pct = 100.0f * us / kBudgetUs;
            printf("%-40s %8.1f us/block  %6.1f %% of budget  %s\n", s.name, us, pct, bench::verdict(pct));
        }
        sleep_ms(3000);
    }
}
