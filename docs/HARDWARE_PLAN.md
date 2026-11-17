# Hardware Plan

Where the pedal's hardware stands and what happens next. Board changed to the Daisy Seed on 2026-11-15 (§1); the analog front end comes from a review of an outside hardware specification (see §7). The real-time background (what to try if the engine is too slow) is in [Developer Guide §11](DEVELOPER_GUIDE.md#11-toward-the-raspberry-pi-pico), which still describes the earlier RP2350 benchmark.

## 1. Board: Electrosmith Daisy Seed 65MB

**Choice:** the [Electrosmith Daisy Seed](https://electro-smith.com/pages/the-daisy-seed), 65MB version: an STM32H750 (Arm Cortex-M7, 480 MHz, single- and double-precision FPU), a stereo audio codec on the board, 31 GPIO, 64 MB SDRAM, 128 KB internal flash plus 8 MB QSPI flash. Firmware uses Electrosmith's [libDaisy](https://github.com/electro-smith/libDaisy).

| | Daisy Seed 65MB | WeAct RP2350B Core + WM8731 (previous plan) |
| :--- | :--- | :--- |
| CPU | 1× Cortex-M7, 480 MHz, FPU | 2× Cortex-M33, 150 MHz, FPU |
| RAM | 512 KB AXI SRAM (+ DTCM, D2 SRAM), 64 MB SDRAM | 520 KB |
| Audio | Codec on the board, line-level in and out | Separate WM8731, I²S over PIO, MCLK from GP21 |
| GPIO | 31 | 48 |
| Toolchain | libDaisy (`daisy/`) | Pico SDK (the removed `pico/`) |

Why it replaced the RP2350B:

- **Cost.** WM8731 parts turned out to cost far more than first assumed (the LCSC bare chip was about $8.67 before shipping, modules $20–50), so the RP2350B + codec came to about the price of a Daisy Seed, which has its codec built in. Stompboxparts (US) [lists the Daisy Seed 65MB](https://stompboxparts.com/electrosmith) at $24.00 with headers supplied but not soldered (checked 2026-11-15; check stock before ordering).
- **Speed.** About 3× the clock of one RP2350 core, so the engine has much more headroom.
- **Less to build.** No codec board, no I²C codec setup, no PIO I²S, no master-clock trick.

What it costs: the Daisy has one core (audio and controls share it, §3), no PIO (the WS2812 chain uses SPI, §4), and 31 GPIO, which the panel uses almost entirely, so the twelfth encoder needs an I²C expander (§4). The benchmark is `daisy/`. The RP2350 benchmark (`pico/`) was removed once the engine outgrew the RP2350B's 520 KB (the echo, freeze and reverb now need about 1 MB); it is in the git history.

## 2. Audio design

```
mic (XLR / TRS) ─► preamp +20…+50 dB ─► Daisy audio in L ─► codec ─► STM32H750 ─► codec ─► Daisy audio out L ─► output buffer ─► line out
                   (NE5532 / TL072,                                  audio callback:
                    DC blocking)                                     DemonicEngine
```

- **Codec: the Daisy's own.** libDaisy sets it up and streams it over the STM32's SAI with DMA; the firmware sees only an audio callback. The codec part differs between Seed revisions, and libDaisy detects which one is fitted. Audio uses the Seed's dedicated audio pins (header pins 16–19), not GPIO. The input is line level.
- **Sample rate: 48 kHz.** libDaisy runs the codec at 48 kHz by default; the engine takes its sample rate in the constructor (`DemonicEngine(float sampleRate)`), so it runs at 48 kHz instead of the desktop's 44.1 kHz.
- **Mic preamp.** The line input needs a mic preamp: a low-noise op-amp (NE5532 or TL072) with a gain trim of roughly +20 to +50 dB and DC blocking.
- **Output.** An op-amp buffer after the Daisy's audio out, to a ¼" TRS (or XLR) line out.
- **Phantom power (optional).** For condenser mics: a 48 V boost converter feeding the mic through two matched 6.81 kΩ resistors, with 47 µF coupling capacitors before the preamp.
- **Grounding and power.** Keep the preamp's analog ground separate from digital ground, joined at one point near the supply filter capacitors; the Seed has separate AGND (pin 20) and GND (pin 40) pins for this. Run the op-amps from the pedal supply on a split (virtual-ground) rail, not from the Seed's 3.3 V pins, which are too low for an NE5532 or TL072. Check the Seed datasheet's VIN range before feeding it the pedal supply.

## 3. Firmware layout

The Daisy has one core, so the RP2350's core split becomes an interrupt split. `daisy/firmware.cpp` is laid out this way (compile-checked only, not yet run on a board):

- **Audio callback (interrupt).** libDaisy calls it with each finished DMA block; it reads the parameter struct, calls the engine's setters, then `DemonicEngine::processBlock`. Nothing else runs there: no I²C, no allocation, no locks.
- **Main loop: everything else.** Encoders, footswitches, WS2812 LED rings, and the SSD1306 OLED (about 30 frames a second, on I²C). Encoders are polled at about 1 kHz (from a timer or the loop) and use the same `simctl` value logic as the simulator.
- **Between them.** The main loop writes parameter values into a shared struct of atomics; the callback reads them at the start of each block. The same no-locks rule as the desktop app.
- **Memory.** The engine object (about 410 KB, most of it the ReverbSc reverb, whose 8 delay lines are read every sample) is in the 512 KB AXI SRAM at 0x24000000, where libDaisy's linker script puts `.bss`. Do not move it to SDRAM, which is much slower. Its long, rarely-read buffers, `DemonicEngineMemory` (2 s echo and 1 s freeze loop, about 576 KB), are a `DSY_SDRAM_BSS` global in the 64 MB SDRAM. The SDRAM only works after `hw.Init()`, and the engine's constructor clears that memory, so the engine is a `static` constructed in `main()` after `hw.Init()`, not a global. The benchmark build uses 438 KB (84 %) of AXI SRAM and 580 KB of SDRAM.
- **Bypass clears the SDRAM buffers in slices** (64 samples per frame, so about 36 blocks of 64), so engaging bypass never overruns a block.
- **Program size.** The internal flash is 128 KB; the benchmark uses 94 KB (72 %) and the firmware 113 KB (88 %, including newlib's float `printf` for the OLED values). If the firmware grows past that, use the Daisy bootloader and run from QSPI or SRAM (libDaisy's `APP_TYPE`).
- **Compiler.** libDaisy builds for the Cortex-M7 with a hard-float ABI; the engine's `float` code runs on the FPU as it is.

### Latency

| Block size | One block | Round trip (≈ 2 blocks, plus the codec's filter delay) |
| :--- | :--- | :--- |
| 64 samples @ 48 kHz | 1.33 ms | about 2.7 ms + codec |
| 32 samples @ 48 kHz | 0.67 ms | about 1.3 ms + codec |

Start with 64-sample blocks, the size the desktop app and the benchmark use. 32-sample blocks get under 2 ms but double the per-block overhead; try them once the benchmark shows the headroom.

## 4. Pin map

The exact assignment is in [`include/hw_pinout.hpp`](../include/hw_pinout.hpp), and `tests/hw_pinout_test.cpp` checks it (no pin used twice, none of the reserved pins, I²C and LED pins on the right peripheral, one encoder per parameter, valid expander pins and address). Names are libDaisy's `seed::D0`…`seed::D30`, which are not the header pin numbers.

| Daisy pin | STM32 | Use |
| :--- | :--- | :--- |
| D7, D8, D9 | PG10, PG11, PB4 | Footswitches: Bypass, Freeze, Preset (to ground, internal pull-ups) |
| D1–D6, D13–D28 | | 11 encoders, A/B pairs, in the simulator's order (PITCH … WET/DRY, except ECHO TIME) |
| *MCP23017 at 0x20, on I²C1* | GPA0, GPA1 | ECHO TIME encoder (the twelfth), internal pull-ups |
| D10 | PB5 | SPI1 MOSI: WS2812 chain for the encoder LED rings |
| D11, D12 | PB8, PB9 | I²C1 SCL, SDA: SSD1306 OLED |
| *kept free* | PB12, PB14, PB15 | D0 (USB HS ID), D29, D30 (USB HS D−, D+), for a later USB port such as USB MIDI |

That is 28 of the 31 pins, plus 2 of the expander's 16. Consequences:

- **WS2812 over SPI.** With no PIO, the LED data is SPI1 in transmit-only master mode, each LED bit encoded as several SPI bits. libDaisy's SPI driver configures only MOSI in that mode, so D8 (SPI1 SCK) stays free for a footswitch. The firmware runs SPI1 at 3.125 MHz (its 25 MHz clock / 8) and sends 4 SPI bits per LED bit by DMA. Fit the usual 300–500 Ω series resistor on the data line.
- **No spare pins.** The encoders use D1–D6 (the SD card pins), D13–D14 (USART1), all twelve ADC pins (D15–D25, D28), the DAC outputs (D22, D23) and the second SAI's pins (D24–D28). So the twelfth encoder, ECHO TIME, is on an **MCP23017** I²C GPIO expander on the OLED's bus (address 0x20, A0–A2 to ground). Its 14 free pins are where any further encoder, an expression-pedal switch or an SD card's freed pins would go.
- **Polling the expander.** The OLED and the expander share I²C1, and a full 128×64 frame takes about 25 ms at 400 kHz, long enough to miss encoder steps. The firmware sends the OLED frame in half-page chunks (16 × 64 bytes, about 1.5 ms each) and reads the expander between chunks, at most once a millisecond. If that misses steps on fast turns, wire the expander's INTA pin to D0 (kept free today) and read on its interrupt.

## 5. Shopping list

**For the benchmark (§6, step 1):** a Daisy Seed 65MB (see §1 for the listing) and a micro-USB **data** cable (some only carry power). Solder the supplied headers.

**For the first audio prototype:**

- Mic preamp and output buffer: NE5532 or TL072 op-amps, plus passives; an XLR or TRS mic jack and a ¼" TRS output jack.
- A pedal power supply and a split-rail (virtual-ground) circuit for the op-amps.
- 128×64 SSD1306 OLED on I²C.
- 12 rotary encoders, 3 footswitches, WS2812 LEDs (rings or a strip), breadboard and jumper wires.
- An MCP23017 I²C GPIO expander (DIP or a breakout board) for the twelfth encoder.
- Optional: a 48 V phantom-power boost module; an ST-Link (such as the STLINK-V3MINI) for debugging.

No prices are given here beyond the board's; check live listings before ordering.

## 6. Next steps

1. **Run the benchmark** (go / no-go). `daisy/main_daisy.cpp` runs the engine at 48 kHz on synthetic input, times `processBlock` with libDaisy's microsecond timer, and prints the share of the 1,333 µs budget over USB serial. Build and flash it with `.agents/skills/vocal-fx-processor/scripts/build_daisy.sh program-dfu` (Seed in DFU mode: hold BOOT, press RESET), then open the serial port. Results go into Developer Guide §11.
2. **If it is too slow**, work through the optimisations in Developer Guide §11 ("Things to expect"), measuring after each one.
3. **Audio pass-through.** Preamp into the Daisy's input, the callback copying input to output, and a clean mic signal straight through (no engine yet).
4. **Engine in the callback.** Written: `daisy/firmware.cpp` (`build_daisy.sh`, then `APP=firmware build_daisy.sh program-dfu`). Check the block time against the deadline: the OLED's bottom line shows the DSP load (average and peak).
5. **Controls and display in the main loop.** Written in the same file: encoders (eleven on GPIO, ECHO TIME on the MCP23017), footswitches, OLED and SPI LED chain, using `include/hw_pinout.hpp` and the simulator's `simctl` value logic. Check each part on the breadboard. Assumed until then: 12-LED rings chained in encoder order with LED 0 at the top, four transitions per encoder detent, and WS2812 timing from SPI1 at 3.125 MHz (4 SPI bits per LED bit).

## 7. Review of the outside specification (2026-11-15)

A separate "Hardware Architecture & Design Specification" proposed a Pico 2 with pots, a multiplexer and two footswitches. Adopted from it: the analog front end, the grounding rules and the split between audio and control work. Its WM8731 codec and I²S pins were adopted at first and then dropped with the move to the Daisy Seed (§1). Not adopted, or corrected:

- **Board and controls:** encoders (11 then, 12 now) and 3 footswitches, not pots, because a pot cannot be moved by a preset, so presets and knob positions would disagree.
- **Latency:** it claimed under 2 ms round trip with 32–64-sample buffers; only 32-sample blocks get there (§3).
- **Engine description:** it counted "12 stages" and left out the pitch shifter. The engine is a noise gate, a pitch shifter, ten numbered stages and a wet/dry mix (User Guide, "How your voice is processed"). "Spectral Freeze" looped 4,096 samples with no FFT (it now loops one second, still no FFT), the "Catacomb" reverb was two feedback echoes (now DaisySP's ReverbSc; Developer Guide §10.5), and bypass is a software crossfade, not a relay "true bypass".
- **Compiler flags:** its `-mfloat-abi=hard` note no longer matters; libDaisy already builds hard-float (§3).
