# Vocal FX Processor — DSP Architecture Reference

Authoritative source: `src/demonic_engine.cpp`. The full walkthrough is in `docs/DEVELOPER_GUIDE.md` §6.

## Signal Flow (`processBlock()` per-sample loop, in code order)

Lengths and coefficients below are the 44.1 kHz values. The constructor converts them for the actual rate
(`rateCoef()` keeps each one-pole's time constant), so 48 kHz on the Daisy Seed sounds the same.

```
dry = input[i]
 → [gate]  activeSignal = |dry| > 0.02 ? dry : 0;  inputEnvelope one-pole (a = 0.1)
 → [0b] Pitch shifter (skipped when |cents| < 1 and pitchBlend has faded out)  → voice
 → [0c] Harmony: voice += pitchTap(harmonyPhase) × harmonyLevel, at pitchCents + harmonyCents
 → [1]  Chorus: widenedVoice = voice + 0.3 × delayLine tap (441 ± 66 samples, 0.5 Hz LFO)
 → [2]  Sub-octave: detector runs on `voice`; wet = widenedVoice + subOctave
 → [3]  Ring mod: carrier robotFreq + 40 × inputEnvelope Hz, blended by robotBlend (×1.5)
 → [4]  tanh(wet × 10^(driveDb/20)); wavefold if wavefoldDrive > 0.1
 → [5]  Sample-and-hold every int(decimationFactor) samples at 44.1 kHz (decimationPhase), then round to 1/32 steps
 → [6]  Tone one-pole LPF (a = 0.35, ~3 kHz)
 → [7]  Spectral freeze (if on): wet = 0.3 × wet + 0.7 × 1 s loop from mem.freeze (20 ms seam crossfade)
 → [8]  Feedback echo: linear-interpolated tap at echoDelaySamples (glides to delayTime, 20 ms–2 s),
        mem.echo[head] = tanhf(wet + delayed × delayFeedback); mem.freeze records the same value unless frozen
 → [9]  Reverb: DaisySP-LGPL ReverbSc (feedback 0.85, damping LPF 5 kHz), mono in, left out, mix reverbMix
 → [10] DC blocker (y = x − x₋₁ + 0.995·y₋₁)
 → processed = dry × (1 − wetDryMix) + wet × wetDryMix      (dry is UNGATED)
 → output = processed × (1 − bypassBlend) + dry × bypassBlend
```

The echo loop saturates through `tanhf`, and the final output is clamped to ±1.

## Engine Memory Layout

`DemonicEngine` itself (fast RAM; AXI SRAM on the Daisy Seed):

| Member | Size | Purpose |
| :--- | :--- | :--- |
| `pitchBuffer[2048]` | 8 KB | Pitch shifter and harmony (sweep ≤ 1,920 samples at 48 kHz) |
| `delayLine[2048]` | 8 KB | Chorus |
| `reverb` (`daisysp::ReverbSc`) | ~396 KB | Reverb: 8 delay lines, read every sample, so it stays in fast RAM |

`DemonicEngineMemory` (caller-owned; SDRAM on the Daisy Seed, a global on the desktop), about 576 KB:

| Member | Size | Purpose |
| :--- | :--- | :--- |
| `echo[96064]` | 375 KB | Echo ring, 2 s at 48 kHz |
| `freeze[48960]` | 191 KB | Freeze ring: records the echo input; when frozen, 1 s loop + 20 ms crossfade |

Bypass clears these with `clearLongBuffers()`, 64 samples per processed frame, so no block overruns.

## Key DSP Techniques

### Pitch shifter (stage 0b)
- `ratio = 2^(cents/1200)`, and `pitchPhase += (1 − ratio) / pitchWindow` (`pitchWindow` = `kPitchWindowSec` 40 ms = 1,764 samples at 44.1 kHz), wrapped to [0, 1).
- `pitchTap(phase)`: the main tap reads at delay `2 + phase × pitchWindow` with linear interpolation. A second tap half a sweep away is mixed in **only** during the first and last `kPitchFade = 10 %` of the sweep, using a raised-cosine crossfade. A constant 50/50 blend would cancel the fundamental.
- `pitchBlend` eases 0↔1 (coefficient 0.002, about 10 ms) when pitch moves to or from 0.
- It shifts formants along with pitch. Expect a sweep-rate flutter of `|1 − ratio| / 0.04 s` Hz.
- Harmony (stage 0c) runs its own `harmonyPhase` through `pitchTap()` over the same buffer, at `2^((pitchCents + harmonyCents)/1200)`, faded by `harmonyLevel` with the same coefficient.

### Schmitt-trigger sub-octave (stage 2)
- Detection runs on a 2-pole low-pass of `voice` (`subDetLP1/2`, a = 0.08 each).
- `subDetEnv` has a fast attack (0.02) and a slow release (0.0005). The detector is gated off while `subDetEnv ≤ 0.006`.
- Hysteresis is `h = max(0.004, 0.25 × subDetEnv)`, so it is relative to level. When armed and the signal goes above +h, `subToggle` flips and the detector disarms. It re-arms when the signal goes below −h. That is one flip per cycle, giving f/2.
- The square wave is smoothed by `subFilterState` (a = 0.04, about 290 Hz) and scaled by `inputEnvelope × subBlend × 1.8`.

### Spectral freeze (stage 7)
- `mem.freeze` is a ring of `freezeLoopLen + freezeFadeLen` samples (1 s + 20 ms) that records the echo input while not frozen.
- `setSpectralFreeze(true)` copies nothing: it notes the ring's oldest sample (`freezeStart`) and stops recording. The loop is the newest 1 s; its last 20 ms fade into the 20 ms recorded just before the loop's start, so the seam is continuous. The request is refused while bypassed.

### Echo (stage 8)
- `setDelayTime(s)` clamps to `kMinDelayTime`..`kMaxDelayTime` (0.02–2 s). `echoDelaySamples` glides towards it (coefficient 0.0002, ~110 ms), so turning ECHO TIME bends the echoes' pitch instead of clicking.

### True bypass
- `bypassBlend` ramps linearly 0↔1 over 5 ms (220 samples at 44.1 kHz) in the final line of the loop.
- Once `bypassTarget && bypassBlend >= 1`, `processBlock` copies input to output exactly and returns early. On the first such block it calls `resetState()` (guarded by `bypassStateCleared`), which zeros the short buffers and re-`Init`s ReverbSc; every bypassed block then clears another slice of the long buffers. If bypass is released before that finishes, the rest is cleared at once.
- `setBypass(true)` also clears `spectralFreezeActive`.

## Thread Safety Model (Desktop)

```
UI Thread (raylib loop, 60 fps)      Audio Thread (PortAudio callback, 64 frames)
───────────────────────────          ───────────────────────────────────────────
selectPreset(i): guiParam[] then guiActivePreset ─atomic──►  loadPreset(i) on change (index only)
guiBypass.store(b)       ─atomic──►  demonic.setBypass(...)        ← first
guiParam[P_PITCH … P_MIX] ─atomic──► demonic.setPitchShift(...) … setDelayTime(...) … setMix(...)
guiSpectralFreeze        ─atomic──►  demonic.setSpectralFreeze(...) ← last
                                     demonic.processBlock(in, out, 64)
                         ◄─atomic──  currentMicPeak.store(peak)
                         ◄─atomic──  scopeHead (scopeBuffer[] itself is a plain array)
```

There are no mutexes. One known benign race exists: `scopeBuffer[]`. The UI reads preset data only through `getPreset()`, which is constant after construction.

## Presets (3 built-in; constructor of `DemonicEngine`)

| Index | Name | Drive | Sub | Robot | Freq | Fold | Decim | DelayFB | DelayMix | Rev | Mix | Echo time | Harmony |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | DEMONIC GROWL | 11 | 0.85 | 0.00 | 45 | 1.0 | 1.0 | 0.60 | 0.35 | 0.45 | 0.85 | 0.2 s | off |
| 1 | MEGATRON | 6 | 0.20 | 0.95 | 75 | 3.0 | 3.5 | 0.40 | 0.20 | 0.20 | 0.90 | 0.2 s | off |
| 2 | CYBER DEMON | 10 | 0.70 | 0.65 | 55 | 2.5 | 2.0 | 0.75 | 0.40 | 0.50 | 0.85 | 0.2 s | +700 cents, 0.4 |

Presets do not include pitch. Harmony has no encoder; only presets (and `setHarmony()`) set it. In the desktop app, loading one turns the encoders to its values (`selectPreset`).
