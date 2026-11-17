# Vocal FX Processor — Developer Guide

**Audience:** you can read and write Python, but C and C++ are new to you. This guide explains how the project is organised, how audio flows through it, and — along the way — the handful of C++ ideas you need to read and change the code with confidence. It does not assume any audio-programming background either.

Line numbers (`file:line`) refer to the code as it exists in the repository snapshot this guide was written against. If you have edited the files, they may have shifted.

---

## Contents

1. [Project at a glance](#1-project-at-a-glance)
2. [Audio concepts in five minutes](#2-audio-concepts-in-five-minutes)
3. [C++ for Python programmers](#3-c-for-python-programmers)
4. [Architecture](#4-architecture)
5. [Walkthrough: the desktop app](#5-walkthrough-the-desktop-app-desktopmain_desktopcpp)
6. [Walkthrough: the DSP engine](#6-walkthrough-the-dsp-engine)
7. [The build system](#7-the-build-system-cmakeliststxt)
8. [How to extend it](#8-how-to-extend-it)
9. [Testing without a microphone](#9-testing-without-a-microphone)
10. [Code review: known issues](#10-code-review-known-issues)
11. [Toward the Daisy Seed](#11-toward-the-daisy-seed)
12. [Glossary](#12-glossary)

---

## 1. Project at a glance

**What it is.** A real-time vocal effects processor. A microphone signal goes in, passes through a chain of ten effect stages, and comes out of the speakers. The goal is a standalone guitar-pedal-style device built on an Electrosmith Daisy Seed, with a desktop version that simulates the whole pedal (enclosure, 128×64 OLED, encoders, footswitches) so the effects can be developed on a laptop first.

**Technology**

| Piece | Role |
|---|---|
| **C++17** | Language of everything. |
| **PortAudio** | Cross-platform library for talking to the sound card. |
| **raylib** | Small graphics library that draws the simulated pedal. |
| **CMake** | Build tool — decides which files to compile and what to link together. |
| **DaisySP** (one file) | Electrosmith's DSP library. Only its ReverbSc reverb, from the LGPL-2.1 DaisySP-LGPL part, is used; CMake downloads it. |

**File map**

```
vocal-fx-processor/
├── CMakeLists.txt              Build recipe: engine library, tests, desktop app
├── README.md                   Overview and quickstart
├── AGENTS.md                   Notes for coding agents (CLAUDE.md is a symlink to it)
├── docs/                       This guide and the User Guide
├── .agents/skills/             Agent runbook, reference notes and build scripts
├── include/
│   ├── demonic_engine.hpp      The DSP engine's "interface": class, its long-buffer memory, preset struct [implemented]
│   ├── sim_controls.hpp        Simulator: the 12 encoders as data (range, step, display, presets)
│   ├── sim_layout.hpp          Simulator: enclosure geometry, hit-testing, mouse-drag knob turning
│   ├── hw_pinout.hpp           Planned hardware wiring: Daisy Seed pins and the I²C expander, for OLED, LEDs, encoders, footswitches
│   ├── engine_bench.hpp        Benchmark scenarios and timing loop for daisy/
│   ├── sim_font.hpp            Simulator: embedded DejaVu Sans Mono font (same as Phase Box)
│   └── dsp_pipeline.hpp        EMPTY (0 bytes), not used by the build
├── src/
│   ├── demonic_engine.cpp      The DSP engine's code: pitch shifter + the effect stages [implemented]
│   └── dsp_pipeline.cpp        EMPTY (0 bytes), not used by the build
├── desktop/
│   └── main_desktop.cpp        Desktop app: audio I/O + the simulated pedal (raylib)  [implemented]
├── daisy/
│   ├── Makefile                Separate build for the Daisy Seed (needs libDaisy)
│   ├── main_daisy.cpp          Benchmark: how much real-time budget does the engine use on the Daisy Seed?
│   └── firmware.cpp            Pedal firmware: engine in the audio callback, panel in the main loop (compile-checked only)
├── tests/
│   ├── engine_dsp_test.cpp     Measures the sound: sample rates, pitch, harmony, echo, freeze, reverb, sub-octave, bypass [built, in ctest]
│   ├── test_engine.hpp         TestEngine: an engine plus its memory, on the heap, for the tests
│   ├── sim_controls_test.cpp   Simulator layout and encoder logic, headless                     [built, in ctest]
│   ├── hw_pinout_test.cpp      Planned pin map: no clashes, right pin types, one encoder each   [built, in ctest]
│   └── engine_smoke_test.cpp   OUT OF DATE: calls methods that no longer exist; not built (section 9)
├── portaudio/                  Vendored PortAudio source. Last-resort fallback only; listed in .gitignore
└── .gitignore                  Ignores build/, build-tests/ and portaudio/
```

The product itself is two files: the **engine** (`demonic_engine.hpp`/`.cpp`, about 300 lines of `.cpp`, all of the sound) and the **desktop app** (`main_desktop.cpp`, about 250 lines, everything around it). The engine has no dependency on audio hardware or graphics, which is why it can be tested and benchmarked by itself. `dsp_pipeline.*` are empty placeholders and can be deleted.

---

## 2. Audio concepts in five minutes

**Samples.** Digital audio is a long list of numbers. Each number (a *sample*) is the speaker-cone position at one instant. Here each is a `float` between roughly **−1.0 and +1.0**; ±1.0 is "full scale". Values outside that range get clipped (flattened, harshly) by the sound hardware.

**Sample rate.** How many samples per second. This project uses **44,100** (CD quality). One sample lasts about 22.7 µs.

**Blocks (buffers).** Software never handles one sample at a time through the sound card; it gets a *block* — here **64 samples**, which lasts 64 / 44,100 ≈ **1.45 ms**. Smaller blocks mean less delay but less time to compute each one.

**The real-time deadline.** The sound card wants a new block every 1.45 ms, forever. If your code is late, the audio glitches. So the code that runs per block (the *audio callback*) follows strict rules: no printing, no file access, no memory allocation, no waiting on locks. Think of it like a game's frame deadline, but 700 times per second.

**Decibels.** Loudness in dB is logarithmic. To convert gain in dB to a multiplier: `multiplier = 10 ** (dB / 20)`. So +12 dB ≈ ×4, and +6 dB ≈ ×2. (`processBlock` line 141.)

**Common effect building blocks used in this project** (each is explained in section 6):

| Idea | One-line explanation |
|---|---|
| **Circular buffer** | A fixed-size array treated as a loop, used to remember recent audio so it can be replayed later (delay, reverb). |
| **One-pole low-pass filter** | `y += a * (x - y)`. A running average that smooths out fast changes, thereby removing high frequencies. |
| **LFO** | "Low-frequency oscillator" — a slow sine wave used to modulate something. |
| **Ring modulation** | Multiply the voice by a sine wave. Creates metallic, robotic sounds. |
| **Saturation** | Squashing loud peaks with a curve (here `tanh`) — adds distortion. |
| **Wavefolding** | Instead of flattening peaks, bend them back down. Adds bright harmonics. |
| **Decimation / bit-crushing** | Hold each sample for several ticks and round to coarse levels: lo-fi digital grit. |
| **DC blocker** | A high-pass filter that removes a constant offset from the signal. |

---

## 3. C++ for Python programmers

You do not need to learn all of C++. This section covers what is used in this codebase, mapped to Python.

### 3.1 It is compiled and statically typed

Python runs your source directly. C++ is first translated by a **compiler** into machine code, and every variable must have a declared type:

```cpp
float driveDb = 11.0f;     // Python: drive_db = 11.0
int   count   = 0;         // Python: count = 0
bool  frozen  = false;     // Python: frozen = False
```

Type errors are caught at *compile* time. The `f` in `11.0f` marks a 32-bit `float` literal (plain `11.0` would be a 64-bit `double`).

### 3.2 Headers and source files (`.hpp` / `.cpp`)

C++ splits a module in two:

- **Header (`.hpp`)** — *declarations*: "a class called `DemonicEngine` exists, and it has these methods." Like a type stub (`.pyi`) or an abstract interface.
- **Source (`.cpp`)** — *definitions*: the actual function bodies.

Other files `#include` the header to learn what exists:

```cpp
#include "demonic_engine.hpp"    // Python: from demonic_engine import DemonicEngine
#include <cmath>                 // Python: import math   (angle brackets = library headers)
```

`#pragma once` at the top of a header means "only include me once, even if several files ask" — a guard against duplicate definitions.

### 3.3 Scalar types you will see

| C++ | Meaning | Python-ish |
|---|---|---|
| `float` | 32-bit decimal number | `float` (less precise) |
| `int` | whole number | `int` (but overflows at ±2.1 billion) |
| `bool` | true/false | `bool` |
| `size_t` | unsigned whole number for sizes and indexes | `int` that can't be negative |
| `unsigned long` | big unsigned integer (PortAudio's choice for frame counts) | `int` |
| `const char*` | text: pointer to characters in memory | `str` (but much more primitive) |

Converting between types is explicit: `static_cast<int>(decimationFactor)` ≈ Python's `int(decimation_factor)`. It truncates toward zero, so `3.9` becomes `3`.

### 3.4 Classes and structs

```cpp
struct VocalPreset {            // like a @dataclass: just named fields
    const char* name;
    float driveDb;
    ...
};

class DemonicEngine {           // like a Python class
public:                         // callable from outside
    DemonicEngine(float sampleRate);                    // constructor ≈ __init__
    void processBlock(const float* input, float* output, size_t numFrames);
private:                        // internal state; only the class's own methods may touch it
    float sampleRate;           // ≈ self.sample_rate
    ...
};
```

Differences worth knowing:

- **No `self`.** Inside a method, member variables are just used by name: `driveDb`, not `self.driveDb`.
- **`DemonicEngine::processBlock`** (in the `.cpp`) means "the `processBlock` that belongs to `DemonicEngine`". `::` is the "belongs to" operator.
- **`public`/`private` is enforced.** Code outside the class cannot touch private members.
- **`void`** means "returns nothing". Every function declares its return type (`float`, `bool`, `const char*`, …).
- **`const` after a method** — `bool isSpectralFrozen() const;` — promises the method does not modify the object.

### 3.5 The constructor's initializer list

```cpp
DemonicEngine::DemonicEngine(float sr)
    : sampleRate(sr), pitchCents(0.0f), subBlend(0.85f), ... {
    // body runs after the list
}
```

The part after the `:` sets each member's starting value *before* the body runs — roughly the first lines of `__init__`. One gotcha: members are really initialised in the order they are **declared in the header**, not the order you write them in this list. An earlier version of this project had the list in a different order from the header, which compilers flag with `-Wreorder`. It was harmless there (no member's starting value depended on another's) but it is a classic source of bugs, so the list now matches the header exactly. Keep the two in sync when you add members.

### 3.6 Arrays have a fixed size and no safety net

```cpp
float delayLine[2048];    // Python: delay_line = [0.0] * 2048   — but it can never grow
```

- The size is fixed at compile time, and the memory lives *inside* the object. There is no `append`.
- **There is no bounds checking.** Python raises `IndexError` for `delay_line[5000]`. C++ silently reads or overwrites whatever memory happens to be there — a crash at best, subtle corruption at worst. This is why you see `% 2048` almost everywhere an index is computed: it wraps the index back into range.

### 3.7 Pointers (the one big idea)

Look at the engine's main function signature:

```cpp
void processBlock(const float* input, float* output, size_t numFrames);
```

`float*` means "a pointer to a float" — in practice, **the memory address of the first element of an array of floats**. The function receives *where* the data lives, not a copy of it. You index it like a list: `input[i]`.

Mapping this to Python:

```python
def process_block(input, output, num_frames):    # `input` and `output` are pre-existing arrays
    for i in range(num_frames):
        output[i] = ...                          # fills the caller's array in place; returns nothing
```

- `const float*` = "you may read this, not modify it" (the input).
- `float*` = "you may write into this" (the output). C/C++ often delivers results by filling a buffer the caller supplied instead of returning a new list. That is deliberate: no allocation, and therefore no unpredictable pauses.
- `nullptr` is the "no value" pointer, like `None`. The audio callback checks `if (in != nullptr)`.
- `&` in `std::atomic<float>& target` (a *reference*) is a nicer pointer: another name for an existing variable, like passing a mutable object in Python.

You will never need to do manual memory management here. The project **allocates nothing dynamically** — all buffers are fixed-size arrays inside `DemonicEngine`.

### 3.8 Other syntax you will meet

| C++ | Python equivalent / meaning |
|---|---|
| `for (size_t i = 0; i < n; ++i) { ... }` | `for i in range(n):` (`++i` adds one) |
| `x % 2048` | `x % 2048` (same; but on negatives C++ gives a negative result — see section 10.4) |
| `cond ? a : b` | `a if cond else b` |
| `!flag`, `&&`, `\|\|` | `not flag`, `and`, `or` |
| `static int lastPreset = -1;` *inside a function* | A variable that keeps its value between calls, like a function attribute or closure state |
| `#define SAMPLE_RATE 44100` | A named constant, substituted by text replacement before compilation |
| `constexpr size_t SCOPE_SIZE = 128;` | A constant computed at compile time |
| `std::` prefix (`std::max`, `std::sin`) | Names from the standard library, like `math.sin`. `std::` is the module prefix |
| `auto f = [&](args) { ... };` | A lambda / nested `def` that can see surrounding variables. `[&]` = "capture them by reference" |
| `std::vector<float>` | A resizable list of floats (`list[float]`) |
| `M_PI` | `math.pi` (but it is a 64-bit double — see section 11) |

### 3.9 `std::atomic` — the mailbox between threads

Python's Global Interpreter Lock quietly protects you from many threading hazards; C++ has no GIL. If one thread writes a float while another is reading it, the result is formally undefined. `std::atomic<float>` wraps a value so that each `.load()` and `.store()` is indivisible. In this project it is used as a *mailbox*: the UI thread drops in an encoder value with `.store(x)`, and the audio thread reads it with `.load()`. See section 4.

### 3.10 `main`, compiling, and linking

`int main() { ... }` is the program's entry point (like `if __name__ == "__main__":`). Compilation happens in two steps:

1. **Compile** — each `.cpp` file is turned into an object file independently. The compiler needs the headers to know what functions look like.
2. **Link** — object files and libraries (`portaudio`, `raylib`) are joined into one executable. "Undefined reference" errors come from this step: you declared something in a header but never provided its code, or forgot to list a library.

CMake (section 7) automates both steps.

---

## 4. Architecture

### 4.1 Two threads

When the app runs there are two separate threads of execution:

| Thread | Runs | Job | Rate |
|---|---|---|---|
| **Audio thread** | `audioCallback` (created and driven by PortAudio) | Reads the microphone block, runs the effects, writes the speaker block | Every 64 samples ≈ 700×/second |
| **UI thread** | `main()`'s `while` loop | Reads keyboard/mouse, turns encoders, presses footswitches, draws the pedal | 60×/second |

They communicate **only** through a set of global `std::atomic` variables (the "mailbox").

```
                         UI THREAD (main, 60 fps)
  keyboard / mouse ───►  ┌─────────────────────────────┐
                         │ handle P / F / B / Space,   │
                         │ footswitches, turn encoders │
                         └───────┬─────────────▲───────┘
                       .store()  │             │  .load()
                                 ▼             │
              ┌─────────────────────────┐  ┌───┴─────────────────────┐
              │ guiParam[11] (encoders) │  │ currentMicPeak          │
              │ guiActivePreset         │  │ scopeBuffer / scopeHead │
              │ guiSpectralFreeze,      │  │ (for the OLED)          │
              │ guiBypass               │  └───▲─────────────────────┘
              │                         │      │
              └───────┬─────────────────┘      │ .store()
                      │ .load()                │
                      ▼                        │
                         AUDIO THREAD (every 64 samples)
  microphone ──► PortAudio ──► audioCallback ──┴─► DemonicEngine::processBlock ──► PortAudio ──► speakers
```

### 4.2 What happens in one audio block

Every ~1.45 ms, PortAudio calls `audioCallback` (`main_desktop.cpp:45`), which:

1. Checks whether the preset changed; if so, calls `demonic.loadPreset(...)` (lines 54–59). This updates the preset index the engine reports and the preset fields that have no encoder (harmony); the other values arrive through the encoder atomics in the next step (section 5.4).
2. Copies every encoder value from the atomics into the engine via setters (lines 61–65: `simctl::apply` sends the twelve encoder values). `setBypass` goes first, so that Freeze is refused in the same block if bypass has just been switched on.
3. Measures the input peak for the level meter and calls `demonic.processBlock(in, out, 64)` (lines 67–72).
4. Copies the output into the oscilloscope ring buffer (lines 74–79) and publishes the mic peak (line 84).
5. Returns `paContinue` ("keep going").

### 4.3 Design decisions worth knowing

- **The engine knows nothing about PortAudio or raylib.** `DemonicEngine` takes in a float array and fills a float array. That is why it can be tested headlessly (section 9) and is the part meant to be reused on the Daisy Seed.
- **Everything is allocated up front.** No `new`, no `malloc`, no `std::vector` growth in the audio path — required for real-time safety and for microcontrollers.
- **Control values are copied in once per block**, not per sample. Cheap, but it means parameter changes are "steppy" (see section 10.10).
- **The engine is a single global object** (`DemonicEngine demonic(SAMPLE_RATE, engineMemory);`, `main_desktop.cpp:24`), with its long buffers in another global, `engineMemory` (line 23). Both threads can see it, but by convention only the audio thread touches its state. The UI thread only calls `getPreset()` and `getPresetCount()`, which read data that never changes after construction.
- **Memory footprint:** `DemonicEngine` is about **410 KB**, almost all of it the ReverbSc reverb (~396 KB) plus the pitch and chorus buffers (8 KB each). `DemonicEngineMemory` adds about **576 KB**: the 2 s echo and the 1 s freeze loop. They are separate so the Daisy Seed can keep the engine in fast SRAM and the long buffers in SDRAM (section 11).

---

## 5. Walkthrough: the desktop app (`desktop/main_desktop.cpp`)

The app is split the way the Phase Box simulator is: pure logic in headers with no graphics dependency (unit-tested by `tests/sim_controls_test.cpp`), and all raylib drawing in `main_desktop.cpp`.

| File | Contents |
|---|---|
| `include/sim_controls.hpp` | `simctl::Param` (one per encoder) and the `spec()` table: label, range, step per click, display format, bipolar or not, warning threshold, group (VOICE / MANGLE / SPACE). Helpers to map a value to its ring position and back (`norm`, `from_norm`), turn by clicks (`step`), format it (`format`), read a preset's values (`preset_values`), and send all twelve to the engine (`apply`). For the Daisy firmware: `ring_led_lit` (which LEDs of a hardware ring are lit) and `Quadrature` (encoder decoder). |
| `include/sim_layout.hpp` | `simlayout::Layout::make()`: where the enclosure, OLED, group panels, encoders, footswitches, LEDs and jacks are. Hit-testing and overlap helpers, `clicks_from_wheel`, and `KnobDrag` (turning a knob with the mouse: drag round the ring, or up/down from the centre). |
| `include/sim_font.hpp` | DejaVu Sans Mono, embedded so text looks the same on every machine. |

### 5.1 Globals (lines 23–34)

```cpp
DemonicEngineMemory engineMemory;                  // the engine's long buffers (~576 KB)
DemonicEngine demonic(SAMPLE_RATE, engineMemory);

std::atomic<bool>  guiBypass(false);        // true bypass: dry signal only, effects off
std::atomic<float> guiParam[PARAM_COUNT];   // one per encoder, indexed by simctl::Param
std::atomic<int>   guiActivePreset(0);
std::atomic<bool>  guiSpectralFreeze(false);
std::atomic<float> currentMicPeak(0.0f);

constexpr size_t SCOPE_SIZE = 128;
float scopeBuffer[SCOPE_SIZE] = {0};
std::atomic<size_t> scopeHead(0);
```

One atomic per encoder, in an array indexed by `simctl::Param` (`guiParam[simctl::P_DRIVE]` and so on), so the drawing and input code can loop over all twelve. `main()` sets them from preset 0 (pitch 0) before audio starts. The **scope buffer** is a 128-sample circular buffer the audio thread fills with output samples; the UI reads it to draw the waveform. `scopeHead` is how many samples have been written so far (it grows without bound; reads wrap it with `% SCOPE_SIZE`).

**`selectPreset(idx)`** (line 38) runs on the UI thread: it copies the preset's values into the encoder atomics (pitch is not part of a preset and keeps its value), then stores the new index. Values first, index second, so the callback never sees the new preset with the old values.

### 5.2 The audio callback (lines 45–86)

The signature is dictated by PortAudio; you cannot change it. The parts you care about are `inputBuffer`, `outputBuffer`, and `framesPerBuffer`. The first thing it does is cast the generic `void*` buffers to `float*` (since the stream was opened with `paFloat32`):

```cpp
const float* in = static_cast<const float*>(inputBuffer);
float* out = static_cast<float*>(outputBuffer);
```

The preset check uses a `static` local, `lastPreset`, which persists across calls so the callback can detect a *change*. Then every parameter is pushed into the engine, then the block is processed.

### 5.3 `main()` setup (lines 269–308)

`vocal_desktop --screenshot file.png` runs the app for 60 frames (about a second), saves the window as `file.png` in the current directory, and quits; `docs/images/simulator.png` is made this way (`cd docs/images && ../../build/vocal_desktop --screenshot simulator.png`). Otherwise there are no command-line options. Setup:

1. Pitch to 0 and `selectPreset(0)`, so the encoders hold preset 0's values before the callback first reads them.
2. `Pa_Initialize()` starts PortAudio.
3. `Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, SAMPLE_RATE, FRAMES_PER_BUFFER, audioCallback, nullptr)` — opens the default devices with **1 input channel, 1 output channel (mono), 32-bit float samples, 44.1 kHz, 64-frame blocks**, and registers `audioCallback`. (`&stream` passes the address of `stream` so the function can fill it in — an "output parameter".)
4. `Pa_StartStream(stream)` — from here on, the callback is being called.
5. `Layout::make()` computes the geometry; `InitWindow(970, 770, ...)` creates the window (high-DPI aware); the embedded font is loaded; `SetTargetFPS(60)` limits the loop to 60 frames/second.
6. `LoadRenderTexture(128, 64)` creates an off-screen 128×64 image — the OLED.

Error handling is by return code, not exceptions: functions return `paNoError` on success and `main` prints and exits otherwise.

### 5.4 The UI loop (lines 310–435)

Each frame:

1. **Keys:** `P` calls `selectPreset` with the next index; `F` toggles `guiSpectralFreeze` (ignored while bypassed); `B` or `Space` toggles `guiBypass` (and clears `guiSpectralFreeze` when switching bypass on). The footswitches call the same lambdas.
2. **Mouse:** a press on an encoder starts a `KnobDrag`; while the button is held, the degrees turned move a 0..1 ring position (`drag_pos`) by `degrees / 270`, and `simctl::from_norm` turns that into the value stored in `guiParam`. Keeping `drag_pos` separately means PITCH's snap-to-zero does not trap the knob at 0. Scrolling over an encoder turns it by whole clicks (`simctl::step`, which lands on multiples of the step and stops at the ends). A press on a footswitch acts immediately.
3. **OLED** (`render_oled`, line 251), drawn at its real 128×64 resolution into the off-screen texture: border, a header (preset number and name, or for 1.5 s after an encoder moves, that encoder's name and value), `BYPASS` or `FREEZE` under the header when active, the waveform (127 line segments between consecutive `scopeBuffer` samples, clamped to the display; grey while bypassed, red while frozen), and the mic-level bar.
4. **The pedal:** enclosure with screws and title, jacks and USB port (decorative), the OLED texture scaled 3× with its bezel, the three group panels, each encoder (`draw_encoder`: LED ring with the lit arc from `simctl::origin_degrees` to `simctl::degrees`, red past `warn_above`; a knurled cap that turns with the value; label and value under it), then the footswitch LEDs (preset flash, freeze red, bypass green while effects are on) and footswitches.

This style is called **immediate-mode UI**: there are no button or knob objects. Every frame the code asks "is the mouse over this circle and pressed?", acts on the answer, and draws it. Only the geometry (`Layout`) and value logic (`simctl`) live outside the loop, which is what lets the tests check them without a window.

### 5.5 Shutdown (lines 437–444)

Unload the texture and fonts, close the window, stop and close the audio stream, terminate PortAudio.

---

## 6. Walkthrough: the DSP engine

### 6.1 The header (`include/demonic_engine.hpp`)

**`VocalPreset`** — a plain struct of thirteen numbers plus a name.

**`DemonicEngineMemory`** — the engine's long buffers, `echo` (2 s at 48 kHz) and `freeze` (1 s + 20 ms). The caller creates it and passes it to the constructor, so it can decide where it lives: a plain global on the desktop, the SDRAM on the Daisy Seed (section 11). It is about 576 KB, so never a local variable.

**`DemonicEngine`** — the public interface:

| Method | Purpose |
|---|---|
| `DemonicEngine(float sampleRate, DemonicEngineMemory& memory)` | Create the engine; convert lengths and coefficients for the sample rate; zero buffers; define presets |
| `processBlock(in, out, n)` | The workhorse: process `n` samples |
| `setDrive`, `setSubOctaveBlend`, `setRobotBlend`, `setRobotFreq`, `setWavefold`, `setDecimation`, `setDelayFeedback`, `setDelayMix`, `setReverbMix`, `setMix` | Plain setters: store a value in a member |
| `setDelayTime(float seconds)` | Echo time, 0.02–2 s (`kMinDelayTime`, `kMaxDelayTime`); the echo glides to it |
| `setHarmony(float cents, float blend)` | Harmony voice, `cents` from the pitch-shifted voice, mixed at `blend` (0 = off). Section 6.7 |
| `setSpectralFreeze(bool)` / `isSpectralFrozen()` | Freeze control (refused while bypassed) |
| `setBypass(bool)` / `isBypassed()` | True bypass: exact pass-through with all effect memory cleared. Section 6.9 |
| `setPitchShift(float cents)` | Pitch shift, −1200…+1200 cents (100 cents = 1 semitone); 0 bypasses the shifter. `setFormantShift` is kept as a deprecated alias that forwards to it |
| `loadPreset(int)`, `getCurrentPresetName()`, `getPresetCount()` | Preset handling |

**Private state** falls into three groups:

- **Parameters** (`driveDb`, `subBlend`, `robotBlend`, …) — set from outside. These survive a bypass untouched.
- **Rate-dependent constants** (`pitchWindow`, `chorusDelay`, `echoLen`, `freezeLoopLen`, …, and the one-pole coefficients `envCoef`, `toneCoef`, `dcPole`, …) — set once in the constructor from values tuned at 44.1 kHz (section 6.4).
- **Per-stage memory** (`subDetLP1`, `subDetLP2`, `subDetEnv`, `subArmed`, `subToggle`, `subFilterState`, `lowpassState`, `ringModPhase`, `heldSample`, `decimationPhase`, `dcInPrev`, `dcOutPrev`, `inputEnvelope`, `pitchPhase`, `pitchBlend`, `harmonyPhase`, `harmonyLevel`, `echoDelaySamples`, the freeze positions) — what each effect must remember from one sample to the next.
- **Buffers** — sized for 48 kHz (`kMaxSampleRate`); at 44.1 kHz the ring lengths are shorter, so the times stay the same:

| Buffer | Size | Duration | Lives in | Used by |
|---|---|---|---|---|
| `pitchBuffer` | 2,048 | 43 ms at 48 kHz | engine | Pitch shifter and harmony (the 40 ms sweep) |
| `delayLine` | 2,048 | 43 ms at 48 kHz | engine | Chorus |
| `reverb` (ReverbSc) | ~396 KB | — | engine | Reverb: 8 delay lines, read every sample |
| `mem.echo` | 96,064 | 2 s | `DemonicEngineMemory` | Echo |
| `mem.freeze` | 48,960 | 1 s + 20 ms | `DemonicEngineMemory` | Freeze: records the echo input, loops it when frozen |

### 6.2 Why effects need "memory"

An effect like a filter produces output based on *previous* samples as well as the current one. Because `processBlock` is called over and over, each stage keeps its own history in member variables (the "per-stage memory" above). That is why `DemonicEngine` is a class, not a pure function — compare a Python generator that holds state between `next()` calls.

### 6.3 Circular buffers (the trick behind delays)

A delay must remember the last N samples and play one back later. A circular buffer does this with a fixed array and a moving "write head":

```python
# Python equivalent of the C++ delay pattern
buf = [0.0] * 22050
head = 0

def step(x):
    global head
    delayed = buf[(head - D) % 22050]   # read the sample written D samples ago
    buf[head] = x                       # write the new sample at the head
    head = (head + 1) % 22050           # advance, wrapping at the end
    return delayed
```

When the head advances, it overwrites the oldest sample. Reading `D` slots *behind* the head gives a delay of `D` samples. (Reading `K` slots *ahead* of it, `(head + K) % size`, gives a delay of `size − K`, which is how the old echo and reverb were written and why their comments were wrong; section 10.5.) In C++, `%` on a negative number stays negative, so the engine adds `size` first, or uses `readDelayLinear()`, which also interpolates between samples for a fractional `D`.

### 6.4 The constructor and presets (`demonic_engine.cpp:49–94`)

Sets all members, then converts every length and coefficient for the sample rate. They were tuned at 44.1 kHz, so the code writes them that way: a chorus delay of `441.0f * sampleRate / kRefRate` samples (10 ms), and one-pole coefficients through `rateCoef(c, sampleRate)` = `1 − (1 − c)^(44100 / sampleRate)`, which keeps each filter's time constant. At 44.1 kHz nothing changes; at 48 kHz (the Daisy Seed) the effects sound the same. `test_same_sound_at_48k` checks it. Then it defines three presets using brace-initialisation (`presets[0] = {"DEMONIC GROWL", 11.0f, ...};` — values in the same order as the struct fields), calls `loadPreset(0)`, `resetState()` (which also initialises the ReverbSc) and clears the long buffers.

`loadPreset(i)` (lines 163–177) copies the thirteen fields from `presets[i % 3]` into the live parameter members. The `% NUM_PRESETS` makes cycling wrap around.

### 6.5 `processBlock` stage by stage (`demonic_engine.cpp:200–397`)

Before the loop, two values are computed once per block:

```cpp
float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);        // dB → linear
int decimationStep   = std::max(1, static_cast<int>(decimationFactor));
float decimationAdvance = kRefRate / sampleRate;                  // 44.1 kHz samples per sample
```

Then a `for` loop handles each sample `input[i]` through the gate, the pitch shifter and harmony, and ten numbered stages, in this order:

| # | Lines | Stage | What the code does |
|---|---|---|---|
| gate | 226–227 | **Noise gate** | `activeSignal = abs(dry) > 0.02 ? dry : 0`. Anything quieter than 0.02 (−34 dBFS) is zeroed. Also updates `inputEnvelope`, a smoothed loudness meter (a one-pole filter on `abs(activeSignal)`). |
| 0b | 229–241 | **Pitch shifter** | One read tap sweeps back through `pitchBuffer`, reading the audio faster or slower than it was written (that is what changes the pitch). A second tap covers for it while the first jumps back at the end of each sweep (both in `pitchTap()`). Faded in and out over about 10 ms so switching it on or off does not click. Details in section 6.7. |
| 0c | 242–250 | **Harmony** | A second `pitchTap()` sweep over the same buffer, `harmonyCents` above or below the shifted voice, added at `harmonyLevel` (faded like the shifter). Skipped while off. |
| 1 | 252–267 | **Chorus doubler** | Writes the gated signal into `delayLine`. A 0.5 Hz sine LFO modulates a read position around 10 ms ± 1.5 ms (`chorusDelay`, `chorusSwing`). It reads between two neighbouring samples with **linear interpolation** (`a*(1-frac) + b*frac`) because the position is fractional. Adds 30% of that copy to the voice. |
| 2 | 269–298 | **Sub-octave** | A Schmitt-trigger octave divider: watches a low-passed copy of the voice, flips a square wave on each rising edge (once per cycle, so the square runs at half the frequency), smooths it (one-pole at about 290 Hz) and scales it by the voice's loudness (`inputEnvelope`) and `subBlend`. Details in section 6.8. |
| 3 | 300–308 | **Ring modulation** | An oscillator at `robotFreq + 40 × inputEnvelope` Hz (`ringModPhase` is the oscillator's angle in radians, wrapped at 2π). `wet` is crossfaded between the clean signal and `wet × sin(phase) × 1.5` by `robotBlend`. |
| 4 | 310–320 | **Drive and wavefold** | `wet = tanh(wet × gainMultiplier)` is the saturation. If `wavefoldDrive > 0.1`, a triangle fold is applied and mixed in at weight `0.4 × wavefoldDrive`. |
| 5 | 322–334 | **Decimation** | Takes a new sample to "hold" (`heldSample`) every N samples *at 44.1 kHz*, and repeats it in between. `decimationPhase` counts in 44.1 kHz samples (`kRefRate / sampleRate` per sample, exactly 1 at 44.1 kHz), so at 48 kHz the hold lasts about 3.27 samples for x3 and the crunch sounds the same. Then `round(wet × 32) / 32` quantises to about 6 bits. |
| 6 | 336–338 | **Tone filter** | One-pole low-pass, `a = 0.35` at 44.1 kHz (`toneCoef`) → roughly 3 kHz cutoff. |
| 7 | 340–356 | **Spectral freeze** | If active, plays the 1 s loop in `mem.freeze` (crossfaded at the seam) and mixes it 70/30 with the live signal. Section 6.6. |
| 8 | 358–373 | **Feedback delay** | Glides `echoDelaySamples` towards the ECHO TIME setting, reads `mem.echo` that far behind the head (interpolated), writes `tanh(wet + delayed × delayFeedback)` back in (and into the freeze ring unless frozen), and crossfades the output by `delayMix`. |
| 9 | 375–380 | **"Catacomb" reverb** | DaisySP's `ReverbSc` (8 modulated, damped delay lines; feedback 0.85, damping 5 kHz), fed `wet` on both inputs; its left output is crossfaded in by `reverbMix`. Skipped if it could not initialise (above about 48 kHz). |
| 10 | 382–386 | **DC blocker** | `y = x − x_prev + 0.995 × y_prev` (`dcPole` at other rates). A classic high-pass at a few Hz. |
| mix | 388–395 | **Wet/dry mix, then bypass crossfade** | `processed = dry × (1 − mix) + wet × mix`. Note `dry` is the *ungated* input. Then `output = processed × (1 − bypassBlend) + dry × bypassBlend`, where `bypassBlend` ramps between 0 and 1 over 5 ms (section 6.9). |

#### The one-pole filter, in Python

Used for the sub filter, tone filter, and envelope follower, so it is worth understanding once:

```python
def one_pole(state, x, a):
    return state + a * (x - state)     # state "chases" x; smaller a = slower = more smoothing
# cutoff (Hz) ≈ -sample_rate * ln(1 - a) / (2π)
#   a = 0.35 → ≈ 3.0 kHz  (tone filter)
#   a = 0.04 → ≈ 290 Hz   (sub filter)
```

In C++ the state is a member variable and the assignment is `lowpassState = lowpassState + toneCoef * (wet - lowpassState);` — identical, with `toneCoef` = 0.35 at 44.1 kHz. The cutoff formula shows why `a` depends on the sample rate: for the same cutoff at 48 kHz, `a` must be slightly smaller, which is what `rateCoef()` computes.

#### Mixing, crossfades and "wet" vs "dry"

*Dry* = the original. *Wet* = the processed signal at that point in the chain. The pattern `wet = wet*(1-m) + effect*m` appears throughout; `m = 0` leaves the signal alone, `m = 1` replaces it with the effect.

### 6.6 Spectral freeze details

`mem.freeze` is a ring of 1 s + 20 ms that stage 8 keeps writing with the echo input, so it always holds the most recent second. `setSpectralFreeze(true)` (lines 148–157) — only on the *off → on* transition — copies nothing: it notes the ring's oldest sample (`freezeStart`), and stage 8 stops writing. Pressing Freeze therefore costs no time, which matters when the ring is in slow SDRAM.

Playback loops the newest second. A plain loop would jump from the end of the recording back to its start, a click once a second. Instead the last 20 ms of the loop fade into the 20 ms recorded just *before* the loop's start, so when the loop wraps, the next sample is the one that originally followed: no jump. `test_freeze_loop` checks the 1 s period and that the step at the seam is no larger than elsewhere.

### 6.7 The pitch shifter

**The idea.** Imagine a tape playing back while you read it from a spot that slowly moves away from (or toward) the write position. If the read position moves *away* (the delay is growing), you read the recording more slowly than it was made, so the pitch drops. If it moves *toward* the write position, you read it faster and the pitch rises. A circular buffer plus a read position that sweeps is all it takes:

```python
# Python sketch of the engine's shifter (one tap; the real code adds a covering tap, see below)
ratio = 2 ** (cents / 1200)                  # 0.5 = octave down, 2.0 = octave up
phase += (1 - ratio) / WINDOW                # how far the read position slides each sample
phase -= floor(phase)                        # wrap into [0, 1): that is the "jump back"
delay = 2 + phase * WINDOW                   # in samples; WINDOW is 40 ms (1764 at 44.1 kHz)
out = read_with_interpolation(buffer, write_head - delay)
```

**The catch.** The read position cannot slide away forever (the buffer is finite), so every `WINDOW / abs(1 - ratio)` samples it must jump back, and that jump is a discontinuity in the audio. The code hides the jump with a **second tap, half a sweep away**, and a short crossfade (the first 10 % and last 10 % of each sweep, about 4 ms) between the two taps.

**Why not just blend the two taps 50/50 all the time?** That was the first implementation here, and the tests caught it: with a steady tone the two taps are a fixed time apart, so they cancel the main frequency and leave two beating side tones. A 220 Hz sine pitched down 5 semitones came out as a pair of peaks at 157 and 170 Hz with essentially nothing at the expected 164.8 Hz. Letting one tap do almost all of the work and using the second only to cover the jump keeps the pitch clean.

**Properties to be aware of**

| Property | Detail |
|---|---|
| Latency | The delay sweeps between about 0 and 40 ms, so the shifted voice is on average ~20 ms behind your dry voice. The shifter is fully bypassed (no delay) when the pitch is 0. |
| Accuracy | On a voice-like test signal every shift tested was within about 6 cents. On a pure sine it is within about 20 cents up to ±1 octave, 40 at the extreme. Pure tones are the worst case because they lock onto the sweep rate. |
| Character | A faint flutter or tick at the sweep rate, which is `abs(1 − ratio) / 0.04 s` Hz: about 6 Hz at −5 semitones and 25 Hz at ±1 octave. Larger shifts sound rougher. |
| Formants | The whole spectrum moves, so it does **not** preserve formants. Big shifts sound like a different-sized voice, not the same voice at another pitch. A formant-preserving shifter would need a spectral or PSOLA-style design. |
| Cost | Two interpolated buffer reads per sample, one `cos` only during the crossfades, one `pow` per block. See section 11. |
| Switching | `pitchBlend` ramps between 0 and 1 (about 10 ms) when the pitch moves to or from 0, so the bypass boundary does not click. The whole stage is skipped, costing nothing, while bypassed. |

The sweep length (`kPitchWindowSec`, converted to `pitchWindow` samples) and the crossfade fraction (`kPitchFade`) are named constants at the top of `demonic_engine.cpp`. Longer windows handle lower voices better but add latency; a smaller crossfade gives a cleaner pitch with harder splices.

**Harmony.** `pitchTap(phase)` holds the two-tap read and crossfade, so a second voice costs one more call: stage 0c keeps its own `harmonyPhase` and sweeps it at `2^((pitchCents + harmonyCents) / 1200)`. The harmony is therefore relative to the shifted voice (a fifth above stays a fifth above whatever PITCH does). It has the same flutter and formant behaviour, and is faded in and out with the shifter's coefficient. On a pure sine, the harmony's fifth measures about 70 % as strong as the main shifter's own +700-cent output (`test_harmony`).

### 6.8 The sub-octave detector

An **octave divider** makes a note one octave lower by flipping a square wave once per cycle of the input. The input completes *f* cycles per second; flipping the square once per cycle means it needs two input cycles to complete one on-off period, so it runs at *f*/2. The hard part is deciding reliably when "one cycle" has happened in a real, noisy, harmonic voice.

```python
# Python sketch of the engine's detector
lp1 += 0.08 * (voice - lp1)                     # two cascaded one-pole low-pass filters
lp2 += 0.08 * (lp1 - lp2)                       # ignore harmonics and hiss; keep the fundamental
env += (0.02 if abs(lp2) > env else 0.0005) * (abs(lp2) - env)   # fast attack, slow release
if env > 0.006:                                 # only listen when there is a signal
    h = max(0.004, 0.25 * env)                  # hysteresis scales with level
    if armed and lp2 > h:                       # rising edge: one per cycle
        armed = False
        toggle = not toggle                     # square wave flips
    elif not armed and lp2 < -h:                # must swing clearly negative before it can fire again
        armed = True
```

The Schmitt-trigger pattern (an "armed" flag and a threshold on each side) means small wiggles around zero, from noise or leftover harmonics, cannot cause extra flips. The square wave is then smoothed by a one-pole low-pass at about 290 Hz and scaled by the voice's smoothed loudness.

**What was wrong before.** The original detector (a) toggled on both rising *and* falling zero crossings, which gives a square at the *same* frequency as the input rather than half; and (b) required the signal to exceed the noise-gate threshold at the very sample after the crossing, which the gate had just forced to zero. In tests the toggle rate was erratic: a 110 Hz sine produced about 3 flips per second at every level, and energy at the sub frequency was absent at 150 Hz. After the rewrite, energy at half the input frequency is clearly present for every frequency and level tested (110–440 Hz, amplitudes 0.1–0.9) and for a noisy harmonic voice-like signal.

**Remaining limits.** Like all simple dividers it follows one dominant periodic component. Whispered, breathy, or very noisy sounds, and chords or several voices at once, can make it flutter or drop out. It is silent when the input is.

### 6.9 True bypass

`setBypass(true)` makes the engine hand your untouched voice straight back. Three details matter:

1. **It is exact.** Once the 5 ms crossfade has finished, `processBlock` skips the whole effect chain and runs `output[i] = input[i]`, so the output is bit-for-bit the input however extreme the settings (the test checks this with every effect pushed to its limit). The check sits at the top of `processBlock`:

   ```cpp
   if (bypassTarget && bypassBlend >= 1.0f) {
       if (!bypassStateCleared) { resetState(); bypassStateCleared = true; }
       clearLongBuffers(numFrames * kClearPerFrame);   // a slice per block (point 3)
       for (size_t i = 0; i < numFrames; ++i) output[i] = input[i];
       return;                                  // no DSP at all: also saves CPU
   }
   ```

2. **It does not click.** Switching mid-note would otherwise jump instantly between two different waveforms. Instead, `bypassBlend` slides linearly between 0 (effects) and 1 (dry) over 5 ms, and the final line of the sample loop blends the two. A linear ramp (not an exponential one) matters: it reaches exactly 1.0, so the exact pass-through above can take over cleanly.

3. **It wipes the effects' memory.** `resetState()` zeroes the short buffers (chorus, pitch) and every filter and detector variable, and re-initialises the ReverbSc (which clears it), but not the *parameters*, so your encoder settings are all still there when you switch back. The long echo and freeze buffers (about 576 KB, in slow SDRAM on the Daisy Seed) would take several blocks' worth of time to clear at once, so `clearLongBuffers()` clears 64 samples per frame of each bypassed block, about 50 ms in all; if bypass is released before it finishes, the rest is cleared at once. Without this wipe, switching bypass off would replay up to two seconds of audio from before the bypass. A useful side effect: **bypass is a panic button**. A runaway echo loop (Delay Feedback above 1.0, issue 10.2) is flushed by a single bypass cycle; the test confirms a loop that had reached 74× full scale is silent afterwards. It also clears any NaN or infinity stuck in the loop.

   `resetState()` runs once per bypass period (`bypassStateCleared`), and the slices stop once the buffers are clear, so it costs nothing while bypassed after that.

**Freeze and bypass.** A frozen loop lives in a buffer that the wipe clears, so Freeze is switched off by `setBypass(true)`, and `setSpectralFreeze(true)` is ignored while bypassed. The desktop app mirrors this: its bypass handler also clears `guiSpectralFreeze`, and the `F` key and freeze button do nothing while bypassed. Without that mirroring, the callback would re-assert the old freeze flag every block and "freeze" an empty buffer the moment bypass ended.

**Thread safety.** As with every other setter, `setBypass` is called from the audio callback after the UI thread stores the flag in `guiBypass`; nothing else touches `bypassTarget` or `bypassBlend`.

**What it is not.** This is *software* bypass: the signal still goes through the computer's input and output path (about 1.5 ms of buffering plus the audio device's own latency). For a truly zero-latency clean voice, use the audio interface's own direct-monitor switch.

---

## 7. The build system (`CMakeLists.txt`)

CMake describes your project as a set of **targets** (things to build) and the **dependencies** between them, much as a Python project has packages and imports. The file defines four targets, plus the options that control them:

| Target | What it is | Depends on |
|---|---|---|
| `daisysp_reverbsc` | A static library of one file, DaisySP-LGPL's `Effects/reverbsc.cpp` (LGPL-2.1). | DaisySP, fetched |
| `demonic_engine` | A static library built from `src/demonic_engine.cpp`. The DSP code, with no audio or graphics. | `daisysp_reverbsc` |
| `engine_dsp_test`, `sim_controls_test`, `hw_pinout_test` | The test programs in `tests/` (the `foreach` in `CMakeLists.txt` lists them). | `demonic_engine` |
| `vocal_desktop` | The desktop app. | `demonic_engine`, PortAudio, raylib (plus Apple system frameworks on macOS) |

Splitting the engine into its own library is what lets the tests and the desktop app share one copy of the DSP code; the Daisy benchmark (`daisy/`) compiles the same source files.

### Options

| Option | Default | Effect |
|---|---|---|
| `BUILD_DESKTOP` | ON | Build the desktop app. Turn OFF to build only the engine and tests, with no PortAudio or raylib needed. |
| `BUILD_TESTS` | ON | Build the three test programs and register them with `ctest`. |
| `USE_SYSTEM_RAYLIB` | OFF | Use an installed raylib 5.0+ (`find_package`) instead of downloading raylib 5.0 at configure time. |

Release (optimised) is the default build type, since real-time DSP needs it. Pass `-DCMAKE_BUILD_TYPE=Debug` to override.

### How dependencies are found

- **PortAudio**, in order: (1) `pkg-config` (`portaudio-2.0`); (2) a manual search that includes both `/opt/homebrew` (Homebrew on Apple Silicon) and `/usr/local` (Homebrew on Intel Macs and most Linux installs); (3) the copy in `./portaudio`, built from source, if it exists. If none of these succeeds, configuration stops with a message saying how to install it.
- **raylib:** downloaded and built from source by CMake's `FetchContent` (needs internet on the first configure), unless `USE_SYSTEM_RAYLIB` is ON.
- **DaisySP:** cloned by `FetchContent` at a pinned commit, with its `DaisySP-LGPL` submodule (`SOURCE_SUBDIR no-cmake` stops CMake from building all of DaisySP). Offline, point it at a local clone: `-DFETCHCONTENT_SOURCE_DIR_DAISYSP=/path/to/DaisySP`; the helper scripts do this for `/Users/ww/src/DaisySP` (or `DAISYSP_DIR`) when it exists. This is why the minimum CMake is 3.18.
- **macOS frameworks** (`CoreAudio`, `Cocoa`, `OpenGL`, and so on) are linked only `if(APPLE)`, so the file no longer breaks other platforms at that step.

### Continuous integration

Every pull request runs `.github/workflows/ci.yml` on Ubuntu: `host-tests` runs `build_tests.sh`, and `daisy` installs the Arm toolchain (apt), builds libDaisy at the commit pinned in the workflow (cached) and DaisySP at the `GIT_TAG` in `CMakeLists.txt`, runs `build_daisy.sh`, and fails if any `daisy/build/*.bin` is larger than the 128 KB internal flash (`.github/scripts/check_flash.sh`). To move libDaisy, change `LIBDAISY_COMMIT` there.

### Version numbers and releases

Every merge to `main` is a release. `.github/workflows/release.yml` (logic in `.github/scripts/release.sh`) tags the merge commit `vX.Y.Z` and publishes a [GitHub Release](https://github.com/wsjk/-vocal-fx-processor/releases) whose notes list the merged PRs by title. Those notes are the project's changelog. The bump is a patch (0.1.0 → 0.1.1) unless the PR has a `minor` or `major` label; `.github/release.yml` groups the notes by the same labels. The first release is v0.1.0.

`cmake/version.cmake` sets `VFX_VERSION` from `git describe` when CMake configures: `0.3.0` on a release commit, `0.3.0-2-gabc1234` two commits later, with `-dirty` for uncommitted changes, and `0.0.0-g<sha>` before the first tag. `daisy/Makefile` applies the same rules. The desktop window title and the Daisy benchmark's banner show it. Re-run `cmake` to pick up a new commit or tag.

### Common commands

```bash
cmake -S . -B build                          # configure (downloads raylib the first time)
cmake --build build                          # compile and link everything
./build/vocal_desktop                        # run the app
ctest --test-dir build --output-on-failure   # run the tests

cmake -S . -B build -DBUILD_DESKTOP=OFF      # engine + tests only (no PortAudio/raylib needed)
cmake -S . -B build -DUSE_SYSTEM_RAYLIB=ON   # use Homebrew's raylib
```

With CMake 4 or newer, raylib 5.0's own `CMakeLists.txt` may refuse to configure. The project sets `CMAKE_POLICY_VERSION_MINIMUM=3.5` for you, but if it still complains, pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` on the command line (the helper scripts in `.agents/skills/vocal-fx-processor/scripts/` do this).

> **Verification status.** The full CMake build (engine, `engine_dsp_test`, `sim_controls_test`, and `vocal_desktop` with PortAudio and the downloaded raylib) has been built on macOS, and both tests pass there. The engine and tests also build and pass on Linux with g++. The desktop app is untested on Linux and Windows.

The Daisy Seed build (`daisy/Makefile`, needs libDaisy and a DaisySP clone) is a **separate** project, because it needs libDaisy and an Arm cross-compiler; see section 11.

---

## 8. How to extend it

### 8.1 Add a new parameter (worked example: tone-filter brightness)

Right now the tone filter's coefficient is fixed: `toneCoef`, 0.35 at 44.1 kHz, used by the `lowpassState = ...` line in stage 6 (`demonic_engine.cpp:337`). Here is how to make it adjustable.

**1. Header — add the state and setter** (`include/demonic_engine.hpp`):

```cpp
// in the public section:
void setTone(float coefficient);   // 0.05 (dark) … 0.9 (bright)

// in the private section:
float toneCoeff;
```

**2. Engine — initialise, implement, use** (`src/demonic_engine.cpp`):

```cpp
// constructor initializer list (add:)   toneCoeff(0.35f),
// the value is given at 44.1 kHz, so convert it for the actual rate, as the constructor does for toneCoef:
void DemonicEngine::setTone(float c) { toneCoeff = rateCoef(c, sampleRate); }

// in processBlock, replace the stage 6 line:
lowpassState = lowpassState + toneCoeff * (wet - lowpassState);
```

**3. Simulator — add an encoder** (`include/sim_controls.hpp`, `desktop/main_desktop.cpp`):

```cpp
// sim_controls.hpp: add P_TONE to the Param enum (inside the group it belongs to), and a matching row to spec():
{"TONE", "%.2f", 1.0f, 0.05f, 0.9f, 0.05f, false, INFINITY, G_VOICE},

// main_desktop.cpp, in audioCallback with the other setters:
demonic.setTone(guiParam[P_TONE].load());

// main(): give it a starting value next to the pitch one (or add it to presets, step 4):
guiParam[simctl::P_TONE].store(0.35f);
```

The encoder is drawn, hit-tested and turned automatically: `Layout::make()` spaces every group's encoders evenly across its panel. The VOICE panel has room for a fourth; MANGLE has four and SPACE five, both full. On the hardware every Seed GPIO is taken, so a new encoder goes on the MCP23017 expander: add it to `hwpins::ENCODERS` with `expander = true` (`hw_pinout_test` checks it). Run `ctest`: `sim_controls_test` checks that nothing overlaps.

**4. (Optional) add it to presets** — add a `float tone;` field to `VocalPreset`, extend the three brace-initialiser lines (the values must stay in the declared order), copy it in `loadPreset`, and set `out[P_TONE]` in `simctl::preset_values` so loading a preset turns the encoder.

### 8.2 Add a preset

1. In the header, change `static const int NUM_PRESETS = 3;` to `4`. (The array `VocalPreset presets[NUM_PRESETS]` is sized from it.)
2. In the constructor, add `presets[3] = {"NEW NAME", ...thirteen numbers in field order...};`.

### 8.3 Add a new effect stage

1. Decide where it sits in the chain (order matters: effects before `tanh` get distorted; effects after it do not).
2. Add any state it needs (a phase, a filter memory, a buffer) as private members and initialise them in the constructor, zeroing any arrays; zero them again in `resetState()` so bypass clears them. Give any length or filter coefficient at 44.1 kHz and convert it in the constructor (section 6.4). A long buffer (more than a few KB, read once per sample) belongs in `DemonicEngineMemory`, cleared by `clearLongBuffers()`.
3. Insert the code inside the `for` loop in `processBlock`, working on the `wet` variable.
4. Expose its controls with setters and encoders as in 8.1.
5. If it needs a buffer, remember: fixed size, wrap indexes with `%`, and watch the Daisy Seed's memory budget (section 11).

**Real-time rules for anything you add:** no `new`/`delete`/`malloc`, no `std::vector` growth, no printing, no mutexes, no file access inside `processBlock` or the callback.

---

## 9. Testing without a microphone

Because `DemonicEngine` has no dependency on audio hardware, tests can drive it from a plain program: generate a signal, run it through `processBlock`, and analyse what comes out. Two test programs are built and run: `engine_dsp_test.cpp` (the sound) and `sim_controls_test.cpp` (the simulator's layout and encoder logic).

### Running it

With CMake (works for the engine alone, no PortAudio or raylib needed):

```bash
cmake -S . -B build-tests -DBUILD_DESKTOP=OFF
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

(`.agents/skills/vocal-fx-processor/scripts/build_tests.sh` runs these three steps, pointing CMake at a local DaisySP clone if there is one.) The engine needs ReverbSc, so build the tests with CMake rather than one `g++` line; after the first configure, `cmake --build build-tests && ./build-tests/engine_dsp_test` is quick.

Each test creates its engine with `std::make_unique<TestEngine>(fs)` (`tests/test_engine.hpp`): an engine plus its `DemonicEngineMemory`, about 1 MB, on the heap rather than the stack.

It prints a `[PASS]`/`[FAIL]` line per check, ends with `ALL TESTS PASSED (0 failures)`, and exits non-zero on any failure (which is how `ctest` knows).

### `engine_smoke_test.cpp`: out of date, not built

This file predates the current engine. It calls `resetSubToggleCount()` and `getSubToggleCount()`, which `DemonicEngine` no longer has, so it **does not compile**, and `CMakeLists.txt` does not build it. Everything it covered (initialisation, preset names, a sine through `processBlock`, the freeze flag, sub-octave activity) is covered more thoroughly by `engine_dsp_test.cpp`. Either delete it, or rewrite its sub-octave check to measure output energy the way `engine_dsp_test.cpp` does and add it to the `foreach` list in `CMakeLists.txt`.

### `engine_dsp_test.cpp`: does it sound right?

Measures **frequencies in the output**, plus bypass behaviour:

| Test | What it checks |
|---|---|
| Same sound at 48 kHz | At 44.1 and 48 kHz: the echo arrives after 200 ms, the tone filter passes 3 kHz at the same level (±0.5 dB), and −500 cents lands on the right note |
| Decimation rate | DECIMATE x3 on a 1 kHz sine puts its sample-and-hold image at 13.7 kHz (44100/3 − 1000) at both 44.1 and 48 kHz, not at 15 kHz |
| Echo time | A click comes back after the ECHO TIME setting (50 ms, 0.5, 1.5 and 2 s), within 1 ms |
| Freeze loop | After a gliding tone, the frozen output repeats every 1.000 s, and the step at the loop seam is no larger than elsewhere |
| Harmony | Off: no fifth. On (+700 cents): a fifth over 20× stronger and at least half as strong as the main shifter's; with PITCH −500 it moves to +200 cents |
| Reverb | A short burst leaves a tail (0.3–0.8 s) that decays (5–6 s is under a tenth of it) |
| Bypass, short | A runaway echo with bypass held for only 15 ms (before the long buffers finish clearing) is still silent afterwards |
| Pitch shifter, pure sine | A 220 Hz sine shifted by −1200…+1200 cents lands within 60 cents of 220 × 2^(cents/1200) |
| Pitch shifter, voice-like | A 150 Hz harmonic signal with vibrato and noise: the shifted fundamental stays within a factor of two of the strongest spectral component |
| Pitch shifter, switching | Toggling it on and off mid-note stays finite, bounded, and free of large sample-to-sample jumps |
| Sub-octave, sines | For 110–440 Hz at amplitudes 0.1–0.9: energy at half the input frequency is at least 8× larger with the sub on than off (in practice thousands of times) |
| Sub-octave, voice-like | The same on a noisy harmonic signal |
| Sub-octave, silence | No output for silent input |
| Bypass, exact | With every effect at an extreme, output after the crossfade is **bit-for-bit identical** to the input; a sanity check confirms the effects were really altering the sound beforehand |
| Bypass, no click | The largest sample-to-sample step while engaging or releasing is no bigger than the chain's normal steps |
| Bypass, tails | Loud sound, then bypass on and off: nothing comes back afterwards, while a control run without bypass still shows audible echoes |
| Bypass, recovery | After bypass is released the effects are audible again and the output is finite |
| Bypass, panic | A feedback loop that had run away (peak ~74× full scale) is silent after one bypass cycle |

**How it measures frequency.** `magnitude_at(signal, f)` is a Goertzel filter (a single bin of a Fourier transform) with a Hann window: it answers "how much of frequency *f* is in this signal?". `dominant_freq` scans a range in 1 Hz steps and refines the peak by parabolic interpolation. These are written out in plain C++ in the test file so you can read them.

**A trap when testing switches.** Blocks start at multiples of 64, so a test that flips a switch with `if (n == 44100)` will silently never fire (44,100 is not a multiple of 64) and the test will pass without testing anything. Use `block_has(n, t)`, which is true for the block that contains sample *t*. The bypass tests originally had exactly this bug, and it was caught only because one of them failed.

### `sim_controls_test.cpp`: does the pedal hold together?

Checks the simulator without opening a window: the enclosure fits the window; the OLED is 128×64 at 3×; panels, encoders (with their labels), footswitches, LEDs and jacks sit where they should without overlapping; a click lands on exactly one control; every encoder's value maps to its ring and back, stops at its ends and steps as documented (one semitone for PITCH, which also snaps to 0 near the centre); values format as shown on screen; every preset value fits its encoder's range and loading a preset keeps the pitch; and `KnobDrag` turns the right way.

### `hw_pinout_test.cpp`: does the planned wiring work?

Checks `include/hw_pinout.hpp`, the planned pin map for the Daisy Seed (see [HARDWARE_PLAN.md](HARDWARE_PLAN.md)): every pin is D0–D30; none is used twice or takes the USB pins kept free (D0, D29, D30); SCL/SDA are I²C1 (D11, D12); the LED chain is on SPI1 MOSI (D10); there is one encoder per `simctl` parameter; and the MCP23017 expander's pins are GPA0–GPB7, unique, with ECHO TIME on it at a valid address that is not the OLED's.

**Writing your own.** Use the helpers: `isolate(engine)` turns every optional effect off so only the one under test is audible; `render(engine, generator, seconds, keepSeconds)` feeds a signal in 64-sample blocks (the desktop app's block size) at 44.1 kHz and returns the last part of the output, and `render_at(engine, fs, generator, seconds)` does the same at any rate and returns all of it; then assert on `magnitude_at` or `dominant_freq`. Turning ECHO TIME glides, so let a new echo time settle (about a second) before measuring.

**Ideas for further tests:** that Delay Feedback of 1.05 stays bounded at a 2 s echo time; write the output to a WAV file and listen to it.

---

## 10. Code review: known issues

> **Status update.** Since the original review, issue **10.3** (no pitch shifting), issue **10.6** (unreliable sub-octave) and most of **10.8** (build portability) have been resolved; they are kept below, marked **RESOLVED**, as a record of what changed and why. Issue **10.1** (presets) was resolved with the pedal-style simulator, **10.2** and **10.4** (output beyond full scale) with the suggested fixes, and **10.5** (echo and reverb timings) and **10.9** (freeze) with the Daisy Seed work. Issue **10.7** and the minor items in 10.10 are still open.

Findings are ordered roughly by impact. Items marked **[measured]** were confirmed by compiling `DemonicEngine` on its own and feeding it synthetic test signals (sine waves and impulses) — they are facts about the code, though real voices will vary in degree. Items marked **[by inspection]** come from reading the code.

### 10.1 Presets were immediately overwritten by the sliders — **RESOLVED**

When the preset changed, `loadPreset()` copied the preset into the engine, and then, a few lines later in the same callback, all ten `set…()` calls overwrote those values with the slider atomics. Net effect: the preset only changed the name. (The atomics' defaults did not match preset 0 either.)

**Now:** the engine has `getPreset(i)`, and the UI thread's `selectPreset(i)` writes the preset's values into the encoder atomics before publishing the index, so the encoders turn to the preset and the callback applies them as usual (section 5.1). The app starts with `selectPreset(0)`, so the starting values are preset 0's. Pitch is still not part of presets.

### 10.2 Delay feedback above 1.0 grew without limit, and nothing limited the output — **RESOLVED**

**Now:** the echo is written back through `tanhf`, so above 1.0 it settles at a steady level instead of growing, and the mixed output is clamped to ±1 before the bypass crossfade. `test_extremes_stay_bounded` runs fold 5, drive 12 and feedback 1.05 for 60 s and checks the peak stays ≤ 1. The original analysis follows.

`demonic_engine.cpp:365` (now). The DELAY FB encoder allows feedback up to 1.05 (its ring turns red above 1.0). With feedback > 1, every echo is louder than the last, with no ceiling. In testing with a brief burst of input, peak output went from 0.33 to roughly **500,000** within 60 seconds. The audio hardware clips long before that, but the numbers keep growing, and if they ever reach infinity/NaN they would circulate in the loop forever (a restart would be needed). There is also no final limiter anywhere, so any stage can send values far outside ±1 straight to the speakers.

*Mitigation already available:* pressing **Bypass** flushes the loop (section 6.9), so a runaway can be silenced with one keypress. It is a workaround, not a fix: the loop can still run away again.

*Suggested fix:* bound the loop with a soft clipper where the signal is written back, and add a final safety clamp:

```cpp
feedbackDelayBuffer[feedbackDelayWriteHead] = std::tanh(wet + delayedSample * delayFeedback);
...
output[i] = std::clamp(output[i], -1.0f, 1.0f);   // or a soft limiter
```

### 10.3 `setFormantShift` and `formantCents` did nothing — **RESOLVED**

The engine stored a "formant shift" value that no code ever read, and the display's `PTCH:` label implied pitch shifting that did not exist. **Now:** `setPitchShift(cents)` drives a real pitch shifter (section 6.7), surfaced as the **PITCH** encoder in the desktop app; `setFormantShift` remains as an alias that forwards to it. Note that the shifter moves pitch and spectral shape together, so it is a pitch shifter, not a formant shifter (hence the rename). The `PTCH:` label is gone: the OLED now shows the preset name, or the value of the encoder being turned.

### 10.4 The wavefolder escaped the ±1 range — **RESOLVED**

**Now:** the floored modulo and clamped blend weight below are in the engine (with `floorf`/`fabsf`). The original analysis follows.

The original line (the fixed version is at `demonic_engine.cpp:315`):

```cpp
folded = std::abs(std::fmod(folded + 1.0f, 4.0f) - 2.0f) - 1.0f;
```

This is meant to be a triangle wave bounded by ±1. But `std::fmod` keeps the **sign of its first argument** (unlike Python's `%`, which always returns a non-negative result for a positive divisor). When `folded + 1` is negative — which happens for the negative half of the waveform at higher fold settings — the result is not folded but mirrored upward to values as high as about +5. Measured with a loud 220 Hz sine at Drive 12: output range was ≈ ±1.1 at fold 0, **−1.9 … +3.9** at fold 2.5, and **−5.6 … +7.2** at fold 5. It is also asymmetric, creating a large DC offset. Above fold 2.5 the blend weight `(1 − 0.4 × fold)` turns negative, which compounds it.

The MEGATRON (fold 3.0) and CYBER DEMON (fold 2.5) presets both sit in this range; feeding each preset a 0.5-amplitude 220 Hz sine for 3 seconds gave output peaks of about **3.4** (MEGATRON) and **2.2** (CYBER DEMON), against 0.55 for DEMONIC GROWL (fold 1.0). Exact figures depend on the input. No test currently guards this.

*Suggested fix:* use a true modulo that always returns a non-negative value, and limit the blend weight:

```cpp
float x = folded + 1.0f;
x -= 4.0f * std::floor(x / 4.0f);                 // Python-style x % 4.0
folded = std::fabs(x - 2.0f) - 1.0f;              // now always within [-1, 1]
float k = std::min(0.4f * wavefoldDrive, 1.0f);   // keep weights between 0 and 1
wet = wet * (1.0f - k) + folded * k;
```

### 10.5 Comments and constants disagreed with actual timings; the "reverb" was an echo — **RESOLVED**

**Now:** the echo reads `echoDelaySamples` *behind* the write head, set by the ECHO TIME control (20 ms–2 s, default 200 ms, the delay it really had), and `test_echo_time` checks the echo arrives on time. The reverb is DaisySP's ReverbSc, a diffuse 8-line design (`test_reverb_tail`). The original analysis follows.

Because of the circular-buffer rule in 6.3 (*delay = size − offset*), the offsets chosen were treated as delay times when they are not:

| Item | Code | Comment/intent | Measured |
|---|---|---|---|
| Echo tap (line 269) | offset 13,230 in a 22,050 buffer | "~300 ms" | **200 ms** |
| Reverb tap 1 (line 279) | offset 4,410 in 16,384 | 100 ms | **121.5 ms** |
| Reverb tap 2 (line 280) | offset 11,025 in 16,384 | 250 ms | **271.5 ms** |

The "reverb" is two feedback echoes (at about 122 and 272 ms, with a third at 393 ms from the loop), not a diffuse reverb. For a real reverb, use several comb and all-pass filters of mutually-prime lengths (e.g. a Schroeder or Freeverb design).

*Fix:* read from `(head + size − delaySamples) % size` so you specify the delay directly, and update the comments.

### 10.6 The sub-octave detector was unreliable — **RESOLVED**

The original toggled on both zero-crossing directions (same frequency as the input instead of half) and depended on a sample the noise gate had zeroed, so its output was erratic. **Now:** a rising-edge Schmitt-trigger divider operating on a low-passed copy of the voice (section 6.8). Measured with the same harness on the old and new engines, energy at half the input frequency with the sub on versus off: the old code showed none at 150 Hz and only weak, uneven amounts at 110–440 Hz; the new code shows it clearly (thousands of times the sub-off level) at every frequency and amplitude tested, and on a noisy harmonic signal. See `tests/engine_dsp_test.cpp`. Known remaining limit: it needs a reasonably steady, single pitch.

### 10.7 Cross-thread data not protected — *low* **[by inspection]**

- `scopeBuffer` (a plain `float` array) is written by the audio thread and read by the UI thread with no synchronisation.

In practice this "works" on common hardware (at worst a slightly torn waveform), but formally it is a data race. *Fix:* accept it, or pass the scope data through a lock-free queue. (The UI used to read the preset name through `getCurrentPresetName()` while the audio thread changed the index; it now looks the name up with `getPreset(guiActivePreset)`, which reads constant data.)

### 10.8 Build portability — mostly **RESOLVED**

**Fixed:** macOS `-framework` flags are now conditional on `if(APPLE)`; PortAudio is found via `pkg-config`, then `/opt/homebrew` and `/usr/local`, then the bundled copy, so Apple Silicon works without edits; the unneeded `ASM` language and the empty `dsp_pipeline.cpp` were removed from the build; the engine is a reusable library with tests registered in `ctest`; and the engine no longer uses `M_PI` (it uses a `float` constant, which also helps on the boards, see section 11). The engine also gained an explicit `#include <iterator>`: it used `std::begin`/`std::end` and had been getting the header only by accident through an unused `#include <vector>`.

**Since then:** the full CMake build, including the desktop app, has been built on macOS (see the verification note in section 7).

**Still open:** Windows and the Linux desktop build are untested; the `portaudio/` folder remains git-ignored, so a fresh clone relies on an installed PortAudio; and `tests/engine_smoke_test.cpp` no longer compiles (section 9).

### 10.9 Freeze behaviour — **RESOLVED**

Freeze captured the *oldest* 4,096 samples in the 500 ms echo buffer (audio from roughly 410–500 ms ago), not the most recent audio, and looped them with no crossfade at the loop point, producing a ~11 Hz flutter and a click. **Now:** it loops the most recent second, with a 20 ms crossfade at the seam (section 6.6, `test_freeze_loop`). A true "spectral" freeze would still need an FFT, holding magnitudes while randomising phases.

### 10.10 Minor items

- **No parameter smoothing.** Setter values are applied once per 64-sample block and take effect instantly, so fast encoder moves can produce small clicks ("zipper noise"). Smooth each parameter per-sample with a one-pole filter.
- ~~Integer overflow in `decimationCounter`~~: the counter is now `decimationPhase`, a float that stays between −N and 1.
- **The noise gate has no hysteresis or release**, so quiet speech near the threshold chatters on and off.
- **Mono only**, hard-wired to the default devices; no device selection or error recovery if a device disappears.
- ~~Decimation is in samples~~: it now holds in 44.1 kHz samples, so DECIMATE sounds the same at 48 kHz (`test_decimation_rate`). Every stage is now rate-independent (section 6.4).
- **Bypass re-initialises ReverbSc in one block** (it clears about 396 KB of SRAM, on the order of 0.1–0.4 ms on the Daisy Seed). The long SDRAM buffers are cleared in slices; if the benchmark shows the reverb's clear overrunning a block, split it the same way.
- **Empty files** `dsp_pipeline.hpp/.cpp` are placeholders that nothing uses. Delete them or fill them in so newcomers aren't misled.
- **Committed build leftovers.** `dsp_test` (a compiled test binary) and `project_dump.txt` (a text dump of the sources) are checked in. Neither is used by the build. Delete them and add `dsp_test` to `.gitignore`.
- ~~No top-level README~~: there now is one (`README.md`), and it links to these guides.

---

## 11. Toward the Daisy Seed

The hardware target is the **Electrosmith Daisy Seed 65MB**: an STM32H750 with one Cortex-M7 core at 480 MHz, a single- and double-precision FPU, an audio codec on the board, 512 KB AXI SRAM plus 64 MB SDRAM, 128 KB internal flash plus 8 MB QSPI, and 31 GPIO. It replaced the WeAct RP2350B Core (two Cortex-M33 cores at 150 MHz, 520 KB RAM, 48 GPIO), mainly because the RP2350B needed a separate WM8731 codec that made it cost about as much as a Daisy Seed; [HARDWARE_PLAN.md](HARDWARE_PLAN.md) has the comparison, audio design, pin map and shopping list. The Seed's headroom paid for the 48 kHz rate-independence (section 6.4), the 2 s echo and 1 s freeze in SDRAM, the ReverbSc reverb and the harmony voice. The first step of the port is not audio I/O but a go/no-go question: **can the board run the engine in real time at all?** `daisy/main_daisy.cpp` answers it by running the engine on synthetic input on the device and reporting how much of the real-time budget it uses.

### The budget

libDaisy runs the codec at 48 kHz, so a 64-sample block lasts 64 / 48,000 ≈ **1,333 µs**. At 480 MHz that is about **640,000 CPU cycles per block, or 10,000 cycles per sample**, to run every stage, plus the audio driver, display, and controls. The benchmark prints microseconds per block and the percentage of 1,333 µs used. Rough guide: under 50 % is comfortable, 50–80 % is tight, and over 100 % cannot work without optimisation.

### Running the benchmark

The instructions are in the header of `daisy/Makefile`. In short:

1. Install the Arm toolchain (`arm-none-eabi-gcc`) and `dfu-util`, clone [libDaisy](https://github.com/electro-smith/libDaisy) (with submodules) and build it once with `make`, and clone [DaisySP](https://github.com/electro-smith/DaisySP) with `--recurse-submodules` (only its ReverbSc source is compiled).
2. `make -C daisy LIBDAISY_DIR=~/src/libDaisy DAISYSP_SRC_DIR=~/src/DaisySP` (or `.agents/skills/vocal-fx-processor/scripts/build_daisy.sh`) builds `daisy/build/vocal_daisy_bench.bin`.
3. Put the Seed in DFU mode (hold BOOT, press and release RESET, release BOOT), then flash with the same `make` line plus `program-dfu` (or `build_daisy.sh program-dfu`).
4. Open the USB serial port (macOS: `screen /dev/tty.usbmodem* 115200`). The program waits for the port to open before it starts.

It repeats a table of results every few seconds covering: a baseline with optional effects off, baseline plus pitch shift, baseline plus reverb, each of the three presets, and a worst case (MEGATRON + pitch shift + harmony + freeze). The scenarios and timing loop live in `include/engine_bench.hpp`.

**Memory placement.** `DemonicEngineMemory` is a `DSY_SDRAM_BSS` global, so its 576 KB go in the SDRAM. The SDRAM only works after `hw.Init()`, and the engine's constructor clears that memory, so the engine is a `static` local constructed in `main()` after `hw.Init()` (its ~410 KB still land in `.bss`, in AXI SRAM). The last build used 94 KB of the 128 KB flash, 438 KB (84 %) of AXI SRAM and 580 KB of SDRAM.

### The firmware

`daisy/firmware.cpp` (`make -C daisy APP=firmware ...`, or `build_daisy.sh`, which builds both) is the pedal itself: the engine in libDaisy's audio callback at 48 kHz in 64-sample blocks, and the panel in the main loop, laid out as in [HARDWARE_PLAN.md §3](HARDWARE_PLAN.md#3-firmware-layout). The encoders go through `simctl::Quadrature` and `simctl::step`, the rings through `simctl::ring_led_lit`, and the values reach the callback through atomics and `simctl::apply`, as in the desktop app. The OLED shows the preset (or the encoder just turned), the bypass/freeze state and the DSP load. It needs `-u _printf_float` (set in the Makefile) for `simctl::format`; the last build used 113 KB (88 %) of the 128 KB flash. It is compile-checked only: nothing in it has run on a board.

> **Not yet run on hardware.** The benchmark builds against libDaisy, but no Seed has run it yet. Treat the first on-device run as the real measurement.

### The previous board's benchmark

`pico/` held the same benchmark for the WeAct RP2350B Core. It was removed when the engine outgrew the RP2350B's 520 KB (the engine and its memory now need about 1 MB); it is in the git history, last on `main` before the change that removed it.

### Things to expect and what to try if it is too slow

- **Probably no optimisation needed.** At 10,000 cycles per sample, about three times one RP2350 core's budget, the per-sample `sinf` (twice), `tanhf` (twice) and the crossfade `cosf` that were the worry on the previous board should fit easily, and the Cortex-M7's FPU does single- *and* double-precision maths in hardware. Do not optimise until the benchmark says so. The engine still uses `float` throughout (`kTwoPi`, not the `double` `M_PI`), which is right for any FPU.
- **The new costs to watch** are the ReverbSc (8 interpolated, modulated delay lines per sample, the likely largest single cost), the harmony voice (a second `pitchTap`, when on) and the SDRAM reads: one or two per sample each for the echo and the freeze loop. SDRAM is far slower than SRAM, but a few accesses per sample are small next to the budget; the "Baseline + reverb" and "Worst" rows show both.
- **If it is over 50 % after all**, in rough order of expected payoff (measure after each change): put the hot code in the M7's ITCM and small hot state in DTCM; replace the per-sample `sin` calls with a recurrence oscillator and `tanh` with a cheap polynomial; use 32-sample blocks only if latency needs it (per-block overhead doubles).
- **Memory.** `DemonicEngine` (about 410 KB, 84 % of AXI SRAM with libDaisy's own needs) must stay a global or static, never a local variable, and out of SDRAM; most of it is the ReverbSc, which is read every sample. The long buffers in `DemonicEngineMemory` are in SDRAM (64 MB, under 1 % used), so a longer echo or freeze costs only `kEchoSize` / `kFreezeSize`. If AXI SRAM gets tight: move the ReverbSc to SDRAM and measure the cost, or shorten its maximum size.
- **Program size.** The internal flash is 128 KB, of which the benchmark uses 94 KB. If the firmware outgrows it, use the Daisy bootloader and run from QSPI or SRAM (libDaisy's `APP_TYPE`).
- **I/O.** The Seed's own codec (with an op-amp mic preamp in front, since its input is line level), an SSD1306 128×64 OLED (the simulator's is already that size), WS2812 LED rings driven over SPI (the STM32 has no PIO), and the simulator's controls: twelve encoders (eleven on GPIO, ECHO TIME on an MCP23017 I²C expander) and three footswitches. The audio design, firmware layout, latency and parts are in [HARDWARE_PLAN.md](HARDWARE_PLAN.md); the pin map is `include/hw_pinout.hpp` (28 of 31 GPIO, plus 2 expander pins). Encoders (not pots) were chosen so that presets can move them.
- **Structure.** Because the engine has no PortAudio or raylib dependency, the finished firmware can create a `DemonicEngine` and call `processBlock` from libDaisy's audio callback, with encoders, footswitches, LEDs and the OLED handled in the main loop: the same shape as the desktop app's audio and UI threads.

---

## 12. Glossary

| Term | Meaning |
|---|---|
| **Atomic** | A variable whose reads and writes are indivisible, so two threads can share it safely. |
| **Audio callback** | A function the audio library calls whenever the sound hardware needs another block of samples. |
| **Block / buffer / frame** | A small group of consecutive samples processed together (64 here). In mono, one frame = one sample. |
| **Circular (ring) buffer** | A fixed array used as a loop, overwriting the oldest data. |
| **Clipping** | Output exceeding ±1.0 being flattened by the hardware, causing harsh distortion. |
| **DC offset** | A constant shift of the whole signal away from zero. Inaudible itself but wastes headroom. |
| **Decimation** | Reducing the effective sample rate by holding samples. |
| **dBFS** | Decibels relative to full scale (±1.0). 0 dBFS is the maximum; −34 dBFS ≈ amplitude 0.02. |
| **DSP** | Digital signal processing — math applied to streams of samples. |
| **Dry / wet** | The unprocessed / processed signal. |
| **Header** | A `.hpp`/`.h` file declaring what classes and functions exist. |
| **Immediate-mode UI** | A UI style where controls are re-evaluated and re-drawn every frame, with no persistent widget objects. |
| **LFO** | Low-frequency oscillator; a slow wave used to modulate a parameter. |
| **Linker** | The build step that joins compiled files and libraries into an executable. |
| **One-pole filter** | The simplest smoothing/low-pass filter: `y += a·(x − y)`. |
| **Pointer** | A variable holding a memory address; here, usually the start of an array. |
| **Real-time safe** | Code guaranteed not to block, allocate, or otherwise run unpredictably long. |
| **Ring modulation** | Multiplying a signal by an oscillator to produce metallic sidebands. |
| **Sample / sample rate** | One number of the audio stream / how many per second. |
| **Wavefolder** | A waveshaper that reflects peaks back instead of cutting them off. |
