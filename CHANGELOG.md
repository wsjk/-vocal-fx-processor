# Changelog

Notable changes to this project, newest first, grouped by the date they reached `main`. Each line ends with its pull-request number. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Every PR adds its lines under the heading for the day it merges, creating that heading at the top if it doesn't exist yet.

## 2026-11-15

### Added
- `CHANGELOG.md`, and a docs-check rule in `AGENTS.md`: before every commit or PR, go through `README.md`, this changelog and every `docs/*.md`. (#10)
- Hardware plan for the WM8731 codec: op-amp mic preamp, output buffer, a 12 MHz MCLK from the board's crystal on GP21, the core split, a latency table and a shopping list. Corrections to an outside hardware spec are in `docs/HARDWARE_PLAN.md` §7. (#9)
- `include/hw_pinout.hpp`, the planned RP2350B pin map, checked by the new `tests/hw_pinout_test.cpp` (registered with ctest). (#9)
- `docs/HARDWARE_PLAN.md`: board choice (pin budget, RAM, FPU; Pico, Pico 2 and Black Pill compared), parts, next steps. (#8)
- `pico/pico_sdk_import.cmake` is now committed, so there is no manual copy step. (#8)

### Changed
- README lists `docs/HARDWARE_PLAN.md` and this changelog under "Documentation" and "Repository Structure". (#10)
- The Pico build targets the **WeAct RP2350B Core** (`PICO_BOARD=weact_studio_rp2350b_core`) by default. `pico2` or `pico` still work. (#8)
- The benchmark reports the chip it runs on (RP2350 or RP2040). (#8)

## 2026-11-14

### Fixed
- Feedback delay writes back through `tanhf`, so feedback above 1.0 holds a steady level instead of growing without limit. (#7)
- Mixed output is clamped to ±1 before the bypass crossfade. The wavefolder stays within ±1. (#7)
- The Pico benchmark builds again (missing `<cmath>`). (#7)

### Changed
- The simulator is drawn as the pedal enclosure: 128×64 OLED, 11 endless encoders with LED rings, and PRESET / FREEZE / BYPASS footswitches. Choosing a preset turns the encoders. (#6)
- README, guides and the agent skill rewritten to match the code; stale root-level guides removed. (#5)

### Added
- Headless simulator layout and encoder tests (`tests/sim_controls_test.cpp`). (#6)
- Agent skill scaffold under `.agents/skills/vocal-fx-processor/`. (#4)

## 2026-11-06

### Added
- Pitch control, sub-octave rework, microphone input path and true bypass. (#3)

## 2026-10-30 – 2026-11-03

### Added
- First user and developer guides. (#1, #2)

## 2026-10-20

### Added
- First version: desktop build, GUI and pulsing OLED; DEMONIC GROWL, MEGATRON and CYBER DEMON presets; chorus widening and adjustable clank frequency; wavefolder; robotic stutter; feedback-delay "reverb"; Spectral Freeze.
