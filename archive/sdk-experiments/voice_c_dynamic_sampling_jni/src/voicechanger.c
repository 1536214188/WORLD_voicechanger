#include "voicechanger.h"
#define M_PI 3.14159265358979323846

#include "world/dio.h"
#include "world/stonemask.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/synthesis.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#endif

#define VC_MIN_SAMPLE_RATE 8000
#define VC_MAX_SAMPLE_RATE 192000
#define VC_FIFO_BLOCKS 12
#define VC_OUTPUT_GAIN 28000.0

typedef struct
{
    int sample_rate;
    int frame_samples;
    int overlap_samples;
    int step_samples;
    int analysis_samples;
    int input_fifo_capacity;
    int output_fifo_capacity;

    double pitch_scale;
    double formant_shift;

    double* input_fifo;
    int input_read_pos;
    int input_write_pos;
    int input_size;

    double* output_fifo;
    int output_read_pos;
    int output_write_pos;
    int output_size;

    double* prev_overlap;
    int16_t* last_output_frame;
    int has_last_output_frame;
    double* hann_window;

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
    return sample_rate >= VC_MIN_SAMPLE_RATE && sample_rate <= VC_MAX_SAMPLE_RATE;
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

static int ring_push(double* fifo, int capacity, int* read_pos, int* write_pos,
    int* size, const double* src, int n)
{
    if (!fifo || !src || capacity <= 0 || n <= 0)
        return 0;

    if (n > capacity)
    {
        src += (n - capacity);
        n = capacity;
    }

    while (*size + n > capacity)
    {
        *read_pos = (*read_pos + 1) % capacity;
        (*size)--;
    }

    for (int i = 0; i < n; i++)
    {
        fifo[*write_pos] = src[i];
        *write_pos = (*write_pos + 1) % capacity;
    }

    *size += n;
    return n;
}

static int ring_pop(double* fifo, int capacity, int* read_pos, int* size,
    double* dst, int n)
{
    if (!fifo || !dst || capacity <= 0 || n <= 0)
        return 0;

    int can_read = min_int(n, *size);
    for (int i = 0; i < can_read; i++)
    {
        dst[i] = fifo[*read_pos];
        *read_pos = (*read_pos + 1) % capacity;
    }

    *size -= can_read;
    return can_read;
}

static int input_ring_push(VoiceChanger* vc, const double* src, int n)
{
    return ring_push(vc->input_fifo, vc->input_fifo_capacity,
        &vc->input_read_pos, &vc->input_write_pos, &vc->input_size, src, n);
}

static int input_ring_pop(VoiceChanger* vc, double* dst, int n)
{
    return ring_pop(vc->input_fifo, vc->input_fifo_capacity,
        &vc->input_read_pos, &vc->input_size, dst, n);
}

static int output_ring_push(VoiceChanger* vc, const double* src, int n)
{
    return ring_push(vc->output_fifo, vc->output_fifo_capacity,
        &vc->output_read_pos, &vc->output_write_pos, &vc->output_size, src, n);
}

static int output_ring_pop(VoiceChanger* vc, double* dst, int n)
{
    return ring_pop(vc->output_fifo, vc->output_fifo_capacity,
        &vc->output_read_pos, &vc->output_size, dst, n);
}

static void free_world_buffers(VoiceChanger* vc)
{
    if (vc->sp)
    {
        for (int i = 0; i < vc->f0_length; i++)
            free(vc->sp[i]);
        free(vc->sp);
    }

    if (vc->sp_new)
    {
        for (int i = 0; i < vc->f0_length; i++)
            free(vc->sp_new[i]);
        free(vc->sp_new);
    }

    if (vc->ap)
    {
        for (int i = 0; i < vc->f0_length; i++)
            free(vc->ap[i]);
        free(vc->ap);
    }

    free(vc->f0);
    free(vc->time_axis);

    vc->sp = NULL;
    vc->sp_new = NULL;
    vc->ap = NULL;
    vc->f0 = NULL;
    vc->time_axis = NULL;
    vc->f0_length = 0;
}

static void free_dynamic_buffers(VoiceChanger* vc)
{
    free(vc->input_fifo);
    free(vc->output_fifo);
    free(vc->prev_overlap);
    free(vc->last_output_frame);
    free(vc->hann_window);

    vc->input_fifo = NULL;
    vc->output_fifo = NULL;
    vc->prev_overlap = NULL;
    vc->last_output_frame = NULL;
    vc->hann_window = NULL;
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

static int alloc_dynamic_buffers(VoiceChanger* vc)
{
    vc->input_fifo_capacity = vc->analysis_samples * VC_FIFO_BLOCKS;
    vc->output_fifo_capacity = vc->analysis_samples * VC_FIFO_BLOCKS;

    vc->input_fifo = (double*)calloc((size_t)vc->input_fifo_capacity, sizeof(double));
    vc->output_fifo = (double*)calloc((size_t)vc->output_fifo_capacity, sizeof(double));
    vc->prev_overlap = (double*)calloc((size_t)vc->overlap_samples, sizeof(double));
    vc->last_output_frame = (int16_t*)calloc((size_t)vc->frame_samples, sizeof(int16_t));
    vc->hann_window = (double*)malloc(sizeof(double) * (size_t)vc->analysis_samples);

    if (!vc->input_fifo || !vc->output_fifo || !vc->prev_overlap ||
        !vc->last_output_frame || !vc->hann_window)
        return 0;

    for (int i = 0; i < vc->analysis_samples; i++)
    {
        vc->hann_window[i] = 0.5 - 0.5 *
            cos(2.0 * M_PI * i / (double)(vc->analysis_samples - 1));
    }

    return 1;
}

static void process_world(VoiceChanger* vc, const double* input,
    double* output, double pitch_scale, double formant_shift)
{
    int x_length = vc->analysis_samples;
    double* x = (double*)malloc(sizeof(double) * (size_t)x_length);
    if (!x)
    {
        memset(output, 0, sizeof(double) * (size_t)x_length);
        return;
    }

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

    free(x);
}

#ifdef _WIN32
static DWORD WINAPI world_thread(LPVOID arg)
#else
static void* world_thread(void* arg)
#endif
{
    VoiceChanger* vc = (VoiceChanger*)arg;
    double* block = (double*)malloc(sizeof(double) * (size_t)vc->analysis_samples);
    double* out = (double*)malloc(sizeof(double) * (size_t)vc->analysis_samples);
    double* curr_step = (double*)malloc(sizeof(double) * (size_t)vc->step_samples);

    if (!block || !out || !curr_step)
        goto done;

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

        memcpy(block, vc->prev_overlap,
            sizeof(double) * (size_t)vc->overlap_samples);
        input_ring_pop(vc, curr_step, vc->step_samples);
        memcpy(block + vc->overlap_samples, curr_step,
            sizeof(double) * (size_t)vc->step_samples);
        memcpy(vc->prev_overlap,
            curr_step + (vc->step_samples - vc->overlap_samples),
            sizeof(double) * (size_t)vc->overlap_samples);

#ifdef _WIN32
        LeaveCriticalSection(&vc->mutex);
#else
        pthread_mutex_unlock(&vc->mutex);
#endif

        process_world(vc, block, out, local_pitch, local_formant);

#ifdef _WIN32
        EnterCriticalSection(&vc->mutex);
#else
        pthread_mutex_lock(&vc->mutex);
#endif

        output_ring_push(vc, out + vc->overlap_samples, vc->step_samples);

#ifdef _WIN32
        LeaveCriticalSection(&vc->mutex);
#else
        pthread_mutex_unlock(&vc->mutex);
#endif
    }

done:
    free(block);
    free(out);
    free(curr_step);
    return 0;
}

int vc_init(int sample_rate)
{
    if (!is_valid_sample_rate(sample_rate))
        return 0;

    vc_destroy();

    VoiceChanger* vc = &g_vc;
    memset(vc, 0, sizeof(*vc));

    vc->sample_rate = sample_rate;
    vc->frame_samples = samples_from_ms(sample_rate, VC_FRAME_MS);
    vc->overlap_samples = samples_from_ms(sample_rate, VC_OVERLAP_MS);
    vc->step_samples = samples_from_ms(sample_rate, VC_STEP_MS);
    vc->analysis_samples = vc->overlap_samples + vc->step_samples;
    vc->pitch_scale = g_default_pitch;
    vc->formant_shift = g_default_formant;

    if (vc->frame_samples <= 0 || vc->overlap_samples <= 0 ||
        vc->step_samples <= vc->overlap_samples || vc->analysis_samples <= 1)
        return 0;

    if (!alloc_dynamic_buffers(vc) || !alloc_world_buffers(vc))
    {
        free_world_buffers(vc);
        free_dynamic_buffers(vc);
        memset(vc, 0, sizeof(*vc));
        return 0;
    }

    ring_reset_input(vc);
    ring_reset_output(vc);
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
        vc_destroy();
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

int vc_process(const int16_t* input, int input_samples,
    int16_t* output, int output_capacity)
{
    VoiceChanger* vc = &g_vc;
    if (!vc->initialized || !input || !output ||
        input_samples <= 0 || output_capacity < input_samples ||
        input_samples != vc->frame_samples)
        return 0;

    double* out_tmp = (double*)malloc(sizeof(double) * (size_t)input_samples);
    if (!out_tmp)
        return 0;

#ifdef _WIN32
    EnterCriticalSection(&vc->mutex);
#else
    pthread_mutex_lock(&vc->mutex);
#endif

    for (int i = 0; i < input_samples; i++)
    {
        double x = (double)input[i] / 32768.0;
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

    if (vc->output_size >= input_samples)
    {
        output_ring_pop(vc, out_tmp, input_samples);

        for (int i = 0; i < input_samples; i++)
        {
            double v = out_tmp[i] * VC_OUTPUT_GAIN;
            if (v > 32767.0) v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            output[i] = (int16_t)v;
            vc->last_output_frame[i] = output[i];
        }
        vc->has_last_output_frame = 1;
    }
    else if (vc->has_last_output_frame)
    {
        memcpy(output, vc->last_output_frame,
            sizeof(int16_t) * (size_t)input_samples);
    }
    else
    {
        memset(output, 0, sizeof(int16_t) * (size_t)input_samples);
    }

#ifdef _WIN32
    LeaveCriticalSection(&vc->mutex);
#else
    pthread_mutex_unlock(&vc->mutex);
#endif

    free(out_tmp);
    return input_samples;
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
    free_dynamic_buffers(vc);
    memset(vc, 0, sizeof(*vc));
}

int vc_get_sample_rate(void)
{
    return g_vc.initialized ? g_vc.sample_rate : 0;
}

int vc_get_frame_samples(void)
{
    return g_vc.initialized ? g_vc.frame_samples : 0;
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
    JNIEnv* env, jclass clazz, jdouble pitch, jdouble formant)
{
    (void)env;
    (void)clazz;
    vc_set_params((double)pitch, (double)formant);
}

JNIEXPORT jshortArray JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcProcess(
    JNIEnv* env, jclass clazz, jshortArray input)
{
    (void)clazz;

    if (input == NULL)
        return NULL;

    jsize len = (*env)->GetArrayLength(env, input);
    if (len <= 0)
        return NULL;

    int sample_rate = vc_get_sample_rate();
    if (!is_valid_sample_rate(sample_rate))
        return NULL;

    if ((int)len != vc_get_frame_samples())
        return NULL;

    int16_t* in_frame = (int16_t*)malloc(sizeof(int16_t) * (size_t)len);
    int16_t* out_frame = (int16_t*)malloc(sizeof(int16_t) * (size_t)len);
    if (!in_frame || !out_frame)
    {
        free(in_frame);
        free(out_frame);
        return NULL;
    }

    (*env)->GetShortArrayRegion(env, input, 0, len, (jshort*)in_frame);
    int out_len = vc_process(in_frame, (int)len, out_frame, (int)len);

    if (out_len <= 0)
    {
        free(in_frame);
        free(out_frame);
        return NULL;
    }

    jshortArray result = (*env)->NewShortArray(env, out_len);
    if (result != NULL)
    {
        (*env)->SetShortArrayRegion(env, result, 0, out_len,
            (jshort*)out_frame);
    }

    free(in_frame);
    free(out_frame);
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
