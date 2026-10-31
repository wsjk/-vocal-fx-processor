# Vocal FX Processor — User Guide

The Vocal FX Processor turns your microphone into a real-time "demonic" vocal effects pedal. You talk or sing into a mic, and what comes out of your headphones is a growling, robotic, glitchy, echoing version of your voice.

The desktop app has three parts: a small simulated OLED screen (modelled on the display of a hardware pedal), two buttons, and ten sliders. Everything runs live with very little delay.

> **Status note.** This guide describes the code as it exists in the repository today. A few things behave differently from what the on-screen labels suggest (most importantly, **presets currently change the displayed name only**). They are collected in [Known limitations](#known-limitations) so you are not surprised.

---

## Contents

1. [What you need](#what-you-need)
2. [Install and run](#install-and-run)
3. [Safety first: volume and headphones](#safety-first-volume-and-headphones)
4. [A tour of the screen](#a-tour-of-the-screen)
5. [Controls](#controls)
6. [How your voice is processed](#how-your-voice-is-processed)
7. [Parameter reference](#parameter-reference)
8. [Presets](#presets)
9. [Spectral Freeze](#spectral-freeze)
10. [Sound recipes](#sound-recipes)
11. [Troubleshooting](#troubleshooting)
12. [Known limitations](#known-limitations)

---

## What you need

| Requirement | Details |
|---|---|
| Computer | **macOS.** The current build file links Apple frameworks (CoreAudio, Cocoa, etc.), so Windows and Linux are not supported without editing `CMakeLists.txt`. |
| Tools | [CMake](https://cmake.org) 3.13 or newer and a C++17 compiler (Xcode Command Line Tools). |
| Audio library | [PortAudio](http://www.portaudio.com), installed on your system (see below). |
| Internet | Needed once, the first time you build, so CMake can download the raylib graphics library. |
| Hardware | A microphone and **headphones** (strongly recommended — see [Safety first](#safety-first-volume-and-headphones)). |

---

## Install and run

1. **Install the tools** (using [Homebrew](https://brew.sh)):

   ```bash
   xcode-select --install        # skip if you already have a compiler
   brew install cmake portaudio
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

5. **Allow microphone access.** The first time you run it, macOS asks whether your terminal (or IDE) may use the microphone. Choose **Allow**. If you miss the prompt, enable it under *System Settings → Privacy & Security → Microphone*.

6. **Quit** by closing the window or pressing `Esc`.

### Apple Silicon Macs (M1/M2/M3/M4)

`CMakeLists.txt` looks for PortAudio in `/usr/local`, which is where Homebrew installs on *Intel* Macs. On Apple Silicon, Homebrew installs to `/opt/homebrew`. If the build fails with `portaudio.h not found` or `library 'portaudio' not found`, open `CMakeLists.txt` and change the two `/usr/local` paths (`include` and `lib`) to `/opt/homebrew/include` and `/opt/homebrew/lib`, then rebuild.

> The `portaudio/` folder you may see inside the project is a copy of the PortAudio source. The build does **not** use it — it uses the copy installed by Homebrew.

---

## Safety first: volume and headphones

- **Use headphones.** The app plays your processed voice straight back out. With speakers, the mic picks the sound up again and you get an ear-piercing howl (acoustic feedback).
- **Start with the system volume low.** There is no master volume control and no output limiter. Some settings can produce output well above full scale — high **Wavefolder** values and **Delay Feedback above 1.0** in particular — which clips harshly and can be very loud.
- **Wired headphones give the lowest delay.** Bluetooth adds noticeable lag on top of the app's own tiny delay.

---

## A tour of the screen

```
┌──────────────────────────────────────────┐
│  RP2040 MASTER PROCESSOR + SUSTAIN       │
│  Press 'P' to cycle preset | 'F' = Freeze│
│        ┌────────────────────────┐        │
│        │ PTCH:DEMONIC GROWL     │  ← "OLED" display
│        │  ~~~~ waveform ~~~~    │
│        │ ████████ (input level) │
│        └────────────────────────┘        │
│  [ PRESET: DEMONIC GROWL ] [ FREEZE: OFF ]│
│  Drive (Gain)    ▓▓▓▓▓▓▓▓▓░░░░           │
│  Sub-Octave      ▓▓▓▓▓▓▓░░░░░░           │
│  …  (10 sliders in total)                │
└──────────────────────────────────────────┘
```

**The simulated OLED display** (top, black box):

- **Top-left text** — `PTCH:` followed by the current preset name. ("PTCH" is just a label; this display does not show pitch.)
- **`[FREEZE ON]`** — appears in red when Spectral Freeze is active.
- **Waveform** — a very short snapshot (about 3 milliseconds) of the sound *coming out* of the processor. It scrolls quickly and mostly serves as a "yes, it's alive" indicator. It turns red during Freeze.
- **Bar along the bottom** — your **microphone input level**. If this bar never moves when you talk, the app is not hearing your mic (see [Troubleshooting](#troubleshooting)).

**The two buttons** sit below the display: the preset button on the left and the Spectral Freeze button on the right.

**The ten sliders** fill the rest of the window. A fuller blue bar means a higher value. The sliders do not show numbers, so you set them by eye and by ear.

---

## Controls

| Action | How |
|---|---|
| Cycle to the next preset | Press **`P`**, or click the preset button |
| Toggle Spectral Freeze | Press **`F`**, or click the freeze button |
| Change a slider | Click or drag with the mouse **inside the slider's bar** |
| Quit | `Esc` or close the window |

Slider tips:

- You can click anywhere on a bar to jump straight to that value, then drag to fine-tune.
- Dragging only works while the pointer stays within the bar's row. If you slide up or down out of it, the slider stops following the mouse until you click it again.
- Changes are heard immediately, even mid-word.

---

## How your voice is processed

Your voice passes through a chain of effects, always in the same order. Knowing the order helps you predict what a slider will do.

| # | Stage | What it does, in plain language |
|---|---|---|
| 0 | **Noise gate** | Anything quieter than a fixed threshold (about −34 dB below full scale) is treated as silence, so room noise and breath do not get processed. |
| 1 | **Chorus doubler** | Adds a faintly delayed, slowly wobbling copy of your voice, like a second person singing along. Always on; not adjustable. |
| 2 | **Sub-octave** | Mixes in a low, rumbling, filtered square-wave layer that follows your voice's loudness. *(Slider: Sub-Octave.)* |
| 3 | **Ring modulator ("Robot Clank")** | Multiplies your voice by a low-frequency tone, producing a metallic, robotic, clanking character. *(Sliders: Robot Clank, Clank Rate.)* |
| 4 | **Drive + Wavefolder** | Drive squashes the signal into a gritty saturation. The wavefolder then bends the waveform back on itself for harsh, synthetic overtones. *(Sliders: Drive, Wavefolder.)* |
| 5 | **Decimation / bit-crush** | Lowers the effective sample rate and resolution for a lo-fi, digital-crunch sound. *(Slider: Decimation.)* |
| 6 | **Tone filter** | A fixed low-pass filter (roughly 3 kHz) that tames the harshness created by the earlier stages. Always on. |
| 7 | **Spectral Freeze** | Optionally overlays a looping "drone" captured from your recent audio. *(Button: Freeze.)* |
| 8 | **Echo** | A repeating echo (about 0.2 seconds between repeats). *(Sliders: Delay Feedback, Delay Mix.)* |
| 9 | **"Catacomb" reverb** | A cavernous multi-echo tail. *(Slider: Catacomb Rev.)* |
| 10 | **DC blocker** | A safety filter that removes inaudible offset the earlier stages can create. Always on. |
| — | **Wet/Dry mix** | Blends the processed signal with your untouched voice. *(Slider: Wet/Dry Mix.)* |

---

## Parameter reference

"Default" is the slider's starting position when the app launches.

| Slider | Range | Default | What it does | Tips |
|---|---|---|---|---|
| **Drive (Gain)** | 0 – 12 dB | 11 | Pushes your voice harder into the saturation stage. Higher = grittier, denser, louder. | Also feeds the wavefolder, so it changes that character too. |
| **Sub-Octave** | 0 – 1 | 0.85 | Level of the low, rumbling layer. 0 = none. | Gives weight and menace. See [Known limitations](#known-limitations) — it is a gritty rumble rather than a clean octave-down. |
| **Robot Clank** | 0 – 1 | 0 | How much of the ring-modulated "robot" sound replaces your natural voice. | 0 = natural voice; near 1 = fully robotic. |
| **Clank Rate** | 10 – 200 Hz | 55 | The frequency of the ring-modulation tone. | Low = slow, throbbing tremolo-like clank. High = harsher, more metallic. Speaking louder pushes the rate up slightly. Only audible when Robot Clank is above 0. |
| **Wavefolder** | 0 – 5 | 1.5 | Amount of waveform folding (harsh, synthetic overtones). Below about 0.1 it switches off. | **Keep this at 2 or below** unless you want a very loud, harsh output. Above that, the output can far exceed full scale. |
| **Decimation** | 1 – 6 | 1 | Lo-fi crunch. 1 = off. 2 ≈ half the sample rate, 3 ≈ one third, up to 6. | Only whole numbers count (2.9 behaves like 2). A fixed ~6-bit quantisation is always applied, so even at 1 the sound has a slightly gritty edge. |
| **Delay Feedback** | 0 – 1.05 | 0.5 | How much of each echo is fed back to create further repeats. | Below 1.0 the repeats fade out. **At 1.0 they never fade. Above 1.0 they get louder** until the output clips. Pull it back below 1.0 to stop it. |
| **Delay Mix** | 0 – 1 | 0.3 | How loud the echoes are compared to the direct sound. | Delay time is fixed at about 200 ms. |
| **Catacomb Rev** | 0 – 1 | 0.45 | How much of the cavernous echo tail is mixed in. | Fixed character (distinct echoes at about 120 ms and 270 ms), more like a stone-room slapback than a smooth hall. |
| **Wet/Dry Mix** | 0 – 1 | 0.85 | 0 = only your untouched voice. 1 = only the processed sound. | Lower values are a handy way to keep words intelligible. |

---

## Presets

There are three built-in presets. Press `P` (or click the preset button) to cycle through them.

| Preset | Drive | Sub | Robot | Rate | Fold | Decim | Delay FB | Delay Mix | Reverb | Wet/Dry |
|---|---|---|---|---|---|---|---|---|---|---|
| **DEMONIC GROWL** | 11 | 0.85 | 0.00 | 45 | 1.0 | 1 | 0.60 | 0.35 | 0.45 | 0.85 |
| **MEGATRON** | 6 | 0.20 | 0.95 | 75 | 3.0 | 3.5 (→3) | 0.40 | 0.20 | 0.20 | 0.90 |
| **CYBER DEMON** | 10 | 0.70 | 0.65 | 55 | 2.5 | 2 | 0.75 | 0.40 | 0.50 | 0.85 |

> **Important: presets do not currently change the sound.** Pressing `P` changes the preset *name* on screen, but the app immediately re-applies the slider positions on top of the preset, so the sound stays whatever your sliders say. To recreate a preset, set the sliders by eye to the values above. (Developers: see the Known Issues section of the Developer Guide for the cause and fix.)
>
> If you do set sliders to the MEGATRON or CYBER DEMON values, remember that **Wavefolder 2.5–3** produces very loud output. Turn your volume down first.

---

## Spectral Freeze

Press **`F`** (or click the freeze button) to toggle Freeze. While it is on, the display shows `[FREEZE ON]` in red.

**What it does:** at the moment you switch it on, the processor grabs a short slice (about 0.09 seconds) of recent audio from its echo memory — audio from roughly half a second earlier — and plays that slice on endless repeat, blended underneath your live voice (about 70% frozen loop, 30% live).

**How it sounds:** a buzzing, sustained drone. Because the loop is so short, it repeats about 11 times per second, which gives the drone a gritty, fluttering texture rather than a smooth held note.

**How to use it well:**

- Sing or say a sustained vowel ("ahhh"), keep going for a second, then press `F`. Stop singing; the drone continues.
- If you press Freeze while silent, the captured slice is silence, so you will hear nothing from the drone.
- Press `F` again to release. Pressing it again later captures a fresh slice.
- The frozen loop passes through the echo and reverb stages afterwards, so it picks up their tails.

---

## Sound recipes

These are starting points. Remember there are no numeric readouts, so aim by eye. Lower your volume before trying the more extreme ones.

**Dark growl** (the DEMONIC GROWL character)
Drive ~11, Sub-Octave ~0.85, Robot Clank 0, Wavefolder ~1, Decimation 1, Delay Mix ~0.35, Delay Feedback ~0.6, Catacomb Rev ~0.45, Wet/Dry ~0.85.

**Robot overlord**
Robot Clank ~0.9, Clank Rate ~75, Drive ~6, Wavefolder ~1.5–2, Sub-Octave low (~0.2), Wet/Dry ~0.9.

**Lo-fi radio**
Decimation 4–6, Drive low (~2–4), Wavefolder 0, Sub-Octave 0, Robot Clank 0, Catacomb Rev 0, Wet/Dry 1.

**Cathedral crypt**
Catacomb Rev ~0.7, Delay Mix ~0.4, Delay Feedback ~0.7, Drive ~4, other effects low, Wet/Dry ~0.8.

**Endless drone**
Sustain a vowel, press `F`, stop singing, and speak or hum over the drone. Add reverb for width.

**Self-oscillating echo (use with care)**
Raise Delay Feedback to about 1.0 and Delay Mix to ~0.5 and say one short word. The echo keeps circulating. Slide Delay Feedback back below 1.0 to let it fade. Going above 1.0 makes it grow louder and louder.

---

## Troubleshooting

**The input level bar never moves; no sound.**
Check macOS microphone permission for your terminal or IDE (*System Settings → Privacy & Security → Microphone*), and make sure the correct microphone and output are chosen as the system defaults (*System Settings → Sound*). The app always uses the default input and output at 44.1 kHz, mono.

**The level bar moves, but I hear nothing — or only some words come through.**
The noise gate ignores anything below about −34 dB full-scale. Speak closer to the mic or raise your input gain in *System Settings → Sound → Input*. Quiet endings of words may be chopped off; that is the gate.

**Loud howling or squealing.**
Acoustic feedback. Use headphones, or move the mic far from the speakers and lower the volume.

**Harsh, crackling, or "blown out" sound; way too loud.**
Reduce **Wavefolder** (to 2 or below), reduce **Drive**, set **Delay Feedback** below 1.0, and lower the system volume.

**An echo that keeps growing or won't stop.**
Delay Feedback is at or above 1.0. Drag it down below 1.0 (e.g., 0.6).

**Sound is delayed or "laggy".**
The app's own buffering is tiny (about 1.5 ms each way). Most lag comes from the audio device — switch from Bluetooth to wired headphones, and avoid sharing the audio device with other apps.

**Build error: `portaudio.h: No such file or directory` or `library not found for -lportaudio`.**
Run `brew install portaudio`. On Apple Silicon, also edit the `/usr/local` paths in `CMakeLists.txt` (see [Apple Silicon Macs](#apple-silicon-macs-m1m2m3m4)).

**Build error while fetching raylib.**
The first configure step downloads raylib from GitHub, so it needs an internet connection.

**"PortAudio error" or "Stream open error" printed in the terminal.**
No usable default input/output device was found, or another app has exclusive control of it. Check *System Settings → Sound*, close other audio apps, and try again.

---

## Known limitations

These are things you may notice and why:

- **Presets change the name only**, not the sound (see [Presets](#presets)). Use the sliders.
- **There is no formant/pitch shifting.** The code has a placeholder for it, and the display says `PTCH`, but nothing shifts your pitch. Any deepening you hear comes from the sub-octave layer, saturation and filtering.
- **Sub-Octave is not a clean octave-down.** It uses a simple zero-crossing method that is unreliable on real voices and on low pitches. Expect a gritty, gated rumble that follows your loudness rather than a precise note one octave below.
- **No output level protection.** High Wavefolder values and Delay Feedback above 1.0 can push the output beyond full scale, causing clipping.
- **Mono, 44.1 kHz only**, using the default audio devices.
- **Fixed echo and reverb timings.** Only their amounts can be adjusted.
- **No numeric slider readouts and no saving of settings.** Settings reset every time you restart.
- **macOS-only build** as shipped.
- **No hardware (Raspberry Pi Pico) version yet.** The window title and layout refer to an RP2040 pedal, but the hardware source files in the repo are currently empty.
