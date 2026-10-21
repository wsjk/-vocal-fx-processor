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
    float reverbMix; // 0.0 to 1.0
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
    void setReverbMix(float revMix);       // 0.0 to 1.0
    void setMix(float wetDry);             

    void loadPreset(int index);
    const char* getCurrentPresetName() const;
    int getPresetCount() const;

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
    float reverbMix;

    int currentPresetIndex;
    static const int NUM_PRESETS = 3;
    VocalPreset presets[NUM_PRESETS];

    float lastInput;
    bool subToggle;
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

    // Reverb / Echo delay line (larger buffer for cavernous reflections)
    float reverbBuffer[16384];
    size_t reverbWriteHead;

    float historyBuffer[512];
    size_t writeHead;
};
