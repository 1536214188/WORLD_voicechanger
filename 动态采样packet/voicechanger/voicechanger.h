#ifndef VOICECHANGER_H
#define VOICECHANGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define VC_MIN_SAMPLE_RATE 8000
#define VC_MAX_SAMPLE_RATE 192000

#define VC_FRAME_MS 10
#define VC_OVERLAP_MS 20
#define VC_STEP_MS 250


#define VC_FRAME_SAMPLES 3840

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

VC_API int vc_init(int sample_rate);
VC_API void vc_set_params(double pitch, double formant);
VC_API PCMFrame10ms vc_process(PCMFrame10ms input);
VC_API void vc_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
