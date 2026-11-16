#pragma once
#include <cstddef>
#include <cstdint>
#include "Effects/reverbsc.h"   // DaisySP-LGPL (LGPL-2.1): the reverb, stage 9

struct VocalPreset {
    const char* name;
    float driveDb;
    float subBlend;
    float robotBlend;
    float robotFreq;
    float wavefoldDrive;
    float decimationFactor;
    float delayFeedback; // 0.0 to 1.05 (Infinite Repeat)
    float delayMix;      // 0.0 to 1.0
    float reverbMix;
    float wetDryMix;
    float delayTime;     // seconds, 0.02 to 2.0
    float harmonyCents;  // harmony voice, relative to the pitch shift
    float harmonyBlend;  // 0 = off
};

// The engine's long buffers, kept outside DemonicEngine so firmware can put them in big, slow RAM
// (on the Daisy Seed, the 64 MB SDRAM) while the engine itself stays in fast SRAM. Each is touched only
// once or twice per sample. The caller owns it, must keep it alive as long as the engine, and must not
// share one between two engines. About 576 KB, so make it a global or static, never a local.
struct DemonicEngineMemory {
    static constexpr size_t kEchoSize   = 96064;   // 2 s echo at 48 kHz, plus margin
    static constexpr size_t kFreezeSize = 48960;   // 1 s freeze loop plus its 20 ms crossfade, at 48 kHz
    float echo[kEchoSize];
    float freeze[kFreezeSize];
};

class DemonicEngine {
public:
    // Highest sample rate the fixed-size buffers are sized for: the Daisy Seed's 48 kHz. Every delay and
    // filter is set in seconds and converted for the actual rate, so 44.1 kHz and 48 kHz sound the same.
    // Above this rate the engine still runs safely, but the time-based effects get shorter.
    static constexpr float kMaxSampleRate = 48000.0f;
    static constexpr float kMinDelayTime = 0.02f;   // seconds
    static constexpr float kMaxDelayTime = 2.0f;

    // The constructor clears `memory`, so on the Daisy Seed construct the engine after hw.Init() has
    // started the SDRAM.
    DemonicEngine(float sampleRate, DemonicEngineMemory& memory);
    DemonicEngine(const DemonicEngine&) = delete;
    DemonicEngine& operator=(const DemonicEngine&) = delete;

    void processBlock(const float* input, float* output, size_t numFrames);

    // Pitch shift in cents (100 cents = 1 semitone). Range -1200..+1200. 0 = bypassed.
    void setPitchShift(float cents);
    // Kept for compatibility with older code: forwards to setPitchShift().
    // (The shifter moves pitch AND formants together, so "formant" was a misnomer.)
    void setFormantShift(float cents);
    // Harmony: a second shifted voice, `cents` away from the pitch-shifted voice (+700 = a fifth above),
    // mixed in at `blend` (0 = off, 1 = as loud as the main voice). Range -1200..+1200 cents.
    void setHarmony(float cents, float blend);
    void setSubOctaveBlend(float blend);
    void setDrive(float driveDb);
    void setRobotBlend(float blend);
    void setRobotFreq(float freqHz);
    void setWavefold(float foldDrive);
    void setDecimation(float factor);
    void setDelayTime(float seconds);      // kMinDelayTime..kMaxDelayTime; changes glide, like tape
    void setDelayFeedback(float feedback); // 0.0 to 1.05
    void setDelayMix(float mix);           // 0.0 to 1.0
    void setReverbMix(float revMix);
    void setMix(float wetDry);

    // Infinite Sustain: loops the last second of the echo input, with a crossfade at the loop point.
    void setSpectralFreeze(bool freeze);
    bool isSpectralFrozen() const;

    // True bypass. After a ~5 ms crossfade (so switching does not click) the output is an exact copy
    // of the input and the whole effect chain stops running. Everything the effects remember (echo and
    // reverb tails, the freeze loop, filter memory) is cleared, and Freeze is switched off, so turning
    // bypass off again starts from a clean slate instead of replaying stale audio. The long buffers are
    // cleared a slice per block (about 30 ms in all), so no single block overruns its deadline.
    void setBypass(bool bypass);
    bool isBypassed() const;

    void loadPreset(int index);
    const char* getCurrentPresetName() const;
    int getPresetCount() const;
    // Preset data (read-only, never changes after construction), so the UI can move its controls to a preset.
    const VocalPreset& getPreset(int index) const { return presets[((index % NUM_PRESETS) + NUM_PRESETS) % NUM_PRESETS]; }

private:
    // Buffer capacities, in samples at kMaxSampleRate. The lengths actually used are the *Len members.
    static constexpr size_t kPitchBufSize   = 2048;    // > pitch sweep (40 ms) + margin
    static constexpr size_t kChorusBufSize  = 2048;    // > chorus delay (10 ms + 1.5 ms swing)

    float sampleRate;
    DemonicEngineMemory& mem;

    // Lengths and delays in samples for this sample rate (set once in the constructor).
    float pitchWindow;      // pitch shifter sweep length
    float chorusDelay;      // chorus centre delay
    float chorusSwing;      // chorus delay swing (+/-)
    size_t echoLen;         // feedback delay ring length
    size_t freezeLoopLen;   // freeze loop length (1 s)
    size_t freezeFadeLen;   // crossfade at the loop point (20 ms)
    size_t freezeRingLen;   // freezeLoopLen + freezeFadeLen

    // One-pole filter coefficients for this sample rate (tuned at 44.1 kHz; see rateCoef()).
    float envCoef;          // input envelope follower
    float pitchBlendCoef;   // pitch shifter (and harmony) on/off fade
    float subDetCoef;       // sub-octave detection low-pass
    float subAttackCoef;    // sub-octave detection envelope, rising
    float subReleaseCoef;   // ... falling
    float subFilterCoef;    // sub-octave square-wave smoothing
    float toneCoef;         // tone low-pass (stage 6)
    float dcPole;           // DC blocker pole (stage 10)
    float delayGlideCoef;   // echo time glide

    float pitchCents;
    float harmonyCents;
    float harmonyBlend;
    float subBlend;
    float driveDb;
    float wetDryMix;
    float robotBlend;
    float robotFreq;
    float wavefoldDrive;
    float decimationFactor;
    float delayTime;
    float delayFeedback;
    float delayMix;
    float reverbMix;
    bool spectralFreezeActive;

    int currentPresetIndex;
    static const int NUM_PRESETS = 3;
    VocalPreset presets[NUM_PRESETS];

    // Sub-octave: Schmitt-trigger octave divider (see processBlock stage 2)
    float subDetLP1;       // 2-pole low-pass of the voice, used only for pitch detection
    float subDetLP2;
    float subDetEnv;       // envelope of the detection signal (sets hysteresis / silence gate)
    bool subArmed;         // true once the detector has swung negative and may fire again
    bool subToggle;
    float subFilterState;
    float dcInPrev;
    float dcOutPrev;
    float lowpassState;
    float ringModPhase;

    float heldSample;
    int decimationCounter;
    float inputEnvelope;

    // Pitch shifter: two crossfaded, sweeping delay taps
    float pitchBuffer[kPitchBufSize];
    size_t pitchWriteHead;
    float pitchPhase;      // 0..1, position of tap 1 within the sweep
    float pitchBlend;      // 0 = bypass, 1 = fully shifted (smoothed to avoid clicks)
    float harmonyPhase;    // the harmony voice's own sweep, over the same buffer
    float harmonyLevel;    // harmonyBlend, smoothed

    // Chorus delay line
    float delayLine[kChorusBufSize];
    size_t delayWriteHead;
    float lfoChorusPhase;

    // Feedback delay (Echo / Infinite Repeat): ring in mem.echo
    size_t feedbackDelayWriteHead;
    float echoDelaySamples;    // current echo delay, gliding towards delayTime

    // Spectral Freeze: mem.freeze is a ring that records the echo input until Freeze is pressed, then
    // plays back as a loop
    size_t freezeWriteHead;
    size_t freezeStart;    // oldest sample in the ring when Freeze was pressed
    size_t freezePos;      // playback position in the loop, 0..freezeLoopLen

    // Reverb (stage 9): DaisySP's ReverbSc, ~396 KB, kept in the engine because it is read 8 times
    // per sample
    daisysp::ReverbSc reverb;
    bool reverbOk;         // false if ReverbSc could not fit this sample rate (the stage is skipped)

    // True bypass
    bool bypassTarget;          // what the caller asked for
    float bypassBlend;          // 0 = effects only ... 1 = dry only (ramps over ~5 ms)
    bool bypassStateCleared;    // effect memory has been wiped for this bypass period
    size_t clearCursor;         // how much of the long buffers is cleared so far (see clearLongBuffers)

    float pitchTap(float phase) const;   // read the pitch buffer at sweep position `phase`, crossfaded
    void resetState();                   // wipe the short effect memory and the reverb (not the parameters)
    // Clear up to `maxSamples` more of mem.echo and mem.freeze. Returns true when all of it is clear.
    bool clearLongBuffers(size_t maxSamples);
};
