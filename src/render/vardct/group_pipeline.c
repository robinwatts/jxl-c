// SPDX-License-Identifier: MIT OR Apache-2.0
#include "group_pipeline.h"

#include "context.h"

#include <string.h>

void jxl_render_transform_with_lf_grouped(jxl_render_vardct_group_params *params) {
    size_t ch;
    jxl_block_info_subgrid block_info;
    jxl_channel_shift shifts[3];
    if (params == NULL || params->frame_header == NULL || params->lf_group == NULL ||
        params->lf == NULL || params->coeff == NULL) {
        return;
    }

    if (!jxl_pass_group_block_info_subgrid(params->frame_header, params->group_idx,
                                         params->lf_group, params->lf_xyb_region, &block_info)) {
                                             size_t ch;
        for (ch = 0; ch < 3; ++ch) {
            size_t y;
            jxl_subgrid_f32 coeff = params->coeff[ch];
            jxl_const_subgrid_f32 lf = params->lf[ch];
            for (y = 0; y < coeff.height; ++y) {
                size_t x;
                float *row = jxl_subgrid_f32_row_mut(coeff, y);
                for (x = 0; x < coeff.width; ++x) {
                    row[x] = jxl_const_subgrid_f32_get(lf, x / 8, y / 8);
                }
            }
        }
        return;
    }

    for (ch = 0; ch < 3; ++ch) {
        shifts[ch] = jxl_channel_shift_from_jpeg_upsampling(params->frame_header->jpeg_upsampling,
                                                            ch);
    }
    jxl_render_transform_varblocks(params->ctx, jxl_context_alloc_state(params->ctx), params->lf,
                                   params->coeff, shifts, block_info);
}

void jxl_render_vardct_dequant_and_transform(jxl_render_vardct_group_params *params) {
    if (params == NULL || params->hf_global == NULL) {
        return;
    }
    jxl_dequant_hf_varblock_grouped(params->ctx, params->coeff, params->group_idx,
                                    params->frame_header, params->hf_global, params->lf_group,
                                    params->lf_xyb_region);
    jxl_render_transform_with_lf_grouped(params);
}
