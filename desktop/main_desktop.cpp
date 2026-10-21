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

std::atomic<float> guiDrive(11.0f);
std::atomic<float> guiSubBlend(0.85f);
std::atomic<float> guiRobotBlend(0.0f);
std::atomic<float> guiRobotFreq(55.0f);
std::atomic<float> guiDecimation(1.0f);
std::atomic<float> guiMix(0.85f);
std::atomic<int>   guiActivePreset(0);
std::atomic<float> currentMicPeak(0.0f);

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

    static int lastPreset = -1;
    int currentPreset = guiActivePreset.load();
    if (currentPreset != lastPreset) {
        demonic.loadPreset(currentPreset);
        lastPreset = currentPreset;
    }

    demonic.setDrive(guiDrive.load());
    demonic.setSubOctaveBlend(guiSubBlend.load());
    demonic.setRobotBlend(guiRobotBlend.load());
    demonic.setRobotFreq(guiRobotFreq.load());
    demonic.setDecimation(guiDecimation.load());
    demonic.setMix(guiMix.load());

    float peakIn = 0.0f;
    if (in != nullptr) {
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            peakIn = std::max(peakIn, std::abs(in[i]));
        }
        demonic.processBlock(in, out, framesPerBuffer);

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
    const int screenHeight = 490;
    InitWindow(screenWidth, screenHeight, "Demonic & Cybernetic Pedal Simulator - Decimation FX");
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
        float freqVal = guiRobotFreq.load();
        float decimVal = guiDecimation.load();
        float mixVal = guiMix.load();
        int presetIdx = guiActivePreset.load();

        BeginTextureMode(oledCanvas);
        ClearBackground(BLACK);
        
        DrawRectangleLines(0, 0, 128, 64, BLUE);
        DrawText(TextFormat("PTCH:%s", demonic.getCurrentPresetName()), 2, 2, 7, SKYBLUE);

        size_t head = scopeHead.load();
        for (int x = 0; x < 128 - 1; ++x) {
            size_t idx1 = (head + x) % SCOPE_SIZE;
            size_t idx2 = (head + x + 1) % SCOPE_SIZE;
            
            float y1 = 32.0f - (scopeBuffer[idx1] * 24.0f);
            float y2 = 32.0f - (scopeBuffer[idx2] * 24.0f);
            
            y1 = std::clamp(y1, 12.0f, 58.0f);
            y2 = std::clamp(y2, 12.0f, 58.0f);

            DrawLine(x, static_cast<int>(y1), x + 1, static_cast<int>(y2), SKYBLUE);
        }

        float peak = currentMicPeak.load();
        DrawRectangle(2, 58, static_cast<int>(std::min(peak * 124.0f, 124.0f)), 4, BLUE);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(GetColor(0x181818FF));

        DrawText("RP2040 CYBER-DEMON PROCESSOR", 24, 10, 18, LIGHTGRAY);
        DrawText("Active Patch: Press 'P' or button below to cycle", 24, 30, 10, GRAY);

        Rectangle sourceRec = { 0.0f, 0.0f, (float)oledCanvas.texture.width, (float)-oledCanvas.texture.height };
        Rectangle destRec = { (screenWidth - 256) / 2.0f, 48.0f, 256.0f, 128.0f };
        DrawTexturePro(oledCanvas.texture, sourceRec, destRec, { 0, 0 }, 0.0f, WHITE);

        Vector2 mousePos = GetMousePosition();

        // Preset Button
        Rectangle presetBtnRec = { 140.0f, 182.0f, 200.0f, 28.0f };
        bool btnHover = CheckCollisionPointRec(mousePos, presetBtnRec);
        if (btnHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int nextPreset = (presetIdx + 1) % demonic.getPresetCount();
            guiActivePreset.store(nextPreset);
        }
        DrawRectangleRec(presetBtnRec, btnHover ? DARKBLUE : BLUE);
        DrawRectangleLines((int)presetBtnRec.x, (int)presetBtnRec.y, (int)presetBtnRec.width, (int)presetBtnRec.height, SKYBLUE);
        DrawText(TextFormat("PRESET: %s", demonic.getCurrentPresetName()), 150, 189, 10, RAYWHITE);

        // Sliders
        // 1. Drive
        float yPos = 222.0f;
        DrawText("Drive (Gain)", 40, yPos + 2, 10, LIGHTGRAY);
        DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
        DrawRectangle(140, yPos + 4, static_cast<int>((driveVal / 12.0f) * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
            float newVal = ((mousePos.x - 140.0f) / 280.0f) * 12.0f;
            guiDrive.store(std::clamp(newVal, 0.0f, 12.0f));
        }

        // 2. Sub-Octave
        yPos += 38.0f;
        DrawText("Sub-Octave", 40, yPos + 2, 10, LIGHTGRAY);
        DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
        DrawRectangle(140, yPos + 4, static_cast<int>(subVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
            float newVal = (mousePos.x - 140.0f) / 280.0f;
            guiSubBlend.store(std::clamp(newVal, 0.0f, 1.0f));
        }

        // 3. Robot Clank Blend
        yPos += 38.0f;
        DrawText("Robot Clank", 40, yPos + 2, 10, LIGHTGRAY);
        DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
        DrawRectangle(140, yPos + 4, static_cast<int>(robotVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
            float newVal = (mousePos.x - 140.0f) / 280.0f;
            guiRobotBlend.store(std::clamp(newVal, 0.0f, 1.0f));
        }

        // 4. Clank Rate (Hz)
        yPos += 38.0f;
        DrawText("Clank Rate", 40, yPos + 2, 10, LIGHTGRAY);
        DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
        DrawRectangle(140, yPos + 4, static_cast<int>(((freqVal - 10.0f) / 190.0f) * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
            float newVal = 10.0f + ((mousePos.x - 140.0f) / 280.0f) * 190.0f;
            guiRobotFreq.store(std::clamp(newVal, 10.0f, 200.0f));
        }

        // 5. Decimation (Aliasing)
        yPos += 38.0f;
        DrawText("Decimation", 40, yPos + 2, 10, LIGHTGRAY);
        DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
        DrawRectangle(140, yPos + 4, static_cast<int>(((decimVal - 1.0f) / 5.0f) * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
            float newVal = 1.0f + ((mousePos.x - 140.0f) / 280.0f) * 5.0f;
            guiDecimation.store(std::clamp(newVal, 1.0f, 6.0f));
        }

        // 6. Wet/Dry Mix
        yPos += 38.0f;
        DrawText("Wet/Dry Mix", 40, yPos + 2, 10, LIGHTGRAY);
        DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
        DrawRectangle(140, yPos + 4, static_cast<int>(mixVal * 280.0f), 10, SKYBLUE);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
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
