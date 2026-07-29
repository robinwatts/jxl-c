// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/memory_manager_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>  // memcpy

#include "lib/jxl/enc_allocator.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/simd_util.h"

jxl_enc_status jxl_bytes_per_row(const size_t xsize, const size_t sizeof_t,
                             size_t* out) {
  // Special case: we don't allow any ops -> don't need extra padding/
  if (xsize == 0) {
    *out = 0;
    return jxl_enc_ok_status();
  }

  const size_t vec_size = jxl_max_vector_size();
  size_t valid_bytes;
  if (!jxl_safe_mul(xsize, sizeof_t, &valid_bytes)) {
    return JXL_FAILURE("jxl_image dimensions are too large");
  }

  // Allow unaligned accesses starting at the last valid value.
  // NB: in scalar build `vec_size` is `4` (one `float`); still worth checking.
  if (vec_size != 0) {
    // NB: in scalar build when T is "double" we "extra" would be negative.
    JXL_ENSURE(vec_size >= sizeof_t);
    size_t extra = vec_size - sizeof_t;
    if (!jxl_safe_add(valid_bytes, extra, &valid_bytes)) {
      return JXL_FAILURE("jxl_image dimensions are too large");
    }
  }

  // Round up to vector and cache line size.
  const size_t align = JXL_MAX(vec_size, kMemoryAlignment);
  JXL_ENSURE((align & (align - 1)) == 0);
  size_t bytes_per_row;
  if (!jxl_safe_round_up_to(valid_bytes, align, &bytes_per_row)) {
    return JXL_FAILURE("jxl_image dimensions are too large");
  }

  // During the lengthy window before writes are committed to memory, CPUs
  // guard against read after write hazards by checking the address, but
  // only the lower 11 bits. We avoid a false dependency between writes to
  // consecutive rows by ensuring their sizes are not multiples of 2 KiB.
  // Avoid2K prevents the same problem for the planes of an Image3.
  if (bytes_per_row % kMemoryAlias == 0) {
    if (!jxl_safe_add(bytes_per_row, align, &bytes_per_row)) {
      return JXL_FAILURE("jxl_image dimensions are too large");
    }
  }

  JXL_DASSERT(bytes_per_row % align == 0);
  *out = bytes_per_row;
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_aligned_memory_create(jxl_context* ctx, size_t size,
                                     size_t pre_padding,
                                     jxl_aligned_memory* out) {
  JXL_ENSURE(pre_padding <= kMemoryAlias);
  size_t allocation_size;
  if (!jxl_safe_add(size, pre_padding, &allocation_size) ||
      !jxl_safe_add(allocation_size, kMemoryAlias, &allocation_size)) {
    return JXL_FAILURE("Requested allocation is too large");
  }
  JXL_ENSURE(ctx);
  void* allocated = jxl_alloc(ctx, allocation_size);
  if (allocated == NULL) {
    return JXL_FAILURE("Allocation failed");
  }
  jxl_aligned_memory mem;
  jxl_aligned_memory_construct_empty(&mem);
  jxl_aligned_memory_init(&mem, ctx, allocated, pre_padding);
  jxl_aligned_memory_swap(out, &mem);
  jxl_aligned_memory_destroy(&mem);
  return jxl_enc_ok_status();
}

void jxl_aligned_memory_init(jxl_aligned_memory* self, jxl_context* ctx,
                             void* allocation, size_t pre_padding) {
  self->allocation_ = allocation;
  self->ctx_ = ctx;
  // Congruence to `offset` (mod kAlias) reduces cache conflicts and load/store
  // stalls, especially with large allocations that would otherwise have similar
  // alignments.
  size_t group;
  if (ctx != NULL) {
    jxl_allocator_state* state = jxl_context_alloc_state(ctx);
    group = (size_t)(state->next_align_group++);
  } else {
    static uint32_t next_group = 0;
    group = (size_t)(__atomic_fetch_add(&next_group, 1u, __ATOMIC_RELAXED));
  }
  group &= (kMemoryNumAlignmentGroups - 1);
  size_t offset = kMemoryAlignment * group;

  // Actual allocation.
  uintptr_t address = (uintptr_t)(allocation) + pre_padding;

  // Aligned address, but might land before allocation (50%/50%) or not have
  // enough pre-padding.
  uintptr_t aligned_address = (address & ~(kMemoryAlias - 1)) + offset;
  if (aligned_address < address) aligned_address += kMemoryAlias;

  self->address_ = (void*)(aligned_address);  // NOLINT
}

void jxl_aligned_memory_destroy(jxl_aligned_memory* self) {
  if (self == NULL) return;
  if (self->ctx_ == NULL) return;
  jxl_free(self->ctx_, self->allocation_);
  self->ctx_ = NULL;
  self->allocation_ = NULL;
  self->address_ = NULL;
}
