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

#define VC_INPUT_FIFO_CAPACITY   (VC_ANALYSIS_SAMPLES * 12)
#define VC_OUTPUT_FIFO_CAPACITY  (VC_ANALYSIS_SAMPLES * 12)

typedef struct
{
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
    double prev_overlap[VC_OVERLAP_SAMPLES];

    // 上一帧输出（保底防静音洞）
    int16_t last_output_frame[VC_FRAME_SAMPLES];
    int has_last_output_frame;

    // Hann 窗
    double hann_window[VC_ANALYSIS_SAMPLES];

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

} VoiceChanger;

static VoiceChanger g_vc;

/* =========================================================
 * 工具函数
 * ========================================================= */

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

/* =========================================================
 * WORLD 缓冲预分配
 * ========================================================= */

static int alloc_world_buffers(VoiceChanger* vc)
{
    vc->frame_period = 5.0;
    vc->f0_length = GetSamplesForDIO(VC_FS, VC_ANALYSIS_SAMPLES, vc->frame_period);

    CheapTrickOption ct_option;
    InitializeCheapTrickOption(VC_FS, &ct_option);
    vc->fft_size = ct_option.fft_size;
    vc->bins = vc->fft_size / 2 + 1;

    vc->f0 = (double*)malloc(sizeof(double) * vc->f0_length);
    vc->time_axis = (double*)malloc(sizeof(double) * vc->f0_length);

    vc->sp = (double**)malloc(sizeof(double*) * vc->f0_length);
    vc->sp_new = (double**)malloc(sizeof(double*) * vc->f0_length);
    vc->ap = (double**)malloc(sizeof(double*) * vc->f0_length);

    if (!vc->f0 || !vc->time_axis || !vc->sp || !vc->sp_new || !vc->ap)
        return 0;

    for (int i = 0; i < vc->f0_length; i++)
    {
        vc->sp[i] = (double*)malloc(sizeof(double) * vc->bins);
        vc->sp_new[i] = (double*)malloc(sizeof(double) * vc->bins);
        vc->ap[i] = (double*)malloc(sizeof(double) * vc->bins);

        if (!vc->sp[i] || !vc->sp_new[i] || !vc->ap[i])
            return 0;
    }

    return 1;
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
}

/* =========================================================
 * WORLD 处理
 * ========================================================= */

static void process_world(VoiceChanger* vc,
    const double* input,
    double* output,
    double pitch_scale,
    double formant_shift)
{
    int x_length = VC_ANALYSIS_SAMPLES;
    double x[VC_ANALYSIS_SAMPLES];

    for (int i = 0; i < x_length; i++)
        x[i] = input[i] * vc->hann_window[i];

    DioOption dio_option;
    InitializeDioOption(&dio_option);
    dio_option.frame_period = vc->frame_period;

    Dio(x, x_length, VC_FS, &dio_option, vc->time_axis, vc->f0);
    StoneMask(x, x_length, VC_FS, vc->time_axis, vc->f0, vc->f0_length, vc->f0);

    for (int i = 0; i < vc->f0_length; i++)
    {
        if (vc->f0[i] > 0.0)
            vc->f0[i] *= pitch_scale;
    }

    CheapTrickOption ct_option;
    InitializeCheapTrickOption(VC_FS, &ct_option);

    CheapTrick(x, x_length, VC_FS, vc->time_axis, vc->f0, vc->f0_length, &ct_option, vc->sp);

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

    D4C(x, x_length, VC_FS, vc->time_axis, vc->f0, vc->f0_length,
        vc->fft_size, &d4c_option, vc->ap);

    Synthesis(vc->f0, vc->f0_length, vc->sp_new, vc->ap,
        vc->fft_size, vc->frame_period, VC_FS, x_length, output);

    for (int i = 0; i < x_length; i++)
        output[i] = soft_clip(output[i]);
}

/* =========================================================
 * 处理线程
 * ========================================================= */

#ifdef _WIN32
static DWORD WINAPI world_thread(LPVOID arg)
#else
static void* world_thread(void* arg)
#endif
{
    VoiceChanger* vc = (VoiceChanger*)arg;

    double block[VC_ANALYSIS_SAMPLES];
    double out[VC_ANALYSIS_SAMPLES];
    double curr_step[VC_STEP_SAMPLES];

    while (1)
    {
        double local_pitch;
        double local_formant;

#ifdef _WIN32
        EnterCriticalSection(&vc->mutex);
        while (vc->running && vc->input_size < VC_STEP_SAMPLES)
            SleepConditionVariableCS(&vc->cond, &vc->mutex, INFINITE);

        if (!vc->running)
        {
            LeaveCriticalSection(&vc->mutex);
            break;
        }
#else
        pthread_mutex_lock(&vc->mutex);
        while (vc->running && vc->input_size < VC_STEP_SAMPLES)
            pthread_cond_wait(&vc->cond, &vc->mutex);

        if (!vc->running)
        {
            pthread_mutex_unlock(&vc->mutex);
            break;
        }
#endif

        local_pitch = vc->pitch_scale;
        local_formant = vc->formant_shift;

        memcpy(block, vc->prev_overlap, sizeof(double) * VC_OVERLAP_SAMPLES);

        input_ring_pop(vc, curr_step, VC_STEP_SAMPLES);
        memcpy(block + VC_OVERLAP_SAMPLES, curr_step, sizeof(double) * VC_STEP_SAMPLES);

        memcpy(vc->prev_overlap,
            curr_step + (VC_STEP_SAMPLES - VC_OVERLAP_SAMPLES),
            sizeof(double) * VC_OVERLAP_SAMPLES);

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

        output_ring_push(vc, out + VC_OVERLAP_SAMPLES, VC_STEP_SAMPLES);

#ifdef _WIN32
        LeaveCriticalSection(&vc->mutex);
#else
        pthread_mutex_unlock(&vc->mutex);
#endif
    }

    return 0;
}

/* =========================================================
 * API
 * ========================================================= */

int vc_init(void)
{
    VoiceChanger* vc = &g_vc;
    memset(vc, 0, sizeof(*vc));

    vc->pitch_scale = 1.0;
    vc->formant_shift = 1.0;

    for (int i = 0; i < VC_ANALYSIS_SAMPLES; i++)
        vc->hann_window[i] = 0.5 - 0.5 * cos(2.0 * M_PI * i / (VC_ANALYSIS_SAMPLES - 1));

    ring_reset_input(vc);
    ring_reset_output(vc);
    memset(vc->prev_overlap, 0, sizeof(vc->prev_overlap));
    memset(vc->last_output_frame, 0, sizeof(vc->last_output_frame));
    vc->has_last_output_frame = 0;

    if (!alloc_world_buffers(vc))
    {
        free_world_buffers(vc);
        return 0;
    }

    vc->running = 1;

#ifdef _WIN32
    InitializeCriticalSection(&vc->mutex);
    InitializeConditionVariable(&vc->cond);

    vc->thread = CreateThread(NULL, 0, world_thread, vc, 0, NULL);
    if (vc->thread == NULL)
    {
        vc->running = 0;
        DeleteCriticalSection(&vc->mutex);
        free_world_buffers(vc);
        return 0;
    }
#else
    if (pthread_mutex_init(&vc->mutex, NULL) != 0)
    {
        vc->running = 0;
        free_world_buffers(vc);
        return 0;
    }

    if (pthread_cond_init(&vc->cond, NULL) != 0)
    {
        pthread_mutex_destroy(&vc->mutex);
        vc->running = 0;
        free_world_buffers(vc);
        return 0;
    }

    if (pthread_create(&vc->thread, NULL, world_thread, vc) != 0)
    {
        pthread_cond_destroy(&vc->cond);
        pthread_mutex_destroy(&vc->mutex);
        vc->running = 0;
        free_world_buffers(vc);
        return 0;
    }
#endif

    return 1;
}

void vc_set_params(double pitch, double formant)
{
    VoiceChanger* vc = &g_vc;

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

#ifdef _WIN32
    EnterCriticalSection(&vc->mutex);
#else
    pthread_mutex_lock(&vc->mutex);
#endif

    for (int i = 0; i < VC_FRAME_SAMPLES; i++)
    {
        double x = (double)in.data[i] / 32768.0;
        input_ring_push(vc, &x, 1);
    }

    if (vc->input_size >= VC_STEP_SAMPLES)
    {
#ifdef _WIN32
        WakeConditionVariable(&vc->cond);
#else
        pthread_cond_signal(&vc->cond);
#endif
    }

    if (vc->output_size >= VC_FRAME_SAMPLES)
    {
        output_ring_pop(vc, out_tmp, VC_FRAME_SAMPLES);

        for (int i = 0; i < VC_FRAME_SAMPLES; i++)
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
            memcpy(out.data, vc->last_output_frame, sizeof(vc->last_output_frame));
        }
        else
        {
            memset(out.data, 0, sizeof(out.data));
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

#ifdef _WIN32
    EnterCriticalSection(&vc->mutex);
    vc->running = 0;
    WakeAllConditionVariable(&vc->cond);
    LeaveCriticalSection(&vc->mutex);

    WaitForSingleObject(vc->thread, INFINITE);
    CloseHandle(vc->thread);
    DeleteCriticalSection(&vc->mutex);
#else
    pthread_mutex_lock(&vc->mutex);
    vc->running = 0;
    pthread_cond_broadcast(&vc->cond);
    pthread_mutex_unlock(&vc->mutex);

    pthread_join(vc->thread, NULL);
    pthread_cond_destroy(&vc->cond);
    pthread_mutex_destroy(&vc->mutex);
#endif

    free_world_buffers(vc);
}

/* ================= JNI ================= */

#ifdef __ANDROID__

JNIEXPORT jint JNICALL
Java_com_banya_anona_ipc_utils_VoiceChangerNative_vcInit(
    JNIEnv* env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return (jint)vc_init();
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

    if (input == NULL)
        return NULL;

    jsize len = (*env)->GetArrayLength(env, input);
    if (len != VC_FRAME_SAMPLES)
        return NULL;

    PCMFrame10ms in_frame;
    (*env)->GetShortArrayRegion(env, input, 0, VC_FRAME_SAMPLES,
        (jshort*)in_frame.data);

    PCMFrame10ms out_frame = vc_process(in_frame);

    jshortArray result = (*env)->NewShortArray(env, VC_FRAME_SAMPLES);
    if (result == NULL)
        return NULL;

    (*env)->SetShortArrayRegion(env, result, 0, VC_FRAME_SAMPLES,
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