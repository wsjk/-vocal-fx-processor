#include <iostream>
#include <cmath>
#include <algorithm>
#include <portaudio.h>
#include "demonic_engine.hpp"

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 64

DemonicEngine demonic(SAMPLE_RATE);

static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    const float* in = static_cast<const float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    float peakIn = 0.0f;
    if (in != nullptr) {
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            peakIn = std::max(peakIn, std::abs(in[i]));
        }
        demonic.processBlock(in, out, framesPerBuffer);
    } else {
        std::fill(out, out + framesPerBuffer, 0.0f);
    }

    // Print mic level meter to console if speaking
    if (peakIn > 0.01f) {
        std::cout << "[Mic Active] Peak Level: " << peakIn << "\r" << std::flush;
    }

    return paContinue;
}

int main() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    // Print default device info
    std::cout << "Default Input Device ID: " << Pa_GetDefaultInputDevice() << "\n";
    std::cout << "Default Output Device ID: " << Pa_GetDefaultOutputDevice() << "\n";

    PaStream* stream;
    err = Pa_OpenDefaultStream(&stream,
                               1,          // 1 input channel (Mic)
                               1,          // 1 output channel (Speaker)
                               paFloat32,  // 32-bit float format
                               SAMPLE_RATE,
                               FRAMES_PER_BUFFER,
                               audioCallback,
                               nullptr);

    if (err != paNoError) {
        std::cerr << "Stream open error: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return 1;
    }

    Pa_StartStream(stream);
    std::cout << "Demonic Vocal DSP Test Harness Running!\n";
    std::cout << "Speak into your mic. Press ENTER to quit...\n";
    std::getchar();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
