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

// Thread-safe parameters updated by GUI sliders
std::atomic<float> guiDrive(8.0f);
std::atomic<float> guiSubBlend(0.6f);
std::atomic<float> guiMix(0.8f);
std::atomic<float> currentMicPeak(0.0f);

static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    const float* in = static_cast<const float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    // Apply live parameters from GUI
    // (Note: In production embedded code, parameter smoothing is used to prevent zipper noise)
    demonic.setDrive(guiDrive.load());
    demonic.setSubOctaveBlend(guiSubBlend.load());
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
    // Initialize PortAudio
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

    // Initialize Raylib Window (Pedal GUI Shell)
    const int screenWidth = 480;
    const int screenHeight = 360;
    InitWindow(screenWidth, screenHeight, "Demonic Vocal Processor - Hardware Simulator");
    SetTargetFPS(60);

    // Render texture to simulate the 128x64 I2C OLED screen
    RenderTexture2D oledCanvas = LoadRenderTexture(128, 64);

    while (!WindowShouldClose()) {
        // Update values from GUI sliders
        float driveVal = guiDrive.load();
        float subVal = guiSubBlend.load();
        float mixVal = guiMix.load();

        // Draw simulated OLED screen content
        BeginTextureMode(oledCanvas);
        ClearBackground(BLACK);
        
        // OLED Screen Border & Content (Classic SSD1306 Blue/Cyan style)
        DrawRectangleLines(0, 0, 128, 64, BLUE);
        DrawText("DEMONIC FX", 6, 4, 10, SKYBLUE);
        DrawLine(2, 14, 126, 14, DARKBLUE);

        // Dynamic Text on OLED
        DrawText(TextFormat("DRV: %.1fdB", driveVal), 6, 18, 8, RAYWHITE);
        DrawText(TextFormat("SUB: %.2f", subVal), 6, 28, 8, RAYWHITE);
        DrawText(TextFormat("MIX: %.2f", mixVal), 6, 38, 8, RAYWHITE);

        // Mini VU Meter on OLED bottom
        float peak = currentMicPeak.load();
        DrawRectangle(6, 50, 116, 8, DARKGRAY);
        DrawRectangle(6, 50, static_cast<int>(std::min(peak * 116.0f, 116.0f)), 8, SKYBLUE);
        EndTextureMode();

        // Main Window Drawing
        BeginDrawing();
        ClearBackground(GetColor(0x181818FF)); // Dark pedal enclosure color

        // Draw Pedal Title Header
        DrawText("RP2040 VOCAL PROCESSOR", 24, 20, 20, LIGHTGRAY);
        DrawText("Desktop DSP & OLED Simulator", 24, 45, 10, GRAY);

        // Draw Scaled OLED Display in Center of Pedal GUI
        // Scale 128x64 up by 2x for readability (256x128)
        Rectangle sourceRec = { 0.0f, 0.0f, (float)oledCanvas.texture.width, (float)-oledCanvas.texture.height };
        Rectangle destRec = { (screenWidth - 256) / 2.0f, 75.0f, 256.0f, 128.0f };
        DrawTexturePro(oledCanvas.texture, sourceRec, destRec, { 0, 0 }, 0.0f, WHITE);

        // Draw Interactive Sliders
        // 1. Drive Slider
        DrawText("Drive (Gain)", 40, 220, 10, LIGHTGRAY);
        // Using Raylib GUI-like slider simulation
        float tempDrive = driveVal;
        // Draw slider bar
        DrawRectangle(140, 222, 280, 10, DARKGRAY);
        DrawRectangle(140, 222, static_cast<int>((tempDrive / 12.0f) * 280.0f), 10, SKYBLUE);
        // Check mouse click/drag on slider
        Vector2 mousePos = GetMousePosition();
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 215 && mousePos.y <= 235) {
            float newDrive = ((mousePos.x - 140.0f) / 280.0f) * 12.0f;
            guiDrive.store(std::clamp(newDrive, 0.0f, 12.0f));
        }

        // 2. Sub-Octave Blend Slider
        DrawText("Sub-Octave", 40, 260, 10, LIGHTGRAY);
        DrawRectangle(140, 262, 280, 10, DARKGRAY);
        DrawRectangle(140, 262, static_cast<int>(subVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 255 && mousePos.y <= 275) {
            float newSub = (mousePos.x - 140.0f) / 280.0f;
            guiSubBlend.store(std::clamp(newSub, 0.0f, 1.0f));
        }

        // 3. Wet/Dry Mix Slider
        DrawText("Wet/Dry Mix", 40, 300, 10, LIGHTGRAY);
        DrawRectangle(140, 302, 280, 10, DARKGRAY);
        DrawRectangle(140, 302, static_cast<int>(mixVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= 295 && mousePos.y <= 315) {
            float newMix = (mousePos.x - 140.0f) / 280.0f;
            guiMix.store(std::clamp(newMix, 0.0f, 1.0f));
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
