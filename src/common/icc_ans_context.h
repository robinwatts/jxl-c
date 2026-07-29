// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_ICC_ANS_CONTEXT_H_
#define JXL_COMMON_ICC_ANS_CONTEXT_H_

#include "compiler.h"

#include <stddef.h>
#include <stdint.h>

#include "common/icc_codec_params.h"

/* ANS context for compressed ICC byte streams. Contexts are in
 * [0, JXL_NUM_ICC_CONTEXTS). */

jxl_inline uint32_t jxl_icc_ans_context(size_t idx, uint8_t b1, uint8_t b2) {
  uint32_t p1;
  uint32_t p2;
  if (idx <= 128) {
    return 0;
  }

  if ((b1 >= 'a' && b1 <= 'z') || (b1 >= 'A' && b1 <= 'Z')) {
    p1 = 0;
  } else if ((b1 >= '0' && b1 <= '9') || b1 == '.' || b1 == ',') {
    p1 = 1;
  } else if (b1 <= 1) {
    p1 = 2 + (uint32_t)b1;
  } else if (b1 <= 15) {
    p1 = 4;
  } else if (b1 >= 241 && b1 <= 254) {
    p1 = 5;
  } else if (b1 == 255) {
    p1 = 6;
  } else {
    p1 = 7;
  }

  if ((b2 >= 'a' && b2 <= 'z') || (b2 >= 'A' && b2 <= 'Z')) {
    p2 = 0;
  } else if ((b2 >= '0' && b2 <= '9') || b2 == '.' || b2 == ',') {
    p2 = 1;
  } else if (b2 <= 15) {
    p2 = 2;
  } else if (b2 >= 241) {
    p2 = 3;
  } else {
    p2 = 4;
  }

  return 1 + p1 + 8 * p2;
}

#endif /* JXL_COMMON_ICC_ANS_CONTEXT_H_ */
