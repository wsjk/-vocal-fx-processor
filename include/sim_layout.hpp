#pragma once
// Where everything sits on the simulated pedal. Pure geometry (no graphics library), so bounds, overlaps and
// hit-testing are unit-tested on the host (tests/sim_controls_test.cpp).
//
//   +------------------------------------------------------------------------------+
//   | DEMONIC  (power LED)                 OUTPUT     USB-C     MIC IN   (top jacks) |
//   |  +------------------------+   +---------------- VOICE ----------------+      |
//   |  |   128x64 OLED (x3)     |   |  PITCH      DRIVE      SUB OCT         |      |
//   |  +------------------------+   +-----------------------------------------+      |
//   |  +--------- MANGLE ----------+ +------------------- SPACE -------------------+|
//   |  | ROBOT  CLANK  FOLD  DECIM | | ECHO TIME  DELAY FB  DELAY MIX  REVERB  MIX ||
//   |  +---------------------------+ +---------------------------------------------+|
//   |        (LED)                (LED)                (LED)                          |
//   |       PRESET               FREEZE               BYPASS      (footswitches)     |
//   +------------------------------------------------------------------------------+
#include <algorithm>
#include <cmath>
#include "sim_controls.hpp"

namespace simlayout {

struct Rect { float x = 0, y = 0, w = 0, h = 0; };
struct Circle { float x = 0, y = 0, r = 0; };

inline bool hit_rect(const Rect& r, float px, float py, float margin = 0) {
    return px >= r.x - margin && px <= r.x + r.w + margin && py >= r.y - margin && py <= r.y + r.h + margin;
}
inline bool hit_circle(const Circle& c, float px, float py, float margin = 0) {
    const float dx = px - c.x, dy = py - c.y, rr = c.r + margin;
    return dx * dx + dy * dy <= rr * rr;
}
inline bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
inline bool overlap(const Circle& a, const Circle& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, rr = a.r + b.r;
    return dx * dx + dy * dy < rr * rr;
}
inline bool contains(const Rect& outer, const Rect& in) {
    return in.x >= outer.x && in.y >= outer.y && in.x + in.w <= outer.x + outer.w && in.y + in.h <= outer.y + outer.h;
}
inline bool contains(const Rect& outer, const Circle& c) {
    return c.x - c.r >= outer.x && c.y - c.r >= outer.y && c.x + c.r <= outer.x + outer.w && c.y + c.r <= outer.y + outer.h;
}

enum Footswitch { FS_PRESET, FS_FREEZE, FS_BYPASS, FOOTSWITCH_COUNT };

struct Layout {
    static constexpr int OLED_W = 128, OLED_H = 64, OLED_SCALE = 3;   // screen pixels per OLED pixel
    static constexpr float KNOB_CAP_R = 28.0f;                       // encoder cap
    static constexpr float KNOB_RING_R = 40.0f;                      // outer edge of its LED ring (= hit area)

    int window_w = 970, window_h = 770;
    Rect enclosure, oled_bezel, oled_screen;
    Rect groups[simctl::GROUP_COUNT];          // silkscreened panels around each group of encoders
    Circle encoders[simctl::PARAM_COUNT];      // r = KNOB_RING_R
    Circle footswitches[FOOTSWITCH_COUNT];
    Circle leds[FOOTSWITCH_COUNT];             // one above each footswitch
    Circle led_power;
    Circle jack_out, jack_in;
    Rect usb_port;
    float help_y = 0;                          // keyboard help, under the enclosure

    static Layout make() {
        Layout L;
        L.enclosure = {30, 20, 910, 680};

        const float sw = OLED_W * OLED_SCALE, sh = OLED_H * OLED_SCALE;
        L.oled_screen = {70, 96, sw, sh};
        L.oled_bezel = {L.oled_screen.x - 10, L.oled_screen.y - 10, sw + 20, sh + 20};

        L.groups[simctl::G_VOICE] = {488, 86, 432, 212};      // beside the OLED, same height as its bezel
        L.groups[simctl::G_MANGLE] = {50, 318, 380, 200};     // 4 encoders
        L.groups[simctl::G_SPACE] = {450, 318, 470, 200};     // 5 encoders, so wider

        // Encoders: evenly spaced across their group, one row per group, centred vertically together with the
        // label and value under them (ring top to value bottom is about 2 * ring radius + 31 px).
        int first[simctl::GROUP_COUNT] = {}, count[simctl::GROUP_COUNT] = {};
        for (int p = simctl::PARAM_COUNT - 1; p >= 0; --p) { first[simctl::spec(p).group] = p; ++count[simctl::spec(p).group]; }
        for (int p = 0; p < simctl::PARAM_COUNT; ++p) {
            const int g = simctl::spec(p).group;
            const Rect& r = L.groups[g];
            const float pitch = r.w / count[g];
            const float block = 2 * KNOB_RING_R + 31;
            L.encoders[p] = {r.x + pitch * (p - first[g] + 0.5f), r.y + (r.h - block) / 2 + KNOB_RING_R + 4, KNOB_RING_R};
        }

        const float fx[FOOTSWITCH_COUNT] = {180, 485, 790};
        for (int i = 0; i < FOOTSWITCH_COUNT; ++i) {
            L.footswitches[i] = {fx[i], 616, 40};
            L.leds[i] = {fx[i], 552, 7};
        }
        L.led_power = {222, 47, 5};
        L.jack_out = {640, 52, 14};
        L.usb_port = {720, 42, 52, 20};
        L.jack_in = {870, 52, 14};
        L.help_y = L.enclosure.y + L.enclosure.h + 14;
        return L;
    }
};

/// Mouse wheel / trackpad scroll -> whole encoder clicks. Fractions are carried over in `acc`.
inline int clicks_from_wheel(float& acc, float wheel, float per_click = 0.5f) {
    acc += wheel;
    const int n = static_cast<int>(acc / per_click);
    acc -= n * per_click;
    return n;
}

/// Turning an on-screen encoder with the mouse (same feel as the Phase Box simulator). Press on it, then drag around
/// it (clockwise = up) or, if grabbed near the middle, straight up/down. Returns degrees turned since the last call.
class KnobDrag {
public:
    static constexpr float DEGREES_PER_PIXEL = 2.5f;   // vertical mode
    static constexpr float DRAG_START_PX = 4.0f;       // less movement than this is not a turn yet
    static constexpr float CENTER_ZONE_PX = 14.0f;     // pressed this close to the centre = vertical mode

    void begin(const Circle& knob, float x, float y) {
        cx_ = knob.x; cy_ = knob.y; px_ = x; py_ = y;
        angular_ = std::hypot(x - cx_, y - cy_) > CENTER_ZONE_PX;
        dragged_ = false; last_angle_ = angle_at(x, y); last_y_ = y; active_ = true;
    }

    /// Call with the mouse position every frame while the button is down.
    float move_degrees(float x, float y) {
        if (!active_) return 0.0f;
        if (!dragged_) {
            if (std::hypot(x - px_, y - py_) <= DRAG_START_PX) return 0.0f;
            dragged_ = true; last_angle_ = angle_at(x, y); last_y_ = y;   // count from here, no jump
            return 0.0f;
        }
        if (angular_) {
            const float a = angle_at(x, y);
            float d = a - last_angle_;
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
            last_angle_ = a;
            return d;
        }
        const float d = (last_y_ - y) * DEGREES_PER_PIXEL;
        last_y_ = y;
        return d;
    }

    bool active() const { return active_; }
    void end() { active_ = false; dragged_ = false; }

private:
    float angle_at(float x, float y) const { return std::atan2(y - cy_, x - cx_) * 180.0f / 3.14159265f; }
    float cx_ = 0, cy_ = 0, px_ = 0, py_ = 0, last_angle_ = 0, last_y_ = 0;
    bool angular_ = true, dragged_ = false, active_ = false;
};

} // namespace simlayout
