#pragma once
#include <vector>
#include <cstdint>

struct VocalPreset {
    const char* name;
    float driveDb;
    float subBlend;
    float robotBlend;
    float wetDryMix;
};

class DemonicEngine {
public:
    DemonicEngine(float sampleRate);
    void processBlock(const float* input, float* output, size_t numFrames);
    
    // Control parameters
    void setFormantShift(float cents);     
    void setSubOctaveBlend(float blend);   
    void setDrive(float driveDb);          
    void setMix(float wetDry);             
    void setRobotBlend(float blend);       

    // Preset management
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

    int currentPresetIndex;
    static const int NUM_PRESETS = 3;
    VocalPreset presets[NUM_PRESETS];

    // DSP State Variables
    float lastInput;
    bool subToggle;
    float subFilterState;
    float dcInPrev;
    float dcOutPrev;
    float lowpassState;
    float ringModPhase;
    float ringModFreq;

    float historyBuffer[512];
    size_t writeHead;
};
