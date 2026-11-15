# Vocal FX Processor — Known Issues & Fixes

Condensed from `docs/DEVELOPER_GUIDE.md` §10, which has the full analysis and measurements. Section numbers match that guide.

## OPEN — High Priority

### 10.1 Presets immediately overwritten by sliders
**Problem:** `audioCallback` calls `loadPreset()` when `guiActivePreset` changes, and then, in the same callback, every `set…()` call re-applies the slider atomics. The preset therefore changes only the name. The atomics' defaults also differ from preset 0 (`guiRobotFreq` is 55 vs 45, `guiWavefold` is 1.5 vs 1.0).
**Fix:** add `const VocalPreset& getPreset(int) const` to the engine. On the UI thread, write the preset's fields into the `gui*` atomics (a `selectPreset(idx)` helper), and call it from `P`, from the preset button and once at startup.

### 10.2 Delay feedback > 1.0 grows without limit; no output limiter
**Problem:** `feedbackDelayBuffer[w] = wet + delayed * delayFeedback` has no bound. At 1.05 the peak reaches about 500,000 within 60 s. Nothing clamps the final output either.
**Mitigation:** a bypass on/off cycle flushes the loop (this is tested).
**Fix:** `feedbackDelayBuffer[w] = std::tanh(wet + delayed * delayFeedback);` plus a final `std::clamp(output[i], -1.0f, 1.0f)` or a soft limiter.

### 10.4 Wavefolder escapes ±1
**Problem:** `std::fmod` keeps the sign of its first argument, so `abs(fmod(x + 1, 4) − 2) − 1` is not bounded for negative input. The blend weight `1 − 0.4 × fold` also goes negative above fold 2.5. With a 0.5-amplitude 220 Hz sine, MEGATRON peaks at about 3.4 and CYBER DEMON at about 2.2.
**Fix:** use a floored modulo, `x -= 4.0f * std::floor(x / 4.0f)`, followed by `folded = std::fabs(x − 2) − 1`, and clamp the weight with `k = std::min(0.4f × fold, 1.0f)`.

## OPEN — Medium / Low Priority

### 10.5 Delay/reverb timings disagree with comments; "reverb" is an echo
**Problem:** the taps read at `(head + offset) % size`, which is a delay of **size − offset**, not offset. The echo commented as "~300 ms" is actually 200 ms. The reverb taps are about 122 ms and 272 ms. The "reverb" is two feedback echoes, not a Schroeder/Freeverb design.
**Fix:** read at `(head + size − delaySamples) % size` and fix the comments. For a real reverb, use parallel combs and series allpasses with mutually prime lengths.

### 10.7 Cross-thread data not protected (low)
`scopeBuffer[]` is a plain array, and the UI's `getCurrentPresetName()` reads a non-atomic `currentPresetIndex`. Both are cosmetic.

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

- **10.3** `setFormantShift` did nothing. There is now a real pitch shifter (`setPitchShift`), and `setFormantShift` forwards to it.
- **10.6** The sub-octave detector was unreliable. It was rewritten as a rising-edge Schmitt trigger on a 2-pole LPF, with level-relative hysteresis `max(0.004, 0.25 × env)`. This is tested from 110 to 440 Hz at amplitudes 0.1 to 0.9.
- **10.8** Build portability: `if(APPLE)` frameworks, PortAudio lookup (pkg-config → Homebrew paths → vendored copy), the engine as a library, `ctest`, and no `M_PI`. The desktop app builds on macOS. Windows and the Linux desktop build remain untested.
