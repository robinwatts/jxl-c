// SPDX-License-Identifier: MIT OR Apache-2.0
#include "color_encoding_util.h"

#include <string.h>

/* Custom xy in the parsed header is stored as fixed-point × 1e6. */
static double jxl_customxy_to_double(int32_t v) {
    return (double)v / 1000000.0;
}

static void jxl_fill_known_white_xy(jxl_color_encoding *out) {
    switch (out->white_point) {
    case JXL_WHITE_POINT_D65:
        out->white_point_xy[0] = 0.3127;
        out->white_point_xy[1] = 0.3290;
        break;
    case JXL_WHITE_POINT_E:
        out->white_point_xy[0] = 1.0 / 3.0;
        out->white_point_xy[1] = 1.0 / 3.0;
        break;
    case JXL_WHITE_POINT_DCI:
        out->white_point_xy[0] = 0.314;
        out->white_point_xy[1] = 0.351;
        break;
    default:
        break;
    }
}

static void jxl_fill_known_primaries_xy(jxl_color_encoding *out) {
    switch (out->primaries) {
    case JXL_PRIMARIES_SRGB:
        out->primaries_red_xy[0] = 0.639998686;
        out->primaries_red_xy[1] = 0.330010138;
        out->primaries_green_xy[0] = 0.300003784;
        out->primaries_green_xy[1] = 0.600003357;
        out->primaries_blue_xy[0] = 0.150002046;
        out->primaries_blue_xy[1] = 0.059997204;
        break;
    case JXL_PRIMARIES_P3:
        out->primaries_red_xy[0] = 0.680;
        out->primaries_red_xy[1] = 0.320;
        out->primaries_green_xy[0] = 0.265;
        out->primaries_green_xy[1] = 0.690;
        out->primaries_blue_xy[0] = 0.150;
        out->primaries_blue_xy[1] = 0.060;
        break;
    case JXL_PRIMARIES_2100:
        out->primaries_red_xy[0] = 0.708;
        out->primaries_red_xy[1] = 0.292;
        out->primaries_green_xy[0] = 0.170;
        out->primaries_green_xy[1] = 0.797;
        out->primaries_blue_xy[0] = 0.131;
        out->primaries_blue_xy[1] = 0.046;
        break;
    default:
        break;
    }
}

void jxl_color_encoding_default_srgb(jxl_color_encoding *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->color_space = JXL_COLOR_SPACE_RGB;
    out->white_point = JXL_WHITE_POINT_D65;
    out->primaries = JXL_PRIMARIES_SRGB;
    out->transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
    out->rendering_intent = JXL_RENDERING_INTENT_RELATIVE;
    jxl_fill_known_white_xy(out);
    jxl_fill_known_primaries_xy(out);
}

jxl_status_t jxl_color_encoding_parsed_to_public(const jxl_color_encoding_parsed *in,
                                                jxl_color_encoding *out) {
    if (in == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    switch (in->color_space) {
    case JXL_COLOR_SPACE_RGB_I:
        out->color_space = JXL_COLOR_SPACE_RGB;
        break;
    case JXL_COLOR_SPACE_GRAY_I:
        out->color_space = JXL_COLOR_SPACE_GRAY;
        break;
    case JXL_COLOR_SPACE_XYB_I:
        out->color_space = JXL_COLOR_SPACE_XYB;
        break;
    default:
        out->color_space = JXL_COLOR_SPACE_UNKNOWN;
        break;
    }
    switch (in->white_point) {
    case JXL_COLOR_WHITE_POINT_D65_I:
        out->white_point = JXL_WHITE_POINT_D65;
        break;
    case JXL_WHITE_POINT_DCI_I:
        out->white_point = JXL_WHITE_POINT_DCI;
        break;
    case JXL_WHITE_POINT_E_I:
        out->white_point = JXL_WHITE_POINT_E;
        break;
    case JXL_COLOR_WHITE_POINT_CUSTOM_I:
        out->white_point = JXL_WHITE_POINT_CUSTOM;
        out->white_point_xy[0] = jxl_customxy_to_double(in->custom_white_x);
        out->white_point_xy[1] = jxl_customxy_to_double(in->custom_white_y);
        break;
    default:
        out->white_point = JXL_WHITE_POINT_D65;
        break;
    }
    if (out->white_point != JXL_WHITE_POINT_CUSTOM) {
        jxl_fill_known_white_xy(out);
    }
    switch (in->primaries) {
    case JXL_COLOR_PRIMARIES_SRGB_I:
        out->primaries = JXL_PRIMARIES_SRGB;
        break;
    case JXL_PRIMARIES_P3_I:
        out->primaries = JXL_PRIMARIES_P3;
        break;
    case JXL_PRIMARIES_BT2100_I:
        out->primaries = JXL_PRIMARIES_2100;
        break;
    case JXL_COLOR_PRIMARIES_CUSTOM_I:
        out->primaries = JXL_PRIMARIES_CUSTOM;
        out->primaries_red_xy[0] = jxl_customxy_to_double(in->custom_red_x);
        out->primaries_red_xy[1] = jxl_customxy_to_double(in->custom_red_y);
        out->primaries_green_xy[0] = jxl_customxy_to_double(in->custom_green_x);
        out->primaries_green_xy[1] = jxl_customxy_to_double(in->custom_green_y);
        out->primaries_blue_xy[0] = jxl_customxy_to_double(in->custom_blue_x);
        out->primaries_blue_xy[1] = jxl_customxy_to_double(in->custom_blue_y);
        break;
    default:
        out->primaries = JXL_PRIMARIES_SRGB;
        break;
    }
    if (out->primaries != JXL_PRIMARIES_CUSTOM) {
        jxl_fill_known_primaries_xy(out);
    }
    switch (in->transfer) {
    case JXL_TRANSFER_LINEAR_I:
        out->transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
        break;
    case JXL_TRANSFER_SRGB_I:
        out->transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
        break;
    case JXL_TRANSFER_BT709_I:
        out->transfer_function = JXL_TRANSFER_FUNCTION_709;
        break;
    case JXL_TRANSFER_PQ_I:
        out->transfer_function = JXL_TRANSFER_FUNCTION_PQ;
        break;
    case JXL_TRANSFER_HLG_I:
        out->transfer_function = JXL_TRANSFER_FUNCTION_HLG;
        break;
    case JXL_TRANSFER_GAMMA_I:
        out->transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
        out->gamma = (double)in->gamma_1e7 / 10000000.0;
        break;
    default:
        out->transfer_function = JXL_TRANSFER_FUNCTION_UNKNOWN;
        break;
    }
    switch (in->rendering_intent) {
    case JXL_RENDERING_PERCEPTUAL_I:
        out->rendering_intent = JXL_RENDERING_INTENT_PERCEPTUAL;
        break;
    case JXL_RENDERING_RELATIVE_I:
        out->rendering_intent = JXL_RENDERING_INTENT_RELATIVE;
        break;
    case JXL_RENDERING_SATURATION_I:
        out->rendering_intent = JXL_RENDERING_INTENT_SATURATION;
        break;
    case JXL_RENDERING_ABSOLUTE_I:
        out->rendering_intent = JXL_RENDERING_INTENT_ABSOLUTE;
        break;
    default:
        out->rendering_intent = JXL_RENDERING_INTENT_RELATIVE;
        break;
    }
    return JXL_OK;
}

int jxl_color_encoding_is_d65_srgb_fast_path(const jxl_color_encoding_parsed *enc) {
    if (enc == NULL) {
        return 0;
    }
    if (enc->have_icc_profile) {
        return 0;
    }
    if (enc->color_space != JXL_COLOR_SPACE_RGB_I &&
        enc->color_space != JXL_COLOR_SPACE_GRAY_I) {
        return 0;
    }
    if (enc->white_point != JXL_COLOR_WHITE_POINT_D65_I) {
        return 0;
    }
    if (enc->primaries != JXL_COLOR_PRIMARIES_SRGB_I) {
        return 0;
    }
    if (enc->transfer != JXL_TRANSFER_SRGB_I && enc->transfer != JXL_TRANSFER_LINEAR_I &&
        enc->transfer != JXL_TRANSFER_BT709_I) {
        return 0;
    }
    return 1;
}

int jxl_color_encoding_parsed_equivalent(const jxl_color_encoding_parsed *a,
                                          const jxl_color_encoding_parsed *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->have_icc_profile != b->have_icc_profile) {
        return 0;
    }
    if (a->have_icc_profile) {
        return 0;
    }
    if (a->color_space != b->color_space) {
        return 0;
    }
    if (a->color_space == JXL_COLOR_SPACE_XYB_I) {
        return 1;
    }
    if (a->rendering_intent != b->rendering_intent || a->white_point != b->white_point ||
        a->transfer != b->transfer) {
        return 0;
    }
    if (a->color_space == JXL_COLOR_SPACE_GRAY_I) {
        return 1;
    }
    return a->primaries == b->primaries;
}

jxl_status_t jxl_color_encoding_to_parsed(const jxl_color_encoding *in,
                                          jxl_color_encoding_parsed *out) {
    if (in == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    switch (in->color_space) {
    case JXL_COLOR_SPACE_RGB:
        out->color_space = JXL_COLOR_SPACE_RGB_I;
        break;
    case JXL_COLOR_SPACE_GRAY:
        out->color_space = JXL_COLOR_SPACE_GRAY_I;
        break;
    case JXL_COLOR_SPACE_XYB:
        out->color_space = JXL_COLOR_SPACE_XYB_I;
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->white_point) {
    case JXL_WHITE_POINT_D65:
        out->white_point = JXL_COLOR_WHITE_POINT_D65_I;
        break;
    case JXL_WHITE_POINT_DCI:
        out->white_point = JXL_WHITE_POINT_DCI_I;
        break;
    case JXL_WHITE_POINT_E:
        out->white_point = JXL_WHITE_POINT_E_I;
        break;
    case JXL_WHITE_POINT_CUSTOM:
        out->white_point = JXL_COLOR_WHITE_POINT_CUSTOM_I;
        out->custom_white_x = (int32_t)(in->white_point_xy[0] * 1000000.0);
        out->custom_white_y = (int32_t)(in->white_point_xy[1] * 1000000.0);
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->primaries) {
    case JXL_PRIMARIES_SRGB:
        out->primaries = JXL_COLOR_PRIMARIES_SRGB_I;
        break;
    case JXL_PRIMARIES_P3:
        out->primaries = JXL_PRIMARIES_P3_I;
        break;
    case JXL_PRIMARIES_2100:
        out->primaries = JXL_PRIMARIES_BT2100_I;
        break;
    case JXL_PRIMARIES_CUSTOM:
        out->primaries = JXL_COLOR_PRIMARIES_CUSTOM_I;
        out->custom_red_x = (int32_t)(in->primaries_red_xy[0] * 1000000.0);
        out->custom_red_y = (int32_t)(in->primaries_red_xy[1] * 1000000.0);
        out->custom_green_x = (int32_t)(in->primaries_green_xy[0] * 1000000.0);
        out->custom_green_y = (int32_t)(in->primaries_green_xy[1] * 1000000.0);
        out->custom_blue_x = (int32_t)(in->primaries_blue_xy[0] * 1000000.0);
        out->custom_blue_y = (int32_t)(in->primaries_blue_xy[1] * 1000000.0);
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->transfer_function) {
    case JXL_TRANSFER_FUNCTION_LINEAR:
        out->transfer = JXL_TRANSFER_LINEAR_I;
        break;
    case JXL_TRANSFER_FUNCTION_SRGB:
        out->transfer = JXL_TRANSFER_SRGB_I;
        break;
    case JXL_TRANSFER_FUNCTION_709:
        out->transfer = JXL_TRANSFER_BT709_I;
        break;
    case JXL_TRANSFER_FUNCTION_PQ:
        out->transfer = JXL_TRANSFER_PQ_I;
        break;
    case JXL_TRANSFER_FUNCTION_HLG:
        out->transfer = JXL_TRANSFER_HLG_I;
        break;
    case JXL_TRANSFER_FUNCTION_DCI:
        /* Closest internal match; DCI TF is not a separate bitstream enum. */
        out->transfer = JXL_TRANSFER_GAMMA_I;
        out->gamma_1e7 = 26000000; /* ~2.6 */
        break;
    case JXL_TRANSFER_FUNCTION_GAMMA:
        out->transfer = JXL_TRANSFER_GAMMA_I;
        out->gamma_1e7 = (uint32_t)(in->gamma * 10000000.0 + 0.5);
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->rendering_intent) {
    case JXL_RENDERING_INTENT_PERCEPTUAL:
        out->rendering_intent = JXL_RENDERING_PERCEPTUAL_I;
        break;
    case JXL_RENDERING_INTENT_RELATIVE:
        out->rendering_intent = JXL_RENDERING_RELATIVE_I;
        break;
    case JXL_RENDERING_INTENT_SATURATION:
        out->rendering_intent = JXL_RENDERING_SATURATION_I;
        break;
    case JXL_RENDERING_INTENT_ABSOLUTE:
        out->rendering_intent = JXL_RENDERING_ABSOLUTE_I;
        break;
    default:
        out->rendering_intent = JXL_RENDERING_RELATIVE_I;
        break;
    }
    return JXL_OK;
}
