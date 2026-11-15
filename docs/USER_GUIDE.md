# Vocal FX Processor — User Guide

The Vocal FX Processor turns your microphone into a real-time "demonic" vocal effects pedal. You talk or sing into a mic, and what comes out of your headphones is a growling, robotic, glitchy, echoing version of your voice.

The desktop app draws the pedal itself: an enclosure with a small OLED screen, eleven **encoders** (knobs with a ring of lights showing their value), three **footswitches** (Preset, Freeze and Bypass), and the jacks a real unit would have. You play it with the mouse and keyboard. Everything runs live with very little delay.

> **Status note.** This guide describes the code as it exists in the repository today. A few things behave differently from what you might expect. They are collected in [Known limitations](#known-limitations) so you are not surprised.

---

## Contents

1. [What you need](#what-you-need)
2. [Install and run](#install-and-run)
3. [Safety first: volume and headphones](#safety-first-volume-and-headphones)
4. [A tour of the pedal](#a-tour-of-the-pedal)
5. [Controls](#controls)
6. [How your voice is processed](#how-your-voice-is-processed)
7. [Parameter reference](#parameter-reference)
8. [Presets](#presets)
9. [Spectral Freeze](#spectral-freeze)
10. [Bypass: effects off in one press](#bypass-effects-off-in-one-press)
11. [Sound recipes](#sound-recipes)
12. [Troubleshooting](#troubleshooting)
13. [Known limitations](#known-limitations)

---

## What you need

| Requirement | Details |
|---|---|
| Computer | **macOS** is the supported platform. The build file no longer hard-codes Mac-only paths, and the sound engine and tests are confirmed to build and pass on Linux, but the desktop app itself has not been tested on Linux or Windows. |
| Tools | [CMake](https://cmake.org) 3.14 or newer and a C++17 compiler (Xcode Command Line Tools). |
| Audio library | [PortAudio](http://www.portaudio.com), installed on your system (see below). The build finds it on both Intel and Apple Silicon Macs. |
| Internet | Needed once, the first time you build, so CMake can download the raylib graphics library. |
| Hardware | A microphone and **headphones** (strongly recommended — see [Safety first](#safety-first-volume-and-headphones)). |

---

## Install and run

1. **Install the tools** (using [Homebrew](https://brew.sh)):

   ```bash
   xcode-select --install        # skip if you already have a compiler
   brew install cmake portaudio pkg-config
   ```

2. **Get the code** and open a terminal in the project folder (`vocal-fx-processor/`).

3. **Build:**

   ```bash
   cmake -S . -B build
   cmake --build build
   ```

4. **Run:**

   ```bash
   ./build/vocal_desktop
   ```

   *(Optional)* To check that the sound engine works before you plug in a microphone, run the built-in tests: `cd build && ctest --output-on-failure` (then `cd ..`).

5. **Allow microphone access.** The first time you run it, macOS asks whether your terminal (or IDE) may use the microphone. Choose **Allow**. If you miss the prompt, enable it under *System Settings → Privacy & Security → Microphone*.

6. **Quit** by closing the window or pressing `Esc`.

### If the build cannot find PortAudio

The build looks for PortAudio in the standard places on both Intel and Apple Silicon Macs (Homebrew's `/usr/local` and `/opt/homebrew`), so no manual edits should be needed. If configuration stops with "PortAudio was not found", run `brew install portaudio pkg-config` and configure again (delete the `build` folder first if it keeps failing: `rm -rf build`).

> The `portaudio/` folder you may see inside the project is a copy of the PortAudio source. It is used only as a last resort when no installed PortAudio is found.

---

## Safety first: volume and headphones

- **Use headphones.** The app plays your processed voice straight back out. With speakers, the mic picks the sound up again and you get an ear-piercing howl (acoustic feedback).
- **Know your panic button.** Press **`B`** or **Space** (or click the BYPASS footswitch) to bypass all effects instantly. It also flushes any runaway echo. See [Bypass](#bypass-effects-off-in-one-press).
- **Start with the system volume low.** There is no master volume control. The output is capped at full scale, but high **FOLD** (wavefolder) values and **DELAY FB above 100%** push it right up to that cap, which sounds harshly clipped and can be very loud.
- **Wired headphones give the lowest delay.** Bluetooth adds noticeable lag on top of the app's own tiny delay.

---

## A tour of the pedal

```
┌───────────────────────────────────────────────────────────────────────┐
│ DEMONIC (●)                       ◎ OUTPUT    ▭ USB-C    ◎ MIC IN       │
│ ┌────────────────────┐  ┌─ VOICE ──────────────────────────────────┐  │
│ │ 1 DEMONIC GROWL    │  │   (PITCH)      (DRIVE)      (SUB OCT)    │  │
│ │  ~~~ waveform ~~~  │  │   +0.0 st      11.0 dB        85%        │  │
│ │ ████ input level   │  └──────────────────────────────────────────┘  │
│ └────────────────────┘                                                │
│ ┌─ MANGLE ─────────────────────────┐  ┌─ SPACE ─────────────────────┐ │
│ │ ROBOT  CLANK RATE  FOLD  DECIMATE │  │ DELAY FB  DELAY MIX  REVERB │ │
│ │                                   │  │ WET/DRY                     │ │
│ └───────────────────────────────────┘  └─────────────────────────────┘ │
│         ●                      ●                      ●               │
│      (PRESET)              (FREEZE)               (BYPASS)            │
└───────────────────────────────────────────────────────────────────────┘
```

**The OLED display** (top left):

- **Top line**: the preset number and name. When you turn an encoder, it shows that encoder's name and value for a moment instead (for example `DRIVE 9.5 dB`).
- **`FREEZE`**: appears in red under the top line when Spectral Freeze is active. The waveform turns red too.
- **`BYPASS`**: appears in red when Bypass is on. The waveform turns grey.
- **Waveform**: a very short snapshot (about 3 milliseconds) of the sound *coming out* of the processor. It scrolls quickly and mostly serves as a "yes, it's alive" indicator.
- **Bar along the bottom**: your **microphone input level**. If this bar never moves when you talk, the app is not hearing your mic (see [Troubleshooting](#troubleshooting)).

**The encoders** sit in three panels: **VOICE** (pitch, drive, sub-octave), **MANGLE** (robot clank, its rate, wavefolder, decimation) and **SPACE** (echo, reverb, wet/dry; all four in one row on screen). The orange ring around each encoder shows its value: empty at the minimum, a full arc at the maximum. **PITCH** is different: its ring grows from the top (0, off) to the left for lower and to the right for higher. The current value is printed under each encoder. The **DELAY FB** ring turns red above 100%, where the echo grows without limit.

**The footswitches** and their lights:

| Footswitch | Light |
|---|---|
| **PRESET**: load the next preset | Blue flash when a preset loads |
| **FREEZE**: Spectral Freeze on/off | Red while frozen |
| **BYPASS**: effects on/off | **Green while the effects are on**, dark when bypassed |

The jacks, USB port and red power light along the top are decoration: the app always uses your computer's default microphone and output.

---

## Controls

| Action | How |
|---|---|
| Turn an encoder | **Drag** it: grab the ring and move around it (clockwise = up), or grab the middle and drag up/down. Or **scroll** over it (one notch = one step). |
| Load the next preset | Press **`P`**, or click the PRESET footswitch |
| Toggle Spectral Freeze | Press **`F`**, or click the FREEZE footswitch (does nothing while bypassed) |
| **Bypass: effects off / on** | Press **`B`** or **`Space`**, or click the BYPASS footswitch |
| Quit | `Esc` or close the window |

Encoder tips:

- Scrolling moves in clean steps: one semitone for PITCH, 0.5 dB for DRIVE, 5 Hz for CLANK RATE, 5% for most amounts, 2.5% for DELAY FB. Dragging is smooth, for anything in between.
- The encoders stop at the ends of their range.
- **PITCH** snaps to exactly 0 when you drag close to the top, which switches the pitch shifter fully off.
- Changes are heard immediately, even mid-word.

---

## How your voice is processed

Your voice passes through a chain of effects, always in the same order. Knowing the order helps you predict what an encoder will do.

| # | Stage | What it does, in plain language |
|---|---|---|
| 0 | **Noise gate** | Anything quieter than a fixed threshold (about −34 dB below full scale) is treated as silence, so room noise and breath do not get processed. |
| 0b | **Pitch shifter** | Shifts your voice up or down by as much as an octave. Completely off (and adds no delay) when set to 0. *(Encoder: PITCH.)* |
| 1 | **Chorus doubler** | Adds a faintly delayed, slowly wobbling copy of your voice, like a second person singing along. Always on; not adjustable. |
| 2 | **Sub-octave** | Follows your voice's pitch and mixes in a low, filtered square-wave layer one octave below it, louder when you are louder. *(Encoder: SUB OCT.)* |
| 3 | **Ring modulator ("Robot Clank")** | Multiplies your voice by a low-frequency tone, producing a metallic, robotic, clanking character. *(Encoders: ROBOT, CLANK RATE.)* |
| 4 | **Drive + Wavefolder** | Drive squashes the signal into a gritty saturation. The wavefolder then bends the waveform back on itself for harsh, synthetic overtones. *(Encoders: DRIVE, FOLD.)* |
| 5 | **Decimation / bit-crush** | Lowers the effective sample rate and resolution for a lo-fi, digital-crunch sound. *(Encoder: DECIMATE.)* |
| 6 | **Tone filter** | A fixed low-pass filter (roughly 3 kHz) that tames the harshness created by the earlier stages. Always on. |
| 7 | **Spectral Freeze** | Optionally overlays a looping "drone" captured from your recent audio. *(Footswitch: FREEZE.)* |
| 8 | **Echo** | A repeating echo (about 0.2 seconds between repeats). *(Encoders: DELAY FB, DELAY MIX.)* |
| 9 | **"Catacomb" reverb** | A cavernous multi-echo tail. *(Encoder: REVERB.)* |
| 10 | **DC blocker** | A safety filter that removes inaudible offset the earlier stages can create. Always on. |
| — | **Wet/Dry mix** | Blends the processed signal with your untouched voice. *(Encoder: WET/DRY.)* |

---

## Parameter reference

"Default" is the encoder's position when the app launches: it starts on the DEMONIC GROWL preset, with PITCH at 0.

| Encoder | Range | Default | What it does | Tips |
|---|---|---|---|---|
| **PITCH** | −12 to +12 semitones | 0 (off) | Shifts your whole voice up or down. The value under the encoder is in semitones. 0 = off. | Try −3 to −7 for a deeper monster, +5 to +12 for a chipmunk. Adds a little delay (up to about 40 ms) while on. Big shifts sound rougher and a bit fluttery, and change the voice's character as well as its pitch. |
| **DRIVE** | 0 – 12 dB | 11 | Pushes your voice harder into the saturation stage. Higher = grittier, denser, louder. | Also feeds the wavefolder, so it changes that character too. |
| **SUB OCT** | 0 – 100% | 85% | Level of the low, rumbling layer. 0 = none. | Gives weight and menace. Works best on a single, steady voice; breathy or noisy sounds can make it flutter. It tracks the pitch you sing, including any Pitch shift. |
| **ROBOT** (clank) | 0 – 100% | 0% | How much of the ring-modulated "robot" sound replaces your natural voice. | 0 = natural voice; near 1 = fully robotic. |
| **CLANK RATE** | 10 – 200 Hz | 45 | The frequency of the ring-modulation tone. | Low = slow, throbbing tremolo-like clank. High = harsher, more metallic. Speaking louder pushes the rate up slightly. Only audible when ROBOT is above 0. |
| **FOLD** (wavefolder) | 0 – 5 | 1.0 | Amount of waveform folding (harsh, synthetic overtones). Below about 0.1 it switches off. | **Keep this at 2 or below** unless you want a very loud, harsh output. Above that, the output is often pushed up to the full-scale cap. |
| **DECIMATE** | x1 – x6 | x1 | Lo-fi crunch. 1 = off. 2 ≈ half the sample rate, 3 ≈ one third, up to 6. | Only whole numbers count (x2.5 behaves like x2). A fixed ~6-bit quantisation is always applied, so even at 1 the sound has a slightly gritty edge. |
| **DELAY FB** (feedback) | 0 – 105% | 60% | How much of each echo is fed back to create further repeats. | Below 100% the repeats fade out. **At 100% they never fade. Above 100% (red ring) they build up to a loud, steady level.** Turn it back below 100% to stop it. |
| **DELAY MIX** | 0 – 100% | 35% | How loud the echoes are compared to the direct sound. | Delay time is fixed at about 200 ms. |
| **REVERB** ("catacomb") | 0 – 100% | 45% | How much of the cavernous echo tail is mixed in. | Fixed character (distinct echoes at about 120 ms and 270 ms), more like a stone-room slapback than a smooth hall. |
| **WET/DRY** | 0 – 100% | 85% | 0% = only your untouched voice. 100% = only the processed sound. | Lower values are a handy way to keep words intelligible. |

---

## Presets

There are three built-in presets. Press `P` (or click the PRESET footswitch) to cycle through them. Loading a preset turns the encoders to its values; you can then adjust any of them, and your changes last until the next preset is loaded.

| Preset | Drive | Sub | Robot | Rate | Fold | Decim | Delay FB | Delay Mix | Reverb | Wet/Dry |
|---|---|---|---|---|---|---|---|---|---|---|
| **DEMONIC GROWL** | 11 | 0.85 | 0.00 | 45 | 1.0 | 1 | 0.60 | 0.35 | 0.45 | 0.85 |
| **MEGATRON** | 6 | 0.20 | 0.95 | 75 | 3.0 | 3.5 (→3) | 0.40 | 0.20 | 0.20 | 0.90 |
| **CYBER DEMON** | 10 | 0.70 | 0.65 | 55 | 2.5 | 2 | 0.75 | 0.40 | 0.50 | 0.85 |

> Presets do not include PITCH; it stays wherever you leave it.
>
> MEGATRON and CYBER DEMON set **FOLD to 2.5–3**, which produces very loud output. Turn your volume down before loading them.

---

## Spectral Freeze

Press **`F`** (or click the FREEZE footswitch) to toggle Freeze. While it is on, the footswitch light is red and the display shows `FREEZE` in red.

**What it does:** at the moment you switch it on, the processor grabs a short slice (about 0.09 seconds) of recent audio from its echo memory — audio from roughly half a second earlier — and plays that slice on endless repeat, blended underneath your live voice (about 70% frozen loop, 30% live).

**How it sounds:** a buzzing, sustained drone. Because the loop is so short, it repeats about 11 times per second, which gives the drone a gritty, fluttering texture rather than a smooth held note.

**How to use it well:**

- Sing or say a sustained vowel ("ahhh"), keep going for a second, then press `F`. Stop singing; the drone continues.
- If you press Freeze while silent, the captured slice is silence, so you will hear nothing from the drone.
- Press `F` again to release. Pressing it again later captures a fresh slice.
- The frozen loop passes through the echo and reverb stages afterwards, so it picks up their tails.

---

## Bypass: effects off in one press

Press **`B`** or **`Space`** (or click the BYPASS footswitch) to switch every effect off. Press again to switch them back on. It is the quickest way to hear your plain voice, compare it with the processed sound, or stop something that has gone wrong.

**What happens when you bypass**

- You hear your **untouched voice**: no pitch shift, no gate, no drive, no echo, nothing. The output is an exact copy of the input.
- The switch takes about **5 thousandths of a second**, so it does not click, even in the middle of a word.
- The BYPASS light goes dark, the **display** shows `BYPASS` in red and the waveform turns grey. The input level bar keeps working, so you can still see your microphone is alive.
- **Your encoder settings are kept.** You can even turn encoders while bypassed. They take effect the moment you switch effects back on.

**What it clears**

- **Echo and reverb tails are wiped**, so nothing from before the bypass comes back when you switch effects on again. You will not hear a ghost of what you said earlier.
- **Spectral Freeze is switched off**, and the `F` key and FREEZE footswitch do nothing while bypassed. Freeze the sound again after you switch effects back on.

**Bypass as a panic button.** If the echo has built up out of control (DELAY FB above 100% keeps it going at a loud, steady level), press **`B`** twice: once to bypass, once to come back. The runaway echo is gone. Also turn DELAY FB back below 100%, or it will start to build again.

**What bypass is not.** It is *software* bypass. Your voice still travels through the computer (a small, fixed delay of a couple of milliseconds plus your audio device's own delay). If you want a completely delay-free clean voice, use your audio interface's own **Direct Monitor** switch instead. Just be careful not to leave Direct Monitor on while you are listening to the effects (you would hear both).

---

## Sound recipes

These are starting points; the value under each encoder tells you where you are. Amounts written 0–1 here are percentages on the pedal (0.85 = 85%). Lower your volume before trying the more extreme ones.

**Dark growl** (the DEMONIC GROWL character)
DRIVE ~11, SUB OCT ~0.85, ROBOT 0, FOLD ~1, DECIMATE 1, DELAY MIX ~0.35, DELAY FB ~0.6, REVERB ~0.45, WET/DRY ~0.85.

**Deep demon**
PITCH about −5 to −7 semitones, DRIVE ~8–11, SUB OCT ~0.7, FOLD ~1, REVERB ~0.5, WET/DRY ~1 for only the shifted voice (lower it to blend your normal voice back in as a harmony). Sing a steady note for the cleanest result.

**Robot overlord**
ROBOT ~0.9, CLANK RATE ~75, DRIVE ~6, FOLD ~1.5–2, SUB OCT low (~0.2), WET/DRY ~0.9.

**Lo-fi radio**
DECIMATE 4–6, DRIVE low (~2–4), FOLD 0, SUB OCT 0, ROBOT 0, REVERB 0, WET/DRY 1.

**Cathedral crypt**
REVERB ~0.7, DELAY MIX ~0.4, DELAY FB ~0.7, DRIVE ~4, other effects low, WET/DRY ~0.8.

**Endless drone**
Sustain a vowel, press `F`, stop singing, and speak or hum over the drone. Add reverb for width.

**Self-oscillating echo (use with care)**
Raise DELAY FB to about 100% and DELAY MIX to ~0.5 and say one short word. The echo keeps circulating. Turn DELAY FB back below 100% to let it fade. Going above 100% makes it build up to a loud, steady level.

---

## Troubleshooting

**The input level bar never moves; no sound.**
Check macOS microphone permission for your terminal or IDE (*System Settings → Privacy & Security → Microphone*), and make sure the correct microphone and output are chosen as the system defaults (*System Settings → Sound*). The app always uses the default input and output at 44.1 kHz, mono.

**The level bar moves, but I hear nothing — or only some words come through.**
The noise gate ignores anything below about −34 dB full-scale. Speak closer to the mic or raise your input gain in *System Settings → Sound → Input*. Quiet endings of words may be chopped off; that is the gate.

**The pitch-shifted voice sounds fluttery or ticks.**
The shifter works by sliding a playback point and jumping back several times a second (about 6 times a second at −5 semitones, up to 25 at a full octave). Smaller shifts are smoother, steady sung notes work better than fast speech, and lowering Wet/Dry blends in your natural voice to mask it.

**I only hear my normal voice, and the effects have stopped.**
Bypass is on. The BYPASS footswitch light is dark and the display shows `BYPASS`. Press `B` or `Space` (or click the BYPASS footswitch) to turn effects back on.

**Loud howling or squealing.**
Acoustic feedback. Use headphones, or move the mic far from the speakers and lower the volume.

**Harsh, crackling, or "blown out" sound; way too loud.**
Reduce **FOLD** (to 2 or below), reduce **DRIVE**, set **DELAY FB** below 100%, and lower the system volume.

**An echo that keeps growing or won't stop.**
DELAY FB is at or above 100%. Press `B` twice (bypass, then back) to flush the echo at once, then turn DELAY FB down below 100% (e.g., 60%).

**Sound is delayed or "laggy".**
The app's own buffering is tiny (about 1.5 ms each way). Most lag comes from the audio device — switch from Bluetooth to wired headphones, and avoid sharing the audio device with other apps.

**Build error: `portaudio.h: No such file or directory` or `library not found for -lportaudio`.**
Run `brew install portaudio pkg-config`, delete the `build` folder, and configure again (see [If the build cannot find PortAudio](#if-the-build-cannot-find-portaudio)). No manual path editing should be needed.

**Build error while fetching raylib.**
The first configure step downloads raylib from GitHub, so it needs an internet connection.

**"PortAudio error" or "Stream open error" printed in the terminal.**
No usable default input/output device was found, or another app has exclusive control of it. Check *System Settings → Sound*, close other audio apps, and try again.

---

## Known limitations

These are things you may notice and why:

- **Pitch shifting is a simple, fast design.** It moves your pitch and the voice's character together (it does not preserve the "vocal tract" sound), and it adds a slight flutter that grows with bigger shifts. In testing, pitch landed within a few cents on voice-like sounds, and within about 40 cents even at a full octave on a pure whistle-like tone.
- **Sub-Octave needs a steady voice.** It follows your pitch, so it works best on one clear sung or spoken note. Whispers, breathy sounds, heavy noise, or several voices at once can make it flutter or drop out. It is silent when you are silent.
- **Output protection is a hard limit.** Nothing goes past full scale, but high FOLD values and DELAY FB above 100% hit that limit and sound harshly clipped.
- **Bypass is software bypass.** It is exact and click-free, but your voice still passes through the computer. For zero-delay monitoring use your audio interface's Direct Monitor.
- **Mono, 44.1 kHz only**, using the default audio devices.
- **Fixed echo and reverb timings.** Only their amounts can be adjusted.
- **No saving of settings.** Your encoder changes are lost when you load another preset or restart.
- **Only tested on macOS-style setups.** The desktop app has not been built or tested on Linux or Windows.
- **No hardware (Raspberry Pi Pico) version yet.** The simulated enclosure shows what an RP2040 pedal could look like; it is not a finished hardware design. The repository now includes a benchmark program for finding out whether the Pico can run the effects in real time (see the Developer Guide), but there is no audio-capable firmware yet.
