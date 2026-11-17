// Daisy Seed pedal firmware (STM32H750, Cortex-M7 at 480 MHz): the engine on the codec, with the panel's
// encoders, footswitches, OLED and LED rings. Built beside the benchmark (daisy/Makefile, APP=firmware).
// Compile-checked only: it has not run on a board yet. Layout and wiring: docs/HARDWARE_PLAN.md §3-§4.
//
// One core, split by interrupt:
//   - Audio callback (interrupt, every 64 samples at 48 kHz): reads the shared values below, calls the
//     engine's setters, then processBlock. No I2C, no allocation, no locks.
//   - Main loop: everything else. Polls the eleven GPIO encoders and the footswitches, reads the twelfth
//     encoder (ECHO TIME) on the MCP23017 at most every millisecond, sends the OLED frame in small chunks
//     (the expander shares its I2C bus, so a whole frame at once would block it for ~25 ms), and refreshes
//     the WS2812 rings over SPI DMA.
//   - Between them: std::atomic values, as in the desktop app, using the same simctl logic.
#include <atomic>
#include <cstring>
#include "daisy_seed.h"
#include "dev/mcp23x17.h"
#include "hid/disp/display.h"
#include "hw_pinout.hpp"
#include "sim_controls.hpp"

using namespace daisy;
using simctl::PARAM_COUNT;

namespace {

constexpr size_t kBlockSize = 64;   // 1.33 ms at 48 kHz (HARDWARE_PLAN.md §3, "Latency")
constexpr uint32_t kFrameMs = 33;   // OLED and LED rings: about 30 frames a second
constexpr uint32_t kExpanderPollUs = 1000;
constexpr uint32_t kTouchShowMs = 1500;   // the OLED shows a turned encoder's value this long

DaisySeed hw;

// The engine's long buffers (~576 KB) go in SDRAM; the engine itself (~410 KB) is a static in main(), so it is
// in .bss (AXI SRAM) and constructed after hw.Init() has started the SDRAM (see daisy/main_daisy.cpp).
DemonicEngineMemory DSY_SDRAM_BSS engineMemory;
DemonicEngine* engine = nullptr;
CpuLoadMeter cpuLoad;

// Main loop -> audio callback.
std::atomic<float> param[PARAM_COUNT];
std::atomic<int> activePreset(0);
std::atomic<bool> bypassOn(false);
std::atomic<bool> freezeOn(false);

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    cpuLoad.OnBlockStart();
    static int lastPreset = -1;
    const int preset = activePreset.load();
    if (preset != lastPreset) {
        engine->loadPreset(preset);   // the encoders already hold its values (selectPreset)
        lastPreset = preset;
    }
    engine->setBypass(bypassOn.load());   // before setSpectralFreeze, which is refused while bypassed
    float values[PARAM_COUNT];
    for (int p = 0; p < PARAM_COUNT; ++p) values[p] = param[p].load();
    simctl::apply(*engine, values);
    engine->setSpectralFreeze(freezeOn.load());

    engine->processBlock(in[0], out[0], size);   // mono: the mic is on input 1
    std::memcpy(out[1], out[0], size * sizeof(float));
    cpuLoad.OnBlockEnd();
}

// Move every encoder to a preset's values (pitch is not part of a preset). Values first, then the index, so
// the callback never loads the new preset with the old values.
void selectPreset(int idx) {
    float v[PARAM_COUNT];
    simctl::preset_values(engine->getPreset(idx), param[simctl::P_PITCH].load(), v);
    for (int p = 0; p < PARAM_COUNT; ++p) param[p].store(v[p]);
    activePreset.store(idx);
}

// ---- OLED: SSD1306 128x64 on I2C1 -------------------------------------------------------------------------
// Drawn with libDaisy's graphics (text, lines) and sent in 64-byte chunks, about 1.5 ms each at 400 kHz, so the
// main loop reads the MCP23017 between them. (libDaisy's own SSD130x I2C driver sends one byte per transfer and
// the whole frame in one call.)
class Oled : public OneBitGraphicsDisplayImpl<Oled> {
  public:
    static constexpr uint8_t kAddress = 0x3C;
    static constexpr size_t kChunk = 64;

    void Init(I2CHandle i2c) {
        i2c_ = i2c;
        // libDaisy's SSD130x init sequence for 128x64, in one transfer. Page addressing mode (the default).
        static uint8_t init[] = {0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xDA, 0x12, 0xD3, 0x00, 0x40, 0xA6, 0xA4,
                                 0x8D, 0x14, 0xA1, 0xC8, 0x81, 0x8F, 0xD9, 0x25, 0xDB, 0x34, 0xAF};
        i2c_.TransmitBlocking(kAddress, init, sizeof init, 100);
    }

    uint16_t Height() const override { return 64; }
    uint16_t Width() const override { return 128; }
    void Fill(bool on) override { std::memset(buffer_, on ? 0xFF : 0x00, sizeof buffer_); }
    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on) override {
        if (x >= 128 || y >= 64) return;
        uint8_t& b = buffer_[x + (y / 8) * 128];
        b = on ? (b | (1 << (y % 8))) : (b & ~(1 << (y % 8)));
    }
    // Starts sending the frame; SendChunk() sends it. Draw only while UpdateFinished(), or the frame tears.
    void Update() override { next_ = 0; }
    bool UpdateFinished() override { return next_ >= kChunks; }

    void SendChunk() {
        if (UpdateFinished()) return;
        const uint8_t page = static_cast<uint8_t>(next_ * kChunk / 128), col = static_cast<uint8_t>(next_ * kChunk % 128);
        uint8_t addr[] = {0x00, static_cast<uint8_t>(0xB0 + page), static_cast<uint8_t>(col & 0x0F),
                          static_cast<uint8_t>(0x10 | (col >> 4))};
        i2c_.TransmitBlocking(kAddress, addr, sizeof addr, 10);
        tx_[0] = 0x40;   // data follows
        std::memcpy(tx_ + 1, buffer_ + next_ * kChunk, kChunk);
        i2c_.TransmitBlocking(kAddress, tx_, sizeof tx_, 10);
        ++next_;
    }

  private:
    static constexpr size_t kChunks = 128 * 64 / 8 / kChunk;   // 16
    I2CHandle i2c_;
    uint8_t buffer_[128 * 64 / 8] = {};
    uint8_t tx_[1 + kChunk];
    size_t next_ = kChunks;
};

// ---- WS2812 rings over SPI1 MOSI ---------------------------------------------------------------------------
// SPI1 runs from PLL2P at 25 MHz; /8 = 3.125 MHz, so 4 SPI bits (1.28 us) make one WS2812 bit: 1000 = 0
// (0.32 us high), 1100 = 1 (0.64 us high). The data line idles low between frames (libDaisy keeps the SPI pins
// driven), which is the >280 us reset. Assumed: 12-LED rings, chained in simctl::Param order, LED 0 at the top.
constexpr int kLedsPerRing = 12;
constexpr int kLedCount = hwpins::ENCODER_COUNT * kLedsPerRing;
constexpr size_t kLedBytes = kLedCount * 3 * 4 + 1;   // GRB, 4 SPI bytes per colour byte, plus one low byte
uint8_t DMA_BUFFER_MEM_SECTION ledTx[kLedBytes];      // D2 SRAM: DMA cannot reach the DTCM
SpiHandle ledSpi;
std::atomic<bool> ledBusy(false);

void putLed(int i, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t* o = ledTx + i * 12;
    for (uint8_t c : {g, r, b}) {
        for (int bit = 7; bit >= 1; bit -= 2) {
            *o++ = static_cast<uint8_t>(((c >> bit) & 1 ? 0xC0 : 0x80) | ((c >> (bit - 1)) & 1 ? 0x0C : 0x08));
        }
    }
}

void sendRings() {
    if (ledBusy.load()) return;
    const bool off = bypassOn.load();   // bypassed: rings dark, like the effects
    for (int p = 0; p < PARAM_COUNT; ++p) {
        const float v = param[p].load();
        const simctl::ParamSpec& s = simctl::spec(p);
        for (int k = 0; k < kLedsPerRing; ++k) {
            const bool lit = !off && simctl::ring_led_lit(p, v, k, kLedsPerRing);
            // Red past warn_above (runaway feedback), otherwise amber like the simulator. Kept dim: 144 LEDs.
            const bool warn = lit && v > s.warn_above && !simctl::ring_led_lit(p, s.warn_above, k, kLedsPerRing);
            putLed(p * kLedsPerRing + k, lit ? 24 : 0, lit && !warn ? 10 : 0, 0);
        }
    }
    ledTx[kLedBytes - 1] = 0;
    ledBusy.store(true);
    if (ledSpi.DmaTransmit(ledTx, kLedBytes, nullptr, [](void*, SpiHandle::Result) { ledBusy.store(false); },
                           nullptr) != SpiHandle::Result::OK) {
        ledBusy.store(false);
    }
}

// ---- Controls ----------------------------------------------------------------------------------------------
struct GpioEncoder {
    GPIO a, b;
    simctl::Quadrature q;
};
GpioEncoder gpioEnc[hwpins::ENCODER_COUNT];   // the expander encoder's entry is unused
simctl::Quadrature expanderEnc[hwpins::ENCODER_COUNT];
Mcp23017 expander;
Switch fsBypass, fsFreeze, fsPreset;

int touched = -1;   // last encoder turned, shown on the OLED until touchedUntil
uint32_t touchedUntil = 0;

void turn(int p, int clicks) {
    if (clicks == 0) return;
    param[p].store(simctl::step(p, param[p].load(), clicks));
    touched = p;
    touchedUntil = System::GetNow() + kTouchShowMs;
}

void initControls(I2CHandle::Config& i2cConfig) {
    for (int p = 0; p < hwpins::ENCODER_COUNT; ++p) {
        const hwpins::EncoderPins& e = hwpins::ENCODERS[p];
        if (e.expander) continue;
        gpioEnc[p].a.Init(DaisySeed::GetPin(e.a), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
        gpioEnc[p].b.Init(DaisySeed::GetPin(e.b), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    }
    fsBypass.Init(DaisySeed::GetPin(hwpins::FS_BYPASS));   // momentary, to ground, pull-up (libDaisy defaults)
    fsFreeze.Init(DaisySeed::GetPin(hwpins::FS_FREEZE));
    fsPreset.Init(DaisySeed::GetPin(hwpins::FS_PRESET));

    Mcp23017::Config mc;
    mc.transport_config.Defaults();
    mc.transport_config.i2c_config = i2cConfig;   // the OLED's bus and speed
    mc.transport_config.i2c_address = hwpins::EXPANDER_I2C_ADDR;
    expander.Init(mc);   // all 16 pins inputs with pull-ups
}

void pollGpio() {
    for (int p = 0; p < hwpins::ENCODER_COUNT; ++p) {
        if (hwpins::ENCODERS[p].expander) continue;
        turn(p, gpioEnc[p].q.update(gpioEnc[p].a.Read(), gpioEnc[p].b.Read()));
    }

    fsBypass.Debounce();
    fsFreeze.Debounce();
    fsPreset.Debounce();
    // Bypass: effects off, the dry voice straight through. Freeze cannot survive a bypass (as in the simulator).
    if (fsBypass.RisingEdge()) {
        const bool on = !bypassOn.load();
        bypassOn.store(on);
        if (on) freezeOn.store(false);
    }
    if (fsFreeze.RisingEdge() && !bypassOn.load()) freezeOn.store(!freezeOn.load());
    if (fsPreset.RisingEdge()) {
        selectPreset((activePreset.load() + 1) % engine->getPresetCount());
        touched = -1;   // show the new preset's name
    }
}

void pollExpander() {
    const uint16_t pins = expander.Read();
    for (int p = 0; p < hwpins::ENCODER_COUNT; ++p) {
        const hwpins::EncoderPins& e = hwpins::ENCODERS[p];
        if (e.expander) turn(p, expanderEnc[p].update((pins >> e.a) & 1, (pins >> e.b) & 1));
    }
}

// ---- Display -----------------------------------------------------------------------------------------------
Oled oled;

void drawScreen() {
    char line[32];
    oled.Fill(false);
    oled.SetCursor(0, 0);
    if (touched >= 0 && System::GetNow() < touchedUntil) {
        char val[16];
        simctl::format(touched, param[touched].load(), val, sizeof val);
        std::snprintf(line, sizeof line, "%s %s", simctl::spec(touched).label, val);
    } else {
        const int idx = activePreset.load();
        std::snprintf(line, sizeof line, "%d %s", idx + 1, engine->getPreset(idx).name);
    }
    oled.WriteString(line, Font_7x10, true);
    oled.SetCursor(0, 20);
    oled.WriteString(bypassOn.load() ? "BYPASS" : freezeOn.load() ? "FREEZE" : "ON", Font_7x10, true);
    std::snprintf(line, sizeof line, "DSP %.0f%%  max %.0f%%", cpuLoad.GetAvgCpuLoad() * 100.0f,
                  cpuLoad.GetMaxCpuLoad() * 100.0f);
    oled.SetCursor(0, 54);
    oled.WriteString(line, Font_6x8, true);
    oled.Update();
}

}  // namespace

int main() {
    hw.Init(true);   // true = 480 MHz
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    static DemonicEngine eng(hw.AudioSampleRate(), engineMemory);   // after hw.Init(): see engineMemory
    engine = &eng;
    cpuLoad.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    param[simctl::P_PITCH].store(0.0f);
    selectPreset(0);   // before audio starts: the callback reads the values

    I2CHandle::Config i2cConfig;
    i2cConfig.periph = I2CHandle::Config::Peripheral::I2C_1;
    i2cConfig.speed = I2CHandle::Config::Speed::I2C_400KHZ;   // the SSD1306's limit
    i2cConfig.mode = I2CHandle::Config::Mode::I2C_MASTER;
    i2cConfig.pin_config.scl = DaisySeed::GetPin(hwpins::I2C_SCL);
    i2cConfig.pin_config.sda = DaisySeed::GetPin(hwpins::I2C_SDA);
    I2CHandle i2c;
    i2c.Init(i2cConfig);
    oled.Init(i2c);
    initControls(i2cConfig);

    SpiHandle::Config sc;
    sc.periph = SpiHandle::Config::Peripheral::SPI_1;
    sc.mode = SpiHandle::Config::Mode::MASTER;
    sc.direction = SpiHandle::Config::Direction::TWO_LINES_TX_ONLY;
    sc.nss = SpiHandle::Config::NSS::SOFT;
    sc.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_8;
    sc.pin_config.mosi = DaisySeed::GetPin(hwpins::LED_RINGS);   // SCK, MISO, NSS left unset: not configured
    ledSpi.Init(sc);

    hw.StartAudio(AudioCallback);

    uint32_t lastFrame = 0, lastExpanderUs = 0;
    while (true) {
        pollGpio();
        const uint32_t us = System::GetUs();
        if (us - lastExpanderUs >= kExpanderPollUs) {
            pollExpander();
            lastExpanderUs = us;
        }
        const uint32_t now = System::GetNow();
        if (!oled.UpdateFinished()) {
            oled.SendChunk();
        } else if (now - lastFrame >= kFrameMs) {
            lastFrame = now;
            drawScreen();
            sendRings();
        }
        hw.SetLed(!bypassOn.load());   // the Seed's LED: effects on
    }
}
