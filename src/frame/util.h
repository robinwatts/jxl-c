// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_UTIL_H_
#define JXL_FRAME_UTIL_H_

#include "bitstream/bitstream.h"
#include "coding/decoder.h"
#include "frame/error.h"

#define JXL_FRAME_TRY_BS(expr)                                                                     \
    do {                                                                                           \
        jxl_bs_status_t _st = (expr);                                                              \
        if (_st != JXL_BS_OK) {                                                                    \
            return JXL_FRAME_BITSTREAM_ERROR;                                                      \
        }                                                                                          \
    } while (0)

#define JXL_FRAME_TRY_CODING(expr)                                                                 \
    do {                                                                                           \
        jxl_coding_status_t _st = (expr);                                                          \
        if (_st != JXL_CODING_OK) {                                                                \
            return JXL_FRAME_DECODER_ERROR;                                                        \
        }                                                                                          \
    } while (0)

#endif /* JXL_FRAME_UTIL_H_ */
