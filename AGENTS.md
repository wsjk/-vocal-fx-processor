# Vocal FX Processor — Agent Instructions

Real-time "demonic" vocal synthesis engine in C++17 with a 12-stage DSP pipeline, desktop GUI (raylib + PortAudio), and RP2040 firmware target.

## Skills

| Skill | Path | When to Use |
| :--- | :--- | :--- |
| **vocal-fx-processor** | [SKILL.md](.agents/skills/vocal-fx-processor/SKILL.md) | Building, testing, modifying DSP stages, extending the desktop app or RP2040 firmware, adding presets/effects, debugging `DemonicEngine`. |

## Quick Reference

- **Build desktop:** `cmake -B build -S . && cmake --build build -j4`
- **Run tests:** `.agents/skills/vocal-fx-processor/scripts/build_tests.sh`
- **Build Pico:** `.agents/skills/vocal-fx-processor/scripts/build_pico.sh`

## Rules

- All DSP math must use `float` (never `double`). Use `sinf`/`tanhf`/`fabsf`.
- Zero dynamic allocation in `DemonicEngine`. All buffers are fixed-size class members.
- `dsp_pipeline.hpp` / `dsp_pipeline.cpp` are empty stubs — all active code is in `demonic_engine.hpp/.cpp`.
- Desktop audio↔UI communication uses `std::atomic` only. No mutexes in the audio callback.
