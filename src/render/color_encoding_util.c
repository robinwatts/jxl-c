// SPDX-License-Identifier: MIT OR Apache-2.0
#include "color_encoding_util.h"

#include <string.h>

void jxl_color_encoding_default_srgb(jxl_color_encoding *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->colour_space = JXL_COLOUR_SPACE_RGB;
    out->white_point = JXL_WHITE_POINT_D65;
    out->primaries = JXL_PRIMARIES_SRGB;
    out->transfer = JXL_TRANSFER_SRGB;
    out->rendering_intent = JXL_RENDERING_RELATIVE;
}

jxl_status_t jxl_colour_encoding_parsed_to_public(const jxl_colour_encoding_parsed *in,
                                                jxl_color_encoding *out) {
    if (in == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    switch (in->colour_space) {
    case JXL_COLOUR_SPACE_RGB_I:
        out->colour_space = JXL_COLOUR_SPACE_RGB;
        break;
    case JXL_COLOUR_SPACE_GRAY_I:
        out->colour_space = JXL_COLOUR_SPACE_GRAY;
        break;
    case JXL_COLOUR_SPACE_XYB_I:
        out->colour_space = JXL_COLOUR_SPACE_XYB;
        break;
    default:
        out->colour_space = JXL_COLOUR_SPACE_UNKNOWN;
        break;
    }
    switch (in->white_point) {
    case JXL_WHITE_POINT_D65_I:
        out->white_point = JXL_WHITE_POINT_D65;
        break;
    case JXL_WHITE_POINT_CUSTOM_I:
        out->white_point = JXL_WHITE_POINT_CUSTOM;
        break;
    default:
        out->white_point = JXL_WHITE_POINT_UNKNOWN;
        break;
    }
    switch (in->primaries) {
    case JXL_PRIMARIES_SRGB_I:
        out->primaries = JXL_PRIMARIES_SRGB;
        break;
    case JXL_PRIMARIES_CUSTOM_I:
        out->primaries = JXL_PRIMARIES_CUSTOM;
        break;
    default:
        out->primaries = JXL_PRIMARIES_UNKNOWN;
        break;
    }
    switch (in->transfer) {
    case JXL_TRANSFER_LINEAR_I:
        out->transfer = JXL_TRANSFER_LINEAR;
        break;
    case JXL_TRANSFER_SRGB_I:
        out->transfer = JXL_TRANSFER_SRGB;
        break;
    default:
        out->transfer = JXL_TRANSFER_UNKNOWN;
        break;
    }
    switch (in->rendering_intent) {
    case JXL_RENDERING_PERCEPTUAL_I:
        out->rendering_intent = JXL_RENDERING_PERCEPTUAL;
        break;
    case JXL_RENDERING_RELATIVE_I:
        out->rendering_intent = JXL_RENDERING_RELATIVE;
        break;
    case JXL_RENDERING_SATURATION_I:
        out->rendering_intent = JXL_RENDERING_SATURATION;
        break;
    case JXL_RENDERING_ABSOLUTE_I:
        out->rendering_intent = JXL_RENDERING_ABSOLUTE;
        break;
    default:
        out->rendering_intent = JXL_RENDERING_UNKNOWN;
        break;
    }
    return JXL_OK;
}

int jxl_colour_encoding_is_d65_srgb_fast_path(const jxl_colour_encoding_parsed *enc) {
    if (enc == NULL) {
        return 0;
    }
    if (enc->have_icc_profile) {
        return 0;
    }
    if (enc->colour_space != JXL_COLOUR_SPACE_RGB_I &&
        enc->colour_space != JXL_COLOUR_SPACE_GRAY_I) {
        return 0;
    }
    if (enc->white_point != JXL_WHITE_POINT_D65_I) {
        return 0;
    }
    if (enc->primaries != JXL_PRIMARIES_SRGB_I) {
        return 0;
    }
    if (enc->transfer != JXL_TRANSFER_SRGB_I && enc->transfer != JXL_TRANSFER_LINEAR_I &&
        enc->transfer != JXL_TRANSFER_BT709_I) {
        return 0;
    }
    return 1;
}

int jxl_colour_encoding_parsed_equivalent(const jxl_colour_encoding_parsed *a,
                                          const jxl_colour_encoding_parsed *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->have_icc_profile != b->have_icc_profile) {
        return 0;
    }
    if (a->have_icc_profile) {
        return 0;
    }
    if (a->colour_space != b->colour_space) {
        return 0;
    }
    if (a->colour_space == JXL_COLOUR_SPACE_XYB_I) {
        return 1;
    }
    if (a->rendering_intent != b->rendering_intent || a->white_point != b->white_point ||
        a->transfer != b->transfer) {
        return 0;
    }
    if (a->colour_space == JXL_COLOUR_SPACE_GRAY_I) {
        return 1;
    }
    return a->primaries == b->primaries;
}

jxl_status_t jxl_color_encoding_to_parsed(const jxl_color_encoding *in,
                                          jxl_colour_encoding_parsed *out) {
    if (in == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    switch (in->colour_space) {
    case JXL_COLOUR_SPACE_RGB:
        out->colour_space = JXL_COLOUR_SPACE_RGB_I;
        break;
    case JXL_COLOUR_SPACE_GRAY:
        out->colour_space = JXL_COLOUR_SPACE_GRAY_I;
        break;
    case JXL_COLOUR_SPACE_XYB:
        out->colour_space = JXL_COLOUR_SPACE_XYB_I;
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->white_point) {
    case JXL_WHITE_POINT_D65:
        out->white_point = JXL_WHITE_POINT_D65_I;
        break;
    case JXL_WHITE_POINT_CUSTOM:
        out->white_point = JXL_WHITE_POINT_CUSTOM_I;
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->primaries) {
    case JXL_PRIMARIES_SRGB:
        out->primaries = JXL_PRIMARIES_SRGB_I;
        break;
    case JXL_PRIMARIES_CUSTOM:
        out->primaries = JXL_PRIMARIES_CUSTOM_I;
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->transfer) {
    case JXL_TRANSFER_LINEAR:
        out->transfer = JXL_TRANSFER_LINEAR_I;
        break;
    case JXL_TRANSFER_SRGB:
        out->transfer = JXL_TRANSFER_SRGB_I;
        break;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
    switch (in->rendering_intent) {
    case JXL_RENDERING_PERCEPTUAL:
        out->rendering_intent = JXL_RENDERING_PERCEPTUAL_I;
        break;
    case JXL_RENDERING_RELATIVE:
        out->rendering_intent = JXL_RENDERING_RELATIVE_I;
        break;
    case JXL_RENDERING_SATURATION:
        out->rendering_intent = JXL_RENDERING_SATURATION_I;
        break;
    case JXL_RENDERING_ABSOLUTE:
        out->rendering_intent = JXL_RENDERING_ABSOLUTE_I;
        break;
    default:
        out->rendering_intent = JXL_RENDERING_RELATIVE_I;
        break;
    }
    return JXL_OK;
}
