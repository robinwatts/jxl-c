// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_TRANSFORM_INTERNAL_H_
#define JXL_RENDER_COLOR_TRANSFORM_INTERNAL_H_

#include <stddef.h>

void jxl_color_linear_to_srgb_base(float *samples, size_t n);
void jxl_color_linear_to_bt709_base(float *samples, size_t n);
void jxl_color_srgb_to_linear_base(float *samples, size_t n);
void jxl_color_bt709_to_linear_base(float *samples, size_t n);
void jxl_color_apply_gamma_base(float *samples, size_t n, float gamma);

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_color_linear_to_bt709_x86_sse41(float *samples, size_t n);
void jxl_color_srgb_to_linear_x86_sse2(float *samples, size_t n);
void jxl_color_bt709_to_linear_x86_sse2(float *samples, size_t n);
void jxl_color_apply_gamma_x86_sse2(float *samples, size_t n, float gamma);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_color_linear_to_srgb_x86_avx2(float *samples, size_t n);
void jxl_color_linear_to_bt709_x86_avx2(float *samples, size_t n);
void jxl_color_srgb_to_linear_x86_fma(float *samples, size_t n);
void jxl_color_srgb_to_linear_x86_avx2(float *samples, size_t n);
void jxl_color_bt709_to_linear_x86_avx2(float *samples, size_t n);
void jxl_color_bt709_to_linear_x86_fma(float *samples, size_t n);
void jxl_color_apply_gamma_x86_avx2(float *samples, size_t n, float gamma);
void jxl_color_apply_gamma_x86_fma(float *samples, size_t n, float gamma);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_color_linear_to_srgb_neon(float *samples, size_t n);
void jxl_color_linear_to_bt709_neon(float *samples, size_t n);
void jxl_color_srgb_to_linear_neon(float *samples, size_t n);
void jxl_color_bt709_to_linear_neon(float *samples, size_t n);
void jxl_color_apply_gamma_neon(float *samples, size_t n, float gamma);
#endif

#endif /* JXL_RENDER_COLOR_TRANSFORM_INTERNAL_H_ */
