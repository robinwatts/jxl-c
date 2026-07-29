// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Encoder-facing context accessors. src/context.h is the shared thin layout
// (allocator + opaque decoder caches); prefer these accessors for CMS/sRGB.

#ifndef LIB_JXL_CONTEXT_INTERNAL_H_
#define LIB_JXL_CONTEXT_INTERNAL_H_

#include <jxl/context.h>
#include "enc_allocator.h"

#include "allocator.h"
#include "color_encoding_internal.h"

void* jxl_context_lcms(jxl_context* ctx);
const jxl_enc_color_encoding* jxl_context_srgb(jxl_context* ctx, int is_gray);

#endif  // LIB_JXL_CONTEXT_INTERNAL_H_
