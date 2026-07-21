// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/simd_util.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/common.h"


size_t jxl_max_vector_size() { return 8; }

uint32_t jxl_max_value(uint32_t* JXL_RESTRICT data, size_t len) {
  uint32_t max = 0;
  for (size_t i = 0; i < len; ++i) {
    max = JXL_MAX(max, data[i]);
  }
  return max;
}

