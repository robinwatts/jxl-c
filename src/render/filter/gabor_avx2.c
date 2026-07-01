// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/gabor_avx2.h"

#if defined(JXL_HAVE_SIMD_AVX2)

#include <immintrin.h>

static void gabor_row_interior_avx2(const float *row_t, const float *row_c, const float *row_b,
                                    float *out, size_t x, float w0, float w1, float gw) {
    __m256 w0_v = _mm256_set1_ps(w0);
    __m256 w1_v = _mm256_set1_ps(w1);
    __m256 gw_v = _mm256_set1_ps(gw);

    __m256 t_c = _mm256_loadu_ps(row_t + x);
    __m256 c_c = _mm256_loadu_ps(row_c + x);
    __m256 b_c = _mm256_loadu_ps(row_b + x);

    __m256 t_l = _mm256_loadu_ps(row_t + x - 1);
    __m256 t_r = _mm256_loadu_ps(row_t + x + 1);
    __m256 c_l = _mm256_loadu_ps(row_c + x - 1);
    __m256 c_r = _mm256_loadu_ps(row_c + x + 1);
    __m256 b_l = _mm256_loadu_ps(row_b + x - 1);
    __m256 b_r = _mm256_loadu_ps(row_b + x + 1);

    __m256 sum_side = _mm256_add_ps(_mm256_add_ps(t_c, c_l), _mm256_add_ps(c_r, b_c));
    __m256 sum_diag = _mm256_add_ps(_mm256_add_ps(t_l, t_r), _mm256_add_ps(b_l, b_r));

    __m256 unweighted = _mm256_fmadd_ps(sum_side, w0_v, c_c);
    unweighted = _mm256_fmadd_ps(sum_diag, w1_v, unweighted);
    _mm256_storeu_ps(out + x, _mm256_mul_ps(unweighted, gw_v));
}

void jxl_gabor_row_avx2(jxl_gabor_row *row) {
    size_t width = row->width;
    float global_weight;
    size_t x;
    float w0;
    float w1;
    const float *row_t = row->row_t;
    const float *row_c = row->row_c;
    const float *row_b = row->row_b;
    float *out = row->out;
    w0 = row->w0;
    w1 = row->w1;
    global_weight = 1.0f / (1.0f + w0 * 4.0f + w1 * 4.0f);

    if (width == 0) {
        return;
    }

    if (width == 1) {
        float t = row_t[0];
        float c = row_c[0];
        float b = row_b[0];
        out[0] = (c + (t + 2.0f * c + b) * w0 + 2.0f * (t + b) * w1) * global_weight;
        return;
    }

    {
        float t1 = row_t[0];
        float c1 = row_c[0];
        float b1 = row_b[0];
        float t0 = row_t[1];
        float c0 = row_c[1];
        float b0 = row_b[1];
        out[0] = (c1 + (t1 + c0 + c1 + b1) * w0 + (t0 + t1 + b0 + b1) * w1) * global_weight;
    }

    x = 1;
    while (x + 8 <= width - 1) {
        gabor_row_interior_avx2(row_t, row_c, row_b, out, x, w0, w1, global_weight);
        x += 8;
    }

    for (; x + 1 < width; ++x) {
        float t0 = row_t[x - 1];
        float t1 = row_t[x];
        float t2 = row_t[x + 1];
        float c0 = row_c[x - 1];
        float c1 = row_c[x];
        float c2 = row_c[x + 1];
        float b0 = row_b[x - 1];
        float b1 = row_b[x];
        float b2 = row_b[x + 1];
        float sum_side = t1 + c0 + c2 + b1;
        float sum_diag = t0 + t2 + b0 + b2;
        out[x] = (c1 + sum_side * w0 + sum_diag * w1) * global_weight;
    }

    {
        size_t last = width - 1;
        float t1 = row_t[last];
        float c1 = row_c[last];
        float b1 = row_b[last];
        float t0 = row_t[last - 1];
        float c0 = row_c[last - 1];
        float b0 = row_b[last - 1];
        out[last] = (c1 + (t1 + c0 + c1 + b1) * w0 + (t0 + t1 + b0 + b1) * w1) * global_weight;
    }
}

#endif /* JXL_HAVE_SIMD_AVX2 */
