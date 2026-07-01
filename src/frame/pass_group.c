// SPDX-License-Identifier: MIT OR Apache-2.0
#include "pass_group.h"

#include "modular/channel_decode.h"
#include "modular/group_subimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const void *lf_quant_subgrid_at(const jxl_lf_quant_subgrid_u32 *src, size_t x, size_t y) {
    size_t st = src->stride != 0 ? src->stride : src->width;
    size_t idx = y * st + x;
    if (src->kind == JXL_MODULAR_SAMPLE_I16) {
        return (const int16_t *)src->data + idx;
    }
    return (const int32_t *)src->data + idx;
}

static uint32_t div_ceil_u32(uint32_t a, uint32_t b) {
    return b == 0 ? a : (a + b - 1) / b;
}

static jxl_frame_status_t vardct_to_frame(jxl_vardct_status_t st) {
    switch (st) {
    case JXL_VARDCT_OK:
        return JXL_FRAME_OK;
    case JXL_VARDCT_OUT_OF_MEMORY:
        return JXL_FRAME_OUT_OF_MEMORY;
    case JXL_VARDCT_BITSTREAM_ERROR:
        return JXL_FRAME_BITSTREAM_ERROR;
    default:
        return JXL_FRAME_DECODER_ERROR;
    }
}

int jxl_pass_group_block_info_subgrid(const jxl_frame_header *frame_header, uint32_t group_idx,
                                    const jxl_lf_group_view *lf_group,
                                    const jxl_modular_region *lf_region,
                                    jxl_block_info_subgrid *out) {
    uint32_t group_w;
    uint32_t group_h;
    jxl_block_info_subgrid compound_tmp;
    uint32_t group_x;
    uint32_t group_y;
    if (frame_header == NULL || lf_group == NULL || out == NULL || lf_group->block_info_data == NULL) {
        return 0;
    }

    if (lf_region == NULL) {
        jxl_pass_group_block_slice slice;
        jxl_block_info_subgrid compound_tmp;
        if (!jxl_pass_group_block_slice_for_group(frame_header, group_idx, lf_group->block_info_width,
                                                  lf_group->block_info_height, &slice)) {
            return 0;
        }
        compound_tmp.data = lf_group->block_info_data + slice.block_top * lf_group->block_info_stride +
                    slice.block_left;
        compound_tmp.width = slice.block_width;
        compound_tmp.height = slice.block_height;
        compound_tmp.stride = lf_group->block_info_stride;
        *out = compound_tmp;

        return 1;
    }

    uint32_t group_dim;
    uint32_t groups_per_row;
    size_t left_in_lf;
    size_t top_in_lf;
    uint32_t lf_w;
    uint32_t lf_h;
    int32_t lf_base_left;
    int32_t lf_base_top;
    uint32_t region_lf_w;
    uint32_t region_lf_h;
    group_dim = jxl_frame_header_group_dim(frame_header);
    groups_per_row = jxl_frame_header_groups_per_row(frame_header);
    if (groups_per_row == 0 || group_dim == 0) {
        return 0;
    }
    group_x = group_idx % groups_per_row;
    group_y = group_idx / groups_per_row;
    left_in_lf = (size_t)((group_x % 8u) * (group_dim / 8u));
    top_in_lf = (size_t)((group_y % 8u) * (group_dim / 8u));

    group_w = 0;
    group_h = 0;
    jxl_frame_header_group_size_for(frame_header, group_idx, &group_w, &group_h);
    lf_w = div_ceil_u32(group_w, 8u);
    lf_h = div_ceil_u32(group_h, 8u);
    lf_base_left = (int32_t)(group_x * group_dim / 8u) - lf_region->left;
    lf_base_top = (int32_t)(group_y * group_dim / 8u) - lf_region->top;
    if (lf_base_left < 0 || lf_base_top < 0) {
        return 0;
    }
    region_lf_w = lf_region->width - (uint32_t)lf_base_left;
    region_lf_h = lf_region->height - (uint32_t)lf_base_top;
    if (lf_w > region_lf_w) {
        lf_w = region_lf_w;
    }
    if (lf_h > region_lf_h) {
        lf_h = region_lf_h;
    }
    if (lf_w == 0u || lf_h == 0u) {
        return 0;
    }
    if (left_in_lf + lf_w > lf_group->block_info_width ||
        top_in_lf + lf_h > lf_group->block_info_height) {
        return 0;
    }

    compound_tmp.data = lf_group->block_info_data + top_in_lf * lf_group->block_info_stride + left_in_lf;
    compound_tmp.width = lf_w;
    compound_tmp.height = lf_h;
    compound_tmp.stride = lf_group->block_info_stride;
    *out = compound_tmp;

    return 1;
}

int jxl_pass_group_block_slice_for_group(const jxl_frame_header *frame_header, uint32_t group_idx,
                                         size_t block_info_width, size_t block_info_height,
                                         jxl_pass_group_block_slice *out) {
    uint32_t group_col;
    uint32_t group_row;
    uint32_t groups_per_row;
    size_t lf_col;
    size_t lf_row;
    size_t group_dim_blocks;
    size_t rem_w;
    size_t rem_h;
    if (frame_header == NULL || out == NULL) {
        return 0;
    }

    groups_per_row = jxl_frame_header_groups_per_row(frame_header);
    if (groups_per_row == 0) {
        return 0;
    }

    group_col = group_idx % groups_per_row;
    group_row = group_idx / groups_per_row;
    lf_col = (size_t)(group_col % 8);
    lf_row = (size_t)(group_row % 8);
    group_dim_blocks = (size_t)(jxl_frame_header_group_dim(frame_header) / 8);

    out->block_left = lf_col * group_dim_blocks;
    out->block_top = lf_row * group_dim_blocks;

    rem_w = block_info_width > out->block_left ? block_info_width - out->block_left : 0;
    rem_h = block_info_height > out->block_top ? block_info_height - out->block_top : 0;
    out->block_width = rem_w < group_dim_blocks ? rem_w : group_dim_blocks;
    out->block_height = rem_h < group_dim_blocks ? rem_h : group_dim_blocks;
    return 1;
}

jxl_frame_status_t jxl_decode_pass_group_vardct(jxl_bs *bs,
                                                const jxl_pass_group_vardct_params *params) {
    jxl_pass_group_block_slice slice;
    jxl_lf_quant_subgrid_u32 lf_quant_slices[3];
    uint32_t coeff_shift;
    jxl_block_info_subgrid block_info;
    jxl_hf_coeff_params hp = {0};
    jxl_subgrid_i32 coeff_out[3];
    const jxl_lf_group_view *lf;
    const jxl_hf_global_view *hf;
    const jxl_lf_quant_subgrid_u32 *lf_quant_ptr = NULL;
    const jxl_frame_passes *passes;
    jxl_vardct_status_t vst;
    if (bs == NULL || params == NULL || params->frame_header == NULL || params->lf_group == NULL ||
        params->hf_global == NULL || params->hf_coeff_out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    lf = params->lf_group;
    if (lf->block_info_data == NULL) {
        return JXL_FRAME_OK;
    }

    hf = params->hf_global;
    if (params->pass_idx >= hf->hf_pass_count || hf->hf_passes == NULL || hf->hf_block_ctx == NULL) {
        return JXL_FRAME_DECODER_ERROR;
    }

    if (!jxl_pass_group_block_slice_for_group(params->frame_header, params->group_idx,
                                              lf->block_info_width, lf->block_info_height,
                                              &slice)) {
        return JXL_FRAME_DECODER_ERROR;
    }

    block_info.data = lf->block_info_data + slice.block_top * lf->block_info_stride + slice.block_left;
    block_info.width = slice.block_width;
    block_info.height = slice.block_height;
    block_info.stride = lf->block_info_stride;


    if (lf->lf_quant != NULL) {
        size_t idx;
        static const size_t k_ch_map[3] = {1, 0, 2};
        const jxl_frame_header *fh = params->frame_header;
        for (idx = 0; idx < 3; ++idx) {
            size_t c = k_ch_map[idx];
            jxl_lf_quant_subgrid_u32 compound_tmp;
            jxl_channel_shift shift =
                jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, idx);
            int32_t hshift = jxl_channel_shift_hshift(&shift);
            int32_t vshift = jxl_channel_shift_vshift(&shift);

            size_t bl = slice.block_left >> (size_t)hshift;
            size_t bt = slice.block_top >> (size_t)vshift;
            uint32_t bw = (uint32_t)slice.block_width;
            uint32_t bh = (uint32_t)slice.block_height;
            const jxl_lf_quant_subgrid_u32 *src;
            jxl_channel_shift_shift_size(&shift, bw, bh, &bw, &bh);

            src = &lf->lf_quant[c];
            compound_tmp.data = lf_quant_subgrid_at(src, bl, bt);
            compound_tmp.kind = src->kind;
            compound_tmp.width = (size_t)bw;
            compound_tmp.height = (size_t)bh;
            compound_tmp.stride = src->stride;
            lf_quant_slices[idx] = compound_tmp;

        }
        lf_quant_ptr = lf_quant_slices;
    }

    coeff_shift = 0;
    passes = &params->frame_header->passes;
    if (params->pass_idx < passes->shift_len && passes->shift != NULL) {
        coeff_shift = passes->shift[params->pass_idx];
    }

    {
        if (JXL_DEBUG_FLAG(params->ctx, debug_hf_coeff)) {
            size_t by;
            size_t data = 0;
            size_t occ = 0;
            size_t uninit = 0;
            for (by = 0; by < block_info.height; ++by) {
                size_t bx;
                for (bx = 0; bx < block_info.width; ++bx) {
                    const jxl_block_info *bi = &block_info.data[by * block_info.stride + bx];
                    if (bi->kind == JXL_BLOCK_INFO_DATA) {
                        ++data;
                    } else if (bi->kind == JXL_BLOCK_INFO_OCCUPIED) {
                        ++occ;
                    } else {
                        ++uninit;
                    }
                }
            }
            fprintf(stderr,
                    "pg vardct pass=%u group=%u slice=%zux%zu data=%zu occ=%zu uninit=%zu "
                    "jpeg={%u,%u,%u}\n",
                    params->pass_idx, params->group_idx, block_info.width, block_info.height, data,
                    occ, uninit, params->frame_header->jpeg_upsampling[0],
                    params->frame_header->jpeg_upsampling[1],
                    params->frame_header->jpeg_upsampling[2]);
        }
    }

    hp.ctx = params->ctx;
    hp.num_hf_presets = hf->num_hf_presets;
    hp.hf_block_ctx = hf->hf_block_ctx;
    hp.block_info = block_info;
    hp.jpeg_upsampling[0] = params->frame_header->jpeg_upsampling[0];
    hp.jpeg_upsampling[1] = params->frame_header->jpeg_upsampling[1];
    hp.jpeg_upsampling[2] = params->frame_header->jpeg_upsampling[2];
    hp.lf_quant = lf_quant_ptr;
    hp.hf_pass = &hf->hf_passes[params->pass_idx];
    hp.coeff_shift = coeff_shift;


    coeff_out[0] = params->hf_coeff_out[0];
    coeff_out[1] = params->hf_coeff_out[1];
    coeff_out[2] = params->hf_coeff_out[2];

    vst = jxl_write_hf_coeff(bs, &hp, coeff_out);
    (void)params->allow_partial;
    return vardct_to_frame(vst);
}

static jxl_frame_status_t modular_to_frame(jxl_modular_status_t st) {
    switch (st) {
    case JXL_MODULAR_OK:
        return JXL_FRAME_OK;
    case JXL_MODULAR_OUT_OF_MEMORY:
        return JXL_FRAME_OUT_OF_MEMORY;
    case JXL_MODULAR_BITSTREAM_ERROR:
        return JXL_FRAME_BITSTREAM_ERROR;
    default:
        return JXL_FRAME_DECODER_ERROR;
    }
}

jxl_frame_status_t jxl_decode_pass_group_modular_coefficients(
    jxl_bs *bs, const jxl_pass_group_modular_params *params) {
    int complete;
    jxl_modular_status_t pst;
    jxl_modular_global_groups *groups;
    jxl_modular_transformed_subimage *sub;
    uint32_t stream_index;
    jxl_modular_status_t st;
    if (params == NULL || params->frame_header == NULL || params->alloc == NULL ||
        params->modular_params == NULL || params->modular_dest == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (params->modular_dest->image_channels_len == 0) {
        return JXL_FRAME_OK;
    }

    pst = jxl_modular_ensure_group_layout(params->alloc, params->modular_dest, params->frame_header);
    if (pst != JXL_MODULAR_OK) {
        return modular_to_frame(pst);
    }

    groups = jxl_modular_dest_group_layout(params->modular_dest);
    sub = jxl_modular_global_pass_group(groups, params->pass_idx, params->group_idx);
    if (jxl_modular_transformed_subimage_is_empty(sub)) {
        return JXL_FRAME_OK;
    }

    stream_index = jxl_frame_header_pass_group_modular_stream_index(
        params->frame_header, params->pass_idx, params->group_idx);

    jxl_modular_debug_tokens_set_pg(params->ctx, params->pass_idx, params->group_idx);

    complete = 0;
    st = jxl_modular_subimage_recursive_decode(
        params->ctx, params->alloc, bs, sub, params->modular_dest, params->modular_params,
        params->global_ma, stream_index, params->allow_partial, &complete);
    if (st == JXL_MODULAR_OK && !complete) {
        sub->partial = 1;
    }
    return modular_to_frame(st);
}
