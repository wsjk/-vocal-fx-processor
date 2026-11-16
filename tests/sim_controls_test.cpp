// Headless tests for the simulator's enclosure layout and encoder logic (include/sim_layout.hpp,
// include/sim_controls.hpp). No window, raylib or PortAudio needed.
//
//   Build & run with CMake: .agents/skills/vocal-fx-processor/scripts/build_tests.sh
#include "test_engine.hpp"
#include "sim_controls.hpp"
#include "sim_layout.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace simctl;
using namespace simlayout;

static int g_failures = 0;
static void check(bool ok, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::printf("[%s] ", ok ? "PASS" : "FAIL"); std::vprintf(fmt, ap); std::printf("\n");
    va_end(ap);
    if (!ok) ++g_failures;
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void test_layout_fits_and_nothing_overlaps() {
    const Layout L = Layout::make();
    const Rect window = {0, 0, static_cast<float>(L.window_w), static_cast<float>(L.window_h)};
    check(contains(window, L.enclosure), "enclosure is inside the window");
    check(L.help_y + 36 <= L.window_h, "help text fits under the enclosure");
    check(contains(L.enclosure, L.oled_bezel), "OLED bezel is on the enclosure");
    check(near(L.oled_screen.w, 128.0f * Layout::OLED_SCALE) && near(L.oled_screen.h, 64.0f * Layout::OLED_SCALE),
          "OLED screen is 128x64 at x%d", Layout::OLED_SCALE);
    check(contains(L.oled_bezel, L.oled_screen), "OLED screen is inside its bezel");

    bool groups_ok = true;
    for (int g = 0; g < GROUP_COUNT; ++g) {
        groups_ok = groups_ok && contains(L.enclosure, L.groups[g]) && !overlap(L.groups[g], L.oled_bezel);
        for (int h = g + 1; h < GROUP_COUNT; ++h) groups_ok = groups_ok && !overlap(L.groups[g], L.groups[h]);
    }
    check(groups_ok, "encoder groups are on the enclosure and do not overlap each other or the OLED");

    bool knobs_ok = true, apart_ok = true;
    for (int p = 0; p < PARAM_COUNT; ++p) {
        // the encoder, plus room for its label and value under it, sits inside its group's panel
        const Circle& k = L.encoders[p];
        const Rect footprint = {k.x - k.r, k.y - k.r, 2 * k.r, 2 * k.r + 40};
        knobs_ok = knobs_ok && contains(L.groups[spec(p).group], footprint);
        for (int q = p + 1; q < PARAM_COUNT; ++q) {
            // 6 px wheel-hover margin each side, so hovering can never pick two encoders
            Circle a = k, b = L.encoders[q]; a.r += 6; b.r += 6;
            apart_ok = apart_ok && !overlap(a, b);
        }
    }
    check(knobs_ok, "every encoder (with its label) is inside its group panel");
    check(apart_ok, "encoder hover areas never overlap");

    bool feet_ok = true;
    for (int i = 0; i < FOOTSWITCH_COUNT; ++i) {
        const Circle& f = L.footswitches[i];
        const Rect with_label = {f.x - f.r, f.y - f.r, 2 * f.r, 2 * f.r + 26};
        feet_ok = feet_ok && contains(L.enclosure, with_label) && contains(L.enclosure, L.leds[i]);
        for (int g = 0; g < GROUP_COUNT; ++g) feet_ok = feet_ok && !overlap(with_label, L.groups[g]);
        for (int g = 0; g < GROUP_COUNT; ++g) {
            const Rect led = {L.leds[i].x - L.leds[i].r, L.leds[i].y - L.leds[i].r, 2 * L.leds[i].r, 2 * L.leds[i].r};
            feet_ok = feet_ok && !overlap(led, L.groups[g]);
        }
        for (int j = i + 1; j < FOOTSWITCH_COUNT; ++j) feet_ok = feet_ok && !overlap(f, L.footswitches[j]);
    }
    check(feet_ok, "footswitches, their LEDs and labels sit below the panels without overlapping");

    const Rect title_band = {L.enclosure.x, L.enclosure.y, L.enclosure.w, L.oled_bezel.y - L.enclosure.y};
    check(contains(title_band, L.jack_in) && contains(title_band, L.jack_out) && contains(title_band, L.usb_port),
          "jacks and USB port are in the top band, above the OLED");
}

static void test_hit_testing() {
    const Layout L = Layout::make();
    const Circle& k = L.encoders[P_DRIVE];
    check(hit_circle(k, k.x, k.y), "centre of an encoder hits it");
    check(hit_circle(k, k.x + k.r - 1, k.y), "edge of the LED ring hits it");
    check(!hit_circle(k, k.x + k.r + 2, k.y), "just outside the ring misses");
    int hits = 0;
    for (int p = 0; p < PARAM_COUNT; ++p) hits += hit_circle(L.encoders[p], k.x, k.y) ? 1 : 0;
    for (int i = 0; i < FOOTSWITCH_COUNT; ++i) hits += hit_circle(L.footswitches[i], k.x, k.y) ? 1 : 0;
    check(hits == 1, "a click lands on exactly one control (%d)", hits);
}

static void test_param_mapping() {
    bool round_trip = true, ends = true;
    for (int p = 0; p < PARAM_COUNT; ++p) {
        const ParamSpec& s = spec(p);
        ends = ends && near(norm(p, s.min), 0.0f) && near(norm(p, s.max), 1.0f) &&
               near(norm(p, s.min - 100), 0.0f) && near(norm(p, s.max + 100), 1.0f);
        for (float n : {0.0f, 0.25f, 0.8f, 1.0f}) round_trip = round_trip && near(norm(p, from_norm(p, n)), n, 1e-3f);
    }
    check(ends, "ring position is 0 at min, 1 at max, and clamps outside the range");
    check(round_trip, "from_norm and norm are inverses (away from the pitch zero snap)");

    check(from_norm(P_PITCH, 0.5f) == 0.0f && from_norm(P_PITCH, 0.51f) == 0.0f, "pitch snaps to exactly 0 near the centre");
    check(from_norm(P_PITCH, 0.6f) > 200.0f, "pitch does not snap away from the centre");
    check(near(degrees(P_PITCH, 0.0f), 0.0f) && near(origin_degrees(P_PITCH), 0.0f), "pitch ring starts at the top (bipolar)");
    check(near(origin_degrees(P_DRIVE), MIN_DEGREES) && near(degrees(P_DRIVE, 12.0f), -MIN_DEGREES), "drive ring sweeps -135..+135");
}

static void test_encoder_steps() {
    check(near(step(P_PITCH, 0.0f, -1), -100.0f), "one click of pitch = one semitone");
    check(near(step(P_PITCH, 37.0f, 1), 100.0f), "a dragged (off-grid) pitch snaps to the next semitone");
    check(near(step(P_DRIVE, 12.0f, 5), 12.0f) && near(step(P_DRIVE, 0.0f, -3), 0.0f), "encoders stop at the ends");
    check(near(step(P_DELAY_FB, 1.0f, 2), 1.05f), "delay feedback reaches its 1.05 maximum");
    check(near(step(P_RATE, 10.0f, 2), 20.0f), "clank rate moves 5 Hz per click");
}

static void test_format() {
    char buf[32];
    format(P_PITCH, -500.0f, buf, sizeof buf);
    check(std::strcmp(buf, "-5.0 st") == 0, "pitch shows semitones (%s)", buf);
    format(P_SUB, 0.85f, buf, sizeof buf);
    check(std::strcmp(buf, "85%") == 0, "blends show percent (%s)", buf);
    format(P_DELAY_FB, 1.05f, buf, sizeof buf);
    check(std::strcmp(buf, "105.0%") == 0, "feedback shows above 100%% (%s)", buf);
    check(spec(P_DELAY_FB).warn_above == 1.0f, "feedback above 1.0 is flagged (runaway echo)");
}

static void test_presets_move_the_encoders() {
    auto rig = std::make_unique<TestEngine>(44100.0);
    DemonicEngine& e = rig->engine;
    float v[PARAM_COUNT];
    preset_values(e.getPreset(1), -300.0f, v);
    const VocalPreset& p = e.getPreset(1);
    check(v[P_PITCH] == -300.0f, "loading a preset keeps the pitch");
    check(v[P_DRIVE] == p.driveDb && v[P_ROBOT] == p.robotBlend && v[P_DECIM] == p.decimationFactor &&
          v[P_MIX] == p.wetDryMix, "loading a preset sets the other encoders to its values");
    bool in_range = true;
    for (int i = 0; i < e.getPresetCount(); ++i) {
        preset_values(e.getPreset(i), 0.0f, v);
        for (int q = 0; q < PARAM_COUNT; ++q) in_range = in_range && v[q] >= spec(q).min && v[q] <= spec(q).max;
    }
    check(in_range, "every preset value is inside its encoder's range");
    check(std::strcmp(e.getPreset(e.getPresetCount()).name, e.getPreset(0).name) == 0 &&
          std::strcmp(e.getPreset(-1).name, e.getPreset(e.getPresetCount() - 1).name) == 0, "preset index wraps");
}

static void test_knob_drag() {
    const Circle k = {100, 100, 40};
    KnobDrag d;
    d.begin(k, 100, 70);                                   // top of the ring: circular mode
    check(d.move_degrees(102, 70) == 0.0f, "tiny movement is not a turn");
    d.move_degrees(110, 70);                               // drag starts here
    const float deg = d.move_degrees(130, 100);            // round to the right side, clockwise
    check(deg > 30.0f, "dragging clockwise round the ring turns up (%.1f deg)", deg);
    d.end();
    d.begin(k, 100, 100);                                  // centre: vertical mode
    d.move_degrees(100, 90);
    check(d.move_degrees(100, 80) > 0.0f, "dragging up from the centre turns up");
    float acc = 0.0f;
    check(clicks_from_wheel(acc, 0.3f) == 0 && clicks_from_wheel(acc, 0.3f) == 1, "scroll fractions add up to a click");
}

int main() {
    test_layout_fits_and_nothing_overlaps();
    test_hit_testing();
    test_param_mapping();
    test_encoder_steps();
    test_format();
    test_presets_move_the_encoders();
    test_knob_drag();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
