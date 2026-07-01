// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream/bitstream.h"
#include "context.h"
#include "vardct/vardct.h"

#include <assert.h>
#include "test_helpers.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_dct_select(void) {
    jxl_transform_type t = JXL_TRANSFORM_DCT8;
    uint32_t w;
    uint32_t h;
    JXL_TEST_REQUIRE(jxl_transform_type_from_u8(0, &t));
    assert(t == JXL_TRANSFORM_DCT8);
    assert(!jxl_transform_type_from_u8(200, &t));

    w = 0;
    h = 0;
    jxl_transform_dct_select_size(JXL_TRANSFORM_DCT32, &w, &h);
    assert(w == 4 && h == 4);
    jxl_transform_dequant_matrix_size(JXL_TRANSFORM_DCT32, &w, &h);
    assert(w == 32 && h == 32);
    assert(jxl_transform_dequant_matrix_param_index(JXL_TRANSFORM_DCT16X8) == 6);
    assert(jxl_transform_order_id(JXL_TRANSFORM_DCT64) == 7);
    JXL_TEST_REQUIRE(jxl_transform_need_transpose(JXL_TRANSFORM_DCT16X8));
    assert(!jxl_transform_need_transpose(JXL_TRANSFORM_DCT4));
}

/* all_default dequant + correlation; quantizer global_scale=1, quant_lf=16; default HF ctx. */
static const uint8_t k_lf_defaults[] = {0x03, 0x00, 0x02};

static void test_lf_bundles_default(void) {
    jxl_allocator_state alloc;
    jxl_bs bs;
    jxl_lf_channel_dequant dequant;
    jxl_lf_channel_correlation corr;
    jxl_quantizer q;
    jxl_hf_block_context ctx;
    jxl_allocator_init(&alloc, NULL);

    jxl_bs_init(&bs, k_lf_defaults, sizeof(k_lf_defaults));

    JXL_TEST_ASSERT_EQ(jxl_lf_channel_dequant_parse(&bs, &dequant), JXL_VARDCT_OK);
    assert(fabsf(dequant.m_x_lf - 1.0f / 32.0f) < 1e-6f);
    assert(fabsf(jxl_lf_channel_dequant_m_x_unscaled(&dequant) - (1.0f / 32.0f) / 128.0f) < 1e-6f);

    JXL_TEST_ASSERT_EQ(jxl_lf_channel_correlation_parse(&bs, &corr), JXL_VARDCT_OK);
    assert(corr.colour_factor == 84);
    assert(corr.x_factor_lf == 128);

    JXL_TEST_ASSERT_EQ(jxl_quantizer_parse(&bs, &q), JXL_VARDCT_OK);
    assert(q.global_scale == 1);
    assert(q.quant_lf == 16);

    memset(&ctx, 0, sizeof(ctx));
    JXL_TEST_ASSERT_EQ(jxl_hf_block_context_parse(&alloc, &bs, &ctx), JXL_VARDCT_OK);
    assert(ctx.num_block_clusters == 15);
    assert(ctx.block_ctx_map_len == 39);
    assert(ctx.block_ctx_map[0] == 0 && ctx.block_ctx_map[38] == 14);
    jxl_hf_block_context_free(&alloc, &ctx);
}

static void test_dequant_all_default(void) {
    jxl_allocator_state alloc;
    jxl_bs bs;
    uint8_t byte;
    jxl_dequant_matrix_set set;
    jxl_dequant_matrix_set_params params = {0};
    jxl_context *library_ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &library_ctx), JXL_OK);

    jxl_allocator_init(&alloc, NULL);

    byte = 0x01;
    jxl_bs_init(&bs, &byte, 1);

    jxl_dequant_matrix_set_init(&set);
    params.bit_depth = 8;
    params.stream_index = jxl_dequant_matrix_set_stream_index(0);
    params.global_ma = NULL;

    JXL_TEST_ASSERT_EQ(jxl_dequant_matrix_set_parse(library_ctx, &alloc, &bs, &params, &set), JXL_VARDCT_OK);
    assert(set.matrices[0].encoding == JXL_DEQUANT_ENC_DEFAULT);
    assert(set.matrices[0].dct_band_lens[0] == 6);
    assert(set.matrices[1].encoding == JXL_DEQUANT_ENC_DEFAULT);
    jxl_dequant_matrix_set_free(&set);
    jxl_context_destroy(library_ctx);
}

static void test_hf_pass_natural_order(void) {
    size_t len;
    jxl_context *library_ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &library_ctx), JXL_OK);

    len = 0;
    const jxl_coeff_order *order = jxl_hf_pass_dct8_natural_order(library_ctx, &len);
    assert(len == 64);
    assert(order[0].x == 0 && order[0].y == 0);
    assert(order[1].x == 1 && order[1].y == 0);
    jxl_context_destroy(library_ctx);
}

static void test_hf_pass_used_orders_zero(void) {
    jxl_allocator_state alloc;
    jxl_hf_block_context ctx;
    jxl_bs bs;
    uint8_t buf[4] = {0x02, 0x00, 0x00, 0x00};
    jxl_hf_pass pass;
    jxl_hf_pass_params params = {0};
    jxl_context *library_ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &library_ctx), JXL_OK);

    jxl_allocator_init(&alloc, NULL);

    /* used_orders=0 (selector 0 for 0x5F), then minimal hf_dist fails — use clusters=0 invalid */
    memset(&ctx, 0, sizeof(ctx));
    ctx.num_block_clusters = 1;

    /* used_orders U32 selector 2 → const 0 */
    jxl_bs_init(&bs, buf, sizeof(buf));

    params.hf_block_ctx = &ctx;
    params.num_hf_presets = 1;

    jxl_hf_pass_init(&pass);
    jxl_vardct_status_t st = jxl_hf_pass_parse(library_ctx, &alloc, &bs, &params, &pass);
    assert(st == JXL_VARDCT_DECODER_ERROR || st == JXL_VARDCT_BITSTREAM_ERROR);
    jxl_hf_pass_destroy(&alloc, &pass);
    jxl_context_destroy(library_ctx);
}

int main(void) {
    test_dct_select();
    test_lf_bundles_default();
    test_dequant_all_default();
    test_hf_pass_natural_order();
    test_hf_pass_used_orders_zero();
    printf("test_vardct: ok\n");
    return 0;
}
