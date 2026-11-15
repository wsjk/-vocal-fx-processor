---
name: vocal-fx-processor
description: >-
  Development runbook for the Vocal FX Processor demonic vocal synthesis engine.
  Use when building, testing, modifying DSP stages, extending the desktop app
  or RP2040 firmware, adding presets or effects, or debugging the DemonicEngine pipeline.
---

# Vocal FX Processor Development Skill

Real-time "demonic" vocal effects engine in C++17. The chain is a noise gate, a pitch shifter, 10 numbered effect stages and a wet/dry mix, with true bypass. It runs in a desktop app (raylib + PortAudio), and there is an RP2040 benchmark target.

---

## ⚡ Ground Rules & Architecture

1. **Single precision only.** All maths is `float`. Use the `kPi` / `kTwoPi` constants and `f`-suffixed literals, and call `std::sin`, `std::tanh`, `std::abs` etc. on `float` arguments (or `sinf`/`tanhf`/`fabsf`). Never use `M_PI` or bare double literals.
2. **Zero dynamic allocation.** All buffers are fixed-size members of `DemonicEngine`, with no `new`, `malloc` or `std::vector`. `sizeof(DemonicEngine)` is 186,824 bytes, so it must be a global/static object, never a local.
3. **Hardware-agnostic core.** `demonic_engine.cpp` includes only `<cmath>`, `<algorithm>` and `<iterator>`, and the header only `<cstddef>` and `<cstdint>`. Do not add PortAudio, raylib or Pico SDK includes.
4. **Two threads (desktop).** The audio callback (real-time, no allocations) talks to the UI thread through `std::atomic` only. There are no mutexes in the audio path.
5. **`resetState()` must zero every piece of effect memory.** True bypass depends on it.
6. **`dsp_pipeline.hpp` / `dsp_pipeline.cpp` are empty stubs.** All active DSP lives in `demonic_engine.hpp/.cpp`.

---

## 🛠️ Common Workflows

### 1. Desktop Build & Run (macOS)
```bash
# Requires: brew install cmake portaudio pkg-config  (raylib 5.0 is downloaded on first configure)
.agents/skills/vocal-fx-processor/scripts/build_desktop.sh
./build/vocal_desktop
```

### 2. Headless DSP Tests (no audio hardware)
```bash
.agents/skills/vocal-fx-processor/scripts/build_tests.sh     # build-tests/ + ctest

# Direct compilation (quick iteration):
g++ -std=c++17 -O2 -Iinclude tests/engine_dsp_test.cpp src/demonic_engine.cpp -o dsp_test && ./dsp_test
```
Expect `ALL TESTS PASSED (0 failures)`. Only `engine_dsp_test` is built. `engine_smoke_test.cpp` is out of date and does not compile.

### 3. RP2040 Pico Benchmark Build
```bash
# One-time: the SDK import helper is not in the repo
cp "$PICO_SDK_PATH/external/pico_sdk_import.cmake" pico/
.agents/skills/vocal-fx-processor/scripts/build_pico.sh      # -> pico/build/vocal_pico_bench.uf2
```
The script puts the CMake and arm-none-eabi toolchains on `PATH` and defaults `PICO_SDK_PATH` to `/Users/ww/src/pico-sdk`. Flash by holding BOOTSEL and copying the `.uf2`, then read the results with `screen /dev/tty.usbmodem* 115200`.

---

## 🔧 Step-by-Step Recipes

### Add a New Effect Stage
1. Add state variables (buffers, filter memory) as **private members** in `include/demonic_engine.hpp`.
2. Add a public setter (`setMyParam(float)`) and a matching `float myParam` member.
3. Initialise the state in the constructor initialiser list, **in header declaration order**, and zero it again in `resetState()` in `src/demonic_engine.cpp`.
4. Insert the processing at the right point in the `processBlock()` sample loop, working on `wet`.
5. Wire it into the desktop app (see "Add a GUI Parameter").
6. Add a test in `tests/engine_dsp_test.cpp` using the `isolate()` + `render()` + `magnitude_at()`/`dominant_freq()` pattern. Use `block_has(n, t)` for mid-render switches, never `n == t`.

### Add a Preset
1. Increment `NUM_PRESETS` in `include/demonic_engine.hpp`.
2. Add a `presets[i] = {"NAME", drive, sub, robot, freq, fold, decim, delayFB, delayMix, revMix, mix};` line in the constructor.
3. Note that presets currently only change the displayed name (see known issue 10.1).

### Add a GUI Parameter (Desktop)
1. Add a `std::atomic<float> guiMyParam(default)` global in `desktop/main_desktop.cpp`.
2. Call `demonic.setMyParam(guiMyParam.load())` in `audioCallback`, after `setBypass` and before `setSpectralFreeze`.
3. In the UI loop, snapshot it (`float myVal = guiMyParam.load();`) and add `drawSlider("Label", myVal, min, max, guiMyParam);`.
4. Each slider takes 34 px. The 11 existing sliders run from y = 258 to about 632 in a 668 px window, so increase `screenHeight` first.

---

## 🔍 Key Symbols & Target Files

| Domain / Task | Target Files | Key Symbols / Notes |
| :--- | :--- | :--- |
| **DSP Engine** | `include/demonic_engine.hpp`, `src/demonic_engine.cpp` | `DemonicEngine`, `processBlock()`, `resetState()`, `setBypass()`, `VocalPreset`, `kPitchWindow`, `kPitchFade` |
| **Desktop App** | `desktop/main_desktop.cpp` | `audioCallback()`, `gui*` atomics, `drawSlider` lambda, `setBypassUi` lambda |
| **Pico Firmware** | `pico/main_pico.cpp`, `pico/CMakeLists.txt` | Benchmark only (`kScenarios`, `measureUsPerBlock`); no audio I/O yet |
| **DSP Tests** | `tests/engine_dsp_test.cpp` | `isolate()`, `render()`, `run_recorded()`, `block_has()`, `dominant_freq()`, `magnitude_at()` |
| **Stale test** | `tests/engine_smoke_test.cpp` | Calls removed `resetSubToggleCount()`/`getSubToggleCount()`; not in CMake |
| **Build System** | `CMakeLists.txt` | `demonic_engine` static lib; options `BUILD_DESKTOP`, `BUILD_TESTS`, `USE_SYSTEM_RAYLIB` |
| **Human docs** | `README.md`, `docs/USER_GUIDE.md`, `docs/DEVELOPER_GUIDE.md` | Update these when behaviour changes. The developer guide cites `file:line`. |

---

## ⚠️ Known Issues (from docs/DEVELOPER_GUIDE.md §10)

See [known_issues.md](./references/known_issues.md) for details.

- **Open, high priority:**
  - presets are overwritten by the sliders (10.1);
  - delay feedback above 1.0 is unbounded and there is no output limiter (10.2);
  - the wavefolder escapes ±1 because of the sign of `fmod` (10.4).
- **Open, medium/low priority:**
  - delay and reverb tap times are wrong because delay = size − offset, and the "reverb" is a two-tap echo (10.5);
  - cross-thread races on `currentPresetIndex` and `scopeBuffer` (10.7);
  - freeze captures the oldest audio and has a click at the loop seam (10.9).
- **Resolved:** `setFormantShift` is now an alias for `setPitchShift` (10.3), the sub-octave detector was rewritten as a Schmitt trigger (10.6), and build portability is fixed (10.8).

---

## 📚 Deep-Dive References
* DSP pipeline stage details & signal flow: [architecture.md](./references/architecture.md)
* Known issues & fixes: [known_issues.md](./references/known_issues.md)
