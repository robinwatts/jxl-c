// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/opsin_internal.h"

#if defined(JXL_HAVE_SIMD_AVX2)

#include <immintrin.h>
#include <math.h>

static void opsin_xyb_to_linear_rgb_vec(__m128 vx, __m128 vy, __m128 vb, const float ob[3],
                                        const float cbrt_ob[3], float itscale, const float m[9],
                                        __m128 *out_x, __m128 *out_y, __m128 *out_b) {
    const __m128 v_itscale = _mm_set1_ps(itscale);
    const __m128 v_ob0 = _mm_set1_ps(ob[0]);
    const __m128 v_ob1 = _mm_set1_ps(ob[1]);
    const __m128 v_ob2 = _mm_set1_ps(ob[2]);
    const __m128 v_cbrt0 = _mm_set1_ps(cbrt_ob[0]);
    const __m128 v_cbrt1 = _mm_set1_ps(cbrt_ob[1]);
    const __m128 v_cbrt2 = _mm_set1_ps(cbrt_ob[2]);
    __m128 g_l;
    __m128 g_m;
    __m128 g_s;
    __m128 lms0;
    __m128 lms1;
    __m128 lms2;

    g_l = _mm_sub_ps(_mm_add_ps(vy, vx), v_cbrt0);
    g_m = _mm_sub_ps(_mm_sub_ps(vy, vx), v_cbrt1);
    g_s = _mm_sub_ps(vb, v_cbrt2);

    lms0 = _mm_mul_ps(_mm_fmadd_ps(_mm_mul_ps(g_l, g_l), g_l, v_ob0), v_itscale);
    lms1 = _mm_mul_ps(_mm_fmadd_ps(_mm_mul_ps(g_m, g_m), g_m, v_ob1), v_itscale);
    lms2 = _mm_mul_ps(_mm_fmadd_ps(_mm_mul_ps(g_s, g_s), g_s, v_ob2), v_itscale);

    *out_x = _mm_fmadd_ps(_mm_set1_ps(m[0]), lms0,
                          _mm_fmadd_ps(_mm_set1_ps(m[1]), lms1, _mm_mul_ps(_mm_set1_ps(m[2]), lms2)));
    *out_y = _mm_fmadd_ps(_mm_set1_ps(m[3]), lms0,
                          _mm_fmadd_ps(_mm_set1_ps(m[4]), lms1, _mm_mul_ps(_mm_set1_ps(m[5]), lms2)));
    *out_b = _mm_fmadd_ps(_mm_set1_ps(m[6]), lms0,
                          _mm_fmadd_ps(_mm_set1_ps(m[7]), lms1, _mm_mul_ps(_mm_set1_ps(m[8]), lms2)));
}

static void opsin_xyb_to_linear_rgb_vec256(__m256 vx, __m256 vy, __m256 vb, const float ob[3],
                                           const float cbrt_ob[3], float itscale, const float m[9],
                                           __m256 *out_x, __m256 *out_y, __m256 *out_b) {
    const __m256 v_itscale = _mm256_set1_ps(itscale);
    const __m256 v_ob0 = _mm256_set1_ps(ob[0]);
    const __m256 v_ob1 = _mm256_set1_ps(ob[1]);
    const __m256 v_ob2 = _mm256_set1_ps(ob[2]);
    const __m256 v_cbrt0 = _mm256_set1_ps(cbrt_ob[0]);
    const __m256 v_cbrt1 = _mm256_set1_ps(cbrt_ob[1]);
    const __m256 v_cbrt2 = _mm256_set1_ps(cbrt_ob[2]);
    __m256 g_l;
    __m256 g_m;
    __m256 g_s;
    __m256 lms0;
    __m256 lms1;
    __m256 lms2;

    g_l = _mm256_sub_ps(_mm256_add_ps(vy, vx), v_cbrt0);
    g_m = _mm256_sub_ps(_mm256_sub_ps(vy, vx), v_cbrt1);
    g_s = _mm256_sub_ps(vb, v_cbrt2);

    lms0 = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_mul_ps(g_l, g_l), g_l, v_ob0), v_itscale);
    lms1 = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_mul_ps(g_m, g_m), g_m, v_ob1), v_itscale);
    lms2 = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_mul_ps(g_s, g_s), g_s, v_ob2), v_itscale);

    *out_x = _mm256_fmadd_ps(_mm256_set1_ps(m[0]), lms0,
                             _mm256_fmadd_ps(_mm256_set1_ps(m[1]), lms1,
                                             _mm256_mul_ps(_mm256_set1_ps(m[2]), lms2)));
    *out_y = _mm256_fmadd_ps(_mm256_set1_ps(m[3]), lms0,
                             _mm256_fmadd_ps(_mm256_set1_ps(m[4]), lms1,
                                             _mm256_mul_ps(_mm256_set1_ps(m[5]), lms2)));
    *out_b = _mm256_fmadd_ps(_mm256_set1_ps(m[6]), lms0,
                             _mm256_fmadd_ps(_mm256_set1_ps(m[7]), lms1,
                                             _mm256_mul_ps(_mm256_set1_ps(m[8]), lms2)));
}

static void opsin_xyb_to_linear_rgb_impl(float *x, float *y, float *b, size_t num_pixels,
                                         const jxl_opsin_inverse_parsed *opsin,
                                         float intensity_target, int use_avx2) {
    float itscale;
    float cbrt_ob[3];
    float ob[3];
    float m[9];
    size_t i;

    if (intensity_target <= 0.0f) {
        intensity_target = 255.0f;
    }
    itscale = 255.0f / intensity_target;
    ob[0] = opsin->opsin_bias[0];
    ob[1] = opsin->opsin_bias[1];
    ob[2] = opsin->opsin_bias[2];
    cbrt_ob[0] = cbrtf(ob[0]);
    cbrt_ob[1] = cbrtf(ob[1]);
    cbrt_ob[2] = cbrtf(ob[2]);
    m[0] = opsin->inv_mat[0][0];
    m[1] = opsin->inv_mat[0][1];
    m[2] = opsin->inv_mat[0][2];
    m[3] = opsin->inv_mat[1][0];
    m[4] = opsin->inv_mat[1][1];
    m[5] = opsin->inv_mat[1][2];
    m[6] = opsin->inv_mat[2][0];
    m[7] = opsin->inv_mat[2][1];
    m[8] = opsin->inv_mat[2][2];

    i = 0;
    if (use_avx2) {
        for (; i + 8 <= num_pixels; i += 8) {
            __m256 vx = _mm256_loadu_ps(x + i);
            __m256 vy = _mm256_loadu_ps(y + i);
            __m256 vb = _mm256_loadu_ps(b + i);
            __m256 out_x;
            __m256 out_y;
            __m256 out_b;
            opsin_xyb_to_linear_rgb_vec256(vx, vy, vb, ob, cbrt_ob, itscale, m, &out_x, &out_y,
                                           &out_b);
            _mm256_storeu_ps(x + i, out_x);
            _mm256_storeu_ps(y + i, out_y);
            _mm256_storeu_ps(b + i, out_b);
        }
    }
    for (; i + 4 <= num_pixels; i += 4) {
        __m128 vx = _mm_loadu_ps(x + i);
        __m128 vy = _mm_loadu_ps(y + i);
        __m128 vb = _mm_loadu_ps(b + i);
        __m128 out_x;
        __m128 out_y;
        __m128 out_b;
        opsin_xyb_to_linear_rgb_vec(vx, vy, vb, ob, cbrt_ob, itscale, m, &out_x, &out_y, &out_b);
        _mm_storeu_ps(x + i, out_x);
        _mm_storeu_ps(y + i, out_y);
        _mm_storeu_ps(b + i, out_b);
    }
    jxl_color_opsin_xyb_to_linear_rgb_base(x + i, y + i, b + i, num_pixels - i, opsin,
                                           intensity_target);
}

void jxl_color_opsin_xyb_to_linear_rgb_x86_avx2(float *x, float *y, float *b, size_t num_pixels,
                                                const jxl_opsin_inverse_parsed *opsin,
                                                float intensity_target) {
    opsin_xyb_to_linear_rgb_impl(x, y, b, num_pixels, opsin, intensity_target, 1);
}

void jxl_color_opsin_xyb_to_linear_rgb_x86_fma(float *x, float *y, float *b, size_t num_pixels,
                                               const jxl_opsin_inverse_parsed *opsin,
                                               float intensity_target) {
    opsin_xyb_to_linear_rgb_impl(x, y, b, num_pixels, opsin, intensity_target, 0);
}

#endif /* JXL_HAVE_SIMD_AVX2 */
