#include <iostream>
#include <cmath>
#include <cassert>
#include "demonic_engine.hpp"

#define SAMPLE_RATE 44100.0f
constexpr float kPi = 3.14159265358979323846f;

void testInitialization() {
    DemonicEngine engine(SAMPLE_RATE);
    assert(engine.getPresetCount() > 0);
    std::cout << "[PASS] testInitialization\n";
}

void testPresets() {
    DemonicEngine engine(SAMPLE_RATE);
    engine.loadPreset(0);
    assert(std::string(engine.getCurrentPresetName()) == "DEMONIC GROWL");
    
    engine.loadPreset(1);
    assert(std::string(engine.getCurrentPresetName()) == "MEGATRON");
    
    std::cout << "[PASS] testPresets\n";
}

void testProcessBlockSine() {
    DemonicEngine engine(SAMPLE_RATE);
    engine.loadPreset(0);

    const size_t blockSize = 512;
    float in[blockSize];
    float out[blockSize];

    // Generate a 440 Hz test sine wave above noise gate threshold
    float amp = 0.5f;
    float freqHz = 440.0f;
    for (size_t n = 0; n < 10; ++n) {
        for (size_t k = 0; k < blockSize; ++k) {
            in[k] = amp * std::sin(2.0f * kPi * freqHz * (n * blockSize + k) / SAMPLE_RATE);
        }
        engine.processBlock(in, out, blockSize);
    }
    std::cout << "[PASS] testProcessBlockSine\n";
}

void testSubOctaveToggles() {
    DemonicEngine engine(SAMPLE_RATE);
    engine.loadPreset(0);
    engine.setSubOctaveBlend(1.0f);

    const size_t blockSize = 512;
    float in[blockSize];
    float out[blockSize];

    engine.resetSubToggleCount();

    // Feed sine wave
    for (size_t n = 0; n < 20; ++n) {
        for (size_t k = 0; k < blockSize; ++k) {
            in[k] = 0.5f * std::sin(2.0f * kPi * 440.0f * (n * blockSize + k) / SAMPLE_RATE);
        }
        engine.processBlock(in, out, blockSize);
    }

    int toggles = engine.getSubToggleCount();
    assert(toggles > 0);
    std::cout << "[PASS] testSubOctaveToggles (Toggles counted: " << toggles << ")\n";
}

void testSpectralFreeze() {
    DemonicEngine engine(SAMPLE_RATE);
    assert(!engine.isSpectralFrozen());
    engine.setSpectralFreeze(true);
    assert(engine.isSpectralFrozen());
    engine.setSpectralFreeze(false);
    assert(!engine.isSpectralFrozen());
    std::cout << "[PASS] testSpectralFreeze\n";
}

int main() {
    std::cout << "--- Running Demonic Engine Smoke Tests ---\n";
    testInitialization();
    testPresets();
    testProcessBlockSine();
    testSubOctaveToggles();
    testSpectralFreeze();
    std::cout << "--- All Tests Passed Successfully! ---\n";
    return 0;
}
