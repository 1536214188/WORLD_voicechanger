#pragma once

#include <vector>
#include <cstdint>

// 兼容 Windows DLL 导出的宏定义
#ifdef _WIN32
#ifdef VC_EXPORTS
#define VC_API __declspec(dllexport)
#else
#define VC_API __declspec(dllimport)
#endif
#else
#define VC_API
#endif

class VC_API VoiceChanger
{
public:
    // ==== 【新增】显式声明构造和析构函数 ====
    VoiceChanger();
    ~VoiceChanger();
    // ==========================================

    
    // 初始化变声器
    bool init();

    // 设置变声参数 (pitch_scale: 音高倍率, formant_shift: 共振峰倍率)
    void setParams(double pitch_scale, double formant_shift);

    // 核心处理接口：严格输入 20ms，严格返回 20ms，全过程无指针
    std::vector<int16_t> process(const std::vector<int16_t>& input_pcm);

    // 释放资源
    void destroy();

private:
    static const int fs = 48000;
    static const int frame_samples = 960;      // 外部 IO：每次 20ms (960)
    static const int overlap_samples = 960;    // 重叠平滑区：20ms (960)
    static const int step_samples = 8640;      // 内部推进步长：180ms (8640)
    static const int analysis_samples = 9600;  // WORLD 一次处理总量：200ms (9600)

    double pitch_scale = 1.0;
    double formant_shift = 1.0;

    // 内部输入与输出缓冲队列
    std::vector<double> in_fifo;
    std::vector<double> out_fifo;

    // 历史上下文缓存，用于拼接和淡入淡出
    std::vector<double> prev_in_overlap;
    std::vector<double> prev_out_overlap;

    // 内部 WORLD 算法处理函数
    std::vector<double> process_world(const std::vector<double>& input);
};