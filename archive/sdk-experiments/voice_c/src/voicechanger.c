#include "voicechanger.h"

#include "world/dio.h"
#include "world/stonemask.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/synthesis.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>


#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif


typedef struct
{
    double pitch_scale;
    double formant_shift;

    double in_fifo[VC_ANALYSIS_SAMPLES * 2];
    int in_fifo_size;

    double out_fifo[VC_ANALYSIS_SAMPLES * 2];
    int out_fifo_size;

    double prev_in_overlap[VC_OVERLAP_SAMPLES];
    double prev_out_overlap[VC_OVERLAP_SAMPLES];

} VoiceChanger;

static VoiceChanger g_vc;

static double get_time_ms()
{
#ifdef _WIN32
    // Windows版本
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    // Linux/macOS版本
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

static void process_world(double* input, double* output)
{
    double t0 = get_time_ms();

    int x_length = VC_ANALYSIS_SAMPLES;
    double frame_period = 5.0;

    int f0_length = GetSamplesForDIO(VC_FS, x_length, frame_period);

    printf("WORLD start  samples=%d  f0_len=%d\n", x_length, f0_length);

    double* f0 = malloc(sizeof(double) * f0_length);
    double* time_axis = malloc(sizeof(double) * f0_length);

    DioOption dio_option;
    InitializeDioOption(&dio_option);
    dio_option.frame_period = frame_period;

    Dio(input, x_length, VC_FS, &dio_option, time_axis, f0);

    StoneMask(input, x_length, VC_FS, time_axis, f0, f0_length, f0);

    for (int i = 0; i < f0_length; i++)
        if (f0[i] > 0)
            f0[i] *= g_vc.pitch_scale;

    CheapTrickOption ct_option;
    InitializeCheapTrickOption(VC_FS, &ct_option);

    int fft_size = ct_option.fft_size;
    int bins = fft_size / 2 + 1;

    double** sp = malloc(sizeof(double*) * f0_length);
    double** sp_new = malloc(sizeof(double*) * f0_length);

    for (int i = 0; i < f0_length; i++)
    {
        sp[i] = malloc(sizeof(double) * bins);
        sp_new[i] = malloc(sizeof(double) * bins);
    }

    CheapTrick(input, x_length, VC_FS, time_axis, f0, f0_length, &ct_option, sp);

    for (int i = 0; i < f0_length; i++)
    {
        for (int j = 0; j < bins; j++)
        {
            int src = j * g_vc.formant_shift;
            if (src >= bins) src = bins - 1;
            sp_new[i][j] = sp[i][src];
        }
    }

    D4COption d4c_option;
    InitializeD4COption(&d4c_option);

    double** ap = malloc(sizeof(double*) * f0_length);

    for (int i = 0; i < f0_length; i++)
        ap[i] = malloc(sizeof(double) * bins);

    D4C(input, x_length, VC_FS, time_axis, f0, f0_length, fft_size, &d4c_option, ap);

    Synthesis(
        f0,
        f0_length,
        sp_new,
        ap,
        fft_size,
        frame_period,
        VC_FS,
        x_length,
        output
    );

    for (int i = 0; i < f0_length; i++)
    {
        free(sp[i]);
        free(sp_new[i]);
        free(ap[i]);
    }

    free(sp);
    free(sp_new);
    free(ap);
    free(f0);
    free(time_axis);

    double t1 = get_time_ms();

    printf("WORLD done  time=%.2f ms  pitch=%.2f formant=%.2f\n",
        t1 - t0,
        g_vc.pitch_scale,
        g_vc.formant_shift);
}

int vc_init()
{
    memset(&g_vc, 0, sizeof(g_vc));

    g_vc.pitch_scale = 1.0;
    g_vc.formant_shift = 1.0;

    return 1;
}

void vc_set_params(double pitch, double formant)
{
    g_vc.pitch_scale = pitch;
    g_vc.formant_shift = formant;
}

PCMFrame20ms vc_process(PCMFrame20ms input)
{
    PCMFrame20ms output;
    memset(&output, 0, sizeof(output));

    for (int i = 0; i < VC_FRAME_SAMPLES; i++)
        g_vc.in_fifo[g_vc.in_fifo_size++] = input.data[i] / 32768.0;

    if (g_vc.in_fifo_size >= VC_STEP_SAMPLES)
    {
        double block[VC_ANALYSIS_SAMPLES];

        memcpy(block, g_vc.prev_in_overlap, sizeof(double) * VC_OVERLAP_SAMPLES);

        memcpy(block + VC_OVERLAP_SAMPLES,
            g_vc.in_fifo,
            sizeof(double) * VC_STEP_SAMPLES);

        memcpy(
            g_vc.prev_in_overlap,
            g_vc.in_fifo + VC_STEP_SAMPLES - VC_OVERLAP_SAMPLES,
            sizeof(double) * VC_OVERLAP_SAMPLES
        );

        memmove(
            g_vc.in_fifo,
            g_vc.in_fifo + VC_STEP_SAMPLES,
            sizeof(double) * (g_vc.in_fifo_size - VC_STEP_SAMPLES)
        );

        g_vc.in_fifo_size -= VC_STEP_SAMPLES;

        double processed[VC_ANALYSIS_SAMPLES];

        process_world(block, processed);

        memcpy(
            g_vc.out_fifo + g_vc.out_fifo_size,
            processed,
            sizeof(double) * VC_STEP_SAMPLES
        );

        g_vc.out_fifo_size += VC_STEP_SAMPLES;
    }

    if (g_vc.out_fifo_size >= VC_FRAME_SAMPLES)
    {
        for (int i = 0; i < VC_FRAME_SAMPLES; i++)
        {
            double v = g_vc.out_fifo[i] * 32767.0;

            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;

            output.data[i] = (int16_t)v;
        }

        memmove(
            g_vc.out_fifo,
            g_vc.out_fifo + VC_FRAME_SAMPLES,
            sizeof(double) * (g_vc.out_fifo_size - VC_FRAME_SAMPLES)
        );

        g_vc.out_fifo_size -= VC_FRAME_SAMPLES;
    }

    return output;
}

void vc_destroy()
{
    memset(&g_vc, 0, sizeof(g_vc));
}