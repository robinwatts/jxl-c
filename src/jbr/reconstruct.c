// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/reconstruct.h"

#include "bitstream/bitstream.h"
#include "frame/frame_header.h"
#include "frame/hf_global.h"
#include "frame/lf_global.h"
#include "frame/lf_group.h"
#include "frame/pass_group.h"
#include "frame/toc.h"
#include "jbr/data.h"
#include "jbr/scan.h"
#include "vardct/dequant.h"
#include "vardct/hf_pass.h"

#include <string.h>

static const uint8_t HEADER_ICC[] = "ICC_PROFILE\0";
static const uint8_t HEADER_EXIF[] = "Exif\0\0";
static const uint8_t HEADER_XMP[] = "http://ns.adobe.com/xap/1.0/\0";

static jxl_jbr_status frame_to_jbr(jxl_frame_status_t st) {
    switch (st) {
    case JXL_FRAME_OK:
        return JXL_JBR_OK;
    case JXL_FRAME_OUT_OF_MEMORY:
        return JXL_JBR_OUT_OF_MEMORY;
    default:
        return JXL_JBR_FRAME_PARSE;
    }
}

static void init_group_bs_at_offset(jxl_bs *bs, const jxl_frame_group_data *src,
                                    size_t bit_offset) {
    jxl_bs_init(bs, src->bytes, src->bytes_len);
    if (bit_offset > 0) {
        jxl_bs_skip_bits(bs, bit_offset);
    }
}

static size_t app_data_len(const jxl_jbr_header *h) {
    size_t i;
    size_t total = 0;
    if (h == NULL) {
        return 0;
    }
    for (i = 0; i < h->app_markers_len; ++i) {
        if (h->app_markers[i].ty == 0) {
            total += h->app_markers[i].length;
        }
    }
    return total;
}

static size_t com_data_len(const jxl_jbr_header *h) {
    size_t i;
    size_t total = 0;
    if (h == NULL) {
        return 0;
    }
    for (i = 0; i < h->com_lengths_len; ++i) {
        total += h->com_lengths[i];
    }
    return total;
}

static size_t intermarker_data_len(const jxl_jbr_header *h) {
    size_t i;
    size_t total = 0;
    if (h == NULL) {
        return 0;
    }
    for (i = 0; i < h->intermarker_lengths_len; ++i) {
        total += h->intermarker_lengths[i];
    }
    return total;
}

static void swap_jpeg_upsampling_ycbcr(uint32_t upsampling[3]) {
    uint32_t tmp = upsampling[0];
    upsampling[0] = upsampling[1];
    upsampling[1] = tmp;
}

static int frame_is_subsampled(const uint32_t jpeg_upsampling_ycbcr[3]) {
    size_t i;
    for (i = 0; i < 3; ++i) {
        if (jpeg_upsampling_ycbcr[i] != 0) {
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

static void frame_fb_channel_size(const uint32_t jpeg_upsampling_ycbcr[3], uint32_t blocks_w,
                                  uint32_t blocks_h, size_t ch, uint32_t *out_w, uint32_t *out_h) {
    uint32_t lw;
    uint32_t lh;
    jxl_channel_shift shift =
        jxl_channel_shift_from_jpeg_upsampling(jpeg_upsampling_ycbcr, ch);
    lw = blocks_w;
    lh = blocks_h;
    jxl_channel_shift_shift_size(&shift, lw, lh, &lw, &lh);
    *out_w = lw * 8u;
    *out_h = lh * 8u;
}

static int group_coeff_bufs_alloc(jxl_allocator_state *alloc, const jxl_frame_header *fh,
                                  const uint32_t jpeg_upsampling_ycbcr[3],
                                  const uint32_t fb_wh[3][2], uint32_t group_idx,
                                  jxl_jbr_group_coeff_bufs *bufs) {
                                      size_t idx;
    uint32_t group_dim = jxl_frame_header_group_dim(fh);
    uint32_t groups_per_row = jxl_frame_header_groups_per_row(fh);
    uint32_t group_x = group_idx % groups_per_row;
    uint32_t group_y = group_idx / groups_per_row;
    uint32_t left = group_x * group_dim;
    uint32_t top = group_y * group_dim;
    memset(bufs, 0, sizeof(*bufs));
    for (idx = 0; idx < 3; ++idx) {
        uint32_t w;
        uint32_t h;
        jxl_channel_shift sh =
            jxl_channel_shift_from_jpeg_upsampling(jpeg_upsampling_ycbcr, idx);
        w = 0;
        h = 0;
        group_channel_padded_size(fh, sh, fb_wh[idx][0], fb_wh[idx][1], left, top, &w, &h);
        size_t count = (size_t)w * (size_t)h;
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

static void group_coeff_bufs_free(jxl_allocator_state *alloc, jxl_jbr_group_coeff_bufs *bufs) {
    size_t i;
    if (bufs == NULL) {
        return;
    }
    for (i = 0; i < 3; ++i) {
        jxl_free(alloc, bufs->data[i]);
        bufs->data[i] = NULL;
    }
    memset(bufs, 0, sizeof(*bufs));
}

static void parsed_frame_free(jxl_allocator_state *alloc, jxl_jbr_parsed_frame *parsed) {
    if (parsed == NULL) {
        return;
    }
    jxl_lf_global_free(alloc, &parsed->lf_global);
    jxl_hf_global_free(alloc, &parsed->hf_global);
    if (parsed->lf_groups != NULL) {
        uint32_t i;
        for (i = 0; i < parsed->num_lf_groups; ++i) {
            jxl_lf_group_free(alloc, &parsed->lf_groups[i]);
        }
        jxl_free(alloc, parsed->lf_groups);
    }
    if (parsed->pass_groups != NULL) {
        uint32_t i;
        for (i = 0; i < parsed->num_groups; ++i) {
            group_coeff_bufs_free(alloc, &parsed->pass_groups[i]);
        }
        jxl_free(alloc, parsed->pass_groups);
    }
    memset(parsed, 0, sizeof(*parsed));
}

static int32_t cfl_subgrid_get(const jxl_cfl_subgrid_i32 *sg, size_t x, size_t y) {
    if (sg == NULL || sg->data == NULL || x >= sg->width || y >= sg->height) {
        return 0;
    }
    return sg->data[y * sg->stride + x];
}

static void integer_cfl(const jxl_frame_header *fh, const jxl_hf_global *hf_global,
                        const jxl_lf_group *lf_groups, jxl_jbr_group_coeff_bufs *pass_groups,
                        uint32_t num_groups) {
                            size_t i;
                            uint32_t group_idx;
    int32_t dequant_yx[64];
    int32_t dequant_yb[64];
    const int32_t *dequant_x = jxl_dequant_matrix_set_jpeg_quant(&hf_global->dequant_matrices, 0);
    const int32_t *dequant_y = jxl_dequant_matrix_set_jpeg_quant(&hf_global->dequant_matrices, 1);
    const int32_t *dequant_b = jxl_dequant_matrix_set_jpeg_quant(&hf_global->dequant_matrices, 2);
    if (dequant_x == NULL || dequant_y == NULL || dequant_b == NULL) {
        return;
    }

    for (i = 0; i < 64; ++i) {
        dequant_yx[i] = ((int32_t)1 << JXL_JBR_CFL_FIXED_POINT_BITS) * dequant_y[i] / dequant_x[i];
        dequant_yb[i] = ((int32_t)1 << JXL_JBR_CFL_FIXED_POINT_BITS) * dequant_y[i] / dequant_b[i];
    }
    const int32_t *quant_ratio[2];
    quant_ratio[0] = dequant_yx;
    quant_ratio[1] = dequant_yb;

    uint32_t groups_per_row = jxl_frame_header_groups_per_row(fh);
    const int32_t rounding_const = (int32_t)1 << (JXL_JBR_CFL_FIXED_POINT_BITS - 1);

    for (group_idx = 0; group_idx < num_groups; ++group_idx) {
        size_t pi;
        jxl_lf_group_view lf_view;
        size_t cfl_x_end;
        size_t cfl_y_end;
        int32_t *coeff_y = pass_groups[group_idx].data[0];
        int32_t *coeff_x = pass_groups[group_idx].data[1];
        int32_t *coeff_b = pass_groups[group_idx].data[2];
        size_t width = pass_groups[group_idx].width[0];
        size_t height = pass_groups[group_idx].height[0];
        size_t stride_y = pass_groups[group_idx].stride[0];
        size_t stride_x = pass_groups[group_idx].stride[1];
        size_t stride_b = pass_groups[group_idx].stride[2];

        uint32_t lf_group_idx = jxl_frame_header_lf_group_idx_from_group_idx(fh, group_idx);
        jxl_lf_group_fill_view(&lf_groups[lf_group_idx], &lf_view);

        uint32_t group_x_in_lf = (group_idx % groups_per_row) % 8;
        uint32_t group_y_in_lf = (group_idx / groups_per_row) % 8;
        size_t cfl_x = (size_t)group_x_in_lf * 4;
        size_t cfl_y = (size_t)group_y_in_lf * 4;
        cfl_x_end = cfl_x + 4;
        cfl_y_end = cfl_y + 4;
        if (cfl_x_end > lf_view.x_from_y.width) {
            cfl_x_end = lf_view.x_from_y.width;
        }
        if (cfl_y_end > lf_view.x_from_y.height) {
            cfl_y_end = lf_view.x_from_y.height;
        }

        struct {
            const jxl_cfl_subgrid_i32 *factor;
            int32_t *coeff;
            size_t stride;
        } pairs[2] = {
            {&lf_view.x_from_y, coeff_x, stride_x},
            {&lf_view.b_from_y, coeff_b, stride_b},
        };

        for (pi = 0; pi < 2; ++pi) {
            size_t y;
            const jxl_cfl_subgrid_i32 *cfl_factor = pairs[pi].factor;
            int32_t *coeff = pairs[pi].coeff;
            size_t coeff_stride = pairs[pi].stride;
            const int32_t *qr = quant_ratio[pi];

            for (y = 0; y < height; ++y) {
                size_t x;
                size_t cfl_yi = y / 64;
                size_t q_y = y % 8;
                for (x = 0; x < width; ++x) {
                    size_t cfl_xi = x / 64;
                    int32_t factor = cfl_subgrid_get(cfl_factor, cfl_x + cfl_xi, cfl_y + cfl_yi);
                    int32_t coeff_y_val = coeff_y[y * stride_y + x];
                    size_t q_x = x % 8;
                    int32_t q = qr[q_y + 8 * q_x];
                    int32_t scale_factor = factor * ((int32_t)1 << JXL_JBR_CFL_FIXED_POINT_BITS) /
                                           JXL_JBR_CFL_DEFAULT_COLOR_FACTOR;
                    int32_t q_scale = (q * scale_factor + rounding_const) >> JXL_JBR_CFL_FIXED_POINT_BITS;
                    int32_t cfl_add =
                        (coeff_y_val * q_scale + rounding_const) >> JXL_JBR_CFL_FIXED_POINT_BITS;
                    coeff[y * coeff_stride + x] += cfl_add;
                }
            }
        }
    }
}

static jxl_jbr_status parse_frame_coeffs(jxl_allocator_state *alloc, jxl_context *ctx,
                                         const jxl_jbr_header *jbr_header,
                                         const jxl_frame *frame,
                                         const jxl_parsed_image_header *image,
                                         jxl_jbr_parsed_frame *parsed) {
                                             uint32_t i;
                                             size_t c;
                                             size_t ch;
                                             uint32_t g;
                                             uint32_t pass_idx;
    size_t all_bit_offset;
    jxl_bs lf_bs;
    jxl_bs hf_bs;
    uint32_t fb_wh[3][2];
    jxl_lf_global_params lp = {0};
    jxl_hf_global_params hp = {0};
    uint32_t jpeg_upsampling_ycbcr[3];
    jxl_hf_global_view hf_view = {0};
    if (!jxl_frame_is_loading_done(frame)) {
        return JXL_JBR_FRAME_INCOMPLETE;
    }

    memset(parsed, 0, sizeof(*parsed));
    jxl_lf_global_init(&parsed->lf_global);
    jxl_hf_global_init(&parsed->hf_global);

    int single_entry = jxl_toc_is_single_entry(&frame->toc);
    const jxl_frame_group_data *all_src =
        single_entry && frame->data_len > 0 ? &frame->data[0] : NULL;
    all_bit_offset = 0;

    const jxl_frame_group_data *lf_src =
        single_entry ? all_src : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    if (lf_src == NULL || lf_src->bytes_len == 0 ||
        (!single_entry && jxl_frame_group_allow_partial(lf_src))) {
        return JXL_JBR_FRAME_INCOMPLETE;
    }
    init_group_bs_at_offset(&lf_bs, lf_src, single_entry ? all_bit_offset : 0);
    lp.ctx = ctx;
    lp.image = image;
    lp.frame = &frame->header;
    lp.allow_partial = single_entry ? 0 : jxl_frame_group_allow_partial(lf_src);

    jxl_jbr_status st = frame_to_jbr(jxl_lf_global_consume(alloc, &lf_bs, &lp, &parsed->lf_global));
    if (st != JXL_JBR_OK) {
        return st;
    }
    if (!parsed->lf_global.has_vardct) {
        return JXL_JBR_INCOMPATIBLE_FRAME;
    }
    if (single_entry) {
        all_bit_offset = lf_bs.num_read_bits;
    }

    parsed->num_lf_groups = jxl_frame_header_num_lf_groups(&frame->header);
    parsed->lf_groups = jxl_calloc(alloc, parsed->num_lf_groups, sizeof(jxl_lf_group));
    if (parsed->lf_groups == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    for (i = 0; i < parsed->num_lf_groups; ++i) {
        jxl_lf_group_init(&parsed->lf_groups[i]);
    }

    for (i = 0; i < parsed->num_lf_groups; ++i) {
        jxl_bs lg_bs;
        jxl_lf_group_params lgp = {0};
        const jxl_frame_group_data *lg_src =
            single_entry ? all_src : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GROUP, i);
        if (lg_src == NULL || lg_src->bytes_len == 0 ||
            (!single_entry && jxl_frame_group_allow_partial(lg_src))) {
            return JXL_JBR_FRAME_INCOMPLETE;
        }
        init_group_bs_at_offset(&lg_bs, lg_src, single_entry ? all_bit_offset : 0);
        lgp.ctx = ctx;
        lgp.image = image;
        lgp.frame = &frame->header;
        lgp.quantizer = &parsed->lf_global.quantizer;
        lgp.global_ma = parsed->lf_global.has_global_ma ? &parsed->lf_global.global_ma : NULL;
        lgp.lf_group_idx = i;
        lgp.allow_partial = single_entry ? 0 : jxl_frame_group_allow_partial(lg_src);

        st = frame_to_jbr(jxl_lf_group_parse(alloc, &lg_bs, &lgp, &parsed->lf_groups[i]));
        if (st != JXL_JBR_OK) {
            return st;
        }
        if (single_entry) {
            all_bit_offset = lg_bs.num_read_bits;
        }
    }

    const jxl_frame_group_data *hf_src =
        single_entry ? all_src : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_HF_GLOBAL, 0);
    if (hf_src == NULL || hf_src->bytes_len == 0 ||
        (!single_entry && jxl_frame_group_allow_partial(hf_src))) {
        return JXL_JBR_FRAME_INCOMPLETE;
    }
    init_group_bs_at_offset(&hf_bs, hf_src, single_entry ? all_bit_offset : 0);
    hp.image = image;
    hp.frame = &frame->header;
    hp.global_ma = parsed->lf_global.has_global_ma ? &parsed->lf_global.global_ma : NULL;
    hp.hf_block_ctx = &parsed->lf_global.hf_block_ctx;

    st = frame_to_jbr(jxl_hf_global_parse(ctx, alloc, &hf_bs, &hp, &parsed->hf_global));
    if (st != JXL_JBR_OK) {
        return st;
    }
    if (single_entry) {
        all_bit_offset = hf_bs.num_read_bits;
    }

    for (c = 0; c < 3; ++c) {
        if (jxl_dequant_matrix_set_jpeg_quant(&parsed->hf_global.dequant_matrices, c) == NULL) {
            return JXL_JBR_INCOMPATIBLE_FRAME;
        }
    }

    parsed->num_groups = jxl_frame_header_num_groups(&frame->header);
    parsed->pass_groups = jxl_calloc(alloc, parsed->num_groups, sizeof(jxl_jbr_group_coeff_bufs));
    if (parsed->pass_groups == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }

    uint32_t blocks_w = jxl_frame_header_color_sample_width(&frame->header);
    uint32_t blocks_h = jxl_frame_header_color_sample_height(&frame->header);
    jpeg_upsampling_ycbcr[0] = frame->header.jpeg_upsampling[0];
    jpeg_upsampling_ycbcr[1] = frame->header.jpeg_upsampling[1];
    jpeg_upsampling_ycbcr[2] = frame->header.jpeg_upsampling[2];
    swap_jpeg_upsampling_ycbcr(jpeg_upsampling_ycbcr);
    for (ch = 0; ch < 3; ++ch) {
        frame_fb_channel_size(jpeg_upsampling_ycbcr, blocks_w, blocks_h, ch, &fb_wh[ch][0],
                              &fb_wh[ch][1]);
    }

    for (g = 0; g < parsed->num_groups; ++g) {
        if (!group_coeff_bufs_alloc(alloc, &frame->header, jpeg_upsampling_ycbcr, fb_wh, g,
                                    &parsed->pass_groups[g])) {
            return JXL_JBR_OUT_OF_MEMORY;
        }
    }

    hf_view.num_hf_presets = parsed->hf_global.num_hf_presets;
    hf_view.hf_block_ctx = &parsed->lf_global.hf_block_ctx;
    hf_view.hf_passes = parsed->hf_global.hf_passes;
    hf_view.hf_pass_count = parsed->hf_global.hf_pass_count;


    uint32_t num_passes = frame->header.passes.num_passes;
    for (pass_idx = 0; pass_idx < num_passes; ++pass_idx) {
        uint32_t group_idx;
        for (group_idx = 0; group_idx < parsed->num_groups; ++group_idx) {
            jxl_lf_group_view lf_view;
            jxl_bs pg_bs;
            jxl_subgrid_i32 coeff_out[3];
            jxl_pass_group_vardct_params vparams = {0};
            const jxl_frame_group_data *pg_src =
                single_entry ? all_src
                             : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_GROUP_PASS,
                                                       pass_idx * parsed->num_groups + group_idx);
            if (pg_src == NULL || pg_src->bytes_len == 0 ||
                (!single_entry && jxl_frame_group_allow_partial(pg_src))) {
                return JXL_JBR_FRAME_INCOMPLETE;
            }

            uint32_t lf_idx =
                jxl_frame_header_lf_group_idx_from_group_idx(&frame->header, group_idx);
            jxl_lf_group_fill_view(&parsed->lf_groups[lf_idx], &lf_view);

            jxl_jbr_group_coeff_bufs *bufs = &parsed->pass_groups[group_idx];
            coeff_out[0].data = bufs->data[1];
            coeff_out[0].width = bufs->width[1];
            coeff_out[0].height = bufs->height[1];
            coeff_out[0].stride = bufs->stride[1];
            coeff_out[1].data = bufs->data[0];
            coeff_out[1].width = bufs->width[0];
            coeff_out[1].height = bufs->height[0];
            coeff_out[1].stride = bufs->stride[0];
            coeff_out[2].data = bufs->data[2];
            coeff_out[2].width = bufs->width[2];
            coeff_out[2].height = bufs->height[2];
            coeff_out[2].stride = bufs->stride[2];


            init_group_bs_at_offset(&pg_bs, pg_src, single_entry ? all_bit_offset : 0);
            vparams.ctx = ctx;
            vparams.frame_header = &frame->header;
            vparams.lf_group = &lf_view;
            vparams.pass_idx = pass_idx;
            vparams.group_idx = group_idx;
            vparams.hf_global = &hf_view;
            vparams.hf_coeff_out[0] = coeff_out[0];
            vparams.hf_coeff_out[1] = coeff_out[1];
            vparams.hf_coeff_out[2] = coeff_out[2];
            vparams.allow_partial = single_entry ? 0 : jxl_frame_group_allow_partial(pg_src);

            st = frame_to_jbr(jxl_decode_pass_group_vardct(&pg_bs, &vparams));
            if (st != JXL_JBR_OK) {
                return st;
            }
            if (single_entry) {
                all_bit_offset = pg_bs.num_read_bits;
            }
        }
    }

    int is_subsampled = frame_is_subsampled(jpeg_upsampling_ycbcr);

    if (!jbr_header->is_gray && !is_subsampled) {
        const jxl_lf_channel_correlation *corr = &parsed->lf_global.lf_chan_corr;
        if (corr->colour_factor != (uint32_t)JXL_JBR_CFL_DEFAULT_COLOR_FACTOR ||
            corr->base_correlation_x != 0.0f || corr->base_correlation_b != 0.0f) {
            return JXL_JBR_INCOMPATIBLE_FRAME;
        }
        integer_cfl(&frame->header, &parsed->hf_global, parsed->lf_groups, parsed->pass_groups,
                    parsed->num_groups);
    }

    if (frame->header.do_ycbcr) {
        parsed->dc_offset[0] = 0;
        parsed->dc_offset[1] = 0;
        parsed->dc_offset[2] = 0;
    } else {
        size_t i;
        int32_t dc_dequant[3];
        const int32_t *dequant_x =
            jxl_dequant_matrix_set_jpeg_quant(&parsed->hf_global.dequant_matrices, 0);
        const int32_t *dequant_y =
            jxl_dequant_matrix_set_jpeg_quant(&parsed->hf_global.dequant_matrices, 1);
        const int32_t *dequant_b =
            jxl_dequant_matrix_set_jpeg_quant(&parsed->hf_global.dequant_matrices, 2);
        dc_dequant[0] = dequant_y[0];
        dc_dequant[1] = dequant_x[0];
        dc_dequant[2] = dequant_b[0];

        for (i = 0; i < 3; ++i) {
            parsed->dc_offset[i] = (int16_t)(1024 / dc_dequant[i]);
        }
    }

    return JXL_JBR_OK;
}

static jxl_jbr_status recon_init_streams(jxl_jbr_reconstructor *recon, const jxl_jbr_header *header,
                                         const uint8_t *data, size_t data_len, const uint8_t *icc,
                                         size_t icc_len, const uint8_t *exif, size_t exif_len,
                                         const uint8_t *xmp, size_t xmp_len) {
                                             size_t i;
    size_t com_start = app_data_len(header);
    size_t inter_start = com_start + com_data_len(header);
    size_t tail_start = inter_start + intermarker_data_len(header);
    if (tail_start > data_len) {
        return JXL_JBR_INVALID_DATA;
    }

    recon->marker_ptr = 0;
    recon->app_marker_ptr = 0;
    recon->next_icc_marker = 0;
    recon->icc_marker_offset = 0;
    recon->num_icc_markers = 0;
    for (i = 0; i < header->app_markers_len; ++i) {
        if (header->app_markers[i].ty == 1) {
            recon->num_icc_markers += 1;
        }
    }
    recon->app_data = data;
    recon->com_data = data + com_start;
    recon->intermarker_data = data + inter_start;
    recon->huffman_code_ptr = 0;
    recon->quant_ptr = 0;
    recon->has_last_quant_val = 0;
    recon->scan_info_ptr = 0;
    recon->tail_data = data + tail_start;
    recon->com_length_ptr = 0;
    recon->intermarker_length_ptr = 0;

    recon->icc_profile = icc;
    recon->icc_len = icc_len;
    recon->exif = exif;
    recon->exif_len = exif_len;
    recon->xmp = xmp;
    recon->xmp_len = xmp_len;

    if (header->padding.bits != NULL && header->padding.bits_len > 0) {
        jxl_bs_init(&recon->padding_bs, header->padding.bits, header->padding.bits_len);
        recon->has_padding_bs = 1;
    } else {
        recon->has_padding_bs = 0;
    }

    return JXL_JBR_OK;
}

static jxl_jbr_status write_bytes(jxl_allocator_state *alloc, jxl_jbr_output *out,
                                  const void *data, size_t len) {
    return jxl_jbr_output_write(alloc, out, (const uint8_t *)data, len);
}

static uint32_t hsample_for(uint32_t upsampling) {
    static const uint32_t k_table[4] = {1, 2, 2, 1};
    return k_table[upsampling < 4 ? upsampling : 0];
}

static uint32_t vsample_for(uint32_t upsampling) {
    static const uint32_t k_table[4] = {1, 2, 1, 2};
    return k_table[upsampling < 4 ? upsampling : 0];
}

static uint32_t trailing_zeros_u32(uint32_t n) {
    if (n == 0) {
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctz(n);
#else
    {
        uint32_t c;

        c = 0;
        while ((n & 1u) == 0) {
            ++c;
            n >>= 1;
        }
        return c;
    }
#endif
}

static jxl_jbr_status process_sos(jxl_jbr_reconstructor *recon, jxl_allocator_state *alloc,
                                  jxl_jbr_output *out) {
                                      size_t i;
    jxl_channel_shift upsampling_shifts_ycbcr[3];
    uint32_t max_hsample;
    uint32_t max_vsample;
    uint8_t sos_hdr[5];
    uint8_t scan_tail[3];
    uint32_t jpeg_upsampling_ycbcr[3];
    jxl_jbr_scan_params params = {0};
    const jxl_jbr_header *header = recon->header;
    const jxl_frame_header *fh = &recon->frame->header;
    size_t idx = recon->scan_info_ptr;
    if (idx >= header->scan_info_len || idx >= header->scan_more_info_len) {
        return JXL_JBR_INVALID_DATA;
    }
    const jxl_jbr_scan_info *si = &header->scan_info[idx];
    const jxl_jbr_scan_more_info *smi = &header->scan_more_info[idx];
    recon->scan_info_ptr += 1;

    if (si->component_info_len == 0) {
        return JXL_JBR_INVALID_DATA;
    }
    uint8_t num_comps = (uint8_t)si->component_info_len;
    uint16_t header_len = (uint16_t)(6 + 2 * num_comps);
    sos_hdr[0] = 0xff;
    sos_hdr[1] = 0xda;
    sos_hdr[2] = (uint8_t)(header_len >> 8);
    sos_hdr[3] = (uint8_t)header_len;
    sos_hdr[4] = num_comps;

    jxl_jbr_status st = write_bytes(alloc, out, sos_hdr, 5);
    if (st != JXL_JBR_OK) {
        return st;
    }

    for (i = 0; i < si->component_info_len; ++i) {
        uint8_t comp_bytes[2];
        const jxl_jbr_scan_component_info *c = &si->component_info[i];
        if (c->comp_idx >= header->components_len) {
            return JXL_JBR_INVALID_DATA;
        }
        uint8_t id = header->components[c->comp_idx].id;
        uint8_t table = (uint8_t)((c->dc_tbl_idx << 4) | c->ac_tbl_idx);
        comp_bytes[0] = id;
        comp_bytes[1] = table;

        st = write_bytes(alloc, out, comp_bytes, 2);
        if (st != JXL_JBR_OK) {
            return st;
        }
    }

    scan_tail[0] = si->ss;
    scan_tail[1] = si->se;
    scan_tail[2] = (uint8_t)((si->ah << 4) | si->al);

    st = write_bytes(alloc, out, scan_tail, 3);
    if (st != JXL_JBR_OK) {
        return st;
    }

    jpeg_upsampling_ycbcr[0] = fh->jpeg_upsampling[0];
    jpeg_upsampling_ycbcr[1] = fh->jpeg_upsampling[1];
    jpeg_upsampling_ycbcr[2] = fh->jpeg_upsampling[2];

    swap_jpeg_upsampling_ycbcr(jpeg_upsampling_ycbcr);
    for (i = 0; i < 3; ++i) {
        upsampling_shifts_ycbcr[i] =
            jxl_channel_shift_from_jpeg_upsampling(jpeg_upsampling_ycbcr, i);
    }

    uint32_t *hsamples = jxl_alloc(alloc, num_comps * sizeof(uint32_t));
    uint32_t *vsamples = jxl_alloc(alloc, num_comps * sizeof(uint32_t));
    if (hsamples == NULL || vsamples == NULL) {
        jxl_free(alloc, hsamples);
        jxl_free(alloc, vsamples);
        return JXL_JBR_OUT_OF_MEMORY;
    }
    for (i = 0; i < si->component_info_len; ++i) {
        uint8_t comp_idx = si->component_info[i].comp_idx;
        uint32_t up = comp_idx < 3 ? jpeg_upsampling_ycbcr[comp_idx] : 0;
        hsamples[i] = hsample_for(up);
        vsamples[i] = vsample_for(up);
    }

    max_hsample = 0;
    max_vsample = 0;
    for (i = 0; i < num_comps; ++i) {
        uint32_t hz = trailing_zeros_u32(hsamples[i]);
        uint32_t vz = trailing_zeros_u32(vsamples[i]);
        if (hz > max_hsample) {
            max_hsample = hz;
        }
        if (vz > max_vsample) {
            max_vsample = vz;
        }
    }

    uint32_t w8 = (fh->width / 8u + (fh->width % 8u != 0) + max_hsample) >> max_hsample;
    uint32_t h8 = (fh->height / 8u + (fh->height % 8u != 0) + max_vsample) >> max_vsample;

    if (num_comps == 1) {
        uint32_t full_w8 = fh->width / 8u + (fh->width % 8u != 0);
        uint32_t full_h8 = fh->height / 8u + (fh->height % 8u != 0);
        if ((1u << max_hsample) == hsamples[0]) {
            w8 = full_w8;
            max_hsample = 0;
        }
        if ((1u << max_vsample) == vsamples[0]) {
            h8 = full_h8;
            max_vsample = 0;
        }
        hsamples[0] = 1;
        vsamples[0] = 1;
    }

    params.si = si;
    params.smi = smi;
    params.hsamples = hsamples;
    params.vsamples = vsamples;
    params.num_comps = num_comps;
    params.max_hsample = max_hsample;
    params.max_vsample = max_vsample;
    params.w8 = w8;
    params.h8 = h8;

    memcpy(params.upsampling_shifts_ycbcr, upsampling_shifts_ycbcr, sizeof(upsampling_shifts_ycbcr));

    if (!recon->is_progressive) {
        if (si->ss != 0 || si->se != 0x3f || si->al != 0 || si->ah != 0) {
            jxl_free(alloc, hsamples);
            jxl_free(alloc, vsamples);
            return JXL_JBR_INVALID_DATA;
        }
        st = jxl_jbr_process_scan(recon, alloc, 0, &params, out);
    } else if (si->ah == 0) {
        st = jxl_jbr_process_scan(recon, alloc, 1, &params, out);
    } else {
        st = jxl_jbr_process_scan(recon, alloc, 2, &params, out);
    }

    jxl_free(alloc, hsamples);
    jxl_free(alloc, vsamples);
    return st;
}

static jxl_jbr_status process_next(jxl_jbr_reconstructor *recon, jxl_allocator_state *alloc,
                                   const jxl_parsed_image_header *image, jxl_jbr_output *out) {
    const jxl_jbr_header *header = recon->header;
    if (recon->marker_ptr >= header->markers_len) {
        return JXL_JBR_INVALID_DATA;
    }
    uint8_t marker = header->markers[recon->marker_ptr];
    const jxl_frame_header *fh = &recon->frame->header;
    jxl_jbr_status st = JXL_JBR_OK;

    switch (marker) {
    case 0xc0:
    case 0xc1:
    case 0xc2:
    case 0xc9:
    case 0xca:
        recon->is_progressive = marker == 0xc2 || marker == 0xca;
        {
            size_t idx;
            uint16_t width = (uint16_t)image->size.width;
            uint16_t height = (uint16_t)image->size.height;
            uint8_t num_comps = (uint8_t)header->components_len;
            uint16_t encoded_len = (uint16_t)(8 + num_comps * 3);
            uint8_t sof[10];
            uint32_t jpeg_upsampling_ycbcr[3];
            sof[0] = 0xff;
            sof[1] = marker;
            sof[2] = (uint8_t)(encoded_len >> 8);
            sof[3] = (uint8_t)encoded_len;
            sof[4] = 8;
            sof[5] = (uint8_t)(height >> 8);
            sof[6] = (uint8_t)height;
            sof[7] = (uint8_t)(width >> 8);
            sof[8] = (uint8_t)width;
            sof[9] = num_comps;

            st = write_bytes(alloc, out, sof, 10);
            if (st != JXL_JBR_OK) {
                return st;
            }

            jpeg_upsampling_ycbcr[0] = fh->jpeg_upsampling[0];
            jpeg_upsampling_ycbcr[1] = fh->jpeg_upsampling[1];
            jpeg_upsampling_ycbcr[2] = fh->jpeg_upsampling[2];

            swap_jpeg_upsampling_ycbcr(jpeg_upsampling_ycbcr);
            for (idx = 0; idx < header->components_len; ++idx) {
                uint32_t sampling_factor = idx < 3 ? jpeg_upsampling_ycbcr[idx] : 0;
                uint8_t sampling_val = 0x11;
                uint8_t comp_bytes[3];
                switch (sampling_factor) {
                case 0:
                    sampling_val = 0x11;
                    break;
                case 1:
                    sampling_val = 0x22;
                    break;
                case 2:
                    sampling_val = 0x21;
                    break;
                case 3:
                    sampling_val = 0x12;
                    break;
                default:
                    sampling_val = 0x11;
                    break;
                }
                comp_bytes[0] = header->components[idx].id;
                comp_bytes[1] = sampling_val;
                comp_bytes[2] = header->components[idx].q_idx;

                st = write_bytes(alloc, out, comp_bytes, 3);
                if (st != JXL_JBR_OK) {
                    return st;
                }
            }
        }
        break;

    case 0xc4: {
        size_t i;
        size_t last_idx = recon->huffman_code_ptr;
        size_t encoded_len;
        uint8_t dht_hdr[4];
        while (last_idx < header->huffman_codes_len && !header->huffman_codes[last_idx].is_last) {
            ++last_idx;
        }
        if (last_idx >= header->huffman_codes_len) {
            return JXL_JBR_INVALID_DATA;
        }
        size_t num_tables = last_idx - recon->huffman_code_ptr + 1;
        encoded_len = 2;
        for (i = 0; i < num_tables; ++i) {
            encoded_len +=
                jxl_jbr_huffman_code_encoded_len(&header->huffman_codes[recon->huffman_code_ptr + i]);
        }
        dht_hdr[0] = 0xff;
        dht_hdr[1] = 0xc4;
        dht_hdr[2] = (uint8_t)(encoded_len >> 8);
        dht_hdr[3] = (uint8_t)encoded_len;

        st = write_bytes(alloc, out, dht_hdr, 4);
        if (st != JXL_JBR_OK) {
            return st;
        }

        for (i = 0; i < num_tables; ++i) {
            int j;
            uint8_t id_and_counts[17];
            jxl_jbr_huffman_table table;
            const jxl_jbr_huffman_code *hc = &header->huffman_codes[recon->huffman_code_ptr + i];
            id_and_counts[0] = (uint8_t)(hc->id | (hc->is_ac ? 0x10 : 0));
            memcpy(&id_and_counts[1], &hc->counts[1], 16);
            for (j = 16; j >= 1; --j) {
                if (id_and_counts[j] != 0) {
                    id_and_counts[j] -= 1;
                    break;
                }
            }
            st = write_bytes(alloc, out, id_and_counts, 17);
            if (st != JXL_JBR_OK) {
                return st;
            }
            if (hc->values_len > 0) {
                st = write_bytes(alloc, out, hc->values, hc->values_len - 1);
                if (st != JXL_JBR_OK) {
                    return st;
                }
            }

            st = jxl_jbr_huffman_code_build(alloc, hc, &table);
            if (st != JXL_JBR_OK) {
                return st;
            }
            if (hc->is_ac) {
                recon->ac_tables[hc->id] = table;
                recon->has_ac_table[hc->id] = 1;
            } else {
                recon->dc_tables[hc->id] = table;
                recon->has_dc_table[hc->id] = 1;
            }
        }
        recon->huffman_code_ptr += num_tables;
        break;
    }

    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3:
    case 0xd4:
    case 0xd5:
    case 0xd6:
    case 0xd7: {
        uint8_t rst[2];
        rst[0] = 0xff;
        rst[1] = marker;

        st = write_bytes(alloc, out, rst, 2);
        break;
    }

    case 0xd9: {
        uint8_t eoi[2] = {0xff, 0xd9};
        st = write_bytes(alloc, out, eoi, 2);
        if (st != JXL_JBR_OK) {
            return st;
        }
        size_t tail_len = header->tail_data_length;
        if (tail_len > 0) {
            st = write_bytes(alloc, out, recon->tail_data, tail_len);
        }
        break;
    }

    case 0xda:
        st = process_sos(recon, alloc, out);
        break;

    case 0xdb: {
        size_t i;
        size_t ti;
        size_t last_idx = recon->quant_ptr;
        size_t encoded_len;
        size_t dct_len;
        uint8_t dqt_hdr[4];
        while (last_idx < header->quant_tables_len && !header->quant_tables[last_idx].is_last) {
            ++last_idx;
        }
        if (last_idx >= header->quant_tables_len) {
            return JXL_JBR_INVALID_DATA;
        }
        size_t num_tables = last_idx - recon->quant_ptr + 1;
        encoded_len = 2 + 65 * num_tables;
        for (i = 0; i < num_tables; ++i) {
            if (header->quant_tables[recon->quant_ptr + i].precision != 0) {
                encoded_len += 64;
            }
        }
        dqt_hdr[0] = 0xff;
        dqt_hdr[1] = 0xdb;
        dqt_hdr[2] = (uint8_t)(encoded_len >> 8);
        dqt_hdr[3] = (uint8_t)encoded_len;

        st = write_bytes(alloc, out, dqt_hdr, 4);
        if (st != JXL_JBR_OK) {
            return st;
        }

        dct_len = 0;
        const jxl_coeff_order *dct_order =
            jxl_hf_pass_dct8_natural_order(recon->ctx, &dct_len);
        if (dct_order == NULL || dct_len < 64) {
            return JXL_JBR_INVALID_DATA;
        }

        for (ti = 0; ti < num_tables; ++ti) {
            size_t ci;
            const jxl_jbr_quant_table *qt = &header->quant_tables[recon->quant_ptr + ti];
            size_t channel = SIZE_MAX;
            for (ci = 0; ci < header->components_len; ++ci) {
                if (header->components[ci].q_idx == qt->index) {
                    channel = ci;
                    break;
                }
            }

            const int32_t *q = NULL;
            if (channel != SIZE_MAX) {
                size_t ch = channel;
                if (fh->do_ycbcr && ch <= 1) {
                    ch ^= 1;
                }
                q = jxl_dequant_matrix_set_jpeg_quant(&recon->parsed.hf_global.dequant_matrices, ch);
            }
            if (q != NULL) {
                size_t i;
                for (i = 0; i < 64; ++i) {
                    const jxl_coeff_order *ord = &dct_order[i];
                    /* Match Rust `q[x + 8 * y]` with `for &(y, x) in DCT8_NATURAL_ORDER`. */
                    recon->last_quant_val[i] =
                        (uint16_t)q[(size_t)ord->y + 8u * (size_t)ord->x];
                }
                recon->has_last_quant_val = 1;
            }
            if (!recon->has_last_quant_val) {
                return JXL_JBR_INVALID_DATA;
            }

            if (qt->precision == 0) {
                size_t i;
                uint8_t buf[65];
                buf[0] = qt->index;
                for (i = 0; i < 64; ++i) {
                    buf[1 + i] = (uint8_t)recon->last_quant_val[i];
                }
                st = write_bytes(alloc, out, buf, 65);
            } else {
                size_t i;
                uint8_t buf[129];
                buf[0] = (uint8_t)(qt->index | (qt->precision << 4));
                for (i = 0; i < 64; ++i) {
                    uint16_t val = recon->last_quant_val[i];
                    buf[1 + i * 2] = (uint8_t)(val >> 8);
                    buf[2 + i * 2] = (uint8_t)val;
                }
                st = write_bytes(alloc, out, buf, 129);
            }
            if (st != JXL_JBR_OK) {
                return st;
            }
        }
        recon->quant_ptr += num_tables;
        break;
    }

    case 0xdd: {
        uint16_t interval = (uint16_t)header->restart_interval;
        uint8_t dri[6];
        dri[0] = 0xff;
        dri[1] = 0xdd;
        dri[2] = 0;
        dri[3] = 4;
        dri[4] = (uint8_t)(interval >> 8);
        dri[5] = (uint8_t)interval;

        st = write_bytes(alloc, out, dri, 6);
        if (st == JXL_JBR_OK && header->restart_interval != 0) {
            recon->restart_interval = header->restart_interval;
        }
        break;
    }

    case 0xe0:
    case 0xe1:
    case 0xe2:
    case 0xe3:
    case 0xe4:
    case 0xe5:
    case 0xe6:
    case 0xe7:
    case 0xe8:
    case 0xe9:
    case 0xea:
    case 0xeb:
    case 0xec:
    case 0xed:
    case 0xee:
    case 0xef: {
        if (recon->app_marker_ptr >= header->app_markers_len) {
            return JXL_JBR_INVALID_DATA;
        }
        const jxl_jbr_app_marker *am = &header->app_markers[recon->app_marker_ptr++];
        uint16_t payload_len = (uint16_t)(am->length - 1);
        switch (am->ty) {
        case 0: {
            uint8_t marker = 0xff;
            st = write_bytes(alloc, out, &marker, 1);
            if (st != JXL_JBR_OK) {
                return st;
            }
            st = write_bytes(alloc, out, recon->app_data, am->length);
            if (st != JXL_JBR_OK) {
                return st;
            }
            recon->app_data += am->length;
            break;
        }
        case 1: {
            uint8_t app_hdr[4];
            uint8_t seq[2];
            app_hdr[0] = 0xff;
            app_hdr[1] = 0xe2;
            app_hdr[2] = (uint8_t)(payload_len >> 8);
            app_hdr[3] = (uint8_t)payload_len;

            st = write_bytes(alloc, out, app_hdr, 4);
            if (st != JXL_JBR_OK) {
                return st;
            }
            st = write_bytes(alloc, out, HEADER_ICC, sizeof(HEADER_ICC) - 1);
            if (st != JXL_JBR_OK) {
                return st;
            }
            seq[0] = (uint8_t)(recon->next_icc_marker + 1);
            seq[1] = (uint8_t)recon->num_icc_markers;

            st = write_bytes(alloc, out, seq, 2);
            if (st != JXL_JBR_OK) {
                return st;
            }
            size_t chunk_len = am->length - 5 - (sizeof(HEADER_ICC) - 1);
            if (recon->icc_marker_offset + chunk_len > recon->icc_len) {
                return JXL_JBR_INVALID_DATA;
            }
            st = write_bytes(alloc, out, recon->icc_profile + recon->icc_marker_offset, chunk_len);
            if (st != JXL_JBR_OK) {
                return st;
            }
            recon->next_icc_marker += 1;
            recon->icc_marker_offset += chunk_len;
            break;
        }
        case 2: {
            uint8_t app_hdr[4];
            app_hdr[0] = 0xff;
            app_hdr[1] = 0xe1;
            app_hdr[2] = (uint8_t)(payload_len >> 8);
            app_hdr[3] = (uint8_t)payload_len;

            st = write_bytes(alloc, out, app_hdr, 4);
            if (st != JXL_JBR_OK) {
                return st;
            }
            st = write_bytes(alloc, out, HEADER_EXIF, sizeof(HEADER_EXIF) - 1);
            if (st != JXL_JBR_OK) {
                return st;
            }
            st = write_bytes(alloc, out, recon->exif, recon->exif_len);
            break;
        }
        case 3: {
            uint8_t app_hdr[4];
            app_hdr[0] = 0xff;
            app_hdr[1] = 0xe1;
            app_hdr[2] = (uint8_t)(payload_len >> 8);
            app_hdr[3] = (uint8_t)payload_len;

            st = write_bytes(alloc, out, app_hdr, 4);
            if (st != JXL_JBR_OK) {
                return st;
            }
            st = write_bytes(alloc, out, HEADER_XMP, sizeof(HEADER_XMP) - 1);
            if (st != JXL_JBR_OK) {
                return st;
            }
            st = write_bytes(alloc, out, recon->xmp, recon->xmp_len);
            break;
        }
        default:
            return JXL_JBR_INVALID_DATA;
        }
        break;
    }

    case 0xfe: {
        uint8_t com_hdr[2] = {0xff, 0xfe};
        if (recon->com_length_ptr >= header->com_lengths_len) {
            return JXL_JBR_INVALID_DATA;
        }
        uint32_t length = header->com_lengths[recon->com_length_ptr++];
        st = write_bytes(alloc, out, com_hdr, 2);
        if (st != JXL_JBR_OK) {
            return st;
        }
        st = write_bytes(alloc, out, recon->com_data, length);
        if (st != JXL_JBR_OK) {
            return st;
        }
        recon->com_data += length;
        break;
    }

    case 0xff: {
        if (recon->intermarker_length_ptr >= header->intermarker_lengths_len) {
            return JXL_JBR_INVALID_DATA;
        }
        uint32_t length = header->intermarker_lengths[recon->intermarker_length_ptr++];
        st = write_bytes(alloc, out, recon->intermarker_data, length);
        if (st != JXL_JBR_OK) {
            return st;
        }
        recon->intermarker_data += length;
        break;
    }

    default:
        return JXL_JBR_INVALID_DATA;
    }

    return st;
}

jxl_jbr_status jxl_jbr_reconstruct(jxl_allocator_state *alloc, jxl_context *ctx,
                                   const jxl_jbr_data *jbrd, const jxl_frame *frame,
                                   const jxl_parsed_image_header *image, const uint8_t *icc,
                                   size_t icc_len, const uint8_t *exif, size_t exif_len,
                                   const uint8_t *xmp, size_t xmp_len, jxl_jbr_output *out) {
    size_t data_len;
    jxl_jbr_reconstructor recon;
    uint8_t soi[2] = {0xff, 0xd8};
    if (alloc == NULL || ctx == NULL || jbrd == NULL || frame == NULL || image == NULL ||
        out == NULL) {
        return JXL_JBR_INVALID_DATA;
    }

    const jxl_jbr_header *header = jxl_jbr_data_header(jbrd);
    data_len = 0;
    const uint8_t *data = jxl_jbr_data_payload(jbrd, &data_len);
    if (header == NULL || (data_len > 0 && data == NULL)) {
        return JXL_JBR_INVALID_DATA;
    }

    size_t expected_icc = jxl_jbr_header_expected_icc_len(header);
    size_t expected_exif = jxl_jbr_header_expected_exif_len(header);
    size_t expected_xmp = jxl_jbr_header_expected_xmp_len(header);
    if (expected_icc > 0 && expected_icc != icc_len) {
        return JXL_JBR_INVALID_DATA;
    }
    if (expected_exif > 0 && expected_exif != exif_len) {
        return JXL_JBR_INVALID_DATA;
    }
    if (expected_xmp > 0 && expected_xmp != xmp_len) {
        return JXL_JBR_INVALID_DATA;
    }

    if (image->xyb_encoded) {
        return JXL_JBR_INCOMPATIBLE_FRAME;
    }
    if (frame->header.encoding != JXL_FRAME_ENCODING_VARDCT ||
        !jxl_frame_header_is_normal_frame(&frame->header) ||
        jxl_frame_flags_use_lf_frame(&frame->header.flags) ||
        !jxl_frame_flags_skip_adaptive_lf_smoothing(&frame->header.flags)) {
        return JXL_JBR_INCOMPATIBLE_FRAME;
    }

    memset(&recon, 0, sizeof(recon));
    recon.header = header;
    recon.frame = frame;
    recon.ctx = ctx;

    jxl_jbr_status st = parse_frame_coeffs(alloc, ctx, header, frame, image, &recon.parsed);
    if (st != JXL_JBR_OK) {
        parsed_frame_free(alloc, &recon.parsed);
        return st;
    }

    st = recon_init_streams(&recon, header, data, data_len, icc, icc_len, exif, exif_len, xmp,
                            xmp_len);
    if (st != JXL_JBR_OK) {
        parsed_frame_free(alloc, &recon.parsed);
        return st;
    }

    st = write_bytes(alloc, out, soi, 2);
    if (st != JXL_JBR_OK) {
        parsed_frame_free(alloc, &recon.parsed);
        return st;
    }

    while (recon.marker_ptr < header->markers_len) {
        st = process_next(&recon, alloc, image, out);
        if (st != JXL_JBR_OK) {
            parsed_frame_free(alloc, &recon.parsed);
            return st;
        }
        recon.marker_ptr += 1;
    }

    parsed_frame_free(alloc, &recon.parsed);
    return JXL_JBR_OK;
}
