// SPDX-License-Identifier: MIT OR Apache-2.0
#include "predictor.h"

#include "modular/util.h"

jxl_modular_status_t jxl_wp_header_parse(jxl_bs *bs, jxl_wp_header *out) {
    int all_default;
    if (out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    all_default = 1;
    JXL_MODULAR_TRY_BS(jxl_bs_read_bool(bs, &all_default));
    if (all_default) {
        out->default_wp = 1;
        out->wp_p1 = 16;
        out->wp_p2 = 10;
        out->wp_p3a = 7;
        out->wp_p3b = 7;
        out->wp_p3c = 7;
        out->wp_p3d = 0;
        out->wp_p3e = 0;
        out->wp_w0 = 13;
        out->wp_w1 = 12;
        out->wp_w2 = 12;
        out->wp_w3 = 12;
        return JXL_MODULAR_OK;
    }
    out->default_wp = 0;
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p1));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p2));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p3a));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p3b));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p3c));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p3d));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 5, &out->wp_p3e));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 4, &out->wp_w0));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 4, &out->wp_w1));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 4, &out->wp_w2));
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 4, &out->wp_w3));
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_predictor_from_u32(uint32_t value, jxl_predictor *out) {
    if (out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (value > 13) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    *out = (jxl_predictor)value;
    return JXL_MODULAR_OK;
}
