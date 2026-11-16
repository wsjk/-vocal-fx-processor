// DSP behaviour tests for DemonicEngine: does the pitch shifter land on the right note, and does the
// sub-octave really sit one octave below the voice? No microphone, window, or PortAudio needed.
//
// Build & run from the repo root (or use CMake: `ctest`):
//   g++ -std=c++17 -Iinclude tests/engine_dsp_test.cpp src/demonic_engine.cpp -o dsp_test && ./dsp_test
#include "demonic_engine.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <vector>

static const double kFs = 44100.0;
static const double kPi = 3.14159265358979323846;
static const size_t kBlock = 64;

// ---------------------------------------------------------------- signal helpers
struct Noise {                                  // tiny deterministic noise source (no <random> needed)
    uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f; }
};

// Strength of frequency `f` in x (Hann-windowed Goertzel). Larger = more of that frequency.
static double magnitude_at(const std::vector<float>& x, double f) {
    const size_t n = x.size();
    const double w = 2.0 * kPi * f / kFs, coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        double win = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
        double s0 = x[i] * win + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / n;
}

// Frequency (Hz) of the strongest component between fmin and fmax, refined by parabolic interpolation.
static double dominant_freq(const std::vector<float>& x, double fmin, double fmax) {
    const double step = 1.0;
    double best = 0, bestF = fmin, prev = 0, next = 0;
    for (double f = fmin; f <= fmax; f += step) {
        double m = magnitude_at(x, f);
        if (m > best) { best = m; bestF = f; }
    }
    prev = magnitude_at(x, bestF - step);
    next = magnitude_at(x, bestF + step);
    double denom = prev - 2.0 * best + next;
    double shift = (denom != 0.0) ? 0.5 * (prev - next) / denom : 0.0;
    return bestF + shift * step;
}

// Put the engine into "only the effect under test is on" mode.
static void isolate(DemonicEngine& e) {
    e.setDrive(0); e.setSubOctaveBlend(0); e.setRobotBlend(0); e.setWavefold(0);
    e.setDecimation(1); e.setDelayMix(0); e.setReverbMix(0); e.setMix(1); e.setPitchShift(0);
}

// Feed a test signal through the engine. `gen(n)` returns input sample n. Returns the last `keepSeconds`.
template <class Gen>
static std::vector<float> render(DemonicEngine& e, Gen gen, double seconds, double keepSeconds) {
    const size_t total = static_cast<size_t>(seconds * kFs), keep = static_cast<size_t>(keepSeconds * kFs);
    std::vector<float> in(kBlock), out(kBlock), kept;
    kept.reserve(keep);
    for (size_t n = 0; n < total; n += kBlock) {
        for (size_t k = 0; k < kBlock; ++k) in[k] = gen(n + k);
        e.processBlock(in.data(), out.data(), kBlock);
        for (size_t k = 0; k < kBlock; ++k)
            if (n + k >= total - keep) kept.push_back(out[k]);
    }
    return kept;
}

static int g_failures = 0;
static void check(bool ok, const char* fmt, ...) {
    std::printf("[%s] ", ok ? "PASS" : "FAIL");
    va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap);
    std::printf("\n");
    if (!ok) ++g_failures;
}

// ---------------------------------------------------------------- tests
static void test_pitch_shift() {
    std::printf("\n-- Pitch shifter, pure 220 Hz sine: expect 220 x 2^(cents/1200) --\n");
    std::printf("   (A pure tone is the worst case: it locks onto the shifter's sweep rate, so we allow 60 cents.)\n");
    const double inF = 220.0;
    const float cents[] = {-1200, -700, -500, -100, 0, 100, 500, 700, 1200};
    for (float c : cents) {
        DemonicEngine e(static_cast<float>(kFs));
        isolate(e);
        e.setPitchShift(c);
        auto out = render(e, [&](size_t n) { return 0.5f * std::sin(2.0 * kPi * inF * n / kFs); }, 2.5, 1.0);
        double expect = inF * std::pow(2.0, c / 1200.0);
        double got = dominant_freq(out, 60.0, 900.0);
        double errCents = 1200.0 * std::log2(got / expect);
        check(std::fabs(errCents) < 60.0, "%+5.0f cents: expected %6.1f Hz, measured %6.1f Hz (error %+.1f cents)",
              c, expect, got, errCents);
    }
}

static void test_pitch_shift_voice_like() {
    std::printf("\n-- Pitch shifter, voice-like input (150 Hz + harmonics, 5.5 Hz vibrato, noise) --\n");
    const double f0 = 150.0;
    const float cents[] = {-700, -500, -200, 300, 700};
    for (float c : cents) {
        DemonicEngine e(static_cast<float>(kFs));
        isolate(e);
        e.setPitchShift(c);
        Noise noise;
        double ph = 0.0;
        auto out = render(e, [&](size_t n) {
            double f = f0 * (1.0 + 0.01 * std::sin(2.0 * kPi * 5.5 * n / kFs));
            ph += 2.0 * kPi * f / kFs;
            return 0.22f * std::sin(ph) + 0.14f * std::sin(2 * ph + 1.0) + 0.10f * std::sin(3 * ph + 2.0) +
                   0.07f * std::sin(4 * ph + 0.5) + 0.02f * noise.next();
        }, 3.0, 1.0);
        double expect = f0 * std::pow(2.0, c / 1200.0);
        // Strongest component anywhere in 40-450 Hz, versus the strongest component within 4 Hz of the target.
        double strongest = 0, nearTarget = 0;
        for (double f = 40.0; f <= 450.0; f += 1.0) {
            double m = magnitude_at(out, f);
            if (m > strongest) strongest = m;
            if (std::fabs(f - expect) <= 4.0 && m > nearTarget) nearTarget = m;
        }
        // The shifted fundamental may sit slightly below a louder harmonic, so require "within a factor of two".
        check(nearTarget > 0.5 * strongest, "%+5.0f cents: target %6.1f Hz carries %.0f%% of the strongest component",
              c, expect, 100.0 * nearTarget / strongest);
    }
}

static void test_pitch_shift_is_bounded_and_clickless() {
    std::printf("\n-- Pitch shifter: switching on/off mid-note must not blow up or click --\n");
    DemonicEngine e(static_cast<float>(kFs));
    isolate(e);
    std::vector<float> in(kBlock), out(kBlock);
    float peak = 0, maxJump = 0, prev = 0;
    bool finite = true;
    for (size_t blk = 0; blk < 44100 * 3 / kBlock; ++blk) {
        float t = static_cast<float>(blk * kBlock) / 44100.0f;
        e.setPitchShift((static_cast<int>(t) % 2 == 0) ? 0.0f : -500.0f);   // flip every second
        for (size_t k = 0; k < kBlock; ++k) in[k] = 0.5f * std::sin(2.0 * kPi * 220.0 * (blk * kBlock + k) / kFs);
        e.processBlock(in.data(), out.data(), kBlock);
        for (float s : out) {
            if (!std::isfinite(s)) finite = false;
            peak = std::fmax(peak, std::fabs(s));
            maxJump = std::fmax(maxJump, std::fabs(s - prev));
            prev = s;
        }
    }
    check(finite && peak < 1.2f, "output finite and bounded while toggling (peak %.2f)", peak);
    // A 220 Hz sine at amplitude 0.5 never moves more than about 0.5*2*pi*220/44100 = 0.016 per sample;
    // a hard cut between shifted and unshifted audio would jump by far more than that.
    check(maxJump < 0.2f, "no hard discontinuity at the switch (largest sample-to-sample step %.3f)", maxJump);
}

// Sub-octave: compare energy at f/2 with the sub on and off. Off ~ nothing there; on should be large.
static void test_sub_octave() {
    std::printf("\n-- Sub-octave: energy at half the input frequency, sub on vs off --\n");
    const double freqs[] = {110.0, 150.0, 220.0, 330.0, 440.0};
    const float amps[] = {0.1f, 0.3f, 0.6f, 0.9f};
    for (double f : freqs) {
        for (float a : amps) {
            auto run = [&](float subBlend) {
                DemonicEngine e(static_cast<float>(kFs));
                isolate(e);
                e.setSubOctaveBlend(subBlend);
                return render(e, [&](size_t n) { return a * std::sin(2.0 * kPi * f * n / kFs); }, 2.0, 1.0);
            };
            double off = magnitude_at(run(0.0f), f / 2.0);
            double on = magnitude_at(run(1.0f), f / 2.0);
            double ratio = on / (off + 1e-9);
            check(ratio > 8.0, "%3.0f Hz, amp %.1f: energy at %3.0f Hz rises x%.0f with sub on", f, a, f / 2.0, ratio);
        }
    }
}

static void test_sub_octave_voice_like() {
    std::printf("\n-- Sub-octave: harmonic-rich 'voice' with a little noise --\n");
    const double f0 = 150.0;
    auto run = [&](float subBlend) {
        DemonicEngine e(static_cast<float>(kFs));
        isolate(e);
        e.setSubOctaveBlend(subBlend);
        Noise noise;                            // fresh, identical noise for the on and off runs
        auto gen = [&](size_t n) {
            double t = n / kFs;
            return 0.25f * std::sin(2 * kPi * f0 * t) + 0.15f * std::sin(2 * kPi * 2 * f0 * t + 1.0) +
                   0.10f * std::sin(2 * kPi * 3 * f0 * t + 2.0) + 0.10f * std::sin(2 * kPi * 4 * f0 * t + 0.5) +
                   0.01f * noise.next();
        };
        return render(e, gen, 2.0, 1.0);
    };
    double off = magnitude_at(run(0.0f), f0 / 2.0);
    double on = magnitude_at(run(1.0f), f0 / 2.0);
    check(on / (off + 1e-9) > 8.0, "150 Hz harmonic voice: energy at 75 Hz rises x%.0f with sub on", on / (off + 1e-9));
}

static void test_sub_silent_on_silence() {
    std::printf("\n-- Sub-octave: silence in, silence out --\n");
    DemonicEngine e(static_cast<float>(kFs));
    isolate(e); e.setSubOctaveBlend(1.0f);
    auto out = render(e, [](size_t) { return 0.0f; }, 1.0, 1.0);
    float peak = 0; for (float s : out) peak = std::fmax(peak, std::fabs(s));
    check(peak == 0.0f, "no output for silent input (peak %.6f)", peak);
}

// ---------------------------------------------------------------- bypass tests
// Drive the engine block by block the way the desktop app does, calling `hook(sampleIndex)` before each
// block so a test can flip a switch at a block boundary. Records every input and output sample.
template <class Gen, class Hook>
static void run_recorded(DemonicEngine& e, Gen gen, Hook hook, double seconds,
                         std::vector<float>& ins, std::vector<float>& outs) {
    const size_t total = static_cast<size_t>(seconds * kFs);
    std::vector<float> in(kBlock), out(kBlock);
    ins.clear(); outs.clear();
    for (size_t n = 0; n < total; n += kBlock) {
        hook(n);
        for (size_t k = 0; k < kBlock; ++k) in[k] = gen(n + k);
        e.processBlock(in.data(), out.data(), kBlock);
        for (size_t k = 0; k < kBlock; ++k) { ins.push_back(in[k]); outs.push_back(out[k]); }
    }
}

// True for the one block (starting at sample n) that contains sample t. Blocks start at multiples of 64,
// so comparing `n == t` would silently never fire for most t.
static bool block_has(size_t n, size_t t) { return t >= n && t < n + kBlock; }

static void extreme_settings(DemonicEngine& e) {      // everything on, everything loud
    e.setPitchShift(-500); e.setSubOctaveBlend(1); e.setRobotBlend(0.9f); e.setRobotFreq(75);
    e.setWavefold(5); e.setDrive(12); e.setDecimation(5); e.setDelayFeedback(1.05f); e.setDelayMix(0.6f);
    e.setReverbMix(0.8f); e.setMix(1.0f);
}

static void test_bypass_is_exact() {
    std::printf("\n-- Bypass: output is an exact copy of the input --\n");
    DemonicEngine e(static_cast<float>(kFs));
    extreme_settings(e);
    Noise noise;
    std::vector<float> ins, outs;
    const size_t switchAt = 44100;            // effects on for the first second, bypass after
    run_recorded(e, [&](size_t n) { return 0.4f * std::sin(2.0 * kPi * 180.0 * n / kFs) + 0.05f * noise.next(); },
                 [&](size_t n) { if (block_has(n, switchAt)) e.setBypass(true); }, 2.5, ins, outs);
    size_t mismatches = 0; double worst = 0;
    for (size_t i = switchAt + 1024; i < ins.size(); ++i) {   // skip the 5 ms crossfade (and some slack)
        if (outs[i] != ins[i]) { ++mismatches; worst = std::fmax(worst, std::fabs(outs[i] - ins[i])); }
    }
    check(mismatches == 0, "after the crossfade, all %zu samples are bit-for-bit identical to the input (mismatches: %zu)",
          ins.size() - (switchAt + 1024), mismatches);
    // And the effects really were doing something before the switch.
    double diff = 0; for (size_t i = 22050; i < switchAt; ++i) diff = std::fmax(diff, std::fabs(outs[i] - ins[i]));
    check(diff > 0.2, "sanity: with effects on, output differed from input by up to %.2f", diff);
    check(e.isBypassed(), "isBypassed() reports true");
}

static void test_bypass_no_click() {
    std::printf("\n-- Bypass: switching on and off does not click --\n");
    DemonicEngine e(static_cast<float>(kFs));
    isolate(e);
    e.setDrive(6); e.setSubOctaveBlend(0.5f); e.setDelayMix(0.3f); e.setReverbMix(0.3f); e.setMix(0.85f);
    std::vector<float> ins, outs;
    const size_t onAt = 44100, offAt = 44100 * 2;
    run_recorded(e, [&](size_t n) { return 0.5f * std::sin(2.0 * kPi * 220.0 * n / kFs); },
                 [&](size_t n) { if (block_has(n, onAt)) e.setBypass(true); if (block_has(n, offAt)) e.setBypass(false); },
                 3.0, ins, outs);
    auto maxStep = [&](size_t from, size_t to) {
        double m = 0; for (size_t i = from + 1; i < to; ++i) m = std::fmax(m, std::fabs(outs[i] - outs[i - 1])); return m; };
    double baseline = maxStep(22050, onAt);                    // normal effect-chain steps (includes quantisation)
    double engaging = maxStep(onAt, onAt + 2048);
    double releasing = maxStep(offAt, offAt + 2048);
    check(engaging <= 1.5 * baseline + 0.01, "engaging: largest step %.3f vs %.3f normally", engaging, baseline);
    check(releasing <= 1.5 * baseline + 0.01, "releasing: largest step %.3f vs %.3f normally", releasing, baseline);
}

static void test_bypass_clears_tails() {
    std::printf("\n-- Bypass: no stale echo comes back when switched off again --\n");
    DemonicEngine e(static_cast<float>(kFs));
    isolate(e);
    e.setDelayMix(1.0f); e.setDelayFeedback(0.8f); e.setReverbMix(1.0f); e.setMix(1.0f);
    std::vector<float> ins, outs;
    const size_t onAt = static_cast<size_t>(0.6 * kFs), offAt = static_cast<size_t>(0.9 * kFs);
    // 0.5 s of loud sound, then silence. Bypass on shortly after the sound stops, off 0.3 s later.
    run_recorded(e, [&](size_t n) { return (n < 22050) ? 0.8f * std::sin(2.0 * kPi * 300.0 * n / kFs) : 0.0f; },
                 [&](size_t n) { if (block_has(n, onAt)) e.setBypass(true); if (block_has(n, offAt)) e.setBypass(false); },
                 3.0, ins, outs);
    double tail = 0; for (size_t i = offAt + 2048; i < outs.size(); ++i) tail = std::fmax(tail, std::fabs(outs[i]));
    check(tail < 1e-6, "silence after un-bypassing (largest sample %.8f)", tail);

    // Control experiment: without bypass, the same tail is clearly audible at that point.
    DemonicEngine c(static_cast<float>(kFs));
    isolate(c);
    c.setDelayMix(1.0f); c.setDelayFeedback(0.8f); c.setReverbMix(1.0f); c.setMix(1.0f);
    run_recorded(c, [&](size_t n) { return (n < 22050) ? 0.8f * std::sin(2.0 * kPi * 300.0 * n / kFs) : 0.0f; },
                 [](size_t) {}, 3.0, ins, outs);
    double control = 0; for (size_t i = offAt + 2048; i < outs.size() && i < offAt + 2048 + 22050; ++i) control = std::fmax(control, std::fabs(outs[i]));
    check(control > 0.01, "control: without bypass the echoes are still audible (largest sample %.3f)", control);
}

static void test_bypass_then_effects_return() {
    std::printf("\n-- Bypass: effects come back to normal afterwards --\n");
    DemonicEngine e(static_cast<float>(kFs));
    isolate(e); e.setDrive(12); e.setMix(1.0f);
    std::vector<float> ins, outs;
    run_recorded(e, [&](size_t n) { return 0.3f * std::sin(2.0 * kPi * 220.0 * n / kFs); },
                 [&](size_t n) { if (block_has(n, 22050)) e.setBypass(true); if (block_has(n, 44100)) e.setBypass(false); },
                 2.0, ins, outs);
    double diff = 0; bool finite = true;
    for (size_t i = 66150; i < outs.size(); ++i) { diff = std::fmax(diff, std::fabs(outs[i] - ins[i])); finite = finite && std::isfinite(outs[i]); }
    check(finite && diff > 0.05, "after bypass is released the effect is audible again (output differs by up to %.2f)", diff);
}

static void test_bypass_clears_runaway_feedback() {
    std::printf("\n-- Bypass: doubles as a panic button for runaway echo --\n");
    DemonicEngine e(static_cast<float>(kFs));
    isolate(e); e.setDelayFeedback(1.05f); e.setDelayMix(0.3f); e.setMix(1.0f);
    std::vector<float> ins, outs;
    const size_t onAt = 44100 * 25;
    run_recorded(e, [&](size_t n) { return (n < 9000) ? 0.3f * std::sin(2.0 * kPi * 200.0 * n / kFs) : 0.0f; },
                 [&](size_t n) { if (block_has(n, onAt)) e.setBypass(true); if (block_has(n, onAt + 44100)) e.setBypass(false); },
                 28.0, ins, outs);
    double before = 0; for (size_t i = onAt - 22050; i < onAt; ++i) before = std::fmax(before, std::fabs(outs[i]));
    double after = 0; bool finite = true;
    for (size_t i = onAt + 44100 + 2048; i < outs.size(); ++i) { after = std::fmax(after, std::fabs(outs[i])); finite = finite && std::isfinite(outs[i]); }
    check(before > 0.1, "sanity: feedback 1.05 keeps the echo circulating long after the input stopped (peak %.2f)", before);
    check(finite && after < 1e-6, "after a bypass cycle the runaway is gone (peak %.8f)", after);
}

static void test_extremes_stay_bounded() {
    std::printf("\n-- Safety: extreme settings never exceed full scale --\n");
    DemonicEngine e(static_cast<float>(kFs));
    e.loadPreset(1); e.setDrive(12); e.setWavefold(5); e.setDelayFeedback(1.05f); e.setDelayMix(1); e.setReverbMix(1);
    auto out = render(e, [](size_t n) { return (n < 44100) ? 0.9f * std::sin(2.0 * kPi * 220.0 * n / kFs) : 0.0f; }, 60.0, 60.0);
    double peak = 0; bool finite = true;
    for (float v : out) { peak = std::fmax(peak, std::fabs(v)); finite = finite && std::isfinite(v); }
    check(finite && peak <= 1.0, "fold 5, drive 12, feedback 1.05 for 60 s: peak %.3f, all finite", peak);
}

// Like render(), but at any sample rate; returns every output sample.
template <class Gen>
static std::vector<float> render_at(DemonicEngine& e, double fs, Gen gen, double seconds) {
    const size_t total = static_cast<size_t>(seconds * fs);
    std::vector<float> in(kBlock), out(kBlock), all;
    all.reserve(total + kBlock);
    for (size_t n = 0; n < total; n += kBlock) {
        for (size_t k = 0; k < kBlock; ++k) in[k] = gen(n + k);
        e.processBlock(in.data(), out.data(), kBlock);
        all.insert(all.end(), out.begin(), out.end());
    }
    return all;
}

static void test_same_sound_at_48k() {
    std::printf("\n-- Sample rate: 48 kHz (Daisy Seed) must sound like 44.1 kHz (desktop) --\n");
    double echoAt[2], toneRms[2], pitchErr[2];
    const double rates[2] = {44100.0, 48000.0};
    for (int r = 0; r < 2; ++r) {
        const double fs = rates[r];
        {   // Echo time: one click in, find the echo.
            DemonicEngine e(static_cast<float>(fs));
            isolate(e); e.setDelayMix(0.5f); e.setDelayFeedback(0.0f);
            auto out = render_at(e, fs, [](size_t n) { return n == 0 ? 0.9f : 0.0f; }, 0.5);
            size_t from = static_cast<size_t>(0.1 * fs), best = from;
            for (size_t i = from; i < out.size(); ++i) if (std::fabs(out[i]) > std::fabs(out[best])) best = i;
            echoAt[r] = 1000.0 * best / fs;
        }
        {   // Tone filter: level of a 3 kHz sine after the always-on low-pass.
            DemonicEngine e(static_cast<float>(fs));
            isolate(e);
            auto out = render_at(e, fs, [&](size_t n) { return 0.5f * std::sin(2.0 * kPi * 3000.0 * n / fs); }, 1.0);
            double sum = 0; size_t from = out.size() / 2;
            for (size_t i = from; i < out.size(); ++i) sum += out[i] * out[i];
            toneRms[r] = std::sqrt(sum / (out.size() - from));
        }
        {   // Pitch shifter: -500 cents on a 220 Hz sine.
            DemonicEngine e(static_cast<float>(fs));
            isolate(e); e.setPitchShift(-500.0f);
            auto out = render_at(e, fs, [&](size_t n) { return 0.5f * std::sin(2.0 * kPi * 220.0 * n / fs); }, 2.5);
            std::vector<float> tail(out.end() - static_cast<long>(fs), out.end());
            // dominant_freq() assumes 44.1 kHz, so measure in "44.1 kHz Hz" and convert back.
            double got = dominant_freq(tail, 60.0 * 44100.0 / fs, 900.0 * 44100.0 / fs) * fs / 44100.0;
            pitchErr[r] = 1200.0 * std::log2(got / (220.0 * std::pow(2.0, -500.0 / 1200.0)));
        }
    }
    check(std::fabs(echoAt[0] - 200.0) < 1.0 && std::fabs(echoAt[1] - 200.0) < 1.0,
          "echo arrives after 200 ms at both rates (44.1 kHz: %.1f ms, 48 kHz: %.1f ms)", echoAt[0], echoAt[1]);
    const double db = 20.0 * std::log10(toneRms[1] / toneRms[0]);
    check(std::fabs(db) < 0.5, "tone filter passes 3 kHz at the same level (48 kHz vs 44.1 kHz: %+.2f dB)", db);
    check(std::fabs(pitchErr[0]) < 60.0 && std::fabs(pitchErr[1]) < 60.0,
          "-500 cents lands on the right note at both rates (error %+.1f / %+.1f cents)", pitchErr[0], pitchErr[1]);
}

int main() {
    test_same_sound_at_48k();
    test_pitch_shift();
    test_pitch_shift_voice_like();
    test_pitch_shift_is_bounded_and_clickless();
    test_sub_octave();
    test_sub_octave_voice_like();
    test_sub_silent_on_silence();
    test_bypass_is_exact();
    test_bypass_no_click();
    test_bypass_clears_tails();
    test_bypass_then_effects_return();
    test_bypass_clears_runaway_feedback();
    test_extremes_stay_bounded();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
