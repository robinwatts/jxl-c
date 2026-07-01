// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color_transform.h"
#include "render/color/color_transform_internal.h"
#include "render/color/fastmath.h"
#include "render/color/pq_internal.h"
#include "render/color/rec2408.h"
#include "render/color/gamut.h"
#include "render/color/tone_map.h"
#include "render/color/rec2408_internal.h"
#include "render/color/gamut_internal.h"
#include "render/color/tone_map_internal.h"

#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int float_eq(float a, float b) {
    float d = fabsf(a - b);
    return d <= 1e-5f * fmaxf(1.0f, fabsf(a));
}

static void test_linear_to_srgb_matches_base(void) {
    size_t i;
    static const float inputs[] = {-0.5f, 0.0f, 0.001f, 0.0031308f, 0.01f, 0.1f, 0.5f, 1.0f, 2.0f,
                                   0.02f, 0.03f, 0.04f, 0.2f, 0.3f, 0.7f, 1.5f};
    float base[sizeof(inputs) / sizeof(inputs[0])];
    float simd[sizeof(inputs) / sizeof(inputs[0])];
    memcpy(base, inputs, sizeof(inputs));
    memcpy(simd, inputs, sizeof(inputs));

    jxl_color_linear_to_srgb_base(base, sizeof(base) / sizeof(base[0]));
    jxl_color_transform_apply_forward_transfer(NULL, simd, sizeof(simd) / sizeof(simd[0]), JXL_TRANSFER_SRGB_I,
                                               0.0f);
    for (i = 0; i < sizeof(base) / sizeof(base[0]); ++i) {
        if (!float_eq(base[i], simd[i])) {
            fprintf(stderr, "srgb mismatch at %zu: base=%g simd=%g\n", i, base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_linear_to_bt709_matches_base(void) {
    size_t i;
    static const float inputs[] = {0.0f, 0.01f, 0.018f, 0.02f, 0.1f, 0.5f, 1.0f, 0.015f,
                                   0.5f, 0.6f, 0.7f, 0.8f};
    float base[sizeof(inputs) / sizeof(inputs[0])];
    float simd[sizeof(inputs) / sizeof(inputs[0])];
    memcpy(base, inputs, sizeof(inputs));
    memcpy(simd, inputs, sizeof(inputs));

    jxl_color_linear_to_bt709_base(base, sizeof(base) / sizeof(base[0]));
    jxl_color_transform_apply_forward_transfer(NULL, simd, sizeof(simd) / sizeof(simd[0]), JXL_TRANSFER_BT709_I,
                                               0.0f);
    for (i = 0; i < sizeof(base) / sizeof(base[0]); ++i) {
        if (!float_eq(base[i], simd[i])) {
            fprintf(stderr, "bt709 mismatch at %zu: base=%g simd=%g\n", i, base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_fastmath_powf_matches_scalar(void) {
    size_t i;
    float buf[32];
    float ref[32];
    for (i = 0; i < 32; ++i) {
        buf[i] = 0.01f + (float)i * 0.03f;
    }
    memcpy(ref, buf, sizeof(ref));
    for (i = 0; i < 32; ++i) {
        ref[i] = jxl_fastmath_powf(ref[i], 0.45f);
    }
    jxl_fastmath_powf_in_place(NULL, buf, 32, 0.45f);
    for (i = 0; i < 32; ++i) {
        if (!float_eq(ref[i], buf[i])) {
            fprintf(stderr, "powf mismatch at %zu: ref=%g simd=%g\n", i, ref[i], buf[i]);
            assert(0);
        }
    }
}

static void test_linear_to_pq_matches_base(void) {
    size_t i;
    float base[64];
    float simd[64];
    for (i = 0; i < 64; ++i) {
        base[i] = (float)i * 1e-4f;
        simd[i] = base[i];
    }
    jxl_color_linear_to_pq_base(base, 64, 10000.0f);
    jxl_color_transform_apply_forward_transfer(NULL, simd, 64, JXL_TRANSFER_PQ_I, 10000.0f);
    for (i = 0; i < 64; ++i) {
        if (!float_eq(base[i], simd[i])) {
            fprintf(stderr, "pq forward mismatch at %zu: base=%g simd=%g\n", i, base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_pq_roundtrip_matches_base(void) {
    size_t i;
    float base[32];
    float simd[32];
    for (i = 0; i < 32; ++i) {
        base[i] = (float)i * 1e-5f;
        simd[i] = base[i];
    }
    jxl_color_linear_to_pq_base(base, 32, 10000.0f);
    jxl_color_pq_to_linear_base(base, 32, 1000.0f);
    jxl_color_transform_apply_forward_transfer(NULL, simd, 32, JXL_TRANSFER_PQ_I, 10000.0f);
    jxl_color_transform_apply_inverse_transfer(NULL, simd, 32, JXL_TRANSFER_PQ_I, 1000.0f);
    for (i = 0; i < 32; ++i) {
        float expected = (float)i * 1e-4f;
        if (!float_eq(expected, base[i]) || !float_eq(base[i], simd[i])) {
            fprintf(stderr, "pq roundtrip mismatch at %zu: expected=%g base=%g simd=%g\n", i, expected,
                    base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_srgb_to_linear_matches_base(void) {
    size_t i;
    static const float inputs[] = {-0.5f, 0.0f, 0.01f, 0.04045f, 0.05f, 0.1f, 0.5f, 1.0f,
                                   0.02f, 0.03f, 0.04f, 0.2f, 0.3f, 0.7f, 1.5f, -0.1f};
    float base[sizeof(inputs) / sizeof(inputs[0])];
    float simd[sizeof(inputs) / sizeof(inputs[0])];
    memcpy(base, inputs, sizeof(inputs));
    memcpy(simd, inputs, sizeof(inputs));

    jxl_color_srgb_to_linear_base(base, sizeof(base) / sizeof(base[0]));
    jxl_color_transform_apply_inverse_transfer(NULL, simd, sizeof(simd) / sizeof(simd[0]), JXL_TRANSFER_SRGB_I,
                                             0.0f);
    for (i = 0; i < sizeof(base) / sizeof(base[0]); ++i) {
        if (!float_eq(base[i], simd[i])) {
            fprintf(stderr, "srgb inverse mismatch at %zu: base=%g simd=%g\n", i, base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_rec2408_eetf_matches_base(void) {
    size_t i;
    const jxl_luminance_nits_range from = {0.0f, 10000.0f};
    const jxl_luminance_nits_range to = {0.0f, 255.0f};
    float base[32];
    float simd[32];
    for (i = 0; i < 32; ++i) {
        base[i] = (float)i * 0.01f;
        simd[i] = base[i];
    }
    const jxl_rec2408_eetf_params params = jxl_rec2408_eetf_prep(10000.0f, from, to);
    jxl_rec2408_eetf_pq_base(base, 32, &params);
    jxl_rec2408_eetf_pq(NULL, simd, 32, 10000.0f, from, to);
    for (i = 0; i < 32; ++i) {
        if (!float_eq(base[i], simd[i])) {
            fprintf(stderr, "rec2408 mismatch at %zu: base=%g simd=%g\n", i, base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_gamut_map_matches_base(void) {
    size_t i;
    static const float luminances[3] = {0.2126f, 0.7152f, 0.0722f};
    float base_g[16];
    float base_b[16];
    float simd_r[16];
    float simd_g[16];
    float simd_b[16];
    float base_r[16] = {0.2f, 0.5f, 1.2f, -0.1f, 0.0f, 0.8f, 1.5f, 0.3f,
                        0.9f, 1.1f, 0.4f, 0.6f, 0.7f, 1.3f, 0.1f, 2.0f};
    memcpy(base_g, base_r, sizeof(base_r));
    memcpy(base_b, base_r, sizeof(base_r));
    memcpy(simd_r, base_r, sizeof(base_r));
    memcpy(simd_g, base_r, sizeof(base_r));
    memcpy(simd_b, base_r, sizeof(base_r));

    jxl_color_gamut_map_base(base_r, base_g, base_b, 16, luminances, 0.85f);
    jxl_color_gamut_map(NULL, simd_r, simd_g, simd_b, 16, luminances, 0.85f);
    for (i = 0; i < 16; ++i) {
        if (!float_eq(base_r[i], simd_r[i]) || !float_eq(base_g[i], simd_g[i]) ||
            !float_eq(base_b[i], simd_b[i])) {
            fprintf(stderr, "gamut mismatch at %zu\n", i);
            assert(0);
        }
    }
}

static void test_tone_map_matches_base(void) {
    size_t i;
    float base_r[10];
    float base_g[10];
    float base_b[10];
    float simd_r[10];
    float simd_g[10];
    float simd_b[10];
    jxl_hdr_params hdr = {0};
    for (i = 0; i < 10; ++i) {
        const float v = (float)(i / 5) * 0.1f;
        base_r[i] = v;
        base_g[i] = v;
        base_b[i] = v;
        simd_r[i] = v;
        simd_g[i] = v;
        simd_b[i] = v;
    }
    hdr.luminances[0] = 0.2126f;
    hdr.luminances[1] = 0.7152f;
    hdr.luminances[2] = 0.0722f;
    hdr.intensity_target = 10000.0f;
    hdr.min_nits = 0.0f;

    const jxl_tone_map_params params = jxl_tone_map_prep(&hdr, 255.0f, 10000.0f);

    jxl_color_tone_map_base(base_r, base_g, base_b, 10, &hdr, &params);
    jxl_color_tone_map(NULL, simd_r, simd_g, simd_b, 10, &hdr, 255.0f, 0);

    for (i = 0; i < 10; ++i) {
        const float expected = (float)(i / 5) * 0.8714331f;
        if (!float_eq(expected, base_r[i]) || !float_eq(base_r[i], simd_r[i])) {
            fprintf(stderr, "tone_map mismatch at %zu: expected=%g base=%g simd=%g\n", i, expected,
                    base_r[i], simd_r[i]);
            assert(0);
        }
    }
}

static void test_bt709_to_linear_matches_base(void) {
    size_t i;
    static const float inputs[] = {0.0f, 0.05f, 0.081f, 0.1f, 0.18f, 0.5f, 1.0f,
                                   0.02f, 0.4f, 0.6f, 0.8f, 0.9f};
    float base[sizeof(inputs) / sizeof(inputs[0])];
    float simd[sizeof(inputs) / sizeof(inputs[0])];
    memcpy(base, inputs, sizeof(inputs));
    memcpy(simd, inputs, sizeof(inputs));

    jxl_color_bt709_to_linear_base(base, sizeof(base) / sizeof(base[0]));
    jxl_color_transform_apply_inverse_transfer(NULL, simd, sizeof(simd) / sizeof(simd[0]),
                                               JXL_TRANSFER_BT709_I, 0.0f);
    for (i = 0; i < sizeof(base) / sizeof(base[0]); ++i) {
        if (!float_eq(base[i], simd[i])) {
            fprintf(stderr, "bt709 inverse mismatch at %zu: base=%g simd=%g\n", i, base[i], simd[i]);
            assert(0);
        }
    }
}

static void test_apply_gamma_matches_base(void) {
    size_t g;
    static const float inputs[] = {0.0f, 1e-8f, 1e-6f, 0.01f, 0.1f, 0.5f, 1.0f, 2.0f,
                                   0.02f, 0.03f, 0.2f, 0.7f};
    static const float gammas[] = {2.6f, 1.0f / 2.6f, 0.45f, 2.2f};
    for (g = 0; g < sizeof(gammas) / sizeof(gammas[0]); ++g) {
        size_t i;
        float base[sizeof(inputs) / sizeof(inputs[0])];
        float simd[sizeof(inputs) / sizeof(inputs[0])];
        memcpy(base, inputs, sizeof(inputs));
        memcpy(simd, inputs, sizeof(inputs));
        jxl_color_apply_gamma_base(base, sizeof(base) / sizeof(base[0]), gammas[g]);
        jxl_color_apply_gamma(NULL, simd, sizeof(simd) / sizeof(simd[0]), gammas[g]);
        for (i = 0; i < sizeof(base) / sizeof(base[0]); ++i) {
            if (!float_eq(base[i], simd[i])) {
                fprintf(stderr, "apply_gamma mismatch gamma=%g at %zu: base=%g simd=%g\n", gammas[g],
                        i, base[i], simd[i]);
                assert(0);
            }
        }
    }
}

static void test_detect_peak_luminance_matches_base(void) {
    size_t i;
    float r[16];
    float g[16];
    float b[16];
    const float luminances[3] = {0.2126f, 0.7152f, 0.0722f};
    for (i = 0; i < 16; ++i) {
        r[i] = (float)i * 0.05f;
        g[i] = (float)(15 - i) * 0.04f;
        b[i] = (float)(i % 5) * 0.08f;
    }
    const float base_peak = jxl_color_detect_peak_luminance_base(r, g, b, 16, luminances);
    const float simd_peak = jxl_color_detect_peak_luminance(NULL, r, g, b, 16, luminances);
    if (!float_eq(base_peak, simd_peak)) {
        fprintf(stderr, "detect_peak mismatch: base=%g simd=%g\n", base_peak, simd_peak);
        assert(0);
    }
    if (jxl_color_detect_peak_luminance(NULL, NULL, g, b, 0, luminances) != 1.0f) {
        assert(0);
    }
}

int main(void) {
    test_linear_to_srgb_matches_base();
    test_linear_to_bt709_matches_base();
    test_fastmath_powf_matches_scalar();
    test_srgb_to_linear_matches_base();
    test_linear_to_pq_matches_base();
    test_pq_roundtrip_matches_base();
    test_rec2408_eetf_matches_base();
    test_gamut_map_matches_base();
    test_tone_map_matches_base();
    test_bt709_to_linear_matches_base();
    test_apply_gamma_matches_base();
    test_detect_peak_luminance_matches_base();
    printf("test_color_simd: ok\n");
    return 0;
}
