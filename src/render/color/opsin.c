// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/opsin_internal.h"

#include "render/simd/features.h"

#include <math.h>

static void matmul3vec(const float m[9], float v[3]) {
    float x = v[0];
    float y = v[1];
    float z = v[2];
    v[0] = m[0] * x + m[1] * y + m[2] * z;
    v[1] = m[3] * x + m[4] * y + m[5] * z;
    v[2] = m[6] * x + m[7] * y + m[8] * z;
}

void jxl_color_opsin_xyb_to_linear_rgb_base(float *x, float *y, float *b, size_t num_pixels,
                                            const jxl_opsin_inverse_parsed *opsin,
                                            float intensity_target) {
    size_t i;
    float itscale;
    float cbrt_ob[3];
    float ob[3];
    float m[9];

    if (x == NULL || y == NULL || b == NULL || opsin == NULL || num_pixels == 0) {
        return;
    }
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

    for (i = 0; i < num_pixels; ++i) {
        float g_l = y[i] + x[i];
        float g_m = y[i] - x[i];
        float g_s = b[i];
        float v[3];

        g_l -= cbrt_ob[0];
        g_m -= cbrt_ob[1];
        g_s -= cbrt_ob[2];

        v[0] = (g_l * g_l * g_l + ob[0]) * itscale;
        v[1] = (g_m * g_m * g_m + ob[1]) * itscale;
        v[2] = (g_s * g_s * g_s + ob[2]) * itscale;
        matmul3vec(m, v);
        x[i] = v[0];
        y[i] = v[1];
        b[i] = v[2];
    }
}

void jxl_color_opsin_xyb_to_linear_rgb(jxl_context *ctx, float *x, float *y, float *b,
                                       size_t num_pixels, const jxl_opsin_inverse_parsed *opsin,
                                       float intensity_target) {
    const jxl_cpu_features *feat = jxl_context_cpu_features(ctx);
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_opsin_xyb_to_linear_rgb_neon(x, y, b, num_pixels, opsin, intensity_target);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->avx2 && feat->fma) {
        jxl_color_opsin_xyb_to_linear_rgb_x86_avx2(x, y, b, num_pixels, opsin, intensity_target);
        return;
    }
    if (feat->fma) {
        jxl_color_opsin_xyb_to_linear_rgb_x86_fma(x, y, b, num_pixels, opsin, intensity_target);
        return;
    }
#endif
    jxl_color_opsin_xyb_to_linear_rgb_base(x, y, b, num_pixels, opsin, intensity_target);
}
