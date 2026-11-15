# Vocal FX Processor — DSP Architecture Reference

Authoritative source: `src/demonic_engine.cpp`. The full walkthrough is in `docs/DEVELOPER_GUIDE.md` §6.

## Signal Flow (`processBlock()` per-sample loop, in code order)

```
dry = input[i]
 → [gate]  activeSignal = |dry| > 0.02 ? dry : 0;  inputEnvelope one-pole (a = 0.1)
 → [0b] Pitch shifter (skipped when |cents| < 1 and pitchBlend has faded out)  → voice
 → [1]  Chorus: widenedVoice = voice + 0.3 × delayLine tap (441 ± 66 samples, 0.5 Hz LFO)
 → [2]  Sub-octave: detector runs on `voice`; wet = widenedVoice + subOctave
 → [3]  Ring mod: carrier robotFreq + 40 × inputEnvelope Hz, blended by robotBlend (×1.5)
 → [4]  tanh(wet × 10^(driveDb/20)); wavefold if wavefoldDrive > 0.1
 → [5]  Sample-and-hold by int(decimationFactor), then round to 1/32 steps
 → [6]  Tone one-pole LPF (a = 0.35, ~3 kHz)
 → [7]  Spectral freeze (if on): wet = 0.3 × wet + 0.7 × freezeBuffer loop
 → [8]  Feedback echo: tap offset 13230 in 22050 (= ~200 ms), feedback delayFeedback, mix delayMix
 → [9]  "Catacomb reverb": taps offset 4410 / 11025 in 16384 (~122 / ~272 ms), weights 0.6/0.4, fb 0.55
 → [10] DC blocker (y = x − x₋₁ + 0.995·y₋₁)
 → processed = dry × (1 − wetDryMix) + wet × wetDryMix      (dry is UNGATED)
 → output = processed × (1 − bypassBlend) + dry × bypassBlend
```

There is **no** output limiter and **no** saturation in the echo loop.

## Engine Memory Layout (`sizeof(DemonicEngine)` = 186,824 bytes ≈ 182 KB)

| Buffer | Size | Duration @44.1k | Purpose |
| :--- | :--- | :--- | :--- |
| `pitchBuffer[2048]` | 8 KB | 46 ms | Pitch shifter (uses ≤ 1,768 samples) |
| `delayLine[2048]` | 8 KB | 46 ms | Chorus |
| `feedbackDelayBuffer[22050]` | 86 KB | 500 ms | Echo; also the source of the Freeze capture |
| `freezeBuffer[4096]` | 16 KB | 93 ms | Freeze loop |
| `reverbBuffer[16384]` | 64 KB | 371 ms | Two-tap feedback "reverb" |

## Key DSP Techniques

### Pitch shifter (stage 0b)
- `ratio = 2^(cents/1200)`, and `pitchPhase += (1 − ratio) / kPitchWindow` (with `kPitchWindow = 1764`, i.e. 40 ms), wrapped to [0, 1).
- The main tap reads at delay `2 + phase × 1764` with linear interpolation. A second tap half a sweep away is mixed in **only** during the first and last `kPitchFade = 10 %` of the sweep, using a raised-cosine crossfade. A constant 50/50 blend would cancel the fundamental.
- `pitchBlend` eases 0↔1 (coefficient 0.002, about 10 ms) when pitch moves to or from 0.
- It shifts formants along with pitch. Expect a sweep-rate flutter of `|1 − ratio| × 44100 / 1764` Hz.

### Schmitt-trigger sub-octave (stage 2)
- Detection runs on a 2-pole low-pass of `voice` (`subDetLP1/2`, a = 0.08 each).
- `subDetEnv` has a fast attack (0.02) and a slow release (0.0005). The detector is gated off while `subDetEnv ≤ 0.006`.
- Hysteresis is `h = max(0.004, 0.25 × subDetEnv)`, so it is relative to level. When armed and the signal goes above +h, `subToggle` flips and the detector disarms. It re-arms when the signal goes below −h. That is one flip per cycle, giving f/2.
- The square wave is smoothed by `subFilterState` (a = 0.04, about 290 Hz) and scaled by `inputEnvelope × subBlend × 1.8`.

### Spectral freeze (stage 7)
- On the off→on transition, `setSpectralFreeze(true)` copies 4,096 samples from `feedbackDelayBuffer`, starting at the write head (the **oldest** data, about 0.41–0.5 s ago).
- It loops with no seam crossfade, at about 10.8 Hz. The request is refused while bypassed.

### True bypass
- `bypassBlend` ramps linearly 0↔1 over 5 ms (220 samples at 44.1 kHz) in the final line of the loop.
- Once `bypassTarget && bypassBlend >= 1`, `processBlock` copies input to output exactly and returns early. On the first such block it calls `resetState()` (guarded by `bypassStateCleared`).
- `setBypass(true)` also clears `spectralFreezeActive`.

## Thread Safety Model (Desktop)

```
UI Thread (raylib loop, 60 fps)      Audio Thread (PortAudio callback, 64 frames)
───────────────────────────          ───────────────────────────────────────────
selectPreset(i): guiParam[] then guiActivePreset ─atomic──►  loadPreset(i) on change (index only)
guiBypass.store(b)       ─atomic──►  demonic.setBypass(...)        ← first
guiParam[P_PITCH … P_MIX] ─atomic──► demonic.setPitchShift(...) … setMix(...)
guiSpectralFreeze        ─atomic──►  demonic.setSpectralFreeze(...) ← last
                                     demonic.processBlock(in, out, 64)
                         ◄─atomic──  currentMicPeak.store(peak)
                         ◄─atomic──  scopeHead (scopeBuffer[] itself is a plain array)
```

There are no mutexes. One known benign race exists: `scopeBuffer[]`. The UI reads preset data only through `getPreset()`, which is constant after construction.

## Presets (3 built-in; constructor of `DemonicEngine`)

| Index | Name | Drive | Sub | Robot | Freq | Fold | Decim | DelayFB | DelayMix | Rev | Mix |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | DEMONIC GROWL | 11 | 0.85 | 0.00 | 45 | 1.0 | 1.0 | 0.60 | 0.35 | 0.45 | 0.85 |
| 1 | MEGATRON | 6 | 0.20 | 0.95 | 75 | 3.0 | 3.5 | 0.40 | 0.20 | 0.20 | 0.90 |
| 2 | CYBER DEMON | 10 | 0.70 | 0.65 | 55 | 2.5 | 2.0 | 0.75 | 0.40 | 0.50 | 0.85 |

Presets do not include pitch. In the desktop app, loading one turns the encoders to its values (`selectPreset`).
