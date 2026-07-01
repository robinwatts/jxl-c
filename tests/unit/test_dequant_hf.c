// SPDX-License-Identifier: MIT OR Apache-2.0
#include "context.h"
#include "frame/frame_header.h"
#include "render/vardct/dequant_hf.h"
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

static void test_build_default_dct8_weights(void) {
    size_t i;
    jxl_dequant_matrix_set set;
    size_t len;
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

    len = 0;
    const float *w = jxl_dequant_matrix_weights(library_ctx, &set, 0, 0, &len);
    assert(w != NULL && len == 64);
    assert(w[0] > 0.0f);

    jxl_dequant_matrix_set_free(&set);
    jxl_context_destroy(library_ctx);
}

static void test_dequant_single_dct8_coeff(void) {
    size_t i;
    jxl_dequant_matrix_set set;
    jxl_frame_header frame;
    float coeff_buf[64 * 3];
    jxl_quantizer quant;
    jxl_opsin_inverse_matrix opsin;
    jxl_hf_global_dequant hf_global;
    jxl_lf_group_view lf;
    jxl_subgrid_f32 out[3];
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

    opsin.quant_bias[0] = 0.0f;
    opsin.quant_bias[1] = 0.0f;
    opsin.quant_bias[2] = 0.0f;
    opsin.quant_bias_numerator = 0.145f;

    hf_global.ctx = library_ctx;
    hf_global.dequant_matrices = &set;
    hf_global.quantizer = &quant;
    hf_global.opsin_inverse = &opsin;


    jxl_frame_header_init(&frame);
    frame.width = 256;
    frame.height = 256;
    frame.group_size_shift = 1;
    frame.jpeg_upsampling[0] = 1;
    frame.jpeg_upsampling[1] = 1;
    frame.jpeg_upsampling[2] = 1;
    frame.x_qm_scale = 2;
    frame.b_qm_scale = 2;

    jxl_block_info blocks[1] = {{JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1}};
    lf.block_info_data = blocks;
    lf.block_info_width = 1;
    lf.block_info_height = 1;
    lf.block_info_stride = 1;


    memset(coeff_buf, 0, sizeof(coeff_buf));
    coeff_buf[0] = 100.0f;

    out[0] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);
    out[1] = jxl_subgrid_f32_from_buf(coeff_buf + 64, 8, 8, 8);
    out[2] = jxl_subgrid_f32_from_buf(coeff_buf + 128, 8, 8, 8);


    jxl_dequant_hf_varblock_grouped(library_ctx, out, 0, &frame, &hf_global, &lf, NULL);
    assert(fabsf(coeff_buf[0]) > 1.0f);

    jxl_dequant_matrix_set_free(&set);
    jxl_frame_header_free(test_alloc_state(), &frame);
    jxl_context_destroy(library_ctx);
}

int main(void) {
    test_build_default_dct8_weights();
    test_dequant_single_dct8_coeff();
    return 0;
}
