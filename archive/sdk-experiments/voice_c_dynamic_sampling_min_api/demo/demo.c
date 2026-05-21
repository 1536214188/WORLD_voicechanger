#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "voicechanger.h"

static int samples_from_ms(int sample_rate, int ms)
{
    return (sample_rate * ms + 500) / 1000;
}

static int run_case(int sample_rate, int frames)
{
    int frame_samples = samples_from_ms(sample_rate, VC_FRAME_MS);
    PCMFrame10ms input;
    PCMFrame10ms output;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));

    printf("\n=== sample_rate=%d, frame_samples=%d ===\n",
        sample_rate, frame_samples);

    if (!vc_init(sample_rate))
    {
        printf("vc_init(%d) failed\n", sample_rate);
        return 0;
    }

    vc_set_params(0.7, 1.2);

    for (int frame = 0; frame < frames; frame++)
    {
        output = vc_process(input);

        if (frame % 25 == 0)
        {
            printf("frame %d processed, first_out=%d\n",
                frame, output.data[0]);
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

    printf("\n=== uninitialized vc_process log check ===\n");
    output = vc_process(input);
    printf("uninitialized output first_out=%d\n", output.data[0]);
}

int main(void)
{
    printf("Dynamic sampling minimal API demo start\n");

    run_uninitialized_case();

    if (!run_case(44100, 80))
        return 1;
    if (!run_case(48000, 80))
        return 1;
    if (!run_case(51200, 80))
        return 1;
    if (!run_case(192000, 20))
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
