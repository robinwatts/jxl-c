// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image/image_internal.h"
#include "modular/modular.h"
#include "modular/transform/squeeze.h"
#include "jxl_oxide/jxl_context.h"
#if defined(JXL_HAVE_SIMD_SSE41)
#include "modular/transform/squeeze_internal.h"
#endif

#include "allocator.h"
#include "test_helpers.h"

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_sample_ops(void) {
    assert(jxl_modular_unpack_signed_u32(0) == 0);
    assert(jxl_modular_unpack_signed_u32(1) == -1);
    assert(jxl_modular_unpack_signed_u32(2) == 1);
    assert(jxl_modular_unpack_signed_u32_i16(0) == 0);
    assert(jxl_modular_unpack_signed_u32_i16(1) == -1);
    assert(jxl_modular_unpack_signed_u32_i16(2) == 1);
    assert(jxl_modular_i32_grad_clamped(10, 5, 8) == 7);
    assert(jxl_modular_i32_wrapping_muladd(3, 4, 1) == 13);
    assert(jxl_modular_i16_grad_clamped(10, 5, 8) == 7);
    assert(jxl_modular_i16_add((int16_t)30000, (int16_t)30000) == (int16_t)-5536);
}

static void test_modular_grid_i16(void) {
    jxl_modular_grid_i32 g;
    JXL_TEST_REQUIRE(jxl_modular_grid_i16_create(test_alloc(), 4, 2, NULL, &g));
    assert(g.kind == JXL_MODULAR_SAMPLE_I16);
    jxl_modular_grid_store_i32(&g, 0, 0, 42);
    assert(jxl_modular_grid_sample_as_i32(&g, 0, 0) == 42);
    jxl_modular_grid_store_i32(&g, 3, 1, 40000);
    assert(jxl_modular_grid_sample_as_i32(&g, 3, 1) == (int32_t)(int16_t)40000);
    jxl_modular_grid_i32_destroy(test_alloc(), &g);
}

static void test_channel_shift(void) {
    uint32_t ups[3] = {1, 2, 2};
    jxl_channel_shift s0 = jxl_channel_shift_from_jpeg_upsampling(ups, 0);
    assert(s0.has_h_subsample && !s0.h_subsample);
    assert(s0.has_v_subsample && !s0.v_subsample);

    jxl_channel_shift s1 = jxl_channel_shift_from_jpeg_upsampling(ups, 1);
    assert(s1.has_h_subsample && !s1.h_subsample);
    assert(s1.has_v_subsample && s1.v_subsample);

    jxl_channel_shift raw = jxl_channel_shift_raw(2, -1);
    assert(raw.kind == JXL_CHANNEL_SHIFT_RAW);
    assert(raw.raw_h == 2);
    assert(raw.raw_v == -1);
}

static void test_modular_params(void) {
    jxl_modular_params p;
    jxl_channel_shift shifts[2];
    jxl_modular_params_init(&p);
    shifts[0] = jxl_channel_shift_from_shift(1);
    shifts[1] = jxl_channel_shift_from_shift(0);

    if (!jxl_modular_params_set_channels(test_alloc(), &p, 64, 32, 256, 8, shifts, 2)) {
        assert(0);
    }
    assert(p.num_channels == 2);
    assert(p.channels[0].width == 64);
    assert(p.channels[1].height == 32);
    assert(p.bit_depth == 8);
    jxl_modular_params_free(test_alloc(), &p);
}

static void test_i16_i32_wrap_divergence(void) {
    int32_t a = 30000;
    int32_t b = 30000;
    assert(jxl_modular_i32_add(a, b) == 60000);
    assert(jxl_modular_i16_add((int16_t)a, (int16_t)b) == (int16_t)-5536);
    assert(jxl_modular_i32_add(a, b) != (int32_t)jxl_modular_i16_add((int16_t)a, (int16_t)b));
}

static void test_squeeze_i16_vs_i32(void) {
    /* 4-sample horizontal squeeze: avg/residu layout after merge is [a0,a1,r0]. */
    int32_t merged_i32[4] = {1000, 2000, 500, 0};
    int16_t merged_i16[4];
    merged_i16[0] = (int16_t)30000;
    merged_i16[1] = (int16_t)30000;
    merged_i16[2] = (int16_t)1000;
    merged_i16[3] = 0;

    jxl_squeeze_inverse_h_i32(test_alloc(), merged_i32, 4, 1, 4);
    jxl_squeeze_inverse_h_i16(NULL, test_alloc(), merged_i16, 4, 1, 4);
    assert(merged_i32[0] != (int32_t)merged_i16[0] || merged_i32[1] != (int32_t)merged_i16[1]);
}

#if defined(JXL_HAVE_SIMD_SSE41)
static void squeeze_fill_pattern(int16_t *buf, size_t width, size_t height, size_t stride, int16_t seed) {
    size_t y;
    for (y = 0; y < height; ++y) {
        size_t x;
        for (x = 0; x < width; ++x) {
            buf[y * stride + x] = (int16_t)(seed + (int16_t)(x * 17 + y * 31));
        }
    }
}

static int squeeze_buffers_equal(const int16_t *a, const int16_t *b, size_t width, size_t height,
                                 size_t stride) {
                                     size_t y;
    for (y = 0; y < height; ++y) {
        size_t x;
        for (x = 0; x < width; ++x) {
            if (a[y * stride + x] != b[y * stride + x]) {
                return 0;
            }
        }
    }
    return 1;
}

static void test_squeeze_h_simd_matches_base(size_t width, size_t height) {
    size_t stride = width;
    size_t n = stride * height;
    int16_t *base = (int16_t *)malloc(n * sizeof(*base));
    int16_t *simd = (int16_t *)malloc(n * sizeof(*simd));
    assert(base != NULL && simd != NULL);
    squeeze_fill_pattern(base, width, height, stride, 100);
    memcpy(simd, base, n * sizeof(*base));
    jxl_squeeze_inverse_h_i16_base(test_alloc(), base, width, height, stride);
    jxl_squeeze_inverse_h_i16(NULL, test_alloc(), simd, width, height, stride);
    assert(squeeze_buffers_equal(base, simd, width, height, stride));
    free(base);
    free(simd);
}

static void test_squeeze_v_simd_matches_base(size_t width, size_t height) {
    size_t stride = width;
    size_t n = stride * height;
    int16_t *base = (int16_t *)malloc(n * sizeof(*base));
    int16_t *simd = (int16_t *)malloc(n * sizeof(*simd));
    assert(base != NULL && simd != NULL);
    squeeze_fill_pattern(base, width, height, stride, -50);
    memcpy(simd, base, n * sizeof(*base));
    jxl_squeeze_inverse_v_i16_base(test_alloc(), base, width, height, stride);
    jxl_squeeze_inverse_v_i16(NULL, test_alloc(), simd, width, height, stride);
    assert(squeeze_buffers_equal(base, simd, width, height, stride));
    free(base);
    free(simd);
}

static void test_squeeze_simd_matches_base(void) {
    size_t i;
    static const size_t sizes[][2] = {
        {4, 1}, {8, 8}, {17, 9}, {24, 16}, {33, 17}, {48, 24}, {64, 32}, {80, 40},
    };
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        test_squeeze_h_simd_matches_base(sizes[i][0], sizes[i][1]);
        test_squeeze_v_simd_matches_base(sizes[i][0], sizes[i][1]);
    }
}
#endif

#ifndef NDEBUG
static void test_force_wide_buffers_env(void) {
    jxl_parsed_image_header hdr;
    jxl_context *ctx = NULL;
    memset(&hdr, 0, sizeof(hdr));
    hdr.modular_16bit_buffers = 1;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);
    if (!jxl_parsed_narrow_modular(ctx, &hdr)) {
        assert(0);
    }
#if defined(_POSIX_VERSION) || defined(_WIN32)
    jxl_context_destroy(ctx);
    ctx = NULL;
    setenv("JXL_FORCE_WIDE_BUFFERS", "1", 1);
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);
    assert(!jxl_parsed_narrow_modular(ctx, &hdr));
    jxl_context_destroy(ctx);
    ctx = NULL;
    unsetenv("JXL_FORCE_WIDE_BUFFERS");
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);
    if (!jxl_parsed_narrow_modular(ctx, &hdr)) {
        assert(0);
    }
#endif
    jxl_context_destroy(ctx);
}
#endif /* !NDEBUG */

static void test_grid_views_i16(void) {
    size_t y;
    jxl_modular_grid g;
    jxl_modular_grid group;
    JXL_TEST_REQUIRE(jxl_modular_grid_i16_create(test_alloc(), 8, 4, NULL, &g));
    assert(g.stride == g.width);
    assert(jxl_modular_grid_row_stride(&g) == g.width);
    for (y = 0; y < g.height; ++y) {
        size_t x;
        for (x = 0; x < g.width; ++x) {
            jxl_modular_grid_store_i32(&g, x, y, (int32_t)((x + 1) * 1000 + y));
        }
    }

    jxl_modular_grid tile = jxl_modular_grid_tile_view(&g, 2, 1, 3, 2);
    assert(tile.kind == JXL_MODULAR_SAMPLE_I16);
    assert(tile.width == 3 && tile.height == 2);
    assert(tile.stride == g.stride);
    assert(jxl_modular_grid_sample_as_i32(&tile, 0, 0) == 3001);
    assert(jxl_modular_grid_sample_as_i32(&tile, 2, 1) == 5002);

    JXL_TEST_REQUIRE(jxl_modular_grid_group_view_at(&g, 4, 2, 2, 2, 2, &group));
    assert(group.kind == JXL_MODULAR_SAMPLE_I16);
    assert(group.width == 4 && group.height == 2);
    assert(jxl_modular_grid_sample_as_i32(&group, 3, 0) == 4002);

    jxl_modular_grid right = jxl_modular_grid_split_horizontal_in_place(&g);
    assert(g.width == 4 && right.width == 4);
    assert(g.kind == JXL_MODULAR_SAMPLE_I16 && right.kind == JXL_MODULAR_SAMPLE_I16);
    assert(jxl_modular_grid_sample_as_i32(&g, 3, 0) == 4000);
    assert(jxl_modular_grid_sample_as_i32(&right, 0, 0) == 5000);

    jxl_modular_grid bottom = jxl_modular_grid_split_vertical_in_place(&g);
    assert(g.height == 2 && bottom.height == 2);
    assert(jxl_modular_grid_sample_as_i32(&bottom, 0, 0) == 1002);

    jxl_modular_grid_i32_destroy(test_alloc(), &g);
}

static void test_grid_views_i32(void) {
    jxl_modular_grid g;
    JXL_TEST_REQUIRE(jxl_modular_grid_i32_create(test_alloc(), 6, 2, NULL, &g));
    jxl_modular_grid_store_i32(&g, 5, 1, 123456);
    jxl_modular_grid tile = jxl_modular_grid_tile_view(&g, 4, 1, 2, 1);
    assert(tile.kind == JXL_MODULAR_SAMPLE_I32);
    assert(jxl_modular_grid_sample_as_i32(&tile, 1, 0) == 123456);
    jxl_modular_grid_i32_destroy(test_alloc(), &g);
}

int main(void) {
    test_sample_ops();
    test_modular_grid_i16();
    test_channel_shift();
    test_modular_params();
    test_i16_i32_wrap_divergence();
    test_squeeze_i16_vs_i32();
#if defined(JXL_HAVE_SIMD_SSE41)
    test_squeeze_simd_matches_base();
#endif
#ifndef NDEBUG
    test_force_wide_buffers_env();
#endif
    test_grid_views_i16();
    test_grid_views_i32();
    printf("test_modular: ok\n");
    return 0;
}
