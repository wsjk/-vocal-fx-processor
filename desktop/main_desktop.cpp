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

std::atomic<float> guiPitch(-500.0f);
std::atomic<float> guiDrive(11.0f);
std::atomic<float> guiSubBlend(0.85f);
std::atomic<float> guiRobotBlend(0.0f);
std::atomic<float> guiRobotFreq(45.0f);
std::atomic<float> guiWavefold(1.0f);
std::atomic<float> guiDecimation(1.0f);
std::atomic<float> guiDelayFB(0.6f);
std::atomic<float> guiDelayMix(0.35f);
std::atomic<float> guiReverb(0.45f);
std::atomic<float> guiMix(0.85f);
std::atomic<int>   guiActivePreset(0);
std::atomic<bool>  guiSpectralFreeze(false);
std::atomic<float> currentMicPeak(0.0f);

constexpr size_t SCOPE_SIZE = 128;
float scopeBuffer[SCOPE_SIZE] = {0};
std::atomic<size_t> scopeHead(0);

// Synchronize GUI slider atomics and engine with preset data
void selectPreset(int idx) {
    const VocalPreset& p = demonic.getPreset(idx);
    guiPitch.store(p.formantCents);
    guiDrive.store(p.driveDb);
    guiSubBlend.store(p.subBlend);
    guiRobotBlend.store(p.robotBlend);
    guiRobotFreq.store(p.robotFreq);
    guiWavefold.store(p.wavefoldDrive);
    guiDecimation.store(p.decimationFactor);
    guiDelayFB.store(p.delayFeedback);
    guiDelayMix.store(p.delayMix);
    guiReverb.store(p.reverbMix);
    guiMix.store(p.wetDryMix);
    guiActivePreset.store(idx);
    demonic.loadPreset(idx);
}

static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    (void)timeInfo;
    (void)statusFlags;
    (void)userData;

    const float* in = static_cast<const float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    // Apply control parameter values from GUI atomics to engine
    demonic.setFormantShift(guiPitch.load());
    demonic.setDrive(guiDrive.load());
    demonic.setSubOctaveBlend(guiSubBlend.load());
    demonic.setRobotBlend(guiRobotBlend.load());
    demonic.setRobotFreq(guiRobotFreq.load());
    demonic.setWavefold(guiWavefold.load());
    demonic.setDecimation(guiDecimation.load());
    demonic.setDelayFeedback(guiDelayFB.load());
    demonic.setDelayMix(guiDelayMix.load());
    demonic.setReverbMix(guiReverb.load());
    demonic.setMix(guiMix.load());
    demonic.setSpectralFreeze(guiSpectralFreeze.load());

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
    // Initialize GUI slider atomics from default preset 0
    selectPreset(0);

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
    const int screenHeight = 660;
    InitWindow(screenWidth, screenHeight, "Demonic Pedal - Freeze & Infinite Repeat");
    SetTargetFPS(60);

    RenderTexture2D oledCanvas = LoadRenderTexture(128, 64);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_P)) {
            int nextPreset = (guiActivePreset.load() + 1) % demonic.getPresetCount();
            selectPreset(nextPreset);
        }
        if (IsKeyPressed(KEY_F)) {
            bool currentFreeze = guiSpectralFreeze.load();
            guiSpectralFreeze.store(!currentFreeze);
        }

        float pitchVal = guiPitch.load();
        float driveVal = guiDrive.load();
        float subVal = guiSubBlend.load();
        float robotVal = guiRobotBlend.load();
        float freqVal = guiRobotFreq.load();
        float foldVal = guiWavefold.load();
        float decimVal = guiDecimation.load();
        float fbVal = guiDelayFB.load();
        float delayMixVal = guiDelayMix.load();
        float revVal = guiReverb.load();
        float mixVal = guiMix.load();
        int presetIdx = guiActivePreset.load();
        bool freezeActive = guiSpectralFreeze.load();

        BeginTextureMode(oledCanvas);
        ClearBackground(BLACK);
        
        DrawRectangleLines(0, 0, 128, 64, BLUE);
        DrawText(TextFormat("PSET:%s", demonic.getCurrentPresetName()), 2, 2, 7, SKYBLUE);
        if (freezeActive) {
            DrawText(" [FREEZE ON]", 75, 2, 7, RED);
        }

        size_t head = scopeHead.load();
        for (int x = 0; x < 128 - 1; ++x) {
            size_t idx1 = (head + x) % SCOPE_SIZE;
            size_t idx2 = (head + x + 1) % SCOPE_SIZE;
            
            float y1 = 32.0f - (scopeBuffer[idx1] * 24.0f);
            float y2 = 32.0f - (scopeBuffer[idx2] * 24.0f);
            
            y1 = std::clamp(y1, 12.0f, 58.0f);
            y2 = std::clamp(y2, 12.0f, 58.0f);

            DrawLine(x, static_cast<int>(y1), x + 1, static_cast<int>(y2), freezeActive ? RED : SKYBLUE);
        }

        float peak = currentMicPeak.load();
        DrawRectangle(2, 58, static_cast<int>(std::min(peak * 124.0f, 124.0f)), 4, freezeActive ? RED : BLUE);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(GetColor(0x181818FF));

        DrawText("RP2040 MASTER PROCESSOR + SUSTAIN", 24, 10, 15, LIGHTGRAY);
        DrawText("Press 'P' to cycle preset | Press 'F' to toggle Freeze", 24, 28, 10, GRAY);

        Rectangle sourceRec = { 0.0f, 0.0f, (float)oledCanvas.texture.width, (float)-oledCanvas.texture.height };
        Rectangle destRec = { (screenWidth - 256) / 2.0f, 46.0f, 256.0f, 128.0f };
        DrawTexturePro(oledCanvas.texture, sourceRec, destRec, { 0, 0 }, 0.0f, WHITE);

        Vector2 mousePos = GetMousePosition();

        // Preset Button
        Rectangle presetBtnRec = { 40.0f, 180.0f, 190.0f, 28.0f };
        bool btnHover = CheckCollisionPointRec(mousePos, presetBtnRec);
        if (btnHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int nextPreset = (presetIdx + 1) % demonic.getPresetCount();
            selectPreset(nextPreset);
        }
        DrawRectangleRec(presetBtnRec, btnHover ? DARKBLUE : BLUE);
        DrawRectangleLines((int)presetBtnRec.x, (int)presetBtnRec.y, (int)presetBtnRec.width, (int)presetBtnRec.height, SKYBLUE);
        DrawText(TextFormat("PRESET: %s", demonic.getCurrentPresetName()), 48, 187, 10, RAYWHITE);

        // Freeze Toggle Button ('F')
        Rectangle freezeBtnRec = { 250.0f, 180.0f, 190.0f, 28.0f };
        bool freezeHover = CheckCollisionPointRec(mousePos, freezeBtnRec);
        if (freezeHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            guiSpectralFreeze.store(!freezeActive);
        }
        DrawRectangleRec(freezeBtnRec, freezeActive ? RED : (freezeHover ? DARKGRAY : BLACK));
        DrawRectangleLines((int)freezeBtnRec.x, (int)freezeBtnRec.y, (int)freezeBtnRec.width, (int)freezeBtnRec.height, RED);
        DrawText(freezeActive ? "SPECTRAL FREEZE: ON" : "SPECTRAL FREEZE: OFF", 258, 187, 10, RAYWHITE);

        // Sliders
        float yPos = 216.0f;
        auto drawSlider = [&](const char* label, float val, float minV, float maxV, std::atomic<float>& target) {
            DrawText(label, 40, yPos + 2, 10, LIGHTGRAY);
            DrawRectangle(140, yPos + 4, 280, 10, DARKGRAY);
            float ratio = (val - minV) / (maxV - minV);
            DrawRectangle(140, yPos + 4, static_cast<int>(ratio * 280.0f), 10, SKYBLUE);
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= 140 && mousePos.x <= 420 && mousePos.y >= yPos && mousePos.y <= yPos + 20) {
                float newVal = minV + ((mousePos.x - 140.0f) / 280.0f) * (maxV - minV);
                target.store(std::clamp(newVal, minV, maxV));
            }
            yPos += 34.0f;
        };

        drawSlider("Pitch (Cents)", pitchVal, -1200.0f, 1200.0f, guiPitch);
        drawSlider("Drive (Gain)", driveVal, 0.0f, 12.0f, guiDrive);
        drawSlider("Sub-Octave", subVal, 0.0f, 1.0f, guiSubBlend);
        drawSlider("Robot Clank", robotVal, 0.0f, 1.0f, guiRobotBlend);
        drawSlider("Clank Rate", freqVal, 10.0f, 200.0f, guiRobotFreq);
        drawSlider("Wavefolder", foldVal, 0.0f, 5.0f, guiWavefold);
        drawSlider("Decimation", decimVal, 1.0f, 6.0f, guiDecimation);
        drawSlider("Delay Feedback", fbVal, 0.0f, 1.05f, guiDelayFB);
        drawSlider("Delay Mix", delayMixVal, 0.0f, 1.0f, guiDelayMix);
        drawSlider("Catacomb Rev", revVal, 0.0f, 1.0f, guiReverb);
        drawSlider("Wet/Dry Mix", mixVal, 0.0f, 1.0f, guiMix);

        EndDrawing();
    }

    UnloadRenderTexture(oledCanvas);
    CloseWindow();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
