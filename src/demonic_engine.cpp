#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>

DemonicEngine::DemonicEngine(float sr) 
    : sampleRate(sr), formantCents(-500.0f), subBlend(0.6f), driveDb(8.0f), wetDryMix(0.8f), writeHead(0) {
    std::fill(std::begin(historyBuffer), std::end(historyBuffer), 0.0f);
}

void DemonicEngine::setFormantShift(float cents) { formantCents = cents; }
void DemonicEngine::setSubOctaveBlend(float blend) { subBlend = blend; }
void DemonicEngine::setDrive(float db) { driveDb = db; }
void DemonicEngine::setMix(float mix) { wetDryMix = mix; }

void DemonicEngine::processBlock(const float* input, float* output, size_t numFrames) {
    float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        // Sub-Harmonic Simulation
        historyBuffer[writeHead] = dry;
        size_t readHead = (writeHead + 256) % 512;
        float subOctave = historyBuffer[readHead] * subBlend;

        // Combine dry, sub-octave, and apply non-linear saturation
        float wet = dry + subOctave;
        wet = std::tanh(wet * gainMultiplier);

        // Dry/Wet Crossfade
        output[i] = (dry * (1.0f - wetDryMix)) + (wet * wetDryMix);

        writeHead = (writeHead + 1) % 512;
    }
}
