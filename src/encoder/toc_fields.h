// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_TOC_FIELDS_H_
#define LIB_JXL_TOC_FIELDS_H_

#include <stddef.h>

#include "base/compiler_specific.h"
#include "field_encodings.h"


// (2+bits) = 2,3,4 bytes so encoders can patch TOC after encoding.
// 30 is sufficient for 4K channels of uncompressed 16-bit samples.
static inline jxl_u32_enc jxl_toc_dist(void) {
  return jxl_u32_enc_make(jxl_bits(10), jxl_bits_offset(14, 1024), jxl_bits_offset(22, 17408),
                    jxl_bits_offset(30, 4211712));
}

size_t jxl_max_bits(size_t num_sizes);

static JXL_INLINE size_t jxl_ac_group_index(size_t pass, size_t group,
                                      size_t num_groups, size_t num_dc_groups) {
  return 2 + num_dc_groups + pass * num_groups + group;
}


#endif  // LIB_JXL_TOC_FIELDS_H_
