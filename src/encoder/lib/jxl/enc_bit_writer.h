// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_BIT_WRITER_H_
#define LIB_JXL_ENC_BIT_WRITER_H_

// jxl_bit_writer: unbuffered writes using unaligned 64-bit stores.

#include <jxl/context.h>
#include "lib/jxl/enc_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/padded_bytes.h"

#include "lib/jxl/layer_type.h"


typedef struct jxl_bit_writer jxl_bit_writer;
typedef struct jxl_bit_writer_allotment jxl_bit_writer_allotment;

jxl_status jxl_bit_writer_append_byte_aligned(jxl_bit_writer* self, const jxl_bit_writer* others,
                                  size_t num);
void jxl_bit_writer_write(jxl_bit_writer* self, size_t n_bits, uint64_t bits);
jxl_status jxl_bit_writer_allotment_init(jxl_bit_writer_allotment* self,
                              jxl_bit_writer* JXL_RESTRICT writer);
jxl_status jxl_bit_writer_allotment_reclaim(jxl_bit_writer_allotment* self,
                                 jxl_bit_writer* JXL_RESTRICT writer);
void jxl_bit_writer_allotment_destroy(jxl_bit_writer_allotment* self);

typedef struct jxl_bit_writer_allotment {
  size_t prev_bits_written_;
  size_t max_bits_;
  bool called_;
  jxl_bit_writer_allotment* parent_;
} jxl_bit_writer_allotment;

static inline void jxl_bit_writer_allotment_reset(jxl_bit_writer_allotment* self,
                                           size_t max_bits) {
  self->prev_bits_written_ = 0;
  self->max_bits_ = max_bits;
  self->called_ = false;
  self->parent_ = NULL;
}

// Upper bound on `n_bits` in each call to jxl_bit_writer_write. We shift a 64-bit
// word by 7 bits (max already valid bits in the last byte) and at least 1 bit
// is needed to zero-initialize the bit-stream ahead (i.e. if 7 bits are valid
// and we write 57 bits, then the next write will access a byte that was not
// yet zero-initialized).
enum { kBitWriterMaxBitsPerCall = 56 };

typedef struct jxl_bit_writer {
  size_t bits_written_;
  jxl_padded_bytes storage_;
  jxl_bit_writer_allotment* current_allotment_;
} jxl_bit_writer;

static inline void jxl_bit_writer_construct_empty(jxl_bit_writer* self) {
  self->bits_written_ = 0;
  self->storage_.capacity_ = 0;
  jxl_array_construct_empty(&self->storage_.data_, NULL);
  self->current_allotment_ = NULL;
}

static inline void jxl_bit_writer_destroy(jxl_bit_writer* self) {
  jxl_array_destroy(&self->storage_.data_);
  self->storage_.capacity_ = 0;
  self->bits_written_ = 0;
  self->current_allotment_ = NULL;
}

static inline void jxl_bit_writer_init(jxl_bit_writer* self, jxl_context* ctx) {
  self->bits_written_ = 0;
  jxl_padded_bytes_make(ctx, &self->storage_);
  self->current_allotment_ = NULL;
}

static inline void jxl_bit_writer_make(jxl_context* ctx, jxl_bit_writer* out) {
  jxl_bit_writer_construct_empty(out);
  jxl_bit_writer_init(out, ctx);
}

static inline void jxl_bit_writer_swap(jxl_bit_writer* self, jxl_bit_writer* other) {
  size_t tb = self->bits_written_;
  self->bits_written_ = other->bits_written_;
  other->bits_written_ = tb;
  jxl_padded_bytes_swap(&self->storage_, &other->storage_);
  jxl_bit_writer_allotment* ta = self->current_allotment_;
  self->current_allotment_ = other->current_allotment_;
  other->current_allotment_ = ta;
}

static inline size_t jxl_bit_writer_bits_written(const jxl_bit_writer* self) {
  return self->bits_written_;
}

static inline jxl_context* jxl_bit_writer_ctx(const jxl_bit_writer* self) {
  return jxl_padded_bytes_ctx(&self->storage_);
}

static inline jxl_bytes jxl_bit_writer_get_span(const jxl_bit_writer* self) {
  // Callers must ensure byte alignment to avoid uninitialized bits.
  JXL_DASSERT(self->bits_written_ % kBitsPerByte == 0);
  return jxl_bytes_make(jxl_padded_bytes_data_const(&self->storage_),
                   jxl_div_ceil(self->bits_written_, kBitsPerByte));
}

static inline void jxl_bit_writer_take_bytes(jxl_bit_writer* self, jxl_padded_bytes* out) {
  // Callers must ensure byte alignment to avoid uninitialized bits.
  JXL_DASSERT(self->bits_written_ % kBitsPerByte == 0);
  jxl_status status = jxl_padded_bytes_resize(
      &self->storage_, jxl_div_ceil(self->bits_written_, kBitsPerByte));
  JXL_DASSERT(jxl_status_ok(status));
  // Can never fail, because we are resizing to a lower size.
  (void)status;
  jxl_padded_bytes_make(jxl_padded_bytes_ctx(&self->storage_), out);
  jxl_padded_bytes_swap(&self->storage_, out);
  self->bits_written_ = 0;
}

static inline void jxl_bit_writer_zero_pad_to_byte(jxl_bit_writer* self) {
  const size_t remainder_bits =
      jxl_round_up_bits_to_byte_multiple(self->bits_written_) - self->bits_written_;
  if (remainder_bits == 0) return;
  jxl_bit_writer_write(self, remainder_bits, 0);
  JXL_DASSERT(self->bits_written_ % kBitsPerByte == 0);
}

static inline jxl_status jxl_bit_writer_with_max_bits(jxl_bit_writer* self, size_t max_bits,
                                   jxl_layer_type layer,
                                   jxl_status (*body)(void* opaque), void* opaque) {
  (void)layer;
  jxl_bit_writer_allotment allotment;
  jxl_bit_writer_allotment_reset(&allotment, max_bits);
  jxl_status status = jxl_bit_writer_allotment_init(&allotment, self);
  if (!jxl_status_ok(status)) {
    jxl_bit_writer_allotment_destroy(&allotment);
    return status;
  }
  const jxl_status result = body(opaque);
  status = jxl_bit_writer_allotment_reclaim(&allotment, self);
  jxl_bit_writer_allotment_destroy(&allotment);
  JXL_RETURN_IF_ERROR(status);
  return result;
}

// Growable list of jxl_bit_writers (was MoveArray<jxl_bit_writer>).
typedef struct jxl_bit_writers {
  jxl_context* ctx;
  jxl_bit_writer* ptr;
  size_t len;
  size_t capacity;
} jxl_bit_writers;
static inline size_t jxl_bit_writers_size(const jxl_bit_writers* self) { return self->len; }
static inline bool jxl_bit_writers_empty(const jxl_bit_writers* self) { return self->len == 0; }
static inline jxl_bit_writer* jxl_bit_writers_data(jxl_bit_writers* self) { return self->ptr; }
static inline const jxl_bit_writer* jxl_bit_writers_data_const(const jxl_bit_writers* self) { return self->ptr; }
static inline jxl_bit_writer* jxl_bit_writers_at(jxl_bit_writers* self, size_t i) { return &self->ptr[i]; }
static inline const jxl_bit_writer* jxl_bit_writers_at_const(const jxl_bit_writers* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_bit_writers_construct_empty(jxl_bit_writers* self) {
  self->ctx = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_bit_writers_swap(jxl_bit_writers* self, jxl_bit_writers* other) {
  jxl_context* tmp_mm = self->ctx;
  self->ctx = other->ctx;
  other->ctx = tmp_mm;
  jxl_bit_writer* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}

static inline void jxl_bit_writers_destroy(jxl_bit_writers* self) {
  size_t i;
  for (i = 0; i < self->len; ++i) {
    jxl_bit_writer_destroy(self->ptr + i);
  }
  self->len = 0;
  if (self->ptr != NULL) {
    if (self->ctx != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline jxl_status jxl_bit_writers_reserve(jxl_bit_writers* self, size_t new_capacity) {
  size_t grown;
  size_t bytes;
  jxl_bit_writer* neu;
  size_t i;
  if (new_capacity <= self->capacity) return jxl_ok_status();

  grown = self->capacity;
  if (grown == 0) grown = 16;
  while (grown < new_capacity) {
    size_t next;
    if (!jxl_safe_add(grown, grown / 2, &next) || next <= grown) {
      grown = new_capacity;
      break;
    }
    grown = next;
  }
  if (grown < new_capacity) grown = new_capacity;

  if (!jxl_safe_mul(grown, sizeof(jxl_bit_writer), &bytes)) {
    return JXL_FAILURE("jxl_bit_writers::reserve: size overflow");
  }
  if (self->ctx == NULL) {
    return JXL_FAILURE("jxl_bit_writers::reserve: missing memory manager");
  }
  neu = (jxl_bit_writer*)(
      jxl_alloc(self->ctx, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_bit_writers::reserve: allocation failed");
  }
  for (i = 0; i < self->len; ++i) {
    jxl_bit_writer_construct_empty(neu + i);
    jxl_bit_writer_init(neu + i, jxl_bit_writer_ctx(&self->ptr[i]));
    jxl_bit_writer_swap(neu + i, &self->ptr[i]);
    jxl_bit_writer_destroy(self->ptr + i);
  }
  if (self->ptr != NULL) {
    jxl_free(self->ctx, self->ptr);
  }
  self->ptr = neu;
  self->capacity = grown;
  return jxl_ok_status();
}

static inline jxl_status jxl_bit_writers_emplace_back(jxl_bit_writers* self,
                                           jxl_context* mm) {
  if (self->len == self->capacity) {
    size_t need;
    if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
      return JXL_FAILURE("jxl_bit_writers::emplace_back: overflow");
    }
    JXL_RETURN_IF_ERROR(jxl_bit_writers_reserve(self, need));
  }
  jxl_bit_writer_construct_empty(self->ptr + self->len);
  jxl_bit_writer_init(self->ptr + self->len, mm);
  ++self->len;
  return jxl_ok_status();
}

#endif  // LIB_JXL_ENC_BIT_WRITER_H_
