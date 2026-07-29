// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENTROPY_CODER_H_
#define JXL_ENC_ENTROPY_CODER_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "field_encodings.h"

// jxl_entropy coding and context modeling of DC and AC coefficients, as well as AC
// strategy and quantization field.


static JXL_INLINE int32_t jxl_predict_from_top_and_left(
    const int32_t* const JXL_RESTRICT row_top,
    const int32_t* const JXL_RESTRICT row, size_t x, int32_t default_val) {
  if (x == 0) {
    return row_top == NULL ? default_val : row_top[x];
  }
  if (row_top == NULL) {
    return row[x - 1];
  }
  return (row_top[x] + row[x - 1] + 1) / 2;
}

static inline jxl_u32_enc jxl_dc_threshold_dist(void) {
  return jxl_u32_enc_make(jxl_bits(4), jxl_bits_offset(8, 16), jxl_bits_offset(16, 272),
                    jxl_bits_offset(32, 65808));
}

static inline jxl_u32_enc jxl_qf_threshold_dist(void) {
  return jxl_u32_enc_make(jxl_bits(2), jxl_bits_offset(3, 4), jxl_bits_offset(5, 12),
                    jxl_bits_offset(8, 44));
}



#endif  // JXL_ENC_ENTROPY_CODER_H_
