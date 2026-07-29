// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_COEFF_ORDER_H_
#define LIB_JXL_COEFF_ORDER_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "coeff_order_fwd.h"
#include "frame_dimensions.h"

// Those offsets get multiplied by kDCTBlockSize.

enum { kCoeffOrderLimit = 6156 };

static const size_t kCoeffOrderOffset[3 * kNumOrders + 1] = {
    0,    1,    2,    3,    4,    5,    6,    10,   14,   18,
    34,   50,   66,   68,   70,   72,   76,   80,   84,   92,
    100,  108,  172,  236,  300,  332,  364,  396,  652,  908,
    1164, 1292, 1420, 1548, 2572, 3596, 4620, 5132, 5644, kCoeffOrderLimit};

// TODO(eustas): rollback to constexpr once modern C++ becomes required.
#define jxl_coeff_order_offset(O, C) \
  (kCoeffOrderOffset[3 * (O) + (C)] * kDCTBlockSize)

static JXL_MAYBE_UNUSED const size_t kCoeffOrderMaxSize =
    kCoeffOrderLimit * kDCTBlockSize;

// Mapping from AC strategy to order bucket. Strategies with different natural
// orders must have different buckets.
static const uint8_t kStrategyOrder[27] = {
    0, 1, 1, 1, 2, 3, 4, 4, 5,  5,  6,  6,  1,  1,
    1, 1, 1, 1, 7, 8, 8, 9, 10, 10, 11, 12, 12,
};
#define kStrategyOrderSize \
    (sizeof(kStrategyOrder) / sizeof((kStrategyOrder)[0]))

enum { kPermutationContexts = 8 };

uint32_t jxl_coeff_order_context(uint32_t val);



#endif  // LIB_JXL_COEFF_ORDER_H_
