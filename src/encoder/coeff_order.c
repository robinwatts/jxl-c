// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "coeff_order.h"

#include <stdint.h>

#include "ac_strategy.h"
#include "dec_ans.h"
#include "base/common.h"
#include "base/compiler_specific.h"


JXL_STATIC_ASSERT(kAcStrategyNumValidStrategies == kStrategyOrderSize,
              "Update this array when adding or removing AC strategies.");

uint32_t jxl_coeff_order_context(uint32_t val) {
  uint32_t token, nbits, bits;
  jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config_make(0, 0, 0), val, &token, &nbits, &bits);
  return JXL_MIN(token, kPermutationContexts - 1);
}

