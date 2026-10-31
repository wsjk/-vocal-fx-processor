# Vocal FX Processor

A real-time "demonic" vocal synthesis engine and guitar-pedal effects processor written in modern C++17.

Talk or sing into a microphone, and the engine transforms your voice into a sub-octave growling, robotic, bit-crushed, echoing creature. Designed with a dual architecture: an interactive desktop application with a simulated 128×64 OLED pedal display, and a zero-allocation, deterministic DSP core ready for microcontrollers like the Raspberry Pi Pico (RP2040).

---

## Features

- **10-Stage Real-Time DSP Pipeline:**
  1. **Noise Gate & Envelope Follower:** Smooth threshold gating with continuous volume envelope tracking.
  2. **Chorus Doubler:** LFO-modulated fractional delay line adding stereo-like vocal width.
  3. **Granular Pitch / Formant Shifter:** Dual-tap rotating crossfading granular delay ($\pm 1200$ cents) with Hann windowing.
  4. **Sub-Octave Synthesizer:** Lowpass fundamental tracking with Schmitt trigger hysteresis ($\pm 0.025$) for rock-solid pitch tracking without chatter.
  5. **Dynamic Ring Modulator ("Robot Clank"):** Metallic carrier oscillator dynamically modulated by your vocal loudness envelope.
  6. **True Positive Wavefolder & Drive:** Dual-stage distortion with asymmetric `tanh` saturation and bounded Euclidean wavefolding.
  7. **Decimator & Bitcrusher:** Lo-fi digital downsampling and amplitude quantization.
  8. **Tone Shaping:** Warm 1-pole lowpass filter eliminating harsh harmonic bite.
  9. **Spectral Freeze:** Instantaneous infinite sustain drone overlay capture.
  10. **Analog Tape Delay (300 ms):** Feedback delay line with `tanh` soft-saturation to allow infinite repeat / self-oscillation without digital clipping.
  11. **Catacomb Reverb:** 4 parallel prime-length Schroeder feedback comb filters + 2 series allpass diffusers creating lush, diffuse cavernous wash.
  12. **Master Safety Limiter & DC Blocker:** Highpass DC filter coupled with an output soft-limiter guaranteeing signals stay within $\pm 1.0$ full scale.

- **Simulated Hardware Pedal GUI (raylib):**
  - **128×64 Monochrome OLED Mock:** Replicating a physical SSD1306 OLED screen with live oscilloscope waveform and microphone VU meter.
  - **Instant Preset Synchronization:** Instantly recall presets (`DEMONIC GROWL`, `MEGATRON`, `CYBER DEMON`) with automatic slider synchronization.
  - **Hands-on Sliders:** 11 parameter sliders with thread-safe atomics between UI and audio threads.

- **Embedded / RP2040 Ready:**
  - **100% Single-Precision Math:** Strict 32-bit `float` math with `kPi` constants (no slow double-precision emulation on Cortex-M0+).
  - **Zero Dynamic Allocation:** All buffers statically sized at compile time (total engine memory footprint $\approx 140\text{ KB}$).
  - **Hardware Agnostic Core:** `DemonicEngine` depends only on the C++ standard library; zero dependencies on PortAudio or raylib.

---

## Signal Flow

```
Microphone In
    │
    ▼
[ Noise Gate ] ──► (Envelope Follower)
    │
    ▼
[ Chorus Doubler ]
    │
    ▼
[ Granular Pitch / Formant Shifter ]
    │
    ├─────────────────────────────┐
    ▼                             ▼
[ Sub-Octave Divider ]     [ Ring Modulator ]
    │                             │
    └──────────────┬──────────────┘
                   ▼
        [ Saturation & Wavefolder ]
                   │
                   ▼
        [ Decimation / Bitcrusher ]
                   │
                   ▼
        [ 1-Pole Tone Lowpass ]
                   │
                   ├─────────────────────────────┐
                   ▼                             ▼
        [ Feedback Delay (300 ms) ]      [ Spectral Freeze Drone ]
                   │                             │
                   └──────────────┬──────────────┘
                                  ▼
                     [ Catacomb Schroeder Reverb ]
                                  │
                                  ▼
                     [ DC Blocker & Soft Limiter ]
                                  │
                                  ▼
                             Speaker Out
```

---

## Getting Started

### Prerequisites

- **C++17 Compiler:** GCC, Clang, or Apple Clang (Xcode Command Line Tools).
- **CMake:** Version 3.13 or newer.
- **PortAudio:** Cross-platform audio I/O library.

#### macOS (Apple Silicon & Intel)
```bash
# Install tools and dependencies via Homebrew
brew install cmake portaudio
```

#### Linux (Debian / Ubuntu)
```bash
sudo apt update
sudo apt install build-essential cmake libportaudio2 portaudio19-dev libasound2-dev libgl1-mesa-dev libx11-dev
```

### Build

```bash
# Configure build (CMake downloads raylib automatically)
cmake -S . -B build

# Compile desktop application and smoke test
cmake --build build
```

### Run the Desktop Pedal App

```bash
./build/vocal_desktop
```

> **Safety Tip:** Always use **headphones** instead of laptop speakers to prevent acoustic feedback howl between microphone and speakers.

---

## Controls

| Key / Control | Action |
|---|---|
| **`P`** / Preset Button | Cycle between presets (`DEMONIC GROWL`, `MEGATRON`, `CYBER DEMON`) |
| **`F`** / Freeze Button | Toggle **Spectral Freeze** infinite sustain drone |
| **Mouse Drag** | Adjust any of the 11 parameter sliders live |
| **`Esc`** / Close Window | Safely close audio streams and exit |

---

## Running the Automated Test Suite

A headless automated smoke test verifies the DSP engine without requiring audio hardware or graphics:

```bash
# Run test suite
./build/engine_smoke_test
```

Expected output:
```
[PASS] silence in -> silence out
[PASS] preset 0 (DEMONIC GROWL): finite=yes, peak=0.99
[PASS] preset 1 (MEGATRON): finite=yes, peak=0.82
[PASS] preset 2 (CYBER DEMON): finite=yes, peak=0.75
[PASS] delay feedback 1.05 (60s test): finite=yes, peak=0.801 (bounded <= 1.0)
[PASS] sub-octave pitch tracking (110 Hz sine at various amplitudes): reliable tracking
[PASS] wavefolder extreme fold (drive 12dB, fold 5.0): peak=1.00 <= 1.0
[PASS] delay tap timing: sample 13230 = 300.0 ms (expected 300.0 ms)
[PASS] catacomb reverb: dense Schroeder diffusion (10988 diffuse samples, peak=0.05)
```

---

## Repository Structure

```
vocal-fx-processor/
├── CMakeLists.txt              # Cross-platform build configuration
├── README.md                   # Project overview and quickstart
├── USER_GUIDE.md               # Complete end-user pedal guide, controls & sound recipes
├── DEVELOPER_GUIDE.md          # Architectural deep-dive, DSP theory & Pico porting guide
├── include/
│   └── demonic_engine.hpp      # DemonicEngine interface, presets, and constants
├── src/
│   └── demonic_engine.cpp      # Pure DSP engine (100% hardware-agnostic C++17)
├── desktop/
│   └── main_desktop.cpp        # Desktop app: PortAudio callbacks & Raylib OLED UI
├── tests/
│   └── engine_smoke_test.cpp   # Headless DSP regression tests
└── pico/                       # Placeholder for RP2040 hardware firmware target
```

---

## Documentation

- **[USER_GUIDE.md](USER_GUIDE.md):** User instructions, slider parameter descriptions, sound recipes, and safety guidelines.
- **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md):** Audio programming concepts in C++, stage-by-stage DSP walkthrough, known architectural considerations, and guide for targeting the Raspberry Pi Pico.
