#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "voicechanger.h"

int main()
{
    printf("Demo start\n");

    vc_init();
    vc_set_params(0.7, 1.2);

    PCMFrame10ms in;
    PCMFrame10ms out;

    for (int i = 0; i < VC_FRAME_SAMPLES; i++)
        in.data[i] = 0;

    for (int frame = 0; frame < 200; frame++)
    {
        out = vc_process(in);

        printf("frame %d processed\n", frame);

#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    vc_destroy();

    printf("Demo end\n");

    return 0;
}