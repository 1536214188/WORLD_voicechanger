#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>

#include "voicechanger.h"

#define SAMPLE_RATE 48000
#define FRAME_SAMPLES 960 // 严格按照你的要求，每次输入 20ms

int main()
{
    VoiceChanger vc;

    if (!vc.init()) {
        std::cerr << "VoiceChanger initialization failed!" << std::endl;
        return -1;
    }

    // 设置变男声/女声参数
    vc.setParams(1.3, 1.2);

    std::vector<int16_t> input_block(FRAME_SAMPLES);
    double phase = 0.0;

    std::cout << "Starting 20ms stream offline test..." << std::endl;

    // 模拟 500 次 20ms 交互 (总计 10 秒音频)
    for (int frame = 0; frame < 500; ++frame)
    {
        // 模拟外部输入的 20ms 正弦波
        for (int i = 0; i < FRAME_SAMPLES; ++i)
        {
            double frequency = 220.0;
            input_block[i] = static_cast<int16_t>(12000.0 * sin(2.0 * 3.1415926535 * frequency * phase));

            phase += 1.0 / SAMPLE_RATE;
            if (phase >= 1.0) phase -= 1.0;
        }

        // 调用变声器核心函数
        std::vector<int16_t> output_block = vc.process(input_block);

        // 验证输出是否严格为 20ms
        if (output_block.size() != FRAME_SAMPLES) {
            std::cerr << "Error: Output size mismatch!" << std::endl;
            break;
        }

        if (frame % 50 == 0) {
            std::cout << "Processed frame: " << frame
                << " | Input size: " << input_block.size()
                << " | Output size: " << output_block.size() << std::endl;
        }
    }

    vc.destroy();
    std::cout << "Offline test completed successfully." << std::endl;

    return 0;
}