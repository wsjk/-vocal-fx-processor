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
11. [Toward the Raspberry Pi Pico](#11-toward-the-raspberry-pi-pico)
12. [Glossary](#12-glossary)

---

## 1. Project at a glance

**What it is.** A real-time vocal effects processor. A microphone signal goes in, passes through a chain of ten effect stages, and comes out of the speakers. The goal is a standalone guitar-pedal-style device built on a Raspberry Pi Pico (RP2040), with a desktop version that simulates the pedal's 128×64 OLED display so the effects can be developed on a laptop first.

**Technology**

| Piece | Role |
|---|---|
| **C++17** | Language of everything. |
| **PortAudio** | Cross-platform library for talking to the sound card. |
| **raylib** | Small graphics library for the window, buttons and sliders. |
| **CMake** | Build tool — decides which files to compile and what to link together. |

**File map**

```
vocal-fx-processor/
├── CMakeLists.txt              Build recipe: engine library, tests, desktop app
├── include/
│   ├── demonic_engine.hpp      The DSP engine's "interface": class + preset struct   [implemented]
│   └── dsp_pipeline.hpp        EMPTY (0 bytes), not used by the build
├── src/
│   ├── demonic_engine.cpp      The DSP engine's code: pitch shifter + the effect stages [implemented]
│   └── dsp_pipeline.cpp        EMPTY (0 bytes), not used by the build
├── desktop/
│   └── main_desktop.cpp        Desktop app: audio I/O, window, sliders, OLED mock   [implemented]
├── pico/
│   ├── CMakeLists.txt          Separate build for the Raspberry Pi Pico (needs the Pico SDK)
│   └── main_pico.cpp           Benchmark: how much real-time budget does the engine use on an RP2040?
├── tests/
│   ├── engine_smoke_test.cpp   Silence / NaN / peak-level checks on every preset
│   └── engine_dsp_test.cpp     Measures output frequencies: pitch-shifter accuracy, sub-octave behaviour
├── portaudio/                  Vendored PortAudio source. Last-resort fallback only; listed in .gitignore
└── .gitignore                  Ignores build/ and portaudio/
```

The product itself is two files: the **engine** (`demonic_engine.hpp`/`.cpp`, about 270 lines of `.cpp`, all of the sound) and the **desktop app** (`main_desktop.cpp`, about 225 lines, everything around it). The engine has no dependency on audio hardware or graphics, which is why it can be tested and benchmarked by itself. `dsp_pipeline.*` are empty placeholders and can be deleted.

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

Python's Global Interpreter Lock quietly protects you from many threading hazards; C++ has no GIL. If one thread writes a float while another is reading it, the result is formally undefined. `std::atomic<float>` wraps a value so that each `.load()` and `.store()` is indivisible. In this project it is used as a *mailbox*: the UI thread drops in a slider value with `.store(x)`, and the audio thread reads it with `.load()`. See section 4.

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
| **UI thread** | `main()`'s `while` loop | Reads keyboard/mouse, updates sliders, draws the window | 60×/second |

They communicate **only** through a set of global `std::atomic` variables (the "mailbox").

```
                         UI THREAD (main, 60 fps)
  keyboard / mouse ───►  ┌─────────────────────────────┐
                         │ handle P / F keys, buttons, │
                         │ drag sliders                │
                         └───────┬─────────────▲───────┘
                       .store()  │             │  .load()
                                 ▼             │
              ┌─────────────────────────┐  ┌───┴─────────────────────┐
              │ guiDrive, guiSubBlend,  │  │ currentMicPeak          │
              │ guiRobotBlend, …        │  │ scopeBuffer / scopeHead │
              │ guiActivePreset         │  │ (for the OLED mock)     │
              │ guiSpectralFreeze       │  └───▲─────────────────────┘
              └───────┬─────────────────┘      │ .store()
                      │ .load()                │
                      ▼                        │
                         AUDIO THREAD (every 64 samples)
  microphone ──► PortAudio ──► audioCallback ──┴─► DemonicEngine::processBlock ──► PortAudio ──► speakers
```

### 4.2 What happens in one audio block

Every ~1.45 ms, PortAudio calls `audioCallback` (`main_desktop.cpp:35`), which:

1. Checks whether the preset changed; if so, calls `demonic.loadPreset(...)` (lines 43–48).
2. Copies every slider value from the atomics into the engine via setters (lines 50–62). `setBypass` goes first, so that Freeze is refused in the same block if bypass has just been switched on.
3. Measures the input peak for the level meter and calls `demonic.processBlock(in, out, 64)` (lines 64–69).
4. Copies the output into the oscilloscope ring buffer (lines 71–76) and publishes the mic peak (line 81).
5. Returns `paContinue` ("keep going").

### 4.3 Design decisions worth knowing

- **The engine knows nothing about PortAudio or raylib.** `DemonicEngine` takes in a float array and fills a float array. That is why it can be tested headlessly (section 9) and is the part meant to be reused on the Pico.
- **Everything is allocated up front.** No `new`, no `malloc`, no `std::vector` growth in the audio path — required for real-time safety and for microcontrollers.
- **Control values are copied in once per block**, not per sample. Cheap, but it means parameter changes are "steppy" (see section 10.10).
- **The engine is a single global object** (`DemonicEngine demonic(SAMPLE_RATE);`, `main_desktop.cpp:13`). Both threads can see it, but by convention only the audio thread should touch its state (the UI thread breaks this convention in two small places — section 10.7).
- **Memory footprint:** `sizeof(DemonicEngine)` is about **182 KB** (186,824 bytes measured), almost all of it the five buffers.

---

## 5. Walkthrough: the desktop app (`desktop/main_desktop.cpp`)

### 5.1 Globals (lines 10–33)

```cpp
#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 64
DemonicEngine demonic(SAMPLE_RATE);

std::atomic<bool>  guiBypass(false);     // true bypass: effects off, dry signal only
std::atomic<float> guiPitch(0.0f);       // pitch shift in cents; 0 = shifter off
std::atomic<float> guiDrive(11.0f);
...
std::atomic<int>   guiActivePreset(0);
std::atomic<bool>  guiSpectralFreeze(false);
std::atomic<float> currentMicPeak(0.0f);

constexpr size_t SCOPE_SIZE = 128;
float scopeBuffer[SCOPE_SIZE] = {0};
std::atomic<size_t> scopeHead(0);
```

One atomic per control, initialised to the slider's default. The **scope buffer** is a 128-sample circular buffer the audio thread fills with output samples; the UI reads it to draw the waveform. `scopeHead` is how many samples have been written so far (it grows without bound; reads wrap it with `% SCOPE_SIZE`).

### 5.2 The audio callback (lines 35–83)

The signature is dictated by PortAudio; you cannot change it. The parts you care about are `inputBuffer`, `outputBuffer`, and `framesPerBuffer`. The first thing it does is cast the generic `void*` buffers to `float*` (since the stream was opened with `paFloat32`):

```cpp
const float* in = static_cast<const float*>(inputBuffer);
float* out = static_cast<float*>(outputBuffer);
```

The preset check uses a `static` local, `lastPreset`, which persists across calls so the callback can detect a *change*. Then every parameter is pushed into the engine, then the block is processed.

### 5.3 `main()` setup (lines 85–106)

1. `Pa_Initialize()` starts PortAudio.
2. `Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, SAMPLE_RATE, FRAMES_PER_BUFFER, audioCallback, nullptr)` — opens the default devices with **1 input channel, 1 output channel (mono), 32-bit float samples, 44.1 kHz, 64-frame blocks**, and registers `audioCallback`. (`&stream` passes the address of `stream` so the function can fill it in — an "output parameter".)
3. `Pa_StartStream(stream)` — from here on, the callback is being called.
4. `InitWindow(480, 630, ...)` creates the window; `SetTargetFPS(60)` limits the loop to 60 frames/second.
5. `LoadRenderTexture(128, 64)` creates an off-screen 128×64 image — the fake OLED.

Error handling is by return code, not exceptions: functions return `paNoError` on success and `main` prints and exits otherwise.

### 5.4 The UI loop (lines 108–245)

Each frame:

1. **Keys:** `P` cycles `guiActivePreset`; `F` toggles `guiSpectralFreeze` (ignored while bypassed); `B` or `Space` toggles `guiBypass` (and clears `guiSpectralFreeze` when switching bypass on).
2. **Snapshot:** reads every atomic into a local variable so the whole frame is drawn from one consistent set of values.
3. **Draw the OLED** (lines 142–169) into the 128×64 off-screen texture: border, preset name, optional `[FREEZE ON]`, the waveform (127 line segments between consecutive `scopeBuffer` samples, clamped to stay inside the display), and the mic-level bar.
4. **Draw the window** (lines 171–244): scales the OLED texture up 2× and blits it (`DrawTexturePro`); draws the three buttons with hit-testing by `CheckCollisionPointRec`; draws the bypass button and eleven sliders with the `drawSlider` lambda.

This style is called **immediate-mode UI**: there are no button or slider objects. Every frame the code asks "is the mouse over this rectangle and pressed?", acts on the answer, and draws it. The `drawSlider` lambda (line 218) both draws the bar and — if the mouse is held down inside it — computes the new value from the mouse x-position and `.store()`s it to the atomic.

### 5.5 Shutdown (lines 247–253)

Unload the texture, close the window, stop and close the audio stream, terminate PortAudio.

---

## 6. Walkthrough: the DSP engine

### 6.1 The header (`include/demonic_engine.hpp`)

**`VocalPreset`** — a plain struct of ten numbers plus a name.

**`DemonicEngine`** — the public interface:

| Method | Purpose |
|---|---|
| `DemonicEngine(float sampleRate)` | Create the engine; zero buffers; define presets |
| `processBlock(in, out, n)` | The workhorse: process `n` samples |
| `setDrive`, `setSubOctaveBlend`, `setRobotBlend`, `setRobotFreq`, `setWavefold`, `setDecimation`, `setDelayFeedback`, `setDelayMix`, `setReverbMix`, `setMix` | Plain setters: store a value in a member |
| `setSpectralFreeze(bool)` / `isSpectralFrozen()` | Freeze control (refused while bypassed) |
| `setBypass(bool)` / `isBypassed()` | True bypass: exact pass-through with all effect memory cleared. Section 6.9 |
| `setPitchShift(float cents)` | Pitch shift, −1200…+1200 cents (100 cents = 1 semitone); 0 bypasses the shifter. `setFormantShift` is kept as a deprecated alias that forwards to it |
| `loadPreset(int)`, `getCurrentPresetName()`, `getPresetCount()` | Preset handling |

**Private state** falls into three groups:

- **Parameters** (`driveDb`, `subBlend`, `robotBlend`, …) — set from outside. These survive a bypass untouched.
- **Per-stage memory** (`subDetLP1`, `subDetLP2`, `subDetEnv`, `subArmed`, `subToggle`, `subFilterState`, `lowpassState`, `ringModPhase`, `heldSample`, `decimationCounter`, `dcInPrev`, `dcOutPrev`, `inputEnvelope`, `pitchPhase`, `pitchBlend`) — what each effect must remember from one sample to the next.
- **Buffers** — five fixed arrays:

| Buffer | Size | Duration at 44.1 kHz | Used by |
|---|---|---|---|
| `pitchBuffer` | 2,048 | 46 ms | Pitch shifter (uses up to 1,768 of it) |
| `delayLine` | 2,048 | 46 ms | Chorus |
| `feedbackDelayBuffer` | 22,050 | 500 ms | Echo and Freeze capture |
| `freezeBuffer` | 4,096 | 93 ms | Freeze |
| `reverbBuffer` | 16,384 | 371 ms | Reverb |

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
    delayed = buf[(head + K) % 22050]   # read from a position relative to the head
    buf[head] = x                       # write the new sample at the head
    head = (head + 1) % 22050           # advance, wrapping at the end
    return delayed
```

When the head advances, it overwrites the oldest sample. A slot at offset *k* **ahead** of the head was last written `22050 − k` samples ago — which means the **delay time is `size − offset`**, not `offset`. This subtlety matters in section 10.5.

### 6.4 The constructor and presets (`demonic_engine.cpp:33–57`)

Sets all members, zero-fills the five buffers with `std::fill`, defines three presets using brace-initialisation (`presets[0] = {"DEMONIC GROWL", 11.0f, ...};` — values in the same order as the struct fields), and calls `loadPreset(0)`.

`loadPreset(i)` (lines 108–120) copies the ten fields from `presets[i % 3]` into the live parameter members. The `% NUM_PRESETS` makes cycling wrap around.

### 6.5 `processBlock` stage by stage (`demonic_engine.cpp:130–304`)

Before the loop, two values are computed once per block:

```cpp
float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);        // dB → linear
int decimationStep   = std::max(1, static_cast<int>(decimationFactor));
```

Then a `for` loop handles each sample `input[i]` through the gate, the pitch shifter, and ten numbered stages, in this order:

| # | Lines | Stage | What the code does |
|---|---|---|---|
| gate | 151–152 | **Noise gate** | `activeSignal = abs(dry) > 0.02 ? dry : 0`. Anything quieter than 0.02 (−34 dBFS) is zeroed. Also updates `inputEnvelope`, a smoothed loudness meter (a one-pole filter on `abs(activeSignal)`). |
| 0b | 154–176 | **Pitch shifter** | One read tap sweeps back through `pitchBuffer`, reading the audio faster or slower than it was written (that is what changes the pitch). A second tap covers for it while the first jumps back at the end of each sweep. Faded in and out over about 10 ms so switching it on or off does not click. Details in section 6.7. |
| 1 | 178–193 | **Chorus doubler** | Writes the gated signal into `delayLine`. A 0.5 Hz sine LFO modulates a read position around 441 samples (10 ms) ± 66 samples. It reads between two neighbouring samples with **linear interpolation** (`a*(1-frac) + b*frac`) because the position is fractional. Adds 30% of that copy to the voice. |
| 2 | 195–224 | **Sub-octave** | A Schmitt-trigger octave divider: watches a low-passed copy of the voice, flips a square wave on each rising edge (once per cycle, so the square runs at half the frequency), smooths it (one-pole at about 290 Hz) and scales it by the voice's loudness (`inputEnvelope`) and `subBlend`. Details in section 6.8. |
| 3 | 226–234 | **Ring modulation** | An oscillator at `robotFreq + 40 × inputEnvelope` Hz (`ringModPhase` is the oscillator's angle in radians, wrapped at 2π). `wet` is crossfaded between the clean signal and `wet × sin(phase) × 1.5` by `robotBlend`. |
| 4 | 236–243 | **Drive and wavefold** | `wet = tanh(wet × gainMultiplier)` is the saturation. If `wavefoldDrive > 0.1`, a triangle fold is applied and mixed in at weight `0.4 × wavefoldDrive`. |
| 5 | 245–253 | **Decimation** | A counter selects every Nth sample to "hold" (`heldSample`); in-between samples repeat it. Then `round(wet × 32) / 32` quantises to about 6 bits. |
| 6 | 255–257 | **Tone filter** | One-pole low-pass, `a = 0.35` → roughly 3 kHz cutoff at 44.1 kHz. |
| 7 | 259–266 | **Spectral freeze** | If active, reads `freezeBuffer` in a loop and mixes it 70/30 with live signal. |
| 8 | 268–276 | **Feedback delay** | Reads from `feedbackDelayBuffer`, writes `wet + delayed × delayFeedback` back in, and crossfades the output by `delayMix`. |
| 9 | 278–288 | **"Catacomb" reverb** | Two tap reads from `reverbBuffer` (weights 0.6 and 0.4), written back with 0.55 feedback; crossfaded by `reverbMix`. |
| 10 | 290–294 | **DC blocker** | `y = x − x_prev + 0.995 × y_prev`. A classic high-pass at a few Hz. |
| mix | 296–302 | **Wet/dry mix, then bypass crossfade** | `processed = dry × (1 − mix) + wet × mix`. Note `dry` is the *ungated* input. Then `output = processed × (1 − bypassBlend) + dry × bypassBlend`, where `bypassBlend` ramps between 0 and 1 over 5 ms (section 6.9). |

#### The one-pole filter, in Python

Used for the sub filter, tone filter, and envelope follower, so it is worth understanding once:

```python
def one_pole(state, x, a):
    return state + a * (x - state)     # state "chases" x; smaller a = slower = more smoothing
# cutoff (Hz) ≈ -sample_rate * ln(1 - a) / (2π)
#   a = 0.35 → ≈ 3.0 kHz  (tone filter)
#   a = 0.04 → ≈ 290 Hz   (sub filter)
```

In C++ the state is a member variable and the assignment is `lowpassState = lowpassState + 0.35f * (wet - lowpassState);` — identical.

#### Mixing, crossfades and "wet" vs "dry"

*Dry* = the original. *Wet* = the processed signal at that point in the chain. The pattern `wet = wet*(1-m) + effect*m` appears throughout; `m = 0` leaves the signal alone, `m = 1` replaces it with the effect.

### 6.6 Spectral freeze details

`setSpectralFreeze(true)` (lines 92–106) — only on the *off → on* transition — copies 4,096 samples out of `feedbackDelayBuffer` starting at the write head into `freezeBuffer` and resets the read position. Because the write head points at the **oldest** data, this captures audio from roughly 0.41–0.5 s ago, not the most recent audio. Playback then loops those 4,096 samples (a ~93 ms loop, ≈ 10.8 repetitions per second).

### 6.7 The pitch shifter

**The idea.** Imagine a tape playing back while you read it from a spot that slowly moves away from (or toward) the write position. If the read position moves *away* (the delay is growing), you read the recording more slowly than it was made, so the pitch drops. If it moves *toward* the write position, you read it faster and the pitch rises. A circular buffer plus a read position that sweeps is all it takes:

```python
# Python sketch of the engine's shifter (one tap; the real code adds a covering tap, see below)
ratio = 2 ** (cents / 1200)                  # 0.5 = octave down, 2.0 = octave up
phase += (1 - ratio) / WINDOW                # how far the read position slides each sample
phase -= floor(phase)                        # wrap into [0, 1): that is the "jump back"
delay = 2 + phase * WINDOW                   # in samples; WINDOW is 1764 (40 ms)
out = read_with_interpolation(buffer, write_head - delay)
```

**The catch.** The read position cannot slide away forever (the buffer is finite), so every `WINDOW / abs(1 - ratio)` samples it must jump back, and that jump is a discontinuity in the audio. The code hides the jump with a **second tap, half a sweep away**, and a short crossfade (the first 10 % and last 10 % of each sweep, about 4 ms) between the two taps.

**Why not just blend the two taps 50/50 all the time?** That was the first implementation here, and the tests caught it: with a steady tone the two taps are a fixed time apart, so they cancel the main frequency and leave two beating side tones. A 220 Hz sine pitched down 5 semitones came out as a pair of peaks at 157 and 170 Hz with essentially nothing at the expected 164.8 Hz. Letting one tap do almost all of the work and using the second only to cover the jump keeps the pitch clean.

**Properties to be aware of**

| Property | Detail |
|---|---|
| Latency | The delay sweeps between about 0 and 40 ms, so the shifted voice is on average ~20 ms behind your dry voice. The shifter is fully bypassed (no delay) when the pitch is 0. |
| Accuracy | On a voice-like test signal every shift tested was within about 6 cents. On a pure sine it is within about 20 cents up to ±1 octave, 40 at the extreme. Pure tones are the worst case because they lock onto the sweep rate. |
| Character | A faint flutter or tick at the sweep rate, which is `abs(1 − ratio) × 44100 / 1764` Hz: about 6 Hz at −5 semitones and 25 Hz at ±1 octave. Larger shifts sound rougher. |
| Formants | The whole spectrum moves, so it does **not** preserve formants. Big shifts sound like a different-sized voice, not the same voice at another pitch. A formant-preserving shifter would need a spectral or PSOLA-style design. |
| Cost | Two interpolated buffer reads per sample, one `cos` only during the crossfades, one `pow` per block. See section 11. |
| Switching | `pitchBlend` ramps between 0 and 1 (about 10 ms) when the pitch moves to or from 0, so the bypass boundary does not click. The whole stage is skipped, costing nothing, while bypassed. |

The sweep length (`kPitchWindow`) and the crossfade fraction (`kPitchFade`) are named constants at the top of `demonic_engine.cpp`. Longer windows handle lower voices better but add latency; a smaller crossfade gives a cleaner pitch with harder splices.

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
       for (size_t i = 0; i < numFrames; ++i) output[i] = input[i];
       return;                                  // no DSP at all: also saves CPU
   }
   ```

2. **It does not click.** Switching mid-note would otherwise jump instantly between two different waveforms. Instead, `bypassBlend` slides linearly between 0 (effects) and 1 (dry) over 5 ms, and the final line of the sample loop blends the two. A linear ramp (not an exponential one) matters: it reaches exactly 1.0, so the exact pass-through above can take over cleanly.

3. **It wipes the effects' memory once.** `resetState()` zeroes every buffer (echo, reverb, chorus, pitch, freeze) and every filter and detector variable, but not the *parameters*, so your slider settings are all still there when you switch back. Without this wipe, switching bypass off would replay up to half a second of audio from before the bypass. A useful side effect: **bypass is a panic button**. A runaway echo loop (Delay Feedback above 1.0, issue 10.2) is flushed by a single bypass cycle; the test confirms a loop that had reached 74× full scale is silent afterwards. It also clears any NaN or infinity stuck in the loop.

   The wipe happens once per bypass period (`bypassStateCleared`), not every block, so it costs nothing while bypassed.

**Freeze and bypass.** A frozen loop lives in a buffer that the wipe clears, so Freeze is switched off by `setBypass(true)`, and `setSpectralFreeze(true)` is ignored while bypassed. The desktop app mirrors this: its bypass handler also clears `guiSpectralFreeze`, and the `F` key and freeze button do nothing while bypassed. Without that mirroring, the callback would re-assert the old freeze flag every block and "freeze" an empty buffer the moment bypass ended.

**Thread safety.** As with every other setter, `setBypass` is called from the audio callback after the UI thread stores the flag in `guiBypass`; nothing else touches `bypassTarget` or `bypassBlend`.

**What it is not.** This is *software* bypass: the signal still goes through the computer's input and output path (about 1.5 ms of buffering plus the audio device's own latency). For a truly zero-latency clean voice, use the audio interface's own direct-monitor switch.

---

## 7. The build system (`CMakeLists.txt`)

CMake describes your project as a set of **targets** (things to build) and the **dependencies** between them, much as a Python project has packages and imports. The file defines three targets, plus the options that control them:

| Target | What it is | Depends on |
|---|---|---|
| `demonic_engine` | A static library built from `src/demonic_engine.cpp`. The DSP code, with no audio or graphics. | nothing |
| `engine_smoke_test`, `engine_dsp_test` | The two test programs in `tests/`. | `demonic_engine` |
| `vocal_desktop` | The desktop app. | `demonic_engine`, PortAudio, raylib (plus Apple system frameworks on macOS) |

Splitting the engine into its own library is what lets the tests, the desktop app, and later the Pico build all share one copy of the DSP code.

### Options

| Option | Default | Effect |
|---|---|---|
| `BUILD_DESKTOP` | ON | Build the desktop app. Turn OFF to build only the engine and tests, with no PortAudio or raylib needed. |
| `BUILD_TESTS` | ON | Build the two test programs and register them with `ctest`. |
| `USE_SYSTEM_RAYLIB` | OFF | Use an installed raylib 5.0+ (`find_package`) instead of downloading raylib 5.0 at configure time. |

Release (optimised) is the default build type, since real-time DSP needs it. Pass `-DCMAKE_BUILD_TYPE=Debug` to override.

### How dependencies are found

- **PortAudio**, in order: (1) `pkg-config` (`portaudio-2.0`); (2) a manual search that includes both `/opt/homebrew` (Homebrew on Apple Silicon) and `/usr/local` (Homebrew on Intel Macs and most Linux installs); (3) the copy in `./portaudio`, built from source, if it exists. If none of these succeeds, configuration stops with a message saying how to install it.
- **raylib:** downloaded and built from source by CMake's `FetchContent` (needs internet on the first configure), unless `USE_SYSTEM_RAYLIB` is ON.
- **macOS frameworks** (`CoreAudio`, `Cocoa`, `OpenGL`, and so on) are linked only `if(APPLE)`, so the file no longer breaks other platforms at that step.

### Common commands

```bash
cmake -S . -B build                          # configure (downloads raylib the first time)
cmake --build build                          # compile and link everything
./build/vocal_desktop                        # run the app
cd build && ctest --output-on-failure        # run the tests

cmake -S . -B build -DBUILD_DESKTOP=OFF      # engine + tests only (no PortAudio/raylib needed)
cmake -S . -B build -DUSE_SYSTEM_RAYLIB=ON   # use Homebrew's raylib
```

> **Verification status.** The engine, both test programs, and the Pico benchmark's logic were compiled and run on Linux with g++ when this version was written. The `CMakeLists.txt` and the desktop app change (the Pitch slider) were checked for syntax and structure, but were **not** built end-to-end with CMake, PortAudio, and raylib, so the first build on your Mac is the real test. If the configure step fails, the error message names the missing piece (PortAudio is the likeliest). The portable CMake structure is untested on Windows.

The Raspberry Pi Pico build lives in a **separate** CMake project (`pico/CMakeLists.txt`) because it needs the Pico SDK and an ARM cross-compiler; see section 11.

---

## 8. How to extend it

### 8.1 Add a new parameter (worked example: tone-filter brightness)

Right now the tone filter's coefficient `0.35` is hard-coded (the `lowpassState = ...` line in stage 6, `demonic_engine.cpp:256`). Here is how to make it adjustable.

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
void DemonicEngine::setTone(float c) { toneCoeff = c; }

// in processBlock, replace the stage 6 line:
lowpassState = lowpassState + toneCoeff * (wet - lowpassState);
```

**3. App — add the mailbox, copy it in, add a slider** (`desktop/main_desktop.cpp`):

```cpp
std::atomic<float> guiTone(0.35f);                         // with the other globals

demonic.setTone(guiTone.load());                           // in audioCallback, with the other setters

drawSlider("Tone", toneVal, 0.05f, 0.9f, guiTone);         // in the UI loop, plus: float toneVal = guiTone.load();
```

The window is 668 px tall (`screenHeight`, line 102) and each slider takes 34 px; the 11 existing sliders start at y = 258 and reach y ≈ 632, so a twelfth would just fit but leave no margin; make the window taller first.

**4. (Optional) add it to presets** — add a `float tone;` field to `VocalPreset`, extend the three brace-initialiser lines (the values must stay in the declared order), and copy it in `loadPreset`. **Note** that presets currently have no effect on the sound until issue 10.1 is fixed.

### 8.2 Add a preset

1. In the header, change `static const int NUM_PRESETS = 3;` to `4`. (The array `VocalPreset presets[NUM_PRESETS]` is sized from it.)
2. In the constructor, add `presets[3] = {"NEW NAME", ...ten numbers in field order...};`.

### 8.3 Add a new effect stage

1. Decide where it sits in the chain (order matters: effects before `tanh` get distorted; effects after it do not).
2. Add any state it needs (a phase, a filter memory, a buffer) as private members and initialise them in the constructor, zeroing any arrays.
3. Insert the code inside the `for` loop in `processBlock`, working on the `wet` variable.
4. Expose its controls with setters, atomics and a slider as in 8.1.
5. If it needs a buffer, remember: fixed size, wrap indexes with `%`, and watch your memory budget if the Pico is a target (section 11).

**Real-time rules for anything you add:** no `new`/`delete`/`malloc`, no `std::vector` growth, no printing, no mutexes, no file access inside `processBlock` or the callback.

---

## 9. Testing without a microphone

Because `DemonicEngine` has no dependency on audio hardware, tests can drive it from a plain program: generate a signal, run it through `processBlock`, and analyse what comes out. There are two test programs.

### Running them

With CMake (works for the engine alone, no PortAudio or raylib needed):

```bash
cmake -S . -B build -DBUILD_DESKTOP=OFF
cmake --build build
cd build && ctest --output-on-failure
```

Or directly with a compiler, from the repo root:

```bash
g++ -std=c++17 -Iinclude tests/engine_smoke_test.cpp src/demonic_engine.cpp -o smoke && ./smoke
g++ -std=c++17 -Iinclude tests/engine_dsp_test.cpp   src/demonic_engine.cpp -o dsp_test && ./dsp_test
```

Each prints `[PASS]`/`[FAIL]` lines and exits non-zero on failure (which is how `ctest` knows).

### `engine_smoke_test.cpp`: does it stay sane?

Checks that silence in gives silence out, and that every preset produces finite (non-NaN) output on a sine input, reporting its peak level. Expected output on the current code:

```
[PASS] silence in -> silence out
[PASS] preset 0 (DEMONIC GROWL): finite=yes, peak=0.58
[PASS] preset 1 (MEGATRON): finite=yes, peak=4.03  (WARNING: exceeds full scale)
[PASS] preset 2 (CYBER DEMON): finite=yes, peak=4.15  (WARNING: exceeds full scale)
```

The warnings are informational (they do not fail the test) and document a real problem, issue 10.4.

### `engine_dsp_test.cpp`: does it sound right?

Measures **frequencies in the output**:

| Test | What it checks |
|---|---|
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

**Writing your own.** Use the helpers: `isolate(engine)` turns every optional effect off so only the one under test is audible; `render(engine, generator, seconds, keepSeconds)` feeds a signal in 64-sample blocks (the desktop app's block size) and returns the last part of the output; then assert on `magnitude_at` or `dominant_freq`.

**Ideas for further tests:** assert that peak output stays ≤ 1.0 once a limiter is added (issue 10.2); that Delay Feedback of 1.05 stays bounded over 60 seconds; that the wavefolder stays within ±1 (issue 10.4); render an impulse and check echo positions (issue 10.5); write the output to a WAV file and listen to it.

---

## 10. Code review: known issues

> **Status update.** Since the original review, issue **10.3** (no pitch shifting), issue **10.6** (unreliable sub-octave) and most of **10.8** (build portability) have been resolved; they are kept below, marked **RESOLVED**, as a record of what changed and why. Issues **10.1, 10.2, 10.4, 10.5, 10.7, 10.9** and the minor items in 10.10 are still open.

Findings are ordered roughly by impact. Items marked **[measured]** were confirmed by compiling `DemonicEngine` on its own and feeding it synthetic test signals (sine waves and impulses) — they are facts about the code, though real voices will vary in degree. Items marked **[by inspection]** come from reading the code.

### 10.1 Presets are immediately overwritten by the sliders — *high* **[measured]**

`main_desktop.cpp:43–62`. When the preset changes, `loadPreset()` copies the preset into the engine — and then, a few lines later in the same callback, all ten `set…()` calls overwrite those values with the slider atomics. Net effect: **the preset only changes the name.** (The atomics' defaults also don't match preset 0 — e.g. `guiRobotFreq` is 55 but DEMONIC GROWL says 45, `guiWavefold` is 1.5 vs 1.0.)

*Suggested fix:* when a preset is chosen, write the preset's values *into the GUI atomics* (UI thread) so the sliders jump to them and the callback applies them as usual. Add an accessor to the engine:

```cpp
// engine
const VocalPreset& getPreset(int i) const { return presets[i % NUM_PRESETS]; }

// main_desktop.cpp
void selectPreset(int idx) {
    const VocalPreset& p = demonic.getPreset(idx);
    guiDrive.store(p.driveDb);          guiSubBlend.store(p.subBlend);
    guiRobotBlend.store(p.robotBlend);  guiRobotFreq.store(p.robotFreq);
    guiWavefold.store(p.wavefoldDrive); guiDecimation.store(p.decimationFactor);
    guiDelayFB.store(p.delayFeedback);  guiDelayMix.store(p.delayMix);
    guiReverb.store(p.reverbMix);       guiMix.store(p.wetDryMix);
    guiActivePreset.store(idx);
}
```

Call `selectPreset` from the `P` key and the preset button, and once at startup (so initial values match preset 0).

### 10.2 Delay feedback above 1.0 grows without limit, and nothing limits the output — *high* **[measured]**

`demonic_engine.cpp:273`. The slider allows feedback up to 1.05. With feedback > 1, every echo is louder than the last, with no ceiling. In testing with a brief burst of input, peak output went from 0.33 to roughly **500,000** within 60 seconds. The audio hardware clips long before that, but the numbers keep growing, and if they ever reach infinity/NaN they would circulate in the loop forever (a restart would be needed). There is also no final limiter anywhere, so any stage can send values far outside ±1 straight to the speakers.

*Mitigation already available:* pressing **Bypass** flushes the loop (section 6.9), so a runaway can be silenced with one keypress. It is a workaround, not a fix: the loop can still run away again.

*Suggested fix:* bound the loop with a soft clipper where the signal is written back, and add a final safety clamp:

```cpp
feedbackDelayBuffer[feedbackDelayWriteHead] = std::tanh(wet + delayedSample * delayFeedback);
...
output[i] = std::clamp(output[i], -1.0f, 1.0f);   // or a soft limiter
```

### 10.3 `setFormantShift` and `formantCents` did nothing — **RESOLVED**

The engine stored a "formant shift" value that no code ever read, and the display's `PTCH:` label implied pitch shifting that did not exist. **Now:** `setPitchShift(cents)` drives a real pitch shifter (section 6.7), surfaced as the **Pitch** slider in the desktop app; `setFormantShift` remains as an alias that forwards to it. Note that the shifter moves pitch and spectral shape together, so it is a pitch shifter, not a formant shifter (hence the rename). The on-screen `PTCH:` label is still only a preset-name label.

### 10.4 The wavefolder escapes the ±1 range — *high* **[measured]**

`demonic_engine.cpp:241`:

```cpp
folded = std::abs(std::fmod(folded + 1.0f, 4.0f) - 2.0f) - 1.0f;
```

This is meant to be a triangle wave bounded by ±1. But `std::fmod` keeps the **sign of its first argument** (unlike Python's `%`, which always returns a non-negative result for a positive divisor). When `folded + 1` is negative — which happens for the negative half of the waveform at higher fold settings — the result is not folded but mirrored upward to values as high as about +5. Measured with a loud 220 Hz sine at Drive 12: output range was ≈ ±1.1 at fold 0, **−1.9 … +3.9** at fold 2.5, and **−5.6 … +7.2** at fold 5. It is also asymmetric, creating a large DC offset. Above fold 2.5 the blend weight `(1 − 0.4 × fold)` turns negative, which compounds it.

The MEGATRON (fold 3.0) and CYBER DEMON (fold 2.5) presets both sit in this range; the smoke test shows their peaks at about 4.0 and 4.2.

*Suggested fix:* use a true modulo that always returns a non-negative value, and limit the blend weight:

```cpp
float x = folded + 1.0f;
x -= 4.0f * std::floor(x / 4.0f);                 // Python-style x % 4.0
folded = std::fabs(x - 2.0f) - 1.0f;              // now always within [-1, 1]
float k = std::min(0.4f * wavefoldDrive, 1.0f);   // keep weights between 0 and 1
wet = wet * (1.0f - k) + folded * k;
```

### 10.5 Comments and constants disagree with actual timings; the "reverb" is an echo — *medium* **[measured]**

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

- `main_desktop.cpp:146,192` call `demonic.getCurrentPresetName()` from the UI thread while the audio thread may be changing `currentPresetIndex` (a plain `int`).
- `scopeBuffer` (a plain `float` array) is written by the audio thread and read by the UI thread with no synchronisation.

In practice both "work" on common hardware (a stale preset name or a slightly torn waveform), but formally they are data races. *Fix:* make `currentPresetIndex` an `std::atomic<int>`, or have the UI look up the name from `guiActivePreset` through a static name table; accept or lock-free-queue the scope data.

### 10.8 Build portability — mostly **RESOLVED**

**Fixed:** macOS `-framework` flags are now conditional on `if(APPLE)`; PortAudio is found via `pkg-config`, then `/opt/homebrew` and `/usr/local`, then the bundled copy, so Apple Silicon works without edits; the unneeded `ASM` language and the empty `dsp_pipeline.cpp` were removed from the build; the engine is a reusable library with tests registered in `ctest`; and the engine no longer uses `M_PI` (it uses a `float` constant, which also helps on the Pico, see section 11). The engine also gained an explicit `#include <iterator>`: it used `std::begin`/`std::end` and had been getting the header only by accident through an unused `#include <vector>`.

**Still open:** the CMake file and desktop app have not been built end-to-end on any platform as part of this change (see the verification note in section 7); Windows is untested; and the `portaudio/` folder remains git-ignored, so a fresh clone relies on an installed PortAudio.

### 10.9 Freeze behaviour — *low* **[by inspection]**

Freeze captures the *oldest* 4,096 samples in the 500 ms echo buffer (audio from roughly 410–500 ms ago), not the most recent audio, and loops them with no crossfade at the loop point, producing a ~11 Hz flutter and a click. A better design captures the *most recent* samples and crossfades the loop seam; a true "spectral" freeze would use an FFT and hold magnitudes while randomising phases.

### 10.10 Minor items

- **No parameter smoothing.** Setter values are applied once per 64-sample block and take effect instantly, so fast slider moves can produce small clicks ("zipper noise"). Smooth each parameter per-sample with a one-pole filter.
- **Integer overflow.** `decimationCounter++` (`int`) will overflow after about 13.5 hours of continuous running (signed overflow is undefined behaviour in C++). Wrap it: `decimationCounter = (decimationCounter + 1) % decimationStep`.
- **Initial slider defaults** don't equal preset 0 (see 10.1). The Pitch slider is not part of presets.
- **The noise gate has no hysteresis or release**, so quiet speech near the threshold chatters on and off.
- **Mono only**, hard-wired to the default devices; no device selection or error recovery if a device disappears.
- **Empty files** `dsp_pipeline.hpp/.cpp` are placeholders that nothing uses. Delete them or fill them in so newcomers aren't misled.
- **No top-level README** in the repository yet (the guides in `docs/` could be linked from one).

---

## 11. Toward the Raspberry Pi Pico

The hardware target is an RP2040 (dual Cortex-M0+ at 133 MHz, 264 KB RAM). The first step of the port is not audio I/O but a go/no-go question: **can the RP2040 run the engine in real time at all?** `pico/main_pico.cpp` answers it by running the engine on synthetic input on the device and reporting how much of the real-time budget it uses.

### The budget

At 44.1 kHz a 64-sample block lasts 64 / 44,100 ≈ **1,451 µs**. At 133 MHz that is about **193,000 CPU cycles per block, or about 3,000 cycles per sample**, to run every stage, plus the audio driver, display, and buttons. The benchmark prints microseconds per block and the percentage of 1,451 µs used. Rough guide: under 50 % is comfortable, 50–80 % is tight, and over 100 % cannot work without optimisation.

### Running the benchmark

The instructions are in the header of `pico/CMakeLists.txt`. In short:

1. Install the Arm toolchain and clone the [Pico SDK](https://github.com/raspberrypi/pico-sdk) (with submodules); set `PICO_SDK_PATH`.
2. Copy the SDK's `external/pico_sdk_import.cmake` into `pico/`.
3. `cmake -S pico -B pico/build -DCMAKE_BUILD_TYPE=Release && cmake --build pico/build`
4. Hold BOOTSEL, plug the Pico in, and copy `pico/build/vocal_pico_bench.uf2` to the drive that appears.
5. Open the USB serial port (macOS: `screen /dev/tty.usbmodem* 115200`).

It repeats a table of results every few seconds covering: a baseline with optional effects off, baseline plus pitch shift, each of the three presets, and a worst case (MEGATRON + pitch shift + freeze).

> **Not yet run on hardware.** The benchmark's logic was compiled and run on a desktop (with a stand-in for the SDK's timing functions) to confirm it works, but that says nothing about Pico speed, since a desktop CPU is hundreds of times faster. The Pico SDK build files were written from the SDK's documented structure and have not been built here. Treat the first on-device run as the real measurement.

### Things to expect and what to try if it is too slow

- **No hardware floating point.** The M0+ has no FPU; every `float` operation runs in software. The engine does `sinf` twice per sample (chorus LFO, ring modulator), `tanhf` once, plus `floorf`, `fmodf`, `roundf` and, while the pitch shifter is crossfading, `cosf`. These are the first suspects.
- **Already done:** `M_PI` is a 64-bit `double`, so expressions such as `2.0f * M_PI * …` silently forced slow double-precision maths. The engine now uses a `float` constant (`kTwoPi`) instead.
- **Candidate optimisations**, in rough order of expected payoff (measure after each change): replace the two per-sample `sin` calls with lookup tables or a recurrence oscillator; replace `tanh` with a cheap polynomial or lookup table; use a lookup table for the pitch crossfade `cos`; drop to a 22.05 kHz sample rate (halves the per-second work); use the second core for part of the chain; move hot paths to fixed-point integer arithmetic. The Pico SDK also ships optimised software float routines, which the benchmark already benefits from.
- **Memory.** `sizeof(DemonicEngine)` is **186,824 bytes (about 182 KB, 69 % of the 264 KB RAM)**, before the SDK's own needs, stacks, and audio I/O buffers. It must be a global or static object (`main_pico.cpp` does this), never a local variable. If RAM gets tight: shorten the delay and reverb buffers, or store them as 16-bit integers.
- **I/O still to design.** An ADC or I²S codec for audio in and out, an SSD1306-style 128×64 OLED (the desktop mock is already that size), and buttons or pots mapped to the same controls the GUI atomics represent.
- **Structure.** Because the engine has no PortAudio or raylib dependency, the finished firmware can create a `DemonicEngine`, read controls from pots and buttons, and call `processBlock` from a DMA or I²S interrupt, the same shape as the desktop app.

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
