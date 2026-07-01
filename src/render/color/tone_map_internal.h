// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_TONE_MAP_INTERNAL_H_
#define JXL_RENDER_COLOR_TONE_MAP_INTERNAL_H_

#include "render/color/rec2408_internal.h"
#include "render/color/tone_map.h"

#include <stddef.h>

typedef struct {
    jxl_rec2408_eetf_params eetf;
    float scale;
} jxl_tone_map_params;

jxl_tone_map_params jxl_tone_map_prep(const jxl_hdr_params *hdr, float target_display_luminance,
                                      float peak_luminance);

float jxl_color_detect_peak_luminance(jxl_context *ctx, const float *r, const float *g,
                                    const float *b, size_t n, const float luminances[3]);

void jxl_color_tone_map_base(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                             const jxl_tone_map_params *params);

void jxl_color_tone_map_luma_base(float *luma, size_t n, const jxl_tone_map_params *params,
                                  float intensity_target);

float jxl_color_detect_peak_luminance_base(const float *r, const float *g, const float *b, size_t n,
                                           const float luminances[3]);

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_color_tone_map_x86_sse2(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                                 const jxl_tone_map_params *params);
void jxl_color_tone_map_luma_x86_sse2(float *luma, size_t n, float intensity_target,
                                      const jxl_tone_map_params *params);
float jxl_color_detect_peak_luminance_x86_sse2(const float *r, const float *g, const float *b,
                                               size_t n, const float luminances[3]);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_color_tone_map_x86_fma(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                                const jxl_tone_map_params *params);
void jxl_color_tone_map_x86_avx2(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                                 const jxl_tone_map_params *params);
void jxl_color_tone_map_luma_x86_fma(float *luma, size_t n, float intensity_target,
                                     const jxl_tone_map_params *params);
void jxl_color_tone_map_luma_x86_avx2(float *luma, size_t n, float intensity_target,
                                      const jxl_tone_map_params *params);
float jxl_color_detect_peak_luminance_x86_avx2(const float *r, const float *g, const float *b,
                                               size_t n, const float luminances[3]);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_color_tone_map_neon(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                             const jxl_tone_map_params *params);
void jxl_color_tone_map_luma_neon(float *luma, size_t n, float intensity_target,
                                  const jxl_tone_map_params *params);
float jxl_color_detect_peak_luminance_neon(const float *r, const float *g, const float *b, size_t n,
                                           const float luminances[3]);
#endif

#endif /* JXL_RENDER_COLOR_TONE_MAP_INTERNAL_H_ */
