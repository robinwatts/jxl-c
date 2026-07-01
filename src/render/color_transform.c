// SPDX-License-Identifier: MIT OR Apache-2.0
#include "color_transform.h"

#include "render/color/color_transform_internal.h"
#include "render/color/fastmath.h"
#include "render/color/opsin_internal.h"
#include "render/color/pq.h"
#include "render/simd/features.h"

#include <math.h>
#include <string.h>

void jxl_color_transform_xyb_to_linear_rgb(jxl_context *ctx, float *x, float *y, float *b,
                                           size_t num_pixels,
                                           const jxl_opsin_inverse_parsed *opsin,
                                           float intensity_target) {
    jxl_color_opsin_xyb_to_linear_rgb(ctx, x, y, b, num_pixels, opsin, intensity_target);
}

/* Ported from crates/jxl-color/src/tf/srgb.rs (generic path). */
void jxl_color_linear_to_srgb_base(float *samples, size_t n) {
    size_t i;
    static const uint8_t k_powtable_upper[16] = {
        0x00, 0x0a, 0x19, 0x26, 0x32, 0x41, 0x4d, 0x5c,
        0x68, 0x75, 0x83, 0x8f, 0xa0, 0xaa, 0xb9, 0xc6,
    };
    static const uint8_t k_powtable_lower[16] = {
        0x00, 0xb7, 0x04, 0x0d, 0xcb, 0xe7, 0x41, 0x68,
        0x51, 0xd1, 0xeb, 0xf2, 0x00, 0xb7, 0x04, 0x0d,
    };

    for (i = 0; i < n; ++i) {
        float s = samples[i];
        uint32_t bits;
        uint32_t v;
        uint32_t v_adj_bits;
        float v_adj;
        float pow;
        float mul;
        float vf;
        float small;
        float acc;
        size_t idx;
        uint32_t mul_bits;
        memcpy(&bits, &s, sizeof(bits));
        v = bits & 0x7fffffffu;
        v_adj_bits = (v | 0x3e800000u) & 0x3effffffu;
        memcpy(&v_adj, &v_adj_bits, sizeof(v_adj));

        pow = 0.059914046f;
        pow = pow * v_adj - 0.10889456f;
        pow = pow * v_adj + 0.107963754f;
        pow = pow * v_adj + 0.018092343f;

        idx = (size_t)((v >> 23) - 118u) & 0xfu;
        mul_bits = 0x40000000u | ((uint32_t)k_powtable_upper[idx] << 18) |
                            ((uint32_t)k_powtable_lower[idx] << 10);
        memcpy(&mul, &mul_bits, sizeof(mul));

        memcpy(&vf, &v, sizeof(vf));
        small = vf * 12.92f;
        acc = pow * mul - 0.055f;

        samples[i] = (vf <= 0.0031308f ? small : acc);
        if (s < 0.0f) {
            samples[i] = -samples[i];
        }
    }
}

static void linear_to_srgb(const jxl_cpu_features *feat, float *samples, size_t n) {
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_linear_to_srgb_neon(samples, n);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->avx2 && feat->fma) {
        jxl_color_linear_to_srgb_x86_avx2(samples, n);
        return;
    }
#endif
    jxl_color_linear_to_srgb_base(samples, n);
}

void jxl_color_linear_to_bt709_base(float *samples, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        float a = samples[i];
        if (a <= 0.018f) {
            samples[i] = 4.5f * a;
        } else {
            samples[i] = jxl_fastmath_powf(a, 0.45f) * 1.099f - 0.099f;
        }
    }
}

static void linear_to_bt709(const jxl_cpu_features *feat, float *samples, size_t n) {
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_linear_to_bt709_neon(samples, n);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->avx2 && feat->fma) {
        jxl_color_linear_to_bt709_x86_avx2(samples, n);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    if (feat->sse41) {
        jxl_color_linear_to_bt709_x86_sse41(samples, n);
        return;
    }
#endif
    jxl_color_linear_to_bt709_base(samples, n);
}

void jxl_color_srgb_to_linear_base(float *samples, size_t n) {
    size_t i;
    static const float k_p[5] = {
        2.200248328e-4f, 1.043637593e-2f, 1.624820318e-1f, 7.961564959e-1f, 8.210152774e-1f,
    };
    static const float k_q[5] = {
        2.631846970e-1f, 1.076976492f, 4.987528350e-1f, -5.512498495e-2f, 6.521209011e-3f,
    };

    if (samples == NULL || n == 0) {
        return;
    }
    for (i = 0; i < n; ++i) {
        float s = samples[i];
        float a = s < 0.0f ? -s : s;
        float out;
        if (a <= 0.04045f) {
            out = a / 12.92f;
        } else {
            out = jxl_fastmath_rational_eval5(a, k_p, k_q);
        }
        samples[i] = s < 0.0f ? -out : out;
    }
}

static void srgb_to_linear(const jxl_cpu_features *feat, float *samples, size_t n) {
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_srgb_to_linear_neon(samples, n);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
        if (feat->avx2) {
            jxl_color_srgb_to_linear_x86_avx2(samples, n);
        } else {
            jxl_color_srgb_to_linear_x86_fma(samples, n);
        }
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_srgb_to_linear_x86_sse2(samples, n);
    return;
#endif
    jxl_color_srgb_to_linear_base(samples, n);
}

static void bt709_to_linear(const jxl_cpu_features *feat, float *samples, size_t n) {
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_bt709_to_linear_neon(samples, n);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
        if (feat->avx2) {
            jxl_color_bt709_to_linear_x86_avx2(samples, n);
        } else {
            jxl_color_bt709_to_linear_x86_fma(samples, n);
        }
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_bt709_to_linear_x86_sse2(samples, n);
    return;
#endif
    jxl_color_bt709_to_linear_base(samples, n);
}

void jxl_color_bt709_to_linear_base(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    for (i = 0; i < n; ++i) {
        float a = samples[i];
        if (a <= 0.081f) {
            samples[i] = a / 4.5f;
        } else {
            samples[i] = jxl_fastmath_powf((a + 0.099f) / 1.099f, 1.0f / 0.45f);
        }
    }
}

void jxl_color_apply_gamma_base(float *samples, size_t n, float gamma) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    for (i = 0; i < n; ++i) {
        float a = samples[i];
        samples[i] = a <= 1e-7f ? 0.0f : jxl_fastmath_powf(a, gamma);
    }
}

static void apply_gamma(const jxl_cpu_features *feat, float *samples, size_t n, float gamma) {
    if (samples == NULL || n == 0) {
        return;
    }
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_apply_gamma_neon(samples, n, gamma);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
        if (feat->avx2) {
            jxl_color_apply_gamma_x86_avx2(samples, n, gamma);
        } else {
            jxl_color_apply_gamma_x86_fma(samples, n, gamma);
        }
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_apply_gamma_x86_sse2(samples, n, gamma);
    return;
#endif
    jxl_color_apply_gamma_base(samples, n, gamma);
}

void jxl_color_apply_gamma(jxl_context *ctx, float *samples, size_t n, float gamma) {
    if (samples == NULL || n == 0) {
        return;
    }
    apply_gamma(jxl_context_cpu_features(ctx), samples, n, gamma);
}

void jxl_color_transform_apply_forward_transfer(jxl_context *ctx, float *samples,
                                                size_t num_pixels, jxl_transfer_function_i tf,
                                                float intensity_target) {
    const jxl_cpu_features *feat;
    if (samples == NULL || num_pixels == 0) {
        return;
    }
    feat = jxl_context_cpu_features(ctx);
    switch (tf) {
    case JXL_TRANSFER_SRGB_I:
        linear_to_srgb(feat, samples, num_pixels);
        break;
    case JXL_TRANSFER_BT709_I:
        linear_to_bt709(feat, samples, num_pixels);
        break;
    case JXL_TRANSFER_PQ_I:
        jxl_color_linear_to_pq(ctx, samples, num_pixels, intensity_target);
        break;
    case JXL_TRANSFER_LINEAR_I:
    default:
        break;
    }
}

void jxl_color_transform_apply_inverse_transfer(jxl_context *ctx, float *samples,
                                                size_t num_pixels, jxl_transfer_function_i tf,
                                                float intensity_target) {
    const jxl_cpu_features *feat;
    if (samples == NULL || num_pixels == 0) {
        return;
    }
    feat = jxl_context_cpu_features(ctx);
    switch (tf) {
    case JXL_TRANSFER_SRGB_I:
        srgb_to_linear(feat, samples, num_pixels);
        break;
    case JXL_TRANSFER_BT709_I:
        bt709_to_linear(feat, samples, num_pixels);
        break;
    case JXL_TRANSFER_PQ_I:
        jxl_color_pq_to_linear(ctx, samples, num_pixels, intensity_target);
        break;
    case JXL_TRANSFER_LINEAR_I:
    default:
        break;
    }
}

void jxl_color_transform_xyb_to_srgb(jxl_context *ctx, float *x, float *y, float *b,
                                     size_t num_pixels, const jxl_opsin_inverse_parsed *opsin,
                                     float intensity_target) {
    const jxl_cpu_features *feat = jxl_context_cpu_features(ctx);
    jxl_color_opsin_xyb_to_linear_rgb(ctx, x, y, b, num_pixels, opsin, intensity_target);
    linear_to_srgb(feat, x, num_pixels);
    linear_to_srgb(feat, y, num_pixels);
    linear_to_srgb(feat, b, num_pixels);
}
