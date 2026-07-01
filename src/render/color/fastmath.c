// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/fastmath.h"

#include "render/simd/features.h"

#include <math.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

static const float k_pow2f_numer[3] = {1.01749063e1f, 4.88687798e1f, 9.85506591e1f};
static const float k_pow2f_denom[4] = {2.10242958e-1f, -2.22328856e-2f, -1.94414990e1f, 9.85506633e1f};
static const float k_log2f_p[3] = {
    -1.8503833400518310e-6f, 1.4287160470083755f, 7.4245873327820566e-1f,
};
static const float k_log2f_q[3] = {
    9.9032814277590719e-1f, 1.0096718572241148f, 1.7409343003366853e-1f,
};

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_fastmath_powf_f32_x86_sse41(float *samples, size_t n, float exponent);
#endif
#if defined(JXL_HAVE_SIMD_NEON)
void jxl_fastmath_powf_f32_neon(float *samples, size_t n, float exponent);
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_fastmath_powf_f32_x86_avx2(float *samples, size_t n, float exponent);
#endif

static float rational_eval_horner(float x, const float *coeffs, size_t n) {
    size_t i;
    float y = coeffs[n - 1];
    for (i = n - 1; i-- > 0;) {
        y = y * x + coeffs[i];
    }
    return y;
}

float jxl_fastmath_rational_eval3(float x, const float p[3], const float q[3]) {
    return rational_eval_horner(x, p, 3) / rational_eval_horner(x, q, 3);
}

float jxl_fastmath_rational_eval5(float x, const float p[5], const float q[5]) {
    return rational_eval_horner(x, p, 5) / rational_eval_horner(x, q, 5);
}

float jxl_fastmath_pow2f(float x) {
    float x_floor = floorf(x);
    uint32_t exp_bits = (uint32_t)((int32_t)x_floor + 127) << 23;
    float two_to_floor;
    float frac;
    float num;
    float den;
    memcpy(&two_to_floor, &exp_bits, sizeof(two_to_floor));
    frac = x - x_floor;

    num = frac + k_pow2f_numer[0];
    num = num * frac + k_pow2f_numer[1];
    num = num * frac + k_pow2f_numer[2];
    num = num * two_to_floor;

    den = k_pow2f_denom[0] * frac + k_pow2f_denom[1];
    den = den * frac + k_pow2f_denom[2];
    den = den * frac + k_pow2f_denom[3];

    return num / den;
}

float jxl_fastmath_log2f(float x) {
    uint32_t bits;
    int32_t exp_shifted;
    float mantissa;
    float xm;
    int32_t x_bits;
    uint32_t mant_bits;
    float exponent_f;
    memcpy(&bits, &x, sizeof(bits));
    x_bits = (int32_t)bits;
    exp_shifted = (x_bits - 0x3f2aaaab) >> 23;
    mant_bits = (uint32_t)((int64_t)x_bits - ((int64_t)exp_shifted << 23));
    memcpy(&mantissa, &mant_bits, sizeof(mantissa));
    exponent_f = (float)exp_shifted;

    xm = mantissa - 1.0f;
    return jxl_fastmath_rational_eval3(xm, k_log2f_p, k_log2f_q) + exponent_f;
}

float jxl_fastmath_powf(float base, float exponent) {
    return jxl_fastmath_pow2f(jxl_fastmath_log2f(base) * exponent);
}

void jxl_fastmath_powf_in_place(jxl_context *ctx, float *samples, size_t n, float exponent) {
    size_t i;
    const jxl_cpu_features *feat;
    if (samples == NULL || n == 0) {
        return;
    }
    feat = jxl_context_cpu_features(ctx);
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
            jxl_fastmath_powf_f32_neon(samples, n, exponent);
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->avx2 && feat->fma) {
        jxl_fastmath_powf_f32_x86_avx2(samples, n, exponent);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    if (feat->sse41) {
        jxl_fastmath_powf_f32_x86_sse41(samples, n, exponent);
        return;
    }
#endif
    for (i = 0; i < n; ++i) {
        samples[i] = jxl_fastmath_powf(samples[i], exponent);
    }
}
