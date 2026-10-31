#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

struct VocalPreset {
    const char* name;
    float formantCents;  // Pitch / formant shift in cents (-1200 to +1200)
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

    void loadPreset(int index);
    const char* getCurrentPresetName() const;
    int getPresetCount() const;
    const VocalPreset& getPreset(int index) const;

    // Diagnostic accessors
    int getSubToggleCount() const { return subToggleCounter; }
    void resetSubToggleCount() { subToggleCounter = 0; }

private:
    float sampleRate;
    float formantCents;
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

    bool subToggle;
    bool subArmed;
    float subLpState;
    float subFilterState;
    int subToggleCounter;

    float dcInPrev;
    float dcOutPrev;
    float lowpassState;
    float ringModPhase;
    
    float heldSample;
    int decimationCounter;
    float inputEnvelope;

    // Chorus delay line
    float delayLine[2048];
    size_t delayWriteHead;
    float lfoChorusPhase;

    // Granular pitch / formant shift buffer
    static constexpr size_t PITCH_BUFFER_SIZE = 2048;
    float pitchBuffer[PITCH_BUFFER_SIZE];
    size_t pitchWriteHead;
    float pitchPhasor;

    // Feedback Delay Buffer (~500ms for Echo / Infinite Repeat)
    float feedbackDelayBuffer[22050];
    size_t feedbackDelayWriteHead;

    // Spectral Freeze Buffer (~4096 samples for Infinite Sustain Drone)
    float freezeBuffer[4096];
    float freezeReadHead;

    // Catacomb Reverb (Schroeder reverberator: 4 parallel comb filters + 2 series allpass diffusers)
    static constexpr size_t COMB1_LEN = 1553;
    static constexpr size_t COMB2_LEN = 1699;
    static constexpr size_t COMB3_LEN = 1787;
    static constexpr size_t COMB4_LEN = 1973;
    static constexpr size_t AP1_LEN = 347;
    static constexpr size_t AP2_LEN = 113;

    float comb1[COMB1_LEN];
    float comb2[COMB2_LEN];
    float comb3[COMB3_LEN];
    float comb4[COMB4_LEN];
    size_t comb1Head;
    size_t comb2Head;
    size_t comb3Head;
    size_t comb4Head;

    float ap1[AP1_LEN];
    float ap2[AP2_LEN];
    size_t ap1Head;
    size_t ap2Head;
};
