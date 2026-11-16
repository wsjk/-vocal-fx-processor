#include "demonic_engine.hpp"
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

// float constant: the M_PI macro is a 64-bit double, which forces slow double-precision math
// on microcontrollers without a hardware FPU (e.g. the RP2040).
constexpr float kTwoPi = 6.28318530717958647692f;

// The engine was tuned at 44.1 kHz. Every length and coefficient below is given at this rate and
// converted to the actual one in the constructor, so the effects sound the same at 48 kHz (Daisy Seed).
constexpr float kRefRate = 44100.0f;

// Pitch shifter sweep length: 40 ms (1764 samples at 44.1 kHz). Long enough for low voices,
// short enough that the delay it adds stays small. pitchBuffer must exceed this + 4.
constexpr float kPitchWindowSec = 0.040f;

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

// A one-pole coefficient tuned at 44.1 kHz (y += c * (x - y)), converted to `rate` so that the filter
// keeps the same time constant: (1 - c) per sample at 44.1 kHz becomes (1 - c)^(44100 / rate).
inline float rateCoef(float c, float rate) { return 1.0f - std::pow(1.0f - c, kRefRate / rate); }

// Bypass clears the long buffers (mem.echo, mem.freeze) this many samples per processed frame, so a
// 64-frame block clears 4096 samples and the whole job takes about 50 ms of blocks.
constexpr size_t kClearPerFrame = 64;

// ReverbSc settings: a long, dark tail ("catacomb").
constexpr float kReverbFeedback = 0.85f;
constexpr float kReverbLpHz     = 5000.0f;

}  // namespace
DemonicEngine::DemonicEngine(float sr, DemonicEngineMemory& memory)
    : sampleRate(sr), mem(memory), pitchCents(0.0f), harmonyCents(0.0f), harmonyBlend(0.0f),
      subBlend(0.85f), driveDb(11.0f), wetDryMix(0.85f), robotBlend(0.0f), robotFreq(55.0f),
      wavefoldDrive(1.5f), decimationFactor(1.0f), delayTime(0.2f), delayFeedback(0.5f), delayMix(0.3f),
      reverbMix(0.35f), spectralFreezeActive(false), currentPresetIndex(0),
      subDetLP1(0.0f), subDetLP2(0.0f), subDetEnv(0.0f), subArmed(false), subToggle(false), subFilterState(0.0f),
      dcInPrev(0.0f), dcOutPrev(0.0f), lowpassState(0.0f), ringModPhase(0.0f),
      heldSample(0.0f), decimationPhase(0.0f), inputEnvelope(0.0f),
      pitchWriteHead(0), pitchPhase(0.0f), pitchBlend(0.0f), harmonyPhase(0.0f), harmonyLevel(0.0f),
      delayWriteHead(0), lfoChorusPhase(0.0f), feedbackDelayWriteHead(0), echoDelaySamples(0.0f),
      freezeWriteHead(0), freezeStart(0), freezePos(0), reverbOk(false),
      bypassTarget(false), bypassBlend(0.0f), bypassStateCleared(false), clearCursor(0) {
    // Lengths in samples for this rate. The comments give the 44.1 kHz value the effect was tuned at.
    pitchWindow  = std::min(kPitchWindowSec * sampleRate, static_cast<float>(kPitchBufSize - 8));   // 1764
    chorusDelay  = 441.0f * sampleRate / kRefRate;                                  // 10 ms
    chorusSwing  = 66.0f * sampleRate / kRefRate;                                   // 1.5 ms
    if (chorusDelay + chorusSwing > kChorusBufSize - 4) {                           // only above 48 kHz
        const float s = (kChorusBufSize - 4) / (chorusDelay + chorusSwing);
        chorusDelay *= s; chorusSwing *= s;
    }
    echoLen       = std::min(static_cast<size_t>(kMaxDelayTime * sampleRate) + 8, DemonicEngineMemory::kEchoSize);
    freezeFadeLen = std::max<size_t>(1, static_cast<size_t>(0.020f * sampleRate));
    freezeLoopLen = std::min(static_cast<size_t>(sampleRate),
                             DemonicEngineMemory::kFreezeSize - freezeFadeLen);     // 1 s loop
    freezeFadeLen = std::min(freezeFadeLen, freezeLoopLen / 2);
    freezeRingLen = freezeLoopLen + freezeFadeLen;

    envCoef        = rateCoef(0.1f, sampleRate);
    pitchBlendCoef = rateCoef(0.002f, sampleRate);
    subDetCoef     = rateCoef(0.08f, sampleRate);
    subAttackCoef  = rateCoef(0.02f, sampleRate);
    subReleaseCoef = rateCoef(0.0005f, sampleRate);
    subFilterCoef  = rateCoef(0.04f, sampleRate);
    toneCoef       = rateCoef(0.35f, sampleRate);
    dcPole         = 1.0f - rateCoef(1.0f - 0.995f, sampleRate);
    delayGlideCoef = rateCoef(0.0002f, sampleRate);                                 // ~110 ms glide

    // {Name, Drive, Sub, Robot, Freq, Fold, Decim, DelayFB, DelayMix, RevMix, Mix, DelayTime, HarmCents, HarmBlend}
    presets[0] = {"DEMONIC GROWL", 11.0f, 0.85f, 0.0f,  45.0f, 1.0f, 1.0f, 0.6f,  0.35f, 0.45f, 0.85f, 0.20f, 0.0f,   0.0f};
    presets[1] = {"MEGATRON",       6.0f,  0.20f, 0.95f, 75.0f, 3.0f, 3.5f, 0.4f,  0.20f, 0.20f, 0.90f, 0.20f, 0.0f,   0.0f};
    presets[2] = {"CYBER DEMON",    10.0f, 0.70f, 0.65f, 55.0f, 2.5f, 2.0f, 0.75f, 0.40f, 0.50f, 0.85f, 0.20f, 700.0f, 0.4f};

    loadPreset(0);
    resetState();
    clearLongBuffers(SIZE_MAX);
}

void DemonicEngine::setPitchShift(float cents) { pitchCents = std::clamp(cents, -1200.0f, 1200.0f); }
void DemonicEngine::setFormantShift(float cents) { setPitchShift(cents); }
void DemonicEngine::setHarmony(float cents, float blend) {
    harmonyCents = std::clamp(cents, -1200.0f, 1200.0f);
    harmonyBlend = std::clamp(blend, 0.0f, 1.0f);
}
void DemonicEngine::setDelayTime(float seconds) { delayTime = std::clamp(seconds, kMinDelayTime, kMaxDelayTime); }
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
    pitchWriteHead = 0; pitchPhase = 0.0f; pitchBlend = 0.0f; harmonyPhase = 0.0f; harmonyLevel = 0.0f;
    subDetLP1 = subDetLP2 = subDetEnv = 0.0f; subArmed = false; subToggle = false; subFilterState = 0.0f;
    dcInPrev = dcOutPrev = lowpassState = ringModPhase = heldSample = 0.0f;
    decimationPhase = 0.0f; inputEnvelope = 0.0f;
    delayWriteHead = 0; lfoChorusPhase = 0.0f; feedbackDelayWriteHead = 0;
    echoDelaySamples = std::clamp(delayTime * sampleRate, 1.0f, static_cast<float>(echoLen - 2));
    freezeWriteHead = 0; freezeStart = 0; freezePos = 0;
    reverbOk = reverb.Init(sampleRate) == 0;   // Init also clears the reverb's delay lines
    if (reverbOk) { reverb.SetFeedback(kReverbFeedback); reverb.SetLpFreq(kReverbLpHz); }
    clearCursor = 0;                           // the long buffers are cleared separately (clearLongBuffers)
}

bool DemonicEngine::clearLongBuffers(size_t maxSamples) {
    const size_t total = echoLen + freezeRingLen;
    while (clearCursor < total && maxSamples > 0) {
        float* buf; size_t from, to;
        if (clearCursor < echoLen) { buf = mem.echo;   from = clearCursor;           to = echoLen; }
        else                       { buf = mem.freeze; from = clearCursor - echoLen; to = freezeRingLen; }
        const size_t n = std::min(to - from, maxSamples);
        std::fill(buf + from, buf + from + n, 0.0f);
        clearCursor += n; maxSamples -= n;
    }
    return clearCursor >= total;
}

void DemonicEngine::setSpectralFreeze(bool freeze) {
    if (freeze && bypassTarget) return;   // nothing to freeze while the effects are off
    if (freeze && !spectralFreezeActive) {
        // The freeze ring holds the last second (plus the crossfade) of the echo input. Stop recording
        // and loop it from its oldest sample; nothing is copied, so pressing Freeze costs nothing.
        freezeStart = freezeWriteHead;
        freezePos = 0;
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
    setDelayTime(presets[currentPresetIndex].delayTime);
    setHarmony(presets[currentPresetIndex].harmonyCents, presets[currentPresetIndex].harmonyBlend);
}

float DemonicEngine::pitchTap(float phase) const {
    float wA = 1.0f;                                           // weight of the main tap
    if (phase < kPitchFade)             wA = 0.5f - 0.5f * std::cos(kPi * phase / kPitchFade);
    else if (phase > 1.0f - kPitchFade) wA = 0.5f - 0.5f * std::cos(kPi * (1.0f - phase) / kPitchFade);
    float shifted = readDelayLinear(pitchBuffer, kPitchBufSize, pitchWriteHead, 2.0f + phase * pitchWindow) * wA;
    if (wA < 1.0f) {
        float phase2 = phase + 0.5f;
        if (phase2 >= 1.0f) phase2 -= 1.0f;
        shifted += readDelayLinear(pitchBuffer, kPitchBufSize, pitchWriteHead, 2.0f + phase2 * pitchWindow) * (1.0f - wA);
    }
    return shifted;
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
        clearLongBuffers(numFrames * kClearPerFrame);   // a slice per block (see setBypass)
        for (size_t i = 0; i < numFrames; ++i) output[i] = input[i];
        return;
    }
    if (bypassStateCleared) clearLongBuffers(SIZE_MAX);   // bypass released before the slices finished
    bypassStateCleared = false;
    const float bypassStep = 1.0f / (0.005f * sampleRate);   // full crossfade takes 5 ms

    float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);
    const float noiseGateThreshold = 0.02f;
    int decimationStep = std::max(1, static_cast<int>(decimationFactor));
    const float decimationAdvance = kRefRate / sampleRate;   // 44.1 kHz samples per sample
    const float pitchRatio = std::pow(2.0f, pitchCents / 1200.0f);   // 100 cents = 1 semitone
    const bool pitchActive = std::abs(pitchCents) >= 1.0f;
    const float pitchBlendTarget = pitchActive ? 1.0f : 0.0f;
    const float harmonyRatio = std::pow(2.0f, (pitchCents + harmonyCents) / 1200.0f);
    const float delayTarget = std::clamp(delayTime * sampleRate, 1.0f, static_cast<float>(echoLen - 2));

    for (size_t i = 0; i < numFrames; ++i) {
        float dry = input[i];

        float activeSignal = (std::abs(dry) > noiseGateThreshold) ? dry : 0.0f;
        inputEnvelope = inputEnvelope + envCoef * (std::abs(activeSignal) - inputEnvelope);

        // 0b. Pitch Shifter: a read tap whose delay sweeps slowly, so the signal is read faster or
        //     slower than it was written (pitchPhase 0..1 is the tap's position in the sweep). When the
        //     tap reaches the end of its sweep it must jump back, so a second tap, half a sweep away,
        //     covers for it with a short crossfade. Using one tap for most of the sweep keeps the pitch
        //     clean; a constant 50/50 blend of two taps would cancel the fundamental of a steady tone.
        pitchBuffer[pitchWriteHead] = activeSignal;
        pitchBlend += pitchBlendCoef * (pitchBlendTarget - pitchBlend);       // ~10 ms fade in/out, avoids clicks
        float voice = activeSignal;
        if (pitchActive || pitchBlend > 0.001f) {
            pitchPhase += (1.0f - pitchRatio) / pitchWindow;          // delay grows if pitching down
            pitchPhase -= std::floor(pitchPhase);                      // wrap into [0, 1)
            voice = activeSignal * (1.0f - pitchBlend) + pitchTap(pitchPhase) * pitchBlend;
        }
        // 0c. Harmony: a second voice read from the same buffer by its own sweep, harmonyCents away from
        //     the pitch-shifted voice, faded in and out like the shifter.
        harmonyLevel += pitchBlendCoef * (harmonyBlend - harmonyLevel);
        if (harmonyBlend > 0.0f || harmonyLevel > 0.001f) {
            harmonyPhase += (1.0f - harmonyRatio) / pitchWindow;
            harmonyPhase -= std::floor(harmonyPhase);
            voice += pitchTap(harmonyPhase) * harmonyLevel;
        }
        pitchWriteHead = (pitchWriteHead + 1) % kPitchBufSize;

        // 1. Chorus Doubler
        delayLine[delayWriteHead] = voice;
        float chorusLfo = std::sin(lfoChorusPhase);
        lfoChorusPhase += (kTwoPi * 0.5f) / sampleRate;
        if (lfoChorusPhase >= kTwoPi) lfoChorusPhase -= kTwoPi;

        float chorusSamples = chorusDelay + chorusLfo * chorusSwing;
        float readIdxFloat = static_cast<float>(delayWriteHead) - chorusSamples;
        while (readIdxFloat < 0.0f) readIdxFloat += static_cast<float>(kChorusBufSize);
        size_t idx1 = static_cast<size_t>(readIdxFloat) % kChorusBufSize;
        size_t idx2 = (idx1 + 1) % kChorusBufSize;
        float frac = readIdxFloat - static_cast<float>(idx1);
        float doubledVoice = delayLine[idx1] * (1.0f - frac) + delayLine[idx2] * frac;

        float widenedVoice = voice + (doubledVoice * 0.3f);
        delayWriteHead = (delayWriteHead + 1) % kChorusBufSize;

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
        subDetLP1 += subDetCoef * (voice - subDetLP1);
        subDetLP2 += subDetCoef * (subDetLP1 - subDetLP2);
        float detAbs = std::abs(subDetLP2);
        subDetEnv += (detAbs > subDetEnv ? subAttackCoef : subReleaseCoef) * (detAbs - subDetEnv);
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
        subFilterState = subFilterState + subFilterCoef * (rawSub - subFilterState);
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
            float x = folded + 1.0f;
            x -= 4.0f * floorf(x * 0.25f);                     // non-negative modulo (fmod keeps the sign)
            folded = fabsf(x - 2.0f) - 1.0f;                     // triangle, always within [-1, 1]
            const float k = std::min(0.4f * wavefoldDrive, 1.0f); // blend weights stay in [0, 1]
            wet = wet * (1.0f - k) + folded * k;
        }

        // 5. Decimation: hold one sample per `decimationStep` samples *at 44.1 kHz*, so the crunch is the
        //    same at any rate. decimationPhase counts 44.1 kHz samples (kRefRate / sampleRate per sample;
        //    exactly 1 at 44.1 kHz) and takes a new sample each time it reaches 0.
        if (decimationPhase >= 0.0f) {
            heldSample = wet;
            decimationPhase -= static_cast<float>(decimationStep);
        } else {
            wet = heldSample;
        }
        decimationPhase += decimationAdvance;

        const float quantizationLevels = 32.0f; 
        wet = std::round(wet * quantizationLevels) / quantizationLevels;

        // 6. Tone Shaping
        lowpassState = lowpassState + toneCoef * (wet - lowpassState);
        wet = lowpassState;

        // 7. Spectral Freeze / Infinite Sustain Drone Overlay
        //    The loop is the ring's last freezeLoopLen samples; its final freezeFadeLen samples fade into
        //    the samples recorded just before the loop's start, so the loop point does not click.
        if (spectralFreezeActive) {
            const size_t loopStart = freezeStart + freezeFadeLen;
            float frozenSample;
            if (freezePos + freezeFadeLen < freezeLoopLen) {
                frozenSample = mem.freeze[(loopStart + freezePos) % freezeRingLen];
            } else {
                const size_t j = freezePos + freezeFadeLen - freezeLoopLen;     // 0..freezeFadeLen-1
                const float w = static_cast<float>(j) / static_cast<float>(freezeFadeLen);
                frozenSample = mem.freeze[(loopStart + freezePos) % freezeRingLen] * (1.0f - w)
                             + mem.freeze[(freezeStart + j) % freezeRingLen] * w;
            }
            if (++freezePos >= freezeLoopLen) freezePos = 0;
            wet = (wet * 0.3f) + (frozenSample * 0.7f); // Blend live signal with infinite sustain drone
        }

        // 8. Feedback Delay with Infinite Repeat / Self-Oscillation. The delay glides towards the ECHO
        //    TIME setting, so turning it bends the echoes' pitch like a tape delay instead of clicking.
        echoDelaySamples += delayGlideCoef * (delayTarget - echoDelaySamples);
        float delayedSample = readDelayLinear(mem.echo, echoLen, feedbackDelayWriteHead, echoDelaySamples);

        // Feed signal + feedback into delay buffer (feedback >= 1.0 creates infinite repeat / self-oscillation)
        // tanhf bounds the loop: above 1.0 the echo sustains at a steady level instead of growing forever.
        const float echoIn = tanhf(wet + (delayedSample * delayFeedback));
        mem.echo[feedbackDelayWriteHead] = echoIn;
        feedbackDelayWriteHead = (feedbackDelayWriteHead + 1) % echoLen;
        if (!spectralFreezeActive) {                    // the freeze ring records until Freeze is pressed
            mem.freeze[freezeWriteHead] = echoIn;
            freezeWriteHead = (freezeWriteHead + 1) % freezeRingLen;
        }

        wet = (wet * (1.0f - delayMix)) + (delayedSample * delayMix);

        // 9. Catacomb Reverb: DaisySP's ReverbSc (8 modulated delay lines), mono in, left output used.
        if (reverbOk) {
            float revL = 0.0f, revR = 0.0f;
            reverb.Process(wet, wet, &revL, &revR);
            wet = (wet * (1.0f - reverbMix)) + (revL * reverbMix);
        }

        // 10. DC Blocker
        float dcBlocked = wet - dcInPrev + dcPole * dcOutPrev;
        dcInPrev = wet;
        dcOutPrev = dcBlocked;
        wet = dcBlocked;

        // Safety clamp: nothing past full scale reaches the output.
        float processed = std::clamp((dry * (1.0f - wetDryMix)) + (wet * wetDryMix), -1.0f, 1.0f);

        // Crossfade to / from the untouched input when bypass is switched.
        if (bypassTarget) bypassBlend = std::min(1.0f, bypassBlend + bypassStep);
        else if (bypassBlend > 0.0f) bypassBlend = std::max(0.0f, bypassBlend - bypassStep);
        output[i] = (bypassBlend >= 1.0f) ? dry                    // exact, and immune to a blown-up chain
                                          : processed * (1.0f - bypassBlend) + dry * bypassBlend;
    }
}
