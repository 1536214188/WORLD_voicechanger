#ifndef VOICECHANGER_H
#define VOICECHANGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define VC_FS 48000
#define VC_FRAME_SAMPLES 480      // 10ms
#define VC_OVERLAP_SAMPLES 960
#define VC_STEP_SAMPLES 12000
#define VC_ANALYSIS_SAMPLES 12960

    typedef struct
    {
        int16_t data[VC_FRAME_SAMPLES];
    } PCMFrame10ms;

#ifdef _WIN32
#ifdef VC_EXPORTS
#define VC_API __declspec(dllexport)
#else
#define VC_API __declspec(dllimport)
#endif
#else
#define VC_API
#endif

    VC_API int vc_init(void);
    VC_API void vc_set_params(double pitch, double formant);
    VC_API PCMFrame10ms vc_process(PCMFrame10ms input);
    VC_API void vc_destroy(void);

#ifdef __cplusplus
}
#endif

#endif