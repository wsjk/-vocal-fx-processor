// Daisy Seed benchmark for DemonicEngine (STM32H750, Cortex-M7 at 480 MHz).
//
// Step one of the hardware port (docs/HARDWARE_PLAN.md §6): can the board run the engine in real time?
// This program runs the engine on synthetic input, measures how long each 64-sample block takes, and
// prints the result over USB serial. It does not start the audio codec, the display or the controls.
//
// The budget: libDaisy runs the codec at 48 kHz, so a 64-sample block lasts 64 / 48000 = 1333
// microseconds. processBlock must finish in LESS than that, with room to spare for the audio driver,
// the display and the controls. A reading above 100 % means the engine cannot keep up.
//
// Build and flash instructions: see daisy/Makefile. The scenarios and timing loop are shared with the
// Pico benchmark (include/engine_bench.hpp).
#include "daisy_seed.h"
#include "engine_bench.hpp"

using namespace daisy;

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kBudgetUs   = bench::budgetUs(kSampleRate);   // ~1333 us

DaisySeed hw;

// ~182 KB: far too big for the stack, so it must be a global. libDaisy's linker script puts .bss in
// the 512 KB AXI SRAM.
DemonicEngine engine(kSampleRate);

float inBlock[bench::kBlock];
float outBlock[bench::kBlock];

// The logger's printf has no float support (newlib-nano), so print tenths as two integers.
struct Tenths { int whole, tenth; };
Tenths tenths(float x) {
    const int t = static_cast<int>(x * 10.0f + 0.5f);   // x is never negative here
    return {t / 10, t % 10};
}

}  // namespace

int main() {
    hw.Init(true);            // true = boost the CPU clock to 480 MHz
    hw.StartLog(true);        // USB serial; waits until a terminal opens the port
    bench::fillInput(inBlock);

    bool led = false;
    while (true) {
        const unsigned mhz = System::GetSysClkFreq() / 1000000u;
        hw.PrintLine("");
        // Lines longer than the logger's 128-byte buffer are cut off, so keep them short.
        hw.PrintLine("=== vocal-fx %s | DemonicEngine on Daisy Seed | sysclk %u MHz | engine %u bytes ===",
                     VFX_VERSION, mhz, static_cast<unsigned>(sizeof(DemonicEngine)));
        hw.PrintLine("Real-time budget per %u-sample block: %u us (about %u CPU cycles per sample)",
                     static_cast<unsigned>(bench::kBlock), static_cast<unsigned>(kBudgetUs + 0.5f),
                     static_cast<unsigned>(mhz * 1.0e6f / kSampleRate + 0.5f));

        for (const bench::Scenario& s : bench::kScenarios) {
            s.configure(engine);
            const float us  = bench::measureUsPerBlock(engine, inBlock, outBlock, [] { return System::GetUs(); });
            const float pct = 100.0f * us / kBudgetUs;
            const Tenths u = tenths(us), p = tenths(pct);
            hw.PrintLine("%-40s %6d.%d us/block  %4d.%d %% of budget  %s",
                         s.name, u.whole, u.tenth, p.whole, p.tenth, bench::verdict(pct));
        }
        hw.SetLed(led = !led);   // blinks once per run, so a silent port still shows the board is alive
        System::Delay(3000);
    }
}
