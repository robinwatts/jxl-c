// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/toc.h"

#include <stddef.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/fields.h"

size_t jxl_max_bits(const size_t num_sizes) {
  const size_t entry_bits = jxl_u32_coder_max_encoded_bits(jxl_toc_dist()) * num_sizes;
  // permutation bit (not its tokens!), padding, entries, padding.
  return 1 + kBitsPerByte + entry_bits + kBitsPerByte;
}
