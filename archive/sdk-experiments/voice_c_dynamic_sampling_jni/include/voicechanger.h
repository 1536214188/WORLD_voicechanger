#ifndef VOICECHANGER_H
#define VOICECHANGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define VC_FRAME_MS 10
#define VC_OVERLAP_MS 20
#define VC_STEP_MS 250

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
VC_API int vc_process(const int16_t* input, int input_samples,
    int16_t* output, int output_capacity);
VC_API void vc_destroy(void);
VC_API int vc_get_sample_rate(void);
VC_API int vc_get_frame_samples(void);

#ifdef __cplusplus
}
#endif

#endif
