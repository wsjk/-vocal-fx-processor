# Vocal FX Processor — Known Issues & Fixes

Condensed from `docs/DEVELOPER_GUIDE.md` §10, which has the full analysis and measurements. Section numbers match that guide.

## OPEN — High Priority

None.

## OPEN — Medium / Low Priority

### 10.5 Delay/reverb timings disagree with comments; "reverb" is an echo
**Problem:** the taps read at `(head + offset) % size`, which is a delay of **size − offset**, not offset. The echo commented as "~300 ms" is actually 200 ms. The reverb taps are about 122 ms and 272 ms. The "reverb" is two feedback echoes, not a Schroeder/Freeverb design.
**Fix:** read at `(head + size − delaySamples) % size` and fix the comments. For a real reverb, use parallel combs and series allpasses with mutually prime lengths.

### 10.7 Cross-thread data not protected (low)
`scopeBuffer[]` is a plain array written by the audio thread and read by the UI. Cosmetic (a torn waveform at worst). The UI no longer calls `getCurrentPresetName()`; it uses `getPreset(guiActivePreset)`.

### 10.9 Freeze captures oldest audio, clicks at the seam (low)
The capture starts at the echo write head (the oldest data, about 0.41–0.5 s ago). The loop seam has no crossfade, which causes an 11 Hz flutter and a click. **Fix:** capture the most recent 4,096 samples and crossfade the seam.

### 10.10 Minor
- Parameters are applied per block with no smoothing, which causes zipper noise.
- The `int` `decimationCounter++` overflows after about 13.5 h. Wrap it modulo `decimationStep`.
- The noise gate has no hysteresis or release.
- Audio is mono only, on the default devices.
- `dsp_pipeline.*` are empty.
- The committed `dsp_test` binary and `project_dump.txt` are unused.
- `tests/engine_smoke_test.cpp` calls removed methods (`resetSubToggleCount`, `getSubToggleCount`) and is not built. Delete it or rewrite it and add it to CMake.

## RESOLVED

- **10.2** Delay feedback above 1.0 grew without limit. The loop now writes back through `tanhf`, and the output is clamped to ±1.
- **10.4** The wavefolder escaped ±1 (`fmod` sign, negative blend weight). It now uses a floored modulo and a clamped weight.
- **10.1** Presets were overwritten by the sliders, so they changed only the name. Fixed with `DemonicEngine::getPreset()` and `selectPreset()` in the desktop app, which writes the preset into the encoder atomics (`guiParam[]`) before publishing the index.
- **10.3** `setFormantShift` did nothing. There is now a real pitch shifter (`setPitchShift`), and `setFormantShift` forwards to it.
- **10.6** The sub-octave detector was unreliable. It was rewritten as a rising-edge Schmitt trigger on a 2-pole LPF, with level-relative hysteresis `max(0.004, 0.25 × env)`. This is tested from 110 to 440 Hz at amplitudes 0.1 to 0.9.
- **10.8** Build portability: `if(APPLE)` frameworks, PortAudio lookup (pkg-config → Homebrew paths → vendored copy), the engine as a library, `ctest`, and no `M_PI`. The desktop app builds on macOS. Windows and the Linux desktop build remain untested.
