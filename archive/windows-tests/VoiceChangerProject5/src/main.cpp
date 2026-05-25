#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <portaudio.h>

extern "C" {
#include "voicechanger.h"
}

struct AudioContext {
    int sample_rate;
    int block_samples;
    std::atomic<int> callback_count;
    std::atomic<int> mismatch_count;
};

static int samples_from_ms(int sample_rate, int ms)
{
    return (sample_rate * ms + 500) / 1000;
}

static int audioCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags statusFlags,
    void* userData)
{
    AudioContext* ctx = static_cast<AudioContext*>(userData);
    const int16_t* in = static_cast<const int16_t*>(inputBuffer);
    int16_t* out = static_cast<int16_t*>(outputBuffer);
    PCMFrame10ms input_frame;
    PCMFrame10ms output_frame;
    int callback_index = ++ctx->callback_count;

    if (framesPerBuffer > VC_FRAME_SAMPLES) {
        std::memset(out, 0, framesPerBuffer * sizeof(int16_t));
        return paContinue;
    }

    std::memset(&input_frame, 0, sizeof(input_frame));
    std::memset(&output_frame, 0, sizeof(output_frame));
    input_frame.samples = (int)framesPerBuffer;

    if (in == nullptr) {
        std::memset(out, 0, framesPerBuffer * sizeof(int16_t));
        return paContinue;
    }

    std::memcpy(input_frame.data, in,
        (size_t)input_frame.samples * sizeof(int16_t));

    output_frame = vc_process(input_frame);

    if (output_frame.samples == input_frame.samples) {
        std::memcpy(out, output_frame.data,
            (size_t)output_frame.samples * sizeof(int16_t));
    }
    else {
        int mismatch_index = ++ctx->mismatch_count;
        if (mismatch_index == 1 || (mismatch_index % 50) == 0) {
            std::cerr << "[Project5] output.samples mismatch input="
                      << input_frame.samples << " output="
                      << output_frame.samples << "\n";
        }
        std::memset(out, 0, framesPerBuffer * sizeof(int16_t));
    }

    if (callback_index == 1 || (callback_index % 50) == 0) {
        std::cout << "[Project5] callback #" << callback_index
                  << " frames=" << framesPerBuffer
                  << " output_samples=" << output_frame.samples
                  << " statusFlags=" << statusFlags
                  << " first_in=" << input_frame.data[0]
                  << " first_out=" << output_frame.data[0]
                  << "\n";
    }

    return paContinue;
}

int main()
{
    int sample_rate = 48000;
    int default_block_samples;
    int block_samples;

    std::cout << "VoiceChangerProject5 variable block-size PortAudio test\n";
    std::cout << "Enter sample rate, e.g. 48000 or 16000 [default 48000]: ";
    if (!(std::cin >> sample_rate)) {
        sample_rate = 48000;
        std::cin.clear();
    }

    default_block_samples = samples_from_ms(sample_rate, VC_FRAME_MS);
    block_samples = default_block_samples;
    std::cout << "Enter block samples, e.g. " << default_block_samples
              << " or 1024 [default " << default_block_samples << "]: ";
    if (!(std::cin >> block_samples)) {
        block_samples = default_block_samples;
        std::cin.clear();
    }
    std::cin.ignore(1024, '\n');

    if (block_samples <= 0 || block_samples > VC_FRAME_SAMPLES) {
        std::cerr << "Invalid block_samples=" << block_samples
                  << ", capacity=" << VC_FRAME_SAMPLES << "\n";
        return 1;
    }

    std::cout << "Using sample_rate=" << sample_rate
              << ", frame_ms=" << VC_FRAME_MS
              << ", default_block_samples=" << default_block_samples
              << ", requested_block_samples=" << block_samples << "\n";

    if (!vc_init(sample_rate)) {
        std::cerr << "vc_init(" << sample_rate << ") failed\n";
        return 1;
    }
    vc_set_params(0.7, 1.2);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << "\n";
        vc_destroy();
        return 1;
    }

    AudioContext context;
    context.sample_rate = sample_rate;
    context.block_samples = block_samples;
    context.callback_count = 0;
    context.mismatch_count = 0;

    PaStream* stream = nullptr;
    err = Pa_OpenDefaultStream(
        &stream,
        1,
        1,
        paInt16,
        sample_rate,
        block_samples,
        audioCallback,
        &context
    );

    if (err != paNoError) {
        std::cerr << "Open stream error: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        vc_destroy();
        return 1;
    }

    const PaStreamInfo* info = Pa_GetStreamInfo(stream);
    if (info != nullptr) {
        std::cout << "PortAudio stream sampleRate=" << info->sampleRate
                  << " inputLatency=" << info->inputLatency
                  << " outputLatency=" << info->outputLatency << "\n";
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Start stream error: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        vc_destroy();
        return 1;
    }

    std::cout << "Realtime test running. Press ENTER to stop.\n";
    std::cin.get();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    vc_destroy();

    std::cout << "Stopped. callbacks=" << context.callback_count
              << " mismatches=" << context.mismatch_count << "\n";
    return 0;
}
