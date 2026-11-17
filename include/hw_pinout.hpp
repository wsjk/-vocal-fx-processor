#pragma once
// Planned wiring of the hardware pedal: Electrosmith Daisy Seed 65MB. Single source of truth for pin
// numbers; docs/HARDWARE_PLAN.md explains the choices. Numbers are libDaisy pin names (n means seed::Dn),
// not header pin numbers. Plain constants (no libDaisy), so the desktop tests can check them
// (tests/hw_pinout_test.cpp); daisy/firmware.cpp reads them. Audio uses the Seed's dedicated audio pins
// and the codec on the board, so it needs no GPIO here.
#include <cstdint>

namespace hwpins {

// I2C1 (D11 = PB8 SCL, D12 = PB9 SDA), for the SSD1306 OLED (0x3C).
constexpr uint8_t I2C_SCL = 11;
constexpr uint8_t I2C_SDA = 12;

// One WS2812 chain for the twelve encoder LED rings, driven by SPI1 MOSI (D10 = PB5) in
// transmit-only master mode. The Daisy has no PIO; in that mode libDaisy configures only MOSI,
// so D8 (SPI1 SCK) is free for a footswitch.
constexpr uint8_t LED_RINGS = 10;

// Footswitches, to ground (internal pull-ups).
constexpr uint8_t FS_BYPASS = 7;
constexpr uint8_t FS_FREEZE = 8;
constexpr uint8_t FS_PRESET = 9;

// MCP23017 16-bit GPIO expander on the OLED's I2C1 bus, at 0x20 (A0-A2 to ground). The Seed's 31 GPIO
// are all used or kept free, so the twelfth encoder (ECHO TIME) is on the expander's GPA0/GPA1, with its
// internal pull-ups. The firmware polls it between OLED transfers (docs/HARDWARE_PLAN.md §4).
constexpr uint8_t EXPANDER_I2C_ADDR = 0x20;
constexpr int EXPANDER_PIN_COUNT = 16;   // GPA0-7 = 0-7, GPB0-7 = 8-15

// Encoders: {A, B} per encoder, in simctl::Param order (PITCH, DRIVE, SUB OCT, ROBOT, CLANK RATE, FOLD,
// DECIMATE, ECHO TIME, DELAY FB, DELAY MIX, REVERB, WET/DRY). Common pin to ground. On the Seed they take
// the SD card pins (D1-D6) and all twelve ADC pins (D15-D25, D28); `expander` means MCP23017 pins instead.
struct EncoderPins { uint8_t a, b; bool expander = false; };
constexpr EncoderPins ENCODERS[] = {
    {1, 2}, {3, 4}, {5, 6},                               // VOICE
    {13, 14}, {15, 16}, {17, 18}, {19, 20},               // MANGLE
    {0, 1, true},                                         // SPACE: ECHO TIME on MCP23017 GPA0/GPA1
    {21, 22}, {23, 24}, {25, 26}, {27, 28},
};
constexpr int ENCODER_COUNT = sizeof(ENCODERS) / sizeof(ENCODERS[0]);

// Kept free for a later USB port (for example USB MIDI): D0 = USB HS ID (PB12),
// D29/D30 = USB HS D-/D+ (PB14/PB15).
constexpr uint8_t RESERVED[] = {0, 29, 30};

constexpr int GPIO_COUNT = 31;   // seed::D0-D30

} // namespace hwpins
