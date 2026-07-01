// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_UTIL_H_
#define JXL_VARDCT_UTIL_H_

#include "bitstream/bitstream.h"
#include "coding/decoder.h"
#include "coding/error.h"
#include "modular/error.h"
#include "vardct/error.h"

jxl_vardct_status_t jxl_vardct_from_bs(jxl_bs_status_t st);
jxl_vardct_status_t jxl_vardct_from_coding(jxl_coding_status_t st);
jxl_vardct_status_t jxl_vardct_from_modular(jxl_modular_status_t st);

#define JXL_VARDCT_TRY_BS(expr)                                                                    \
    do {                                                                                           \
        jxl_bs_status_t _st = (expr);                                                              \
        jxl_vardct_status_t _vst = jxl_vardct_from_bs(_st);                                        \
        if (_vst != JXL_VARDCT_OK) {                                                               \
            return _vst;                                                                           \
        }                                                                                          \
    } while (0)

#define JXL_VARDCT_TRY_CODING(expr)                                                                \
    do {                                                                                           \
        jxl_coding_status_t _st = (expr);                                                          \
        jxl_vardct_status_t _vst = jxl_vardct_from_coding(_st);                                    \
        if (_vst != JXL_VARDCT_OK) {                                                               \
            return _vst;                                                                           \
        }                                                                                          \
    } while (0)

#define JXL_VARDCT_TRY_MODULAR(expr)                                                               \
    do {                                                                                           \
        jxl_modular_status_t _st = (expr);                                                         \
        jxl_vardct_status_t _vst = jxl_vardct_from_modular(_st);                                   \
        if (_vst != JXL_VARDCT_OK) {                                                               \
            return _vst;                                                                           \
        }                                                                                          \
    } while (0)

#endif /* JXL_VARDCT_UTIL_H_ */
