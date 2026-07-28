// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BROTLI_ALLOC_H_
#define LIB_JXL_BROTLI_ALLOC_H_

#include <brotli/encode.h>
#include "lib/jxl/memory_manager.h"

#include <stddef.h>

static void* jxl_brotli_alloc(void* opaque, size_t size) {
  jxl_memory_manager* mm = (jxl_memory_manager*)opaque;
  if (mm == NULL || size == 0) return NULL;
  return mm->alloc(mm->opaque, size);
}

static void jxl_brotli_free(void* opaque, void* ptr) {
  jxl_memory_manager* mm = (jxl_memory_manager*)opaque;
  if (mm == NULL || ptr == NULL) return;
  mm->free(mm->opaque, ptr);
}

static inline BrotliEncoderState* jxl_brotli_encoder_create(
    jxl_memory_manager* memory_manager) {
  if (memory_manager == NULL) return NULL;
  return BrotliEncoderCreateInstance(jxl_brotli_alloc, jxl_brotli_free,
                                     memory_manager);
}

#endif  // LIB_JXL_BROTLI_ALLOC_H_
