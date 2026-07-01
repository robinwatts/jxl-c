// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/gabor_sse41.h"

#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_SSE41)

#include <immintrin.h>

jxl_inline __m128 jxl_gabor_load_f32x2_dup(const float *p) {
    return _mm_load1_ps(p);
}

jxl_inline __m128 jxl_gabor_load_f32x2(const float *p) {
    return _mm_castsi128_ps(_mm_loadl_epi64((const __m128i *)p));
}

jxl_inline __m128 jxl_gabor_splat_w(const float *p) {
    return _mm_load1_ps(p);
}

jxl_inline __m128 jxl_gabor_vext_f32_1(__m128 a, __m128 b) {
    float a1 = _mm_cvtss_f32(_mm_shuffle_ps(a, a, _MM_SHUFFLE(0, 0, 0, 1)));
    float b0 = _mm_cvtss_f32(b);
    return _mm_set_ps(0.0f, 0.0f, b0, a1);
}

void jxl_gabor_row_sse41(jxl_gabor_row *row) {
    size_t width = row->width;
    float global_weight;
    size_t dx2;
    size_t x;
    float w0;
    float w1;
    const float *input_ptr_t = row->row_t;
    const float *input_ptr_c = row->row_c;
    const float *input_ptr_b = row->row_b;
    float *output_ptr = row->out;
    w0 = row->w0;
    w1 = row->w1;
    global_weight = 1.0f / (1.0f + w0 * 4.0f + w1 * 4.0f);

    if (width == 0) {
        return;
    }

    __m128 tl = jxl_gabor_load_f32x2_dup(input_ptr_t);
    __m128 cl = jxl_gabor_load_f32x2_dup(input_ptr_c);
    __m128 bl = jxl_gabor_load_f32x2_dup(input_ptr_b);
    __m128 w0_v = jxl_gabor_splat_w(&w0);
    __m128 w1_v = jxl_gabor_splat_w(&w1);
    __m128 gw_v = jxl_gabor_splat_w(&global_weight);

    for (dx2 = 0; dx2 < (width - 1) / 2; ++dx2) {
        x = dx2 * 2;

        __m128 tr = jxl_gabor_load_f32x2(input_ptr_t + 1 + x);
        __m128 cr = jxl_gabor_load_f32x2(input_ptr_c + 1 + x);
        __m128 br = jxl_gabor_load_f32x2(input_ptr_b + 1 + x);

        __m128 t = jxl_gabor_vext_f32_1(tl, tr);
        __m128 c = jxl_gabor_vext_f32_1(cl, cr);
        __m128 b = jxl_gabor_vext_f32_1(bl, br);

        __m128 sum_side = _mm_add_ps(_mm_add_ps(t, cl), _mm_add_ps(cr, b));
        __m128 sum_diag = _mm_add_ps(_mm_add_ps(tl, tr), _mm_add_ps(bl, br));
        __m128 unweighted_sum = _mm_add_ps(_mm_mul_ps(sum_side, w0_v), c);
        unweighted_sum = _mm_add_ps(_mm_mul_ps(sum_diag, w1_v), unweighted_sum);
        __m128 sum = _mm_mul_ps(unweighted_sum, gw_v);

        _mm_storel_epi64((void *)(output_ptr + x), _mm_castps_si128(sum));
        tl = tr;
        cl = cr;
        bl = br;
    }

    if ((width % 2) == 0) {
        x = width - 2;

        __m128 tr = jxl_gabor_load_f32x2_dup(input_ptr_t + 1 + x);
        __m128 cr = jxl_gabor_load_f32x2_dup(input_ptr_c + 1 + x);
        __m128 br = jxl_gabor_load_f32x2_dup(input_ptr_b + 1 + x);

        __m128 t = jxl_gabor_vext_f32_1(tl, tr);
        __m128 c = jxl_gabor_vext_f32_1(cl, cr);
        __m128 b = jxl_gabor_vext_f32_1(bl, br);

        __m128 sum_side = _mm_add_ps(_mm_add_ps(t, cl), _mm_add_ps(cr, b));
        __m128 sum_diag = _mm_add_ps(_mm_add_ps(tl, tr), _mm_add_ps(bl, br));
        __m128 unweighted_sum = _mm_add_ps(_mm_mul_ps(sum_side, w0_v), c);
        unweighted_sum = _mm_add_ps(_mm_mul_ps(sum_diag, w1_v), unweighted_sum);
        __m128 sum = _mm_mul_ps(unweighted_sum, gw_v);

        _mm_storel_epi64((void *)(output_ptr + x), _mm_castps_si128(sum));
    } else {
        float lanes[2];
        float t0;
        float t1;
        float c0;
        float c1;
        float b0;
        float b1;
        float sum_side;
        float sum_diag;
        float unweighted_sum;
        x = width - 1;
        _mm_storel_epi64((void *)lanes, _mm_castps_si128(tl));
        t0 = lanes[0];
        t1 = lanes[1];
        _mm_storel_epi64((void *)lanes, _mm_castps_si128(cl));
        c0 = lanes[0];
        c1 = lanes[1];
        _mm_storel_epi64((void *)lanes, _mm_castps_si128(bl));
        b0 = lanes[0];
        b1 = lanes[1];
        sum_side = t1 + c0 + c1 + b1;
        sum_diag = t0 + t1 + b0 + b1;
        unweighted_sum = c1 + sum_side * w0 + sum_diag * w1;
        output_ptr[x] = unweighted_sum * global_weight;
    }
}

#endif /* JXL_HAVE_SIMD_SSE41 */
