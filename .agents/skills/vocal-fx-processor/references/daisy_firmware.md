# Daisy Seed firmware: libDaisy facts

Checked against libDaisy `1289a00` (the commit CI pins in `.github/workflows/ci.yml`). Read this before changing
`daisy/firmware.cpp` or adding a peripheral, so you don't have to dig through libDaisy's source again.

## Build and size

- `daisy/Makefile`: `APP=bench` (default, `main_daisy.cpp` -> `vocal_daisy_bench`) or `APP=firmware`
  (`firmware.cpp` -> `vocal_daisy`). Both share `daisy/build/`, because the flags are identical. `build_daisy.sh` builds both.
- Flash: the `.bin` of a `BOOT_NONE` build is the flash image, so its size equals the linker's `FLASH:` line.
  CI fails above 131,072 B (`.github/scripts/check_flash.sh`).
- **CI builds bigger.** CI uses apt `arm-none-eabi-gcc` 13.2. Locally it is xPack 15.2. The firmware is 120,908 B (92 %)
  in CI and 115,548 B locally; the benchmark is 95,892 B and 93,992 B. Judge headroom by the CI figure. If it gets tight:
  `OPT = -Os` for the firmware only, drop `-u _printf_float` (format values with integers), or move to the Daisy
  bootloader with `APP_TYPE=BOOT_SRAM` (docs/HARDWARE_PLAN.md §3).
- newlib-nano: `printf`/`snprintf` print nothing for `%f` unless the link has `-u _printf_float`.
  The firmware sets it because `simctl::format` uses `%f`. The benchmark logger prints tenths as integers instead.
- Linker warnings `_close is not implemented` etc. and `LOAD segment with RWX permissions` appear in every libDaisy build. Ignore them.

## Memory

- `.bss` goes to the 512 KB AXI SRAM. `DSY_SDRAM_BSS` puts data in SDRAM, which only works after `hw.Init()`.
  `DemonicEngine`'s constructor clears its memory, so the engine is a `static` in `main()`, built after `hw.Init()`.
- DMA buffers need `DMA_BUFFER_MEM_SECTION` (D2 SRAM, `.sram1_bss`): DMA cannot reach the DTCM.

## Clocks

- `hw.Init(true)` gives 480 MHz. SPI1/2/3 run from PLL2P = **25 MHz** (HSE 16 MHz, M 1, N 12.5, P 8), so the
  prescalers give 12.5 / 6.25 / 3.125 / 1.5625 MHz. The WS2812 code uses `PS_8` = 3.125 MHz with 4 SPI bits per LED bit.
- In `TWO_LINES_TX_ONLY` mode the SPI driver configures only the pins you set. Leave unused pins as `Pin()`
  (PORTX); they are skipped. `MasterKeepIOState` is enabled, so MOSI stays driven (low) between transfers.

## Drivers: what to reuse and what to replace

| Driver | Catch | What the firmware does |
| :--- | :--- | :--- |
| `dev/oled_ssd130x.h` `SSD130xI2CTransport` | Sends **one byte per I2C transfer** and the whole frame in `Update()` (tens of ms, blocks the shared bus). Defaults to 1 MHz, which is too fast for SSD1306. | Own `Oled : OneBitGraphicsDisplayImpl<Oled>` with libDaisy's init sequence. It sends 16 chunks of 64 bytes (~1.5 ms each at 400 kHz) and keeps libDaisy's text and line drawing. |
| `dev/mcp23x17.h` `Mcp23017` | Transport defaults: address **0x27**, **1 MHz**. `Init()` makes all 16 pins inputs with pull-ups. `Read()` reads both ports (2 transfers). | Config with the shared 400 kHz I2C config and `hwpins::EXPANDER_I2C_ADDR` (0x20); read at most once per ms. |
| `hid/switch.h` `Switch` | `Debounce()` limits itself to 1 ms with `System::GetNow()`, so calling it every loop is fine. `RisingEdge()` = pressed. | Defaults: momentary, inverted, pull-up. |
| `hid/encoder.h` | Needs a click pin. Our encoders have none, and one is on the expander. | `simctl::Quadrature` (host-tested), the same for GPIO and expander pins. |
| `I2CHandle` | A small handle around a per-peripheral object. Several `Init()`s of `I2C_1` with the same config share the bus. The last `Init()` sets the speed. | One `I2CHandle::Config` (400 kHz) for the OLED and the expander. |

## Pins

- `hwpins` numbers are libDaisy `seed::Dn`. Turn them into a `Pin` with `DaisySeed::GetPin(n)`.
- Audio: `hw.SetAudioBlockSize(64)` and `SetAudioSampleRate(SAI_48KHZ)` before `StartAudio`. The callback is
  `void (AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)`, non-interleaved (`in[0]`, `out[0..1]`).
- Main loop -> callback: `std::atomic<float/int/bool>`. They are lock-free on the M7, so no libatomic is needed.

## Not yet checked on hardware

The WS2812 timing, the ring wiring (12 LEDs per ring, chained in `simctl::Param` order, LED 0 at the top), 4
transitions per encoder detent, and the expander encoder's speed while the OLED is sending. To test on the board, use the
`daisy-hw-test` skill (global) to flash and read the serial output.
