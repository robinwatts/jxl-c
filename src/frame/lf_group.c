// SPDX-License-Identifier: MIT OR Apache-2.0
#include "lf_group.h"

#include "image/image_internal.h"
#include "modular/group_subimage.h"
#include "modular/image.h"
#include "modular/param.h"
#include "modular/prepare_subimage.h"
#include "modular/subimage_decode.h"
#include "modular/transform/inverse.h"
#include "vardct/dct_select.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t div_ceil_u32(uint32_t a, uint32_t b) {
    return b == 0 ? a : (a + b - 1) / b;
}

static uint32_t trailing_zeros_u32(uint32_t v) {
    uint32_t n;
    if (v == 0) {
        return 0;
    }
    n = 0;
    while ((v & 1u) == 0) {
        n += 1;
        v >>= 1;
    }
    return n;
}

static uint32_t next_power_of_two_u32(uint32_t v) {
    if (v <= 1) {
        return 1;
    }
    v -= 1;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
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

static int modular_params_set_custom(jxl_allocator_state *alloc, jxl_modular_params *p, uint32_t group_dim, uint32_t bit_depth,
                                     const jxl_modular_channel_params *ch, size_t n) {
    jxl_modular_params_free(alloc, p);
    p->group_dim = group_dim;
    p->bit_depth = bit_depth;
    p->num_channels = n;
    if (n == 0) {
        return 1;
    }
    p->channels = jxl_calloc(alloc, n, sizeof(*p->channels));
    if (p->channels == NULL) {
        return 0;
    }
    memcpy(p->channels, ch, n * sizeof(*ch));
    return 1;
}

static int32_t grid_get(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    return jxl_modular_grid_sample_as_i32(g, x, y);
}

static jxl_modular_status_t modular_parse_decode_finish(
    jxl_allocator_state *alloc, jxl_bs *bs, const jxl_modular_parse_ctx *ctx, uint32_t stream_index,
    uint32_t region_w, uint32_t region_h, jxl_modular_image_destination *dest, int allow_partial) {
    jxl_modular_status_t st = jxl_modular_dest_apply_local_header(alloc, bs, ctx, dest);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    st = jxl_modular_image_prepare_subimage_grids(alloc, dest);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    st = jxl_modular_subimage_decode(ctx->ctx, alloc, bs, dest, stream_index, allow_partial);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    return jxl_modular_image_apply_inverse_transforms(ctx->ctx, alloc, dest, region_w, region_h,
                                                    ctx->params->bit_depth, ctx->params);
}

static void lf_group_sync_lf_quant_views(jxl_lf_group *g) {
    size_t c;
    if (g == NULL || !g->has_lf_coeff || g->lf_quant.image_channels_len < 3) {
        return;
    }
    for (c = 0; c < 3; ++c) {
        jxl_lf_quant_subgrid_u32 compound_tmp;
        const jxl_modular_grid_i32 *grid = &g->lf_quant.image_channels[c];
        const void *data;
        size_t st;
        if (grid->buf == NULL) {
            continue;
        }
        if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
            data = (const int16_t *)grid->buf + grid->offset;
        } else {
            data = (const int32_t *)grid->buf + grid->offset;
        }
        st = jxl_modular_grid_row_stride(grid);
        compound_tmp.data = data;
        compound_tmp.kind = grid->kind;
        compound_tmp.width = grid->width;
        compound_tmp.height = grid->height;
        compound_tmp.stride = st;
        g->lf_quant_view[c] = compound_tmp;

    }
}

static jxl_frame_status_t lf_coeff_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                         const jxl_lf_group_params *params, uint32_t lf_width,
                                         uint32_t lf_height, jxl_lf_group *out) {
                                             size_t i;
    uint32_t extra = 0;
    jxl_modular_channel_params ch[3];
    jxl_modular_params mod_params;
    jxl_modular_parse_ctx ctx = {0};
    uint32_t width;
    uint32_t height;
    static const size_t k_shift_idx[3] = {1, 0, 2};
    uint32_t stream_index;
    jxl_modular_status_t mst;
    if (jxl_bs_read_bits(bs, 2, &extra) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    out->extra_precision = (uint8_t)extra;

    width = div_ceil_u32(lf_width, 8);
    height = div_ceil_u32(lf_height, 8);
    for (i = 0; i < 3; ++i) {
        ch[i].width = width;
        ch[i].height = height;
        ch[i].shift =
            jxl_channel_shift_from_jpeg_upsampling(params->frame->jpeg_upsampling, k_shift_idx[i]);
    }

    jxl_modular_params_init(&mod_params);
    if (!modular_params_set_custom(alloc, &mod_params, 0, params->image->bit_depth_bits, ch, 3)) {
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    mod_params.narrow_buffer = jxl_parsed_narrow_modular(params->ctx, params->image);

    ctx.params = &mod_params;
    ctx.global_ma = params->global_ma;
    ctx.tracker = params->tracker;
    ctx.ctx = params->ctx;


    stream_index = 1u + params->lf_group_idx;
    mst = modular_parse_decode_finish(alloc, bs, &ctx, stream_index, lf_width, lf_height,
                                      &out->lf_quant, params->allow_partial);
    jxl_modular_params_free(alloc, &mod_params);
    if (mst != JXL_MODULAR_OK) {
        return modular_to_frame(mst);
    }

    out->has_lf_coeff = 1;
    lf_group_sync_lf_quant_views(out);
    return JXL_FRAME_OK;
}

static jxl_frame_status_t mlf_group_decode(jxl_allocator_state *alloc, jxl_bs *bs,
                                           const jxl_lf_group_params *params, int *mlf_complete);

jxl_frame_status_t jxl_decode_lf_group_modular_coefficients(jxl_allocator_state *alloc,
                                                            jxl_bs *bs,
                                                            const jxl_lf_group_params *params) {
    int complete;
    jxl_frame_status_t fst;
    if (alloc == NULL || params == NULL || params->frame == NULL || params->gmodular == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    complete = 0;
    fst = mlf_group_decode(alloc, bs, params, &complete);
    (void)complete;
    return fst;
}

static jxl_frame_status_t mlf_group_decode(jxl_allocator_state *alloc, jxl_bs *bs,
                                           const jxl_lf_group_params *params, int *mlf_complete) {
    jxl_modular_params mod_params;
    int complete;
    jxl_modular_image_destination *gmodular;
    jxl_modular_status_t pst;
    jxl_modular_global_groups *groups;
    jxl_modular_transformed_subimage *sub;
    uint32_t stream_index;
    jxl_modular_status_t st;
    if (mlf_complete != NULL) {
        *mlf_complete = 1;
    }
    if (params->gmodular == NULL || params->gmodular->group_dim == 0) {
        return JXL_FRAME_OK;
    }

    gmodular = params->gmodular;
    pst = jxl_modular_ensure_group_layout(alloc, gmodular, params->frame);
    if (pst != JXL_MODULAR_OK) {
        return modular_to_frame(pst);
    }

    groups = jxl_modular_dest_group_layout(gmodular);
    sub = jxl_modular_global_lf_group(groups, params->lf_group_idx);
    if (jxl_modular_transformed_subimage_is_empty(sub)) {
        return JXL_FRAME_OK;
    }

    jxl_modular_params_init(&mod_params);
    mod_params.group_dim = gmodular->group_dim;
    mod_params.bit_depth = gmodular->bit_depth;

    stream_index =
        params->modular_from_pass_group
            ? jxl_frame_header_pass_group_modular_stream_index(params->frame, 0u,
                                                               params->lf_group_idx)
            : (1u + jxl_frame_header_num_lf_groups(params->frame) + params->lf_group_idx);

    complete = 0;
    st = jxl_modular_subimage_recursive_decode(
        params->ctx, alloc, bs, sub, gmodular, &mod_params, params->global_ma, stream_index,
        params->allow_partial, &complete);
    if (mlf_complete != NULL) {
        *mlf_complete = complete;
    }
    if (st == JXL_MODULAR_OK && !complete) {
        sub->partial = 1;
    }

    jxl_modular_params_free(alloc, &mod_params);
    return modular_to_frame(st);
}

static int block_info_is_occupied(const jxl_block_info *info) {
    return info != NULL &&
           (info->kind == JXL_BLOCK_INFO_OCCUPIED || info->kind == JXL_BLOCK_INFO_DATA);
}

static int copy_modular_grid_i32(jxl_allocator_state *alloc, const jxl_modular_grid_i32 *src,
                                 int32_t **out_data, size_t *width_out, size_t *height_out,
                                 size_t *stride_out) {
    size_t y;
    size_t st;
    size_t count;
    int32_t *buf;
    if (alloc == NULL || src == NULL || src->buf == NULL || out_data == NULL || width_out == NULL ||
        height_out == NULL || stride_out == NULL || src->width == 0 || src->height == 0) {
        return 0;
    }
    st = jxl_modular_grid_row_stride(src);
    (void)st;
    count = src->width * src->height;
    buf = jxl_alloc(alloc, count * sizeof(int32_t));
    if (buf == NULL) {
        return 0;
    }
    for (y = 0; y < src->height; ++y) {
        size_t x;
        for (x = 0; x < src->width; ++x) {
            buf[y * src->width + x] = jxl_modular_grid_sample_as_i32(src, x, y);
        }
    }
    *out_data = buf;
    *width_out = src->width;
    *height_out = src->height;
    *stride_out = src->width;
    return 1;
}

static jxl_frame_status_t hf_metadata_expand(jxl_allocator_state *alloc, uint32_t lf_group_idx,
                                             size_t bw, size_t bh,
                                             const jxl_modular_grid_i32 *block_info_raw,
                                             const jxl_modular_grid_i32 *sharpness,
                                             jxl_block_info *block_info, size_t stride,
                                             const jxl_lf_group_params *params, float *epf_sigma,
                                             size_t epf_stride) {
                                                 size_t y;
    float base_sigma;
    size_t data_idx;
    const jxl_restoration_filter *restoration =
        params != NULL && params->frame != NULL ? &params->frame->restoration : NULL;
    int epf_on = restoration != NULL && jxl_epf_enabled(restoration);
    (void)alloc;
    (void)lf_group_idx;

    base_sigma = 0.0f;
    if (epf_on && params != NULL && params->quantizer != NULL) {
        base_sigma = restoration->epf.sigma.quant_mul * 65536.0f /
                     (float)params->quantizer->global_scale;
    }
    data_idx = 0;
    for (y = 0; y < bh; ++y) {
        size_t x = 0;
        while (x < bw) {
            if (!block_info_is_occupied(&block_info[y * stride + x])) {
                uint32_t dy;
                jxl_transform_type dct_select;
                int32_t hf_mul;
                uint32_t dw;
                uint32_t dh;
                int32_t dct_raw;
                int32_t mul_raw;
                uint32_t x_in_group;
                uint32_t y_in_group;
                if (data_idx >= block_info_raw->width) {
                    return JXL_FRAME_VALIDATION_ERROR;
                }
                dct_raw = grid_get(block_info_raw, data_idx, 0);
                mul_raw = grid_get(block_info_raw, data_idx, 1);
                if (!jxl_transform_type_from_u8((uint8_t)dct_raw, &dct_select)) {
                    return JXL_FRAME_VALIDATION_ERROR;
                }
                hf_mul = mul_raw + 1;
                if (hf_mul <= 0) {
                    return JXL_FRAME_VALIDATION_ERROR;
                }

                dw = 0;
                dh = 0;
                jxl_transform_dct_select_size(dct_select, &dw, &dh);
                x_in_group = (uint32_t)(x % 32);
                y_in_group = (uint32_t)(y % 32);
                if (x_in_group + dw > 32 || y_in_group + dh > 32) {
                    return JXL_FRAME_VALIDATION_ERROR;
                }

                for (dy = 0; dy < dh; ++dy) {
                    uint32_t dx;
                    for (dx = 0; dx < dw; ++dx) {
                        size_t px = x + (size_t)dx;
                        size_t py = y + (size_t)dy;
                        jxl_block_info *slot;
                        if (px >= bw || py >= bh) {
                            return JXL_FRAME_VALIDATION_ERROR;
                        }
                        slot = &block_info[py * stride + px];
                        if (block_info_is_occupied(slot)) {
                            return JXL_FRAME_VALIDATION_ERROR;
                        }
                        if (dx == 0 && dy == 0) {
                            slot->kind = JXL_BLOCK_INFO_DATA;
                            slot->dct_select = dct_select;
                            slot->hf_mul = hf_mul;
                        } else {
                            slot->kind = JXL_BLOCK_INFO_OCCUPIED;
                            slot->dct_select = JXL_TRANSFORM_DCT8;
                            slot->hf_mul = 0;
                        }
                        if (sharpness != NULL) {
                            int32_t sharp = grid_get(sharpness, px, py);
                            if (sharp < 0 || sharp > 7) {
                                return JXL_FRAME_VALIDATION_ERROR;
                            }
                            if (epf_on && epf_sigma != NULL) {
                                float sigma = (base_sigma / (float)hf_mul) *
                                              restoration->epf.sharp_lut[(size_t)sharp];
                                epf_sigma[py * epf_stride + px] = sigma;
                            }
                        }
                    }
                }
                data_idx += 1;
                x += (size_t)dw;
            } else {
                x += 1;
            }
        }
    }
    return JXL_FRAME_OK;
}

static jxl_frame_status_t hf_metadata_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                            const jxl_lf_group_params *params, uint32_t lf_width,
                                            uint32_t lf_height, jxl_lf_group *out) {
                                                size_t i;
    int h_upsample;
    int v_upsample;
    uint32_t nb_extra;
    uint32_t nb_blocks;
    jxl_modular_params mod_params;
    jxl_modular_image_destination meta;
    jxl_modular_parse_ctx ctx = {0};
    const jxl_frame_header *fh = params->frame;
    size_t bw = div_ceil_u32(lf_width, 8);
    size_t bh = div_ceil_u32(lf_height, 8);
    uint32_t nb_bits;
    jxl_modular_channel_params ch[4];
    uint32_t num_lf_groups;
    jxl_frame_status_t fst;
    uint32_t stream_index;
    jxl_modular_status_t mst;

    h_upsample = 0;
    v_upsample = 0;
    for (i = 0; i < 3; ++i) {
        uint32_t j = fh->jpeg_upsampling[i];
        if (j == 1 || j == 2) {
            h_upsample = 1;
        }
        if (j == 1 || j == 3) {
            v_upsample = 1;
        }
    }
    if (h_upsample) {
        bw = (bw + 1) / 2 * 2;
    }
    if (v_upsample) {
        bh = (bh + 1) / 2 * 2;
    }

    nb_bits = trailing_zeros_u32(next_power_of_two_u32((uint32_t)(bw * bh)));
    nb_extra = 0;
    if (jxl_bs_read_bits(bs, nb_bits, &nb_extra) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    nb_blocks = 1u + nb_extra;

    ch[0].width = div_ceil_u32(lf_width, 64);
    ch[0].height = div_ceil_u32(lf_height, 64);
    ch[0].shift = jxl_channel_shift_from_shift(0);
    ch[1].width = div_ceil_u32(lf_width, 64);
    ch[1].height = div_ceil_u32(lf_height, 64);
    ch[1].shift = jxl_channel_shift_from_shift(0);
    ch[2].width = nb_blocks;
    ch[2].height = 2;
    ch[2].shift = jxl_channel_shift_from_shift(0);
    ch[3].width = (uint32_t)bw;
    ch[3].height = (uint32_t)bh;
    ch[3].shift = jxl_channel_shift_from_shift(0);

    jxl_modular_params_init(&mod_params);
    if (!modular_params_set_custom(alloc, &mod_params, 0, params->image->bit_depth_bits, ch, 4)) {
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    ctx.params = &mod_params;
    ctx.global_ma = params->global_ma;
    ctx.tracker = params->tracker;


    jxl_modular_image_destination_init(&meta);
    num_lf_groups = jxl_frame_header_num_lf_groups(fh);
    stream_index = 1u + 2u * num_lf_groups + params->lf_group_idx;
    mst = modular_parse_decode_finish(alloc, bs, &ctx, stream_index, lf_width, lf_height, &meta,
                                    params->allow_partial);
    jxl_modular_params_free(alloc, &mod_params);
    if (mst != JXL_MODULAR_OK) {
        jxl_modular_image_destination_free(alloc, &meta);
        return modular_to_frame(mst);
    }

    if (meta.image_channels_len < 4) {
        jxl_modular_image_destination_free(alloc, &meta);
        return JXL_FRAME_DECODER_ERROR;
    }

    out->block_info_stride = bw;
    out->block_info_width = bw;
    out->block_info_height = bh;
    out->block_info = jxl_calloc(alloc, bw * bh, sizeof(*out->block_info));
    if (out->block_info == NULL) {
        jxl_modular_image_destination_free(alloc, &meta);
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    for (i = 0; i < bw * bh; ++i) {
        out->block_info[i].kind = JXL_BLOCK_INFO_UNINIT;
    }

    if (jxl_epf_enabled(&params->frame->restoration)) {
        out->epf_sigma_stride = bw;
        out->epf_sigma_width = bw;
        out->epf_sigma_height = bh;
        out->epf_sigma = jxl_calloc(alloc, bw * bh, sizeof(float));
        if (out->epf_sigma == NULL) {
            jxl_modular_image_destination_free(alloc, &meta);
            return JXL_FRAME_OUT_OF_MEMORY;
        }
    }

    fst = hf_metadata_expand(alloc, params->lf_group_idx, bw, bh, &meta.image_channels[2],
                           &meta.image_channels[3], out->block_info, bw, params, out->epf_sigma,
                           out->epf_sigma_stride);
    if (fst != JXL_FRAME_OK) {
        jxl_modular_image_destination_free(alloc, &meta);
        return fst;
    }

    if (!copy_modular_grid_i32(alloc, &meta.image_channels[0], &out->x_from_y, &out->cfl_width,
                               &out->cfl_height, &out->cfl_stride) ||
        !copy_modular_grid_i32(alloc, &meta.image_channels[1], &out->b_from_y, &out->cfl_width,
                               &out->cfl_height, &out->cfl_stride)) {
        jxl_modular_image_destination_free(alloc, &meta);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    jxl_modular_image_destination_free(alloc, &meta);
    out->has_hf_meta = 1;
    return JXL_FRAME_OK;
}

void jxl_lf_group_init(jxl_lf_group *g) {
    if (g == NULL) {
        return;
    }
    memset(g, 0, sizeof(*g));
    jxl_modular_image_destination_init(&g->lf_quant);
}

void jxl_lf_group_free(jxl_allocator_state *alloc, jxl_lf_group *g) {
    if (g == NULL) {
        return;
    }
    jxl_modular_image_destination_free(alloc, &g->lf_quant);
    jxl_free(alloc, g->block_info);
    jxl_free(alloc, g->x_from_y);
    jxl_free(alloc, g->b_from_y);
    jxl_free(alloc, g->epf_sigma);
    g->block_info = NULL;
    g->x_from_y = NULL;
    g->b_from_y = NULL;
    g->epf_sigma = NULL;
    jxl_lf_group_init(g);
}

void jxl_lf_group_fill_view(const jxl_lf_group *g, jxl_lf_group_view *out) {
    if (g == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (g->has_hf_meta && g->block_info != NULL) {
        out->block_info_data = g->block_info;
        out->block_info_width = g->block_info_width;
        out->block_info_height = g->block_info_height;
        out->block_info_stride = g->block_info_stride;
    }
    if (g->has_lf_coeff) {
        out->lf_quant = g->lf_quant_view;
    }
    if (g->x_from_y != NULL && g->b_from_y != NULL) {
        jxl_cfl_subgrid_i32 compound_tmp;
        jxl_cfl_subgrid_i32 compound_tmp_2;
        compound_tmp.data = g->x_from_y;
        compound_tmp.width = g->cfl_width;
        compound_tmp.height = g->cfl_height;
        compound_tmp.stride = g->cfl_stride;
        out->x_from_y = compound_tmp;

        compound_tmp_2.data = g->b_from_y;
        compound_tmp_2.width = g->cfl_width;
        compound_tmp_2.height = g->cfl_height;
        compound_tmp_2.stride = g->cfl_stride;
        out->b_from_y = compound_tmp_2;

    }
}

jxl_frame_status_t jxl_lf_group_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                      const jxl_lf_group_params *params, jxl_lf_group *out) {
    uint32_t lf_width;
    uint32_t lf_height;
    int mlf_complete;
    if (alloc == NULL || bs == NULL || params == NULL || params->image == NULL ||
        params->frame == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    lf_width = 0;
    lf_height = 0;
    jxl_frame_header_lf_group_size_for(params->frame, params->lf_group_idx, &lf_width, &lf_height);

    if (params->frame->encoding == JXL_FRAME_ENCODING_VARDCT &&
        !jxl_frame_flags_use_lf_frame(&params->frame->flags)) {
        jxl_frame_status_t fst = lf_coeff_parse(alloc, bs, params, lf_width, lf_height, out);
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
        if (out->partial) {
            return JXL_FRAME_OK;
        }
    }

    mlf_complete = 1;
    if (params->gmodular != NULL) {
        jxl_frame_status_t fst = mlf_group_decode(alloc, bs, params, &mlf_complete);
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
        if (!mlf_complete) {
            out->partial = 1;
            return JXL_FRAME_OK;
        }
    }

    if (params->frame->encoding == JXL_FRAME_ENCODING_VARDCT) {
        jxl_frame_status_t fst = hf_metadata_parse(alloc, bs, params, lf_width, lf_height, out);
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
    }

    return JXL_FRAME_OK;
}
