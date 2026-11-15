# Vocal FX Processor — Agent Instructions

Real-time "demonic" vocal effects engine in C++17. It has a noise gate, a pitch shifter and ten numbered effect stages, plus true bypass. It runs in a desktop app (raylib + PortAudio), and there is an RP2350 benchmark target (WeAct RP2350B Core).

## Skills

| Skill | Path | When to Use |
| :--- | :--- | :--- |
| **vocal-fx-processor** | [SKILL.md](.agents/skills/vocal-fx-processor/SKILL.md) | Building, testing, modifying DSP stages, extending the desktop app or RP2350 firmware, adding presets/effects, debugging `DemonicEngine`. |

## Quick Reference

- **Build desktop:** `.agents/skills/vocal-fx-processor/scripts/build_desktop.sh` (or `cmake -B build -S . && cmake --build build -j4`)
- **Run tests:** `.agents/skills/vocal-fx-processor/scripts/build_tests.sh` (builds into `build-tests/` and runs `ctest`: `engine_dsp_test`, `sim_controls_test`, `hw_pinout_test`)
- **Build Pico:** `.agents/skills/vocal-fx-processor/scripts/build_pico.sh` (WeAct RP2350B Core by default; `PICO_BOARD=pico2` or `pico` to change). Hardware decisions: `docs/HARDWARE_PLAN.md`; pin map: `include/hw_pinout.hpp`
- **Human docs:** [README.md](README.md), [docs/USER_GUIDE.md](docs/USER_GUIDE.md), [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md)

## Rules

- All DSP math must be single-precision `float`. Call `std::sin`/`std::tanh`/`std::abs` etc. on `float` arguments (the float overloads), or `sinf`/`tanhf`/`fabsf`. Never use `M_PI` or unsuffixed double literals; use `kPi` / `kTwoPi` and `1.0f`-style literals.
- Zero dynamic allocation in `DemonicEngine`. All buffers are fixed-size class members.
- Any new effect state must be zeroed in both the constructor and `resetState()`, or bypass will not clear it.
- `dsp_pipeline.hpp` / `dsp_pipeline.cpp` are empty stubs. All active code is in `demonic_engine.hpp/.cpp`.
- `tests/engine_smoke_test.cpp` is out of date (it calls removed methods) and is not built. Add tests to `tests/engine_dsp_test.cpp`.
- Desktop audio↔UI communication uses `std::atomic` only. No mutexes in the audio callback.
- When you change behaviour, update `docs/DEVELOPER_GUIDE.md` (including its `file:line` references) and `docs/USER_GUIDE.md`.
