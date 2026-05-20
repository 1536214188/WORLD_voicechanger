#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "voicechanger.h"

static int run_case(int sample_rate, int frames)
{
    int frame_samples = sample_rate * VC_FRAME_MS / 1000;
    int16_t* input = (int16_t*)calloc((size_t)frame_samples, sizeof(int16_t));
    int16_t* output = (int16_t*)calloc((size_t)frame_samples, sizeof(int16_t));

    if (!input || !output)
    {
        free(input);
        free(output);
        printf("alloc failed for %d Hz\n", sample_rate);
        return 0;
    }

    printf("\n=== sample_rate=%d, frame_samples=%d ===\n",
        sample_rate, frame_samples);

    if (!vc_init(sample_rate))
    {
        printf("vc_init(%d) failed\n", sample_rate);
        free(input);
        free(output);
        return 0;
    }

    vc_set_params(0.7, 1.2);

    for (int frame = 0; frame < frames; frame++)
    {
        int out_len = vc_process(input, frame_samples, output, frame_samples);
        if (out_len != frame_samples)
        {
            printf("frame %d failed, out_len=%d\n", frame, out_len);
            vc_destroy();
            free(input);
            free(output);
            return 0;
        }

        if (frame % 25 == 0)
            printf("frame %d processed\n", frame);

#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    printf("current sample_rate=%d, frame_samples=%d\n",
        vc_get_sample_rate(), vc_get_frame_samples());

    vc_destroy();
    free(input);
    free(output);
    return 1;
}

int main(void)
{
    printf("Dynamic sampling demo start\n");

    if (!run_case(44100, 80))
        return 1;

    if (!run_case(48000, 80))
        return 1;

    printf("\nDynamic sampling demo end\n");
    return 0;
}
