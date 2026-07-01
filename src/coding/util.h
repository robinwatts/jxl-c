// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_UTIL_H_
#define JXL_CODING_UTIL_H_

#include "bitstream/bitstream.h"
#include "coding/error.h"

#include "jxl_oxide/jxl_types.h"

uint32_t jxl_coding_add_log2_ceil(uint32_t x);

jxl_coding_status_t jxl_coding_from_bs(jxl_bs_status_t st);

#endif /* JXL_CODING_UTIL_H_ */
