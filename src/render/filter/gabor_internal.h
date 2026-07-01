// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_GABOR_INTERNAL_H_
#define JXL_RENDER_FILTER_GABOR_INTERNAL_H_

#include <stddef.h>

typedef struct {
    const float *row_t;
    const float *row_c;
    const float *row_b;
    float *out;
    size_t width;
    float w0;
    float w1;
} jxl_gabor_row;

void jxl_gabor_row_generic(jxl_gabor_row *row);

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_gabor_row_avx2(jxl_gabor_row *row);
#endif

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_gabor_row_sse41(jxl_gabor_row *row);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_gabor_row_neon(jxl_gabor_row *row);
#endif

#endif /* JXL_RENDER_FILTER_GABOR_INTERNAL_H_ */
