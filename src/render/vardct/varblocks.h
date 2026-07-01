// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_VARBLOCKS_H_
#define JXL_RENDER_VARDCT_VARBLOCKS_H_

#include "allocator.h"
#include "render/subgrid_f32.h"
#include "vardct/dct_select.h"
#include "vardct/hf_metadata.h"
#include "modular/param.h"

#include <stddef.h>

typedef struct {
    size_t shifted_bx;
    size_t shifted_by;
    jxl_transform_type dct_select;
    int32_t hf_mul;
} jxl_varblock_info;

typedef void (*jxl_varblock_fn)(const jxl_varblock_info *info, void *ctx);

void jxl_for_each_varblocks(jxl_block_info_subgrid block_info, jxl_channel_shift shift,
                          jxl_varblock_fn fn, void *ctx);

void jxl_render_transform_varblocks(jxl_context *ctx, jxl_allocator_state *alloc,
                                    const jxl_const_subgrid_f32 lf[3],
                                    jxl_subgrid_f32 coeff_out[3],
                                    const jxl_channel_shift shifts_cbycr[3],
                                    jxl_block_info_subgrid block_info);

#endif /* JXL_RENDER_VARDCT_VARBLOCKS_H_ */
