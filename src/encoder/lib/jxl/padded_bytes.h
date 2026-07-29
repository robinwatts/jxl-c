// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_PADDED_BYTES_H_
#define LIB_JXL_BASE_PADDED_BYTES_H_

// Growable byte buffer with +8 capacity padding so jxl_bit_writer can write 64 bits
// without bounds checks. Backed by jxl_array_u8; bind a ctx via
// jxl_padded_bytes_make before growing.

#include <jxl/context.h>
#include "lib/jxl/enc_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>  // memcpy

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/enc_status.h"

// Provides a subset of the std::vector interface with some differences:
// - allows jxl_bit_writer to write 64 bits at a time without bounds checking;
// - ONLY zero-initializes the first byte (required by jxl_bit_writer);
// - keeps 8 extra bytes of capacity beyond the usable capacity_.
typedef struct jxl_padded_bytes {
  // Usable capacity excluding the 8-byte jxl_bit_writer pad.
  size_t capacity_;
  jxl_array_u8 data_;
} jxl_padded_bytes;

static inline size_t jxl_padded_bytes_size(const jxl_padded_bytes* self) {
  return jxl_array_len(&self->data_);
}
static inline bool jxl_padded_bytes_empty(const jxl_padded_bytes* self) {
  return jxl_array_empty(&self->data_);
}
static inline uint8_t* jxl_padded_bytes_data(jxl_padded_bytes* self) {
  return jxl_array_data(&self->data_);
}
static inline const uint8_t* jxl_padded_bytes_data_const(const jxl_padded_bytes* self) {
  return jxl_array_data_const(&self->data_);
}

static inline void jxl_padded_bytes_construct_empty(jxl_padded_bytes* self) {
  self->capacity_ = 0;
  jxl_array_construct_empty(&self->data_, NULL);
}

static inline void jxl_padded_bytes_destroy(jxl_padded_bytes* self) {
  jxl_array_destroy(&self->data_);
  self->capacity_ = 0;
}

static inline void jxl_padded_bytes_make(jxl_context* ctx,
                                   jxl_padded_bytes* out) {
  jxl_padded_bytes_construct_empty(out);
  out->data_.ctx = ctx;
}

static inline void jxl_padded_bytes_bounds_check(const jxl_padded_bytes* self, size_t i) {
  // <= is safe due to padding and required by jxl_bit_writer.
  JXL_DASSERT(i <= jxl_padded_bytes_size(self));
}

static inline uint8_t* jxl_padded_bytes_at(jxl_padded_bytes* self, size_t i) {
  jxl_padded_bytes_bounds_check(self, i);
  return &jxl_padded_bytes_data(self)[i];
}
static inline const uint8_t* jxl_padded_bytes_at_const(const jxl_padded_bytes* self,
                                                size_t i) {
  jxl_padded_bytes_bounds_check(self, i);
  return &jxl_padded_bytes_data_const(self)[i];
}

static inline void jxl_padded_bytes_swap(jxl_padded_bytes* self, jxl_padded_bytes* other) {
  size_t c = self->capacity_;
  self->capacity_ = other->capacity_;
  other->capacity_ = c;
  jxl_array_swap(&self->data_, &other->data_);
}

static inline jxl_status jxl_padded_bytes_change_capacity(jxl_padded_bytes* self,
                                               size_t new_capacity) {
  jxl_array_u8 neu;
  size_t padded_capacity;
  size_t old_size;
  JXL_DASSERT(new_capacity >= jxl_array_len(&self->data_));

  new_capacity = JXL_MAX((size_t)(64), new_capacity);

  // jxl_bit_writer writes up to 7 bytes past the end.
  if (!jxl_safe_add(new_capacity, (size_t)(8), &padded_capacity)) {
    return JXL_FAILURE("jxl_padded_bytes capacity too large");
  }

  jxl_array_construct_empty(&neu, self->data_.ctx);
  JXL_RETURN_IF_ERROR(jxl_array_reserve(&neu, padded_capacity));

  old_size = jxl_array_len(&self->data_);
  if (self->data_.ptr == NULL) {
    // First allocation: ensure first byte is initialized (won't be copied).
    neu.ptr[0] = 0;
  } else {
    memmove(neu.ptr, self->data_.ptr, old_size);
    // Ensure that the first new byte is initialized, to allow write_bits to
    // safely append to the newly-resized jxl_padded_bytes.
    neu.ptr[old_size] = 0;
  }
  neu.len = old_size;

  self->capacity_ = new_capacity;
  jxl_array_swap(&self->data_, &neu);
  jxl_array_destroy(&neu);
  return jxl_ok_status();
}

static inline jxl_status jxl_padded_bytes_reserve(jxl_padded_bytes* self, size_t capacity) {
  size_t new_capacity;
  if (capacity <= self->capacity_) return jxl_ok_status();

  if (!jxl_safe_add(self->capacity_, self->capacity_ / 2, &new_capacity)) {
    return JXL_FAILURE("jxl_padded_bytes reserve: capacity overflow");
  }
  new_capacity = JXL_MAX(capacity, new_capacity);

  return jxl_padded_bytes_change_capacity(self, new_capacity);
}

static inline jxl_status jxl_padded_bytes_resize(jxl_padded_bytes* self, size_t size) {
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_reserve(self, size));
  self->data_.len = size;
  return jxl_ok_status();
}

static inline jxl_status jxl_padded_bytes_push_back(jxl_padded_bytes* self, uint8_t x) {
  if (jxl_array_len(&self->data_) == self->capacity_) {
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_reserve(self, self->capacity_ + 1));
  }

  self->data_.ptr[self->data_.len++] = x;
  return jxl_ok_status();
}

static inline void jxl_padded_bytes_clear(jxl_padded_bytes* self) {
  // Not passing on the jxl_status, because resizing to 0 cannot fail.
  (void)(jxl_padded_bytes_resize(self, 0));
}

static inline jxl_status jxl_padded_bytes_append(jxl_padded_bytes* self, const uint8_t* begin,
                                       const uint8_t* end) {
  if (end > begin) {
    size_t old_size = jxl_padded_bytes_size(self);
    size_t new_size;
    if (!jxl_safe_add(old_size, (size_t)(end - begin), &new_size)) {
      return JXL_FAILURE("jxl_padded_bytes append: overflow");
    }
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_resize(self, new_size));
    memcpy(jxl_padded_bytes_data(self) + old_size, begin, end - begin);
  }
  return jxl_ok_status();
}

static inline jxl_status jxl_padded_bytes_init(jxl_padded_bytes* self, size_t size) {
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_reserve(self, size));
  self->data_.len = size;
  return jxl_ok_status();
}

static inline jxl_context* jxl_padded_bytes_ctx(
    const jxl_padded_bytes* self) {
  return self->data_.ctx;
}

static inline jxl_status jxl_padded_bytes_with_initial_space(
    jxl_context* ctx, size_t size, jxl_padded_bytes* out) {
  jxl_padded_bytes_make(ctx, out);
  return jxl_padded_bytes_init(out, size);
}

#endif  // LIB_JXL_BASE_PADDED_BYTES_H_
