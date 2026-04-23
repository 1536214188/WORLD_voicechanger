// main.c - 与 main.cpp 功能相同的纯 C 版本
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <portaudio.h>
#else
#include <unistd.h>
#include <portaudio.h>
#endif

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
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData)
{
    const int16_t* in = (const int16_t*)input;
    int16_t* out = (int16_t*)output;
    PCMFrame10ms inFrame;
    PCMFrame10ms outFrame;

    (void)timeInfo;    // 未使用参数
    (void)statusFlags; // 未使用参数
    (void)userData;    // 未使用参数

    if (!in)
    {
        memset(out, 0, frameCount * sizeof(int16_t));
        return paContinue;
    }

    // 复制输入
    memcpy(inFrame.data, in, sizeof(int16_t) * VC_FRAME_SAMPLES);

    // 调用变声处理
    outFrame = vc_process(inFrame);

    // 复制输出
    memcpy(out, outFrame.data, sizeof(int16_t) * VC_FRAME_SAMPLES);

    return paContinue;
}

int main()
{
    PaError err;
    PaStream* stream = NULL;

    printf("Initializing voice changer...\n");

    // 初始化变声器
    if (!vc_init())
    {
        printf("Failed to initialize voice changer!\n");
        return -1;
    }

    // 设置变声参数
    vc_set_params(1.6, 0.8);  // pitch=1.6, formant=0.8
    printf("Parameters set: pitch=1.6, formant=0.8\n");

    // 初始化 PortAudio
    err = Pa_Initialize();
    if (err != paNoError)
    {
        printf("PortAudio initialization failed: %s\n", Pa_GetErrorText(err));
        vc_destroy();
        return -1;
    }

    // 打开默认音频流
    err = Pa_OpenDefaultStream(
        &stream,
        1,                      // 输入声道数
        1,                      // 输出声道数
        paInt16,                // 格式
        SAMPLE_RATE,            // 采样率
        FRAMES_PER_BUFFER,      // 每缓冲帧数
        audioCallback,          // 回调函数
        NULL                    // 用户数据
    );

    if (err != paNoError)
    {
        printf("Failed to open audio stream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        vc_destroy();
        return -1;
    }

    // 开始音频流
    err = Pa_StartStream(stream);
    if (err != paNoError)
    {
        printf("Failed to start audio stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        Pa_Terminate();
        vc_destroy();
        return -1;
    }

    printf("\n========== 实时变声已开始 ==========\n");
    printf("说话测试...\n");
    printf("按回车键退出\n");
    printf("===================================\n\n");

    // 等待用户输入
    getchar();

    // 停止并关闭音频流
    err = Pa_StopStream(stream);
    if (err != paNoError)
    {
        printf("Warning: Failed to stop stream: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError)
    {
        printf("Warning: Failed to close stream: %s\n", Pa_GetErrorText(err));
    }

    // 终止 PortAudio
    Pa_Terminate();

    // 销毁变声器
    vc_destroy();

    printf("程序已退出\n");

    return 0;
}