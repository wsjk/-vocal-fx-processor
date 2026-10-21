#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>

DemonicEngine::DemonicEngine(float sr) 
    : sampleRate(sr), formantCents(-500.0f), currentPresetIndex(0),
      lastInput(0.0f), subToggle(false), subFilterState(0.0f), 
      dcInPrev(0.0f), dcOutPrev(0.0f), lowpassState(0.0f), 
      ringModPhase(0.0f), robotFreq(45.0f), chorusDepth(0.3f),
      delayWriteHead(0), lfoChorusPhase(0.0f), writeHead(0) {
    std::fill(std::begin(historyBuffer), std::end(historyBuffer), 0.0f);
    std::fill(std::begin(delayLine), std::end(delayLine), 0.0f);

    // Initialize Factory Presets with expanded parameters
    // {Name, Drive, SubBlend, RobotBlend, RobotFreq, ChorusDepth, Mix}
    presets[0] = {"DEMONIC GROWL", 11.0f, 0.85f, 0.0f,  45.0f, 0.4f, 0.85f};
    presets[1] = {"MEGATRON",       6.0f,  0.20f, 0.95f, 85.0f, 0.2f, 0.90f};
    presets[2] = {"CYBER DEMON",    10.0f, 0.70f, 0.55f, 55.0f, 0.5f, 0.85f};

    loadPreset(0);
}

void DemonicEngine::setFormantShift(float cents) { formantCents = cents; }
void DemonicEngine::setSubOctaveBlend(float blend) { subBlend = blend; }
void DemonicEngine::setDrive(float db) { driveDb = db; }
void DemonicEngine::setRobotBlend(float blend) { robotBlend = blend; }
void DemonicEngine::setRobotFreq(float freq) { robotFreq = freq; }
void DemonicEngine::setChorusDepth(float depth) { chorusDepth = depth; }
void DemonicEngine::setMix(float mix) { wetDryMix = mix; }

void DemonicEngine::loadPreset(int index) {
    currentPresetIndex = index % NUM_PRESETS;
    driveDb = presets[currentPresetIndex].driveDb;
    subBlend = presets[currentPresetIndex].subBlend;
    robotBlend = presets[currentPresetIndex].robotBlend;
    robotFreq = presets[currentPresetIndex].robotFreq;
    chorusDepth = presets[currentPresetIndex].chorusDepth;
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

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        // 1. Input Noise Gate
        float activeSignal = (std::abs(dry) > noiseGateThreshold) ? dry : 0.0f;

        // 2. Vocal Chorus / Doubler Layer (Widening)
        delayLine[delayWriteHead] = activeSignal;
        // Modulate delay read position with a slow LFO for a thick chorus effect
        float chorusLfo = std::sin(lfoChorusPhase);
        lfoChorusPhase += (2.0f * M_PI * 0.5f) / sampleRate; // 0.5 Hz chorus sweep
        if (lfoChorusPhase >= 2.0f * M_PI) lfoChorusPhase -= 2.0f * M_PI;

        float delaySamples = 441.0f + (chorusLfo * chorusDepth * 220.0f); // ~10ms base delay
        float readIdxFloat = static_cast<float>(delayWriteHead) - delaySamples;
        while (readIdxFloat < 0.0f) readIdxFloat += 2048.0f;
        size_t idx1 = static_cast<size_t>(readIdxFloat) % 2048;
        size_t idx2 = (idx1 + 1) % 2048;
        float frac = readIdxFloat - static_cast<float>(idx1);
        float doubledVoice = delayLine[idx1] * (1.0f - frac) + delayLine[idx2] * frac;

        // Blend dry voice with doubled chorus voice
        float widenedVoice = activeSignal + (doubledVoice * chorusDepth);
        delayWriteHead = (delayWriteHead + 1) % 2048;

        // 3. Zero-Crossing Sub-Octave Generator
        if ((widenedVoice > 0.0f && lastInput <= 0.0f) || (widenedVoice < 0.0f && lastInput >= 0.0f)) {
            if (std::abs(widenedVoice) > noiseGateThreshold) {
                subToggle = !subToggle;
            }
        }
        lastInput = widenedVoice;

        float rawSub = subToggle ? 1.0f : -1.0f;
        subFilterState = subFilterState + 0.04f * (rawSub - subFilterState);
        float subOctave = subFilterState * std::abs(widenedVoice) * subBlend * 1.8f;

        // 4. Blend Widened Voice + Sub-Bass
        float wet = widenedVoice + subOctave;

        // 5. Ring Modulation (Adjustable Robot Clank Frequency)
        float phaseInc = (2.0f * M_PI * robotFreq) / sampleRate;
        float lfo = std::sin(ringModPhase);
        ringModPhase += phaseInc;
        if (ringModPhase >= 2.0f * M_PI) ringModPhase -= 2.0f * M_PI;
        
        float roboticSignal = wet * lfo;
        wet = (wet * (1.0f - robotBlend)) + (roboticSignal * robotBlend * 1.5f);

        // 6. Asymmetrical Saturation & Bitcrush Grit
        wet = std::tanh(wet * gainMultiplier);
        if (wet < 0.0f) {
            wet *= 1.3f;
        }
        wet = std::tanh(wet);

        const float quantizationLevels = 32.0f; 
        wet = std::round(wet * quantizationLevels) / quantizationLevels;

        // 7. Post-Saturation Tone Shaping (High-Cut)
        lowpassState = lowpassState + 0.35f * (wet - lowpassState);
        wet = lowpassState;

        // 8. DC Blocker Filter
        float dcBlocked = wet - dcInPrev + 0.995f * dcOutPrev;
        dcInPrev = wet;
        dcOutPrev = dcBlocked;
        wet = dcBlocked;

        // 9. Dry/Wet Crossfade
        output[i] = (dry * (1.0f - wetDryMix)) + (wet * wetDryMix);
    }
}
