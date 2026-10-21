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

    float delayLine[2048];
    size_t delayWriteHead;
    float lfoChorusPhase;

    float historyBuffer[512];
    size_t writeHead;
};
