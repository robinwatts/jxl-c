// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image_internal.h"

uint32_t jxl_size_header_default_width(uint32_t ratio, uint32_t w_div8, uint32_t height) {
    uint64_t h = height;
    uint64_t res;
    switch (ratio) {
    case 0:
        res = 8ull * w_div8;
        break;
    case 1:
        res = h;
        break;
    case 2:
        res = h * 12 / 10;
        break;
    case 3:
        res = h * 4 / 3;
        break;
    case 4:
        res = h * 3 / 2;
        break;
    case 5:
        res = h * 16 / 9;
        break;
    case 6:
        res = h * 5 / 4;
        break;
    case 7:
        res = h * 2;
        break;
    default:
        return 0;
    }
    return (uint32_t)res;
}

static jxl_bs_status_t read_height(jxl_bs *bs, int div8, int preview, uint32_t h_div8_in,
                                   uint32_t *height_out) {
    if (div8) {
        if (preview) {
            const jxl_u32_spec specs[4] = {JXL_U32_C(16), JXL_U32_C(32), JXL_U32_BITS(1, 5),
                                           JXL_U32_BITS(33, 9)};
            uint32_t h_div8 = 1;
            jxl_bs_status_t st = jxl_bs_read_u32(bs, specs, &h_div8);
            if (st != JXL_BS_OK) {
                return st;
            }
            *height_out = 8 * h_div8;
        } else {
            *height_out = 8 * h_div8_in;
        }
        return JXL_BS_OK;
    }
    if (preview) {
        const jxl_u32_spec specs[4] = {JXL_U32_BITS(1, 6), JXL_U32_BITS(65, 8), JXL_U32_BITS(321, 10),
                                       JXL_U32_BITS(1345, 12)};
        return jxl_bs_read_u32(bs, specs, height_out);
    }
    {
        const jxl_u32_spec specs[4] = {JXL_U32_BITS(1, 9), JXL_U32_BITS(1, 13), JXL_U32_BITS(1, 18),
                                       JXL_U32_BITS(1, 30)};
        return jxl_bs_read_u32(bs, specs, height_out);
    }
}

static jxl_bs_status_t read_width(jxl_bs *bs, int div8, int preview, uint32_t ratio,
                                  uint32_t w_div8_in, uint32_t height, uint32_t *width_out) {
    if (div8 && ratio == 0) {
        if (preview) {
            const jxl_u32_spec specs[4] = {JXL_U32_C(16), JXL_U32_C(32), JXL_U32_BITS(1, 5),
                                           JXL_U32_BITS(33, 9)};
            return jxl_bs_read_u32(bs, specs, width_out);
        }
        *width_out = 8 * w_div8_in;
        return JXL_BS_OK;
    }
    if (!div8 && ratio == 0) {
        if (preview) {
            const jxl_u32_spec specs[4] = {JXL_U32_BITS(1, 6), JXL_U32_BITS(65, 8), JXL_U32_BITS(321, 10),
                                           JXL_U32_BITS(1345, 12)};
            return jxl_bs_read_u32(bs, specs, width_out);
        }
        {
            const jxl_u32_spec specs[4] = {JXL_U32_BITS(1, 9), JXL_U32_BITS(1, 13), JXL_U32_BITS(1, 18),
                                           JXL_U32_BITS(1, 30)};
            return jxl_bs_read_u32(bs, specs, width_out);
        }
    }
    *width_out = jxl_size_header_default_width(ratio, w_div8_in, height);
    return JXL_BS_OK;
}

static jxl_bs_status_t size_header_parse_inner(jxl_bs *bs, jxl_size_header *out, int preview) {
    int div8 = 0;
    uint32_t h_div8 = preview ? 1 : 0;
    uint32_t w_div8 = preview ? 1 : 0;
    uint32_t height = 0;
    uint32_t ratio = 0;
    jxl_bs_status_t st;

    st = jxl_bs_read_bool(bs, &div8);
    if (st != JXL_BS_OK) {
        return st;
    }

    if (div8 && !preview) {
        uint32_t bits = 0;
        st = jxl_bs_read_bits(bs, 5, &bits);
        if (st != JXL_BS_OK) {
            return st;
        }
        h_div8 = bits + 1;
    }

    st = read_height(bs, div8, preview, h_div8, &height);
    if (st != JXL_BS_OK) {
        return st;
    }

    st = jxl_bs_read_bits(bs, 3, &ratio);
    if (st != JXL_BS_OK) {
        return st;
    }

    if (div8 && ratio == 0 && !preview) {
        uint32_t bits = 0;
        st = jxl_bs_read_bits(bs, 5, &bits);
        if (st != JXL_BS_OK) {
            return st;
        }
        w_div8 = bits + 1;
    }

    st = read_width(bs, div8, preview, ratio, w_div8, height, &out->width);
    if (st != JXL_BS_OK) {
        return st;
    }
    out->height = height;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_size_header_parse(jxl_bs *bs, jxl_size_header *out) {
    return size_header_parse_inner(bs, out, 0);
}

jxl_bs_status_t jxl_preview_header_parse(jxl_bs *bs, jxl_size_header *out) {
    return size_header_parse_inner(bs, out, 1);
}
