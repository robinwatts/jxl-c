// SPDX-License-Identifier: MIT OR Apache-2.0
#include "noise.h"

#include <string.h>

jxl_bs_status_t jxl_noise_parameters_parse(jxl_bs *bs, jxl_noise_parameters *out) {
    size_t i;
    if (bs == NULL || out == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    memset(out, 0, sizeof(*out));
    for (i = 0; i < 8; ++i) {
        uint32_t raw = 0;
        jxl_bs_status_t st = jxl_bs_read_bits(bs, 10, &raw);
        if (st != JXL_BS_OK) {
            return st;
        }
        out->lut[i] = (float)raw / 1024.0f;
    }
    return JXL_BS_OK;
}
