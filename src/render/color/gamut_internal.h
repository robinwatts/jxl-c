// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_GAMUT_INTERNAL_H_
#define JXL_RENDER_COLOR_GAMUT_INTERNAL_H_

#include <stddef.h>

#if defined(JXL_HAVE_SIMD_SSE41)
#include <immintrin.h>
#endif

void jxl_color_map_gamut_rgb_base(const float rgb[3], float out[3], const float luminances[3],
                                    float saturation_factor);

void jxl_color_gamut_map_base(float *r, float *g, float *b, size_t n, const float luminances[3],
                              float saturation_factor);

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_color_gamut_map_x86_sse2(float *r, float *g, float *b, size_t n, const float luminances[3],
                                  float saturation_factor);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_color_gamut_map_x86_fma(float *r, float *g, float *b, size_t n, const float luminances[3],
                                 float saturation_factor);
void jxl_color_gamut_map_x86_avx2(float *r, float *g, float *b, size_t n, const float luminances[3],
                                  float saturation_factor);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_color_gamut_map_neon(float *r, float *g, float *b, size_t n, const float luminances[3],
                              float saturation_factor);
#endif

#endif /* JXL_RENDER_COLOR_GAMUT_INTERNAL_H_ */
