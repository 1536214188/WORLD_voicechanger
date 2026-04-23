#include "voicechanger.h"

// 引入 WORLD 算法头文件
#include "world/dio.h"
#include "world/stonemask.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/synthesis.h"

#include <cmath>
#include <algorithm>
#include <cstring>
// ==== 【新增】显式实现构造和析构函数 ====
VoiceChanger::VoiceChanger() {}
VoiceChanger::~VoiceChanger() {}
// ==========================================

bool VoiceChanger::init()
{
    // 清空缓冲队列
    in_fifo.clear();
    out_fifo.clear();

    // 预分配内存以避免频繁扩容导致的性能损耗
    in_fifo.reserve(analysis_samples * 2);
    out_fifo.reserve(analysis_samples * 2);

    // 初始化重叠历史缓存为 0 (静音)
    prev_in_overlap.assign(overlap_samples, 0.0);
    prev_out_overlap.assign(overlap_samples, 0.0);

    return true;
}

void VoiceChanger::setParams(double pitch, double formant)
{
    pitch_scale = pitch;
    formant_shift = formant;
}

std::vector<int16_t> VoiceChanger::process(const std::vector<int16_t>& input_pcm)
{
    // 1. 将输入的 20ms PCM 数据归一化为 double 并推入输入队列
    for (size_t i = 0; i < input_pcm.size(); ++i)
    {
        in_fifo.push_back(input_pcm[i] / 32768.0);
    }

    // 2. 当积攒够了内部步长 (180ms / 8640个点) 时，触发一次统一处理
    if (in_fifo.size() >= step_samples)
    {
        // 准备 200ms 的 WORLD 处理块 (9600)
        std::vector<double> block(analysis_samples, 0.0);

        // 拼装前 20ms：取自上一次留下来的历史缓存
        for (int i = 0; i < overlap_samples; ++i)
        {
            block[i] = prev_in_overlap[i];
        }

        // 拼装后 180ms：取自当前输入队列
        for (int i = 0; i < step_samples; ++i)
        {
            block[overlap_samples + i] = in_fifo[i];
        }

        // 保存当前输入的最后 20ms，留作下一次拼接的头部
        for (int i = 0; i < overlap_samples; ++i)
        {
            prev_in_overlap[i] = in_fifo[step_samples - overlap_samples + i];
        }

        // 从输入队列中移除已经消费掉的 180ms 数据
        in_fifo.erase(in_fifo.begin(), in_fifo.begin() + step_samples);

        // ==========================================
        // 执行 WORLD 核心处理，耗时操作都在这里
        // ==========================================
        std::vector<double> processed_block = process_world(block);

        // ==========================================
        // 核心平滑过渡 (Crossfade) 逻辑
        // 消除 200ms 拼接缝隙带来的“咔哒”爆音
        // ==========================================
        for (int i = 0; i < overlap_samples; ++i)
        {
            double fade_in = (double)i / overlap_samples;
            double fade_out = 1.0 - fade_in;
            processed_block[i] = (processed_block[i] * fade_in) + (prev_out_overlap[i] * fade_out);
        }

        // 将平滑处理后的前 180ms 存入输出队列
        for (int i = 0; i < step_samples; ++i)
        {
            out_fifo.push_back(processed_block[i]);
        }

        // 截取 WORLD 产出的最后 20ms 留作下一次融合的尾巴
        for (int i = 0; i < overlap_samples; ++i)
        {
            prev_out_overlap[i] = processed_block[step_samples + i];
        }
    }

    // 3. 严格返回 20ms 数据给外部 App
    std::vector<int16_t> output(frame_samples, 0);

    if (out_fifo.size() >= frame_samples)
    {
        for (int i = 0; i < frame_samples; ++i)
        {
            double val = out_fifo[i] * 32767.0;

            // 防止数值溢出导致爆音
            if (val > 32767.0) val = 32767.0;
            if (val < -32768.0) val = -32768.0;

            output[i] = static_cast<int16_t>(val);
        }
        // 弹出已返回的 20ms
        out_fifo.erase(out_fifo.begin(), out_fifo.begin() + frame_samples);
    }
    else
    {
        // 初始阶段如果 out_fifo 数据还没积攒够，默认输出静音 (0)
        std::memset(output.data(), 0, frame_samples * sizeof(int16_t));
    }

    return output;
}

std::vector<double> VoiceChanger::process_world(const std::vector<double>& input)
{
    // WORLD 要求输入内存连续，使用 vector.data() 是安全的
    const double* x = input.data();
    int x_length = analysis_samples;

    double frame_period = 5.0; // 5.0 ms 的帧移
    int f0_length = GetSamplesForDIO(fs, x_length, frame_period);

    // 内存分配：F0 与 时间轴
    std::vector<double> f0(f0_length);
    std::vector<double> time_axis(f0_length);

    // 1. DIO (基频提取)
    DioOption dio_option;
    InitializeDioOption(&dio_option);
    dio_option.frame_period = frame_period;
    Dio(x, x_length, fs, &dio_option, time_axis.data(), f0.data());

    // 2. StoneMask (基频细化)
    StoneMask(x, x_length, fs, time_axis.data(), f0.data(), f0_length, f0.data());

    // 修改音高 (Pitch)
    for (int i = 0; i < f0_length; ++i)
    {
        if (f0[i] > 0.0)
        {
            f0[i] *= pitch_scale;
        }
    }

    // 3. CheapTrick (提取频谱包络)
    CheapTrickOption ct_option;
    InitializeCheapTrickOption(fs, &ct_option);
    int fft_size = ct_option.fft_size;
    int bins = fft_size / 2 + 1;

    // 二维数组模拟 (频谱图)
    std::vector<std::vector<double>> sp(f0_length, std::vector<double>(bins));
    std::vector<std::vector<double>> sp_new(f0_length, std::vector<double>(bins));

    // WORLD C 接口需要的指针数组
    std::vector<double*> sp_ptr(f0_length);
    std::vector<double*> sp_new_ptr(f0_length);
    for (int i = 0; i < f0_length; ++i)
    {
        sp_ptr[i] = sp[i].data();
        sp_new_ptr[i] = sp_new[i].data();
    }

    CheapTrick(x, x_length, fs, time_axis.data(), f0.data(), f0_length, &ct_option, sp_ptr.data());

    // 修改共振峰 (Formant / 音色)
    for (int i = 0; i < f0_length; ++i)
    {
        for (int j = 0; j < bins; ++j)
        {
            int src_index = static_cast<int>(j * formant_shift);
            if (src_index < bins)
            {
                sp_new[i][j] = sp[i][src_index];
            }
            else
            {
                sp_new[i][j] = sp[i][bins - 1];
            }
        }
    }

    // 4. D4C (提取非周期性参数)
    D4COption d4c_option;
    InitializeD4COption(&d4c_option);

    std::vector<std::vector<double>> ap(f0_length, std::vector<double>(bins));
    std::vector<double*> ap_ptr(f0_length);
    for (int i = 0; i < f0_length; ++i)
    {
        ap_ptr[i] = ap[i].data();
    }

    D4C(x, x_length, fs, time_axis.data(), f0.data(), f0_length, fft_size, &d4c_option, ap_ptr.data());

    // 5. Synthesis (声音合成)
    std::vector<double> y(x_length, 0.0);
    Synthesis(f0.data(), f0_length, sp_new_ptr.data(), ap_ptr.data(), fft_size, frame_period, fs, x_length, y.data());

    return y;
}

void VoiceChanger::destroy()
{
    in_fifo.clear();
    out_fifo.clear();
    prev_in_overlap.clear();
    prev_out_overlap.clear();
}