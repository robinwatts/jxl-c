// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_ANS_SIMD_H_
#define JXL_ENC_ENC_ANS_SIMD_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "dec_ans.h"
#include "memory_manager_internal.h"

// Returns "extra_bits" sum and puts tokens into `tokens`.
uint32_t jxl_estimate_token_cost(uint32_t* JXL_RESTRICT values, size_t len,
                           jxl_hybrid_uint_config cfg, jxl_aligned_memory* tokens);


#endif  // JXL_ENC_ENC_ANS_SIMD_H_
