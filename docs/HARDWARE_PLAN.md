# Hardware Plan

Where the pedal's hardware stands and what happens next. Board decided 2026-11-14; audio design and pin map added 2026-11-15 from a review of an outside hardware specification (see §7). The technical background (real-time budget, what to try if the engine is too slow) is in [Developer Guide §11](DEVELOPER_GUIDE.md#11-toward-the-raspberry-pi-pico).

## 1. Board: WeAct RP2350B Core

**Choice:** the [WeAct Studio RP2350B Core](https://github.com/WeActStudio/WeActStudio.RP2350BCoreBoard) with 16 MB flash. It is about 41 × 41 mm, has USB-C, and costs a few dollars. Buy it from WeAct's own AliExpress store ([WeAct Studio Official Store](https://weactstudio.aliexpress.com/store/1101545918) or [WeAct Studio One Store](https://www.aliexpress.com/store/1104927695)); other sellers copy the listings.

| | WeAct RP2350B Core | Pico / Pico 2 | Black Pill (STM32F411) |
| :--- | :--- | :--- | :--- |
| Usable GPIO | **48** | 26 | about 30 |
| RAM | **520 KB** | 264 KB / 520 KB | 128 KB |
| CPU | 2× Cortex-M33, 150 MHz, **FPU** | M0+ 133 MHz, no FPU / M33 150 MHz, FPU | M4F 100 MHz, FPU |
| Toolchain | Pico SDK (already used by `pico/`) | Pico SDK | STM32 HAL (rewrite) |

Why the others are out:

- **Potentiometers instead of encoders** would fit a Pico 2 (16 pots through one CD74HC4067 analog multiplexer need only 5 pins), but a pot cannot be moved by a preset, so presets and knob positions would disagree. Encoders stay, as in the simulator.

- **Pico / Pico 2:** 26 GPIO is not enough for the panel (see §4) without I²C expanders. The Pico 2 (SC1631 / SC1632 "Pico 2 H") has the same chip as the WeAct in its smaller RP2350A package, so it shares the FPU and RAM but not the pins.
- **Black Pill:** the engine (`sizeof(DemonicEngine)` ≈ 182 KB) does not fit in 128 KB, the pins are just as tight, and `pico/` would have to be rewritten.

**Use the Arm cores.** The RP2350 can run either two Arm Cortex-M33 cores or two RISC-V Hazard3 cores. Only the Arm cores have the floating-point unit the engine needs. The board's SDK definition (`weact_studio_rp2350b_core`, in Pico SDK 2.x) builds for Arm by default.

## 2. Audio design

```
mic (XLR / TRS) ─► preamp +20…+50 dB ─► WM8731 ADC ─I²S─► RP2350B ─I²S─► WM8731 DAC ─► output buffer + filter ─► line out
                   (NE5532 / TL072,                        core 1: DemonicEngine
                    DC blocking)                           core 0: controls, OLED, codec setup
```

- **Codec: WM8731** (Wolfson / Cirrus Logic). Stereo 24-bit ADC and DAC, set up over I²C (address 0x1A) at boot: input select, mic boost, mute, I²S format. The RP2350 is I²S clock master and drives the data through PIO and DMA. Breakout boards exist (for example MikroElektronika "Audio Codec Proto").
- **Mic preamp.** The WM8731's own mic input has only about +20 dB of boost, which is not enough for dynamic mics. Put a low-noise op-amp preamp (NE5532 or TL072) in front of the line input, with a gain trim of roughly +20 to +50 dB and DC blocking.
- **Output.** An op-amp buffer and low-pass reconstruction filter after the DAC, to a ¼" TRS (or XLR) line out.
- **Phantom power (optional).** For condenser mics: a 48 V boost converter feeding the mic through two matched 6.81 kΩ resistors, with 47 µF coupling capacitors before the preamp.
- **Grounding and power.** Separate analog (AGND) and digital (DGND) ground areas joined at one point near the supply filter capacitors, and separate low-dropout regulators for the analog 3.3 V (AVDD) and digital 3.3 V (DVDD), so I²S clock noise stays out of the preamp.
- **Master clock.** The board's 12 MHz crystal goes straight out on clock-output pin GP21 (`clk_gpout0`) to the WM8731's MCLK, and the codec runs in its 12 MHz "USB mode", which produces exact 44.1 kHz and 48 kHz. This avoids making 11.2896 / 12.288 MHz with a PIO or fractional divider, which adds jitter.

## 3. Firmware layout

- **Core 1: audio only.** Two ping-pong DMA buffers per direction between the PIO I²S state machines and RAM; Core 1 calls `DemonicEngine::processBlock` on each finished buffer. Its code should run from RAM (`__not_in_flash_func`, or build the whole program with `pico_set_binary_type(... copy_to_ram)`) so flash cache misses cannot delay it. The engine object (~182 KB) is a global in SRAM.
- **Core 0: everything else.** Encoders, footswitches, WS2812 LED rings, the OLED (about 30 frames a second), and the WM8731 register setup, all over the shared I²C bus. It applies encoder values with the same `simctl` logic the simulator uses.
- **Between the cores.** Core 0 writes parameter values into a shared struct of atomics; Core 1 reads them at the start of each block and calls the engine's setters. No locks in the audio path (the same rule as the desktop app).
- **Compiler.** The SDK already compiles for the Cortex-M33 with its FPU (`-mcpu=cortex-m33`, `-mfloat-abi=softfp`); the `float` code runs on the FPU as it is, with no fixed-point rewrite. `-mfloat-abi=hard` is an optional later tweak, not a requirement.

### Latency

| Block size | One block | Round trip (≈ 2 blocks, plus the codec's filter delay) |
| :--- | :--- | :--- |
| 64 samples @ 44.1 kHz | 1.45 ms | about 3 ms + codec |
| 32 samples @ 44.1 kHz | 0.73 ms | about 1.5 ms + codec |

Start with 64-sample blocks: that is what the desktop app and the benchmark use, and 3–4 ms is fine for live vocals. Getting under 2 ms needs 32-sample blocks, which doubles the per-block overhead; try it once the benchmark shows how much headroom there is.

## 4. Pin map

The exact assignment is in [`include/hw_pinout.hpp`](../include/hw_pinout.hpp), and `tests/hw_pinout_test.cpp` checks it (no pin used twice, none of the board's own pins, I²C and clock pins able to do their job, one encoder per parameter).

| GPIO | Use |
| :--- | :--- |
| GP2, GP3, GP4 | Footswitches: Bypass, Freeze, Preset (to ground, internal pull-ups) |
| GP5–GP6, GP15–GP20, GP26–GP39 | 11 encoders, A/B pairs, in the simulator's order (PITCH … WET/DRY) |
| GP8, GP9 | I²C0 SDA, SCL: WM8731 control and SSD1306 OLED (the board's default I²C pins) |
| GP11, GP12 | I²S bit clock, word clock (must be neighbours) |
| GP13, GP14 | I²S data out (to the DAC), data in (from the ADC) |
| GP21 | WM8731 MCLK, 12 MHz from the crystal |
| GP22 | WS2812 chain for the encoder LED rings |
| *kept free* | GP0 (optional PSRAM chip select on the board), GP23 (board button), GP25 (board LED), GP40–GP47 (the only ADC pins); also unused: GP1, GP7, GP10, GP24 |

That is 33 of 48 pins.

## 5. Shopping list

**For the benchmark (§6, step 1):** the board and a USB-C **data** cable (some only carry power). Solder the supplied headers, or leave them off for now.

**For the first audio prototype:**

- WM8731 codec breakout board.
- Mic preamp and output buffer: NE5532 or TL072 op-amps, plus passives; an XLR or TRS mic jack and a ¼" TRS output jack.
- Two 3.3 V low-dropout regulators (analog and digital).
- 128×64 SSD1306 OLED on I²C.
- 11 rotary encoders, 3 footswitches, WS2812 LEDs (rings or a strip), breadboard and jumper wires.
- Optional: a 48 V phantom-power boost module; a Raspberry Pi Debug Probe for SWD debugging.

## 6. Next steps

1. **Run the benchmark** (go / no-go). Hold BOOT, plug the board in, copy `pico/build/vocal_pico_bench.uf2` to the `RP2350` drive, and read the USB serial port. Build it with `.agents/skills/vocal-fx-processor/scripts/build_pico.sh`. Results go into Developer Guide §11.
2. **If it is too slow**, work through the optimisations in Developer Guide §11 ("Things to expect"), measuring after each one.
3. **Audio pass-through.** WM8731 on a breadboard: I²C setup, 12 MHz MCLK on GP21, PIO I²S in and out with DMA, and a clean sine wave or mic signal straight through (no engine yet).
4. **Engine on Core 1.** Call `processBlock` from the DMA handler; measure the block time against the deadline.
5. **Controls and display on Core 0.** Encoders, footswitches, OLED and LED rings, using `include/hw_pinout.hpp` and the simulator's `simctl` value logic.

## 7. Review of the outside specification (2026-11-15)

A separate "Hardware Architecture & Design Specification" proposed a Pico 2 with pots, a multiplexer and two footswitches. Adopted from it: the WM8731 codec, the analog front end, grounding and power rules, the core split, and its I²S pins. Not adopted, or corrected:

- **Board and controls:** kept the WeAct RP2350B with 11 encoders and 3 footswitches (see §1).
- **Latency:** it claimed under 2 ms round trip with 32–64-sample buffers; only 32-sample blocks get close (§3).
- **Engine description:** it counted "12 stages" and left out the pitch shifter. The engine is a noise gate, a pitch shifter, ten numbered stages and a wet/dry mix (User Guide, "How your voice is processed"). "Spectral Freeze" loops 4,096 samples of recent audio with no FFT, the "Catacomb" reverb is two feedback echoes (Developer Guide §10.5), and bypass is a software crossfade, not a relay "true bypass".
- **Master clock:** a PIO-generated 11.2896 / 12.288 MHz clock replaced by the crystal's 12 MHz and the codec's USB mode (§2).
- **I²C pins:** moved from GP0/GP1 to GP8/GP9, because GP0 is the WeAct board's optional PSRAM chip select.
- **Compiler flags:** `-mfloat-abi=hard` is optional (§3).
