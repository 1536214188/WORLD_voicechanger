#ifndef VOICECHANGER_H
#define VOICECHANGER_H

#ifdef __cplusplus
extern "C" {  // 添加C链接声明
#endif

#include <stdint.h>

#define VC_FS 48000
#define VC_FRAME_SAMPLES 960
#define VC_OVERLAP_SAMPLES 960
#define VC_STEP_SAMPLES 8640
#define VC_ANALYSIS_SAMPLES 9600

    typedef struct
    {
        int16_t data[VC_FRAME_SAMPLES];
    } PCMFrame20ms;

    // 添加DLL导出声明
#ifdef _WIN32
#ifdef VC_EXPORTS
#define VC_API __declspec(dllexport)
#else
#define VC_API __declspec(dllimport)
#endif
#else
#define VC_API
#endif

    VC_API int vc_init();  // 添加VC_API前缀
    VC_API void vc_set_params(double pitch, double formant);
    VC_API PCMFrame20ms vc_process(PCMFrame20ms input);
    VC_API void vc_destroy();

#ifdef __cplusplus
}
#endif

#endif