#ifndef VOICECHANGER_H
#define VOICECHANGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define VC_FS 48000
#define VC_FRAME_SAMPLES 480  // 10ms: 48000 * 0.01 = 480
#define VC_OVERLAP_SAMPLES 960
#define VC_STEP_SAMPLES 8640
#define VC_ANALYSIS_SAMPLES 9600

    typedef struct
    {
        int16_t data[VC_FRAME_SAMPLES];
    } PCMFrame10ms;  // 结构体名称改为PCMFrame10ms

#ifdef _WIN32
#ifdef VC_EXPORTS
#define VC_API __declspec(dllexport)
#else
#define VC_API __declspec(dllimport)
#endif
#else
#define VC_API
#endif

    VC_API int vc_init();
    VC_API void vc_set_params(double pitch, double formant);
    VC_API PCMFrame10ms vc_process(PCMFrame10ms input);  // 参数类型改为PCMFrame10ms
    VC_API void vc_destroy();

#ifdef __cplusplus
}
#endif

#endif