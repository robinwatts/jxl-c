// SPDX-License-Identifier: MIT OR Apache-2.0
#include "context.h"
#include "frame/frame_header.h"
#include "frame/pass_group.h"
#include "render/vardct/dequant_hf.h"
#include "render/vardct/group_pipeline.h"
#include "vardct/dequant.h"
#include "vardct/dequant_expand.h"


static jxl_allocator_state *test_alloc_state(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}
#include "allocator.h"
#include <assert.h>
#include "test_helpers.h"
#include <math.h>
#include <string.h>

static const jxl_transform_type k_dct_select_list[JXL_DEQUANT_MATRIX_COUNT] = {
    JXL_TRANSFORM_DCT8,      JXL_TRANSFORM_HORNUSS,   JXL_TRANSFORM_DCT2,
    JXL_TRANSFORM_DCT4,      JXL_TRANSFORM_DCT16,     JXL_TRANSFORM_DCT32,
    JXL_TRANSFORM_DCT8X16,   JXL_TRANSFORM_DCT8X32,   JXL_TRANSFORM_DCT16X32,
    JXL_TRANSFORM_DCT4X8,    JXL_TRANSFORM_AFV0,      JXL_TRANSFORM_DCT64,
    JXL_TRANSFORM_DCT32X64,  JXL_TRANSFORM_DCT128,    JXL_TRANSFORM_DCT64X128,
    JXL_TRANSFORM_DCT256,    JXL_TRANSFORM_DCT128X256,
};

static void test_modular_stream_index(void) {
    jxl_frame_header h;
    jxl_frame_header_init(&h);
    h.width = 256;
    h.height = 256;
    h.group_size_shift = 1;
    uint32_t idx = jxl_frame_header_pass_group_modular_stream_index(&h, 0, 0);
    assert(idx == 1u + 3u * jxl_frame_header_num_lf_groups(&h) + 17u);
    jxl_frame_header_free(test_alloc_state(), &h);
}

static void test_lf_group_idx(void) {
    jxl_frame_header h;
    jxl_frame_header_init(&h);
    h.width = 2048;
    h.height = 2048;
    h.group_size_shift = 1;
    assert(jxl_frame_header_lf_group_idx_from_group_idx(&h, 0) == 0);
    assert(jxl_frame_header_lf_group_idx_from_group_idx(&h, 64) == 1);
    jxl_frame_header_free(test_alloc_state(), &h);
}

static void test_dequant_and_transform(void) {
    size_t i;
    jxl_dequant_matrix_set set;
    jxl_frame_header frame;
    float lf_buf[1] = {2.0f};
    float coeff_buf[64];
    float before;
    jxl_quantizer quant;
    jxl_opsin_inverse_matrix opsin;
    jxl_hf_global_dequant hf_global;
    jxl_lf_group_view lf_group;
    jxl_render_vardct_group_params params = {0};
    jxl_context *library_ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &library_ctx), JXL_OK);

    jxl_dequant_matrix_set_init(&set);
    set.ctx = library_ctx;
    for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
        JXL_TEST_ASSERT_EQ(jxl_dequant_matrix_params_default(test_alloc_state(), k_dct_select_list[i],
                                                             &set.matrices[i]),
                           JXL_VARDCT_OK);
    }
    JXL_TEST_ASSERT_EQ(jxl_dequant_matrix_set_build_weights(library_ctx, &set), JXL_VARDCT_OK);

    quant.global_scale = 100;
    quant.quant_lf = 0;

    opsin.quant_bias_numerator = 0.145f;

    hf_global.ctx = library_ctx;
    hf_global.dequant_matrices = &set;
    hf_global.quantizer = &quant;
    hf_global.opsin_inverse = &opsin;


    jxl_frame_header_init(&frame);
    frame.width = 256;
    frame.height = 256;
    frame.group_size_shift = 1;

    jxl_block_info blocks[1] = {{JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1}};
    lf_group.block_info_data = blocks;
    lf_group.block_info_width = 1;
    lf_group.block_info_height = 1;
    lf_group.block_info_stride = 1;


    memset(coeff_buf, 0, sizeof(coeff_buf));
    coeff_buf[0] = 50.0f;

    params.ctx = library_ctx;
    params.frame_header = &frame;
    params.lf_group = &lf_group;
    params.group_idx = 0;
    params.hf_global = &hf_global;
    params.lf[0] = jxl_const_subgrid_f32_from_buf(lf_buf, 1, 1, 1);
    params.lf[1] = jxl_const_subgrid_f32_from_buf(lf_buf, 1, 1, 1);
    params.lf[2] = jxl_const_subgrid_f32_from_buf(lf_buf, 1, 1, 1);
    params.coeff[0] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);
    params.coeff[1] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);
    params.coeff[2] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);


    before = coeff_buf[0];
    jxl_render_vardct_dequant_and_transform(&params);
    assert(fabsf(coeff_buf[0] - before) > 0.01f);

    jxl_dequant_matrix_set_free(&set);
    jxl_frame_header_free(test_alloc_state(), &frame);
    jxl_context_destroy(library_ctx);
}

int main(void) {
    test_modular_stream_index();
    test_lf_group_idx();
    test_dequant_and_transform();
    return 0;
}
