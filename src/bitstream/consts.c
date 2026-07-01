// SPDX-License-Identifier: MIT OR Apache-2.0
#include "consts.h"

const uint8_t jxl_codestream_sig[JXL_CODESTREAM_SIG_LEN] = {0xff, 0x0a};
const uint8_t jxl_container_sig[JXL_CONTAINER_SIG_LEN] = {
    0x00, 0x00, 0x00, 0x0c, 'J', 'X', 'L', ' ', 0x0d, 0x0a, 0x87, 0x0a,
};
