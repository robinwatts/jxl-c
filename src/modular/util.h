// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_UTIL_H_
#define JXL_MODULAR_UTIL_H_

#include "bitstream/bitstream.h"
#include "bitstream/unpack.h"
#include "grid/error.h"
#include "coding/error.h"
#include "modular/error.h"

jxl_modular_status_t jxl_modular_from_bs(jxl_bs_status_t st);
jxl_modular_status_t jxl_modular_from_coding(jxl_coding_status_t st);
jxl_modular_status_t jxl_modular_from_grid_oom(const jxl_grid_oom *oom);

#define JXL_MODULAR_TRY_BS(expr)                                                                   \
    do {                                                                                           \
        jxl_bs_status_t _st = (expr);                                                              \
        jxl_modular_status_t _mst = jxl_modular_from_bs(_st);                                      \
        if (_mst != JXL_MODULAR_OK) {                                                              \
            return _mst;                                                                           \
        }                                                                                          \
    } while (0)

#define JXL_MODULAR_TRY(expr)                                                                      \
    do {                                                                                           \
        jxl_modular_status_t _st = (expr);                                                         \
        if (_st != JXL_MODULAR_OK) {                                                               \
            return _st;                                                                           \
        }                                                                                          \
    } while (0)

#endif /* JXL_MODULAR_UTIL_H_ */
