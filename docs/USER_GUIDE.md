# Vocal FX Processor — User Guide

The Vocal FX Processor turns your microphone into a real-time "demonic" vocal effects pedal. You talk or sing into a mic, and what comes out of your headphones is a growling, robotic, glitchy, echoing version of your voice.

The desktop app has three parts: a small simulated OLED screen (modelled on the display of a hardware pedal), three buttons (preset, freeze, and a big **Bypass** button that switches all effects off in one click), and eleven sliders. Everything runs live with very little delay.

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
- **Know your panic button.** Press **`B`** or **Space** (or click the big green button) to bypass all effects instantly. It also flushes any runaway echo. See [Bypass](#bypass-effects-off-in-one-press).
- **Start with the system volume low.** There is no master volume control and no output limiter. Some settings can produce output well above full scale — high **Wavefolder** values and **Delay Feedback above 1.0** in particular — which clips harshly and can be very loud.
- **Wired headphones give the lowest delay.** Bluetooth adds noticeable lag on top of the app's own tiny delay.

---

## A tour of the screen

```
┌──────────────────────────────────────────┐
│  RP2040 MASTER PROCESSOR + SUSTAIN       │
│ 'P' preset | 'F' Freeze | 'B'/Space Bypass│
│        ┌────────────────────────┐        │
│        │ PTCH:DEMONIC GROWL     │  ← "OLED" display
│        │  ~~~~ waveform ~~~~    │
│        │ ████████ (input level) │
│        └────────────────────────┘        │
│  [ PRESET: DEMONIC GROWL ] [ FREEZE: OFF ]│
│  [ EFFECTS ON - click or B/Space = BYPASS ]│   ← big green/red bypass button
│  Pitch +0.0 st   ░░░░░░▓░░░░░░           │
│  Drive (Gain)    ▓▓▓▓▓▓▓▓▓░░░░           │
│  Sub-Octave      ▓▓▓▓▓▓▓░░░░░░           │
│  …  (11 sliders in total)                │
└──────────────────────────────────────────┘
```

**The simulated OLED display** (top, black box):

- **Top-left text** — `PTCH:` followed by the current preset name. ("PTCH" is just a label; this display does not show pitch.)
- **`[FREEZE ON]`** — appears in red when Spectral Freeze is active.
- **`[BYPASS]`** — appears in red when Bypass is on. The waveform turns grey.
- **Waveform** — a very short snapshot (about 3 milliseconds) of the sound *coming out* of the processor. It scrolls quickly and mostly serves as a "yes, it's alive" indicator. It turns red during Freeze.
- **Bar along the bottom** — your **microphone input level**. If this bar never moves when you talk, the app is not hearing your mic (see [Troubleshooting](#troubleshooting)).

**The buttons** sit below the display: the preset button on the left and the Spectral Freeze button on the right, with one wide **Bypass button** beneath them. The Bypass button is **green** with the text "EFFECTS ON" while your effects are working, and turns **red** with "BYPASSED" when they are switched off.

**The eleven sliders** fill the rest of the window. A fuller blue bar means a higher value. Most sliders do not show numbers, so you set them by eye and by ear; the **Pitch** slider's label shows its current value in semitones.

---

## Controls

| Action | How |
|---|---|
| Cycle to the next preset | Press **`P`**, or click the preset button |
| Toggle Spectral Freeze | Press **`F`**, or click the freeze button (does nothing while bypassed) |
| **Bypass: effects off / on** | Press **`B`** or **`Space`**, or click the wide Bypass button |
| Change a slider | Click or drag with the mouse **inside the slider's bar** |
| Quit | `Esc` or close the window |

Slider tips:

- You can click anywhere on a bar to jump straight to that value, then drag to fine-tune.
- Dragging only works while the pointer stays within the bar's row. If you slide up or down out of it, the slider stops following the mouse until you click it again.
- The **Pitch** slider snaps to exactly 0 when you drag close to its center, which switches the pitch shifter fully off.
- Changes are heard immediately, even mid-word.

---

## How your voice is processed

Your voice passes through a chain of effects, always in the same order. Knowing the order helps you predict what a slider will do.

| # | Stage | What it does, in plain language |
|---|---|---|
| 0 | **Noise gate** | Anything quieter than a fixed threshold (about −34 dB below full scale) is treated as silence, so room noise and breath do not get processed. |
| 0b | **Pitch shifter** | Shifts your voice up or down by as much as an octave. Completely off (and adds no delay) when set to 0. *(Slider: Pitch.)* |
| 1 | **Chorus doubler** | Adds a faintly delayed, slowly wobbling copy of your voice, like a second person singing along. Always on; not adjustable. |
| 2 | **Sub-octave** | Follows your voice's pitch and mixes in a low, filtered square-wave layer one octave below it, louder when you are louder. *(Slider: Sub-Octave.)* |
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
| **Pitch** | −12 to +12 semitones | 0 (off) | Shifts your whole voice up or down. The slider label shows the value in semitones. 0 = off. | Try −3 to −7 for a deeper monster, +5 to +12 for a chipmunk. Adds a little delay (up to about 40 ms) while on. Big shifts sound rougher and a bit fluttery, and change the voice's character as well as its pitch. |
| **Drive (Gain)** | 0 – 12 dB | 11 | Pushes your voice harder into the saturation stage. Higher = grittier, denser, louder. | Also feeds the wavefolder, so it changes that character too. |
| **Sub-Octave** | 0 – 1 | 0.85 | Level of the low, rumbling layer. 0 = none. | Gives weight and menace. Works best on a single, steady voice; breathy or noisy sounds can make it flutter. It tracks the pitch you sing, including any Pitch shift. |
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
> Presets do not include the Pitch slider; it stays wherever you leave it.
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

## Bypass: effects off in one press

Press **`B`** or **`Space`** (or click the wide button under the display) to switch every effect off. Press again to switch them back on. It is the quickest way to hear your plain voice, compare it with the processed sound, or stop something that has gone wrong.

**What happens when you bypass**

- You hear your **untouched voice**: no pitch shift, no gate, no drive, no echo, nothing. The output is an exact copy of the input.
- The switch takes about **5 thousandths of a second**, so it does not click, even in the middle of a word.
- The **display** shows `[BYPASS]` in red and the waveform turns grey. The input level bar keeps working, so you can still see your microphone is alive.
- **Your slider settings are kept.** You can even move sliders while bypassed. They take effect the moment you switch effects back on.

**What it clears**

- **Echo and reverb tails are wiped**, so nothing from before the bypass comes back when you switch effects on again. You will not hear a ghost of what you said earlier.
- **Spectral Freeze is switched off**, and the `F` key and freeze button do nothing while bypassed. Freeze the sound again after you switch effects back on.

**Bypass as a panic button.** If the echo has built up out of control (Delay Feedback above 1.0 makes it grow louder and louder), press **`B`** twice: once to bypass, once to come back. The runaway echo is gone. Also pull Delay Feedback back below 1.0, or it will start to build again.

**What bypass is not.** It is *software* bypass. Your voice still travels through the computer (a small, fixed delay of a couple of milliseconds plus your audio device's own delay). If you want a completely delay-free clean voice, use your audio interface's own **Direct Monitor** switch instead. Just be careful not to leave Direct Monitor on while you are listening to the effects (you would hear both).

---

## Sound recipes

These are starting points. Remember there are no numeric readouts, so aim by eye. Lower your volume before trying the more extreme ones.

**Dark growl** (the DEMONIC GROWL character)
Drive ~11, Sub-Octave ~0.85, Robot Clank 0, Wavefolder ~1, Decimation 1, Delay Mix ~0.35, Delay Feedback ~0.6, Catacomb Rev ~0.45, Wet/Dry ~0.85.

**Deep demon**
Pitch about −5 to −7 semitones, Drive ~8–11, Sub-Octave ~0.7, Wavefolder ~1, Catacomb Rev ~0.5, Wet/Dry ~1 for only the shifted voice (lower it to blend your normal voice back in as a harmony). Sing a steady note for the cleanest result.

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

**The pitch-shifted voice sounds fluttery or ticks.**
The shifter works by sliding a playback point and jumping back several times a second (about 6 times a second at −5 semitones, up to 25 at a full octave). Smaller shifts are smoother, steady sung notes work better than fast speech, and lowering Wet/Dry blends in your natural voice to mask it.

**I only hear my normal voice, and the effects have stopped.**
Bypass is on. The wide button under the display will be red and say "BYPASSED", and the display shows `[BYPASS]`. Press `B` or `Space` (or click the button) to turn effects back on.

**Loud howling or squealing.**
Acoustic feedback. Use headphones, or move the mic far from the speakers and lower the volume.

**Harsh, crackling, or "blown out" sound; way too loud.**
Reduce **Wavefolder** (to 2 or below), reduce **Drive**, set **Delay Feedback** below 1.0, and lower the system volume.

**An echo that keeps growing or won't stop.**
Delay Feedback is at or above 1.0. Press `B` twice (bypass, then back) to flush the echo at once, then drag Delay Feedback down below 1.0 (e.g., 0.6).

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

- **Presets change the name only**, not the sound (see [Presets](#presets)). Use the sliders.
- **Pitch shifting is a simple, fast design.** It moves your pitch and the voice's character together (it does not preserve the "vocal tract" sound), and it adds a slight flutter that grows with bigger shifts. In testing, pitch landed within a few cents on voice-like sounds, and within about 40 cents even at a full octave on a pure whistle-like tone.
- **The `PTCH:` label on the display is only the preset name.** It does not show the pitch setting; the Pitch slider's own label does.
- **Sub-Octave needs a steady voice.** It follows your pitch, so it works best on one clear sung or spoken note. Whispers, breathy sounds, heavy noise, or several voices at once can make it flutter or drop out. It is silent when you are silent.
- **No output level protection.** High Wavefolder values and Delay Feedback above 1.0 can push the output beyond full scale, causing clipping.
- **Bypass is software bypass.** It is exact and click-free, but your voice still passes through the computer. For zero-delay monitoring use your audio interface's Direct Monitor.
- **Mono, 44.1 kHz only**, using the default audio devices.
- **Fixed echo and reverb timings.** Only their amounts can be adjusted.
- **No numeric slider readouts and no saving of settings.** Settings reset every time you restart.
- **Only tested on macOS-style setups.** The desktop app has not been built or tested on Linux or Windows.
- **No hardware (Raspberry Pi Pico) version yet.** The window title and layout refer to an RP2040 pedal. The repository now includes a benchmark program for finding out whether the Pico can run the effects in real time (see the Developer Guide), but there is no audio-capable firmware yet.
