// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MEMORY_MANAGER_INTERNAL_H_
#define LIB_JXL_MEMORY_MANAGER_INTERNAL_H_

// Memory allocator with support for alignment + misalignment.

#include <jxl/memory_manager.h>

#include <stddef.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"


// To avoid RFOs, match L2 fill size (pairs of lines); 2 x cache line size.
enum {
  kMemoryAlignment = 2 * 64,
  kMemoryNumAlignmentGroups = 16,
  kMemoryAlias = kMemoryNumAlignmentGroups * kMemoryAlignment
};
JXL_STATIC_ASSERT((kMemoryAlignment & (kMemoryAlignment - 1)) == 0,
                  "kMemoryAlignment must be a power of 2");
JXL_STATIC_ASSERT(
    (kMemoryNumAlignmentGroups & (kMemoryNumAlignmentGroups - 1)) == 0,
    "kMemoryNumAlignmentGroups must be a power of 2");

// Initializes the memory manager instance with the passed one. The
// MemoryManager passed in |memory_manager| may be NULL or contain NULL
// functions which will be initialized with the default ones. If either alloc
// or free are NULL, then both must be NULL, otherwise this function returns an
// error.
jxl_status jxl_memory_manager_init(jxl_memory_manager* self,
                         const jxl_memory_manager* memory_manager);

void* jxl_memory_manager_alloc(const jxl_memory_manager* memory_manager, size_t size);
void jxl_memory_manager_free(const jxl_memory_manager* memory_manager, void* address);

// Returns recommended distance in bytes between the start of two consecutive
// rows.
jxl_status jxl_bytes_per_row(size_t xsize, size_t sizeof_t, size_t* out);

typedef struct jxl_aligned_memory {
  // TODO(eustas): we can offer "actually accessible" size; it is 0-2KiB bigger
  //               than requested size, due to generous alignment;
  //               might be useful for resizeable containers (e.g. jxl_padded_bytes)

  void* allocation_;
  jxl_memory_manager* memory_manager_;
  void* address_;
} jxl_aligned_memory;

void jxl_aligned_memory_destroy(jxl_aligned_memory* self);
jxl_status jxl_aligned_memory_create(jxl_memory_manager* memory_manager, size_t size,
                           size_t pre_padding, jxl_aligned_memory* out);
void jxl_aligned_memory_init(jxl_aligned_memory* self, jxl_memory_manager* memory_manager,
                       void* allocation, size_t pre_padding);

static inline void* jxl_aligned_memory_address(const jxl_aligned_memory* self) {
  return self->address_;
}

static inline jxl_memory_manager* jxl_aligned_memory_memory_manager(
    const jxl_aligned_memory* self) {
  return self->memory_manager_;
}

static inline void jxl_aligned_memory_construct_empty(jxl_aligned_memory* self) {
  self->allocation_ = NULL;
  self->memory_manager_ = NULL;
  self->address_ = NULL;
}

static inline void jxl_aligned_memory_swap(jxl_aligned_memory* self,
                                     jxl_aligned_memory* other) {
  void* a = self->allocation_;
  self->allocation_ = other->allocation_;
  other->allocation_ = a;
  jxl_memory_manager* m = self->memory_manager_;
  self->memory_manager_ = other->memory_manager_;
  other->memory_manager_ = m;
  void* addr = self->address_;
  self->address_ = other->address_;
  other->address_ = addr;
}

#endif  // LIB_JXL_MEMORY_MANAGER_INTERNAL_H_
