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

#ifdef __ANDROID__
#include <jni.h>
#endif

#define VC_OVERLAP_SAMPLES_MAX  ((VC_MAX_SAMPLE_RATE * VC_OVERLAP_MS + 500) / 1000)
#define VC_STEP_SAMPLES_MAX     ((VC_MAX_SAMPLE_RATE * VC_STEP_MS + 500) / 1000)
#define VC_ANALYSIS_SAMPLES_MAX (VC_OVERLAP_SAMPLES_MAX + VC_STEP_SAMPLES_MAX)
#define VC_INPUT_FIFO_CAPACITY  (VC_ANALYSIS_SAMPLES_MAX * 12)
#define VC_OUTPUT_FIFO_CAPACITY (VC_ANALYSIS_SAMPLES_MAX * 12)

typedef struct
{
    int sample_rate;
    int frame_samples;
    int overlap_samples;
    int step_samples;
    int analysis_samples;

    double pitch_scale;
    double formant_shift;

    // 输入环形缓冲
    double input_fifo[VC_INPUT_FIFO_CAPACITY];
    int input_read_pos;
    int input_write_pos;
    int input_size;

    // 输出环形缓冲
    double output_fifo[VC_OUTPUT_FIFO_CAPACITY];
    int output_read_pos;
    int output_write_pos;
    int output_size;

    // 前一块 overlap
    double prev_overlap[VC_OVERLAP_SAMPLES_MAX];

    // 上一帧输出（保底防静音洞）
    int16_t last_output_frame[VC_FRAME_SAMPLES];
    int has_last_output_frame;

    // Hann 窗和后台线程临时缓冲
    double hann_window[VC_ANALYSIS_SAMPLES_MAX];
    double thread_block[VC_ANALYSIS_SAMPLES_MAX];
    double thread_out[VC_ANALYSIS_SAMPLES_MAX];
    double thread_step[VC_STEP_SAMPLES_MAX];

    // WORLD 预分配缓存
    int f0_length;
    int fft_size;
    int bins;
    double frame_period;

    double* f0;
    double* time_axis;
    double** sp;
    double** sp_new;
    double** ap;

#ifdef _WIN32
    HANDLE thread;
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond;
#else
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif

    int running;
    int initialized;
    int mutex_initialized;
    int cond_initialized;
    int thread_started;

} VoiceChanger;

static VoiceChanger g_vc;
static double g_default_pitch = 1.0;
static double g_default_formant = 1.0;

static inline double soft_clip(double x)
{
    if (x > 1.25) x = 1.25;
    if (x < -1.25) x = -1.25;
    return tanh(x * 0.8);
}

static inline int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int samples_from_ms(int sample_rate, int ms)
{
    return (sample_rate * ms + 500) / 1000;
}

static int is_valid_sample_rate(int sample_rate)
{
    return sample_rate >= VC_MIN_SAMPLE_RATE &&
        sample_rate <= VC_MAX_SAMPLE_RATE;
}

static void ring_reset_input(VoiceChanger* vc)
{
    vc->input_read_pos = 0;
    vc->input_write_pos = 0;
    vc->input_size = 0;
}

static void ring_reset_output(VoiceChanger* vc)
{
    vc->output_read_pos = 0;
    vc->output_write_pos = 0;
    vc->output_size = 0;
}

static int input_ring_push(VoiceChanger* vc, const double* src, int n)
{
    if (n > VC_INPUT_FIFO_CAPACITY)
    {
        src += (n - VC_INPUT_FIFO_CAPACITY);
        n = VC_INPUT_FIFO_CAPACITY;
    }

    while (vc->input_size + n > VC_INPUT_FIFO_CAPACITY)
    {
        vc->input_read_pos = (vc->input_read_pos + 1) % VC_INPUT_FIFO_CAPACITY;
        vc->input_size--;
    }

    for (int i = 0; i < n; i++)
    {
        vc->input_fifo[vc->input_write_pos] = src[i];
        vc->input_write_pos = (vc->input_write_pos + 1) % VC_INPUT_FIFO_CAPACITY;
    }

    vc->input_size += n;
    return n;
}

static int input_ring_pop(VoiceChanger* vc, double* dst, int n)
{
    int can_read = min_int(n, vc->input_size);
    for (int i = 0; i < can_read; i++)
    {
        dst[i] = vc->input_fifo[vc->input_read_pos];
        vc->input_read_pos = (vc->input_read_pos + 1) % VC_INPUT_FIFO_CAPACITY;
    }
    vc->input_size -= can_read;
    return can_read;
}

static int output_ring_push(VoiceChanger* vc, const double* src, int n)
{
    if (n > VC_OUTPUT_FIFO_CAPACITY)
    {
        src += (n - VC_OUTPUT_FIFO_CAPACITY);
        n = VC_OUTPUT_FIFO_CAPACITY;
    }

    while (vc->output_size + n > VC_OUTPUT_FIFO_CAPACITY)
    {
        vc->output_read_pos = (vc->output_read_pos + 1) % VC_OUTPUT_FIFO_CAPACITY;
        vc->output_size--;
    }

    for (int i = 0; i < n; i++)
    {
        vc->output_fifo[vc->output_write_pos] = src[i];
        vc->output_write_pos = (vc->output_write_pos + 1) % VC_OUTPUT_FIFO_CAPACITY;
    }

    vc->output_size += n;
    return n;
}

static int output_ring_pop(VoiceChanger* vc, double* dst, int n)
{
    int can_read = min_int(n, vc->output_size);
    for (int i = 0; i < can_read; i++)
    {
        dst[i] = vc->output_fifo[vc->output_read_pos];
        vc->output_read_pos = (vc->output_read_pos + 1) % VC_OUTPUT_FIFO_CAPACITY;
    }
    vc->output_read_pos %= VC_OUTPUT_FIFO_CAPACITY;
    vc->output_size -= can_read;
    return can_read;
}

static void free_world_buffers(VoiceChanger* vc)
{
    if (vc->sp)
    {
        for (int i = 0; i < vc->f0_length; i++)
            free(vc->sp[i]);
        free(vc->sp);
        vc->sp = NULL;
    }

    if (vc->sp_new)
    {
        for (int i = 0; i < vc->f0_length; i++)
            free(vc->sp_new[i]);
        free(vc->sp_new);
        vc->sp_new = NULL;
    }

    if (vc->ap)
    {
        for (int i = 0; i < vc->f0_length; i++)
            free(vc->ap[i]);
        free(vc->ap);
        vc->ap = NULL;
    }

    free(vc->f0);
    vc->f0 = NULL;

    free(vc->time_axis);
    vc->time_axis = NULL;

    vc->f0_length = 0;
    vc->fft_size = 0;
    vc->bins = 0;
}

static int alloc_world_buffers(VoiceChanger* vc)
{
    vc->frame_period = 5.0;
    vc->f0_length = GetSamplesForDIO(vc->sample_rate,
        vc->analysis_samples, vc->frame_period);

    CheapTrickOption ct_option;
    InitializeCheapTrickOption(vc->sample_rate, &ct_option);
    vc->fft_size = ct_option.fft_size;
    vc->bins = vc->fft_size / 2 + 1;

    vc->f0 = (double*)calloc((size_t)vc->f0_length, sizeof(double));
    vc->time_axis = (double*)calloc((size_t)vc->f0_length, sizeof(double));

    vc->sp = (double**)calloc((size_t)vc->f0_length, sizeof(double*));
    vc->sp_new = (double**)calloc((size_t)vc->f0_length, sizeof(double*));
    vc->ap = (double**)calloc((size_t)vc->f0_length, sizeof(double*));

    if (!vc->f0 || !vc->time_axis || !vc->sp || !vc->sp_new || !vc->ap)
        return 0;

    for (int i = 0; i < vc->f0_length; i++)
    {
        vc->sp[i] = (double*)malloc(sizeof(double) * (size_t)vc->bins);
        vc->sp_new[i] = (double*)malloc(sizeof(double) * (size_t)vc->bins);
        vc->ap[i] = (double*)malloc(sizeof(double) * (size_t)vc->bins);

        if (!vc->sp[i] || !vc->sp_new[i] || !vc->ap[i])
            return 0;
    }

    return 1;
}

static void process_world(VoiceChanger* vc,
    const double* input,
    double* output,
    double pitch_scale,
    double formant_shift)
{
    int x_length = vc->analysis_samples;
    double x[VC_ANALYSIS_SAMPLES_MAX];

    for (int i = 0; i < x_length; i++)
        x[i] = input[i] * vc->hann_window[i];

    DioOption dio_option;
    InitializeDioOption(&dio_option);
    dio_option.frame_period = vc->frame_period;

    Dio(x, x_length, vc->sample_rate, &dio_option, vc->time_axis, vc->f0);
    StoneMask(x, x_length, vc->sample_rate, vc->time_axis,
        vc->f0, vc->f0_length, vc->f0);

    for (int i = 0; i < vc->f0_length; i++)
    {
        if (vc->f0[i] > 0.0)
            vc->f0[i] *= pitch_scale;
    }

    CheapTrickOption ct_option;
    InitializeCheapTrickOption(vc->sample_rate, &ct_option);

    CheapTrick(x, x_length, vc->sample_rate, vc->time_axis,
        vc->f0, vc->f0_length, &ct_option, vc->sp);

    for (int i = 0; i < vc->f0_length; i++)
    {
        for (int j = 0; j < vc->bins; j++)
        {
            double src = (double)j * formant_shift;
            int s0 = (int)src;
            int s1 = s0 + 1;

            if (s0 < 0) s0 = 0;
            if (s1 < 0) s1 = 0;
            if (s0 >= vc->bins) s0 = vc->bins - 1;
            if (s1 >= vc->bins) s1 = vc->bins - 1;

            double w = src - (double)s0;
            if (w < 0.0) w = 0.0;
            if (w > 1.0) w = 1.0;

            vc->sp_new[i][j] =
                vc->sp[i][s0] * (1.0 - w) +
                vc->sp[i][s1] * w;
        }
    }

    D4COption d4c_option;
    InitializeD4COption(&d4c_option);

    D4C(x, x_length, vc->sample_rate, vc->time_axis,
        vc->f0, vc->f0_length, vc->fft_size, &d4c_option, vc->ap);

    Synthesis(vc->f0, vc->f0_length,
        (const double* const*)vc->sp_new, (const double* const*)vc->ap,
        vc->fft_size, vc->frame_period, vc->sample_rate, x_length, output);

    for (int i = 0; i < x_length; i++)
        output[i] = soft_clip(output[i]);
}

#ifdef _WIN32
static DWORD WINAPI world_thread(LPVOID arg)
#else
static void* world_thread(void* arg)
#endif
{
    VoiceChanger* vc = (VoiceChanger*)arg;

    while (1)
    {
        double local_pitch;
        double local_formant;

#ifdef _WIN32
        EnterCriticalSection(&vc->mutex);
        while (vc->running && vc->input_size < vc->step_samples)
            SleepConditionVariableCS(&vc->cond, &vc->mutex, INFINITE);

        if (!vc->running)
        {
            LeaveCriticalSection(&vc->mutex);
            break;
        }
#else
        pthread_mutex_lock(&vc->mutex);
        while (vc->running && vc->input_size < vc->step_samples)
            pthread_cond_wait(&vc->cond, &vc->mutex);

        if (!vc->running)
        {
            pthread_mutex_unlock(&vc->mutex);
            break;
        }
#endif

        local_pitch = vc->pitch_scale;
        local_formant = vc->formant_shift;

        memcpy(vc->thread_block, vc->prev_overlap,
            sizeof(double) * (size_t)vc->overlap_samples);

        input_ring_pop(vc, vc->thread_step, vc->step_samples);
        memcpy(vc->thread_block + vc->overlap_samples, vc->thread_step,
            sizeof(double) * (size_t)vc->step_samples);

        memcpy(vc->prev_overlap,
            vc->thread_step + (vc->step_samples - vc->overlap_samples),
            sizeof(double) * (size_t)vc->overlap_samples);

#ifdef _WIN32
        LeaveCriticalSection(&vc->mutex);
#else
        pthread_mutex_unlock(&vc->mutex);
#endif

        process_world(vc, vc->thread_block, vc->thread_out,
            local_pitch, local_formant);

#ifdef _WIN32
        EnterCriticalSection(&vc->mutex);
#else
        pthread_mutex_lock(&vc->mutex);
#endif

        output_ring_push(vc, vc->thread_out + vc->overlap_samples,
            vc->step_samples);

#ifdef _WIN32
        LeaveCriticalSection(&vc->mutex);
#else
        pthread_mutex_unlock(&vc->mutex);
#endif
    }

    return 0;
}

int vc_init(int sample_rate)
{
    if (!is_valid_sample_rate(sample_rate))
        return 0;

    if (g_vc.initialized || g_vc.thread_started ||
        g_vc.mutex_initialized || g_vc.cond_initialized)
        vc_destroy();

    VoiceChanger* vc = &g_vc;
    memset(vc, 0, sizeof(*vc));

    vc->sample_rate = sample_rate;
    vc->frame_samples = samples_from_ms(sample_rate, VC_FRAME_MS);
    vc->overlap_samples = samples_from_ms(sample_rate, VC_OVERLAP_MS);
    vc->step_samples = samples_from_ms(sample_rate, VC_STEP_MS);
    vc->analysis_samples = vc->overlap_samples + vc->step_samples;

    if (vc->frame_samples <= 0 || vc->frame_samples > VC_FRAME_SAMPLES ||
        vc->overlap_samples <= 0 || vc->overlap_samples > VC_OVERLAP_SAMPLES_MAX ||
        vc->step_samples <= vc->overlap_samples ||
        vc->step_samples > VC_STEP_SAMPLES_MAX ||
        vc->analysis_samples > VC_ANALYSIS_SAMPLES_MAX)
        return 0;

    vc->pitch_scale = g_default_pitch;
    vc->formant_shift = g_default_formant;

    for (int i = 0; i < vc->analysis_samples; i++)
    {
        vc->hann_window[i] = 0.5 - 0.5 *
            cos(2.0 * M_PI * i / (double)(vc->analysis_samples - 1));
    }

    ring_reset_input(vc);
    ring_reset_output(vc);
    memset(vc->prev_overlap, 0,
        sizeof(double) * (size_t)vc->overlap_samples);
    memset(vc->last_output_frame, 0, sizeof(vc->last_output_frame));
    vc->has_last_output_frame = 0;

    if (!alloc_world_buffers(vc))
    {
        free_world_buffers(vc);
        memset(vc, 0, sizeof(*vc));
        return 0;
    }

    vc->running = 1;

#ifdef _WIN32
    InitializeCriticalSection(&vc->mutex);
    vc->mutex_initialized = 1;
    InitializeConditionVariable(&vc->cond);
    vc->cond_initialized = 1;

    vc->thread = CreateThread(NULL, 0, world_thread, vc, 0, NULL);
    if (vc->thread == NULL)
    {
        vc->running = 0;
        vc_destroy();
        return 0;
    }
    vc->thread_started = 1;
#else
    if (pthread_mutex_init(&vc->mutex, NULL) != 0)
    {
        vc->running = 0;
        free_world_buffers(vc);
        memset(vc, 0, sizeof(*vc));
        return 0;
    }
    vc->mutex_initialized = 1;

    if (pthread_cond_init(&vc->cond, NULL) != 0)
    {
        vc->running = 0;
        vc_destroy();
        return 0;
    }
    vc->cond_initialized = 1;

    if (pthread_create(&vc->thread, NULL, world_thread, vc) != 0)
    {
        vc->running = 0;
        vc_destroy();
        return 0;
    }
    vc->thread_started = 1;
#endif

    vc->initialized = 1;
    return 1;
}

void vc_set_params(double pitch, double formant)
{
    g_default_pitch = pitch;
    g_default_formant = formant;

    VoiceChanger* vc = &g_vc;
    if (!vc->initialized)
        return;

#ifdef _WIN32
    EnterCriticalSection(&vc->mutex);
#else
    pthread_mutex_lock(&vc->mutex);
#endif

    vc->pitch_scale = pitch;
    vc->formant_shift = formant;

#ifdef _WIN32
    LeaveCriticalSection(&vc->mutex);
#else
    pthread_mutex_unlock(&vc->mutex);
#endif
}

PCMFrame10ms vc_process(PCMFrame10ms in)
{
    VoiceChanger* vc = &g_vc;
    PCMFrame10ms out;
    double out_tmp[VC_FRAME_SAMPLES];

    memset(&out, 0, sizeof(out));
    if (!vc->initialized)
        return out;

#ifdef _WIN32
    EnterCriticalSection(&vc->mutex);
#else
    pthread_mutex_lock(&vc->mutex);
#endif

    for (int i = 0; i < vc->frame_samples; i++)
    {
        double x = (double)in.data[i] / 32768.0;
        input_ring_push(vc, &x, 1);
    }

    if (vc->input_size >= vc->step_samples)
    {
#ifdef _WIN32
        WakeConditionVariable(&vc->cond);
#else
        pthread_cond_signal(&vc->cond);
#endif
    }

    if (vc->output_size >= vc->frame_samples)
    {
        output_ring_pop(vc, out_tmp, vc->frame_samples);

        for (int i = 0; i < vc->frame_samples; i++)
        {
            double v = out_tmp[i] * 28000.0;
            if (v > 32767.0) v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            out.data[i] = (int16_t)v;
            vc->last_output_frame[i] = out.data[i];
        }
        vc->has_last_output_frame = 1;
    }
    else
    {
        if (vc->has_last_output_frame)
        {
            memcpy(out.data, vc->last_output_frame,
                sizeof(int16_t) * (size_t)vc->frame_samples);
        }
        else
        {
            memset(out.data, 0,
                sizeof(int16_t) * (size_t)vc->frame_samples);
        }
    }

#ifdef _WIN32
    LeaveCriticalSection(&vc->mutex);
#else
    pthread_mutex_unlock(&vc->mutex);
#endif

    return out;
}

void vc_destroy(void)
{
    VoiceChanger* vc = &g_vc;

    if (vc->thread_started)
    {
#ifdef _WIN32
        EnterCriticalSection(&vc->mutex);
        vc->running = 0;
        WakeAllConditionVariable(&vc->cond);
        LeaveCriticalSection(&vc->mutex);

        WaitForSingleObject(vc->thread, INFINITE);
        CloseHandle(vc->thread);
#else
        pthread_mutex_lock(&vc->mutex);
        vc->running = 0;
        pthread_cond_broadcast(&vc->cond);
        pthread_mutex_unlock(&vc->mutex);

        pthread_join(vc->thread, NULL);
#endif
        vc->thread_started = 0;
    }

#ifndef _WIN32
    if (vc->cond_initialized)
        pthread_cond_destroy(&vc->cond);
    if (vc->mutex_initialized)
        pthread_mutex_destroy(&vc->mutex);
#else
    if (vc->mutex_initialized)
        DeleteCriticalSection(&vc->mutex);
#endif

    free_world_buffers(vc);
    memset(vc, 0, sizeof(*vc));
}

#ifdef __ANDROID__

JNIEXPORT jint JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcInit(
    JNIEnv* env, jclass clazz, jint sampleRate)
{
    (void)env;
    (void)clazz;
    return (jint)vc_init((int)sampleRate);
}

JNIEXPORT void JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcSetParams(
    JNIEnv* env, jclass clazz,
    jdouble pitch, jdouble formant)
{
    (void)env;
    (void)clazz;
    vc_set_params((double)pitch, (double)formant);
}

JNIEXPORT jshortArray JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcProcess(
    JNIEnv* env, jclass clazz,
    jshortArray input)
{
    (void)clazz;

    if (input == NULL || !g_vc.initialized)
        return NULL;

    jsize len = (*env)->GetArrayLength(env, input);
    if (len != g_vc.frame_samples)
        return NULL;

    PCMFrame10ms in_frame;
    memset(&in_frame, 0, sizeof(in_frame));
    (*env)->GetShortArrayRegion(env, input, 0, len,
        (jshort*)in_frame.data);

    PCMFrame10ms out_frame = vc_process(in_frame);

    jshortArray result = (*env)->NewShortArray(env, len);
    if (result == NULL)
        return NULL;

    (*env)->SetShortArrayRegion(env, result, 0, len,
        (jshort*)out_frame.data);

    return result;
}

JNIEXPORT void JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcDestroy(
    JNIEnv* env, jclass clazz)
{
    (void)env;
    (void)clazz;
    vc_destroy();
}

#endif
