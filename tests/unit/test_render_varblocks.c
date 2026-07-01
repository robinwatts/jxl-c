// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/subgrid_f32.h"
#include "render/vardct/varblocks.h"


static jxl_allocator_state *test_alloc_state(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}
#include "allocator.h"
#include <assert.h>
#include <string.h>

typedef struct {
    size_t count;
} count_ctx;

static void count_varblock(const jxl_varblock_info *info, void *ctx_void) {
    (void)info;
    count_ctx *ctx = (count_ctx *)ctx_void;
    ++ctx->count;
}

static void test_for_each_no_shift(void) {
    jxl_block_info_subgrid bi;
    jxl_block_info blocks[4] = {
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
    };
    count_ctx ctx = {0};

    bi.data = blocks;
    bi.width = 2;
    bi.height = 2;
    bi.stride = 2;
    jxl_for_each_varblocks(bi, jxl_channel_shift_from_shift(0), count_varblock, &ctx);
    assert(ctx.count == 4);
}

static void test_for_each_hshift(void) {
    jxl_block_info_subgrid bi;
    jxl_block_info blocks[4] = {
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_OCCUPIED, JXL_TRANSFORM_DCT8, 0},
        {JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1},
        {JXL_BLOCK_INFO_OCCUPIED, JXL_TRANSFORM_DCT8, 0},
    };
    count_ctx ctx = {0};

    bi.data = blocks;
    bi.width = 2;
    bi.height = 2;
    bi.stride = 2;
    jxl_for_each_varblocks(bi, jxl_channel_shift_raw(1, 0), count_varblock, &ctx);
    assert(ctx.count == 2);
}

static void test_transform_dct8_produces_samples(void) {
    size_t i;
    jxl_block_info blocks[1] = {{JXL_BLOCK_INFO_DATA, JXL_TRANSFORM_DCT8, 1}};
    float lf_buf[1] = {3.5f};
    float coeff_buf[64];
    jxl_block_info_subgrid bi;
    jxl_const_subgrid_f32 lf[3];
    jxl_subgrid_f32 coeff[3];
    jxl_channel_shift shifts[3];

    float energy;
    bi.data = blocks;
    bi.width = 1;
    bi.height = 1;
    bi.stride = 1;
    lf[0] = jxl_const_subgrid_f32_from_buf(lf_buf, 1, 1, 1);
    lf[1] = jxl_const_subgrid_f32_from_buf(lf_buf, 1, 1, 1);
    lf[2] = jxl_const_subgrid_f32_from_buf(lf_buf, 1, 1, 1);
    coeff[0] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);
    coeff[1] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);
    coeff[2] = jxl_subgrid_f32_from_buf(coeff_buf, 8, 8, 8);
    shifts[0] = jxl_channel_shift_from_shift(0);
    shifts[1] = jxl_channel_shift_from_shift(0);
    shifts[2] = jxl_channel_shift_from_shift(0);

    memset(coeff_buf, 0, sizeof(coeff_buf));

    jxl_render_transform_varblocks(NULL, test_alloc_state(), lf, coeff, shifts, bi);

    energy = 0.0f;
    for (i = 0; i < 64; ++i) {
        float v = coeff_buf[i];
        energy += v < 0.0f ? -v : v;
    }
    assert(energy > 0.01f);
}

int main(void) {
    test_for_each_no_shift();
    test_for_each_hshift();
    test_transform_dct8_produces_samples();
    return 0;
}
