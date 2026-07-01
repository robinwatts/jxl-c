// SPDX-License-Identifier: MIT OR Apache-2.0
#include "util.h"

jxl_vardct_status_t jxl_vardct_from_bs(jxl_bs_status_t st) {
    return st == JXL_BS_OK ? JXL_VARDCT_OK : JXL_VARDCT_BITSTREAM_ERROR;
}

jxl_vardct_status_t jxl_vardct_from_coding(jxl_coding_status_t st) {
    return st == JXL_CODING_OK ? JXL_VARDCT_OK : JXL_VARDCT_DECODER_ERROR;
}

jxl_vardct_status_t jxl_vardct_from_modular(jxl_modular_status_t st) {
    switch (st) {
    case JXL_MODULAR_OK:
        return JXL_VARDCT_OK;
    case JXL_MODULAR_OUT_OF_MEMORY:
        return JXL_VARDCT_OUT_OF_MEMORY;
    default:
        return JXL_VARDCT_MODULAR_ERROR;
    }
}
