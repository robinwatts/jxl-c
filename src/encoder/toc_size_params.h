// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_TOC_SIZE_PARAMS_H_
#define JXL_ENC_TOC_SIZE_PARAMS_H_

#include <stddef.h>

// TOC group-size U32 buckets (ISO/IEC 18181-1): value is encoded as a 2-bit
// selector plus `jxl_toc_group_size_extra_bits[bucket]` extra bits, offset by
// `jxl_toc_group_size_offset[bucket]`. Shared by JPEG→JXL and simple lossless.

enum { JXL_TOC_GROUP_SIZE_BUCKETS = 4 };

static const size_t jxl_toc_group_size_offset[JXL_TOC_GROUP_SIZE_BUCKETS] = {
    0u, 1024u, 17408u, 4211712u};
static const size_t jxl_toc_group_size_extra_bits[JXL_TOC_GROUP_SIZE_BUCKETS] = {
    10u, 14u, 22u, 30u};

#endif  // JXL_ENC_TOC_SIZE_PARAMS_H_
