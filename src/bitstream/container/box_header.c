// SPDX-License-Identifier: MIT OR Apache-2.0
#include "box_header.h"

jxl_bs_status_t jxl_box_header_parse(const uint8_t *buf, size_t len, jxl_box_header *out,
                                     size_t *header_size) {
    if (len >= 16 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1) {
        uint64_t xlbox = ((uint64_t)buf[8] << 56) | ((uint64_t)buf[9] << 48) |
                         ((uint64_t)buf[10] << 40) | ((uint64_t)buf[11] << 32) |
                         ((uint64_t)buf[12] << 24) | ((uint64_t)buf[13] << 16) |
                         ((uint64_t)buf[14] << 8) | (uint64_t)buf[15];
        if (xlbox < 16) {
            return JXL_BS_INVALID_BOX;
        }
        out->ty.bytes[0] = buf[4];
        out->ty.bytes[1] = buf[5];
        out->ty.bytes[2] = buf[6];
        out->ty.bytes[3] = buf[7];
        out->box_size = xlbox - 16;
        out->has_box_size = 1;
        out->is_last = 0;
        *header_size = 16;
        return JXL_BS_OK;
    }
    if (len >= 8) {
        uint32_t sbox = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                        ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
        out->ty.bytes[0] = buf[4];
        out->ty.bytes[1] = buf[5];
        out->ty.bytes[2] = buf[6];
        out->ty.bytes[3] = buf[7];
        if (sbox == 0) {
            out->has_box_size = 0;
            out->is_last = 1;
        } else if (sbox < 8) {
            return JXL_BS_INVALID_BOX;
        } else {
            out->box_size = (uint64_t)sbox - 8u;
            out->has_box_size = 1;
            out->is_last = 0;
        }
        *header_size = 8;
        return JXL_BS_OK;
    }
    *header_size = 0;
    return JXL_BS_EOF;
}
