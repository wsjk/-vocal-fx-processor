# Vocal FX Processor — Agent Instructions

Real-time "demonic" vocal effects engine in C++17. It has a noise gate, a pitch shifter with a harmony voice and ten numbered effect stages, plus true bypass. It runs in a desktop app (raylib + PortAudio), and the hardware target is the Electrosmith Daisy Seed (firmware and benchmark in `daisy/`, not yet run on a board). The reverb is DaisySP-LGPL's ReverbSc (LGPL-2.1), fetched by CMake.

## Skills

| Skill | Path | When to Use |
| :--- | :--- | :--- |
| **vocal-fx-processor** | [SKILL.md](.agents/skills/vocal-fx-processor/SKILL.md) | Building, testing, modifying DSP stages, extending the desktop app or Daisy Seed firmware, adding presets/effects, debugging `DemonicEngine`. |

## Quick Reference

- **Build desktop:** `.agents/skills/vocal-fx-processor/scripts/build_desktop.sh` (or `cmake -B build -S . && cmake --build build -j4`)
- **Run tests:** `.agents/skills/vocal-fx-processor/scripts/build_tests.sh` (builds into `build-tests/` and runs `ctest`: `engine_dsp_test`, `sim_controls_test`, `hw_pinout_test`)
- **Build Daisy:** `.agents/skills/vocal-fx-processor/scripts/build_daisy.sh` (benchmark and firmware for the Daisy Seed; needs libDaisy at `/Users/ww/src/libDaisy` or `LIBDAISY_DIR`, and DaisySP with submodules at `/Users/ww/src/DaisySP` or `DAISYSP_SRC_DIR`; `program-dfu` to flash the benchmark, `APP=firmware` for the firmware). Hardware decisions: `docs/HARDWARE_PLAN.md`; pin map: `include/hw_pinout.hpp`
- **CI:** every PR runs the host tests and both Daisy builds, and fails if a `.bin` is over 128 KB (`.github/workflows/ci.yml`). CI's gcc 13 builds 2–5 % bigger than the local gcc 15. libDaisy notes: `.agents/skills/vocal-fx-processor/references/daisy_firmware.md`
- **DaisySP for CMake:** downloaded at a pinned commit, or offline `-DFETCHCONTENT_SOURCE_DIR_DAISYSP=/path/to/DaisySP` (the build scripts pass `/Users/ww/src/DaisySP` when it exists)
- **Human docs:** [README.md](README.md) and every file in [docs/](docs/) ([USER_GUIDE.md](docs/USER_GUIDE.md), [DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md), [HARDWARE_PLAN.md](docs/HARDWARE_PLAN.md))

## Rules

- All DSP math must be single-precision `float`. Call `std::sin`/`std::tanh`/`std::abs` etc. on `float` arguments (the float overloads), or `sinf`/`tanhf`/`fabsf`. Never use `M_PI` or unsuffixed double literals; use `kPi` / `kTwoPi` and `1.0f`-style literals.
- Zero dynamic allocation in `DemonicEngine`. All buffers are fixed size: short ones are class members, the long echo/freeze buffers live in the caller-owned `DemonicEngineMemory` (SDRAM on the Daisy Seed).
- Never hard-code a sample count or per-sample coefficient: write it at 44.1 kHz and convert it in the constructor (`rateCoef()`), so 44.1 and 48 kHz sound the same.
- Any new effect state must be zeroed in both the constructor and `resetState()` (or, for `DemonicEngineMemory`, in `clearLongBuffers()`), or bypass will not clear it.
- `dsp_pipeline.hpp` / `dsp_pipeline.cpp` are empty stubs. All active code is in `demonic_engine.hpp/.cpp`.
- `tests/engine_smoke_test.cpp` is out of date (it calls removed methods) and is not built. Add tests to `tests/engine_dsp_test.cpp`.
- Desktop audio↔UI communication uses `std::atomic` only. No mutexes in the audio callback.
- **Docs check before every commit or PR.** Grep `README.md` and `docs/*.md` for every file name, symbol, control, pin, constant and number the change touches, and read the matching sections, not whole files. Read a whole doc only when the change adds a feature or reworks the area that doc covers. Update anything the change makes wrong or incomplete. In particular:
  - `docs/DEVELOPER_GUIDE.md`: keep its `file:line` references pointing at the right code.
  - `docs/USER_GUIDE.md`: controls, presets and behaviour the user sees.
  - `docs/HARDWARE_PLAN.md`: board, codec, pins (`include/hw_pinout.hpp`) and parts.
  - `README.md`: a new or renamed file goes in "Repository Structure", and a new doc also goes in "Documentation".
  - There is no changelog file. Each merge to `main` is released automatically, and the release notes are the merged PR titles, so write the PR title as a release-note line. Label the PR `minor` for a new feature or behaviour change, or `major` for a breaking one; otherwise it is a patch release (see Developer Guide §7).
  Say in the PR description which docs you updated, or that none needed it. For a full audit, use the `docs-drift-check` skill.
