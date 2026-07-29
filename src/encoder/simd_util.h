// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_SIMD_UTIL_H_
#define JXL_ENC_SIMD_UTIL_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"


// Upper bound on SIMD vector width used for alignment / layout asserts.
// Runtime width for padding helpers is jxl_max_vector_size() (may be smaller).
enum { kJxlMaxVectorSize = 64 };

// Maximal vector size in bytes.
size_t jxl_max_vector_size();

uint32_t jxl_max_value(uint32_t* JXL_RESTRICT data, size_t len);


#endif  // JXL_ENC_SIMD_UTIL_H_
