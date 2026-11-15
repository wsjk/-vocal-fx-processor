# Vocal FX Processor — DSP Architecture Reference

## Signal Flow (12 stages in `processBlock()`)

```
Mic In → [1. Noise Gate + Envelope] → [2. Chorus Doubler] → [3. Granular Pitch Shifter]
  → [4. Sub-Octave Divider] ─┬─ [5. Ring Modulator] ─┘
                              └───────┬────────────────┘
                                      ▼
  [6. Saturation + Wavefolder] → [7. Decimator/Bitcrusher] → [8. 1-Pole Tone LPF]
  → [9. Spectral Freeze] ─┬─ [10. Feedback Delay 300ms] ─┘
                           └───────┬───────────────────────┘
                                   ▼
  [11. Catacomb Reverb (4 comb + 2 allpass)] → [12. DC Blocker + Soft Limiter] → Out
```

## Engine Memory Layout (~190 KB static)

| Buffer | Size | Purpose |
| :--- | :--- | :--- |
| `pitchBuffer[2048]` | 8 KB | Granular pitch shifter delay taps |
| `delayLine[2048]` | 8 KB | Chorus LFO-modulated delay |
| `feedbackDelayBuffer[22050]` | 86 KB | 500 ms feedback echo / infinite repeat |
| `freezeBuffer[4096]` | 16 KB | Spectral freeze drone loop |
| `reverbBuffer[16384]` | 64 KB | Schroeder comb/allpass reverb |

## Key DSP Techniques

### Schmitt Trigger Sub-Octave (Stage 4)
- 2-pole lowpass extracts fundamental from voice
- Hysteresis thresholds at ±0.025 of detection envelope prevent chatter
- `subToggle` flips on each positive zero-crossing → half-frequency square wave
- Result smoothed by `subFilterState` 1-pole LPF before mixing

### Granular Pitch Shifter (Stage 3)
- Two delay taps sweep through `pitchBuffer[2048]` at rate determined by `pitchCents`
- Hann window crossfade between taps (phase offset by 0.5) prevents clicks
- `pitchBlend` smoothly ramps 0→1 when `pitchCents != 0`, 1→0 when bypassed

### True Bypass with Crossfade
- `bypassBlend` ramps 0↔1 over ~5 ms (220 samples at 44.1 kHz)
- At blend = 1.0, `resetState()` clears all buffers/filter memory
- Freeze is force-disabled during bypass

## Thread Safety Model (Desktop)

```
UI Thread (raylib loop)              Audio Thread (PortAudio callback)
─────────────────────               ──────────────────────────────────
guiDrive.store(val)     ──atomic──►  demonic.setDrive(guiDrive.load())
guiPitch.store(val)     ──atomic──►  demonic.setPitchShift(guiPitch.load())
...                                  demonic.processBlock(in, out, 64)
                        ◄──atomic──  currentMicPeak.store(peak)
                        ◄──atomic──  scopeBuffer[] (relaxed, cosmetic)
```

No mutexes. All parameter transfer via `std::atomic<float>` / `std::atomic<bool>`.
The `scopeBuffer[]` write is technically a race (non-atomic array) but is cosmetic-only.

## Presets (3 built-in)

| Index | Name | Character |
| :--- | :--- | :--- |
| 0 | DEMONIC GROWL | Heavy drive + sub-octave + wavefold + delay |
| 1 | MEGATRON | Robot ring-mod + decimation + high reverb |
| 2 | CYBER DEMON | Full pipeline: pitch shift + all effects |
