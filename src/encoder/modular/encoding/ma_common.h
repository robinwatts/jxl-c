// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_MODULAR_ENCODING_MA_COMMON_H_
#define JXL_ENC_MODULAR_ENCODING_MA_COMMON_H_

#include <stddef.h>


enum jxl_ma_tree_context {
  kSplitValContext = 0,
  kPropertyContext = 1,
  kPredictorContext = 2,
  kOffsetContext = 3,
  kMultiplierLogContext = 4,
  kMultiplierBitsContext = 5,

  kNumTreeContexts = 6,
};

static const size_t kMaxTreeSize = 1 << 22;


#endif  // JXL_ENC_MODULAR_ENCODING_MA_COMMON_H_
