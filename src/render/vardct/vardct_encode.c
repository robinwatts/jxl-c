// SPDX-License-Identifier: MIT OR Apache-2.0
#include "vardct_encode.h"

#include "bitstream/bitstream.h"
#include "frame/filter.h"
#include "frame/frame_header.h"
#include "frame/hf_global.h"
#include "frame/lf_group.h"
#include "frame/pass_group.h"
#include "frame/toc.h"
#include "modular/region.h"
#include "modular/image.h"
#include "grid/aligned_grid.h"
#include "render/subgrid_f32.h"
#include "render/vardct/cfl_hf.h"
#include "render/vardct/dequant_hf.h"
#include "render/vardct/group_pipeline.h"
#include "render/vardct/lf_dequant.h"
#include "render/vardct/lf_smooth.h"
#include "vardct/lf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t div_ceil_u32(uint32_t a, uint32_t b) {
    if (b == 0) {
        return 0;
    }
    return (a + b - 1u) / b;
}

static void modular_region_channel_fb_pixel_size(const jxl_modular_region *region,
                                                 const jxl_frame_header *fh, size_t ch,
                                                 uint32_t *out_w, uint32_t *out_h) {
    jxl_channel_shift shift;
    uint32_t w_blocks;
    uint32_t h_blocks;
    if (region == NULL || fh == NULL || out_w == NULL || out_h == NULL) {
        return;
    }
    shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, ch);
    w_blocks = div_ceil_u32(region->width, 8u);
    h_blocks = div_ceil_u32(region->height, 8u);
    jxl_channel_shift_shift_size(&shift, w_blocks, h_blocks, &w_blocks, &h_blocks);
    *out_w = w_blocks * 8u;
    *out_h = h_blocks * 8u;
}

static void modular_lf_region_channel_block_size(const jxl_modular_region *lf_region,
                                                 const jxl_frame_header *fh, size_t ch,
                                                 uint32_t *out_w, uint32_t *out_h) {
    jxl_channel_shift shift;
    if (lf_region == NULL || fh == NULL || out_w == NULL || out_h == NULL) {
        return;
    }
    shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, ch);
    jxl_channel_shift_shift_size(&shift, lf_region->width, lf_region->height, out_w, out_h);
}

static void frame_color_blocks_rounded(const jxl_frame_header *fh, uint32_t *blocks_w,
                                       uint32_t *blocks_h) {
                                           size_t i;
    int h_upsample;
    int v_upsample;
    uint32_t w8;
    uint32_t h8;
    if (fh == NULL || blocks_w == NULL || blocks_h == NULL) {
        return;
    }
    w8 = div_ceil_u32(jxl_frame_header_color_sample_width(fh), 8u);
    h8 = div_ceil_u32(jxl_frame_header_color_sample_height(fh), 8u);
    h_upsample = 0;
    v_upsample = 0;
    for (i = 0; i < 3; ++i) {
        uint32_t v = fh->jpeg_upsampling[i];
        if (v == 1u || v == 2u) {
            h_upsample = 1;
        }
        if (v == 1u || v == 3u) {
            v_upsample = 1;
        }
    }
    if (h_upsample) {
        w8 = div_ceil_u32(w8, 2u) * 2u;
    }
    if (v_upsample) {
        h8 = div_ceil_u32(h8, 2u) * 2u;
    }
    *blocks_w = w8;
    *blocks_h = h8;
}

static size_t simd_padded_dim(size_t n) {
    return (n + 3u) & ~(size_t)3u;
}

static void channel_f32_buf_free(jxl_allocator_state *alloc, jxl_vardct_f32_buf *buf) {
    if (buf == NULL) {
        return;
    }
    jxl_grid_f32_destroy(alloc, &buf->grid);
    buf->data = NULL;
    buf->width = buf->height = buf->stride = 0;
}

static int channel_f32_buf_alloc(jxl_allocator_state *alloc, uint32_t width, uint32_t height,
                                 jxl_vardct_f32_buf *buf) {
    size_t w;
    size_t h;
    if (buf == NULL || width == 0 || height == 0) {
        return 0;
    }
    w = simd_padded_dim((size_t)width);
    h = simd_padded_dim((size_t)height);
    if (!jxl_grid_f32_create(alloc, w, h, NULL, &buf->grid, NULL)) {
        return 0;
    }
    buf->data = jxl_grid_f32_buf(&buf->grid);
    buf->width = (size_t)width;
    buf->height = (size_t)height;
    buf->stride = w;
    if (buf->data != NULL) {
        memset(buf->data, 0, w * h * sizeof(float));
    }
    return 1;
}

static void blit_subgrid_to_buf(const jxl_subgrid_f32 src, jxl_vardct_f32_buf *dst, size_t dst_x,
                                size_t dst_y) {
                                    size_t y;
    if (dst == NULL || dst->data == NULL) {
        return;
    }
    for (y = 0; y < src.height; ++y) {
        size_t x;
        if (dst_y + y >= dst->height) {
            break;
        }
        for (x = 0; x < src.width; ++x) {
            if (dst_x + x >= dst->width) {
                break;
            }
            dst->data[(dst_y + y) * dst->stride + (dst_x + x)] = jxl_subgrid_f32_get(src, x, y);
        }
    }
}

static int frame_is_subsampled(const jxl_frame_header *fh) {
    size_t i;
    if (fh == NULL) {
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        if (fh->jpeg_upsampling[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static void group_channel_padded_size(const jxl_frame_header *fh, jxl_channel_shift shift,
                                      uint32_t fb_w, uint32_t fb_h, uint32_t left, uint32_t top,
                                      uint32_t *out_w, uint32_t *out_h) {
    uint32_t group_dim = jxl_frame_header_group_dim(fh);
    int32_t hshift = jxl_channel_shift_hshift(&shift);
    int32_t vshift = jxl_channel_shift_vshift(&shift);
    uint32_t cw = group_dim >> (uint32_t)hshift;
    uint32_t ch = group_dim >> (uint32_t)vshift;
    uint32_t dst_x = left >> (uint32_t)hshift;
    uint32_t dst_y = top >> (uint32_t)vshift;
    uint32_t rem_w = fb_w > dst_x ? fb_w - dst_x : 0u;
    uint32_t rem_h = fb_h > dst_y ? fb_h - dst_y : 0u;
    if (cw > rem_w) {
        cw = rem_w;
    }
    if (ch > rem_h) {
        ch = rem_h;
    }
    *out_w = cw;
    *out_h = ch;
}

static void frame_fb_channel_size(const jxl_frame_header *fh, uint32_t blocks_w, uint32_t blocks_h,
                                  size_t ch, uint32_t *out_w, uint32_t *out_h) {
    jxl_channel_shift shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, ch);
    uint32_t lw = blocks_w;
    uint32_t lh = blocks_h;
    jxl_channel_shift_shift_size(&shift, lw, lh, &lw, &lh);
    *out_w = lw * 8u;
    *out_h = lh * 8u;
}

static int group_coeff_bufs_alloc(jxl_allocator_state *alloc, const jxl_frame_header *fh,
                                  const uint32_t fb_wh[3][2], uint32_t group_idx,
                                  jxl_vardct_group_coeff_bufs *bufs) {
                                      size_t idx;
    uint32_t group_dim = jxl_frame_header_group_dim(fh);
    uint32_t groups_per_row = jxl_frame_header_groups_per_row(fh);
    uint32_t group_x = group_idx % groups_per_row;
    uint32_t group_y = group_idx / groups_per_row;
    uint32_t left = group_x * group_dim;
    uint32_t top = group_y * group_dim;
    memset(bufs, 0, sizeof(*bufs));
    for (idx = 0; idx < 3; ++idx) {
        uint32_t w = 0;
        uint32_t h = 0;
        jxl_channel_shift sh =
            jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, idx);
        size_t count;
        group_channel_padded_size(fh, sh, fb_wh[idx][0], fb_wh[idx][1], left, top, &w, &h);
        count = (size_t)w * (size_t)h;
        bufs->data[idx] = jxl_alloc(alloc, count * sizeof(int32_t));
        if (bufs->data[idx] == NULL) {
            return 0;
        }
        memset(bufs->data[idx], 0, count * sizeof(int32_t));
        bufs->width[idx] = (size_t)w;
        bufs->height[idx] = (size_t)h;
        bufs->stride[idx] = (size_t)w;
    }
    return 1;
}

static void init_group_bs_at_offset(jxl_bs *bs, const jxl_frame_group_data *src,
                                    size_t bit_offset) {
    jxl_bs_init(bs, src->bytes, src->bytes_len);
    if (bit_offset > 0) {
        jxl_bs_skip_bits(bs, bit_offset);
    }
}

static void encode_set_error(char **error_out, jxl_allocator_state *alloc, const char *message) {
    if (error_out == NULL || alloc == NULL) {
        return;
    }
    jxl_free(alloc, *error_out);
    *error_out = jxl_strdup(alloc, message);
}

static jxl_status_t frame_to_status(jxl_frame_status_t st) {
    switch (st) {
    case JXL_FRAME_OK:
        return JXL_OK;
    case JXL_FRAME_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    case JXL_FRAME_BITSTREAM_ERROR:
    case JXL_FRAME_VALIDATION_ERROR:
        return JXL_ERROR_INVALID_INPUT;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
}

void jxl_vardct_encode_ctx_init(jxl_vardct_encode_ctx *ctx) {
    size_t ch;
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    for (ch = 0; ch < 3; ++ch) {
        jxl_grid_f32_init_empty(&ctx->group_coeff_grid[ch]);
    }
    jxl_lf_global_init(&ctx->lf_global);
    jxl_hf_global_init(&ctx->hf_global);
    jxl_modular_params_init(&ctx->mod_params);
}

void jxl_vardct_encode_ctx_free(jxl_allocator_state *alloc, jxl_vardct_encode_ctx *ctx) {
    size_t ch;
    if (alloc == NULL || ctx == NULL) {
        return;
    }
    for (ch = 0; ch < 3; ++ch) {
        jxl_free(alloc, ctx->prepared_fb_data[ch]);
        ctx->prepared_fb_data[ch] = NULL;
        channel_f32_buf_free(alloc, &ctx->lf_xyb[ch]);
        channel_f32_buf_free(alloc, &ctx->fb_xyb[ch]);
        jxl_grid_f32_destroy(alloc, &ctx->group_coeff_grid[ch]);
        ctx->group_coeff_stride[ch] = 0;
    }
    if (ctx->group_coeffs != NULL) {
        uint32_t g;
        for (g = 0; g < ctx->num_groups; ++g) {
            size_t i;
            for (i = 0; i < 3; ++i) {
                jxl_free(alloc, ctx->group_coeffs[g].data[i]);
            }
        }
        jxl_free(alloc, ctx->group_coeffs);
        ctx->group_coeffs = NULL;
    }
    if (ctx->lf_groups != NULL) {
        uint32_t i;
        for (i = 0; i < ctx->num_lf_groups; ++i) {
            jxl_lf_group_free(alloc, &ctx->lf_groups[i]);
        }
        jxl_free(alloc, ctx->lf_groups);
        ctx->lf_groups = NULL;
    }
    if (ctx->has_mod_params) {
        jxl_modular_params_free(alloc, &ctx->mod_params);
        ctx->has_mod_params = 0;
    }
    jxl_hf_global_free(alloc, &ctx->hf_global);
    jxl_lf_global_free(alloc, &ctx->lf_global);
    memset(ctx, 0, sizeof(*ctx));
}


jxl_status_t jxl_vardct_encode_frame(const jxl_vardct_render_params *params, jxl_frame *frame,
                                     const jxl_parsed_image_header *parsed,
                                     jxl_progressive_lf_store *lf_store,
                                     const jxl_modular_region *filter_region,
                                     jxl_vardct_encode_ctx *ctx) {
                                         uint32_t i;
    size_t all_bit_offset;
    jxl_bs lf_bs;
    jxl_modular_region pass_region_aligned;
    jxl_modular_region lf_blk_filter;
    jxl_modular_region modular_region_storage;
    jxl_modular_region modular_lf_region_storage;
    jxl_modular_region lf_region_px;
    int crop_sized_buffers;
    uint32_t frame_blocks_w;
    uint32_t frame_blocks_h;
    uint32_t w_rounded;
    uint32_t h_rounded;
    jxl_bs hf_bs;
    jxl_status_t st = JXL_OK;
    int single_entry;
    const jxl_frame_group_data *all_src;
    const jxl_frame_group_data *lf_src;
    const jxl_modular_region *pass_filter;
    const jxl_modular_region *aligned_filter;
    const jxl_modular_region *fb_filter;
    const jxl_modular_region *lf_filter;
    uint32_t group_dim;
    const jxl_frame_group_data *hf_src;
    if (params == NULL || params->alloc == NULL || frame == NULL || parsed == NULL || ctx == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    single_entry = jxl_toc_is_single_entry(&frame->toc);
    all_src =
        single_entry && frame->data_len > 0 ? &frame->data[0] : NULL;
    all_bit_offset = 0;

    lf_src =
        single_entry ? all_src : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    if (lf_src == NULL) {
        st = JXL_ERROR_INVALID_INPUT;
        encode_set_error(params->error_out, params->alloc, "missing lf-global group");
        return st;
    }

    init_group_bs_at_offset(&lf_bs, lf_src, single_entry ? all_bit_offset : 0);
    {
        jxl_frame_status_t fst;
        jxl_lf_global_params lp = {0};
        lp.ctx = params->ctx;
        lp.image = parsed;
        lp.frame = &frame->header;
        lp.tracker = NULL;
        lp.allow_partial = jxl_frame_group_allow_partial(lf_src);

        fst = jxl_lf_global_consume(params->alloc, &lf_bs, &lp, &ctx->lf_global);
        st = frame_to_status(fst);
        if (st != JXL_OK) {
            encode_set_error(params->error_out, params->alloc, "failed to parse lf-global for vardct render");
            return st;
        }
        if (single_entry) {
            all_bit_offset = lf_bs.num_read_bits;
        }
    }
    if (jxl_frame_flags_use_lf_frame(&frame->header.flags) && ctx->lf_global.gmodular_used &&
        jxl_modular_image_is_partial(&ctx->lf_global.gmodular)) {
        st = JXL_ERROR_INVALID_INPUT;
        encode_set_error(params->error_out, params->alloc,
                  "incomplete gmodular while progressive lf frame is required");
        return st;
    }

    pass_filter = filter_region;
    aligned_filter = pass_filter;
    fb_filter = pass_filter;
    lf_filter = NULL;
    crop_sized_buffers = 0;
    frame_blocks_w = 0;
    frame_blocks_h = 0;
    frame_color_blocks_rounded(&frame->header, &frame_blocks_w, &frame_blocks_h);
    w_rounded = frame_blocks_w * 8u;
    h_rounded = frame_blocks_h * 8u;
    if (filter_region != NULL) {
        jxl_modular_region compound_tmp;
        jxl_modular_vardct_decode_regions(&frame->header, *filter_region,
                                          &pass_region_aligned, &lf_blk_filter);
        pass_filter = &pass_region_aligned;
        modular_region_storage =
            jxl_modular_region_intersection(pass_region_aligned,
                                            jxl_modular_region_with_size(w_rounded, h_rounded));
        modular_lf_region_storage =
            jxl_modular_region_intersection(lf_blk_filter,
                                            jxl_modular_region_with_size(frame_blocks_w,
                                                                         frame_blocks_h));
        if (ctx->lf_global.gmodular_used) {
            jxl_modular_image_destination *dest =
                (jxl_modular_image_destination *)&ctx->lf_global.gmodular;
            modular_region_storage =
                jxl_modular_compute_region(&frame->header, dest, modular_region_storage, 0);
            modular_lf_region_storage =
                jxl_modular_compute_region(&frame->header, dest, modular_lf_region_storage, 1);
            modular_region_storage =
                jxl_modular_region_intersection(modular_region_storage,
                                                jxl_modular_region_with_size(w_rounded, h_rounded));
            modular_lf_region_storage =
                jxl_modular_region_intersection(modular_lf_region_storage,
                                                jxl_modular_region_with_size(frame_blocks_w,
                                                                             frame_blocks_h));
        }
        crop_sized_buffers = 1;
        ctx->crop_sized_buffers = 1;
        fb_filter = &modular_region_storage;
        ctx->fb_region = modular_region_storage;
        ctx->lf_xyb_region = modular_lf_region_storage;
        if (!ctx->lf_global.gmodular_used) {
            ctx->lf_xyb_region =
                jxl_modular_region_intersection(lf_blk_filter,
                                                jxl_modular_region_with_size(frame_blocks_w,
                                                                             frame_blocks_h));
        } else {
            jxl_modular_image_destination *dest =
                (jxl_modular_image_destination *)&ctx->lf_global.gmodular;
            if (!jxl_modular_image_has_palette(dest) && !jxl_modular_image_has_squeeze(dest)) {
                ctx->lf_xyb_region =
                    jxl_modular_region_intersection(lf_blk_filter,
                                                    jxl_modular_region_with_size(frame_blocks_w,
                                                                                 frame_blocks_h));
            }
        }
        compound_tmp.left = ctx->lf_xyb_region.left * 8;
        compound_tmp.top = ctx->lf_xyb_region.top * 8;
        compound_tmp.width = ctx->lf_xyb_region.width * 8;
        compound_tmp.height = ctx->lf_xyb_region.height * 8;
        lf_region_px = compound_tmp;

        lf_filter = &lf_region_px;
    } else {
        jxl_modular_region full_request = jxl_modular_region_with_size(w_rounded, h_rounded);
        jxl_modular_region compound_tmp;
        jxl_modular_vardct_decode_regions(&frame->header, full_request, &pass_region_aligned,
                                          &lf_blk_filter);
        modular_region_storage = jxl_modular_region_intersection(
            pass_region_aligned, jxl_modular_region_with_size(w_rounded, h_rounded));
        modular_lf_region_storage =
            jxl_modular_region_intersection(lf_blk_filter,
                                            jxl_modular_region_with_size(frame_blocks_w,
                                                                         frame_blocks_h));
        if (ctx->lf_global.gmodular_used) {
            jxl_modular_image_destination *dest =
                (jxl_modular_image_destination *)&ctx->lf_global.gmodular;
            modular_region_storage =
                jxl_modular_compute_region(&frame->header, dest, modular_region_storage, 0);
            modular_lf_region_storage =
                jxl_modular_compute_region(&frame->header, dest, modular_lf_region_storage, 1);
            modular_region_storage =
                jxl_modular_region_intersection(modular_region_storage,
                                                jxl_modular_region_with_size(w_rounded, h_rounded));
            modular_lf_region_storage =
                jxl_modular_region_intersection(modular_lf_region_storage,
                                                jxl_modular_region_with_size(frame_blocks_w,
                                                                             frame_blocks_h));
        }
        ctx->fb_region = modular_region_storage;
        ctx->lf_xyb_region = modular_lf_region_storage;
        if (!ctx->lf_global.gmodular_used) {
            ctx->lf_xyb_region =
                jxl_modular_region_intersection(lf_blk_filter,
                                                jxl_modular_region_with_size(frame_blocks_w,
                                                                             frame_blocks_h));
        } else {
            jxl_modular_image_destination *dest =
                (jxl_modular_image_destination *)&ctx->lf_global.gmodular;
            if (!jxl_modular_image_has_palette(dest) && !jxl_modular_image_has_squeeze(dest)) {
                ctx->lf_xyb_region =
                    jxl_modular_region_intersection(lf_blk_filter,
                                                    jxl_modular_region_with_size(frame_blocks_w,
                                                                                 frame_blocks_h));
            }
        }
        crop_sized_buffers = 1;
        ctx->crop_sized_buffers = 1;
        pass_filter = &pass_region_aligned;
        aligned_filter = &pass_region_aligned;
        fb_filter = &modular_region_storage;
        compound_tmp.left = ctx->lf_xyb_region.left * 8;
        compound_tmp.top = ctx->lf_xyb_region.top * 8;
        compound_tmp.width = ctx->lf_xyb_region.width * 8;
        compound_tmp.height = ctx->lf_xyb_region.height * 8;
        lf_region_px = compound_tmp;

        lf_filter = &lf_region_px;
    }

    ctx->num_lf_groups = jxl_frame_header_num_lf_groups(&frame->header);
    if (ctx->num_lf_groups > 0) {
        uint32_t i;
        ctx->lf_groups = jxl_alloc(params->alloc, (size_t)ctx->num_lf_groups * sizeof(*ctx->lf_groups));
        if (ctx->lf_groups == NULL) {
            st = JXL_ERROR_OUT_OF_MEMORY;
            encode_set_error(params->error_out, params->alloc, "out of memory for lf groups");
            return st;
        }
        for (i = 0; i < ctx->num_lf_groups; ++i) {
            jxl_lf_group_init(&ctx->lf_groups[i]);
        }
    }

    group_dim = jxl_frame_header_group_dim(&frame->header);
    for (i = 0; i < ctx->num_lf_groups; ++i) {
        jxl_bs lg_bs;
        jxl_lf_group_params lgp = {0};
        const jxl_frame_group_data *lf_grp_src;
        jxl_frame_status_t fst;
        if (!jxl_modular_lf_group_intersects(&frame->header, i, lf_filter)) {
            continue;
        }
        lf_grp_src =
            single_entry ? all_src : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GROUP, i);
        if (lf_grp_src == NULL) {
            st = JXL_ERROR_INVALID_INPUT;
            encode_set_error(params->error_out, params->alloc, "missing lf group payload");
            return st;
        }

        init_group_bs_at_offset(&lg_bs, lf_grp_src, single_entry ? all_bit_offset : 0);
        lgp.ctx = params->ctx;
        lgp.image = parsed;
        lgp.frame = &frame->header;
        lgp.quantizer = &ctx->lf_global.quantizer;
        lgp.global_ma = ctx->lf_global.has_global_ma ? &ctx->lf_global.global_ma : NULL;
        lgp.gmodular = ctx->lf_global.gmodular_used ? &ctx->lf_global.gmodular : NULL;
        lgp.lf_group_idx = i;
        lgp.tracker = NULL;
        lgp.allow_partial = jxl_frame_group_allow_partial(lf_grp_src);

        fst = jxl_lf_group_parse(params->alloc, &lg_bs, &lgp, &ctx->lf_groups[i]);
        st = frame_to_status(fst);
        if (st != JXL_OK) {
            encode_set_error(params->error_out, params->alloc, "failed to parse lf group for vardct render");
            return st;
        }
        if (!ctx->lf_groups[i].has_hf_meta && !lgp.allow_partial) {
            st = JXL_ERROR_INVALID_INPUT;
            encode_set_error(params->error_out, params->alloc, "incomplete lf group hf metadata");
            return st;
        }
        if (single_entry) {
            all_bit_offset = lg_bs.num_read_bits;
        }
    }

    hf_src =
        single_entry ? all_src : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_HF_GLOBAL, 0);
    if (hf_src == NULL) {
        st = JXL_ERROR_INVALID_INPUT;
        encode_set_error(params->error_out, params->alloc, "missing hf-global group");
        return st;
    }

    init_group_bs_at_offset(&hf_bs, hf_src, single_entry ? all_bit_offset : 0);
    {
        jxl_frame_status_t fst;
        jxl_hf_global_params hp = {0};
        hp.image = parsed;
        hp.frame = &frame->header;
        hp.global_ma = ctx->lf_global.has_global_ma ? &ctx->lf_global.global_ma : NULL;
        hp.hf_block_ctx = &ctx->lf_global.hf_block_ctx;

        fst = jxl_hf_global_parse(params->ctx, params->alloc, &hf_bs, &hp,
                                                     &ctx->hf_global);
        st = frame_to_status(fst);
        if (st != JXL_OK) {
            encode_set_error(params->error_out, params->alloc, "failed to parse hf-global for vardct render");
            return st;
        }
        if (single_entry) {
            all_bit_offset = hf_bs.num_read_bits;
        }
    }

    ctx->num_groups = jxl_frame_header_num_groups(&frame->header);
    {
        size_t ch;
        uint32_t blocks_w = frame_blocks_w;
        uint32_t blocks_h = frame_blocks_h;
        uint32_t fb_wh[3][2];
        for (ch = 0; ch < 3; ++ch) {
            frame_fb_channel_size(&frame->header, blocks_w, blocks_h, ch, &fb_wh[ch][0],
                                  &fb_wh[ch][1]);
        }
        if (ctx->num_groups > 0) {
            uint32_t g;
            ctx->group_coeffs =
                jxl_alloc(params->alloc, (size_t)ctx->num_groups * sizeof(*ctx->group_coeffs));
            if (ctx->group_coeffs == NULL) {
                st = JXL_ERROR_OUT_OF_MEMORY;
                encode_set_error(params->error_out, params->alloc,
                          "out of memory for group coefficients");
                return st;
            }
            memset(ctx->group_coeffs, 0, (size_t)ctx->num_groups * sizeof(*ctx->group_coeffs));
            for (g = 0; g < ctx->num_groups; ++g) {
                if (!jxl_modular_pass_group_intersects(&frame->header, g, fb_filter, group_dim)) {
                    continue;
                }
                if (!group_coeff_bufs_alloc(params->alloc, &frame->header, fb_wh, g,
                                            &ctx->group_coeffs[g])) {
                    st = JXL_ERROR_OUT_OF_MEMORY;
                    encode_set_error(params->error_out, params->alloc,
                              "out of memory for group coefficient buffers");
                    return st;
                }
            }
        }
    }

    if (ctx->lf_global.gmodular_used) {
        ctx->has_mod_params =
            jxl_modular_params_set_for_vardct_frame(params->alloc, params->ctx, &ctx->mod_params,
                                                    parsed, &frame->header);
        if (!ctx->has_mod_params) {
            st = JXL_ERROR_OUT_OF_MEMORY;
            encode_set_error(params->error_out, params->alloc, "failed to build modular params");
            return st;
        }
    }

    {
        uint32_t pass;
        jxl_hf_global_view hf_view = {0};
        hf_view.num_hf_presets = ctx->hf_global.num_hf_presets;
        hf_view.hf_block_ctx = &ctx->lf_global.hf_block_ctx;
        hf_view.hf_passes = ctx->hf_global.hf_passes;
        hf_view.hf_pass_count = ctx->hf_global.hf_pass_count;

        uint32_t num_passes = frame->header.passes.num_passes;
        for (pass = 0; pass < num_passes; ++pass) {
            uint32_t group;
            for (group = 0; group < ctx->num_groups; ++group) {
                size_t c;
                jxl_lf_group_view lf_view;
                jxl_subgrid_i32 coeff_out[3];
                jxl_bs pg_bs;
                uint32_t lf_idx;
                const jxl_frame_group_data *pg_src;
                jxl_pass_group_vardct_params vparams = {0};
                jxl_frame_status_t fst;
                if (!jxl_modular_pass_group_intersects(&frame->header, group, fb_filter,
                                                       group_dim)) {
                    continue;
                }
                lf_idx =
                    jxl_frame_header_lf_group_idx_from_group_idx(&frame->header, group);
                if (lf_idx >= ctx->num_lf_groups || !ctx->lf_groups[lf_idx].has_hf_meta) {
                    continue;
                }

                pg_src = single_entry ? all_src
                                      : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_GROUP_PASS,
                                                                pass * ctx->num_groups + group);
                if (pg_src == NULL || pg_src->bytes_len == 0) {
                    continue;
                }

                jxl_lf_group_fill_view(&ctx->lf_groups[lf_idx], &lf_view);

                for (c = 0; c < 3; ++c) {
                    jxl_subgrid_i32 compound_tmp;
                    compound_tmp.data = ctx->group_coeffs[group].data[c];
                    compound_tmp.width = ctx->group_coeffs[group].width[c];
                    compound_tmp.height = ctx->group_coeffs[group].height[c];
                    compound_tmp.stride = ctx->group_coeffs[group].stride[c];
                    coeff_out[c] = compound_tmp;

                }

                init_group_bs_at_offset(&pg_bs, pg_src, single_entry ? all_bit_offset : 0);
                vparams.ctx = params->ctx;
                vparams.frame_header = &frame->header;
                vparams.lf_group = &lf_view;
                vparams.pass_idx = pass;
                vparams.group_idx = group;
                vparams.hf_global = &hf_view;
                vparams.hf_coeff_out[0] = coeff_out[0];
                vparams.hf_coeff_out[1] = coeff_out[1];
                vparams.hf_coeff_out[2] = coeff_out[2];
                vparams.allow_partial = jxl_frame_group_allow_partial(pg_src);

                fst = jxl_decode_pass_group_vardct(&pg_bs, &vparams);
                st = frame_to_status(fst);
                if (st != JXL_OK) {
                    if (JXL_DEBUG_FLAG(params->ctx, debug_vardct_pg)) {
                        size_t pg_idx =
                            jxl_toc_group_index_bitstream_order(&frame->toc, JXL_TOC_KIND_GROUP_PASS,
                                                                pass * ctx->num_groups + group);
                        fprintf(stderr,
                                "vardct pg fail pass=%u group=%u fst=%d pg_len=%zu idx=%zu "
                                "toc_pass=%u toc_group=%u\n",
                                pass, group, (int)fst, pg_src->bytes_len, pg_idx,
                                pg_idx < frame->toc.groups_len
                                    ? frame->toc.groups[pg_idx].pass_idx
                                    : 999u,
                                pg_idx < frame->toc.groups_len ? frame->toc.groups[pg_idx].group_idx
                                                              : 999u);
                    }
                    encode_set_error(params->error_out, params->alloc,
                              "failed to decode vardct pass group");
                    return st;
                }
                if (ctx->has_mod_params && ctx->lf_global.gmodular_used) {
                    jxl_pass_group_modular_params mparams = {0};
                    mparams.ctx = params->ctx;
                    mparams.alloc = params->alloc;
                    mparams.frame_header = &frame->header;
                    mparams.global_ma = ctx->lf_global.has_global_ma ? &ctx->lf_global.global_ma : NULL;
                    mparams.modular_params = &ctx->mod_params;
                    mparams.modular_dest = &ctx->lf_global.gmodular;
                    mparams.pass_idx = pass;
                    mparams.group_idx = group;
                    mparams.allow_partial = jxl_frame_group_allow_partial(pg_src);

                    fst = jxl_decode_pass_group_modular_coefficients(&pg_bs, &mparams);
                    st = frame_to_status(fst);
                    if (st != JXL_OK) {
                        encode_set_error(params->error_out, params->alloc,
                                  "failed to decode pass group modular coefficients");
                        return st;
                    }
                }
                if (single_entry) {
                    all_bit_offset = pg_bs.num_read_bits;
                }
            }
        }
    }

    {
        size_t ch;
        uint32_t g;
        uint32_t group;
        size_t max_coeff_w[3] = {0, 0, 0};
        size_t max_coeff_h[3] = {0, 0, 0};
        jxl_opsin_inverse_matrix opsin;
        jxl_hf_global_dequant hf_dequant;
        jxl_hf_global_view hf_view = {0};
        uint32_t cs_w = ctx->color_sample_w = jxl_frame_header_color_sample_width(&frame->header);
        uint32_t cs_h = ctx->color_sample_h = jxl_frame_header_color_sample_height(&frame->header);
        uint32_t groups_per_row;
        uint32_t lf_groups_per_row;
        uint32_t lf_group_dim;
        uint32_t w8 = ctx->lf_xyb_region.width;
        uint32_t h8 = ctx->lf_xyb_region.height;
        const jxl_progressive_lf_image *prog_lf;
        int skip_lf_frame;
        int use_prog_lf;
        if (w8 == 0u || h8 == 0u) {
            w8 = frame_blocks_w;
            h8 = frame_blocks_h;
        }
        groups_per_row = jxl_frame_header_groups_per_row(&frame->header);
        lf_groups_per_row = jxl_frame_header_lf_groups_per_row(&frame->header);
        lf_group_dim = jxl_frame_header_lf_group_dim(&frame->header);

        prog_lf = jxl_progressive_lf_store_get(lf_store, frame->header.lf_level);
        skip_lf_frame = JXL_DEBUG_FLAG(params->ctx, skip_lf_frame);
        use_prog_lf = jxl_frame_flags_use_lf_frame(&frame->header.flags) && !skip_lf_frame &&
                          prog_lf != NULL && prog_lf->valid;

        for (ch = 0; ch < 3; ++ch) {
            uint32_t lw = 0;
            uint32_t lh = 0;
            uint32_t fw = 0;
            uint32_t fh = 0;
            modular_lf_region_channel_block_size(&ctx->lf_xyb_region, &frame->header, ch, &lw, &lh);
            modular_region_channel_fb_pixel_size(&ctx->fb_region, &frame->header, ch, &fw, &fh);
            if (!channel_f32_buf_alloc(params->alloc, lw, lh, &ctx->lf_xyb[ch])) {
                st = JXL_ERROR_OUT_OF_MEMORY;
                encode_set_error(params->error_out, params->alloc, "out of memory for lf xyb");
                return st;
            }
            if (!channel_f32_buf_alloc(params->alloc, fw, fh, &ctx->fb_xyb[ch])) {
                st = JXL_ERROR_OUT_OF_MEMORY;
                encode_set_error(params->error_out, params->alloc, "out of memory for frame buffer");
                return st;
            }
        }
        if (jxl_frame_flags_use_lf_frame(&frame->header.flags) && !skip_lf_frame &&
            (prog_lf == NULL || !prog_lf->valid)) {
            encode_set_error(params->error_out, params->alloc, "uninitialized LF frame reference");
            st = JXL_ERROR_INVALID_INPUT;
            return st;
        }
        if (use_prog_lf) {
            size_t ch;
            jxl_lf_xyb_plane lf_planes[3];
            for (ch = 0; ch < 3; ++ch) {
                jxl_lf_xyb_plane compound_tmp;
                compound_tmp.data = ctx->lf_xyb[ch].data;
                compound_tmp.width = ctx->lf_xyb[ch].width;
                compound_tmp.height = ctx->lf_xyb[ch].height;
                compound_tmp.stride = ctx->lf_xyb[ch].stride;
                lf_planes[ch] = compound_tmp;

            }
            st = jxl_copy_lf_quant_from_progressive(params->alloc, prog_lf, lf_planes,
                                                    &frame->header);
            if (JXL_DEBUG_FLAG(params->ctx, debug_fb) && ctx->lf_xyb[1].data != NULL) {
                uint32_t dx = 2560u;
                uint32_t dy = 1200u;
                size_t bx = dx / 8u;
                size_t by = dy / 8u;
                if (by < ctx->lf_xyb[1].height && bx < ctx->lf_xyb[1].stride) {
                    size_t idx = by * ctx->lf_xyb[1].stride + bx;
                    fprintf(stderr, "ctx->lf_xyb@%u,%u x=%g y=%g b=%g\n", dx, dy,
                            ctx->lf_xyb[0].data[idx], ctx->lf_xyb[1].data[idx], ctx->lf_xyb[2].data[idx]);
                }
            }
            if (JXL_DEBUG_FLAG(params->ctx, debug_lf) && prog_lf->plane[0] != NULL) {
                size_t i;
                float mx[3] = {0, 0, 0};
                size_t n = (size_t)prog_lf->width * (size_t)prog_lf->height;
                for (i = 0; i < n; ++i) {
                    int p;
                    for (p = 0; p < 3; ++p) {
                        float v = prog_lf->plane[p][i];
                        float a = v < 0 ? -v : v;
                        if (a > mx[p]) {
                            mx[p] = a;
                        }
                    }
                }
                fprintf(stderr, "prog_lf %ux%u max_abs=%g,%g,%g p0[0]=%g p1[0]=%g\n",
                        prog_lf->width, prog_lf->height, mx[0], mx[1], mx[2], prog_lf->plane[0][0],
                        prog_lf->plane[1][0]);
            }
            if (st != JXL_OK) {
                encode_set_error(params->error_out, params->alloc,
                          "failed to fill lf buffer from progressive lf frame");
                return st;
            }
        }

        if (!use_prog_lf) {
            uint32_t i;
            for (i = 0; i < ctx->num_lf_groups; ++i) {
                size_t ch;
                uint32_t lf_w;
                uint32_t lf_h;
                jxl_lf_group_view lf_view;
                uint32_t lf_group_x;
                uint32_t lf_group_y;
                uint32_t left_px;
                uint32_t top_px;
                if (!ctx->lf_groups[i].has_lf_coeff || ctx->lf_groups[i].lf_quant.image_channels_len < 3) {
                    continue;
                }
                lf_group_x = i % lf_groups_per_row;
                lf_group_y = i / lf_groups_per_row;
                left_px = lf_group_x * lf_group_dim;
                top_px = lf_group_y * lf_group_dim;
                lf_w = 0;
                lf_h = 0;
                jxl_frame_header_lf_group_size_for(&frame->header, i, &lf_w, &lf_h);

                jxl_lf_group_fill_view(&ctx->lf_groups[i], &lf_view);
                if (lf_view.lf_quant == NULL) {
                    continue;
                }

                for (ch = 0; ch < 3; ++ch) {
                    float m_lf;
                    jxl_channel_shift shift =
                        jxl_channel_shift_from_jpeg_upsampling(frame->header.jpeg_upsampling, ch);
                    uint32_t left_ch = left_px >> (uint32_t)jxl_channel_shift_hshift(&shift);
                    uint32_t top_ch = top_px >> (uint32_t)jxl_channel_shift_vshift(&shift);
                    uint32_t gw = lf_w >> (uint32_t)jxl_channel_shift_hshift(&shift);
                    uint32_t gh = lf_h >> (uint32_t)jxl_channel_shift_vshift(&shift);
                    uint32_t dh = (uint32_t)jxl_channel_shift_hshift(&shift);
                    uint32_t dv = (uint32_t)jxl_channel_shift_vshift(&shift);
                    int64_t dst_x_i =
                        (int64_t)(left_ch / 8u) - (int64_t)(ctx->lf_xyb_region.left >> (int32_t)dh);
                    int64_t dst_y_i =
                        (int64_t)(top_ch / 8u) - (int64_t)(ctx->lf_xyb_region.top >> (int32_t)dv);
                    size_t dst_x;
                    size_t dst_y;
                    size_t bw;
                    size_t bh;
                    jxl_subgrid_f32 dst;
                    const jxl_lf_quant_subgrid_u32 *src;
                    if (dst_x_i < 0 || dst_y_i < 0) {
                        continue;
                    }
                    dst_x = (size_t)dst_x_i;
                    dst_y = (size_t)dst_y_i;
                    bw = (size_t)div_ceil_u32(gw, 8u);
                    bh = (size_t)div_ceil_u32(gh, 8u);

                    dst = jxl_subgrid_f32_sub(jxl_subgrid_f32_from_buf(ctx->lf_xyb[ch].data, ctx->lf_xyb[ch].width,
                                                                       ctx->lf_xyb[ch].height, ctx->lf_xyb[ch].stride),
                                            dst_x, dst_y, bw, bh);
                    src = NULL;
                    m_lf = 0.0f;
                    if (ch == 0) {
                        src = &lf_view.lf_quant[1];
                        m_lf = ctx->lf_global.lf_dequant.m_x_lf;
                    } else if (ch == 1) {
                        src = &lf_view.lf_quant[0];
                        m_lf = ctx->lf_global.lf_dequant.m_y_lf;
                    } else {
                        src = &lf_view.lf_quant[2];
                        m_lf = ctx->lf_global.lf_dequant.m_b_lf;
                    }
                    jxl_copy_lf_dequant(dst, &ctx->lf_global.quantizer, m_lf, src,
                                        ctx->lf_groups[i].extra_precision);
                }
            }
        }

        if (!use_prog_lf && !frame_is_subsampled(&frame->header)) {
            jxl_chroma_from_luma_lf(
                jxl_subgrid_f32_from_buf(ctx->lf_xyb[0].data, ctx->lf_xyb[0].width, ctx->lf_xyb[0].height,
                                         ctx->lf_xyb[0].stride),
                jxl_const_subgrid_f32_from_buf(ctx->lf_xyb[1].data, ctx->lf_xyb[1].width, ctx->lf_xyb[1].height,
                                               ctx->lf_xyb[1].stride),
                jxl_subgrid_f32_from_buf(ctx->lf_xyb[2].data, ctx->lf_xyb[2].width, ctx->lf_xyb[2].height,
                                         ctx->lf_xyb[2].stride),
                &ctx->lf_global.lf_chan_corr);
        }

        if (!use_prog_lf && !jxl_frame_flags_skip_adaptive_lf_smoothing(&frame->header.flags)) {
            if (!jxl_adaptive_lf_smoothing(
                    params->alloc,
                    jxl_subgrid_f32_from_buf(ctx->lf_xyb[0].data, ctx->lf_xyb[0].width, ctx->lf_xyb[0].height,
                                             ctx->lf_xyb[0].stride),
                    jxl_subgrid_f32_from_buf(ctx->lf_xyb[1].data, ctx->lf_xyb[1].width, ctx->lf_xyb[1].height,
                                             ctx->lf_xyb[1].stride),
                    jxl_subgrid_f32_from_buf(ctx->lf_xyb[2].data, ctx->lf_xyb[2].width, ctx->lf_xyb[2].height,
                                             ctx->lf_xyb[2].stride),
                    &ctx->lf_global.lf_dequant, &ctx->lf_global.quantizer)) {
                st = JXL_ERROR_OUT_OF_MEMORY;
                encode_set_error(params->error_out, params->alloc, "out of memory for lf smoothing");
                return st;
            }
        }

        for (g = 0; g < ctx->num_groups; ++g) {
            size_t ch;
            for (ch = 0; ch < 3; ++ch) {
                if (ctx->group_coeffs[g].width[ch] > max_coeff_w[ch]) {
                    max_coeff_w[ch] = ctx->group_coeffs[g].width[ch];
                }
                if (ctx->group_coeffs[g].height[ch] > max_coeff_h[ch]) {
                    max_coeff_h[ch] = ctx->group_coeffs[g].height[ch];
                }
            }
        }
        for (ch = 0; ch < 3; ++ch) {
            size_t stride = simd_padded_dim(max_coeff_w[ch]);
            size_t height = simd_padded_dim(max_coeff_h[ch]);
            float *coeff_buf;
	    if (stride == 0 || height == 0) {
                continue;
            }
            if (!jxl_grid_f32_create(params->alloc, stride, height, NULL, &ctx->group_coeff_grid[ch],
                                     NULL)) {
                st = JXL_ERROR_OUT_OF_MEMORY;
                encode_set_error(params->error_out, params->alloc,
                                 "out of memory for group float coefficients");
                return st;
            }
            ctx->group_coeff_stride[ch] = stride;
            coeff_buf = jxl_grid_f32_buf(&ctx->group_coeff_grid[ch]);
            if (coeff_buf != NULL) {
                memset(coeff_buf, 0, stride * height * sizeof(float));
            }
        }

        opsin.quant_bias[0] = parsed->opsin_inverse.quant_bias[0];
        opsin.quant_bias[1] = parsed->opsin_inverse.quant_bias[1];
        opsin.quant_bias[2] = parsed->opsin_inverse.quant_bias[2];
        opsin.quant_bias_numerator = parsed->opsin_inverse.quant_bias_numerator;

        hf_dequant.ctx = params->ctx;
        hf_dequant.dequant_matrices = &ctx->hf_global.dequant_matrices;
        hf_dequant.quantizer = &ctx->lf_global.quantizer;
        hf_dequant.opsin_inverse = &opsin;

        hf_view.num_hf_presets = ctx->hf_global.num_hf_presets;
        hf_view.hf_block_ctx = &ctx->lf_global.hf_block_ctx;
        hf_view.hf_passes = ctx->hf_global.hf_passes;
        hf_view.hf_pass_count = ctx->hf_global.hf_pass_count;


        for (group = 0; group < ctx->num_groups; ++group) {
            size_t ch;
            jxl_lf_group_view lf_view;
            jxl_subgrid_f32 coeff_out[3];
            jxl_const_subgrid_f32 lf_in[3];
            jxl_lf_xyb_plane lf_planes[3];
            jxl_render_vardct_group_params gparams = {0};
            uint32_t lf_idx;
            uint32_t group_x;
            uint32_t group_y;
            uint32_t left;
            uint32_t top;
            int has_hf_meta;
            int transform_hf;
            float *group_coeff_buf[3];
            int32_t lf_reg_l;
            int32_t lf_reg_t;
            if (!jxl_modular_pass_group_intersects(&frame->header, group, fb_filter, group_dim)) {
                continue;
            }
            lf_idx =
                jxl_frame_header_lf_group_idx_from_group_idx(&frame->header, group);
            if (lf_idx >= ctx->num_lf_groups) {
                continue;
            }

            group_x = group % groups_per_row;
            group_y = group / groups_per_row;
            left = group_x * group_dim;
            top = group_y * group_dim;

            jxl_lf_group_fill_view(&ctx->lf_groups[lf_idx], &lf_view);

            has_hf_meta = ctx->lf_groups[lf_idx].has_hf_meta;
            transform_hf =
                has_hf_meta &&
                (aligned_filter == NULL ||
                 jxl_modular_pass_group_intersects(&frame->header, group, aligned_filter,
                                                   group_dim));

            for (ch = 0; ch < 3; ++ch) {
                group_coeff_buf[ch] = jxl_grid_f32_buf(&ctx->group_coeff_grid[ch]);
            }
            for (ch = 0; ch < 3; ++ch) {
                uint32_t cw = (uint32_t)ctx->group_coeffs[group].width[ch];
                uint32_t chh = (uint32_t)ctx->group_coeffs[group].height[ch];
                size_t coeff_stride = ctx->group_coeff_stride[ch];
                if (has_hf_meta && ctx->group_coeffs[group].data[ch] != NULL && cw > 0 &&
                    chh > 0 && group_coeff_buf[ch] != NULL && coeff_stride > 0) {
                        size_t y;
                    for (y = 0; y < (size_t)chh; ++y) {
                        size_t x;
                        float *row = group_coeff_buf[ch] + y * coeff_stride;
                        const int32_t *src_row =
                            ctx->group_coeffs[group].data[ch] +
                            y * ctx->group_coeffs[group].stride[ch];
                        for (x = 0; x < (size_t)cw; ++x) {
                            row[x] = (float)src_row[x];
                        }
                    }
                } else if (cw > 0 && chh > 0 && group_coeff_buf[ch] != NULL && coeff_stride > 0) {
                    size_t y;
                    for (y = 0; y < (size_t)chh; ++y) {
                        memset(group_coeff_buf[ch] + y * coeff_stride, 0, (size_t)cw * sizeof(float));
                    }
                }
                coeff_out[ch] = jxl_subgrid_f32_from_buf(group_coeff_buf[ch], (size_t)cw, (size_t)chh,
                                                           coeff_stride);
            }
            for (ch = 0; ch < 3; ++ch) {
                jxl_lf_xyb_plane compound_tmp;
                compound_tmp.data = ctx->lf_xyb[ch].data;
                compound_tmp.width = (uint32_t)ctx->lf_xyb[ch].width;
                compound_tmp.height = (uint32_t)ctx->lf_xyb[ch].height;
                compound_tmp.stride = (uint32_t)ctx->lf_xyb[ch].stride;
                lf_planes[ch] = compound_tmp;

            }
            lf_reg_l = ctx->lf_xyb_region.left;
            lf_reg_t = ctx->lf_xyb_region.top;
            if (use_prog_lf && !crop_sized_buffers) {
                jxl_lf_rendered_subgrid_for_group(lf_planes, prog_lf, &frame->header, group, lf_in);
            } else {
                jxl_lf_xyb_subgrid_for_group(lf_planes, &frame->header, group, lf_reg_l, lf_reg_t,
                                             (uint32_t)ctx->lf_xyb_region.width,
                                             (uint32_t)ctx->lf_xyb_region.height, lf_in);
            }

            gparams.ctx = params->ctx;
            gparams.frame_header = &frame->header;
            gparams.lf_group = &lf_view;
            gparams.group_idx = group;
            gparams.hf_global = &hf_dequant;
            gparams.lf_xyb_region = &ctx->lf_xyb_region;
            gparams.lf[0] = lf_in[0];
            gparams.lf[1] = lf_in[1];
            gparams.lf[2] = lf_in[2];
            gparams.coeff[0] = coeff_out[0];
            gparams.coeff[1] = coeff_out[1];
            gparams.coeff[2] = coeff_out[2];


            if (transform_hf) {
                jxl_dequant_hf_varblock_grouped(params->ctx, gparams.coeff, group, &frame->header,
                                                &hf_dequant, &lf_view, &ctx->lf_xyb_region);

                if (!frame_is_subsampled(&frame->header) && lf_view.x_from_y.data != NULL &&
                    lf_view.b_from_y.data != NULL) {
                    size_t cfl_base_x = (size_t)((group_x % 8u) * group_dim / 64u);
                    size_t cfl_base_y = (size_t)((group_y % 8u) * group_dim / 64u);
                    size_t cfl_gw = (coeff_out[0].width + 63) / 64;
                    size_t cfl_gh = (coeff_out[0].height + 63) / 64;
                    jxl_const_subgrid_i32 x_cfl;
                    jxl_const_subgrid_i32 b_cfl;
                    x_cfl.data = lf_view.x_from_y.data + cfl_base_y * lf_view.x_from_y.stride +
                                cfl_base_x;
                    x_cfl.width = cfl_gw;
                    x_cfl.height = cfl_gh;
                    x_cfl.stride = lf_view.x_from_y.stride;

                    b_cfl.data = lf_view.b_from_y.data + cfl_base_y * lf_view.b_from_y.stride +
                                cfl_base_x;
                    b_cfl.width = cfl_gw;
                    b_cfl.height = cfl_gh;
                    b_cfl.stride = lf_view.b_from_y.stride;

                    jxl_chroma_from_luma_hf_grouped(coeff_out, x_cfl, b_cfl,
                                                    &ctx->lf_global.lf_chan_corr);
                }
            }

            jxl_render_transform_with_lf_grouped(&gparams);

            for (ch = 0; ch < 3; ++ch) {
                jxl_channel_shift shift =
                    jxl_channel_shift_from_jpeg_upsampling(frame->header.jpeg_upsampling, ch);
                size_t dst_x = (size_t)(left >> (uint32_t)jxl_channel_shift_hshift(&shift));
                size_t dst_y = (size_t)(top >> (uint32_t)jxl_channel_shift_vshift(&shift));
                uint32_t dh = (uint32_t)jxl_channel_shift_hshift(&shift);
                uint32_t dv = (uint32_t)jxl_channel_shift_vshift(&shift);
                int64_t dst_x_i =
                    (int64_t)dst_x - (int64_t)(ctx->fb_region.left >> (int32_t)dh);
                int64_t dst_y_i =
                    (int64_t)dst_y - (int64_t)(ctx->fb_region.top >> (int32_t)dv);
                if (dst_x_i >= 0 && dst_y_i >= 0) {
                    dst_x = (size_t)dst_x_i;
                    dst_y = (size_t)dst_y_i;
                }
                blit_subgrid_to_buf(coeff_out[ch], &ctx->fb_xyb[ch], dst_x, dst_y);
            }

        }

    }
    return JXL_OK;
}
