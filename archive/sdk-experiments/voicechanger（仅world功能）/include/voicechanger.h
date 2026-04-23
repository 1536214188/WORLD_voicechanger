#ifndef VOICECHANGER_H
#define VOICECHANGER_H

#include <stdint.h>

#ifdef VC_EXPORTS
#define VC_API __declspec(dllexport)
#else
#define VC_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

    VC_API int vc_init();
    VC_API int vc_process_pcm(const int16_t* input_pcm, int samples, int16_t* output_pcm, double pitch, double formant);
    VC_API void vc_destroy();

#ifdef __cplusplus
}
#endif
#endif