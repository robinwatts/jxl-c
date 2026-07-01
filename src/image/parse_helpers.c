// SPDX-License-Identifier: MIT OR Apache-2.0
#include "parse_helpers.h"

#include "bitstream/unpack.h"

#include <string.h>

static const jxl_u32_spec k_name_len[4] = {JXL_U32_C(0), JXL_U32_BITS(0, 4), JXL_U32_BITS(16, 5),
                                           JXL_U32_BITS(48, 10)};

static const jxl_u32_spec k_customxy_x[4] = {JXL_U32_BITS(0, 19), JXL_U32_BITS(524288, 19),
                                           JXL_U32_BITS(1048576, 20), JXL_U32_BITS(2097152, 21)};

jxl_bs_status_t jxl_image_skip_name(jxl_bs *bs) {
    uint32_t len = 0;
    jxl_bs_status_t st = jxl_bs_read_u32(bs, k_name_len, &len);
    if (st != JXL_BS_OK) {
        return st;
    }
    return jxl_bs_skip_bits(bs, len * 8);
}

static jxl_bs_status_t read_customxy(jxl_bs *bs, int32_t *x, int32_t *y) {
    uint32_t ux = 0;
    uint32_t uy = 0;
    jxl_bs_status_t st = jxl_bs_read_u32(bs, k_customxy_x, &ux);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = jxl_bs_read_u32(bs, k_customxy_x, &uy);
    if (st != JXL_BS_OK) {
        return st;
    }
    *x = jxl_unpack_signed(ux);
    *y = jxl_unpack_signed(uy);
    return JXL_BS_OK;
}

static jxl_bs_status_t parse_white_point(jxl_bs *bs, jxl_colour_space_i cs,
                                         jxl_white_point_i *wp, int32_t *cx, int32_t *cy) {
    uint32_t d;
    jxl_bs_status_t st;
    if (cs == JXL_COLOUR_SPACE_XYB_I) {
        *wp = JXL_WHITE_POINT_D65_I;
        return JXL_BS_OK;
    }
    d = 0;
    st = jxl_bs_read_enum(bs, &d);
    if (st != JXL_BS_OK) {
        return st;
    }
    switch (d) {
    case 1:
        *wp = JXL_WHITE_POINT_D65_I;
        return JXL_BS_OK;
    case 2:
        *wp = JXL_WHITE_POINT_CUSTOM_I;
        return read_customxy(bs, cx, cy);
    case 10:
        *wp = JXL_WHITE_POINT_E_I;
        return JXL_BS_OK;
    case 11:
        *wp = JXL_WHITE_POINT_DCI_I;
        return JXL_BS_OK;
    default:
        return JXL_BS_VALIDATION_FAILED;
    }
}

static jxl_bs_status_t parse_primaries(jxl_bs *bs, jxl_colour_space_i cs, jxl_primaries_i *prim,
                                       int32_t *rx, int32_t *ry, int32_t *gx, int32_t *gy,
                                       int32_t *bx, int32_t *by) {
    uint32_t d;
    jxl_bs_status_t st;
    if (cs == JXL_COLOUR_SPACE_XYB_I || cs == JXL_COLOUR_SPACE_GRAY_I) {
        *prim = JXL_PRIMARIES_SRGB_I;
        return JXL_BS_OK;
    }
    d = 0;
    st = jxl_bs_read_enum(bs, &d);
    if (st != JXL_BS_OK) {
        return st;
    }
    switch (d) {
    case 1:
        *prim = JXL_PRIMARIES_SRGB_I;
        return JXL_BS_OK;
    case 2:
        *prim = JXL_PRIMARIES_CUSTOM_I;
        st = read_customxy(bs, rx, ry);
        if (st != JXL_BS_OK) {
            return st;
        }
        st = read_customxy(bs, gx, gy);
        if (st != JXL_BS_OK) {
            return st;
        }
        return read_customxy(bs, bx, by);
    case 9:
        *prim = JXL_PRIMARIES_BT2100_I;
        return JXL_BS_OK;
    case 11:
        *prim = JXL_PRIMARIES_P3_I;
        return JXL_BS_OK;
    default:
        return JXL_BS_VALIDATION_FAILED;
    }
}

static jxl_bs_status_t parse_transfer_function(jxl_bs *bs, jxl_transfer_function_i *tf,
                                               uint32_t *gamma_1e7) {
    int has_gamma = 0;
    jxl_bs_status_t st = jxl_bs_read_bool(bs, &has_gamma);
    uint32_t v;
    if (st != JXL_BS_OK) {
        return st;
    }
    if (has_gamma) {
        uint32_t g = 0;
        st = jxl_bs_read_bits(bs, 24, &g);
        if (st != JXL_BS_OK) {
            return st;
        }
        *tf = JXL_TRANSFER_GAMMA_I;
        *gamma_1e7 = g;
        return JXL_BS_OK;
    }
    v = 0;
    st = jxl_bs_read_enum(bs, &v);
    if (st != JXL_BS_OK) {
        return st;
    }
    switch (v) {
    case 1:
        *tf = JXL_TRANSFER_BT709_I;
        return JXL_BS_OK;
    case 8:
        *tf = JXL_TRANSFER_LINEAR_I;
        return JXL_BS_OK;
    case 13:
        *tf = JXL_TRANSFER_SRGB_I;
        return JXL_BS_OK;
    case 16:
        *tf = JXL_TRANSFER_PQ_I;
        return JXL_BS_OK;
    case 18:
        *tf = JXL_TRANSFER_HLG_I;
        return JXL_BS_OK;
    default:
        return JXL_BS_VALIDATION_FAILED;
    }
}

jxl_bs_status_t jxl_colour_encoding_parse(jxl_bs *bs, jxl_colour_encoding_parsed *out) {
    int all_default;
    int want_icc;
    uint32_t cs;
    uint32_t ri;
    jxl_bs_status_t st;
    memset(out, 0, sizeof(*out));
    all_default = 0;
    st = jxl_bs_read_bool(bs, &all_default);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (all_default) {
        out->colour_space = JXL_COLOUR_SPACE_RGB_I;
        out->white_point = JXL_WHITE_POINT_D65_I;
        out->primaries = JXL_PRIMARIES_SRGB_I;
        out->transfer = JXL_TRANSFER_SRGB_I;
        out->rendering_intent = JXL_RENDERING_RELATIVE_I;
        return JXL_BS_OK;
    }

    want_icc = 0;
    st = jxl_bs_read_bool(bs, &want_icc);
    if (st != JXL_BS_OK) {
        return st;
    }

    cs = 0;
    st = jxl_bs_read_enum(bs, &cs);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (cs > 3) {
        return JXL_BS_VALIDATION_FAILED;
    }
    out->colour_space = (jxl_colour_space_i)cs;

    if (want_icc) {
        out->have_icc_profile = 1;
        return JXL_BS_OK;
    }

    st = parse_white_point(bs, out->colour_space, &out->white_point, &out->custom_white_x,
                           &out->custom_white_y);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = parse_primaries(bs, out->colour_space, &out->primaries, &out->custom_red_x,
                         &out->custom_red_y, &out->custom_green_x, &out->custom_green_y,
                         &out->custom_blue_x, &out->custom_blue_y);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = parse_transfer_function(bs, &out->transfer, &out->gamma_1e7);
    if (st != JXL_BS_OK) {
        return st;
    }
    ri = 0;
    st = jxl_bs_read_enum(bs, &ri);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (ri > 3) {
        return JXL_BS_VALIDATION_FAILED;
    }
    out->rendering_intent = (jxl_rendering_intent_i)ri;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bit_depth_parse(jxl_bs *bs, uint32_t *bits_per_sample_out) {
    int is_float = 0;
    jxl_bs_status_t st = jxl_bs_read_bool(bs, &is_float);
    uint32_t bits_per_sample;
    const jxl_u32_spec specs[4] = {JXL_U32_C(8), JXL_U32_C(10), JXL_U32_C(12), JXL_U32_BITS(1, 6)};
    if (st != JXL_BS_OK) {
        return st;
    }
    if (is_float) {
        uint32_t bits_per_sample;
        uint32_t exp_bits;
        uint32_t mantissa_bits;
        const jxl_u32_spec specs[4] = {JXL_U32_C(32), JXL_U32_C(16), JXL_U32_C(24),
                                       JXL_U32_BITS(1, 6)};
        bits_per_sample = 0;
        st = jxl_bs_read_u32(bs, specs, &bits_per_sample);
        if (st != JXL_BS_OK) {
            return st;
        }
        exp_bits = 0;
        st = jxl_bs_read_bits(bs, 4, &exp_bits);
        if (st != JXL_BS_OK) {
            return st;
        }
        exp_bits += 1;
        if (exp_bits < 2 || exp_bits > 8) {
            return JXL_BS_VALIDATION_FAILED;
        }
        mantissa_bits = bits_per_sample - exp_bits - 1;
        if (mantissa_bits < 2 || mantissa_bits > 23) {
            return JXL_BS_VALIDATION_FAILED;
        }
        *bits_per_sample_out = bits_per_sample;
        return JXL_BS_OK;
    }

    bits_per_sample = 0;
    st = jxl_bs_read_u32(bs, specs, &bits_per_sample);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (bits_per_sample > 31) {
        return JXL_BS_VALIDATION_FAILED;
    }
    *bits_per_sample_out = bits_per_sample;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_extra_channel_parse(jxl_bs *bs, int *is_alpha_out, int *alpha_associated_out,
                                        uint32_t *bit_depth_out, uint32_t *dim_shift_out) {
    int default_alpha = 0;
    jxl_bs_status_t st = jxl_bs_read_bool(bs, &default_alpha);
    uint32_t ty;
    uint32_t bits;
    uint32_t dim_shift;
    const jxl_u32_spec dim_specs[4] = {JXL_U32_C(0), JXL_U32_C(3), JXL_U32_C(4), JXL_U32_BITS(1, 3)};
    if (st != JXL_BS_OK) {
        return st;
    }
    if (default_alpha) {
        if (is_alpha_out != NULL) {
            *is_alpha_out = 1;
        }
        if (alpha_associated_out != NULL) {
            *alpha_associated_out = 0;
        }
        if (dim_shift_out != NULL) {
            *dim_shift_out = 0;
        }
        if (bit_depth_out != NULL) {
            *bit_depth_out = 8;
        }
        return JXL_BS_OK;
    }

    ty = 0;
    st = jxl_bs_read_enum(bs, &ty);
    if (st != JXL_BS_OK) {
        return st;
    }
    bits = 0;
    st = jxl_bit_depth_parse(bs, &bits);
    if (st != JXL_BS_OK) {
        return st;
    }
    dim_shift = 0;
    st = jxl_bs_read_u32(bs, dim_specs, &dim_shift);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (dim_shift_out != NULL) {
        *dim_shift_out = dim_shift;
    }
    if (bit_depth_out != NULL) {
        *bit_depth_out = bits;
    }
    st = jxl_image_skip_name(bs);
    if (st != JXL_BS_OK) {
        return st;
    }

    switch (ty) {
        int i;
    case 0: {
        int alpha_associated = 0;
        st = jxl_bs_read_bool(bs, &alpha_associated);
        if (st != JXL_BS_OK) {
            return st;
        }
        if (is_alpha_out != NULL) {
            *is_alpha_out = 1;
        }
        if (alpha_associated_out != NULL) {
            *alpha_associated_out = alpha_associated;
        }
        return JXL_BS_OK;
    }
    case 2:
        for (i = 0; i < 4; ++i) {
            float f = 0;
            st = jxl_bs_read_f16_as_f32(bs, &f);
            if (st != JXL_BS_OK) {
                return st;
            }
        }
        return JXL_BS_OK;
    case 5: {
        uint32_t cfa = 0;
        const jxl_u32_spec cfa_specs[4] = {JXL_U32_C(1), JXL_U32_BITS(0, 2), JXL_U32_BITS(3, 4),
                                           JXL_U32_BITS(19, 8)};
        return jxl_bs_read_u32(bs, cfa_specs, &cfa);
    }
    default:
        if (ty > 16) {
            return JXL_BS_VALIDATION_FAILED;
        }
        return JXL_BS_OK;
    }
}

jxl_bs_status_t skip_extra_channel(jxl_bs *bs) {
    return jxl_extra_channel_parse(bs, NULL, NULL, NULL, NULL);
}

jxl_bs_status_t jxl_extensions_parse(jxl_bs *bs) {
    int i;
    uint64_t extension_bits = 0;
    jxl_bs_status_t st = jxl_bs_read_u64(bs, &extension_bits);
    if (st != JXL_BS_OK) {
        return st;
    }
    for (i = 0; i < 64; ++i) {
        if ((extension_bits >> i) & 1) {
            uint64_t len = 0;
            st = jxl_bs_read_u64(bs, &len);
            if (st != JXL_BS_OK) {
                return st;
            }
            st = jxl_bs_skip_bits(bs, (size_t)len);
            if (st != JXL_BS_OK) {
                return st;
            }
        }
    }
    return JXL_BS_OK;
}

jxl_bs_status_t skip_tone_mapping(jxl_bs *bs) {
    int all_default = 0;
    int relative_to_max_display = 0;
    float scratch = 0.0f;
    jxl_bs_status_t st = jxl_bs_read_bool(bs, &all_default);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (all_default) {
        return JXL_BS_OK;
    }
    st = jxl_bs_read_f16_as_f32(bs, &scratch);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = jxl_bs_read_f16_as_f32(bs, &scratch);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = jxl_bs_read_bool(bs, &relative_to_max_display);
    if (st != JXL_BS_OK) {
        return st;
    }
    (void)relative_to_max_display;
    return jxl_bs_read_f16_as_f32(bs, &scratch);
}

jxl_bs_status_t skip_f16_array(jxl_bs *bs, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        float f = 0;
        jxl_bs_status_t st = jxl_bs_read_f16_as_f32(bs, &f);
        if (st != JXL_BS_OK) {
            return st;
        }
    }
    return JXL_BS_OK;
}

void jxl_opsin_inverse_set_defaults(jxl_opsin_inverse_parsed *out) {
    static const float k_inv_mat[3][3] = {
        {11.031566901960783f, -9.866943921568629f, -0.16462299647058826f},
        {-3.254147380392157f, 4.418770392156863f, -0.16462299647058826f},
        {-3.6588512862745097f, 2.7129230470588235f, 1.9459282392156863f},
    };
    if (out == NULL) {
        return;
    }
    memcpy(out->inv_mat, k_inv_mat, sizeof(k_inv_mat));
    out->opsin_bias[0] = -0.0037930732552754493f;
    out->opsin_bias[1] = -0.0037930732552754493f;
    out->opsin_bias[2] = -0.0037930732552754493f;
    out->quant_bias[0] = 1.0f - 0.05465007330715401f;
    out->quant_bias[1] = 1.0f - 0.07005449891748593f;
    out->quant_bias[2] = 1.0f - 0.049935103337343655f;
    out->quant_bias_numerator = 0.145f;
}

jxl_bs_status_t jxl_opsin_inverse_parse(jxl_bs *bs, jxl_opsin_inverse_parsed *out) {
    int r;
    int i;
    int all_default;
    jxl_bs_status_t st;
    if (bs == NULL || out == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    all_default = 0;
    st = jxl_bs_read_bool(bs, &all_default);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (all_default) {
        jxl_opsin_inverse_set_defaults(out);
        return JXL_BS_OK;
    }
    for (r = 0; r < 3; ++r) {
        int c;
        for (c = 0; c < 3; ++c) {
            st = jxl_bs_read_f16_as_f32(bs, &out->inv_mat[r][c]);
            if (st != JXL_BS_OK) {
                return st;
            }
        }
    }
    for (i = 0; i < 3; ++i) {
        st = jxl_bs_read_f16_as_f32(bs, &out->opsin_bias[i]);
        if (st != JXL_BS_OK) {
            return st;
        }
    }
    for (i = 0; i < 3; ++i) {
        st = jxl_bs_read_f16_as_f32(bs, &out->quant_bias[i]);
        if (st != JXL_BS_OK) {
            return st;
        }
    }
    return jxl_bs_read_f16_as_f32(bs, &out->quant_bias_numerator);
}

jxl_bs_status_t skip_opsin_inverse(jxl_bs *bs) {
    int r;
    int all_default = 0;
    float scratch = 0.0f;
    jxl_bs_status_t st = jxl_bs_read_bool(bs, &all_default);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (all_default) {
        return JXL_BS_OK;
    }
    for (r = 0; r < 3; ++r) {
        int c;
        for (c = 0; c < 3; ++c) {
            st = jxl_bs_read_f16_as_f32(bs, &scratch);
            if (st != JXL_BS_OK) {
                return st;
            }
        }
    }
    st = skip_f16_array(bs, 3);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = skip_f16_array(bs, 3);
    if (st != JXL_BS_OK) {
        return st;
    }
    return jxl_bs_read_f16_as_f32(bs, &scratch);
}

jxl_bs_status_t skip_animation_header(jxl_bs *bs, int *have_timecodes_out) {
    uint32_t v;
    int have_timecodes;
    const jxl_u32_spec tps_num[4] = {JXL_U32_C(100), JXL_U32_C(1000), JXL_U32_BITS(1, 10),
                                     JXL_U32_BITS(1, 30)};
    const jxl_u32_spec tps_den[4] = {JXL_U32_C(1), JXL_U32_C(1001), JXL_U32_BITS(1, 8),
                                     JXL_U32_BITS(1, 10)};
    const jxl_u32_spec loops[4] = {JXL_U32_C(0), JXL_U32_BITS(0, 3), JXL_U32_BITS(0, 16),
                                   JXL_U32_BITS(0, 32)};
    jxl_bs_status_t st;
    if (have_timecodes_out != NULL) {
        *have_timecodes_out = 0;
    }
    v = 0;
    st = jxl_bs_read_u32(bs, tps_num, &v);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = jxl_bs_read_u32(bs, tps_den, &v);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = jxl_bs_read_u32(bs, loops, &v);
    if (st != JXL_BS_OK) {
        return st;
    }
    have_timecodes = 0;
    st = jxl_bs_read_bool(bs, &have_timecodes);
    if (st == JXL_BS_OK && have_timecodes_out != NULL) {
        *have_timecodes_out = have_timecodes;
    }
    return st;
}
