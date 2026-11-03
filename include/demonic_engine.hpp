#pragma once
#include <vector>
#include <cstdint>

struct VocalPreset {
    const char* name;
    float driveDb;
    float subBlend;
    float robotBlend;
    float robotFreq;
    float wavefoldDrive;
    float decimationFactor;
    float delayFeedback;
    float delayMix;
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
    void setDelayFeedback(float feedback); 
    void setDelayMix(float mix);           
    void setReverbMix(float revMix);       
    void setMix(float wetDry);             
    
    void setSpectralFreeze(bool freeze);   
    bool isSpectralFrozen() const;

    void loadPreset(int index);
    const char* getCurrentPresetName() const;
    int getPresetCount() const;

    // Test accessors
    void resetSubToggleCount() { subToggleCount = 0; }
    int getSubToggleCount() const { return subToggleCount; }

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

    // DSP State & Buffers
    float lastInput;
    bool subToggle;
    int subToggleCount;
    float subFilterState;
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

    // Feedback Delay Buffer (~500ms)
    float feedbackDelayBuffer[22050];
    size_t feedbackDelayWriteHead;

    // Spectral Freeze Buffer
    float freezeBuffer[4096];
    float freezeReadHead;

    // Catacomb Reverb Buffer
    float reverbBuffer[16384];
    size_t reverbWriteHead;
};
