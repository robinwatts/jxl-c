// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_TOC_H_
#define LIB_JXL_ENC_TOC_H_

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/enc_bit_writer.h"



// Writes the TOC permutation header: always "no permutation" + ZeroPadToByte.
jxl_enc_status jxl_write_toc_permutation(jxl_bit_writer* JXL_RESTRICT writer);

// Writes the TOC size entries
jxl_enc_status jxl_write_toc_sizes(const jxl_array_size* group_sizes,
                     jxl_bit_writer* JXL_RESTRICT writer);


#endif  // LIB_JXL_ENC_TOC_H_
