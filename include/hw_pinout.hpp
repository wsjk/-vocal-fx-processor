#pragma once
// Planned wiring of the hardware pedal: Electrosmith Daisy Seed 65MB. Single source of truth for pin
// numbers; docs/HARDWARE_PLAN.md explains the choices. Numbers are libDaisy pin names (n means seed::Dn),
// not header pin numbers. Plain constants (no libDaisy), so the desktop tests can check them
// (tests/hw_pinout_test.cpp). No firmware reads them yet. Audio uses the Seed's dedicated audio pins
// and the codec on the board, so it needs no GPIO here.
#include <cstdint>

namespace hwpins {

// I2C1 (D11 = PB8 SCL, D12 = PB9 SDA), for the SSD1306 OLED (0x3C).
constexpr uint8_t I2C_SCL = 11;
constexpr uint8_t I2C_SDA = 12;

// One WS2812 chain for the eleven encoder LED rings, driven by SPI1 MOSI (D10 = PB5) in
// transmit-only master mode. The Daisy has no PIO; in that mode libDaisy configures only MOSI,
// so D8 (SPI1 SCK) is free for a footswitch.
constexpr uint8_t LED_RINGS = 10;

// Footswitches, to ground (internal pull-ups).
constexpr uint8_t FS_BYPASS = 7;
constexpr uint8_t FS_FREEZE = 8;
constexpr uint8_t FS_PRESET = 9;

// Encoders: {A, B} per encoder, in simctl::Param order (PITCH, DRIVE, SUB OCT, ROBOT, CLANK RATE, FOLD,
// DECIMATE, DELAY FB, DELAY MIX, REVERB, WET/DRY). Common pin to ground. These take the SD card pins
// (D1-D6) and all twelve ADC pins (D15-D25, D28).
struct EncoderPins { uint8_t a, b; };
constexpr EncoderPins ENCODERS[] = {
    {1, 2}, {3, 4}, {5, 6},                               // VOICE
    {13, 14}, {15, 16}, {17, 18}, {19, 20},               // MANGLE
    {21, 22}, {23, 24}, {25, 26}, {27, 28},               // SPACE
};
constexpr int ENCODER_COUNT = sizeof(ENCODERS) / sizeof(ENCODERS[0]);

// Kept free for a later USB port (for example USB MIDI): D0 = USB HS ID (PB12),
// D29/D30 = USB HS D-/D+ (PB14/PB15).
constexpr uint8_t RESERVED[] = {0, 29, 30};

constexpr int GPIO_COUNT = 31;   // seed::D0-D30

} // namespace hwpins
