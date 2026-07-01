// SPDX-License-Identifier: MIT OR Apache-2.0
#include "frame_header.h"

#include "bitstream/unpack.h"
#include "frame/filter.h"
#include "frame/util.h"
#include "image/parse_helpers.h"

#include <string.h>

static const jxl_u32_spec k_upsampling[4] = {JXL_U32_C(1), JXL_U32_C(2), JXL_U32_C(4), JXL_U32_C(8)};
static const jxl_u32_spec k_crop_coord[4] = {JXL_U32_BITS(0, 8), JXL_U32_BITS(256, 11),
                                             JXL_U32_BITS(2304, 14), JXL_U32_BITS(18688, 30)};
static const jxl_u32_spec k_num_passes[4] = {JXL_U32_C(1), JXL_U32_C(2), JXL_U32_C(3),
                                             JXL_U32_BITS(4, 3)};
static const jxl_u32_spec k_num_ds[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_C(2),
                                         JXL_U32_BITS(3, 1)};

static uint32_t div_ceil_u32(uint32_t a, uint32_t b) {
    return b == 0 ? a : (a + b - 1) / b;
}

void jxl_frame_passes_free(jxl_allocator_state *alloc, jxl_frame_passes *p) {
    if (p == NULL) {
        return;
    }
    jxl_free(alloc, p->shift);
    jxl_free(alloc, p->downsample);
    jxl_free(alloc, p->last_pass);
    memset(p, 0, sizeof(*p));
}

void jxl_frame_header_init(jxl_frame_header *h) {
    if (h != NULL) {
        memset(h, 0, sizeof(*h));
        h->upsampling = 1;
        h->passes.num_passes = 1;
        jxl_restoration_filter_init(&h->restoration);
    }
}

void jxl_frame_header_free(jxl_allocator_state *alloc, jxl_frame_header *h) {
    if (h == NULL) {
        return;
    }
    jxl_free(alloc, h->ec_upsampling);
    jxl_free(alloc, h->ec_blending_info);
    jxl_frame_passes_free(alloc, &h->passes);
    jxl_frame_header_init(h);
}

static void apply_defaults(jxl_allocator_state *alloc, jxl_frame_header *h,
                           const jxl_parsed_image_header *image) {
    jxl_frame_header_init(h);
    h->frame_type = JXL_FRAME_TYPE_REGULAR;
    h->encoding = JXL_FRAME_ENCODING_VARDCT;
    h->do_ycbcr = image != NULL && !image->xyb_encoded;
    h->upsampling = 1;
    h->group_size_shift = 1;
    h->x_qm_scale = (image != NULL && image->xyb_encoded) ? 3 : 2;
    h->b_qm_scale = 2;
    h->passes.num_passes = 1;
    if (image != NULL) {
        int gray;
        h->width = image->size.width;
        h->height = image->size.height;
        if (image->num_extra_channels > 0) {
            h->ec_upsampling = jxl_calloc(alloc, image->num_extra_channels, sizeof(uint32_t));
            if (h->ec_upsampling != NULL) {
                size_t i;
                h->ec_upsampling_len = image->num_extra_channels;
                for (i = 0; i < h->ec_upsampling_len; ++i) {
                    h->ec_upsampling[i] = 1;
                }
            }
        }
        gray = h->encoding == JXL_FRAME_ENCODING_MODULAR && !h->do_ycbcr &&
                   !image->xyb_encoded && image->colour.colour_space == JXL_COLOUR_SPACE_GRAY_I;
        h->encoded_color_channels = gray ? 1 : 3;
    }
    h->is_last = 1;
    h->resets_canvas = 1;
    h->blending_info.mode = JXL_BLEND_REPLACE;
    jxl_restoration_filter_set_defaults(&h->restoration);
}

static jxl_frame_status_t parse_gabor(jxl_bs *bs, jxl_gabor_filter *out) {
    size_t ch;
    int gab_enabled = 0;
    int custom;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &gab_enabled));
    if (!gab_enabled) {
        out->enabled = 0;
        return JXL_FRAME_OK;
    }

    custom = 0;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &custom));
    out->enabled = 1;
    if (!custom) {
        jxl_restoration_filter tmp;
        jxl_restoration_filter_set_defaults(&tmp);
        *out = tmp.gab;
        return JXL_FRAME_OK;
    }

    for (ch = 0; ch < 3; ++ch) {
        float sum;
        JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->weights[ch].w0));
        JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->weights[ch].w1));
        sum = 1.0f + (out->weights[ch].w0 + out->weights[ch].w1) * 4.0f;
        if (sum > -1e-6f && sum < 1e-6f) {
            return JXL_FRAME_VALIDATION_ERROR;
        }
    }
    return JXL_FRAME_OK;
}

static jxl_frame_status_t parse_epf_sigma(jxl_bs *bs, jxl_frame_encoding enc, jxl_epf_sigma *out) {
    if (enc == JXL_FRAME_ENCODING_VARDCT) {
        JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->quant_mul));
    } else {
        out->quant_mul = 0.46f;
    }
    JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->pass0_sigma_scale));
    JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->pass2_sigma_scale));
    JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->border_sad_mul));
    return JXL_FRAME_OK;
}

static jxl_frame_status_t parse_epf(jxl_bs *bs, jxl_frame_encoding enc, jxl_epf_filter *out) {
    uint32_t iters = 0;
    jxl_restoration_filter defaults;
    int weight_custom;
    int sigma_custom;
    JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &iters));
    if (iters == 0) {
        out->enabled = 0;
        return JXL_FRAME_OK;
    }

    out->enabled = 1;
    out->iters = iters;

    jxl_restoration_filter_set_defaults(&defaults);
    memcpy(out->sharp_lut, defaults.epf.sharp_lut, sizeof(out->sharp_lut));
    memcpy(out->channel_scale, defaults.epf.channel_scale, sizeof(out->channel_scale));
    out->sigma = defaults.epf.sigma;
    out->sigma_for_modular = defaults.epf.sigma_for_modular;

    if (enc == JXL_FRAME_ENCODING_VARDCT) {
        int sharp_custom = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &sharp_custom));
        if (sharp_custom) {
            size_t i;
            for (i = 0; i < 8; ++i) {
                JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->sharp_lut[i]));
            }
        }
    }

    weight_custom = 0;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &weight_custom));
    if (weight_custom) {
        size_t i;
        uint32_t ignored;
        for (i = 0; i < 3; ++i) {
            JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->channel_scale[i]));
        }
        ignored = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 32, &ignored));
    }

    sigma_custom = 0;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &sigma_custom));
    if (sigma_custom) {
        jxl_frame_status_t fst = parse_epf_sigma(bs, enc, &out->sigma);
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
    }

    if (enc == JXL_FRAME_ENCODING_MODULAR) {
        JXL_FRAME_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->sigma_for_modular));
    }
    return JXL_FRAME_OK;
}

static jxl_frame_status_t parse_restoration_filter(jxl_bs *bs, jxl_frame_encoding enc,
                                                   jxl_restoration_filter *out) {
    int all_default;
    jxl_frame_status_t fst;
    jxl_restoration_filter_init(out);

    all_default = 1;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &all_default));
    if (all_default) {
        jxl_restoration_filter_set_defaults(out);
        return JXL_FRAME_OK;
    }

    fst = parse_gabor(bs, &out->gab);
    if (fst != JXL_FRAME_OK) {
        return fst;
    }
    fst = parse_epf(bs, enc, &out->epf);
    if (fst != JXL_FRAME_OK) {
        return fst;
    }

    if (jxl_extensions_parse(bs) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    return JXL_FRAME_OK;
}

static jxl_frame_status_t parse_passes(jxl_allocator_state *alloc, jxl_bs *bs,
                                       jxl_frame_passes *p) {
    size_t nshift;
    jxl_frame_passes_free(alloc, p);
    JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_num_passes, &p->num_passes));
    if (p->num_passes == 1) {
        return JXL_FRAME_OK;
    }
    JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_num_ds, &p->num_ds));
    nshift = p->num_passes - 1;
    if (nshift > 0) {
        size_t i;
        p->shift = jxl_calloc(alloc, nshift, sizeof(uint32_t));
        if (p->shift == NULL) {
            return JXL_FRAME_OUT_OF_MEMORY;
        }
        p->shift_len = nshift;
        for (i = 0; i < nshift; ++i) {
            JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &p->shift[i]));
        }
    }
    if (p->num_ds > 0) {
        size_t i;
        p->downsample = jxl_calloc(alloc, p->num_ds, sizeof(uint32_t));
        p->last_pass = jxl_calloc(alloc, p->num_ds, sizeof(uint32_t));
        if (p->downsample == NULL || p->last_pass == NULL) {
            return JXL_FRAME_OUT_OF_MEMORY;
        }
        p->downsample_len = p->num_ds;
        p->last_pass_len = p->num_ds;
        for (i = 0; i < p->num_ds; ++i) {
            uint32_t lp;
            JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_upsampling, &p->downsample[i]));
            lp = 0;
            JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &lp));
            p->last_pass[i] = lp;
        }
    }
    return JXL_FRAME_OK;
}

static int canvas_covers_image(int have_crop, int32_t x0, int32_t y0, uint32_t width,
                               uint32_t height, const jxl_parsed_image_header *image) {
    int64_t right;
    int64_t bottom;
    if (!have_crop) {
        return 1;
    }
    if (image == NULL || x0 > 0 || y0 > 0) {
        return 0;
    }
    right = (int64_t)x0 + (int64_t)width;
    bottom = (int64_t)y0 + (int64_t)height;
    return right >= (int64_t)image->size.width && bottom >= (int64_t)image->size.height;
}

static int blending_resets_canvas(uint32_t mode, int have_crop, int32_t x0, int32_t y0,
                                  uint32_t width, uint32_t height,
                                  const jxl_parsed_image_header *image) {
    return mode == 0 && canvas_covers_image(have_crop, x0, y0, width, height, image);
}

static jxl_frame_status_t parse_blending_info(jxl_bs *bs, int has_ec, int have_crop, int32_t x0,
                                              int32_t y0, uint32_t width, uint32_t height,
                                              const jxl_parsed_image_header *image,
                                              jxl_blending_info *out,
                                              int source_from_color_mode,
                                              jxl_blend_mode color_blend_mode) {
    uint32_t mode;
    static const jxl_u32_spec k_blend_mode[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_C(2),
                                                 JXL_U32_BITS(3, 2)};
    static const jxl_u32_spec k_blend_alpha[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_C(2),
                                                  JXL_U32_BITS(3, 3)};
    jxl_blend_mode source_mode;
    if (out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    mode = 0;
    JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_blend_mode, &mode));
    if (mode > JXL_BLEND_MUL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    out->mode = (jxl_blend_mode)mode;
    out->alpha_channel = 0;
    out->clamp = 0;
    out->source = 0;
    if (has_ec && (mode == 2 || mode == 3)) {
        JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_blend_alpha, &out->alpha_channel));
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &out->clamp));
    } else if (mode == 4) {
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &out->clamp));
    }
    source_mode =
        source_from_color_mode ? color_blend_mode : (jxl_blend_mode)mode;
    if (!blending_resets_canvas((uint32_t)source_mode, have_crop, x0, y0, width, height, image)) {
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &out->source));
    }
    return JXL_FRAME_OK;
}

static jxl_frame_status_t parse_non_default(jxl_allocator_state *alloc, jxl_bs *bs,
                                            const jxl_parsed_image_header *image,
                                            jxl_frame_header *h) {
    uint32_t ft = 0;
    uint32_t enc;
    int read_save_before_ct;
    jxl_bs_status_t st;
    int gray;
    JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &ft));
    h->frame_type = (jxl_frame_type)ft;

    enc = 0;
    JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 1, &enc));
    h->encoding = enc ? JXL_FRAME_ENCODING_MODULAR : JXL_FRAME_ENCODING_VARDCT;

    JXL_FRAME_TRY_BS(jxl_bs_read_u64(bs, &h->flags.flags));

    if (image != NULL && !image->xyb_encoded) {
        int do_ycbcr = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &do_ycbcr));
        h->do_ycbcr = do_ycbcr;
    }

    if (!jxl_frame_flags_use_lf_frame(&h->flags)) {
        size_t nec;
        if (h->do_ycbcr) {
            size_t i;
            for (i = 0; i < 3; ++i) {
                JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &h->jpeg_upsampling[i]));
            }
        }
        JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_upsampling, &h->upsampling));
        nec = image != NULL ? image->num_extra_channels : 0;
        if (nec > 0) {
            size_t i;
            h->ec_upsampling = jxl_calloc(alloc, nec, sizeof(uint32_t));
            if (h->ec_upsampling == NULL) {
                return JXL_FRAME_OUT_OF_MEMORY;
            }
            h->ec_upsampling_len = nec;
            for (i = 0; i < nec; ++i) {
                JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_upsampling, &h->ec_upsampling[i]));
            }
        }
    } else {
        /* use_lf_frame: jpeg_upsampling is absent from the bitstream (Rust leaves zeros). */
        h->upsampling = 1;
    }

    if (h->encoding == JXL_FRAME_ENCODING_MODULAR) {
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &h->group_size_shift));
    } else {
        h->group_size_shift = 1;
    }

    if (image != NULL && image->xyb_encoded && h->encoding == JXL_FRAME_ENCODING_VARDCT) {
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 3, &h->x_qm_scale));
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 3, &h->b_qm_scale));
    } else {
        h->x_qm_scale = (image != NULL && image->xyb_encoded) ? 3u : 2u;
        h->b_qm_scale = 2u;
    }

    if (h->frame_type != JXL_FRAME_TYPE_REFERENCE_ONLY) {
        if (parse_passes(alloc, bs, &h->passes) != JXL_FRAME_OK) {
            return JXL_FRAME_BITSTREAM_ERROR;
        }
    }

    if (h->frame_type == JXL_FRAME_TYPE_LF) {
        uint32_t lf = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &lf));
        h->lf_level = lf + 1u;
    }

    if (h->frame_type != JXL_FRAME_TYPE_LF) {
        int have_crop = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &have_crop));
        h->have_crop = have_crop;
    }

    if (h->have_crop) {
        uint32_t ux = 0;
        uint32_t uy = 0;
        if (h->frame_type != JXL_FRAME_TYPE_REFERENCE_ONLY) {
            JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_crop_coord, &ux));
            JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_crop_coord, &uy));
            h->x0 = jxl_unpack_signed(ux);
            h->y0 = jxl_unpack_signed(uy);
        }
        JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_crop_coord, &h->width));
        JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_crop_coord, &h->height));
    } else if (image != NULL) {
        h->width = image->size.width;
        h->height = image->size.height;
    }

    if (jxl_frame_header_is_normal_frame(h)) {
        int has_ec = image != NULL && image->num_extra_channels > 0;
        int is_last;
        if (parse_blending_info(bs, has_ec, h->have_crop, h->x0, h->y0, h->width, h->height, image,
                                &h->blending_info, 0, JXL_BLEND_REPLACE) != JXL_FRAME_OK) {
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        size_t nec = image != NULL ? image->num_extra_channels : 0;
        if (nec > 0) {
            size_t i;
            h->ec_blending_info = jxl_calloc(alloc, nec, sizeof(*h->ec_blending_info));
            if (h->ec_blending_info == NULL) {
                return JXL_FRAME_OUT_OF_MEMORY;
            }
            h->ec_blending_info_len = nec;
            for (i = 0; i < nec; ++i) {
                if (parse_blending_info(bs, has_ec, h->have_crop, h->x0, h->y0, h->width, h->height,
                                        image, &h->ec_blending_info[i], 1,
                                        h->blending_info.mode) != JXL_FRAME_OK) {
                    return JXL_FRAME_BITSTREAM_ERROR;
                }
            }
        }
        if (image != NULL && image->have_animation) {
            const jxl_u32_spec dur[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_BITS(1, 8),
                                         JXL_U32_BITS(1, 32)};
            JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, dur, &h->duration));
            if (image->have_timecodes) {
                JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 32, &h->timecode));
            }
        }
        is_last = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &is_last));
        h->is_last = is_last;
    } else {
        h->is_last = h->frame_type == JXL_FRAME_TYPE_REGULAR;
    }

    if (h->frame_type != JXL_FRAME_TYPE_LF && !h->is_last) {
        JXL_FRAME_TRY_BS(jxl_bs_read_bits(bs, 2, &h->save_as_reference));
    }

    h->resets_canvas = jxl_frame_header_is_normal_frame(h)
                           ? blending_resets_canvas((uint32_t)h->blending_info.mode, h->have_crop,
                                                    h->x0, h->y0, h->width, h->height, image)
                           : !jxl_frame_header_is_normal_frame(h);

    read_save_before_ct =
        h->frame_type == JXL_FRAME_TYPE_REFERENCE_ONLY ||
        (h->resets_canvas && !h->is_last &&
         (h->duration == 0 || h->save_as_reference != 0) && h->frame_type != JXL_FRAME_TYPE_LF);
    if (read_save_before_ct) {
        int save_before_ct = 0;
        JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &save_before_ct));
        h->save_before_ct = save_before_ct;
    } else {
        h->save_before_ct = !jxl_frame_header_is_normal_frame(h);
    }

    st = jxl_image_skip_name(bs);
    if (st != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (parse_restoration_filter(bs, h->encoding, &h->restoration) != JXL_FRAME_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (jxl_extensions_parse(bs) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    gray = h->encoding == JXL_FRAME_ENCODING_MODULAR && !h->do_ycbcr && image != NULL &&
               !image->xyb_encoded && image->colour.colour_space == JXL_COLOUR_SPACE_GRAY_I;
    h->encoded_color_channels = gray ? 1 : 3;
    return JXL_FRAME_OK;
}

jxl_frame_status_t jxl_frame_header_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                          const jxl_parsed_image_header *image,
                                          jxl_frame_header *out) {
    int all_default;
    if (alloc == NULL || bs == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    jxl_frame_header_free(alloc, out);

    JXL_FRAME_TRY_BS(jxl_bs_zero_pad_to_byte(bs));

    all_default = 1;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &all_default));
    if (all_default) {
        apply_defaults(alloc, out, image);
        return JXL_FRAME_OK;
    }
    return parse_non_default(alloc, bs, image, out);
}

static uint32_t sample_dim(uint32_t dim, uint32_t upsampling, uint32_t lf_level) {
    if (upsampling > 1) {
        dim = div_ceil_u32(dim, upsampling);
    }
    if (lf_level > 0) {
        uint32_t div = 1u << (3 * lf_level);
        dim = (dim + div - 1) >> (3 * lf_level);
    }
    return dim;
}

uint32_t jxl_frame_header_color_sample_width(const jxl_frame_header *h) {
    return h != NULL ? sample_dim(h->width, h->upsampling, h->lf_level) : 0;
}

uint32_t jxl_frame_header_color_sample_height(const jxl_frame_header *h) {
    return h != NULL ? sample_dim(h->height, h->upsampling, h->lf_level) : 0;
}

uint32_t jxl_frame_header_group_dim(const jxl_frame_header *h) {
    if (h == NULL) {
        return 256;
    }
    return 128u << h->group_size_shift;
}

uint32_t jxl_frame_header_num_groups(const jxl_frame_header *h) {
    uint32_t w = jxl_frame_header_color_sample_width(h);
    uint32_t ht = jxl_frame_header_color_sample_height(h);
    uint32_t gd = jxl_frame_header_group_dim(h);
    return div_ceil_u32(w, gd) * div_ceil_u32(ht, gd);
}

uint32_t jxl_frame_header_groups_per_row(const jxl_frame_header *h) {
    uint32_t gd = jxl_frame_header_group_dim(h);
    return div_ceil_u32(jxl_frame_header_color_sample_width(h), gd);
}

uint32_t jxl_frame_header_lf_group_dim(const jxl_frame_header *h) {
    return jxl_frame_header_group_dim(h) * 8u;
}

uint32_t jxl_frame_header_lf_groups_per_row(const jxl_frame_header *h) {
    uint32_t lfg = jxl_frame_header_lf_group_dim(h);
    return div_ceil_u32(jxl_frame_header_color_sample_width(h), lfg);
}

uint32_t jxl_frame_header_lf_group_idx_from_group_idx(const jxl_frame_header *h, uint32_t group_idx) {
    uint32_t lf_group_col;
    uint32_t lf_group_row;
    uint32_t groups_per_row;
    if (h == NULL) {
        return 0;
    }
    groups_per_row = jxl_frame_header_groups_per_row(h);
    if (groups_per_row == 0) {
        return 0;
    }
    lf_group_col = (group_idx % groups_per_row) / 8u;
    lf_group_row = (group_idx / groups_per_row) / 8u;
    return lf_group_col + lf_group_row * jxl_frame_header_lf_groups_per_row(h);
}

uint32_t jxl_frame_header_group_idx_from_coord(const jxl_frame_header *h, uint32_t x, uint32_t y) {
    uint32_t group_idx;
    uint32_t group_x;
    uint32_t group_y;
    uint32_t shift;
    uint32_t groups_per_row;
    if (h == NULL) {
        return UINT32_MAX;
    }
    shift = 7u + h->group_size_shift;
    group_x = x >> shift;
    group_y = y >> shift;
    groups_per_row = jxl_frame_header_groups_per_row(h);
    if (group_x >= groups_per_row) {
        return UINT32_MAX;
    }
    group_idx = group_y * groups_per_row + group_x;
    if (group_idx >= jxl_frame_header_num_groups(h)) {
        return UINT32_MAX;
    }
    return group_idx;
}

void jxl_frame_header_lf_group_size_for(const jxl_frame_header *h, uint32_t lf_group_idx,
                                        uint32_t *width_out, uint32_t *height_out) {
    uint32_t full_cols;
    uint32_t cols_remainder;
    uint32_t full_rows;
    uint32_t rows_remainder;
    uint32_t stride;
    uint32_t row;
    uint32_t col;
    uint32_t lfg;
    uint32_t width;
    uint32_t height;
    if (width_out != NULL) {
        *width_out = 0;
    }
    if (height_out != NULL) {
        *height_out = 0;
    }
    if (h == NULL || width_out == NULL || height_out == NULL) {
        return;
    }
    lfg = jxl_frame_header_lf_group_dim(h);
    width = jxl_frame_header_color_sample_width(h);
    height = jxl_frame_header_color_sample_height(h);
    full_cols = width / lfg;
    cols_remainder = width % lfg;
    full_rows = height / lfg;
    rows_remainder = height % lfg;
    stride = full_cols + (cols_remainder > 0 ? 1u : 0u);
    row = lf_group_idx / stride;
    col = lf_group_idx % stride;
    *width_out = col >= full_cols ? cols_remainder : lfg;
    *height_out = row >= full_rows ? rows_remainder : lfg;
}

void jxl_frame_header_group_size_for(const jxl_frame_header *h, uint32_t group_idx, uint32_t *width_out,
                                     uint32_t *height_out) {
    uint32_t full_cols;
    uint32_t cols_remainder;
    uint32_t full_rows;
    uint32_t rows_remainder;
    uint32_t stride;
    uint32_t row;
    uint32_t col;
    uint32_t group_dim;
    uint32_t width;
    uint32_t height;
    if (width_out != NULL) {
        *width_out = 0;
    }
    if (height_out != NULL) {
        *height_out = 0;
    }
    if (h == NULL || width_out == NULL || height_out == NULL) {
        return;
    }
    group_dim = jxl_frame_header_group_dim(h);
    width = jxl_frame_header_color_sample_width(h);
    height = jxl_frame_header_color_sample_height(h);
    full_cols = width / group_dim;
    cols_remainder = width % group_dim;
    full_rows = height / group_dim;
    rows_remainder = height % group_dim;
    stride = full_cols + (cols_remainder > 0 ? 1u : 0u);
    row = group_idx / stride;
    col = group_idx % stride;
    *width_out = col >= full_cols ? cols_remainder : group_dim;
    *height_out = row >= full_rows ? rows_remainder : group_dim;
}

uint32_t jxl_frame_header_pass_group_modular_stream_index(const jxl_frame_header *h, uint32_t pass_idx,
                                                          uint32_t group_idx) {
    if (h == NULL) {
        return 0;
    }
    return 1u + 3u * jxl_frame_header_num_lf_groups(h) + 17u +
           pass_idx * jxl_frame_header_num_groups(h) + group_idx;
}

uint32_t jxl_frame_header_num_lf_groups(const jxl_frame_header *h) {
    uint32_t w = jxl_frame_header_color_sample_width(h);
    uint32_t ht = jxl_frame_header_color_sample_height(h);
    uint32_t lfg = jxl_frame_header_lf_group_dim(h);
    return div_ceil_u32(w, lfg) * div_ceil_u32(ht, lfg);
}

int jxl_frame_header_is_normal_frame(const jxl_frame_header *h) {
    return h != NULL &&
           (h->frame_type == JXL_FRAME_TYPE_REGULAR || h->frame_type == JXL_FRAME_TYPE_SKIP_PROGRESSIVE);
}

int jxl_frame_header_is_keyframe(const jxl_frame_header *h) {
    return jxl_frame_header_is_normal_frame(h) && (h->is_last || h->duration != 0);
}

int jxl_frame_flags_use_lf_frame(const jxl_frame_flags *f) {
    return f != NULL && (f->flags & 0x20) != 0;
}

int jxl_frame_flags_skip_adaptive_lf_smoothing(const jxl_frame_flags *f) {
    return f != NULL && (f->flags & 0x80) != 0;
}

int jxl_frame_flags_noise(const jxl_frame_flags *f) {
    return f != NULL && (f->flags & 0x1) != 0;
}

int jxl_frame_flags_patches(const jxl_frame_flags *f) {
    return f != NULL && (f->flags & 0x2) != 0;
}

int jxl_frame_flags_splines(const jxl_frame_flags *f) {
    return f != NULL && (f->flags & 0x10) != 0;
}
