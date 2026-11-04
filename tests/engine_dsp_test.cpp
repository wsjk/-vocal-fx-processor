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

int main() {
    test_pitch_shift();
    test_pitch_shift_voice_like();
    test_pitch_shift_is_bounded_and_clickless();
    test_sub_octave();
    test_sub_octave_voice_like();
    test_sub_silent_on_silence();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
