#pragma once
#include <cstddef>
#include <cstdint>

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
};

class DemonicEngine {
public:
    // Highest sample rate the fixed-size buffers are sized for: the Daisy Seed's 48 kHz. Every delay and
    // filter is set in seconds and converted for the actual rate, so 44.1 kHz and 48 kHz sound the same.
    // Above this rate the engine still runs safely, but the time-based effects get shorter.
    static constexpr float kMaxSampleRate = 48000.0f;

    DemonicEngine(float sampleRate);
    void processBlock(const float* input, float* output, size_t numFrames);
    
    // Pitch shift in cents (100 cents = 1 semitone). Range -1200..+1200. 0 = bypassed.
    void setPitchShift(float cents);
    // Kept for compatibility with older code: forwards to setPitchShift().
    // (The shifter moves pitch AND formants together, so "formant" was a misnomer.)
    void setFormantShift(float cents);
    void setSubOctaveBlend(float blend);   
    void setDrive(float driveDb);          
    void setRobotBlend(float blend);       
    void setRobotFreq(float freqHz);       
    void setWavefold(float foldDrive);     
    void setDecimation(float factor);      
    void setDelayFeedback(float feedback); // 0.0 to 1.05
    void setDelayMix(float mix);           // 0.0 to 1.0
    void setReverbMix(float revMix);       
    void setMix(float wetDry);             
    
    void setSpectralFreeze(bool freeze);   // Infinite Sustain Toggle
    bool isSpectralFrozen() const;

    // True bypass. After a ~5 ms crossfade (so switching does not click) the output is an exact copy
    // of the input and the whole effect chain stops running. Everything the effects remember (echo and
    // reverb tails, the freeze loop, filter memory) is cleared, and Freeze is switched off, so turning
    // bypass off again starts from a clean slate instead of replaying stale audio.
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
    static constexpr size_t kEchoBufSize    = 24000;   // 500 ms
    static constexpr size_t kFreezeBufSize  = 4459;    // ~93 ms (4096 samples at 44.1 kHz)
    static constexpr size_t kReverbBufSize  = 17834;   // ~372 ms (16384 samples at 44.1 kHz)

    float sampleRate;

    // Lengths and delays in samples for this sample rate (set once in the constructor).
    float pitchWindow;      // pitch shifter sweep length
    float chorusDelay;      // chorus centre delay
    float chorusSwing;      // chorus delay swing (+/-)
    size_t echoLen;         // feedback delay ring length
    size_t echoDelay;       // echo tap: how far behind the write head
    size_t freezeLen;       // freeze loop length
    size_t reverbLen;       // reverb ring length
    size_t reverbDelay1;    // reverb taps
    size_t reverbDelay2;

    // One-pole filter coefficients for this sample rate (tuned at 44.1 kHz; see rateCoef()).
    float envCoef;          // input envelope follower
    float pitchBlendCoef;   // pitch shifter on/off fade
    float subDetCoef;       // sub-octave detection low-pass
    float subAttackCoef;    // sub-octave detection envelope, rising
    float subReleaseCoef;   // ... falling
    float subFilterCoef;    // sub-octave square-wave smoothing
    float toneCoef;         // tone low-pass (stage 6)
    float dcPole;           // DC blocker pole (stage 10)

    float pitchCents;
    float subBlend;
    float driveDb;
    float wetDryMix;
    float robotBlend;
    float robotFreq;
    float wavefoldDrive;
    float decimationFactor;
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

    // Chorus delay line
    float delayLine[kChorusBufSize];
    size_t delayWriteHead;
    float lfoChorusPhase;

    // Feedback Delay Buffer (500 ms ring, 200 ms echo, for Echo / Infinite Repeat)
    float feedbackDelayBuffer[kEchoBufSize];
    size_t feedbackDelayWriteHead;

    // Spectral Freeze Buffer (~93 ms loop for Infinite Sustain Drone)
    float freezeBuffer[kFreezeBufSize];
    float freezeReadHead;

    // Reverb buffer
    float reverbBuffer[kReverbBufSize];
    size_t reverbWriteHead;

    // True bypass
    bool bypassTarget;          // what the caller asked for
    float bypassBlend;          // 0 = effects only ... 1 = dry only (ramps over ~5 ms)
    bool bypassStateCleared;    // effect memory has been wiped for this bypass period

    void resetState();          // wipe all effect memory (not the parameters)
};
