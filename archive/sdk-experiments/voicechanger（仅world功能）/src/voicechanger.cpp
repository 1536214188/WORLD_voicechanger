\#include "voicechanger.h"
#include "world/dio.h"
#include "world/stonemask.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/synthesis.h"
#include <vector>
#include <algorithm>
#include <cmath>

static int fs = 48000;
static int initialized = 0;
static std::vector<double> input_buf, output_buf;

int vc_init() {
    if (initialized) return 0;
    input_buf.resize(2048, 0.0);
    output_buf.resize(2048, 0.0);
    initialized = 1;
    return 0;
}

int vc_process_pcm(const int16_t* input_pcm, int samples, int16_t* output_pcm, double pitch, double formant) {
    if (!initialized) vc_init();

    for (int i = 0; i < samples; ++i) input_buf[i] = (double)input_pcm[i] / 32768.0;

    double frame_period = 5.0;
    int f0_length = GetSamplesForDIO(fs, samples, frame_period);
    std::vector<double> f0(f0_length), time_axis(f0_length);

    DioOption dio; InitializeDioOption(&dio);
    Dio(input_buf.data(), samples, fs, &dio, time_axis.data(), f0.data());
    StoneMask(input_buf.data(), samples, fs, time_axis.data(), f0.data(), f0_length, f0.data());

    for (int i = 0; i < f0_length; ++i) f0[i] *= pitch;

    CheapTrickOption ct; InitializeCheapTrickOption(fs, &ct);
    int fft_size = ct.fft_size;
    int freq_bins = fft_size / 2 + 1;
    std::vector<std::vector<double>> sp(f0_length, std::vector<double>(freq_bins));
    std::vector<std::vector<double>> sp_new(f0_length, std::vector<double>(freq_bins));

    std::vector<double*> sp_ptr(f0_length);
    for (int i = 0; i < f0_length; ++i) sp_ptr[i] = sp[i].data();
    CheapTrick(input_buf.data(), samples, fs, time_axis.data(), f0.data(), f0_length, &ct, sp_ptr.data());

    for (int i = 0; i < f0_length; ++i) {
        for (int j = 0; j < freq_bins; ++j) {
            int src = (int)(j * formant);
            sp_new[i][j] = (src < freq_bins) ? sp[i][src] : sp[i][freq_bins - 1];
        }
    }

    D4COption d4c; InitializeD4COption(&d4c);
    std::vector<std::vector<double>> ap(f0_length, std::vector<double>(freq_bins));
    std::vector<double*> ap_ptr(f0_length);
    for (int i = 0; i < f0_length; ++i) ap_ptr[i] = ap[i].data();
    D4C(input_buf.data(), samples, fs, time_axis.data(), f0.data(), f0_length, fft_size, &d4c, ap_ptr.data());

    std::vector<double*> sp_new_ptr(f0_length);
    for (int i = 0; i < f0_length; ++i) sp_new_ptr[i] = sp_new[i].data();
    Synthesis(f0.data(), f0_length, sp_new_ptr.data(), ap_ptr.data(), fft_size, frame_period, fs, samples, output_buf.data());

    for (int i = 0; i < samples; ++i) {
        double val = output_buf[i] * 32767.0;
        output_pcm[i] = (int16_t)std::max(-32768.0, std::min(32767.0, val));
    }
    return 0;
}

void vc_destroy() {
    input_buf.clear();
    output_buf.clear();
    initialized = 0;
}