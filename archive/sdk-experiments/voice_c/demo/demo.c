#include "voicechanger.h"
#include <stdio.h>


int main()
{
    vc_init();

    vc_set_params(1, 1);

    PCMFrame20ms in;
    PCMFrame20ms out;

    for (int f = 0; f < 200; f++)
    {
        for (int i = 0; i < VC_FRAME_SAMPLES; i++)
        {
            double t = (f * VC_FRAME_SAMPLES + i) / 48000.0;
            in.data[i] = 10000 * sin(2 * 3.14159265358979323846 * 220 * t);
        }

        out = vc_process(in);

        printf("frame %d processed\n", f);
    }

    vc_destroy();
}