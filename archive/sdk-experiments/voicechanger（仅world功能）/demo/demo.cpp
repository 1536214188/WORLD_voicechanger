#include <iostream>
#include <vector>
#include <cstdint>
#include "voicechanger.h"

int main() {
    vc_init();
    const int SAMPLES = 1024;
    std::vector<int16_t> in(SAMPLES, 500), out(SAMPLES, 0);

    vc_process_pcm(in.data(), SAMPLES, out.data(), 0.7, 0.9);
    std::cout << "Uncle voice processed." << std::endl;

    vc_process_pcm(in.data(), SAMPLES, out.data(), 1.5, 1.2);
    std::cout << "Girl voice processed." << std::endl;

    vc_destroy();
    return 0;
}