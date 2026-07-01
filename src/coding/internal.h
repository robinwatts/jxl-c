// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_INTERNAL_H_
#define JXL_CODING_INTERNAL_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "coding/error.h"

#define JXL_CODING_TRY_BS(expr)                                                                    \
    do {                                                                                           \
        jxl_bs_status_t _st = (expr);                                                              \
        jxl_coding_status_t _cst = jxl_coding_from_bs(_st);                                        \
        if (_cst != JXL_CODING_OK) {                                                               \
            return _cst;                                                                           \
        }                                                                                          \
    } while (0)

#define JXL_CODING_RETURN_IF(expr, code)                                                           \
    do {                                                                                           \
        if (expr) {                                                                                \
            return (code);                                                                         \
        }                                                                                          \
    } while (0)

#endif /* JXL_CODING_INTERNAL_H_ */
