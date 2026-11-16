# Vocal FX Processor — Agent Instructions

Real-time "demonic" vocal effects engine in C++17. It has a noise gate, a pitch shifter and ten numbered effect stages, plus true bypass. It runs in a desktop app (raylib + PortAudio), and there is an RP2350 benchmark target (WeAct RP2350B Core).

## Skills

| Skill | Path | When to Use |
| :--- | :--- | :--- |
| **vocal-fx-processor** | [SKILL.md](.agents/skills/vocal-fx-processor/SKILL.md) | Building, testing, modifying DSP stages, extending the desktop app or RP2350 firmware, adding presets/effects, debugging `DemonicEngine`. |

## Quick Reference

- **Build desktop:** `.agents/skills/vocal-fx-processor/scripts/build_desktop.sh` (or `cmake -B build -S . && cmake --build build -j4`)
- **Run tests:** `.agents/skills/vocal-fx-processor/scripts/build_tests.sh` (builds into `build-tests/` and runs `ctest`: `engine_dsp_test`, `sim_controls_test`, `hw_pinout_test`)
- **Build Daisy:** `.agents/skills/vocal-fx-processor/scripts/build_daisy.sh` (benchmark for the Daisy Seed, the planned board; needs libDaisy at `/Users/ww/src/libDaisy` or `LIBDAISY_DIR`; `program-dfu` to flash)
- **Build Pico:** `.agents/skills/vocal-fx-processor/scripts/build_pico.sh` (WeAct RP2350B Core by default; `PICO_BOARD=pico2` or `pico` to change). Hardware decisions: `docs/HARDWARE_PLAN.md`; pin map: `include/hw_pinout.hpp`
- **Human docs:** [README.md](README.md) and every file in [docs/](docs/) ([USER_GUIDE.md](docs/USER_GUIDE.md), [DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md), [HARDWARE_PLAN.md](docs/HARDWARE_PLAN.md))

## Rules

- All DSP math must be single-precision `float`. Call `std::sin`/`std::tanh`/`std::abs` etc. on `float` arguments (the float overloads), or `sinf`/`tanhf`/`fabsf`. Never use `M_PI` or unsuffixed double literals; use `kPi` / `kTwoPi` and `1.0f`-style literals.
- Zero dynamic allocation in `DemonicEngine`. All buffers are fixed-size class members.
- Any new effect state must be zeroed in both the constructor and `resetState()`, or bypass will not clear it.
- `dsp_pipeline.hpp` / `dsp_pipeline.cpp` are empty stubs. All active code is in `demonic_engine.hpp/.cpp`.
- `tests/engine_smoke_test.cpp` is out of date (it calls removed methods) and is not built. Add tests to `tests/engine_dsp_test.cpp`.
- Desktop audio↔UI communication uses `std::atomic` only. No mutexes in the audio callback.
- **Docs check before every commit or PR.** Run `ls docs/*.md`, then go through `README.md` and every file listed; update any that the change makes wrong or incomplete. In particular:
  - `docs/DEVELOPER_GUIDE.md`: keep its `file:line` references pointing at the right code.
  - `docs/USER_GUIDE.md`: controls, presets and behaviour the user sees.
  - `docs/HARDWARE_PLAN.md`: board, codec, pins (`include/hw_pinout.hpp`) and parts.
  - `README.md`: a new or renamed file goes in "Repository Structure", and a new doc also goes in "Documentation".
  - There is no changelog file. Each merge to `main` is released automatically, and the release notes are the merged PR titles, so write the PR title as a release-note line. Label the PR `minor` for a new feature or behaviour change, or `major` for a breaking one; otherwise it is a patch release (see Developer Guide §7).
  Say in the PR description which docs you updated, or that none needed it. For a full audit, use the `docs-drift-check` skill.
