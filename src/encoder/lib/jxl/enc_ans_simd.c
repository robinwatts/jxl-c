// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_ans_simd.h"

#include <stdint.h>

#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/memory_manager_internal.h"


uint32_t jxl_estimate_token_cost(uint32_t* JXL_RESTRICT values, size_t len,
                           jxl_hybrid_uint_config cfg, jxl_aligned_memory* tokens) {
  uint32_t* JXL_RESTRICT out = (uint32_t*)(jxl_aligned_memory_address(tokens));
  JXL_DASSERT(cfg.lsb_in_token + cfg.msb_in_token <= cfg.split_exponent);
  uint32_t extra_bits = 0;
  for (size_t i = 0; i < len; ++i) {
    uint32_t v = values[i];
    uint32_t tok, nbits, bits;
    jxl_hybrid_uint_config_encode(cfg, v, &tok, &nbits, &bits);
    extra_bits += nbits;
    out[i] = tok;
  }
  return extra_bits;
}

