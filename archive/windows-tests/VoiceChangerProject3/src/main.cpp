#include <iostream>
#include <cstring>
#include <portaudio.h>

extern "C" {
#include "voicechanger.h"
}

#define SAMPLE_RATE 48000
#define FRAME_SAMPLES 960

struct AudioContext {
    PCMFrame20ms input;
};

static int audioCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void*)
{
    const int16_t* in = (const int16_t*)inputBuffer;
    int16_t* out = (int16_t*)outputBuffer;

    if (!in) {
        memset(out, 0, framesPerBuffer * sizeof(int16_t));
        return paContinue;
    }

    PCMFrame20ms input_frame;
    PCMFrame20ms output_frame;

    memcpy(input_frame.data, in, FRAME_SAMPLES * sizeof(int16_t));

    output_frame = vc_process(input_frame);

    memcpy(out, output_frame.data, FRAME_SAMPLES * sizeof(int16_t));

    return paContinue;
}

int main()
{
    if (!vc_init()) {
        std::cout << "VoiceChanger init failed\n";
        return -1;
    }

    vc_set_params(0.7, 1.2);

    PaError err = Pa_Initialize();

    if (err != paNoError) {
        std::cout << "PortAudio init error\n";
        return -1;
    }

    PaStream* stream;

    err = Pa_OpenDefaultStream(
        &stream,
        1,
        1,
        paInt16,
        SAMPLE_RATE,
        FRAME_SAMPLES,
        audioCallback,
        NULL
    );

    if (err != paNoError) {
        std::cout << "Open stream error\n";
        return -1;
    }

    Pa_StartStream(stream);

    std::cout << "=========================================\n";
    std::cout << "WORLD Realtime Voice Changer\n";
    std::cout << "Input 20ms PCM -> Output 20ms PCM\n";
    std::cout << "Press ENTER to stop\n";
    std::cout << "=========================================\n";

    std::cin.get();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    vc_destroy();

    std::cout << "Application exited\n";

    return 0;
}