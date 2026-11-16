#pragma once
// The pedal's twelve encoders as data: what each one controls, its range, how far one click moves it, and how its
// value is shown. No graphics library, so it is unit-tested on the host (tests/sim_controls_test.cpp) and shared by
// the desktop simulator (desktop/main_desktop.cpp).
//
// The encoders are endless (no end stops on the hardware), so loading a preset can move them. Each one has an LED
// ring that shows its value across a 270 degree sweep, like a pot would.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include "demonic_engine.hpp"

namespace simctl {

enum Param {
    P_PITCH, P_DRIVE, P_SUB,                      // VOICE
    P_ROBOT, P_RATE, P_FOLD, P_DECIM,             // MANGLE
    P_ECHO_TIME, P_DELAY_FB, P_DELAY_MIX, P_REVERB, P_MIX,   // SPACE
    PARAM_COUNT
};

enum Group { G_VOICE, G_MANGLE, G_SPACE, GROUP_COUNT };

struct ParamSpec {
    const char* label;     // silkscreen under the encoder
    const char* fmt;       // printf format of the shown value (value * display_scale)
    float display_scale;
    float min, max;
    float step;            // one encoder click (mouse wheel / arrow); values snap to multiples of it
    bool bipolar;          // LED ring grows from the centre (0) instead of from the min stop
    float warn_above;      // ring turns red above this (e.g. delay feedback > 1 = runaway echo)
    Group group;
};

inline const ParamSpec& spec(int p) {
    static const ParamSpec kSpecs[PARAM_COUNT] = {
        {"PITCH",      "%+.1f st", 0.01f,  -1200.0f, 1200.0f, 100.0f,  true,  INFINITY, G_VOICE},
        {"DRIVE",      "%.1f dB",  1.0f,   0.0f,     12.0f,   0.5f,    false, INFINITY, G_VOICE},
        {"SUB OCT",    "%.0f%%",   100.0f, 0.0f,     1.0f,    0.05f,   false, INFINITY, G_VOICE},
        {"ROBOT",      "%.0f%%",   100.0f, 0.0f,     1.0f,    0.05f,   false, INFINITY, G_MANGLE},
        {"CLANK RATE", "%.0f Hz",  1.0f,   10.0f,    200.0f,  5.0f,    false, INFINITY, G_MANGLE},
        {"FOLD",       "%.2f",     1.0f,   0.0f,     5.0f,    0.1f,    false, INFINITY, G_MANGLE},
        {"DECIMATE",   "x%.1f",    1.0f,   1.0f,     6.0f,    0.5f,    false, INFINITY, G_MANGLE},
        {"ECHO TIME",  "%.0f ms",  1000.0f, 0.02f,   2.0f,    0.02f,   false, INFINITY, G_SPACE},
        {"DELAY FB",   "%.1f%%",   100.0f, 0.0f,     1.05f,   0.025f,  false, 1.0f,     G_SPACE},
        {"DELAY MIX",  "%.0f%%",   100.0f, 0.0f,     1.0f,    0.05f,   false, INFINITY, G_SPACE},
        {"REVERB",     "%.0f%%",   100.0f, 0.0f,     1.0f,    0.05f,   false, INFINITY, G_SPACE},
        {"WET/DRY",    "%.0f%%",   100.0f, 0.0f,     1.0f,    0.05f,   false, INFINITY, G_SPACE},
    };
    return kSpecs[std::clamp(p, 0, PARAM_COUNT - 1)];
}

inline const char* group_name(int g) {
    static const char* kNames[GROUP_COUNT] = {"VOICE", "MANGLE", "SPACE"};
    return kNames[std::clamp(g, 0, GROUP_COUNT - 1)];
}

constexpr float SWEEP_DEGREES = 270.0f;                  // ring runs from -135 (min) to +135 (max); 0 = straight up
constexpr float MIN_DEGREES = -SWEEP_DEGREES / 2.0f;

/// Position of `v` along the ring: 0 = min, 1 = max.
inline float norm(int p, float v) {
    const ParamSpec& s = spec(p);
    return std::clamp((v - s.min) / (s.max - s.min), 0.0f, 1.0f);
}

/// Value at ring position `n` (0..1). A bipolar control snaps to exactly 0 near the centre, so "off" is easy to hit.
inline float from_norm(int p, float n) {
    const ParamSpec& s = spec(p);
    const float v = s.min + std::clamp(n, 0.0f, 1.0f) * (s.max - s.min);
    if (s.bipolar && std::fabs(v) < 0.02f * (s.max - s.min)) return 0.0f;
    return v;
}

/// Turn by whole clicks (+ = clockwise): lands on a multiple of the step and stops at the ends.
inline float step(int p, float v, int clicks) {
    const ParamSpec& s = spec(p);
    const float snapped = std::round((v + clicks * s.step) / s.step) * s.step;
    return std::clamp(snapped, s.min, s.max);
}

/// Ring angle of a value, in degrees: 0 = up, clockwise positive.
inline float degrees(int p, float v) { return MIN_DEGREES + SWEEP_DEGREES * norm(p, v); }

/// Where the lit part of the ring starts: the min stop, or the centre for a bipolar control.
inline float origin_degrees(int p) { return spec(p).bipolar ? degrees(p, 0.0f) : MIN_DEGREES; }

inline void format(int p, float v, char* buf, size_t n) {
    const ParamSpec& s = spec(p);
    std::snprintf(buf, n, s.fmt, v * s.display_scale);
}

/// The values a preset sets. Pitch is not part of a preset, so it keeps `pitch_cents`.
inline void preset_values(const VocalPreset& pr, float pitch_cents, float out[PARAM_COUNT]) {
    out[P_PITCH] = pitch_cents;
    out[P_DRIVE] = pr.driveDb;
    out[P_SUB] = pr.subBlend;
    out[P_ROBOT] = pr.robotBlend;
    out[P_RATE] = pr.robotFreq;
    out[P_FOLD] = pr.wavefoldDrive;
    out[P_DECIM] = pr.decimationFactor;
    out[P_ECHO_TIME] = pr.delayTime;
    out[P_DELAY_FB] = pr.delayFeedback;
    out[P_DELAY_MIX] = pr.delayMix;
    out[P_REVERB] = pr.reverbMix;
    out[P_MIX] = pr.wetDryMix;
}

} // namespace simctl
