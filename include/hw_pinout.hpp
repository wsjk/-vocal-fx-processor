#pragma once
// Planned wiring of the hardware pedal: WeAct RP2350B Core + WM8731 codec. Single source of truth for
// pin numbers; docs/HARDWARE_PLAN.md explains the choices. Plain constants (no Pico SDK), so the desktop
// tests can check them (tests/hw_pinout_test.cpp). No firmware reads them yet.
#include <cstdint>

namespace hwpins {

// I2C0, shared by the WM8731 (control registers, address 0x1A) and the SSD1306 OLED (0x3C).
// GP8/GP9 are the board's default I2C0 pins; GP0 is kept free (see RESERVED).
constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;

// I2S to the WM8731 (PIO0). The RP2350 is clock master. BCLK and LRCLK must be neighbours (pico-extras audio_i2s).
constexpr uint8_t I2S_BCLK  = 11;
constexpr uint8_t I2S_LRCLK = 12;
constexpr uint8_t I2S_DOUT  = 13;   // RP2350 -> WM8731 DAC
constexpr uint8_t I2S_DIN   = 14;   // WM8731 ADC -> RP2350

// WM8731 master clock: the board's 12 MHz crystal, sent out unchanged on clock output clk_gpout0
// (GP21). The codec runs in its 12 MHz "USB mode", which gives exact 44.1 kHz / 48 kHz with no PIO
// clock and no fractional-divider jitter.
constexpr uint8_t CODEC_MCLK = 21;

// Footswitches, to ground (internal pull-ups).
constexpr uint8_t FS_BYPASS = 2;
constexpr uint8_t FS_FREEZE = 3;
constexpr uint8_t FS_PRESET = 4;

// One WS2812 chain for the eleven encoder LED rings.
constexpr uint8_t LED_RINGS = 22;

// Encoders: {A, B} per encoder, in simctl::Param order (PITCH, DRIVE, SUB OCT, ROBOT, CLANK RATE, FOLD,
// DECIMATE, DELAY FB, DELAY MIX, REVERB, WET/DRY). Common pin to ground.
struct EncoderPins { uint8_t a, b; };
constexpr EncoderPins ENCODERS[] = {
    {15, 16}, {17, 18}, {19, 20},                         // VOICE
    {26, 27}, {28, 29}, {30, 31}, {32, 33},               // MANGLE
    {34, 35}, {36, 37}, {38, 39}, {5, 6},                 // SPACE
};
constexpr int ENCODER_COUNT = sizeof(ENCODERS) / sizeof(ENCODERS[0]);

// Pins the board already uses or that are kept free: GP0 = optional PSRAM chip select, GP23 = user
// button, GP25 = LED, GP40-GP47 = the only ADC-capable pins (kept for a future analog input).
constexpr uint8_t RESERVED[] = {0, 23, 25, 40, 41, 42, 43, 44, 45, 46, 47};

constexpr int GPIO_COUNT = 48;   // RP2350B

} // namespace hwpins
