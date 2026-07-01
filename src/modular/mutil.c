// SPDX-License-Identifier: MIT OR Apache-2.0
#include "util.h"

jxl_modular_status_t jxl_modular_from_bs(jxl_bs_status_t st) {
    switch (st) {
    case JXL_BS_OK:
        return JXL_MODULAR_OK;
    default:
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
}

jxl_modular_status_t jxl_modular_from_coding(jxl_coding_status_t st) {
    switch (st) {
    case JXL_CODING_OK:
        return JXL_MODULAR_OK;
    default:
        return JXL_MODULAR_DECODER_ERROR;
    }
}

jxl_modular_status_t jxl_modular_from_grid_oom(const jxl_grid_oom *oom) {
    (void)oom;
    return JXL_MODULAR_OUT_OF_MEMORY;
}
