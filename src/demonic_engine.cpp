#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>

DemonicEngine::DemonicEngine(float sr) 
    : sampleRate(sr), formantCents(-500.0f), currentPresetIndex(0),
      lastInput(0.0f), subToggle(false), subFilterState(0.0f), 
      dcInPrev(0.0f), dcOutPrev(0.0f), lowpassState(0.0f), 
      ringModPhase(0.0f), ringModFreq(45.0f), writeHead(0) {
    std::fill(std::begin(historyBuffer), std::end(historyBuffer), 0.0f);

    // Initialize Factory Presets
    presets[0] = {"DEMONIC GROWL", 11.0f, 0.85f, 0.0f, 0.85f};
    presets[1] = {"MEGATRON",       6.0f,  0.20f, 0.95f, 0.90f};
    presets[2] = {"CYBER DEMON",    10.0f, 0.70f, 0.55f, 0.85f};

    // Load default preset
    loadPreset(0);
}

void DemonicEngine::setFormantShift(float cents) { formantCents = cents; }
void DemonicEngine::setSubOctaveBlend(float blend) { subBlend = blend; }
void DemonicEngine::setDrive(float db) { driveDb = db; }
void DemonicEngine::setMix(float mix) { wetDryMix = mix; }
void DemonicEngine::setRobotBlend(float blend) { robotBlend = blend; }

void DemonicEngine::loadPreset(int index) {
    currentPresetIndex = index % NUM_PRESETS;
    driveDb = presets[currentPresetIndex].driveDb;
    subBlend = presets[currentPresetIndex].subBlend;
    robotBlend = presets[currentPresetIndex].robotBlend;
    wetDryMix = presets[currentPresetIndex].wetDryMix;
}

const char* DemonicEngine::getCurrentPresetName() const {
    return presets[currentPresetIndex].name;
}

int DemonicEngine::getPresetCount() const {
    return NUM_PRESETS;
}

void DemonicEngine::processBlock(const float* input, float* output, size_t numFrames) {
    float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);
    const float noiseGateThreshold = 0.02f;
    float phaseInc = (2.0f * M_PI * ringModFreq) / sampleRate;

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        // 1. Input Noise Gate
        float activeSignal = (std::abs(dry) > noiseGateThreshold) ? dry : 0.0f;

        // 2. Zero-Crossing Sub-Octave Generator
        if ((activeSignal > 0.0f && lastInput <= 0.0f) || (activeSignal < 0.0f && lastInput >= 0.0f)) {
            if (std::abs(activeSignal) > noiseGateThreshold) {
                subToggle = !subToggle;
            }
        }
        lastInput = activeSignal;

        float rawSub = subToggle ? 1.0f : -1.0f;
        subFilterState = subFilterState + 0.04f * (rawSub - subFilterState);
        float subOctave = subFilterState * std::abs(activeSignal) * subBlend * 1.8f;

        // 3. Blend Dry Voice + Sub-Bass
        float wet = activeSignal + subOctave;

        // 4. Ring Modulation (Robot Clank)
        float lfo = std::sin(ringModPhase);
        ringModPhase += phaseInc;
        if (ringModPhase >= 2.0f * M_PI) {
            ringModPhase -= 2.0f * M_PI;
        }
        
        float roboticSignal = wet * lfo;
        wet = (wet * (1.0f - robotBlend)) + (roboticSignal * robotBlend * 1.5f);

        // 5. Asymmetrical Saturation & Bitcrush Grit
        wet = std::tanh(wet * gainMultiplier);
        if (wet < 0.0f) {
            wet *= 1.3f;
        }
        wet = std::tanh(wet);

        const float quantizationLevels = 32.0f; 
        wet = std::round(wet * quantizationLevels) / quantizationLevels;

        // 6. Post-Saturation Tone Shaping (High-Cut)
        lowpassState = lowpassState + 0.35f * (wet - lowpassState);
        wet = lowpassState;

        // 7. DC Blocker Filter
        float dcBlocked = wet - dcInPrev + 0.995f * dcOutPrev;
        dcInPrev = wet;
        dcOutPrev = dcBlocked;
        wet = dcBlocked;

        // 8. Dry/Wet Crossfade
        output[i] = (dry * (1.0f - wetDryMix)) + (wet * wetDryMix);
    }
}
