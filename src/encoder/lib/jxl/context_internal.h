// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Encoder-facing context accessors. Do NOT include src/context.h here — it
// pulls decoder modular/vardct types that collide with encoder symbols.

#ifndef LIB_JXL_CONTEXT_INTERNAL_H_
#define LIB_JXL_CONTEXT_INTERNAL_H_

#include <jxl/context.h>
#include "lib/jxl/memory_manager.h"

#include "allocator.h"
#include "lib/jxl/color_encoding_internal.h"

jxl_memory_manager* jxl_context_memory_manager(jxl_context* ctx);
void jxl_context_bind_memory_manager(jxl_context* ctx);
void* jxl_context_lcms(jxl_context* ctx);
const jxl_enc_color_encoding* jxl_context_srgb(jxl_context* ctx, int is_gray);

#endif  // LIB_JXL_CONTEXT_INTERNAL_H_
