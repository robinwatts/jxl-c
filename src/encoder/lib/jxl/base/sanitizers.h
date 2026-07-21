// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_SANITIZERS_H_
#define LIB_JXL_SANITIZERS_H_

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/sanitizer_definitions.h"

#if JXL_MEMORY_SANITIZER
#include "sanitizer/msan_interface.h"
#endif


#if JXL_MEMORY_SANITIZER

static const uint8_t kMsanSentinelByte = 0x48;

static JXL_INLINE JXL_MAYBE_UNUSED void jxl_msan_unpoison_memory(
    const volatile void* m, size_t size) {
  __msan_unpoison(m, size);
}

static JXL_INLINE JXL_MAYBE_UNUSED void jxl_msan_memory_is_initialized(
    const volatile void* m, size_t size) {
  __msan_check_mem_is_initialized(m, size);
}

#else  // JXL_MEMORY_SANITIZER

static const uint8_t kMsanSentinelByte = 0x48;

static JXL_INLINE JXL_MAYBE_UNUSED void jxl_msan_unpoison_memory(const void* m,
                                                           size_t size) {}
static JXL_INLINE JXL_MAYBE_UNUSED void jxl_msan_memory_is_initialized(const void* m,
                                                                size_t size) {}

#endif


#endif  // LIB_JXL_SANITIZERS_H_
