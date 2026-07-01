// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/ycbcr_internal.h"

#include <immintrin.h>

#if defined(JXL_HAVE_SIMD_SSE41)

static const float k_ycbcr_y_offset = 128.0f / 255.0f;
static const float k_ycbcr_r_cr = 1.402f;
static const float k_ycbcr_g_cb = -0.114f * 1.772f / 0.587f;
static const float k_ycbcr_g_cr = -0.299f * 1.402f / 0.587f;
static const float k_ycbcr_b_cb = 1.772f;

static void ycbcr_to_rgb_vec_sse2(__m128 vcb, __m128 vy, __m128 vcr, __m128 *out_r, __m128 *out_g,
                                  __m128 *out_b) {
    const __m128 y_v = _mm_add_ps(vy, _mm_set1_ps(k_ycbcr_y_offset));
    *out_r = _mm_add_ps(_mm_mul_ps(vcr, _mm_set1_ps(k_ycbcr_r_cr)), y_v);
    *out_g = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vcb, _mm_set1_ps(k_ycbcr_g_cb)),
                                   _mm_mul_ps(vcr, _mm_set1_ps(k_ycbcr_g_cr))),
                        y_v);
    *out_b = _mm_add_ps(_mm_mul_ps(vcb, _mm_set1_ps(k_ycbcr_b_cb)), y_v);
}

void jxl_ycbcr_to_rgb_x86_sse2(float *cb, float *y, float *cr, size_t count) {
    size_t i;
    if (cb == NULL || y == NULL || cr == NULL || count == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= count; i += 4) {
        const __m128 vcb = _mm_loadu_ps(cb + i);
        const __m128 vy = _mm_loadu_ps(y + i);
        const __m128 vcr = _mm_loadu_ps(cr + i);
        __m128 out_r;
        __m128 out_g;
        __m128 out_b;
        ycbcr_to_rgb_vec_sse2(vcb, vy, vcr, &out_r, &out_g, &out_b);
        _mm_storeu_ps(cb + i, out_r);
        _mm_storeu_ps(y + i, out_g);
        _mm_storeu_ps(cr + i, out_b);
    }
    jxl_ycbcr_to_rgb_base(cb + i, y + i, cr + i, count - i);
}

#endif /* defined(JXL_HAVE_SIMD_SSE41) */
