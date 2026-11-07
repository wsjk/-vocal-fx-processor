#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

// float constant: the M_PI macro is a 64-bit double, which forces slow double-precision math
// on microcontrollers without a hardware FPU (e.g. the RP2040).
constexpr float kTwoPi = 6.28318530717958647692f;

// Pitch shifter sweep length: 1764 samples = 40 ms at 44.1 kHz. Long enough for low voices,
// short enough that the delay it adds stays small. pitchBuffer (2048) must exceed this + 4.
constexpr float kPitchWindow = 1764.0f;

// Fraction of each sweep, at each end, spent crossfading between the two taps. Smaller = cleaner pitch
// but harder splices (a faint tick at the sweep rate); larger = smoother splices but a blurrier pitch.
// 0.10 of a 40 ms sweep is about 4 ms of crossfade.
constexpr float kPitchFade = 0.10f;
constexpr float kPi = 3.14159265358979323846f;

// Read `delay` samples behind the write head of a circular buffer, with linear interpolation.
inline float readDelayLinear(const float* buf, size_t size, size_t writeHead, float delay) {
    float pos = static_cast<float>(writeHead) - delay;
    while (pos < 0.0f) pos += static_cast<float>(size);
    size_t i0 = static_cast<size_t>(pos) % size;
    size_t i1 = (i0 + 1) % size;
    float frac = pos - static_cast<float>(static_cast<size_t>(pos));
    return buf[i0] * (1.0f - frac) + buf[i1] * frac;
}

}  // namespace
DemonicEngine::DemonicEngine(float sr)
    : sampleRate(sr), pitchCents(0.0f),
      subBlend(0.85f), driveDb(11.0f), wetDryMix(0.85f), robotBlend(0.0f), robotFreq(55.0f),
      wavefoldDrive(1.5f), decimationFactor(1.0f), delayFeedback(0.5f), delayMix(0.3f), reverbMix(0.35f),
      spectralFreezeActive(false), currentPresetIndex(0),
      subDetLP1(0.0f), subDetLP2(0.0f), subDetEnv(0.0f), subArmed(false), subToggle(false), subFilterState(0.0f),
      dcInPrev(0.0f), dcOutPrev(0.0f), lowpassState(0.0f), ringModPhase(0.0f),
      heldSample(0.0f), decimationCounter(0), inputEnvelope(0.0f),
      pitchWriteHead(0), pitchPhase(0.0f), pitchBlend(0.0f),
      delayWriteHead(0), lfoChorusPhase(0.0f), feedbackDelayWriteHead(0),
      freezeReadHead(0.0f), reverbWriteHead(0),
      bypassTarget(false), bypassBlend(0.0f), bypassStateCleared(false) {
    std::fill(std::begin(pitchBuffer), std::end(pitchBuffer), 0.0f);
    std::fill(std::begin(delayLine), std::end(delayLine), 0.0f);
    std::fill(std::begin(feedbackDelayBuffer), std::end(feedbackDelayBuffer), 0.0f);
    std::fill(std::begin(freezeBuffer), std::end(freezeBuffer), 0.0f);
    std::fill(std::begin(reverbBuffer), std::end(reverbBuffer), 0.0f);

    // {Name, Drive, Sub, Robot, Freq, Fold, Decim, DelayFB, DelayMix, RevMix, Mix}
    presets[0] = {"DEMONIC GROWL", 11.0f, 0.85f, 0.0f,  45.0f, 1.0f, 1.0f, 0.6f,  0.35f, 0.45f, 0.85f};
    presets[1] = {"MEGATRON",       6.0f,  0.20f, 0.95f, 75.0f, 3.0f, 3.5f, 0.4f,  0.20f, 0.20f, 0.90f};
    presets[2] = {"CYBER DEMON",    10.0f, 0.70f, 0.65f, 55.0f, 2.5f, 2.0f, 0.75f, 0.40f, 0.50f, 0.85f};

    loadPreset(0);
}

void DemonicEngine::setPitchShift(float cents) { pitchCents = std::clamp(cents, -1200.0f, 1200.0f); }
void DemonicEngine::setFormantShift(float cents) { setPitchShift(cents); }
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

void DemonicEngine::setBypass(bool bypass) {
    bypassTarget = bypass;
    if (bypass) spectralFreezeActive = false;   // a frozen loop cannot survive a bypass
}
bool DemonicEngine::isBypassed() const { return bypassTarget; }

void DemonicEngine::resetState() {
    std::fill(std::begin(pitchBuffer), std::end(pitchBuffer), 0.0f);
    std::fill(std::begin(delayLine), std::end(delayLine), 0.0f);
    std::fill(std::begin(feedbackDelayBuffer), std::end(feedbackDelayBuffer), 0.0f);
    std::fill(std::begin(freezeBuffer), std::end(freezeBuffer), 0.0f);
    std::fill(std::begin(reverbBuffer), std::end(reverbBuffer), 0.0f);
    pitchWriteHead = 0; pitchPhase = 0.0f; pitchBlend = 0.0f;
    subDetLP1 = subDetLP2 = subDetEnv = 0.0f; subArmed = false; subToggle = false; subFilterState = 0.0f;
    dcInPrev = dcOutPrev = lowpassState = ringModPhase = heldSample = 0.0f;
    decimationCounter = 0; inputEnvelope = 0.0f;
    delayWriteHead = 0; lfoChorusPhase = 0.0f; feedbackDelayWriteHead = 0;
    freezeReadHead = 0.0f; reverbWriteHead = 0;
}

void DemonicEngine::setSpectralFreeze(bool freeze) {
    if (freeze && bypassTarget) return;   // nothing to freeze while the effects are off
    if (freeze && !spectralFreezeActive) {
        // Capture current audio frame block into freeze buffer for infinite sustain drone
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
    currentPresetIndex = index % NUM_PRESETS;
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

void DemonicEngine::processBlock(const float* input, float* output, size_t numFrames) {
    // True bypass: once the crossfade to "dry only" has finished, copy the input straight through and
    // skip all DSP. The effect memory is wiped once, so a runaway echo loop is also cleared this way.
    if (bypassTarget && bypassBlend >= 1.0f) {
        if (!bypassStateCleared) { resetState(); bypassStateCleared = true; }
        for (size_t i = 0; i < numFrames; ++i) output[i] = input[i];
        return;
    }
    bypassStateCleared = false;
    const float bypassStep = 1.0f / (0.005f * sampleRate);   // full crossfade takes 5 ms

    float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);
    const float noiseGateThreshold = 0.02f;
    int decimationStep = std::max(1, static_cast<int>(decimationFactor));
    const float pitchRatio = std::pow(2.0f, pitchCents / 1200.0f);   // 100 cents = 1 semitone
    const bool pitchActive = std::abs(pitchCents) >= 1.0f;
    const float pitchBlendTarget = pitchActive ? 1.0f : 0.0f;

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        float activeSignal = (std::abs(dry) > noiseGateThreshold) ? dry : 0.0f;
        inputEnvelope = inputEnvelope + 0.1f * (std::abs(activeSignal) - inputEnvelope);

        // 0b. Pitch Shifter: a read tap whose delay sweeps slowly, so the signal is read faster or
        //     slower than it was written (pitchPhase 0..1 is the tap's position in the sweep). When the
        //     tap reaches the end of its sweep it must jump back, so a second tap, half a sweep away,
        //     covers for it with a short crossfade. Using one tap for most of the sweep keeps the pitch
        //     clean; a constant 50/50 blend of two taps would cancel the fundamental of a steady tone.
        pitchBuffer[pitchWriteHead] = activeSignal;
        pitchBlend += 0.002f * (pitchBlendTarget - pitchBlend);       // ~10 ms fade in/out, avoids clicks
        float voice = activeSignal;
        if (pitchActive || pitchBlend > 0.001f) {
            pitchPhase += (1.0f - pitchRatio) / kPitchWindow;         // delay grows if pitching down
            pitchPhase -= std::floor(pitchPhase);                      // wrap into [0, 1)
            float wA = 1.0f;                                           // weight of the main tap
            if (pitchPhase < kPitchFade)             wA = 0.5f - 0.5f * std::cos(kPi * pitchPhase / kPitchFade);
            else if (pitchPhase > 1.0f - kPitchFade) wA = 0.5f - 0.5f * std::cos(kPi * (1.0f - pitchPhase) / kPitchFade);
            float shifted = readDelayLinear(pitchBuffer, 2048, pitchWriteHead, 2.0f + pitchPhase * kPitchWindow) * wA;
            if (wA < 1.0f) {
                float phase2 = pitchPhase + 0.5f;
                if (phase2 >= 1.0f) phase2 -= 1.0f;
                shifted += readDelayLinear(pitchBuffer, 2048, pitchWriteHead, 2.0f + phase2 * kPitchWindow) * (1.0f - wA);
            }
            voice = activeSignal * (1.0f - pitchBlend) + shifted * pitchBlend;
        }
        pitchWriteHead = (pitchWriteHead + 1) % 2048;

        // 1. Chorus Doubler
        delayLine[delayWriteHead] = voice;
        float chorusLfo = std::sin(lfoChorusPhase);
        lfoChorusPhase += (kTwoPi * 0.5f) / sampleRate;
        if (lfoChorusPhase >= kTwoPi) lfoChorusPhase -= kTwoPi;

        float delaySamples = 441.0f + (chorusLfo * 0.3f * 220.0f);
        float readIdxFloat = static_cast<float>(delayWriteHead) - delaySamples;
        while (readIdxFloat < 0.0f) readIdxFloat += 2048.0f;
        size_t idx1 = static_cast<size_t>(readIdxFloat) % 2048;
        size_t idx2 = (idx1 + 1) % 2048;
        float frac = readIdxFloat - static_cast<float>(idx1);
        float doubledVoice = delayLine[idx1] * (1.0f - frac) + delayLine[idx2] * frac;

        float widenedVoice = voice + (doubledVoice * 0.3f);
        delayWriteHead = (delayWriteHead + 1) % 2048;

        // 2. Sub-Octave (octave divider)
        //    Detect one rising edge per cycle of the voice and flip a square wave each time, so the
        //    square runs at half the voice's frequency. To make the edges trustworthy:
        //      - detect on a 2-pole low-passed copy of the voice (harmonics and hiss would otherwise
        //        add extra crossings),
        //      - use a Schmitt trigger: after firing, the signal must swing below -h before it can
        //        fire again (hysteresis h scales with the signal's level),
        //      - ignore everything while the detection signal is near silence.
        //    (The old version toggled on rising AND falling zero crossings, which gives the same
        //    frequency as the voice rather than half, and it needed |signal| > the noise-gate threshold
        //    exactly at the crossing, which the gate itself had just zeroed.)
        subDetLP1 += 0.08f * (voice - subDetLP1);
        subDetLP2 += 0.08f * (subDetLP1 - subDetLP2);
        float detAbs = std::abs(subDetLP2);
        subDetEnv += (detAbs > subDetEnv ? 0.02f : 0.0005f) * (detAbs - subDetEnv);
        if (subDetEnv > 0.006f) {
            float hysteresis = std::max(0.004f, 0.25f * subDetEnv);
            if (subArmed && subDetLP2 > hysteresis) {
                subArmed = false;
                subToggle = !subToggle;
            } else if (!subArmed && subDetLP2 < -hysteresis) {
                subArmed = true;
            }
        }

        float rawSub = subToggle ? 1.0f : -1.0f;
        subFilterState = subFilterState + 0.04f * (rawSub - subFilterState);
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

        // 4. Saturation & Wavefolder
        wet = std::tanh(wet * gainMultiplier);
        
        if (wavefoldDrive > 0.1f) {
            float folded = wet * (1.0f + wavefoldDrive * 2.0f);
            folded = std::abs(std::fmod(folded + 1.0f, 4.0f) - 2.0f) - 1.0f;
            wet = (wet * (1.0f - 0.4f * wavefoldDrive)) + (folded * 0.4f * wavefoldDrive);
        }

        // 5. Decimation
        if (decimationCounter++ % decimationStep == 0) {
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
            wet = (wet * 0.3f) + (frozenSample * 0.7f); // Blend live signal with infinite sustain drone
        }

        // 8. Feedback Delay with Infinite Repeat / Self-Oscillation (~300ms delay tap)
        size_t delayTap = (feedbackDelayWriteHead + 13230) % 22050;
        float delayedSample = feedbackDelayBuffer[delayTap];
        
        // Feed signal + feedback into delay buffer (feedback >= 1.0 creates infinite repeat / self-oscillation)
        feedbackDelayBuffer[feedbackDelayWriteHead] = wet + (delayedSample * delayFeedback);
        feedbackDelayWriteHead = (feedbackDelayWriteHead + 1) % 22050;

        wet = (wet * (1.0f - delayMix)) + (delayedSample * delayMix);

        // 9. Catacomb Reverb
        size_t revDelay1 = (reverbWriteHead + 4410) % 16384;
        size_t revDelay2 = (reverbWriteHead + 11025) % 16384;
        float revTap1 = reverbBuffer[revDelay1];
        float revTap2 = reverbBuffer[revDelay2];
        
        float reverbEcho = (revTap1 * 0.6f) + (revTap2 * 0.4f);
        reverbBuffer[reverbWriteHead] = wet + (reverbEcho * 0.55f);
        reverbWriteHead = (reverbWriteHead + 1) % 16384;

        wet = (wet * (1.0f - reverbMix)) + (reverbEcho * reverbMix);

        // 10. DC Blocker
        float dcBlocked = wet - dcInPrev + 0.995f * dcOutPrev;
        dcInPrev = wet;
        dcOutPrev = dcBlocked;
        wet = dcBlocked;

        float processed = (dry * (1.0f - wetDryMix)) + (wet * wetDryMix);

        // Crossfade to / from the untouched input when bypass is switched.
        if (bypassTarget) bypassBlend = std::min(1.0f, bypassBlend + bypassStep);
        else if (bypassBlend > 0.0f) bypassBlend = std::max(0.0f, bypassBlend - bypassStep);
        output[i] = (bypassBlend >= 1.0f) ? dry                    // exact, and immune to a blown-up chain
                                          : processed * (1.0f - bypassBlend) + dry * bypassBlend;
    }
}
