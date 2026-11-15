# Vocal FX Processor

A real-time "demonic" vocal effects engine and guitar-pedal-style processor written in C++17.

Talk or sing into a microphone and the engine turns your voice into a pitch-shifted, sub-octave, robotic, bit-crushed, echoing creature. There are two parts: a desktop app that simulates the whole pedal (OLED, encoders, footswitches), and a hardware-agnostic DSP core intended for the Raspberry Pi Pico (RP2040).

<p align="center">
  <img src="docs/images/simulator.png" alt="The desktop simulator: the pedal enclosure with a 128x64 OLED showing the preset name and waveform, eleven encoders with LED rings in VOICE, MANGLE and SPACE panels, and PRESET, FREEZE and BYPASS footswitches" width="720">
</p>

---

## Features

- **Real-time DSP chain** (`DemonicEngine::processBlock`, in this order):
  1. **Noise gate + envelope follower.** Samples below 0.02 (about −34 dBFS) are zeroed, and a smoothed loudness envelope drives later stages.
  2. **Pitch shifter.** ±1200 cents (±1 octave). A sweeping delay tap plus a second covering tap, crossfaded only around the splice. It is fully skipped at 0 cents and moves formants along with pitch.
  3. **Chorus doubler.** A copy delayed about 10 ms, modulated by a 0.5 Hz LFO, mixed in at 30 %. Always on.
  4. **Sub-octave divider.** A Schmitt-trigger detector on a 2-pole low-passed copy of the voice flips a square wave once per cycle, giving a tone one octave down. It is smoothed and scaled by the voice's loudness.
  5. **Ring modulator ("Robot Clank").** The carrier runs at `robotFreq + 40 × envelope` Hz.
  6. **Drive + wavefolder.** `tanh` saturation followed by a triangle fold.
  7. **Decimator / bit-crusher.** Sample-and-hold by an integer factor, then fixed 32-level quantisation.
  8. **Tone filter.** A fixed one-pole low-pass at about 3 kHz.
  9. **Spectral Freeze.** Loops a 4,096-sample (about 93 ms) slice captured from the echo buffer, mixed 70/30 with the live signal.
  10. **Feedback echo.** A fixed tap at about 200 ms with feedback up to 1.05 (self-oscillation). There is no saturation in the loop.
  11. **"Catacomb" reverb.** Two feedback taps at about 122 ms and 272 ms. This is a multi-tap echo, not a diffuse reverb.
  12. **DC blocker**, then the **wet/dry mix**.
- **True bypass.** A 5 ms crossfade, then the output is a bit-for-bit copy of the input. All effect memory (echo and reverb tails, freeze loop, filters) is wiped, so bypass doubles as a panic button for a runaway echo.
- **Simulated pedal (raylib):** the window shows the pedal enclosure: a 128×64 OLED (output waveform, mic level, preset name or the value being turned), 11 endless encoders with LED value rings in VOICE / MANGLE / SPACE panels, PRESET / FREEZE / BYPASS footswitches with LEDs, and top-mounted jacks. Loading a preset turns the encoders to its values. Controls reach the audio thread through `std::atomic` values. Layout and encoder logic are plain headers, tested headlessly.
- **Embedded-minded core:** `float` maths only (no `M_PI` or double literals), no dynamic allocation (all buffers are fixed-size members, `sizeof(DemonicEngine)` = 186,824 bytes ≈ 182 KB), and no dependencies beyond the C++ standard library.

### Current limitations

These are tracked in [Developer Guide §10](docs/DEVELOPER_GUIDE.md#10-code-review-known-issues):

- **There is no output limiter.** Wavefolder settings above about 2.5 and Delay Feedback above 1.0 can push the output far beyond full scale.
- **The Pico build is a benchmark only.** There is no audio-capable firmware yet.

---

## Signal Flow

```
Mic in
  │
  ▼
[ Noise gate ] ──► envelope follower (feeds sub-octave level and ring-mod rate)
  │
  ▼
[ Pitch shifter ]        skipped when Pitch = 0
  │
  ▼
[ Chorus doubler ]  voice + 0.3 × delayed copy
  │
  ▼
  (+) ◄── [ Sub-octave divider ]   detects on the pitch-shifted voice
  │
  ▼
[ Ring modulator ]
  │
  ▼
[ tanh drive ] ─► [ Wavefolder ]
  │
  ▼
[ Decimator + 32-level quantiser ]
  │
  ▼
[ Tone low-pass ~3 kHz ]
  │
  ▼
[ Spectral Freeze loop ]  only while Freeze is on
  │
  ▼
[ Feedback echo ~200 ms ]
  │
  ▼
[ Catacomb two-tap echo ]
  │
  ▼
[ DC blocker ]
  │
  ▼
[ Wet/dry mix ] ─► [ Bypass crossfade ] ─► Speaker out
```

---

## Getting Started

### Prerequisites

- **C++17 compiler:** Apple Clang (Xcode Command Line Tools), GCC or Clang.
- **CMake** 3.14 or newer.
- **PortAudio**, needed for the desktop app only.
- **Internet access** on the first configure, so CMake can download raylib 5.0.

macOS:

```bash
brew install cmake portaudio pkg-config
```

Debian / Ubuntu (the engine and tests are tested on Linux; the desktop app is not):

```bash
sudo apt install build-essential cmake pkg-config portaudio19-dev libasound2-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

### Build and run

```bash
cmake -S . -B build          # add -DCMAKE_POLICY_VERSION_MINIMUM=3.5 if CMake 4 complains about raylib
cmake --build build -j4
./build/vocal_desktop
```

> **Use headphones.** With laptop speakers the mic picks up the output and you get feedback howl.

Build options: `-DBUILD_DESKTOP=OFF` builds only the engine and tests (no PortAudio or raylib), `-DBUILD_TESTS=OFF` skips the tests, and `-DUSE_SYSTEM_RAYLIB=ON` uses an installed raylib 5.0+ instead of downloading it.

---

## Controls

| Key / Control | Action |
|---|---|
| **`P`** / PRESET footswitch | Load the next preset (`DEMONIC GROWL`, `MEGATRON`, `CYBER DEMON`); the encoders turn to its values |
| **`F`** / FREEZE footswitch | Toggle Spectral Freeze. This does nothing while bypassed. |
| **`B`** or **`Space`** / BYPASS footswitch | True bypass on/off. This also switches Freeze off. |
| **Mouse drag / scroll on an encoder** | Turn any of the 11 encoders: PITCH, DRIVE, SUB OCT, ROBOT, CLANK RATE, FOLD, DECIMATE, DELAY FB, DELAY MIX, REVERB, WET/DRY. Drag round the ring (or up/down from the centre); scroll for single steps. |
| **`Esc`** / close window | Quit |

---

## Tests

The headless DSP test suite runs without audio hardware:

```bash
cmake -S . -B build-tests -DBUILD_DESKTOP=OFF
cmake --build build-tests -j4
ctest --test-dir build-tests --output-on-failure
```

`tests/engine_dsp_test.cpp` measures what comes out of the engine, and should print `ALL TESTS PASSED (0 failures)`. It checks:
- pitch-shifter accuracy on sine and voice-like input, and that switching it on or off does not click;
- sub-octave energy at f/2, and silence in giving silence out;
- that bypass is bit-exact, click-free and clears echo and reverb tails, and that it recovers from a runaway echo.

`tests/engine_smoke_test.cpp` is an **out-of-date** file. It calls engine methods that no longer exist, so it does not compile, and CMake does not build it.

---

## Raspberry Pi Pico

`pico/` is a separate CMake project that cross-compiles a **benchmark**. It runs the engine on synthetic input and reports, over USB serial, how much of the 1,451 µs real-time budget each 64-sample block uses. Setup, build and flashing instructions are in the header of [`pico/CMakeLists.txt`](pico/CMakeLists.txt) and in [Developer Guide §11](docs/DEVELOPER_GUIDE.md#11-toward-the-raspberry-pi-pico). You must copy `pico_sdk_import.cmake` from the Pico SDK into `pico/` first. The benchmark has not yet been run on hardware.

---

## Repository Structure

```
vocal-fx-processor/
├── CMakeLists.txt              Desktop/test build: demonic_engine library, engine_dsp_test, sim_controls_test, vocal_desktop
├── README.md                   This file
├── AGENTS.md                   Instructions for coding agents (CLAUDE.md is a symlink to it)
├── docs/
│   ├── USER_GUIDE.md           End-user guide: controls, parameters, presets, recipes, troubleshooting
│   └── DEVELOPER_GUIDE.md      Architecture, stage-by-stage DSP walkthrough, C++ primer, known issues, Pico notes
├── include/
│   ├── demonic_engine.hpp      DemonicEngine class and VocalPreset struct
│   ├── sim_controls.hpp        Simulator encoders: ranges, steps, display, preset values
│   ├── sim_layout.hpp          Simulator enclosure geometry, hit-testing, knob dragging
│   ├── sim_font.hpp            Embedded DejaVu Sans Mono for the simulator
│   └── dsp_pipeline.hpp        Empty placeholder (not used)
├── src/
│   ├── demonic_engine.cpp      The whole DSP engine (standard library only)
│   └── dsp_pipeline.cpp        Empty placeholder (not used)
├── desktop/
│   └── main_desktop.cpp        Desktop app: PortAudio callback + raylib pedal simulator
├── tests/
│   ├── engine_dsp_test.cpp     Frequency-measuring DSP tests (registered with ctest)
│   ├── sim_controls_test.cpp   Simulator layout and encoder tests (registered with ctest)
│   └── engine_smoke_test.cpp   Out of date; not built
├── pico/
│   ├── CMakeLists.txt          Separate Pico SDK project (setup notes in its header)
│   └── main_pico.cpp           RP2040 real-time budget benchmark
├── .agents/skills/vocal-fx-processor/   Agent runbook, references and build scripts
└── portaudio/                  Optional vendored PortAudio source (git-ignored; last-resort fallback)
```

---

## Documentation

- **[docs/USER_GUIDE.md](docs/USER_GUIDE.md):** using the pedal: controls, every encoder, presets, Freeze, Bypass, sound recipes, troubleshooting.
- **[docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md):** how the code works, written for Python programmers new to C++. It covers threads and atomics, each DSP stage, the build system, how to extend the engine, tests, known issues and the Pico port.
