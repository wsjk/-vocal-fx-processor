---
name: vocal-fx-processor
description: >-
  Development runbook for the Vocal FX Processor demonic vocal synthesis engine.
  Use when building, testing, modifying DSP stages, extending the desktop app
  or RP2040 firmware, adding presets or effects, or debugging the DemonicEngine pipeline.
---

# Vocal FX Processor Development Skill

Real-time "demonic" vocal synthesis engine in C++17: 12-stage DSP pipeline, desktop app (raylib + PortAudio), and RP2040 firmware target.

---

## ⚡ Ground Rules & Architecture

1. **Single-Precision Only:** All math uses `float` (no `double`). Use `kPi` float constants, `sinf`/`tanhf`/`fabsf` — never `sin`/`tanh`/`fabs`.
2. **Zero Dynamic Allocation:** All buffers are fixed-size members of `DemonicEngine`. No `new`, `malloc`, `std::vector` in the engine.
3. **Hardware-Agnostic Core:** `DemonicEngine` depends only on `<cstddef>`, `<cstdint>`, `<cmath>`, `<cstring>`, `<algorithm>`. No PortAudio, raylib, or Pico SDK.
4. **Two Threads (Desktop):** Audio callback (real-time, no allocations) communicates with the UI thread via `std::atomic` only. No mutexes in the audio path.
5. **`dsp_pipeline.hpp` / `dsp_pipeline.cpp` are empty stubs.** All active DSP lives in `demonic_engine.hpp/.cpp`.

---

## 🛠️ Common Workflows

### 1. Desktop Build & Run (macOS)
```bash
# Requires: brew install cmake portaudio
cmake -B build -S . -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j4
./build/vocal_desktop
```

### 2. Headless DSP Tests (no audio hardware)
```bash
# CMake path:
cmake -B build -S . -DBUILD_DESKTOP=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j4
ctest --test-dir build --output-on-failure

# Direct compilation (quick iteration):
g++ -std=c++17 -O2 -Iinclude tests/engine_dsp_test.cpp src/demonic_engine.cpp -o dsp_test && ./dsp_test
```

### 3. RP2040 Pico Firmware Build
```bash
export PATH="/Applications/CMake.app/Contents/bin:/Users/ww/tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin:$PATH"
export PICO_SDK_PATH="/Users/ww/src/pico-sdk"
cmake -S pico -B pico/build -DCMAKE_BUILD_TYPE=Release
cmake --build pico/build -j4
# Output: pico/build/vocal_pico_bench.uf2
```

---

## 🔧 Step-by-Step Recipes

### Add a New Effect Stage
1. Add state variables (buffers, filter memory) as **private members** in `include/demonic_engine.hpp`.
2. Add a public setter (`setMyParam(float)`) and corresponding `float myParam` member.
3. Initialize state in the constructor and in `resetState()` in `src/demonic_engine.cpp`.
4. Insert processing logic at the correct pipeline position in `processBlock()`.
5. Add an `std::atomic<float>` in `desktop/main_desktop.cpp` and wire it in the audio callback + UI slider.
6. Add a test case in `tests/engine_dsp_test.cpp` using the `isolate()` + `render()` pattern.

### Add a Preset
1. Increment `NUM_PRESETS` in `include/demonic_engine.hpp`.
2. Add the `VocalPreset{...}` initializer in the constructor in `src/demonic_engine.cpp`.

### Add a GUI Parameter (Desktop)
1. Add `std::atomic<float> guiMyParam(default)` global in `desktop/main_desktop.cpp`.
2. Call `demonic.setMyParam(guiMyParam.load())` in `audioCallback`.
3. Add a `GuiSliderBar(...)` call in the UI loop, storing into `guiMyParam`.

---

## 🔍 Key Symbols & Target Files

| Domain / Task | Target Files | Key Symbols / Notes |
| :--- | :--- | :--- |
| **DSP Engine** | `include/demonic_engine.hpp`, `src/demonic_engine.cpp` | `DemonicEngine`, `processBlock()`, `VocalPreset` |
| **Desktop App** | `desktop/main_desktop.cpp` | `audioCallback()`, `std::atomic` GUI params, raylib UI |
| **Pico Firmware** | `pico/main_pico.cpp`, `pico/CMakeLists.txt` | Benchmark-only for now; no audio I/O yet |
| **DSP Tests** | `tests/engine_dsp_test.cpp` | `isolate()`, `render()`, `dominant_freq()`, `magnitude_at()` |
| **Smoke Tests** | `tests/engine_smoke_test.cpp` | Basic init, preset, processBlock assertions |
| **Build System** | `CMakeLists.txt` | `demonic_engine` static lib, `BUILD_DESKTOP`, `BUILD_TESTS` |

---

## ⚠️ Known Issues (from DEVELOPER_GUIDE.md §10)

See detailed analysis: [known_issues.md](./references/known_issues.md)

Critical: preset values immediately overwritten by sliders, delay feedback > 1.0 unbounded, wavefolder escapes ±1. Medium: `setFormantShift` was a misnomer (now forwards to `setPitchShift`), sub-octave detector unreliable, reverb is really a single comb echo.

---

## 📚 Deep-Dive References
* DSP pipeline stage details & signal flow: [architecture.md](./references/architecture.md)
* Known issues & fixes: [known_issues.md](./references/known_issues.md)
