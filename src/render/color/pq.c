// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/pq.h"

#include "render/color/fastmath.h"
#include "render/color/pq_internal.h"
#include "render/simd/features.h"

#include <math.h>
#include <string.h>

static const float k_eotf_p[5] = {
    2.6297566e-4f, -6.235531e-3f, 7.386023e-1f, 2.6455317f, 5.500349e-1f,
};
static const float k_eotf_q[5] = {
    4.213501e2f, -4.2873682e2f, 1.7436467e2f, -3.3907887e1f, 2.6771877f,
};
static const float k_inv_eotf_p[5] = {
    1.351392e-2f, -1.095778f, 5.522776e1f, 1.492516e2f, 4.838434e1f,
};
static const float k_inv_eotf_q[5] = {
    1.012416f, 2.016708e1f, 9.26371e1f, 1.120607e2f, 2.590418e1f,
};
static const float k_inv_eotf_p_small[5] = {
    9.863406e-6f, 3.881234e-1f, 1.352821e2f, 6.889862e4f, -2.864824e5f,
};
static const float k_inv_eotf_q_small[5] = {
    3.371868e1f, 1.477719e3f, 1.608477e4f, -4.389884e4f, -2.072546e5f,
};

static float pq_normalize_intensity(float intensity_target) {
    if (intensity_target <= 0.0f) {
        return 10000.0f;
    }
    return intensity_target;
}

static float linear_to_pq_generic(float s, float intensity_target) {
    float y_mult = intensity_target / 10000.0f;
    float a = fabsf(s);
    float a_scaled = a * y_mult;
    float a_1_4 = sqrtf(sqrtf(a_scaled));
    float y;
    if (a < 1e-4f) {
        y = jxl_fastmath_rational_eval5(a_1_4, k_inv_eotf_p_small, k_inv_eotf_q_small);
    } else {
        y = jxl_fastmath_rational_eval5(a_1_4, k_inv_eotf_p, k_inv_eotf_q);
    }
    return copysignf(y, s);
}

static float pq_to_linear_generic(float s, float intensity_target) {
    float y_mult = 10000.0f / intensity_target;
    float a = fabsf(s);
    float x = a * a + a;
    float y = jxl_fastmath_rational_eval5(x, k_eotf_p, k_eotf_q);
    return copysignf(y * y_mult, s);
}

float jxl_color_pq_to_linear_sample(float s, float intensity_target) {
    intensity_target = pq_normalize_intensity(intensity_target);
    return pq_to_linear_generic(s, intensity_target);
}

float jxl_color_linear_to_pq_sample(float s, float intensity_target) {
    intensity_target = pq_normalize_intensity(intensity_target);
    return linear_to_pq_generic(s, intensity_target);
}

void jxl_color_linear_to_pq_base(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    intensity_target = pq_normalize_intensity(intensity_target);
    for (i = 0; i < n; ++i) {
        samples[i] = linear_to_pq_generic(samples[i], intensity_target);
    }
}

void jxl_color_pq_to_linear_base(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    intensity_target = pq_normalize_intensity(intensity_target);
    for (i = 0; i < n; ++i) {
        samples[i] = pq_to_linear_generic(samples[i], intensity_target);
    }
}

void jxl_color_linear_to_pq(jxl_context *ctx, float *samples, size_t n, float intensity_target) {
    const jxl_cpu_features *feat;
    if (samples == NULL || n == 0) {
        return;
    }
    intensity_target = pq_normalize_intensity(intensity_target);
    feat = jxl_context_cpu_features(ctx);

#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
            jxl_color_linear_to_pq_neon(samples, n, intensity_target);
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
            if (feat->avx2) {
                jxl_color_linear_to_pq_x86_avx2(samples, n, intensity_target);
            } else {
                jxl_color_linear_to_pq_x86_fma(samples, n, intensity_target);
            }
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_linear_to_pq_x86_sse2(samples, n, intensity_target);
    return;
#endif
    jxl_color_linear_to_pq_base(samples, n, intensity_target);
}

void jxl_color_pq_to_linear(jxl_context *ctx, float *samples, size_t n, float intensity_target) {
    const jxl_cpu_features *feat;
    if (samples == NULL || n == 0) {
        return;
    }
    intensity_target = pq_normalize_intensity(intensity_target);
    feat = jxl_context_cpu_features(ctx);

#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
            jxl_color_pq_to_linear_neon(samples, n, intensity_target);
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
            if (feat->avx2) {
                jxl_color_pq_to_linear_x86_avx2(samples, n, intensity_target);
            } else {
                jxl_color_pq_to_linear_x86_fma(samples, n, intensity_target);
            }
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_pq_to_linear_x86_sse2(samples, n, intensity_target);
    return;
#endif
    jxl_color_pq_to_linear_base(samples, n, intensity_target);
}
