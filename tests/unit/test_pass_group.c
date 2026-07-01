// SPDX-License-Identifier: MIT OR Apache-2.0
#include "frame/pass_group.h"
#include "jxl_oxide/jxl_context.h"

#include "allocator.h"
#include <assert.h>
#include "test_helpers.h"
#include <string.h>

static jxl_allocator_state *test_alloc_state(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

static jxl_context *test_library_ctx(void) {
    static jxl_context *ctx = NULL;
    if (ctx == NULL && jxl_context_create(NULL, &ctx) != JXL_OK) {
        assert(0);
    }
    return ctx;
}

static void test_groups_per_row(void) {
    jxl_frame_header h;
    jxl_frame_header_init(&h);
    h.width = 256;
    h.height = 128;
    h.upsampling = 0;
    h.lf_level = 0;
    h.group_size_shift = 1;
    assert(jxl_frame_header_group_dim(&h) == 256);
    assert(jxl_frame_header_groups_per_row(&h) == 1);
    assert(jxl_frame_header_num_groups(&h) == 1);

    h.width = 512;
    assert(jxl_frame_header_groups_per_row(&h) == 2);
    jxl_frame_header_free(test_alloc_state(), &h);
}

static void test_block_slice(void) {
    jxl_frame_header h;
    jxl_pass_group_block_slice slice;
    jxl_frame_header_init(&h);
    h.width = 2048;
    h.height = 2048;
    h.group_size_shift = 1;

    size_t blocks_per_lf = jxl_frame_header_group_dim(&h);
    JXL_TEST_REQUIRE(jxl_pass_group_block_slice_for_group(&h, 0, blocks_per_lf, blocks_per_lf, &slice));
    assert(slice.block_left == 0 && slice.block_top == 0);
    assert(slice.block_width == 32 && slice.block_height == 32);

    JXL_TEST_REQUIRE(jxl_pass_group_block_slice_for_group(&h, 8, blocks_per_lf, blocks_per_lf, &slice));
    assert(slice.block_left == 0 && slice.block_top == 32);
    assert(slice.block_width == 32 && slice.block_height == 32);

    jxl_frame_header_free(test_alloc_state(), &h);
}

static void test_block_info_subgrid(void) {
    jxl_frame_header h;
    jxl_block_info_subgrid bi;
    jxl_lf_group_view lf;
    jxl_frame_header_init(&h);
    h.width = 256;
    h.height = 256;
    h.group_size_shift = 1;

    jxl_block_info blocks[4] = {
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
    };
    lf.block_info_data = blocks;
    lf.block_info_width = 2;
    lf.block_info_height = 2;
    lf.block_info_stride = 2;

    JXL_TEST_REQUIRE(jxl_pass_group_block_info_subgrid(&h, 0, &lf, NULL, &bi));
    assert(bi.width == 2 && bi.height == 2);
    jxl_frame_header_free(test_alloc_state(), &h);
}

static void test_decode_vardct_null_hf_meta(void) {
    jxl_frame_header h;
    jxl_lf_group_view lf = {0};
    jxl_hf_global_view hf = {0};
    int32_t coeff_buf[64] = {0};
    jxl_bs bs;
    jxl_pass_group_vardct_params p = {0};
    jxl_frame_header_init(&h);

    jxl_subgrid_i32 coeff[3] = {
        {coeff_buf, 8, 8, 8},
        {coeff_buf, 8, 8, 8},
        {coeff_buf, 8, 8, 8},
    };

    jxl_bs_init(&bs, NULL, 0);

    p.ctx = test_library_ctx();
    p.frame_header = &h;
    p.lf_group = &lf;
    p.pass_idx = 0;
    p.group_idx = 0;
    p.hf_global = &hf;
    p.hf_coeff_out[0] = coeff[0];
    p.hf_coeff_out[1] = coeff[1];
    p.hf_coeff_out[2] = coeff[2];
    p.allow_partial = 0;

    JXL_TEST_ASSERT_EQ(jxl_decode_pass_group_vardct(&bs, &p), JXL_FRAME_OK);
    jxl_frame_header_free(test_alloc_state(), &h);
}

int main(void) {
    test_groups_per_row();
    test_block_slice();
    test_block_info_subgrid();
    test_decode_vardct_null_hf_meta();
    return 0;
}
