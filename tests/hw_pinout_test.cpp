// Checks the planned pedal wiring (include/hw_pinout.hpp): every pin exists, none is used twice or
// collides with the board, the I2C/I2S/clock pins can do their job, and there is one encoder per parameter.
//
//   g++ -std=c++17 -Iinclude tests/hw_pinout_test.cpp -o hw_pinout_test && ./hw_pinout_test
#include "hw_pinout.hpp"
#include "sim_controls.hpp"
#include <cstdarg>
#include <cstdio>
#include <vector>

using namespace hwpins;

static int g_failures = 0;
static void check(bool ok, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::printf("[%s] ", ok ? "PASS" : "FAIL"); std::vprintf(fmt, ap); std::printf("\n");
    va_end(ap);
    if (!ok) ++g_failures;
}

static std::vector<int> used_pins() {
    std::vector<int> pins = {I2C_SDA, I2C_SCL, I2S_BCLK, I2S_LRCLK, I2S_DOUT, I2S_DIN, CODEC_MCLK,
                             FS_BYPASS, FS_FREEZE, FS_PRESET, LED_RINGS};
    for (const EncoderPins& e : ENCODERS) { pins.push_back(e.a); pins.push_back(e.b); }
    return pins;
}

int main() {
    const std::vector<int> pins = used_pins();

    bool in_range = true, unique = true, clear_of_reserved = true;
    for (size_t i = 0; i < pins.size(); ++i) {
        in_range = in_range && pins[i] >= 0 && pins[i] < GPIO_COUNT;
        for (size_t j = i + 1; j < pins.size(); ++j) unique = unique && pins[i] != pins[j];
        for (uint8_t r : RESERVED) clear_of_reserved = clear_of_reserved && pins[i] != r;
    }
    check(in_range, "every pin is GP0-GP%d", GPIO_COUNT - 1);
    check(unique, "no pin is used twice (%zu pins)", pins.size());
    check(clear_of_reserved, "no pin uses the board's LED, button, PSRAM select or the ADC pins");

    check(ENCODER_COUNT == simctl::PARAM_COUNT, "one encoder per parameter (%d)", ENCODER_COUNT);
    check(I2C_SDA % 4 == 0 && I2C_SCL % 4 == 1, "SDA/SCL are I2C0 pins (GPIO 4n / 4n+1)");
    check(I2S_LRCLK == I2S_BCLK + 1, "I2S word clock is the pin after the bit clock");
    const int gpout[] = {13, 15, 21, 23, 24, 25};   // RP2350 clock-output pins
    bool mclk_ok = false;
    for (int g : gpout) mclk_ok = mclk_ok || CODEC_MCLK == g;
    check(mclk_ok, "codec MCLK is on a clock-output pin (GP%d)", CODEC_MCLK);

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
