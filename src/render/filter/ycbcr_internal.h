// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_YCBCR_INTERNAL_H_
#define JXL_RENDER_FILTER_YCBCR_INTERNAL_H_

#include <stddef.h>

void jxl_ycbcr_to_rgb_base(float *cb, float *y, float *cr, size_t count);

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_ycbcr_to_rgb_x86_sse2(float *cb, float *y, float *cr, size_t count);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_ycbcr_to_rgb_x86_fma(float *cb, float *y, float *cr, size_t count);
void jxl_ycbcr_to_rgb_x86_avx2(float *cb, float *y, float *cr, size_t count);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_ycbcr_to_rgb_neon(float *cb, float *y, float *cr, size_t count);
#endif

#endif /* JXL_RENDER_FILTER_YCBCR_INTERNAL_H_ */
