#pragma once
#include <vector>
#include <cstdint>

class DemonicEngine {
public:
    DemonicEngine(float sampleRate);
    void processBlock(const float* input, float* output, size_t numFrames);
    
    // Control parameters
    void setFormantShift(float cents);     // -1200 to 0
    void setSubOctaveBlend(float blend);   // 0.0 to 1.0
    void setDrive(float driveDb);          // 0.0 to 12.0
    void setMix(float wetDry);             // 0.0 (Dry) to 1.0 (Wet)

private:
    float sampleRate;
    float formantCents;
    float subBlend;
    float driveDb;
    float wetDryMix;

    float historyBuffer[512];
    size_t writeHead;
};
