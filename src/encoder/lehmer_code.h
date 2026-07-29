// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_LEHMER_CODE_H_
#define LIB_JXL_LEHMER_CODE_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "coeff_order_fwd.h"


// Permutation <=> factorial base representation (Lehmer code).

typedef uint32_t jxl_lehmer_t;

static inline uint32_t jxl_value_of_lowest1_bit(uint32_t t) { return t & -t; }

// Computes the Lehmer (factorial basis) code of permutation, an array of n
// unique indices in [0..n), and stores it in code[0..len). N*logN time.
// temp must have n + 1 elements but need not be initialized.
static inline jxl_enc_status jxl_compute_lehmer_code(const coeff_order_t* JXL_RESTRICT permutation,
                                uint32_t* JXL_RESTRICT temp, const size_t n,
                                jxl_lehmer_t* JXL_RESTRICT code) {
  for (size_t idx = 0; idx < n + 1; ++idx) temp[idx] = 0;

  for (size_t idx = 0; idx < n; ++idx) {
    const coeff_order_t s = permutation[idx];

    // Compute sum in Fenwick tree
    uint32_t penalty = 0;
    uint32_t i = s + 1;
    while (i != 0) {
      penalty += temp[i];
      i &= i - 1;  // clear lowest bit
    }
    JXL_ENSURE(s >= penalty);
    code[idx] = s - penalty;
    i = s + 1;
    // Add operation in Fenwick tree
    while (i < n + 1) {
      temp[i] += 1;
      i += jxl_value_of_lowest1_bit(i);
    }
  }
  return jxl_enc_ok_status();
}


#endif  // LIB_JXL_LEHMER_CODE_H_
