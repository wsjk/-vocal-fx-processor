// Checks the planned pedal wiring (include/hw_pinout.hpp): every pin exists, none is used twice or
// takes a pin kept free, the I2C and LED pins are on the right peripheral, there is one encoder per parameter,
// and the expander's pins are valid.
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
    std::vector<int> pins = {I2C_SDA, I2C_SCL, FS_BYPASS, FS_FREEZE, FS_PRESET, LED_RINGS};
    for (const EncoderPins& e : ENCODERS)
        if (!e.expander) { pins.push_back(e.a); pins.push_back(e.b); }
    return pins;
}

static std::vector<int> expander_pins() {
    std::vector<int> pins;
    for (const EncoderPins& e : ENCODERS)
        if (e.expander) { pins.push_back(e.a); pins.push_back(e.b); }
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
    check(in_range, "every pin is D0-D%d", GPIO_COUNT - 1);
    check(unique, "no pin is used twice (%zu pins)", pins.size());
    check(clear_of_reserved, "no pin uses the USB HS pins kept free (D0, D29, D30)");

    check(ENCODER_COUNT == simctl::PARAM_COUNT, "one encoder per parameter (%d)", ENCODER_COUNT);

    const std::vector<int> ex = expander_pins();
    bool ex_ok = true;
    for (size_t i = 0; i < ex.size(); ++i) {
        ex_ok = ex_ok && ex[i] >= 0 && ex[i] < EXPANDER_PIN_COUNT;
        for (size_t j = i + 1; j < ex.size(); ++j) ex_ok = ex_ok && ex[i] != ex[j];
    }
    check(ex_ok, "expander pins are GPA0-GPB7 and none is used twice (%zu pins)", ex.size());
    check(ENCODERS[simctl::P_ECHO_TIME].expander, "ECHO TIME is the encoder on the expander");
    check(EXPANDER_I2C_ADDR >= 0x20 && EXPANDER_I2C_ADDR <= 0x27 && EXPANDER_I2C_ADDR != 0x3C,
          "expander address 0x%02X is an MCP23017 address and not the OLED's 0x3C", EXPANDER_I2C_ADDR);
    check(I2C_SCL == 11 && I2C_SDA == 12, "SCL/SDA are I2C1 (D11 = PB8, D12 = PB9)");
    check(LED_RINGS == 10, "LED chain is on SPI1 MOSI (D10 = PB5)");

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
