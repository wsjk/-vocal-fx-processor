#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>

DemonicEngine::DemonicEngine(float sr) 
    : sampleRate(sr), formantCents(-500.0f), currentPresetIndex(0),
      lastInput(0.0f), subToggle(false), subFilterState(0.0f), 
      dcInPrev(0.0f), dcOutPrev(0.0f), lowpassState(0.0f), 
      ringModPhase(0.0f), robotFreq(55.0f), wavefoldDrive(1.5f), decimationFactor(1.0f),
      heldSample(0.0f), decimationCounter(0), inputEnvelope(0.0f),
      delayWriteHead(0), lfoChorusPhase(0.0f), writeHead(0) {
    std::fill(std::begin(historyBuffer), std::end(historyBuffer), 0.0f);
    std::fill(std::begin(delayLine), std::end(delayLine), 0.0f);

    presets[0] = {"DEMONIC GROWL", 11.0f, 0.85f, 0.0f,  45.0f, 1.0f, 1.0f, 0.85f};
    presets[1] = {"MEGATRON",       6.0f,  0.20f, 0.95f, 75.0f, 3.0f, 3.5f, 0.90f};
    presets[2] = {"CYBER DEMON",    10.0f, 0.70f, 0.65f, 55.0f, 2.5f, 2.0f, 0.85f};

    loadPreset(0);
}

void DemonicEngine::setFormantShift(float cents) { formantCents = cents; }
void DemonicEngine::setSubOctaveBlend(float blend) { subBlend = blend; }
void DemonicEngine::setDrive(float db) { driveDb = db; }
void DemonicEngine::setRobotBlend(float blend) { robotBlend = blend; }
void DemonicEngine::setRobotFreq(float freq) { robotFreq = freq; }
void DemonicEngine::setWavefold(float fold) { wavefoldDrive = fold; }
void DemonicEngine::setDecimation(float factor) { decimationFactor = factor; }
void DemonicEngine::setMix(float mix) { wetDryMix = mix; }

void DemonicEngine::loadPreset(int index) {
    currentPresetIndex = index % NUM_PRESETS;
    driveDb = presets[currentPresetIndex].driveDb;
    subBlend = presets[currentPresetIndex].subBlend;
    robotBlend = presets[currentPresetIndex].robotBlend;
    robotFreq = presets[currentPresetIndex].robotFreq;
    wavefoldDrive = presets[currentPresetIndex].wavefoldDrive;
    decimationFactor = presets[currentPresetIndex].decimationFactor;
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
    int decimationStep = std::max(1, static_cast<int>(decimationFactor));

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        float activeSignal = (std::abs(dry) > noiseGateThreshold) ? dry : 0.0f;
        inputEnvelope = inputEnvelope + 0.1f * (std::abs(activeSignal) - inputEnvelope);

        delayLine[delayWriteHead] = activeSignal;
        float chorusLfo = std::sin(lfoChorusPhase);
        lfoChorusPhase += (2.0f * M_PI * 0.5f) / sampleRate;
        if (lfoChorusPhase >= 2.0f * M_PI) lfoChorusPhase -= 2.0f * M_PI;

        float delaySamples = 441.0f + (chorusLfo * 0.3f * 220.0f);
        float readIdxFloat = static_cast<float>(delayWriteHead) - delaySamples;
        while (readIdxFloat < 0.0f) readIdxFloat += 2048.0f;
        size_t idx1 = static_cast<size_t>(readIdxFloat) % 2048;
        size_t idx2 = (idx1 + 1) % 2048;
        float frac = readIdxFloat - static_cast<float>(idx1);
        float doubledVoice = delayLine[idx1] * (1.0f - frac) + delayLine[idx2] * frac;

        float widenedVoice = activeSignal + (doubledVoice * 0.3f);
        delayWriteHead = (delayWriteHead + 1) % 2048;

        if ((widenedVoice > 0.0f && lastInput <= 0.0f) || (widenedVoice < 0.0f && lastInput >= 0.0f)) {
            if (std::abs(widenedVoice) > noiseGateThreshold) {
                subToggle = !subToggle;
            }
        }
        lastInput = widenedVoice;

        float rawSub = subToggle ? 1.0f : -1.0f;
        subFilterState = subFilterState + 0.04f * (rawSub - subFilterState);
        float subOctave = subFilterState * std::abs(widenedVoice) * subBlend * 1.8f;

        float wet = widenedVoice + subOctave;

        float dynamicFreq = robotFreq + (inputEnvelope * 40.0f);
        float phaseInc = (2.0f * M_PI * dynamicFreq) / sampleRate;
        float lfo = std::sin(ringModPhase);
        ringModPhase += phaseInc;
        if (ringModPhase >= 2.0f * M_PI) ringModPhase -= 2.0f * M_PI;
        
        float roboticSignal = wet * lfo;
        wet = (wet * (1.0f - robotBlend)) + (roboticSignal * robotBlend * 1.5f);

        wet = std::tanh(wet * gainMultiplier);
        
        if (wavefoldDrive > 0.1f) {
            float folded = wet * (1.0f + wavefoldDrive * 2.0f);
            folded = std::abs(std::fmod(folded + 1.0f, 4.0f) - 2.0f) - 1.0f;
            wet = (wet * (1.0f - 0.4f * wavefoldDrive)) + (folded * 0.4f * wavefoldDrive);
        }

        if (decimationCounter++ % decimationStep == 0) {
            heldSample = wet;
        } else {
            wet = heldSample;
        }

        const float quantizationLevels = 32.0f; 
        wet = std::round(wet * quantizationLevels) / quantizationLevels;

        lowpassState = lowpassState + 0.35f * (wet - lowpassState);
        wet = lowpassState;

        float dcBlocked = wet - dcInPrev + 0.995f * dcOutPrev;
        dcInPrev = wet;
        dcOutPrev = dcBlocked;
        wet = dcBlocked;

        output[i] = (dry * (1.0f - wetDryMix)) + (wet * wetDryMix);
    }
}
