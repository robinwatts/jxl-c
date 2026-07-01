// SPDX-License-Identifier: MIT OR Apache-2.0
#include "dct_select.h"

#include <stddef.h>

int jxl_transform_type_from_u8(uint8_t value, jxl_transform_type *out) {
    if (out == NULL) {
        return 0;
    }
    if (value > (uint8_t)JXL_TRANSFORM_DCT128X256) {
        return 0;
    }
    *out = (jxl_transform_type)value;
    return 1;
}

void jxl_transform_dct_select_size(jxl_transform_type t, uint32_t *w_blocks, uint32_t *h_blocks) {
    uint32_t w = 1;
    uint32_t h = 1;
    switch (t) {
    case JXL_TRANSFORM_DCT16:
        w = h = 2;
        break;
    case JXL_TRANSFORM_DCT32:
        w = h = 4;
        break;
    case JXL_TRANSFORM_DCT16X8:
        w = 1;
        h = 2;
        break;
    case JXL_TRANSFORM_DCT8X16:
        w = 2;
        h = 1;
        break;
    case JXL_TRANSFORM_DCT32X8:
        w = 1;
        h = 4;
        break;
    case JXL_TRANSFORM_DCT8X32:
        w = 4;
        h = 1;
        break;
    case JXL_TRANSFORM_DCT32X16:
        w = 2;
        h = 4;
        break;
    case JXL_TRANSFORM_DCT16X32:
        w = 4;
        h = 2;
        break;
    case JXL_TRANSFORM_DCT64:
        w = h = 8;
        break;
    case JXL_TRANSFORM_DCT64X32:
        w = 4;
        h = 8;
        break;
    case JXL_TRANSFORM_DCT32X64:
        w = 8;
        h = 4;
        break;
    case JXL_TRANSFORM_DCT128:
        w = h = 16;
        break;
    case JXL_TRANSFORM_DCT128X64:
        w = 8;
        h = 16;
        break;
    case JXL_TRANSFORM_DCT64X128:
        w = 16;
        h = 8;
        break;
    case JXL_TRANSFORM_DCT256:
        w = h = 32;
        break;
    case JXL_TRANSFORM_DCT256X128:
        w = 16;
        h = 32;
        break;
    case JXL_TRANSFORM_DCT128X256:
        w = 32;
        h = 16;
        break;
    default:
        break;
    }
    if (w_blocks != NULL) {
        *w_blocks = w;
    }
    if (h_blocks != NULL) {
        *h_blocks = h;
    }
}

void jxl_transform_dequant_matrix_size(jxl_transform_type t, uint32_t *w, uint32_t *h) {
    uint32_t wb = 0;
    uint32_t hb = 0;
    jxl_transform_dct_select_size(t, &wb, &hb);
    if (w != NULL) {
        *w = wb * 8;
    }
    if (h != NULL) {
        *h = hb * 8;
    }
}

uint32_t jxl_transform_dequant_matrix_param_index(jxl_transform_type t) {
    switch (t) {
    case JXL_TRANSFORM_DCT8:
        return 0;
    case JXL_TRANSFORM_HORNUSS:
        return 1;
    case JXL_TRANSFORM_DCT2:
        return 2;
    case JXL_TRANSFORM_DCT4:
        return 3;
    case JXL_TRANSFORM_DCT16:
        return 4;
    case JXL_TRANSFORM_DCT32:
        return 5;
    case JXL_TRANSFORM_DCT16X8:
    case JXL_TRANSFORM_DCT8X16:
        return 6;
    case JXL_TRANSFORM_DCT32X8:
    case JXL_TRANSFORM_DCT8X32:
        return 7;
    case JXL_TRANSFORM_DCT32X16:
    case JXL_TRANSFORM_DCT16X32:
        return 8;
    case JXL_TRANSFORM_DCT4X8:
    case JXL_TRANSFORM_DCT8X4:
        return 9;
    case JXL_TRANSFORM_AFV0:
    case JXL_TRANSFORM_AFV1:
    case JXL_TRANSFORM_AFV2:
    case JXL_TRANSFORM_AFV3:
        return 10;
    case JXL_TRANSFORM_DCT64:
        return 11;
    case JXL_TRANSFORM_DCT64X32:
    case JXL_TRANSFORM_DCT32X64:
        return 12;
    case JXL_TRANSFORM_DCT128:
        return 13;
    case JXL_TRANSFORM_DCT128X64:
    case JXL_TRANSFORM_DCT64X128:
        return 14;
    case JXL_TRANSFORM_DCT256:
        return 15;
    case JXL_TRANSFORM_DCT256X128:
    case JXL_TRANSFORM_DCT128X256:
        return 16;
    }
    return 0;
}

uint32_t jxl_transform_order_id(jxl_transform_type t) {
    switch (t) {
    case JXL_TRANSFORM_DCT8:
        return 0;
    case JXL_TRANSFORM_DCT16:
        return 2;
    case JXL_TRANSFORM_DCT32:
        return 3;
    case JXL_TRANSFORM_DCT16X8:
    case JXL_TRANSFORM_DCT8X16:
        return 4;
    case JXL_TRANSFORM_DCT32X8:
    case JXL_TRANSFORM_DCT8X32:
        return 5;
    case JXL_TRANSFORM_DCT32X16:
    case JXL_TRANSFORM_DCT16X32:
        return 6;
    case JXL_TRANSFORM_DCT64:
        return 7;
    case JXL_TRANSFORM_DCT64X32:
    case JXL_TRANSFORM_DCT32X64:
        return 8;
    case JXL_TRANSFORM_DCT128:
        return 9;
    case JXL_TRANSFORM_DCT128X64:
    case JXL_TRANSFORM_DCT64X128:
        return 10;
    case JXL_TRANSFORM_DCT256:
        return 11;
    case JXL_TRANSFORM_DCT256X128:
    case JXL_TRANSFORM_DCT128X256:
        return 12;
    default:
        return 1;
    }
}

int jxl_transform_need_transpose(jxl_transform_type t) {
    switch (t) {
    case JXL_TRANSFORM_HORNUSS:
    case JXL_TRANSFORM_DCT2:
    case JXL_TRANSFORM_DCT4:
    case JXL_TRANSFORM_DCT4X8:
    case JXL_TRANSFORM_DCT8X4:
    case JXL_TRANSFORM_AFV0:
    case JXL_TRANSFORM_AFV1:
    case JXL_TRANSFORM_AFV2:
    case JXL_TRANSFORM_AFV3:
        return 0;
    default: {
        uint32_t w = 0;
        uint32_t h = 0;
        jxl_transform_dct_select_size(t, &w, &h);
        return h >= w;
    }
    }
}
