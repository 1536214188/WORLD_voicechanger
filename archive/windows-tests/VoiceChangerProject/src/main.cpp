#include <iostream>
#include <portaudio.h>
#include <cstring>

#include "voicechanger.h"

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 480   // 10ms

// ============================
// PortAudio 回调
// ============================

static int audioCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void*)
{
    const int16_t* in = (const int16_t*)input;
    int16_t* out = (int16_t*)output;

    PCMFrame10ms inFrame;
    PCMFrame10ms outFrame;

    if (!in)
    {
        memset(out, 0, frameCount * sizeof(int16_t));
        return paContinue;
    }

    // copy input
    memcpy(inFrame.data, in, sizeof(int16_t) * VC_FRAME_SAMPLES);

    // 调用变声
    outFrame = vc_process(inFrame);

    // copy output
    memcpy(out, outFrame.data, sizeof(int16_t) * VC_FRAME_SAMPLES);

    return paContinue;
}

int main()
{
    PaStream* stream;

    std::cout << "Initializing voice changer...\n";

    vc_init();

    vc_set_params(
        1.6,   // pitch
        0.8    // formant
    );

    Pa_Initialize();

    Pa_OpenDefaultStream(
        &stream,
        1,                      // input channels
        1,                      // output channels
        paInt16,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        audioCallback,
        NULL
    );

    Pa_StartStream(stream);

    std::cout << "实时变声开始\n";
    std::cout << "说话测试...\n";
    std::cout << "按回车退出\n";

    getchar();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);

    Pa_Terminate();

    vc_destroy();

    return 0;
}