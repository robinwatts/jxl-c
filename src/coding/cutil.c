// SPDX-License-Identifier: MIT OR Apache-2.0
#include "util.h"

static uint32_t next_power_of_two(uint32_t v) {
    if (v == 0) {
        return 1;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

uint32_t jxl_coding_add_log2_ceil(uint32_t x) {
    uint32_t tz;
    uint32_t p;
    if (x >= 0x80000000u) {
        return 32;
    }
    p = next_power_of_two(x + 1);
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctz(p);
#else
    tz = 0;
    while (((p >> tz) & 1u) == 0) {
        tz++;
    }
    return tz;
#endif
}

jxl_coding_status_t jxl_coding_from_bs(jxl_bs_status_t st) {
    switch (st) {
    case JXL_BS_OK:
        return JXL_CODING_OK;
    case JXL_BS_EOF:
        return JXL_CODING_EOF;
    default:
        return JXL_CODING_BITSTREAM_ERROR;
    }
}
