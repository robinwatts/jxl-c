// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BROTLI_ALLOC_H_
#define LIB_JXL_BROTLI_ALLOC_H_

#include <brotli/encode.h>
#include <jxl/context.h>
#include "lib/jxl/enc_allocator.h"

#include <stddef.h>

static void* jxl_brotli_alloc(void* opaque, size_t size) {
  jxl_context* mm = (jxl_context*)opaque;
  if (mm == NULL || size == 0) return NULL;
  return jxl_alloc(mm, size);
}

static void jxl_brotli_free(void* opaque, void* ptr) {
  jxl_context* mm = (jxl_context*)opaque;
  if (mm == NULL || ptr == NULL) return;
  jxl_free(mm, ptr);
}

static inline BrotliEncoderState* jxl_brotli_encoder_create(
    jxl_context* ctx) {
  if (ctx == NULL) return NULL;
  return BrotliEncoderCreateInstance(jxl_brotli_alloc, jxl_brotli_free,
                                     ctx);
}

#endif  // LIB_JXL_BROTLI_ALLOC_H_
