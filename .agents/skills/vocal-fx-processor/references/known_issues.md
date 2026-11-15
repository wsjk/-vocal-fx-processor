# Vocal FX Processor — Known Issues & Fixes

From `DEVELOPER_GUIDE.md` §10. Severity and verification method noted.

## HIGH Priority

### 10.1 Presets immediately overwritten by sliders
**Problem:** `loadPreset()` sets engine parameters, but on the very next audio callback the GUI
atomics (which still hold the old slider positions) overwrite them.
**Fix:** When a preset is loaded, push preset values back into the `guiXxx` atomics so the sliders
snap to the preset's values. The audio callback should only call setters when the slider actually
changes, or always push preset → atomics on load.

### 10.2 Delay feedback > 1.0 grows without limit
**Problem:** `delayFeedback` goes up to 1.05 ("Infinite Repeat") but nothing clamps the delay
buffer contents — it grows exponentially until float overflow.
**Fix:** Apply `tanhf()` soft saturation to the feedback sample before writing it back into
`feedbackDelayBuffer`. This allows self-oscillation without digital clipping.

### 10.4 Wavefolder escapes ±1 range
**Problem:** The Euclidean wavefolding formula can produce values outside ±1 for certain
combinations of input amplitude and fold drive.
**Fix:** Clamp or apply a final `tanhf()` after the fold stage. Alternatively, use a bounded
triangle-wave fold: `4.0f * fabsf(fmodf(x * 0.25f + 0.25f, 1.0f) - 0.5f) - 1.0f`.

## MEDIUM Priority

### 10.3 `setFormantShift` / `formantCents` misnomer
**Status:** Already fixed — `setFormantShift()` now forwards to `setPitchShift()`.
The shifter moves pitch AND formants together (granular, not vocoder-based).

### 10.5 Comments/constants disagree with actual timings
**Problem:** Comments say "300 ms delay" but buffer is 22050 samples = 500 ms at 44.1 kHz.
The "reverb" is a single feedback comb filter, not a true Schroeder reverb (despite the README).
**Fix:** Either expand to a real 4-comb + 2-allpass Schroeder, or update docs to say "echo".

### 10.6 Sub-octave detector unreliable
**Problem:** Schmitt trigger thresholds are absolute (±0.025) rather than relative to the
detection envelope, causing false triggers on quiet signals and missed triggers on loud ones.
**Fix:** Scale thresholds by `subDetEnv` (the detection envelope): `threshold = 0.025f * subDetEnv`.

## LOW Priority

### 10.7 Cross-thread `scopeBuffer` not protected
Cosmetic-only race on the scope display array. Acceptable for visualization.

### 10.9 Freeze captures at arbitrary phase
Freezing grabs whatever is in `freezeBuffer` at the moment — may include a click if not at
a zero-crossing. A refinement would wait for a zero-crossing before latching.
