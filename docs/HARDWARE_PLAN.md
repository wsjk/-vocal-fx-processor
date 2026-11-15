# Hardware Plan

Where the pedal's hardware stands and what happens next. Decided 2026-11-14. The technical background (real-time budget, what to try if the engine is too slow) is in [Developer Guide §11](DEVELOPER_GUIDE.md#11-toward-the-raspberry-pi-pico).

## 1. Board: WeAct RP2350B Core

**Choice:** the [WeAct Studio RP2350B Core](https://github.com/WeActStudio/WeActStudio.RP2350BCoreBoard) with 16 MB flash. It is about 41 × 41 mm, has USB-C, and costs a few dollars. Buy it from WeAct's own AliExpress store ([WeAct Studio Official Store](https://weactstudio.aliexpress.com/store/1101545918) or [WeAct Studio One Store](https://www.aliexpress.com/store/1104927695)); other sellers copy the listings.

| | WeAct RP2350B Core | Pico / Pico 2 | Black Pill (STM32F411) |
| :--- | :--- | :--- | :--- |
| Usable GPIO | **48** | 26 | about 30 |
| RAM | **520 KB** | 264 KB / 520 KB | 128 KB |
| CPU | 2× Cortex-M33, 150 MHz, **FPU** | M0+ 133 MHz, no FPU / M33 150 MHz, FPU | M4F 100 MHz, FPU |
| Toolchain | Pico SDK (already used by `pico/`) | Pico SDK | STM32 HAL (rewrite) |

Why the others are out:

- **Pico / Pico 2:** 26 GPIO is not enough for the panel (see §2) without I²C expanders. The Pico 2 (SC1631 / SC1632 "Pico 2 H") has the same chip as the WeAct in its smaller RP2350A package, so it shares the FPU and RAM but not the pins.
- **Black Pill:** the engine (`sizeof(DemonicEngine)` ≈ 182 KB) does not fit in 128 KB, the pins are just as tight, and `pico/` would have to be rewritten.

**Use the Arm cores.** The RP2350 can run either two Arm Cortex-M33 cores or two RISC-V Hazard3 cores. Only the Arm cores have the floating-point unit the engine needs. The board's SDK definition (`weact_studio_rp2350b_core`, in Pico SDK 2.x) builds for Arm by default.

## 2. Pin budget

What the simulated enclosure needs, if every control gets its own pins:

| Need | GPIO |
| :--- | :--- |
| 11 encoders × 2 (A/B) | 22 |
| 3 footswitches (Preset, Freeze, Bypass) | 3 |
| OLED, SSD1306 128×64 on I²C (an audio codec's control bus can share it) | 2 |
| I²S audio codec: bit clock, word clock, data in, data out | 4 |
| 11 encoder LED rings as one WS2812 chain | 1 |
| **Total** | **≈ 32 of 48** |

That leaves about 16 pins: enough for push switches on some encoders, an MCLK line if the codec wants one, or status LEDs. The exact pin map is still to do; constraints to respect when writing it:

- GP25 drives the board's LED, and the SDK's default UART is on GP12/GP13 (only used if the firmware turns on serial output there).
- The ADC-capable pins on the RP2350B are GP40–GP47. Keep them free in case an analog input is wanted later.
- I²S runs on PIO, so its pins can go almost anywhere. The usual library (pico-extras `audio_i2s`) needs the bit clock and word clock on two neighbouring pins.

## 3. Shopping list

**For the benchmark (step 4):** the board and a USB-C **data** cable (some only carry power). Solder the supplied headers, or leave them off for now.

**For the first audio prototype:**

- I²S audio: a codec module (WM8960 or SGTL5000), or a PCM5102 DAC plus a PCM1808 ADC.
- 128×64 SSD1306 OLED on I²C.
- 11 rotary encoders, 3 footswitches, WS2812 LEDs (rings or a strip), breadboard and jumper wires.
- Optional: a Raspberry Pi Debug Probe for SWD debugging.

## 4. Next steps

1. **Run the benchmark** (go / no-go). Hold BOOT, plug the board in, copy `pico/build/vocal_pico_bench.uf2` to the `RP2350` drive, and read the USB serial port. Build it with `.agents/skills/vocal-fx-processor/scripts/build_pico.sh`. Results go into Developer Guide §11.
2. **If it is too slow**, work through the optimisations in Developer Guide §11 ("Things to expect"), measuring after each one. The second core is the big lever.
3. **Pin map:** write the exact assignment (§2) into one header (for example `pico/pinout.hpp`) that the firmware and this plan both refer to.
4. **Audio I/O:** I²S in and out through a codec, with `processBlock` called from the DMA interrupt.
5. **Controls and display:** encoders, footswitches, OLED and LED rings, reusing the simulator's `simctl` value logic.
