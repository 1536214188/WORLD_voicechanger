#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "voicechanger.h"

#define DEMO_PI 3.14159265358979323846

static int samples_from_ms(int sample_rate, int ms)
{
    return (sample_rate * ms + 500) / 1000;
}

static void fill_demo_input(PCMFrame10ms* input,
    int sample_rate, int block_samples, int frame_index)
{
    int base_sample = frame_index * block_samples;

    memset(input, 0, sizeof(*input));
    input->samples = block_samples;
    for (int i = 0; i < block_samples; i++)
    {
        double t = (double)(base_sample + i) / (double)sample_rate;
        double s = sin(2.0 * DEMO_PI * 220.0 * t) +
            0.35 * sin(2.0 * DEMO_PI * 440.0 * t);
        input->data[i] = (int16_t)(s * 9000.0);
    }
}

static int run_case(int sample_rate, int block_samples, int frames)
{
    PCMFrame10ms input;
    PCMFrame10ms output;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));

    printf("\n=== sample_rate=%d, block_samples=%d ===\n",
        sample_rate, block_samples);

    if (!vc_init(sample_rate))
    {
        printf("vc_init(%d) failed\n", sample_rate);
        return 0;
    }

    vc_set_params(0.7, 1.2);

    for (int frame = 0; frame < frames; frame++)
    {
        fill_demo_input(&input, sample_rate, block_samples, frame);
        output = vc_process(input);

        if (output.samples != input.samples)
        {
            printf("frame %d output.samples mismatch input=%d output=%d\n",
                frame, input.samples, output.samples);
            vc_destroy();
            return 0;
        }

        if (frame % 25 == 0)
        {
            printf("frame %d processed, samples=%d, first_out=%d\n",
                frame, output.samples, output.data[0]);
        }

#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    vc_destroy();
    return 1;
}

static void run_uninitialized_case(void)
{
    PCMFrame10ms input;
    PCMFrame10ms output;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.samples = samples_from_ms(48000, VC_FRAME_MS);

    printf("\n=== uninitialized vc_process log check ===\n");
    output = vc_process(input);
    printf("uninitialized output samples=%d first_out=%d\n",
        output.samples, output.data[0]);
}

int main(void)
{
    printf("Dynamic sampling minimal API demo start\n");

    run_uninitialized_case();

    if (!run_case(48000, 960, 80))
        return 1;
    if (!run_case(48000, 1024, 80))
        return 1;
    if (!run_case(16000, 320, 80))
        return 1;

    if (vc_init(192001))
    {
        printf("vc_init(192001) should fail but succeeded\n");
        vc_destroy();
        return 1;
    }

    printf("\nInvalid sample_rate rejected as expected\n");
    printf("Dynamic sampling minimal API demo end\n");
    return 0;
}
