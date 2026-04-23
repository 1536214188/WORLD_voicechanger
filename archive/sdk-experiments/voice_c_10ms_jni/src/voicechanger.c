#include "voicechanger.h"
#define M_PI 3.14159265358979323846

#include "world/dio.h"
#include "world/stonemask.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/synthesis.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#endif

// ✅ 只有 Android 才 include JNI
#ifdef __ANDROID__
#include <jni.h>
#endif

typedef struct
{
    double pitch_scale;
    double formant_shift;

    double input_fifo[VC_ANALYSIS_SAMPLES * 4];
    int input_size;

    double output_fifo[VC_ANALYSIS_SAMPLES * 4];
    int output_size;

    double prev_overlap[VC_OVERLAP_SAMPLES];

#ifdef _WIN32
    HANDLE thread;
    CRITICAL_SECTION mutex;
#else
    pthread_t thread;
    pthread_mutex_t mutex;
#endif

    int running;

} VoiceChanger;

static VoiceChanger g_vc;
static double hann_window[VC_ANALYSIS_SAMPLES];

static double get_time_ms()
{
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

static inline double soft_clip(double x)
{
    if (x > 1.0) x = 1.0;
    if (x < -1.0) x = -1.0;
    return tanh(x);
}

/* ================= WORLD ================= */

static void process_world(double* input, double* output)
{
    int x_length = VC_ANALYSIS_SAMPLES;
    double frame_period = 5.0;

    for (int i = 0; i < x_length; i++)
        input[i] *= hann_window[i];

    int f0_length = GetSamplesForDIO(VC_FS, x_length, frame_period);

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
            double src = j * g_vc.formant_shift;
            int s0 = (int)src;
            int s1 = s0 + 1;

            if (s0 >= bins) s0 = bins - 1;
            if (s1 >= bins) s1 = bins - 1;

            double w = src - s0;

            sp_new[i][j] =
                sp[i][s0] * (1.0 - w) +
                sp[i][s1] * w;
        }
    }

    D4COption d4c_option;
    InitializeD4COption(&d4c_option);

    double** ap = malloc(sizeof(double*) * f0_length);
    for (int i = 0; i < f0_length; i++)
        ap[i] = malloc(sizeof(double) * bins);

    D4C(input, x_length, VC_FS, time_axis, f0, f0_length, fft_size, &d4c_option, ap);

    Synthesis(f0, f0_length, sp_new, ap, fft_size, frame_period, VC_FS, x_length, output);

    for (int i = 0; i < x_length; i++)
        output[i] = soft_clip(output[i]);

    for (int i = 0; i < f0_length; i++)
    {
        free(sp[i]); free(sp_new[i]); free(ap[i]);
    }

    free(sp); free(sp_new); free(ap);
    free(f0); free(time_axis);
}

/* ================= 线程 ================= */

#ifdef _WIN32
DWORD WINAPI world_thread(LPVOID arg)
#else
void* world_thread(void* arg)
#endif
{
    while (g_vc.running)
    {
#ifdef _WIN32
        EnterCriticalSection(&g_vc.mutex);
#else
        pthread_mutex_lock(&g_vc.mutex);
#endif

        if (g_vc.input_size >= VC_STEP_SAMPLES)
        {
            double block[VC_ANALYSIS_SAMPLES];

            memcpy(block, g_vc.prev_overlap, sizeof(double) * VC_OVERLAP_SAMPLES);
            memcpy(block + VC_OVERLAP_SAMPLES, g_vc.input_fifo, sizeof(double) * VC_STEP_SAMPLES);

            memcpy(g_vc.prev_overlap,
                   g_vc.input_fifo + VC_STEP_SAMPLES - VC_OVERLAP_SAMPLES,
                   sizeof(double) * VC_OVERLAP_SAMPLES);

            memmove(g_vc.input_fifo,
                    g_vc.input_fifo + VC_STEP_SAMPLES,
                    sizeof(double) * (g_vc.input_size - VC_STEP_SAMPLES));

            g_vc.input_size -= VC_STEP_SAMPLES;

#ifdef _WIN32
            LeaveCriticalSection(&g_vc.mutex);
#else
            pthread_mutex_unlock(&g_vc.mutex);
#endif

            double out[VC_ANALYSIS_SAMPLES];
            process_world(block, out);

#ifdef _WIN32
            EnterCriticalSection(&g_vc.mutex);
#else
            pthread_mutex_lock(&g_vc.mutex);
#endif

            for (int i = 0; i < VC_STEP_SAMPLES; i++)
                g_vc.output_fifo[g_vc.output_size + i] =
                    out[i + VC_OVERLAP_SAMPLES];

            g_vc.output_size += VC_STEP_SAMPLES;
        }

#ifdef _WIN32
        LeaveCriticalSection(&g_vc.mutex);
        Sleep(1);
#else
        pthread_mutex_unlock(&g_vc.mutex);
        usleep(1000);
#endif
    }
    return 0;
}

/* ================= API ================= */

int vc_init()
{
    memset(&g_vc, 0, sizeof(g_vc));

    for (int i = 0; i < VC_ANALYSIS_SAMPLES; i++)
        hann_window[i] = 0.5 - 0.5 * cos(2 * M_PI * i / (VC_ANALYSIS_SAMPLES - 1));

#ifdef _WIN32
    InitializeCriticalSection(&g_vc.mutex);
    g_vc.thread = CreateThread(NULL, 0, world_thread, NULL, 0, NULL);
#else
    pthread_mutex_init(&g_vc.mutex, NULL);
    pthread_create(&g_vc.thread, NULL, world_thread, NULL);
#endif

    g_vc.running = 1;
    return 1;
}

void vc_set_params(double pitch, double formant)
{
#ifdef _WIN32
    EnterCriticalSection(&g_vc.mutex);
#else
    pthread_mutex_lock(&g_vc.mutex);
#endif

    g_vc.pitch_scale = pitch;
    g_vc.formant_shift = formant;

#ifdef _WIN32
    LeaveCriticalSection(&g_vc.mutex);
#else
    pthread_mutex_unlock(&g_vc.mutex);
#endif
}

PCMFrame10ms vc_process(PCMFrame10ms in)
{
    PCMFrame10ms out;
    memset(&out, 0, sizeof(out));

#ifdef _WIN32
    EnterCriticalSection(&g_vc.mutex);
#else
    pthread_mutex_lock(&g_vc.mutex);
#endif

    for (int i = 0; i < VC_FRAME_SAMPLES; i++)
        g_vc.input_fifo[g_vc.input_size++] = in.data[i] / 32768.0;

    if (g_vc.output_size >= VC_FRAME_SAMPLES)
    {
        for (int i = 0; i < VC_FRAME_SAMPLES; i++)
        {
            double v = g_vc.output_fifo[i] * 30000;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out.data[i] = (int16_t)v;
        }

        memmove(g_vc.output_fifo,
                g_vc.output_fifo + VC_FRAME_SAMPLES,
                sizeof(double) * (g_vc.output_size - VC_FRAME_SAMPLES));

        g_vc.output_size -= VC_FRAME_SAMPLES;
    }

#ifdef _WIN32
    LeaveCriticalSection(&g_vc.mutex);
#else
    pthread_mutex_unlock(&g_vc.mutex);
#endif

    return out;
}

void vc_destroy()
{
    g_vc.running = 0;

#ifdef _WIN32
    WaitForSingleObject(g_vc.thread, INFINITE);
    CloseHandle(g_vc.thread);
    DeleteCriticalSection(&g_vc.mutex);
#else
    pthread_join(g_vc.thread, NULL);
    pthread_mutex_destroy(&g_vc.mutex);
#endif
}

/* ================= JNI ================= */

#ifdef __ANDROID__

JNIEXPORT jint JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcInit(JNIEnv* env, jclass clazz) {
    return (jint)vc_init();
}

JNIEXPORT void JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcSetParams(JNIEnv* env, jclass clazz,
    jdouble pitch, jdouble formant) {
    vc_set_params((double)pitch, (double)formant);
}

JNIEXPORT jshortArray JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcProcess(JNIEnv* env, jclass clazz,
    jshortArray input) {
    if (!input || (*env)->GetArrayLength(env, input) != VC_FRAME_SAMPLES)
        return NULL;

    PCMFrame10ms in_frame;
    (*env)->GetShortArrayRegion(env, input, 0, VC_FRAME_SAMPLES, (jshort*)in_frame.data);

    PCMFrame10ms out_frame = vc_process(in_frame);

    jshortArray result = (*env)->NewShortArray(env, VC_FRAME_SAMPLES);
    if (result)
        (*env)->SetShortArrayRegion(env, result, 0, VC_FRAME_SAMPLES, (jshort*)out_frame.data);

    return result;
}

JNIEXPORT void JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcDestroy(JNIEnv* env, jclass clazz) {
    vc_destroy();
}

#endif