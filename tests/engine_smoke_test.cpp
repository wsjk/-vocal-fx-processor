// Comprehensive headless test for DemonicEngine: no microphone, no window, no PortAudio.
// Build & run from the repo root:
//   g++ -std=c++17 -Wall -Wextra -Iinclude tests/engine_smoke_test.cpp src/demonic_engine.cpp -o smoke && ./smoke
#include "demonic_engine.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

static const float SAMPLE_RATE = 44100.0f;
static const size_t BLOCK = 64;           // same block size the desktop app uses

// Run `seconds` of a sine wave through the engine; return the largest |output| seen.
// Sets *sawBadValue if any output sample is NaN or infinite.
static float run_sine(DemonicEngine& engine, float freqHz, float amp, float seconds, bool* sawBadValue) {
    std::vector<float> in(BLOCK), out(BLOCK);
    float peak = 0.0f;
    size_t total = static_cast<size_t>(seconds * SAMPLE_RATE);
    for (size_t n = 0; n < total; n += BLOCK) {
        for (size_t k = 0; k < BLOCK; ++k)
            in[k] = amp * std::sin(2.0f * kPi * freqHz * (n + k) / SAMPLE_RATE);
        engine.processBlock(in.data(), out.data(), BLOCK);
        for (size_t k = 0; k < BLOCK; ++k) {
            if (!std::isfinite(out[k])) *sawBadValue = true;
            peak = std::fmax(peak, std::fabs(out[k]));
        }
    }
    return peak;
}

int main() {
    int failures = 0;

    // 1. Silence in -> silence out
    {
        DemonicEngine e(SAMPLE_RATE);
        std::vector<float> in(BLOCK, 0.0f), out(BLOCK, 1.0f);
        e.processBlock(in.data(), out.data(), BLOCK);
        bool silent = true;
        for (float s : out) if (s != 0.0f) silent = false;
        std::printf("[%s] silence in -> silence out\n", silent ? "PASS" : "FAIL");
        failures += !silent;
    }

    // 2. Every preset stays finite on a sine input, and stays within full scale (<= 1.0).
    for (int p = 0; p < 3; ++p) {
        DemonicEngine e(SAMPLE_RATE);
        e.loadPreset(p);
        bool bad = false;
        float peak = run_sine(e, 220.0f, 0.6f, 2.0f, &bad);
        bool pass = !bad && (peak <= 1.0f);
        std::printf("[%s] preset %d (%s): finite=%s, peak=%.2f%s\n",
                    pass ? "PASS" : "FAIL", p, e.getCurrentPresetName(),
                    bad ? "no" : "yes", peak, peak > 1.0f ? "  (WARNING: exceeds full scale)" : "");
        failures += !pass;
    }

    // 3. Delay feedback runaway test (60 seconds at feedback 1.05)
    {
        DemonicEngine e(SAMPLE_RATE);
        e.setDrive(0.0f);
        e.setWavefold(0.0f);
        e.setDelayMix(1.0f);
        e.setDelayFeedback(1.05f); // feedback > 1.0
        e.setMix(1.0f);

        std::vector<float> in(BLOCK, 0.0f), out(BLOCK);
        bool sawBad = false;
        float maxPeak = 0.0f;

        // 0.5s burst of sine wave
        size_t burstFrames = static_cast<size_t>(0.5f * SAMPLE_RATE);
        for (size_t n = 0; n < burstFrames; n += BLOCK) {
            for (size_t k = 0; k < BLOCK; ++k)
                in[k] = 0.5f * std::sin(2.0f * kPi * 440.0f * (n + k) / SAMPLE_RATE);
            e.processBlock(in.data(), out.data(), BLOCK);
            for (float s : out) {
                if (!std::isfinite(s)) sawBad = true;
                maxPeak = std::max(maxPeak, std::abs(s));
            }
        }

        // 59.5 seconds of silence: feedback circulates and must stay bounded <= 1.0
        std::fill(in.begin(), in.end(), 0.0f);
        size_t silentFrames = static_cast<size_t>(59.5f * SAMPLE_RATE);
        for (size_t n = 0; n < silentFrames; n += BLOCK) {
            e.processBlock(in.data(), out.data(), BLOCK);
            for (float s : out) {
                if (!std::isfinite(s)) sawBad = true;
                maxPeak = std::max(maxPeak, std::abs(s));
            }
        }

        bool pass = !sawBad && (maxPeak <= 1.0f);
        std::printf("[%s] delay feedback 1.05 (60s test): finite=%s, peak=%.3f (bounded <= 1.0)\n",
                    pass ? "PASS" : "FAIL", sawBad ? "no" : "yes", maxPeak);
        failures += !pass;
    }

    // 4. Sub-octave pitch tracking: 110 Hz sine wave should produce ~110 toggles/sec
    {
        bool allPassed = true;
        float testAmps[] = { 0.1f, 0.3f, 0.6f, 0.9f };
        for (float amp : testAmps) {
            DemonicEngine e(SAMPLE_RATE);
            e.setDrive(0.0f);
            e.setSubOctaveBlend(1.0f);
            e.resetSubToggleCount();

            bool bad = false;
            run_sine(e, 110.0f, amp, 1.0f, &bad);
            int toggles = e.getSubToggleCount();
            // A 110 Hz sine should toggle rising edges exactly ~110 times per second
            bool pass = !bad && (toggles >= 108 && toggles <= 112);
            if (!pass) allPassed = false;
        }
        std::printf("[%s] sub-octave pitch tracking (110 Hz sine at various amplitudes): reliable tracking\n",
                    allPassed ? "PASS" : "FAIL");
        failures += !allPassed;
    }

    // 5. Wavefolder extreme range test (Drive = 12 dB, Wavefold = 5.0)
    {
        DemonicEngine e(SAMPLE_RATE);
        e.setDrive(12.0f);
        e.setWavefold(5.0f);
        e.setMix(1.0f);
        bool bad = false;
        float peak = run_sine(e, 220.0f, 0.9f, 1.0f, &bad);
        bool pass = !bad && (peak <= 1.0f);
        std::printf("[%s] wavefolder extreme fold (drive 12dB, fold 5.0): peak=%.2f <= 1.0\n",
                    pass ? "PASS" : "FAIL", peak);
        failures += !pass;
    }

    // 6. Delay timing verification: exactly 300 ms (13,230 samples at 44.1 kHz)
    {
        DemonicEngine e(SAMPLE_RATE);
        e.setFormantShift(0.0f); // Pure echo delay tap measurement (bypass granular shift)
        e.setDrive(0.0f);
        e.setSubOctaveBlend(0.0f);
        e.setRobotBlend(0.0f);
        e.setWavefold(0.0f);
        e.setDecimation(1.0f);
        e.setReverbMix(0.0f);
        e.setDelayMix(1.0f);
        e.setDelayFeedback(0.0f);
        e.setMix(1.0f);

        std::vector<float> in(BLOCK, 0.0f), out(BLOCK);
        // Single impulse at sample 0
        in[0] = 0.8f;
        e.processBlock(in.data(), out.data(), BLOCK);
        in[0] = 0.0f;

        size_t impulseSampleFound = 0;
        float maxEcho = 0.0f;
        size_t totalSamples = 22050; // 500 ms
        for (size_t n = BLOCK; n < totalSamples; n += BLOCK) {
            e.processBlock(in.data(), out.data(), BLOCK);
            for (size_t k = 0; k < BLOCK; ++k) {
                if (std::abs(out[k]) > maxEcho) {
                    maxEcho = std::abs(out[k]);
                    impulseSampleFound = n + k;
                }
            }
        }
        // Exactly 300 ms is 13,230 samples at 44.1 kHz
        float measuredDelayMs = (static_cast<float>(impulseSampleFound) / SAMPLE_RATE) * 1000.0f;
        bool pass = (impulseSampleFound == 13230);
        std::printf("[%s] delay tap timing: sample %zu = %.1f ms (expected 300.0 ms)\n",
                    pass ? "PASS" : "FAIL", impulseSampleFound, measuredDelayMs);
        failures += !pass;
    }

    // 7. Catacomb Reverb diffuse decay verification
    {
        DemonicEngine e(SAMPLE_RATE);
        e.setDrive(0.0f);
        e.setSubOctaveBlend(0.0f);
        e.setRobotBlend(0.0f);
        e.setWavefold(0.0f);
        e.setDecimation(1.0f);
        e.setDelayMix(0.0f);
        e.setReverbMix(1.0f);
        e.setMix(1.0f);

        std::vector<float> in(BLOCK, 0.0f), out(BLOCK);
        in[0] = 1.0f;
        e.processBlock(in.data(), out.data(), BLOCK);
        in[0] = 0.0f;

        int nonZeroSamples = 0;
        float maxReverb = 0.0f;
        // Count non-zero diffuse samples over 15,000 samples
        for (size_t n = BLOCK; n < 15000; n += BLOCK) {
            e.processBlock(in.data(), out.data(), BLOCK);
            for (size_t k = 0; k < BLOCK; ++k) {
                if (std::abs(out[k]) > 0.0001f) nonZeroSamples++;
                maxReverb = std::max(maxReverb, std::abs(out[k]));
            }
        }
        // Schroeder reverb produces dense continuous reverberation tail (>5000 non-zero samples vs 2 discrete echo pulses)
        bool pass = (nonZeroSamples > 5000) && (maxReverb <= 1.0f);
        std::printf("[%s] catacomb reverb: dense Schroeder diffusion (%d diffuse samples, peak=%.2f)\n",
                    pass ? "PASS" : "FAIL", nonZeroSamples, maxReverb);
        failures += !pass;
    }

    return failures;
}
