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

private:
    float sampleRate;
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

    // Pitch shifter: two crossfaded, sweeping delay taps (2048 samples >= window + margin)
    float pitchBuffer[2048];
    size_t pitchWriteHead;
    float pitchPhase;      // 0..1, position of tap 1 within the sweep
    float pitchBlend;      // 0 = bypass, 1 = fully shifted (smoothed to avoid clicks)

    // Chorus delay line
    float delayLine[2048];
    size_t delayWriteHead;
    float lfoChorusPhase;

    // Feedback Delay Buffer (~500ms for Echo / Infinite Repeat)
    float feedbackDelayBuffer[22050];
    size_t feedbackDelayWriteHead;

    // Spectral Freeze Buffer (~4096 samples for Infinite Sustain Drone)
    float freezeBuffer[4096];
    float freezeReadHead;

    // Reverb buffer
    float reverbBuffer[16384];
    size_t reverbWriteHead;

    // True bypass
    bool bypassTarget;          // what the caller asked for
    float bypassBlend;          // 0 = effects only ... 1 = dry only (ramps over ~5 ms)
    bool bypassStateCleared;    // effect memory has been wiped for this bypass period

    void resetState();          // wipe all effect memory (not the parameters)
};
