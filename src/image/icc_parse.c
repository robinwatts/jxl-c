// SPDX-License-Identifier: MIT OR Apache-2.0
#include "icc_parse.h"

#include "jxl_oxide/jxl_status.h"
#include "jxl_oxide/jxl_types.h"

#include <string.h>

static int icc_tag_data(const uint8_t *icc, size_t len, const char tag[4], const uint8_t **out,
                        uint32_t *out_len) {
                            uint32_t i;
    uint32_t size;
    uint32_t tag_count;
    size_t tags_end;
    if (icc == NULL || len < 132 || out == NULL || out_len == NULL) {
        return 0;
    }
    size = ((uint32_t)icc[0] << 24) | ((uint32_t)icc[1] << 16) | ((uint32_t)icc[2] << 8) |
                    (uint32_t)icc[3];
    if (size > len) {
        return 0;
    }
    tag_count =
        ((uint32_t)icc[0x80] << 24) | ((uint32_t)icc[0x81] << 16) | ((uint32_t)icc[0x82] << 8) |
        (uint32_t)icc[0x83];
    tags_end = 0x84u + (size_t)tag_count * 12u;
    if (tags_end > len) {
        return 0;
    }
    for (i = 0; i < tag_count; ++i) {
        const uint8_t *entry = icc + 0x84u + (size_t)i * 12u;
        uint32_t offset;
        uint32_t tag_len;
        if (memcmp(entry, tag, 4) != 0) {
            continue;
        }
        offset = ((uint32_t)entry[4] << 24) | ((uint32_t)entry[5] << 16) |
                          ((uint32_t)entry[6] << 8) | (uint32_t)entry[7];
        tag_len = ((uint32_t)entry[8] << 24) | ((uint32_t)entry[9] << 16) |
                           ((uint32_t)entry[10] << 8) | (uint32_t)entry[11];
        if ((uint64_t)offset + tag_len > len) {
            return 0;
        }
        *out = icc + offset;
        *out_len = tag_len;
        return 1;
    }
    return 0;
}

static int icc_trc_is_linear(const uint8_t *data, uint32_t len) {
    if (data == NULL || len < 12) {
        return 0;
    }
    if (memcmp(data, "para", 4) == 0) {
        uint16_t ty;
        uint32_t p0;
	if (len < 12 + 4u) {
            return 0;
        }
        ty = (uint16_t)((data[8] << 8) | data[9]);
        if (ty != 3) {
            return 0;
        }
        p0 = ((uint32_t)data[12] << 24) | ((uint32_t)data[13] << 16) |
                      ((uint32_t)data[14] << 8) | (uint32_t)data[15];
        return p0 == 65536u;
    }
    if (memcmp(data, "curv", 4) == 0) {
        uint32_t count = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
                         ((uint32_t)data[10] << 8) | (uint32_t)data[11];
        return count == 0;
    }
    return 0;
}

static int icc_trc_is_srgb(const uint8_t *data, uint32_t len) {
    if (data == NULL || len < 12) {
        return 0;
    }
    if (memcmp(data, "para", 4) == 0) {
        uint16_t ty;
        uint32_t p0;
        uint32_t p2;
        uint32_t p4;
        if (len < 12) {
            return 0;
        }
        ty = (uint16_t)((data[8] << 8) | data[9]);
        if (ty != 3) {
            return 0;
        }
        if (len < 12 + 5u * 4u) {
            return 0;
        }
        p0 = ((uint32_t)data[12] << 24) | ((uint32_t)data[13] << 16) |
                      ((uint32_t)data[14] << 8) | (uint32_t)data[15];
        p2 = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
                      ((uint32_t)data[22] << 8) | (uint32_t)data[23];
        p4 = ((uint32_t)data[28] << 24) | ((uint32_t)data[29] << 16) |
                      ((uint32_t)data[30] << 8) | (uint32_t)data[31];
        return p0 >= 157200 && p0 <= 157400 && p2 >= 3350 && p2 <= 3500 && p4 >= 2600 &&
               p4 <= 2700;
    }
    if (memcmp(data, "curv", 4) == 0) {
        uint32_t count = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
                         ((uint32_t)data[10] << 8) | (uint32_t)data[11];
        return count == 0;
    }
    return 0;
}

int jxl_icc_maps_to_linear_display(const uint8_t *icc, size_t len) {
    uint32_t trc_len;
    const uint8_t *trc;
    if (icc == NULL || len < 128) {
        return 0;
    }
    if (memcmp(icc + 0x10, "RGB ", 4) != 0) {
        return 0;
    }

    trc = NULL;
    trc_len = 0;
    if (!icc_tag_data(icc, len, "rTRC", &trc, &trc_len) || !icc_trc_is_linear(trc, trc_len)) {
        return 0;
    }
    if (!icc_tag_data(icc, len, "gTRC", &trc, &trc_len) || !icc_trc_is_linear(trc, trc_len)) {
        return 0;
    }
    if (!icc_tag_data(icc, len, "bTRC", &trc, &trc_len) || !icc_trc_is_linear(trc, trc_len)) {
        return 0;
    }
    return 1;
}

int jxl_icc_maps_to_srgb_display(const uint8_t *icc, size_t len) {
    uint32_t trc_len;
    const uint8_t *trc;
    if (icc == NULL || len < 128) {
        return 0;
    }
    if (memcmp(icc + 0x10, "RGB ", 4) != 0) {
        return 0;
    }

    trc = NULL;
    trc_len = 0;
    if (!icc_tag_data(icc, len, "rTRC", &trc, &trc_len) || !icc_trc_is_srgb(trc, trc_len)) {
        return 0;
    }
    if (!icc_tag_data(icc, len, "gTRC", &trc, &trc_len) || !icc_trc_is_srgb(trc, trc_len)) {
        return 0;
    }
    if (!icc_tag_data(icc, len, "bTRC", &trc, &trc_len) || !icc_trc_is_srgb(trc, trc_len)) {
        return 0;
    }
    return 1;
}

static jxl_rendering_intent_t icc_header_rendering_intent(const uint8_t *icc, size_t len) {
    if (icc == NULL || len < 0x44) {
        return JXL_RENDERING_RELATIVE;
    }
    switch (icc[0x43]) {
    case 0:
        return JXL_RENDERING_PERCEPTUAL;
    case 1:
        return JXL_RENDERING_RELATIVE;
    case 2:
        return JXL_RENDERING_SATURATION;
    case 3:
        return JXL_RENDERING_ABSOLUTE;
    default:
        return JXL_RENDERING_RELATIVE;
    }
}

static jxl_status_t fill_rgb_encoding(jxl_color_encoding *out, jxl_transfer_function_t tf,
                                    jxl_rendering_intent_t ri) {
    if (out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    out->colour_space = JXL_COLOUR_SPACE_RGB;
    out->white_point = JXL_WHITE_POINT_D65;
    out->primaries = JXL_PRIMARIES_SRGB;
    out->transfer = tf;
    out->rendering_intent = ri;
    return JXL_OK;
}

static jxl_status_t fill_gray_encoding(jxl_color_encoding *out, jxl_transfer_function_t tf,
                                       jxl_rendering_intent_t ri) {
    if (out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    out->colour_space = JXL_COLOUR_SPACE_GRAY;
    out->white_point = JXL_WHITE_POINT_D65;
    out->primaries = JXL_PRIMARIES_SRGB;
    out->transfer = tf;
    out->rendering_intent = ri;
    return JXL_OK;
}

jxl_status_t jxl_icc_parse_color_encoding(const uint8_t *icc, size_t len, jxl_color_encoding *out) {
    jxl_rendering_intent_t ri;
    if (icc == NULL || len < 128 || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    ri = icc_header_rendering_intent(icc, len);

    if (jxl_icc_maps_to_srgb_display(icc, len)) {
        return fill_rgb_encoding(out, JXL_TRANSFER_SRGB, ri);
    }
    if (jxl_icc_maps_to_linear_display(icc, len)) {
        return fill_rgb_encoding(out, JXL_TRANSFER_LINEAR, ri);
    }

    if (memcmp(icc + 0x10, "GRAY", 4) == 0) {
        uint32_t trc_len;
        const uint8_t *trc = NULL;
        trc_len = 0;
        if (icc_tag_data(icc, len, "kTRC", &trc, &trc_len) ||
            icc_tag_data(icc, len, "rTRC", &trc, &trc_len)) {
            if (icc_trc_is_srgb(trc, trc_len)) {
                return fill_gray_encoding(out, JXL_TRANSFER_SRGB, ri);
            }
            if (icc_trc_is_linear(trc, trc_len)) {
                return fill_gray_encoding(out, JXL_TRANSFER_LINEAR, ri);
            }
        }
    }

    return JXL_ERROR_UNSUPPORTED;
}
