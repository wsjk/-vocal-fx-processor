#include <iostream>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <vector>
#include <portaudio.h>
#include "raylib.h"
#include "demonic_engine.hpp"

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 64

DemonicEngine demonic(SAMPLE_RATE);

// Thread-safe parameters and oscilloscope buffer
std::atomic<float> guiDrive(11.0f);
std::atomic<float> guiSubBlend(0.85f);
std::atomic<float> guiRobotBlend(0.0f);
std::atomic<float> guiMix(0.85f);
std::atomic<int>   guiActivePreset(0);
std::atomic<float> currentMicPeak(0.0f);

// Ring buffer for oscilloscope visualization (128 samples wide to match OLED width)
constexpr size_t SCOPE_SIZE = 128;
float scopeBuffer[SCOPE_SIZE] = {0};
std::atomic<size_t> scopeHead(0);

static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    const float* in = static_cast<const float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    // Apply preset changes
    static int lastPreset = -1;
    int currentPreset = guiActivePreset.load();
    if (currentPreset != lastPreset) {
        demonic.loadPreset(currentPreset);
        lastPreset = currentPreset;
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

        // Feed output samples into the oscilloscope ring buffer
        size_t head = scopeHead.load();
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            scopeBuffer[head % SCOPE_SIZE] = out[i];
            head++;
        }
        scopeHead.store(head);
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

    const int screenWidth = 480;
    const int screenHeight = 430;
    InitWindow(screenWidth, screenHeight, "Demonic & Cybernetic Pedal Simulator - Oscilloscope");
    SetTargetFPS(60);

    RenderTexture2D oledCanvas = LoadRenderTexture(128, 64);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_P)) {
            int nextPreset = (guiActivePreset.load() + 1) % demonic.getPresetCount();
            guiActivePreset.store(nextPreset);
        }

        float driveVal = guiDrive.load();
        float subVal = guiSubBlend.load();
        float robotVal = guiRobotBlend.load();
        float mixVal = guiMix.load();
        int presetIdx = guiActivePreset.load();

        // Draw simulated OLED screen content with live oscilloscope waveform
        BeginTextureMode(oledCanvas);
        ClearBackground(BLACK);
        
        DrawRectangleLines(0, 0, 128, 64, BLUE);
        DrawText(TextFormat("PTCH:%s", demonic.getCurrentPresetName()), 2, 2, 7, SKYBLUE);

        // Draw Live Oscilloscope Waveform inside OLED center (y-range: 12 to 52)
        size_t head = scopeHead.load();
        for (int x = 0; x < 128 - 1; ++x) {
            // Sample historical data from the ring buffer
            size_t idx1 = (head + x) % SCOPE_SIZE;
            size_t idx2 = (head + x + 1) % SCOPE_SIZE;
            
            float y1 = 32.0f - (scopeBuffer[idx1] * 24.0f);
            float y2 = 32.0f - (scopeBuffer[idx2] * 24.0f);
            
            // Clamp within OLED drawing bounds
            y1 = std::clamp(y1, 12.0f, 58.0f);
            y2 = std::clamp(y2, 12.0f, 58.0f);

            DrawLine(x, static_cast<int>(y1), x + 1, static_cast<int>(y2), SKYBLUE);
        }

        // Mini VU / peak indicator in bottom corner
        float peak = currentMicPeak.load();
        DrawRectangle(2, 58, static_cast<int>(std::min(peak * 124.0f, 124.0f)), 4, BLUE);
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
