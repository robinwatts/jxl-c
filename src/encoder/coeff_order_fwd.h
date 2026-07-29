// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_COEFF_ORDER_FWD_H_
#define JXL_ENC_COEFF_ORDER_FWD_H_

// Breaks circular dependency between ac_strategy and coeff_order.

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"

// Needs at least 16 bits. A 32-bit type speeds up DecodeAC by 2% at the cost of
// more memory.
typedef uint32_t coeff_order_t;

// Maximum number of orders to be used. Note that this needs to be multiplied by
// the number of channels. One per "size class" (plus one extra for DCT8),
// shared between transforms of size XxY and of size YxX.
enum { kNumOrders = 13 };

// DCT coefficients are laid out so the number of rows is always the smaller
// coordinate.
static JXL_INLINE void jxl_coefficient_layout(size_t* JXL_RESTRICT rows,
                                         size_t* JXL_RESTRICT columns) {
  if (*rows > *columns) {
    size_t tmp = *rows;
    *rows = *columns;
    *columns = tmp;
  }
}

#endif  // JXL_ENC_COEFF_ORDER_FWD_H_
