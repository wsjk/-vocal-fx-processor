#include <iostream>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <portaudio.h>
#include "raylib.h"
#include "demonic_engine.hpp"

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 64

DemonicEngine demonic(SAMPLE_RATE);

// Thread-safe parameters updated by GUI sliders/buttons
std::atomic<float> guiDrive(11.0f);
std::atomic<float> guiSubBlend(0.85f);
std::atomic<float> guiRobotBlend(0.0f);
std::atomic<float> guiMix(0.85f);
std::atomic<int>   guiActivePreset(0);
std::atomic<float> currentMicPeak(0.0f);

static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    const float* in = static_cast<const float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    // Apply preset changes or manual slider overrides
    static int lastPreset = -1;
    int currentPreset = guiActivePreset.load();
    if (currentPreset != lastPreset) {
        demonic.loadPreset(currentPreset);
        lastPreset = currentPreset;
        // Sync sliders back to preset values
        // (In a full app, getters would handle this smoothly)
    }

    demonic.setDrive(guiDrive.load());
    demonic.setSubOctaveBlend(guiSubBlend.load());
    demonic.setRobotBlend(guiRobotBlend.load());
    demonic.setMix(guiMix.load());

    float peakIn = 0.0f;
    if (in != nullptr) {
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            peakIn = std::max(peakIn, std::abs(in[i]));
        }
        demonic.processBlock(in, out, framesPerBuffer);
    } else {
        std::fill(out, out + framesPerBuffer, 0.0f);
    }

    currentMicPeak.store(peakIn);
    return paContinue;
}

int main() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    PaStream* stream;
    err = Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, SAMPLE_RATE, FRAMES_PER_BUFFER, audioCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "Stream open error: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return 1;
    }
    Pa_StartStream(stream);

    // Initialize Raylib Window (Expanded for preset button)
    const int screenWidth = 480;
    const int screenHeight = 430;
    InitWindow(screenWidth, screenHeight, "Demonic & Cybernetic Pedal Simulator - Presets");
    SetTargetFPS(60);

    RenderTexture2D oledCanvas = LoadRenderTexture(128, 64);

    while (!WindowShouldClose()) {
        // Keyboard shortcut 'P' to cycle presets
        if (IsKeyPressed(KEY_P)) {
            int nextPreset = (guiActivePreset.load() + 1) % demonic.getPresetCount();
            guiActivePreset.store(nextPreset);
        }

        float driveVal = guiDrive.load();
        float subVal = guiSubBlend.load();
        float robotVal = guiRobotBlend.load();
        float mixVal = guiMix.load();
        int presetIdx = guiActivePreset.load();

        // Draw simulated OLED screen content
        BeginTextureMode(oledCanvas);
        ClearBackground(BLACK);
        
        DrawRectangleLines(0, 0, 128, 64, BLUE);
        DrawText(TextFormat("PATCH: %s", demonic.getCurrentPresetName()), 4, 4, 9, SKYBLUE);
        DrawLine(2, 14, 126, 14, DARKBLUE);

        DrawText(TextFormat("DRV:%.1f SUB:%.2f", driveVal, subVal), 4, 18, 7, RAYWHITE);
        DrawText(TextFormat("ROB:%.2f MIX:%.2f", robotVal, mixVal), 4, 27, 7, RAYWHITE);

        float peak = currentMicPeak.load();
        DrawRectangle(4, 52, 120, 8, DARKGRAY);
        DrawRectangle(4, 52, static_cast<int>(std::min(peak * 120.0f, 120.0f)), 8, SKYBLUE);
        EndTextureMode();

        // Main Window Drawing
        BeginDrawing();
        ClearBackground(GetColor(0x181818FF));

        DrawText("RP2040 MULTI-EFFECTS PEDAL", 24, 15, 18, LIGHTGRAY);
        DrawText("Active Patch: Press 'P' or button below to cycle", 24, 38, 10, GRAY);

        // Draw Scaled OLED Display
        Rectangle sourceRec = { 0.0f, 0.0f, (float)oledCanvas.texture.width, (float)-oledCanvas.texture.height };
        Rectangle destRec = { (screenWidth - 256) / 2.0f, 58.0f, 256.0f, 128.0f };
        DrawTexturePro(oledCanvas.texture, sourceRec, destRec, { 0, 0 }, 0.0f, WHITE);

        Vector2 mousePos = GetMousePosition();

        // Preset Stomp Button
        Rectangle presetBtnRec = { 140.0f, 195.0f, 200.0f, 32.0f };
        bool btnHover = CheckCollisionPointRec(mousePos, presetBtnRec);
        if (btnHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int nextPreset = (presetIdx + 1) % demonic.getPresetCount();
            guiActivePreset.store(nextPreset);
        }
        DrawRectangleRec(presetBtnRec, btnHover ? DARKBLUE : BLUE);
        DrawRectangleLines((int)presetBtnRec.x, (int)presetBtnRec.y, (int)presetBtnRec.width, (int)presetBtnRec.height, SKYBLUE);
        DrawText(TextFormat("PRESET: %s", demonic.getCurrentPresetName()), 150, 204, 10, RAYWHITE);

        // Sliders
        // 1. Drive Slider
        DrawText("Drive (Gain)", 40, 245, 10, LIGHTGRAY);
        DrawRectangle(140, 247, 280, 10, DARKGRAY);
        DrawRectangle(140, 247, static_cast<int>((driveVal / 12.0f) * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 240 && mousePos.y <= 260) {
            float newVal = ((mousePos.x - 140.0f) / 280.0f) * 12.0f;
            guiDrive.store(std::clamp(newVal, 0.0f, 12.0f));
        }

        // 2. Sub-Octave Slider
        DrawText("Sub-Octave", 40, 285, 10, LIGHTGRAY);
        DrawRectangle(140, 287, 280, 10, DARKGRAY);
        DrawRectangle(140, 287, static_cast<int>(subVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 280 && mousePos.y <= 300) {
            float newVal = (mousePos.x - 140.0f) / 280.0f;
            guiSubBlend.store(std::clamp(newVal, 0.0f, 1.0f));
        }

        // 3. Robot Clank Slider
        DrawText("Robot Clank", 40, 325, 10, LIGHTGRAY);
        DrawRectangle(140, 327, 280, 10, DARKGRAY);
        DrawRectangle(140, 327, static_cast<int>(robotVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 320 && mousePos.y <= 340) {
            float newVal = (mousePos.x - 140.0f) / 280.0f;
            guiRobotBlend.store(std::clamp(newVal, 0.0f, 1.0f));
        }

        // 4. Wet/Dry Mix Slider
        DrawText("Wet/Dry Mix", 40, 365, 10, LIGHTGRAY);
        DrawRectangle(140, 367, 280, 10, DARKGRAY);
        DrawRectangle(140, 367, static_cast<int>(mixVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 360 && mousePos.y <= 380) {
            float newVal = (mousePos.x - 140.0f) / 280.0f;
            guiMix.store(std::clamp(newVal, 0.0f, 1.0f));
        }

        EndDrawing();
    }

    UnloadRenderTexture(oledCanvas);
    CloseWindow();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
