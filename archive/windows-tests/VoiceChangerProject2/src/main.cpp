#include <iostream>
#include <vector>
#include <cstring>
#include <portaudio.h>

#include "voicechanger.h"

#define SAMPLE_RATE 48000
#define FRAME_SAMPLES 960 // 严格锁定每次回调 20ms

// 用于向音频回调传递上下文
struct AudioContext {
    VoiceChanger* vc_ptr;
    std::vector<int16_t> input_buffer;
};

// PortAudio 回调函数：硬件每次强制给我们 20ms 数据，我们必须立刻返回 20ms 数据
static int audioCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData)
{
    AudioContext* ctx = static_cast<AudioContext*>(userData);

    const int16_t* in = static_cast<const int16_t*>(inputBuffer);
    int16_t* out = static_cast<int16_t*>(outputBuffer);

    // 如果麦克风未能提供数据，则输出静音
    if (in == nullptr) {
        std::memset(out, 0, framesPerBuffer * sizeof(int16_t));
        return paContinue;
    }

    // 1. 将 C 指针数据安全拷贝入 C++ std::vector
    std::memcpy(ctx->input_buffer.data(), in, framesPerBuffer * sizeof(int16_t));

    // 2. 送入变声器处理 (20ms进，必定 20ms出)
    std::vector<int16_t> output_vec = ctx->vc_ptr->process(ctx->input_buffer);

    // 3. 将处理完毕的数据送回扬声器
    if (output_vec.size() == framesPerBuffer) {
        std::memcpy(out, output_vec.data(), framesPerBuffer * sizeof(int16_t));
    }
    else {
        std::memset(out, 0, framesPerBuffer * sizeof(int16_t));
    }

    return paContinue;
}

int main()
{
    VoiceChanger vc;

    if (!vc.init()) {
        std::cerr << "Failed to initialize VoiceChanger!" << std::endl;
        return -1;
    }

    // 设置变声参数 (0.8 变男声，1.5 变女声等)
    vc.setParams(0.7, 1.2);

    // 准备上下文，并预先分配好内存，严禁在回调中 new
    AudioContext context;
    context.vc_ptr = &vc;
    context.input_buffer.resize(FRAME_SAMPLES, 0);

    // 初始化硬件音频流
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization error: " << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    PaStream* stream;
    err = Pa_OpenDefaultStream(
        &stream,
        1,              // 麦克风 1 通道
        1,              // 扬声器 1 通道
        paInt16,        // 16-bit 格式
        SAMPLE_RATE,    // 48000 Hz
        FRAME_SAMPLES,  // 每次硬件呼叫 960 帧 (20ms)
        audioCallback,
        &context        // 传入环境指针
    );

    if (err != paNoError) {
        std::cerr << "PortAudio stream opening error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio stream start error: " << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    std::cout << "=========================================\n";
    std::cout << "🎤 WORLD Realtime Voice Changer (20ms strict)\n";
    std::cout << "⚙️ Built-in Crossfade & Buffer mechanics\n";
    std::cout << "👉 Press ENTER to stop streaming...\n";
    std::cout << "=========================================\n";

    // 保持主线程运行
    std::cin.get();

    // 正常退出与清理
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    vc.destroy();

    std::cout << "Application exited gracefully." << std::endl;

    return 0;
}