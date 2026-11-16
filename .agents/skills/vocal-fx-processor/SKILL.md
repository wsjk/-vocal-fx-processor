---
name: vocal-fx-processor
description: >-
  Development runbook for the Vocal FX Processor demonic vocal synthesis engine.
  Use when building, testing, modifying DSP stages, extending the desktop app
  or RP2350 firmware, adding presets or effects, or debugging the DemonicEngine pipeline.
---

# Vocal FX Processor Development Skill

Real-time "demonic" vocal effects engine in C++17. The chain is a noise gate, a pitch shifter, 10 numbered effect stages and a wet/dry mix, with true bypass. It runs in a desktop app (raylib + PortAudio), and there is an RP2350 benchmark target (WeAct RP2350B Core).

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
g++ -std=c++17 -Iinclude tests/sim_controls_test.cpp src/demonic_engine.cpp -o sim_test && ./sim_test
```
Expect `ALL TESTS PASSED (0 failures)` from each. `engine_dsp_test`, `sim_controls_test` and `hw_pinout_test` are built. `engine_smoke_test.cpp` is out of date and does not compile.

### 3. Daisy Seed Benchmark Build (the planned board)
```bash
.agents/skills/vocal-fx-processor/scripts/build_daisy.sh              # -> daisy/build/vocal_daisy_bench.bin
.agents/skills/vocal-fx-processor/scripts/build_daisy.sh program-dfu  # also flash (Seed in DFU mode: hold BOOT, press RESET)
```
The script puts arm-none-eabi on `PATH`, defaults `LIBDAISY_DIR` to `/Users/ww/src/libDaisy`, and builds libDaisy once if `build/libdaisy.a` is missing. `daisy/Makefile` includes libDaisy's `core/Makefile` (C++17, `-O3`, `APP_TYPE=BOOT_NONE`: the program must fit the 128 KB internal flash). Flashing needs `dfu-util`. The program waits for the USB serial port to open; read it with `screen /dev/tty.usbmodem* 115200`. The scenarios and timing loop are shared with the Pico benchmark in `include/engine_bench.hpp`.

### 4. RP2350 Benchmark Build (WeAct RP2350B Core, previous board)
```bash
.agents/skills/vocal-fx-processor/scripts/build_pico.sh      # -> pico/build/vocal_pico_bench.uf2
PICO_BOARD=pico .agents/skills/vocal-fx-processor/scripts/build_pico.sh   # original RP2040 Pico instead (fresh pico/build)
```
The script puts the CMake and arm-none-eabi toolchains on `PATH` and defaults `PICO_SDK_PATH` to `/Users/ww/src/pico-sdk`. `pico/CMakeLists.txt` defaults `PICO_BOARD` to `weact_studio_rp2350b_core` (Arm Cortex-M33 cores, FPU). Flash by holding BOOT and copying the `.uf2` to the `RP2350` drive (or use the `pico-hw-test` skill), then read the results with `screen /dev/tty.usbmodem* 115200`. Board choice, WM8731 audio design, core split and parts: `docs/HARDWARE_PLAN.md`; pin map: `include/hw_pinout.hpp` (checked by `tests/hw_pinout_test.cpp`).

---

## 🔧 Step-by-Step Recipes

### Add a New Effect Stage
1. Add state variables (buffers, filter memory) as **private members** in `include/demonic_engine.hpp`.
2. Add a public setter (`setMyParam(float)`) and a matching `float myParam` member.
3. Initialise the state in the constructor initialiser list, **in header declaration order**, and zero it again in `resetState()` in `src/demonic_engine.cpp`.
4. Insert the processing at the right point in the `processBlock()` sample loop, working on `wet`.
5. Wire it into the desktop app (see "Add an Encoder").
6. Add a test in `tests/engine_dsp_test.cpp` using the `isolate()` + `render()` + `magnitude_at()`/`dominant_freq()` pattern. Use `block_has(n, t)` for mid-render switches, never `n == t`.

### Add a Preset
1. Increment `NUM_PRESETS` in `include/demonic_engine.hpp`.
2. Add a `presets[i] = {"NAME", drive, sub, robot, freq, fold, decim, delayFB, delayMix, revMix, mix};` line in the constructor.
3. Loading it turns the encoders (`selectPreset` in `desktop/main_desktop.cpp`, via `simctl::preset_values`). A new preset field also needs a line in `simctl::preset_values`.

### Add an Encoder (Desktop simulator)
1. In `include/sim_controls.hpp`, add `P_MY_PARAM` to `simctl::Param` inside its group's run, and a matching `spec()` row (label, printf format, display scale, min, max, step per click, bipolar, warn_above, group). Order of the enum = order of the table.
2. Call `demonic.setMyParam(guiParam[P_MY_PARAM].load())` in `audioCallback`, after `setBypass` and before `setSpectralFreeze`.
3. Give it a start value in `main()` next to the pitch one, or add it to presets and `simctl::preset_values`.
4. Drawing, hit-testing and turning are automatic: `Layout::make()` spaces each group's encoders across its panel. VOICE has room for one more; MANGLE and SPACE hold four each. Run `ctest`; `sim_controls_test` fails if anything overlaps.
5. Layout rules mirror the Phase Box simulator (`raylib-hardware-sim` skill): geometry and value logic stay free of raylib so they are unit-tested; only `main_desktop.cpp` draws.

---

## 🔍 Key Symbols & Target Files

| Domain / Task | Target Files | Key Symbols / Notes |
| :--- | :--- | :--- |
| **DSP Engine** | `include/demonic_engine.hpp`, `src/demonic_engine.cpp` | `DemonicEngine`, `processBlock()`, `resetState()`, `setBypass()`, `VocalPreset`, `kPitchWindow`, `kPitchFade` |
| **Desktop App** | `desktop/main_desktop.cpp` | `audioCallback()`, `guiParam[]` atomics, `selectPreset()`, `render_oled()`, `draw_encoder()`, `toggleBypass` lambda |
| **Simulator logic** | `include/sim_controls.hpp`, `include/sim_layout.hpp`, `tests/sim_controls_test.cpp` | `simctl::spec()`, `norm`/`from_norm`/`step`, `preset_values`; `simlayout::Layout::make()`, `KnobDrag` |
| **Pico Firmware** | `pico/main_pico.cpp`, `pico/CMakeLists.txt` | Benchmark only (`kScenarios`, `measureUsPerBlock`); no audio I/O yet |
| **DSP Tests** | `tests/engine_dsp_test.cpp` | `isolate()`, `render()`, `run_recorded()`, `block_has()`, `dominant_freq()`, `magnitude_at()` |
| **Stale test** | `tests/engine_smoke_test.cpp` | Calls removed `resetSubToggleCount()`/`getSubToggleCount()`; not in CMake |
| **Build System** | `CMakeLists.txt` | `demonic_engine` static lib; options `BUILD_DESKTOP`, `BUILD_TESTS`, `USE_SYSTEM_RAYLIB` |
| **Human docs** | `README.md`, every `docs/*.md` (`USER_GUIDE.md`, `DEVELOPER_GUIDE.md`, `HARDWARE_PLAN.md`) | Check all of them before every commit or PR (see the docs-check rule in `AGENTS.md`). The developer guide cites `file:line`. No changelog file: the PR title becomes the release note, and a `minor`/`major` label sets the version bump. |

---

## ⚠️ Known Issues (from docs/DEVELOPER_GUIDE.md §10)

See [known_issues.md](./references/known_issues.md) for details.

- **Open, medium/low priority:**
  - delay and reverb tap times are wrong because delay = size − offset, and the "reverb" is a two-tap echo (10.5);
  - cross-thread race on `scopeBuffer` (10.7);
  - freeze captures the oldest audio and has a click at the loop seam (10.9).
- **Resolved:** presets now turn the encoders (10.1), feedback is bounded and the output clamped to ±1 (10.2), the wavefolder stays within ±1 (10.4), `setFormantShift` is now an alias for `setPitchShift` (10.3), the sub-octave detector was rewritten as a Schmitt trigger (10.6), and build portability is fixed (10.8).

---

## 📚 Deep-Dive References
* DSP pipeline stage details & signal flow: [architecture.md](./references/architecture.md)
* Known issues & fixes: [known_issues.md](./references/known_issues.md)
