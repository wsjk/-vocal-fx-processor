#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>

DemonicEngine::DemonicEngine(float sr) 
    : sampleRate(sr), formantCents(-500.0f), subBlend(0.85f), driveDb(11.0f), wetDryMix(0.85f),
      robotBlend(0.0f), robotFreq(55.0f), wavefoldDrive(1.5f), decimationFactor(1.0f),
      delayFeedback(0.5f), delayMix(0.3f), reverbMix(0.35f), spectralFreezeActive(false),
      currentPresetIndex(0), subToggle(false), subArmed(false),
      subLpState(0.0f), subFilterState(0.0f), subToggleCounter(0),
      dcInPrev(0.0f), dcOutPrev(0.0f), lowpassState(0.0f), ringModPhase(0.0f),
      heldSample(0.0f), decimationCounter(0), inputEnvelope(0.0f),
      delayWriteHead(0), lfoChorusPhase(0.0f), pitchWriteHead(0), pitchPhasor(0.0f),
      feedbackDelayWriteHead(0), freezeReadHead(0.0f),
      comb1Head(0), comb2Head(0), comb3Head(0), comb4Head(0),
      ap1Head(0), ap2Head(0) {
    std::fill(std::begin(delayLine), std::end(delayLine), 0.0f);
    std::fill(std::begin(pitchBuffer), std::end(pitchBuffer), 0.0f);
    std::fill(std::begin(feedbackDelayBuffer), std::end(feedbackDelayBuffer), 0.0f);
    std::fill(std::begin(freezeBuffer), std::end(freezeBuffer), 0.0f);
    std::fill(std::begin(comb1), std::end(comb1), 0.0f);
    std::fill(std::begin(comb2), std::end(comb2), 0.0f);
    std::fill(std::begin(comb3), std::end(comb3), 0.0f);
    std::fill(std::begin(comb4), std::end(comb4), 0.0f);
    std::fill(std::begin(ap1), std::end(ap1), 0.0f);
    std::fill(std::begin(ap2), std::end(ap2), 0.0f);

    // {Name, FormantCents, Drive, Sub, Robot, Freq, Fold, Decim, DelayFB, DelayMix, RevMix, Mix}
    presets[0] = {"DEMONIC GROWL", -500.0f, 11.0f, 0.85f, 0.0f,  45.0f, 1.0f, 1.0f, 0.6f,  0.35f, 0.45f, 0.85f};
    presets[1] = {"MEGATRON",         0.0f,  6.0f, 0.20f, 0.95f, 75.0f, 3.0f, 3.5f, 0.4f,  0.20f, 0.20f, 0.90f};
    presets[2] = {"CYBER DEMON",   -300.0f, 10.0f, 0.70f, 0.65f, 55.0f, 2.5f, 2.0f, 0.75f, 0.40f, 0.50f, 0.85f};

    loadPreset(0);
}

void DemonicEngine::setFormantShift(float cents) { formantCents = cents; }
void DemonicEngine::setSubOctaveBlend(float blend) { subBlend = blend; }
void DemonicEngine::setDrive(float db) { driveDb = db; }
void DemonicEngine::setRobotBlend(float blend) { robotBlend = blend; }
void DemonicEngine::setRobotFreq(float freq) { robotFreq = freq; }
void DemonicEngine::setWavefold(float fold) { wavefoldDrive = fold; }
void DemonicEngine::setDecimation(float factor) { decimationFactor = factor; }
void DemonicEngine::setDelayFeedback(float fb) { delayFeedback = fb; }
void DemonicEngine::setDelayMix(float mix) { delayMix = mix; }
void DemonicEngine::setReverbMix(float revMix) { reverbMix = revMix; }
void DemonicEngine::setMix(float mix) { wetDryMix = mix; }

void DemonicEngine::setSpectralFreeze(bool freeze) {
    if (freeze && !spectralFreezeActive) {
        // Capture recent audio into freeze buffer for infinite sustain drone
        for (size_t i = 0; i < 4096; ++i) {
            freezeBuffer[i] = feedbackDelayBuffer[(feedbackDelayWriteHead + i) % 22050];
        }
        freezeReadHead = 0.0f;
    }
    spectralFreezeActive = freeze;
}

bool DemonicEngine::isSpectralFrozen() const {
    return spectralFreezeActive;
}

void DemonicEngine::loadPreset(int index) {
    currentPresetIndex = (index % NUM_PRESETS + NUM_PRESETS) % NUM_PRESETS;
    formantCents = presets[currentPresetIndex].formantCents;
    driveDb = presets[currentPresetIndex].driveDb;
    subBlend = presets[currentPresetIndex].subBlend;
    robotBlend = presets[currentPresetIndex].robotBlend;
    robotFreq = presets[currentPresetIndex].robotFreq;
    wavefoldDrive = presets[currentPresetIndex].wavefoldDrive;
    decimationFactor = presets[currentPresetIndex].decimationFactor;
    delayFeedback = presets[currentPresetIndex].delayFeedback;
    delayMix = presets[currentPresetIndex].delayMix;
    reverbMix = presets[currentPresetIndex].reverbMix;
    wetDryMix = presets[currentPresetIndex].wetDryMix;
}

const char* DemonicEngine::getCurrentPresetName() const {
    return presets[currentPresetIndex].name;
}

int DemonicEngine::getPresetCount() const {
    return NUM_PRESETS;
}

const VocalPreset& DemonicEngine::getPreset(int index) const {
    int idx = (index % NUM_PRESETS + NUM_PRESETS) % NUM_PRESETS;
    return presets[idx];
}

void DemonicEngine::processBlock(const float* input, float* output, size_t numFrames) {
    float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);
    const float noiseGateThreshold = 0.02f;
    int decimationStep = std::max(1, static_cast<int>(decimationFactor));

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        float activeSignal = (std::abs(dry) > noiseGateThreshold) ? dry : 0.0f;
        inputEnvelope = inputEnvelope + 0.1f * (std::abs(activeSignal) - inputEnvelope);

        // 1. Chorus Doubler
        delayLine[delayWriteHead] = activeSignal;
        float chorusLfo = std::sin(lfoChorusPhase);
        lfoChorusPhase += (kTwoPi * 0.5f) / sampleRate;
        if (lfoChorusPhase >= kTwoPi) lfoChorusPhase -= kTwoPi;

        float chorusDelaySamples = 441.0f + (chorusLfo * 0.3f * 220.0f);
        float readIdxFloat = static_cast<float>(delayWriteHead) - chorusDelaySamples;
        while (readIdxFloat < 0.0f) readIdxFloat += 2048.0f;
        size_t idx1 = static_cast<size_t>(readIdxFloat) % 2048;
        size_t idx2 = (idx1 + 1) % 2048;
        float frac = readIdxFloat - static_cast<float>(idx1);
        float doubledVoice = delayLine[idx1] * (1.0f - frac) + delayLine[idx2] * frac;

        float widenedVoice = activeSignal + (doubledVoice * 0.3f);
        delayWriteHead = (delayWriteHead + 1) % 2048;

        // 1b. Granular Pitch / Formant Shift
        pitchBuffer[pitchWriteHead] = widenedVoice;
        if (std::abs(formantCents) > 1.0f) {
            float pitchRatio = std::pow(2.0f, formantCents / 1200.0f);
            constexpr float kGrainWindow = 1024.0f;
            float phaseInc = (1.0f - pitchRatio) / kGrainWindow;
            pitchPhasor += phaseInc;
            if (pitchPhasor >= 1.0f) pitchPhasor -= 1.0f;
            if (pitchPhasor < 0.0f) pitchPhasor += 1.0f;

            float tap1 = pitchPhasor * kGrainWindow;
            float p2 = pitchPhasor + 0.5f;
            if (p2 >= 1.0f) p2 -= 1.0f;
            float tap2 = p2 * kGrainWindow;

            // Hann window weights
            float w1 = 0.5f * (1.0f - std::cos(kTwoPi * pitchPhasor));
            float w2 = 1.0f - w1;

            float r1 = static_cast<float>(pitchWriteHead) - tap1;
            while (r1 < 0.0f) r1 += static_cast<float>(PITCH_BUFFER_SIZE);
            size_t pIdx1 = static_cast<size_t>(r1) % PITCH_BUFFER_SIZE;
            size_t pIdx2 = (pIdx1 + 1) % PITCH_BUFFER_SIZE;
            float f1 = r1 - static_cast<float>(pIdx1);
            float s1 = pitchBuffer[pIdx1] * (1.0f - f1) + pitchBuffer[pIdx2] * f1;

            float r2 = static_cast<float>(pitchWriteHead) - tap2;
            while (r2 < 0.0f) r2 += static_cast<float>(PITCH_BUFFER_SIZE);
            size_t qIdx1 = static_cast<size_t>(r2) % PITCH_BUFFER_SIZE;
            size_t qIdx2 = (qIdx1 + 1) % PITCH_BUFFER_SIZE;
            float f2 = r2 - static_cast<float>(qIdx1);
            float s2 = pitchBuffer[qIdx1] * (1.0f - f2) + pitchBuffer[qIdx2] * f2;

            widenedVoice = s1 * w1 + s2 * w2;
        }
        pitchWriteHead = (pitchWriteHead + 1) % PITCH_BUFFER_SIZE;

        // 2. Sub-Octave Divider (lowpass fundamental tracking on raw input + Schmitt trigger hysteresis)
        // Decoupled from noise gate so zero-crossings are continuous and uncorrupted
        float subLpAlpha = (kTwoPi * 160.0f) / sampleRate;
        subLpState += subLpAlpha * (dry - subLpState);

        constexpr float kHysteresis = 0.025f;
        if (inputEnvelope > noiseGateThreshold * 0.5f) {
            if (subLpState < -kHysteresis) {
                subArmed = true;
            } else if (subLpState > kHysteresis && subArmed) {
                subToggle = !subToggle;
                subArmed = false;
                subToggleCounter++;
            }
        }

        float rawSub = subToggle ? 1.0f : -1.0f;
        subFilterState += 0.04f * (rawSub - subFilterState);
        float subOctave = subFilterState * inputEnvelope * subBlend * 1.8f;

        float wet = widenedVoice + subOctave;

        // 3. Ring Mod
        float dynamicFreq = robotFreq + (inputEnvelope * 40.0f);
        float phaseInc = (kTwoPi * dynamicFreq) / sampleRate;
        float lfo = std::sin(ringModPhase);
        ringModPhase += phaseInc;
        if (ringModPhase >= kTwoPi) ringModPhase -= kTwoPi;
        
        float roboticSignal = wet * lfo;
        wet = (wet * (1.0f - robotBlend)) + (roboticSignal * robotBlend * 1.5f);

        // 4. Saturation & True Positive Wavefolder
        wet = std::tanh(wet * gainMultiplier);
        
        if (wavefoldDrive > 0.1f) {
            float folded = wet * (1.0f + wavefoldDrive * 2.0f);
            float x = folded + 1.0f;
            x -= 4.0f * std::floor(x / 4.0f); // True Euclidean modulo in [0, 4)
            folded = std::fabs(x - 2.0f) - 1.0f; // strictly within [-1.0f, 1.0f]
            float k = std::min(0.4f * wavefoldDrive, 1.0f);
            wet = (wet * (1.0f - k)) + (folded * k);
        }

        // 5. Decimation
        decimationCounter = (decimationCounter + 1) % decimationStep;
        if (decimationCounter == 0) {
            heldSample = wet;
        } else {
            wet = heldSample;
        }

        const float quantizationLevels = 32.0f; 
        wet = std::round(wet * quantizationLevels) / quantizationLevels;

        // 6. Tone Shaping
        lowpassState = lowpassState + 0.35f * (wet - lowpassState);
        wet = lowpassState;

        // 7. Spectral Freeze / Infinite Sustain Drone Overlay
        if (spectralFreezeActive) {
            size_t freezeIdx = static_cast<size_t>(freezeReadHead) % 4096;
            float frozenSample = freezeBuffer[freezeIdx];
            freezeReadHead += 1.0f;
            if (freezeReadHead >= 4096.0f) freezeReadHead = 0.0f;
            wet = (wet * 0.3f) + (frozenSample * 0.7f); // Blend live signal with sustain drone
        }

        // 8. Feedback Delay with Saturation (exactly 300 ms delay tap at 44.1 kHz)
        constexpr size_t kDelayBufLen = 22050;
        size_t echoDelaySamples = static_cast<size_t>(0.300f * sampleRate);
        if (echoDelaySamples >= kDelayBufLen) echoDelaySamples = kDelayBufLen - 1;
        size_t delayTap = (feedbackDelayWriteHead + kDelayBufLen - echoDelaySamples) % kDelayBufLen;
        float delayedSample = feedbackDelayBuffer[delayTap];
        
        // Feed signal + feedback into delay buffer; saturated with tanh to prevent runaway even when feedback > 1.0
        feedbackDelayBuffer[feedbackDelayWriteHead] = std::tanh(wet + (delayedSample * delayFeedback));
        feedbackDelayWriteHead = (feedbackDelayWriteHead + 1) % kDelayBufLen;

        wet = (wet * (1.0f - delayMix)) + (delayedSample * delayMix);

        // 9. Catacomb Reverb (Schroeder reverberator: 4 parallel comb filters + 2 series allpass diffusers)
        constexpr float kCombG = 0.80f;
        float c1 = comb1[comb1Head];
        comb1[comb1Head] = wet + c1 * kCombG;
        comb1Head = (comb1Head + 1 == COMB1_LEN) ? 0 : comb1Head + 1;

        float c2 = comb2[comb2Head];
        comb2[comb2Head] = wet + c2 * kCombG;
        comb2Head = (comb2Head + 1 == COMB2_LEN) ? 0 : comb2Head + 1;

        float c3 = comb3[comb3Head];
        comb3[comb3Head] = wet + c3 * kCombG;
        comb3Head = (comb3Head + 1 == COMB3_LEN) ? 0 : comb3Head + 1;

        float c4 = comb4[comb4Head];
        comb4[comb4Head] = wet + c4 * kCombG;
        comb4Head = (comb4Head + 1 == COMB4_LEN) ? 0 : comb4Head + 1;

        float combSum = (c1 + c2 + c3 + c4) * 0.25f;

        // Allpass diffuser 1 (gain = 0.5)
        float ap1Buf = ap1[ap1Head];
        float ap1Out = -0.5f * combSum + ap1Buf;
        ap1[ap1Head] = combSum + 0.5f * ap1Out;
        ap1Head = (ap1Head + 1 == AP1_LEN) ? 0 : ap1Head + 1;

        // Allpass diffuser 2 (gain = 0.5)
        float ap2Buf = ap2[ap2Head];
        float ap2Out = -0.5f * ap1Out + ap2Buf;
        ap2[ap2Head] = ap1Out + 0.5f * ap2Out;
        ap2Head = (ap2Head + 1 == AP2_LEN) ? 0 : ap2Head + 1;

        float reverbSignal = ap2Out;
        wet = (wet * (1.0f - reverbMix)) + (reverbSignal * reverbMix);

        // 10. DC Blocker
        float dcBlocked = wet - dcInPrev + 0.995f * dcOutPrev;
        dcInPrev = wet;
        dcOutPrev = dcBlocked;
        wet = dcBlocked;

        // Safety output limiter & clamp to prevent clipping
        float finalOut = (dry * (1.0f - wetDryMix)) + (wet * wetDryMix);
        if (finalOut > 1.0f || finalOut < -1.0f) {
            finalOut = std::tanh(finalOut);
        }
        output[i] = std::clamp(finalOut, -1.0f, 1.0f);
    }
}
