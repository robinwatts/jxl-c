// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/dct.h"


#include "render/subgrid_f32.h"
#include "render/vardct/dct_2d.h"
#include "render/vardct/transform.h"

#include "allocator.h"
#include <assert.h>
#include "test_helpers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static jxl_allocator_state *test_alloc_state(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int quantize(float v) {
    return (int)(v * 65536.0f);
}

static void expect_forward_dct(const float *original, size_t n) {
    size_t i;
    size_t k;
    float *io = (float *)malloc(n * sizeof(float));
    float *scratch = (float *)malloc(n * sizeof(float));
    assert(io != NULL && scratch != NULL);
    for (i = 0; i < n; ++i) {
        io[i] = original[i];
    }
    jxl_dct_1d(io, n, scratch, JXL_DCT_FORWARD);
    for (k = 0; k < n; ++k) {
        size_t i;
        double exp_value = 0.0;
        for (i = 0; i < n; ++i) {
            double cosv = cos((double)(k * (2 * i + 1)) / (double)n * M_PI * 0.5);
            exp_value += (double)original[i] * cosv;
        }
        exp_value /= (double)n;
        if (k != 0) {
            exp_value *= 1.4142135623730951;
        }
        assert(quantize((float)exp_value) == quantize(io[k]));
    }
    free(scratch);
    free(io);
}

static void expect_inverse_dct(const float *original, size_t n) {
    size_t i;
    size_t k;
    float *io = (float *)malloc(n * sizeof(float));
    float *scratch = (float *)malloc(n * sizeof(float));
    assert(io != NULL && scratch != NULL);
    for (i = 0; i < n; ++i) {
        io[i] = original[i];
    }
    jxl_dct_1d(io, n, scratch, JXL_DCT_INVERSE);
    for (k = 0; k < n; ++k) {
        size_t i;
        double exp_value = (double)original[0];
        for (i = 1; i < n; ++i) {
            double cosv = cos((double)(i * (2 * k + 1)) / (double)n * M_PI * 0.5);
            exp_value += (double)original[i] * cosv * 1.4142135623730951;
        }
        assert(quantize((float)exp_value) == quantize(io[k]));
    }
    free(scratch);
    free(io);
}

static void test_forward_2_4_8(void) {
    const float d2[] = {-1.0f, 3.0f};
    const float d4[] = {-1.0f, 2.0f, 3.0f, -4.0f};
    const float d8[] = {1.0f, 0.3f, 1.0f, 2.0f, -2.0f, -0.1f, 1.0f, 0.1f};
    expect_forward_dct(d2, 2);
    expect_forward_dct(d4, 4);
    expect_forward_dct(d8, 8);
}

static void test_inverse_2_4_8(void) {
    const float d2[] = {3.0f, 0.2f};
    const float d4[] = {3.0f, 0.2f, 0.3f, -1.0f};
    const float d8[] = {3.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.3f, 0.2f, 0.0f};
    expect_inverse_dct(d2, 2);
    expect_inverse_dct(d4, 4);
    expect_inverse_dct(d8, 8);
}

static void test_dct_2d_row_matches_1d(void) {
    size_t i;
    size_t k;
    const float original[] = {1.0f, 0.3f, 1.0f, 2.0f, -2.0f, -0.1f, 1.0f, 0.1f};
    const size_t n = 8;
    float row_io[8];
    float row_scratch[8];
    float grid_io[8];
    float grid_scratch[8];
    for (i = 0; i < n; ++i) {
        row_io[i] = original[i];
        grid_io[i] = original[i];
    }
    jxl_dct_1d(row_io, n, row_scratch, JXL_DCT_FORWARD);
    jxl_subgrid_f32 sg = jxl_subgrid_f32_from_buf(grid_io, n, 1, n);
    jxl_dct_2d(test_alloc_state(), sg, JXL_DCT_FORWARD);
    for (k = 0; k < n; ++k) {
        assert(quantize(row_io[k]) == quantize(grid_io[k]));
    }
}

static void test_transform_dct8_smoke(void) {
    size_t i;
    float block[64];
    for (i = 0; i < 64; ++i) {
        block[i] = (float)(i % 7) * 0.01f;
    }
    jxl_subgrid_f32 coeff = jxl_subgrid_f32_from_buf(block, 8, 8, 8);
    jxl_render_transform_varblock(NULL, test_alloc_state(), coeff, JXL_TRANSFORM_DCT8);
}

static void test_roundtrip_8(void) {
    size_t i;
    const float orig[] = {1.0f, 0.3f, 1.0f, 2.0f, -2.0f, -0.1f, 1.0f, 0.1f};
    float io[8];
    float scratch[8];
    for (i = 0; i < 8; ++i) {
        io[i] = orig[i];
    }
    jxl_dct_1d(io, 8, scratch, JXL_DCT_FORWARD);
    jxl_dct_1d(io, 8, scratch, JXL_DCT_INVERSE);
    for (i = 0; i < 8; ++i) {
        int got = quantize(io[i]);
        int expected = quantize(orig[i]);
        int diff = got - expected;
        if (diff < 0) {
            diff = -diff;
        }
        assert(diff <= 1);
    }
}

#if defined(JXL_HAVE_SIMD_SSE2)
#include "render/vardct/transform_sse2.h"

static void test_dct_2d_simd_matches_generic(void) {
    size_t i;
    jxl_allocator_state *alloc = test_alloc_state();
    float *generic_buf = (float *)jxl_alloc_aligned(alloc, 16, 64 * sizeof(float));
    float *simd_buf = (float *)jxl_alloc_aligned(alloc, 16, 64 * sizeof(float));
    assert(generic_buf != NULL && simd_buf != NULL);
    for (i = 0; i < 64; ++i) {
        generic_buf[i] = (float)((i * 13 + 7) % 17) * 0.05f - 0.1f;
        simd_buf[i] = generic_buf[i];
    }

    jxl_subgrid_f32 generic_sg = jxl_subgrid_f32_from_buf(generic_buf, 8, 8, 8);
    jxl_subgrid_f32 simd_sg = jxl_subgrid_f32_from_buf(simd_buf, 8, 8, 8);

    jxl_dct_2d_generic(alloc, generic_sg, JXL_DCT_FORWARD);
    jxl_dct_2d(alloc, simd_sg, JXL_DCT_FORWARD);
    for (i = 0; i < 64; ++i) {
        int diff = quantize(generic_buf[i]) - quantize(simd_buf[i]);
        if (diff < 0) {
            diff = -diff;
        }
        assert(diff <= 1);
    }

    jxl_dct_2d_generic(alloc, generic_sg, JXL_DCT_INVERSE);
    jxl_dct_2d(alloc, simd_sg, JXL_DCT_INVERSE);
    for (i = 0; i < 64; ++i) {
        int diff = quantize(generic_buf[i]) - quantize(simd_buf[i]);
        if (diff < 0) {
            diff = -diff;
        }
        assert(diff <= 1);
    }

    jxl_free_aligned(alloc, generic_buf);
    jxl_free_aligned(alloc, simd_buf);
}

static void test_transform_simd_matches_fallback(void) {
    size_t t;
    static const jxl_transform_type types[] = {
        JXL_TRANSFORM_DCT8, JXL_TRANSFORM_DCT4, JXL_TRANSFORM_DCT4X8, JXL_TRANSFORM_DCT8X4,
    };
    jxl_allocator_state *alloc = test_alloc_state();

    for (t = 0; t < sizeof(types) / sizeof(types[0]); ++t) {
        size_t i;
        float *ref = (float *)jxl_alloc_aligned(alloc, 16, 64 * sizeof(float));
        float *simd = (float *)jxl_alloc_aligned(alloc, 16, 64 * sizeof(float));
        assert(ref != NULL && simd != NULL);
        for (i = 0; i < 64; ++i) {
            ref[i] = (float)((i * 13 + 7) % 17) * 0.05f - 0.1f;
            simd[i] = ref[i];
        }

        jxl_render_transform_varblock_fallback(NULL, alloc, jxl_subgrid_f32_from_buf(ref, 8, 8, 8), types[t]);
        JXL_TEST_REQUIRE(jxl_render_transform_varblock_sse2(alloc, jxl_subgrid_f32_from_buf(simd, 8, 8, 8),
                                                          types[t]));

        for (i = 0; i < 64; ++i) {
            int diff = quantize(ref[i]) - quantize(simd[i]);
            if (diff < 0) {
                diff = -diff;
            }
            assert(diff <= 1);
        }

        jxl_free_aligned(alloc, ref);
        jxl_free_aligned(alloc, simd);
    }
}
#endif

int main(void) {
    test_forward_2_4_8();
    test_inverse_2_4_8();
    test_dct_2d_row_matches_1d();
    test_transform_dct8_smoke();
    test_roundtrip_8();
#if defined(JXL_HAVE_SIMD_SSE2)
    test_dct_2d_simd_matches_generic();
    test_transform_simd_matches_fallback();
#endif
    return 0;
}
