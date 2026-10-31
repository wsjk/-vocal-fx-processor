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
├── CMakeLists.txt              Build recipe (macOS-only as written)
├── include/
│   ├── demonic_engine.hpp      The DSP engine's "interface": class + preset struct   [implemented]
│   └── dsp_pipeline.hpp        EMPTY (0 bytes)
├── src/
│   ├── demonic_engine.cpp      The DSP engine's code: all ten effects live here      [implemented]
│   └── dsp_pipeline.cpp        EMPTY (0 bytes) — but still listed in CMakeLists.txt
├── desktop/
│   └── main_desktop.cpp        Desktop app: audio I/O, window, sliders, OLED mock   [implemented]
├── pico/
│   └── main_pico.cpp           EMPTY (0 bytes) — hardware build not started
├── portaudio/                  Vendored PortAudio source. NOT used by the build; also in .gitignore
└── .gitignore                  Ignores build/ and portaudio/
```

Only **three files contain real code** — about 450 lines in total. Everything interesting is in `demonic_engine.cpp` (the sound) and `main_desktop.cpp` (the app around it).

---

## 2. Audio concepts in five minutes

**Samples.** Digital audio is a long list of numbers. Each number (a *sample*) is the speaker-cone position at one instant. Here each is a `float` between roughly **−1.0 and +1.0**; ±1.0 is "full scale". Values outside that range get clipped (flattened, harshly) by the sound hardware.

**Sample rate.** How many samples per second. This project uses **44,100** (CD quality). One sample lasts about 22.7 µs.

**Blocks (buffers).** Software never handles one sample at a time through the sound card; it gets a *block* — here **64 samples**, which lasts 64 / 44,100 ≈ **1.45 ms**. Smaller blocks mean less delay but less time to compute each one.

**The real-time deadline.** The sound card wants a new block every 1.45 ms, forever. If your code is late, the audio glitches. So the code that runs per block (the *audio callback*) follows strict rules: no printing, no file access, no memory allocation, no waiting on locks. Think of it like a game's frame deadline, but 700 times per second.

**Decibels.** Loudness in dB is logarithmic. To convert gain in dB to a multiplier: `multiplier = 10 ** (dB / 20)`. So +12 dB ≈ ×4, and +6 dB ≈ ×2. (`processBlock` line 77.)

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
    : sampleRate(sr), formantCents(-500.0f), currentPresetIndex(0), ... {
    // body runs after the list
}
```

The part after the `:` sets each member's starting value *before* the body runs — roughly the first lines of `__init__`. One gotcha: members are really initialised in the order they are **declared in the header**, not the order you write them in this list. This project's list is in a different order from the header; compilers warn about it (`-Wreorder`). It is harmless here because no member's initial value depends on another's, but keep it in mind.

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

Every ~1.45 ms, PortAudio calls `audioCallback` (`main_desktop.cpp:33`), which:

1. Checks whether the preset changed; if so, calls `demonic.loadPreset(...)` (lines 41–46).
2. Copies every slider value from the atomics into the engine via setters (lines 48–58).
3. Measures the input peak for the level meter and calls `demonic.processBlock(in, out, 64)` (lines 60–65).
4. Copies the output into the oscilloscope ring buffer (lines 67–72) and publishes the mic peak (line 77).
5. Returns `paContinue` ("keep going").

### 4.3 Design decisions worth knowing

- **The engine knows nothing about PortAudio or raylib.** `DemonicEngine` takes in a float array and fills a float array. That is why it can be tested headlessly (section 9) and is the part meant to be reused on the Pico.
- **Everything is allocated up front.** No `new`, no `malloc`, no `std::vector` growth in the audio path — required for real-time safety and for microcontrollers.
- **Control values are copied in once per block**, not per sample. Cheap, but it means parameter changes are "steppy" (see section 10.10).
- **The engine is a single global object** (`DemonicEngine demonic(SAMPLE_RATE);`, `main_desktop.cpp:13`). Both threads can see it, but by convention only the audio thread should touch its state (the UI thread breaks this convention in two small places — section 10.7).
- **Memory footprint:** `sizeof(DemonicEngine)` is about **174 KB** (178,592 bytes measured), almost all of it the four buffers.

---

## 5. Walkthrough: the desktop app (`desktop/main_desktop.cpp`)

### 5.1 Globals (lines 10–31)

```cpp
#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 64
DemonicEngine demonic(SAMPLE_RATE);

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

### 5.2 The audio callback (lines 33–79)

The signature is dictated by PortAudio; you cannot change it. The parts you care about are `inputBuffer`, `outputBuffer`, and `framesPerBuffer`. The first thing it does is cast the generic `void*` buffers to `float*` (since the stream was opened with `paFloat32`):

```cpp
const float* in = static_cast<const float*>(inputBuffer);
float* out = static_cast<float*>(outputBuffer);
```

The preset check uses a `static` local, `lastPreset`, which persists across calls so the callback can detect a *change*. Then every parameter is pushed into the engine, then the block is processed.

### 5.3 `main()` setup (lines 81–102)

1. `Pa_Initialize()` starts PortAudio.
2. `Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, SAMPLE_RATE, FRAMES_PER_BUFFER, audioCallback, nullptr)` — opens the default devices with **1 input channel, 1 output channel (mono), 32-bit float samples, 44.1 kHz, 64-frame blocks**, and registers `audioCallback`. (`&stream` passes the address of `stream` so the function can fill it in — an "output parameter".)
3. `Pa_StartStream(stream)` — from here on, the callback is being called.
4. `InitWindow(480, 630, ...)` creates the window; `SetTargetFPS(60)` limits the loop to 60 frames/second.
5. `LoadRenderTexture(128, 64)` creates an off-screen 128×64 image — the fake OLED.

Error handling is by return code, not exceptions: functions return `paNoError` on success and `main` prints and exits otherwise.

### 5.4 The UI loop (lines 104–213)

Each frame:

1. **Keys:** `P` cycles `guiActivePreset`; `F` toggles `guiSpectralFreeze`.
2. **Snapshot:** reads every atomic into a local variable so the whole frame is drawn from one consistent set of values.
3. **Draw the OLED** (lines 127–152) into the 128×64 off-screen texture: border, preset name, optional `[FREEZE ON]`, the waveform (127 line segments between consecutive `scopeBuffer` samples, clamped to stay inside the display), and the mic-level bar.
4. **Draw the window** (lines 154–212): scales the OLED texture up 2× and blits it (`DrawTexturePro`); draws the two buttons with hit-testing by `CheckCollisionPointRec`; draws ten sliders with the `drawSlider` lambda.

This style is called **immediate-mode UI**: there are no button or slider objects. Every frame the code asks "is the mouse over this rectangle and pressed?", acts on the answer, and draws it. The `drawSlider` lambda (line 189) both draws the bar and — if the mouse is held down inside it — computes the new value from the mouse x-position and `.store()`s it to the atomic.

### 5.5 Shutdown (lines 215–220)

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
| `setSpectralFreeze(bool)` / `isSpectralFrozen()` | Freeze control |
| `setFormantShift(float)` | Declared and stored, **but never used** (see 10.3) |
| `loadPreset(int)`, `getCurrentPresetName()`, `getPresetCount()` | Preset handling |

**Private state** falls into three groups:

- **Parameters** (`driveDb`, `subBlend`, `robotBlend`, …) — set from outside.
- **Per-stage memory** (`lastInput`, `subToggle`, `subFilterState`, `lowpassState`, `ringModPhase`, `heldSample`, `decimationCounter`, `dcInPrev`, `dcOutPrev`, `inputEnvelope`) — what each effect must remember from one sample to the next.
- **Buffers** — four fixed arrays:

| Buffer | Size | Duration at 44.1 kHz | Used by |
|---|---|---|---|
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

### 6.4 The constructor and presets (`demonic_engine.cpp:5–25`)

Sets all members, zero-fills the four buffers with `std::fill`, defines three presets using brace-initialisation (`presets[0] = {"DEMONIC GROWL", 11.0f, ...};` — values in the same order as the struct fields), and calls `loadPreset(0)`.

`loadPreset(i)` (lines 54–66) copies the ten fields from `presets[i % 3]` into the live parameter members. The `% NUM_PRESETS` makes cycling wrap around.

### 6.5 `processBlock` stage by stage (`demonic_engine.cpp:76–190`)

Before the loop, two values are computed once per block:

```cpp
float gainMultiplier = std::pow(10.0f, driveDb / 20.0f);        // dB → linear
int decimationStep   = std::max(1, static_cast<int>(decimationFactor));
```

Then a `for` loop handles each sample `input[i]` through ten stages, in this order:

| # | Lines | Stage | What the code does |
|---|---|---|---|
| gate | 84–85 | **Noise gate** | `activeSignal = abs(dry) > 0.02 ? dry : 0`. Anything quieter than 0.02 (−34 dBFS) is zeroed. Also updates `inputEnvelope`, a smoothed loudness meter (a one-pole filter on `abs(activeSignal)`). |
| 1 | 87–102 | **Chorus doubler** | Writes the gated signal into `delayLine`. A 0.5 Hz sine LFO modulates a read position around 441 samples (10 ms) ± 66 samples. It reads between two neighbouring samples with **linear interpolation** (`a*(1-frac) + b*frac`) because the position is fractional. Adds 30% of that copy to the voice. |
| 2 | 104–116 | **Sub-octave** | Flips a boolean `subToggle` at zero-crossings of the signal, low-passes that ±1 square wave (`subFilterState`, a one-pole at about 290 Hz), scales it by the signal's amplitude and `subBlend`, and adds it. |
| 3 | 118–126 | **Ring modulation** | An oscillator at `robotFreq + 40 × inputEnvelope` Hz (`ringModPhase` is the oscillator's angle in radians, wrapped at 2π). `wet` is crossfaded between the clean signal and `wet × sin(phase) × 1.5` by `robotBlend`. |
| 4 | 128–135 | **Drive and wavefold** | `wet = tanh(wet × gainMultiplier)` is the saturation. If `wavefoldDrive > 0.1`, a triangle fold is applied and mixed in at weight `0.4 × wavefoldDrive`. |
| 5 | 137–145 | **Decimation** | A counter selects every Nth sample to "hold" (`heldSample`); in-between samples repeat it. Then `round(wet × 32) / 32` quantises to about 6 bits. |
| 6 | 147–149 | **Tone filter** | One-pole low-pass, `a = 0.35` → roughly 3 kHz cutoff at 44.1 kHz. |
| 7 | 151–158 | **Spectral freeze** | If active, reads `freezeBuffer` in a loop and mixes it 70/30 with live signal. |
| 8 | 160–168 | **Feedback delay** | Reads from `feedbackDelayBuffer`, writes `wet + delayed × delayFeedback` back in, and crossfades the output by `delayMix`. |
| 9 | 170–180 | **"Catacomb" reverb** | Two tap reads from `reverbBuffer` (weights 0.6 and 0.4), written back with 0.55 feedback; crossfaded by `reverbMix`. |
| 10 | 182–186 | **DC blocker** | `y = x − x_prev + 0.995 × y_prev`. A classic high-pass at a few Hz. |
| mix | 188 | **Wet/dry** | `output[i] = dry × (1 − mix) + wet × mix`. Note `dry` is the *ungated* input. |

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

`setSpectralFreeze(true)` (lines 39–48) — only on the *off → on* transition — copies 4,096 samples out of `feedbackDelayBuffer` starting at the write head into `freezeBuffer` and resets the read position. Because the write head points at the **oldest** data, this captures audio from roughly 0.41–0.5 s ago, not the most recent audio. Playback then loops those 4,096 samples (a ~93 ms loop, ≈ 10.8 repetitions per second).

---

## 7. The build system (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.13)
project(vocal_fx_processor C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
```
Declares the project, the languages (`ASM` is unnecessary for the desktop build — a leftover from the Pico SDK style), and C++17.

```cmake
include(FetchContent)
FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz)
FetchContent_MakeAvailable(raylib)
```
Downloads and builds raylib 5.0 at configure time (needs internet). Think of it as `pip install`, but compiled from source.

```cmake
add_executable(vocal_desktop desktop/main_desktop.cpp src/demonic_engine.cpp src/dsp_pipeline.cpp)
```
The program and the three `.cpp` files that make it up. (`dsp_pipeline.cpp` is empty and could be removed from the list.)

```cmake
target_include_directories(vocal_desktop PRIVATE include /usr/local/include)
target_link_directories(vocal_desktop PRIVATE /usr/local/lib)
target_link_libraries(vocal_desktop PRIVATE portaudio raylib "-framework CoreAudio" ... )
```
Where to look for headers (`include/` for ours; `/usr/local/include` for PortAudio) and libraries, and what to link. The `-framework …` entries are **macOS system libraries** — on Linux or Windows these would break the build.

**Notes**

- PortAudio comes from the *installed* system copy (via `brew install portaudio`), **not** from the `portaudio/` folder in the repo, which is also git-ignored.
- `/usr/local` is where Homebrew installs on Intel Macs; Apple Silicon uses `/opt/homebrew`.
- There is no `pico/` target and no tests target yet.

Typical build commands:

```bash
cmake -S . -B build          # configure (downloads raylib the first time)
cmake --build build          # compile + link
./build/vocal_desktop        # run
```

A more portable approach is outlined in section 10.8.

---

## 8. How to extend it

### 8.1 Add a new parameter (worked example: tone-filter brightness)

Right now the tone filter's coefficient `0.35` is hard-coded (`demonic_engine.cpp:148`). Here is how to make it adjustable.

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

// in processBlock, replace line 148:
lowpassState = lowpassState + toneCoeff * (wet - lowpassState);
```

**3. App — add the mailbox, copy it in, add a slider** (`desktop/main_desktop.cpp`):

```cpp
std::atomic<float> guiTone(0.35f);                         // with the other globals

demonic.setTone(guiTone.load());                           // in audioCallback, with the other setters

drawSlider("Tone", toneVal, 0.05f, 0.9f, guiTone);         // in the UI loop, plus: float toneVal = guiTone.load();
```

Also increase `screenHeight` (line 98) — each slider takes 34 pixels.

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

Because `DemonicEngine` has no dependency on audio hardware, you can drive it from a plain program. A ready-made smoke test is provided at `tests/engine_smoke_test.cpp`. It checks that silence in gives silence out, and that each preset produces finite (non-NaN) output and reports its peak level.

```bash
g++ -std=c++17 -Wall -Wextra -Iinclude tests/engine_smoke_test.cpp src/demonic_engine.cpp -o smoke
./smoke
```

Expected output on the current code:

```
[PASS] silence in -> silence out
[PASS] preset 0 (DEMONIC GROWL): finite=yes, peak=0.61
[PASS] preset 1 (MEGATRON): finite=yes, peak=4.02  (WARNING: exceeds full scale)
[PASS] preset 2 (CYBER DEMON): finite=yes, peak=3.59  (WARNING: exceeds full scale)
```

The warnings document a real problem (issue 10.4). `-Wall -Wextra` will also print the harmless `-Wreorder` warning described in section 3.5.

**Ideas for further tests:** assert that peak output stays ≤ 1.0 once a limiter is added; assert that Delay Feedback of 1.05 stays bounded over 60 seconds of input; render an impulse and check echo positions; or write the output to a WAV file and listen to it.

---

## 10. Code review: known issues

Findings are ordered roughly by impact. Items marked **[measured]** were confirmed by compiling `DemonicEngine` on its own and feeding it synthetic test signals (sine waves and impulses) — they are facts about the code, though real voices will vary in degree. Items marked **[by inspection]** come from reading the code.

### 10.1 Presets are immediately overwritten by the sliders — *high* **[measured]**

`main_desktop.cpp:41–58`. When the preset changes, `loadPreset()` copies the preset into the engine — and then, a few lines later in the same callback, all ten `set…()` calls overwrite those values with the slider atomics. Net effect: **the preset only changes the name.** (The atomics' defaults also don't match preset 0 — e.g. `guiRobotFreq` is 55 but DEMONIC GROWL says 45, `guiWavefold` is 1.5 vs 1.0.)

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

`demonic_engine.cpp:165`. The slider allows feedback up to 1.05. With feedback > 1, every echo is louder than the last, with no ceiling. In testing with a brief burst of input, peak output went from 0.33 to roughly **500,000** within 60 seconds. The audio hardware clips long before that, but the numbers keep growing, and if they ever reach infinity/NaN they would circulate in the loop forever (a restart would be needed). There is also no final limiter anywhere, so any stage can send values far outside ±1 straight to the speakers.

*Suggested fix:* bound the loop with a soft clipper where the signal is written back, and add a final safety clamp:

```cpp
feedbackDelayBuffer[feedbackDelayWriteHead] = std::tanh(wet + delayedSample * delayFeedback);
...
output[i] = std::clamp(output[i], -1.0f, 1.0f);   // or a soft limiter
```

### 10.3 `setFormantShift` and `formantCents` do nothing — *medium* **[by inspection]**

`formantCents` is set in the constructor (−500) and by `setFormantShift`, but never read in `processBlock`. The OLED label `PTCH:` is also not a pitch display. Either implement a pitch/formant shifter or remove the member, setter, and the user-facing mention. (Implementing real pitch shifting is a substantial project; granular and phase-vocoder approaches are the usual starting points.)

### 10.4 The wavefolder escapes the ±1 range — *high* **[measured]**

`demonic_engine.cpp:133`:

```cpp
folded = std::abs(std::fmod(folded + 1.0f, 4.0f) - 2.0f) - 1.0f;
```

This is meant to be a triangle wave bounded by ±1. But `std::fmod` keeps the **sign of its first argument** (unlike Python's `%`, which always returns a non-negative result for a positive divisor). When `folded + 1` is negative — which happens for the negative half of the waveform at higher fold settings — the result is not folded but mirrored upward to values as high as about +5. Measured with a loud 220 Hz sine at Drive 12: output range was ≈ ±1.1 at fold 0, **−1.9 … +3.9** at fold 2.5, and **−5.6 … +7.2** at fold 5. It is also asymmetric, creating a large DC offset. Above fold 2.5 the blend weight `(1 − 0.4 × fold)` turns negative, which compounds it.

The MEGATRON (fold 3.0) and CYBER DEMON (fold 2.5) presets both sit in this range; the smoke test shows their peaks at about 4.0 and 3.6.

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
| Echo tap (line 161) | offset 13,230 in a 22,050 buffer | "~300 ms" | **200 ms** |
| Reverb tap 1 (line 171) | offset 4,410 in 16,384 | 100 ms | **121.5 ms** |
| Reverb tap 2 (line 172) | offset 11,025 in 16,384 | 250 ms | **271.5 ms** |

The "reverb" is two feedback echoes (at about 122 and 272 ms, with a third at 393 ms from the loop), not a diffuse reverb. For a real reverb, use several comb and all-pass filters of mutually-prime lengths (e.g. a Schroeder or Freeverb design).

*Fix:* read from `(head + size − delaySamples) % size` so you specify the delay directly, and update the comments.

### 10.6 The sub-octave detector is unreliable — *medium* **[measured]**

`demonic_engine.cpp:104–116`. Two problems:

1. It toggles on **both** rising and falling zero crossings. A classic octave divider toggles on one direction only, since two toggles per cycle yields a square wave at the *same* frequency as the input.
2. It requires `|widenedVoice| > 0.02` at the crossing sample, but the noise gate (stage 0) has already zeroed everything within ±0.02 of zero. The signal therefore jumps across zero in a single sample and the crossing is missed or detected erratically.

In testing with sine inputs, toggles per second were wildly inconsistent — for example, a 110 Hz sine produced 3 toggles per second at *every* amplitude from 0.1 to 0.9, while 220 Hz produced between 33 and 116 depending on level. A clean octave-down would need about 110 toggles/s for a 220 Hz tone.

*Suggested approach:* detect crossings on a low-passed version of the signal, toggle on rising edges only, add hysteresis (e.g. require the signal to exceed +0.02 and fall below −0.02 before arming the next crossing), and decouple detection from the gated signal.

### 10.7 Cross-thread data not protected — *low* **[by inspection]**

- `main_desktop.cpp:131,175` call `demonic.getCurrentPresetName()` from the UI thread while the audio thread may be changing `currentPresetIndex` (a plain `int`).
- `scopeBuffer` (a plain `float` array) is written by the audio thread and read by the UI thread with no synchronisation.

In practice both "work" on common hardware (a stale preset name or a slightly torn waveform), but formally they are data races. *Fix:* make `currentPresetIndex` an `std::atomic<int>`, or have the UI look up the name from `guiActivePreset` through a static name table; accept or lock-free-queue the scope data.

### 10.8 Build portability — *medium* **[by inspection]**

- macOS-only `-framework …` link flags.
- Hard-coded `/usr/local/{include,lib}` — wrong on Apple Silicon (`/opt/homebrew`).
- PortAudio comes from the system install; the repo's `portaudio/` copy (7 MB, 400 files) is git-ignored and unused.
- `ASM` language enabled needlessly; `dsp_pipeline.cpp` is empty but compiled.
- `M_PI` is not guaranteed by standard C++ on MSVC without `_USE_MATH_DEFINES`.

*Suggested approach:* use `find_package(PortAudio)` or `pkg_check_modules(PORTAUDIO portaudio-2.0)`, or `add_subdirectory(portaudio)` / `FetchContent` for PortAudio, then add platform branches:

```cmake
if(APPLE)
    target_link_libraries(vocal_desktop PRIVATE "-framework CoreAudio" ... )
endif()
```

and define a constant `kPi` instead of relying on `M_PI`.

### 10.9 Freeze behaviour — *low* **[by inspection]**

Freeze captures the *oldest* 4,096 samples in the 500 ms echo buffer (audio from roughly 410–500 ms ago), not the most recent audio, and loops them with no crossfade at the loop point, producing a ~11 Hz flutter and a click. A better design captures the *most recent* samples and crossfades the loop seam; a true "spectral" freeze would use an FFT and hold magnitudes while randomising phases.

### 10.10 Minor items

- **No parameter smoothing.** Setter values are applied once per 64-sample block and take effect instantly, so fast slider moves can produce small clicks ("zipper noise"). Smooth each parameter per-sample with a one-pole filter.
- **Integer overflow.** `decimationCounter++` (`int`) will overflow after about 13.5 hours of continuous running (signed overflow is undefined behaviour in C++). Wrap it: `decimationCounter = (decimationCounter + 1) % decimationStep`.
- **Initial slider defaults** don't equal preset 0 (see 10.1).
- **The noise gate has no hysteresis or release**, so quiet speech near the threshold chatters on and off.
- **Mono only**, hard-wired to the default devices; no device selection or error recovery if a device disappears.
- **Empty files** (`dsp_pipeline.hpp/.cpp`, `main_pico.cpp`) are placeholders. Delete them or fill them in so newcomers aren't misled.
- **No README and no tests directory** in the repository yet.

---

## 11. Toward the Raspberry Pi Pico

The hardware target is an RP2040 (dual Cortex-M0+ at 133 MHz, 264 KB RAM). Points to consider before porting `DemonicEngine`:

- **No hardware floating point.** The M0+ has no FPU; every `float` operation runs in software. The engine does `sinf` ×2, `tanhf`, and `fmodf` **per sample**, 44,100 times per second. This may be too slow, so benchmark early; consider lookup tables, integer/fixed-point math, a lower sample rate, or simplified effects.
- **Accidental double-precision math.** `M_PI` is a 64-bit `double`, so expressions like `(2.0f * M_PI * 0.5f) / sampleRate` (lines 90, 91, 120, 123) are computed in double precision and then narrowed. On a chip without an FPU, double math is much slower. Define a `constexpr float kPi = 3.14159265f;` and use it.
- **Memory.** The engine holds about 174 KB of state, which is two-thirds of the chip's RAM before the SDK, stacks, and audio I/O buffers. Reducing buffers (e.g., shorter reverb and delay), or storing them as 16-bit integers, may be needed.
- **I/O.** The Pico needs an ADC/I²S codec for audio in/out, an SSD1306-style 128×64 OLED driver (the desktop OLED mock is already sized to match), and buttons/pots mapped to the same controls the GUI atomics represent.
- **Structure.** Because the engine has no PortAudio/raylib dependencies, `main_pico.cpp` can create a `DemonicEngine`, read control values from pots and buttons, and call `processBlock` from a DMA or I²S interrupt — the same shape as the desktop app. Then `dsp_pipeline.*` could become the place where shared glue code between the two lives, or be deleted.

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
