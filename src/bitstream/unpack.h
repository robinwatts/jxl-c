// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_BITSTREAM_UNPACK_H_
#define JXL_BITSTREAM_UNPACK_H_

#include "jxl_oxide/jxl_types.h"

jxl_inline int32_t jxl_unpack_signed(uint32_t x) {
    uint32_t bit = x & 1u;
    uint32_t base = x >> 1;
    uint32_t flip = 0u - bit;
    return (int32_t)(base ^ flip);
}

jxl_inline int64_t jxl_unpack_signed_u64(uint64_t x) {
    uint64_t bit = x & 1ull;
    uint64_t base = x >> 1;
    uint64_t flip = 0ull - bit;
    return (int64_t)(base ^ flip);
}

#endif /* JXL_BITSTREAM_UNPACK_H_ */
