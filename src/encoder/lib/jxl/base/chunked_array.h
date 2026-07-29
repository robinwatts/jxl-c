// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_CHUNKED_ARRAY_H_
#define LIB_JXL_BASE_CHUNKED_ARRAY_H_

// CSR-style list of chunks: flat Array data plus start offsets.
// starts has NumChunks()+1 entries when non-empty;
// *jxl_array_at(&starts, jxl_array_len(&starts)-1) == jxl_array_len(&data).

#include <jxl/context.h>
#include "lib/jxl/enc_allocator.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/base/enc_status.h"

// Byte payloads (JPEG APP/COM markers, box contents).
typedef struct jxl_byte_chunks {
  jxl_array_u8 data;
  jxl_array_u32 starts;
} jxl_byte_chunks;

static inline size_t jxl_byte_chunks_size(const jxl_byte_chunks* self) {
  return jxl_array_empty(&self->starts) ? 0 : jxl_array_len(&self->starts) - 1;
}
static inline bool jxl_byte_chunks_empty(const jxl_byte_chunks* self) {
  return jxl_byte_chunks_size(self) == 0;
}

static inline jxl_bytes jxl_byte_chunks_at(const jxl_byte_chunks* self, size_t i) {
  return jxl_bytes_make(
      jxl_array_data_const(&self->data) + *jxl_array_at_const(&self->starts, i),
      *jxl_array_at_const(&self->starts, i + 1) - *jxl_array_at_const(&self->starts, i));
}

static inline void jxl_byte_chunks_clear(jxl_byte_chunks* self) {
  jxl_array_clear(&self->data);
  jxl_array_clear(&self->starts);
}

static inline void jxl_byte_chunks_construct_empty(jxl_byte_chunks* self,
                                                   jxl_context* mm) {
  jxl_array_construct_empty(&self->data, mm);
  jxl_array_construct_empty(&self->starts, mm);
}

static inline void jxl_byte_chunks_destroy(jxl_byte_chunks* self) {
  jxl_array_destroy(&self->data);
  jxl_array_destroy(&self->starts);
}

static inline void jxl_byte_chunks_swap(jxl_byte_chunks* self, jxl_byte_chunks* other) {
  jxl_array_swap(&self->data, &other->data);
  jxl_array_swap(&self->starts, &other->starts);
}

static inline jxl_status jxl_byte_chunks_push_empty(jxl_byte_chunks* self) {
  if (jxl_array_empty(&self->starts)) {
    JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(&self->starts, (uint32_t)(0)));
  }
  return jxl_array_u32_push_back(&self->starts, (uint32_t)(jxl_array_len(&self->data)));
}

static inline jxl_status jxl_byte_chunks_push_back(jxl_byte_chunks* self, const uint8_t* items,
                                        size_t len) {
  if (jxl_array_empty(&self->starts)) {
    JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(&self->starts, (uint32_t)(0)));
  }
  JXL_RETURN_IF_ERROR(jxl_array_append(&self->data, items, len));
  return jxl_array_u32_push_back(&self->starts, (uint32_t)(jxl_array_len(&self->data)));
}

static inline jxl_status jxl_byte_chunks_push_back_u8(jxl_byte_chunks* self,
                                          const jxl_array_u8* chunk) {
  return jxl_byte_chunks_push_back(self, jxl_array_data_const(chunk), jxl_array_len(chunk));
}

static inline jxl_status jxl_byte_chunks_push_to_last(jxl_byte_chunks* self, uint8_t value) {
  JXL_DASSERT(jxl_byte_chunks_size(self) > 0);
  JXL_RETURN_IF_ERROR(jxl_array_u8_push_back(&self->data, value));
  *jxl_array_at(&self->starts, jxl_array_len(&self->starts) - 1) =
      (uint32_t)(jxl_array_len(&self->data));
  return jxl_ok_status();
}

// JPEG scan reset-point sidecars.
typedef struct jxl_u32_chunks {
  jxl_array_u32 data;
  jxl_array_u32 starts;
} jxl_u32_chunks;

static inline size_t jxl_u32_chunks_size(const jxl_u32_chunks* self) {
  return jxl_array_empty(&self->starts) ? 0 : jxl_array_len(&self->starts) - 1;
}
static inline bool jxl_u32_chunks_empty(const jxl_u32_chunks* self) {
  return jxl_u32_chunks_size(self) == 0;
}

static inline jxl_u32_span jxl_u32_chunks_mutable(jxl_u32_chunks* self, size_t i) {
  return jxl_u32_span_make(
      jxl_array_data(&self->data) + *jxl_array_at(&self->starts, i),
      *jxl_array_at(&self->starts, i + 1) - *jxl_array_at(&self->starts, i));
}

static inline void jxl_u32_chunks_clear(jxl_u32_chunks* self) {
  jxl_array_clear(&self->data);
  jxl_array_clear(&self->starts);
}

static inline void jxl_u32_chunks_construct_empty(jxl_u32_chunks* self,
                                                  jxl_context* mm) {
  jxl_array_construct_empty(&self->data, mm);
  jxl_array_construct_empty(&self->starts, mm);
}

static inline void jxl_u32_chunks_destroy(jxl_u32_chunks* self) {
  jxl_array_destroy(&self->data);
  jxl_array_destroy(&self->starts);
}

static inline void jxl_u32_chunks_swap(jxl_u32_chunks* self, jxl_u32_chunks* other) {
  jxl_array_swap(&self->data, &other->data);
  jxl_array_swap(&self->starts, &other->starts);
}

static inline jxl_status jxl_u32_chunks_push_empty(jxl_u32_chunks* self) {
  if (jxl_array_empty(&self->starts)) {
    JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(&self->starts, (uint32_t)(0)));
  }
  return jxl_array_u32_push_back(&self->starts, (uint32_t)(jxl_array_len(&self->data)));
}

static inline jxl_status jxl_u32_chunks_push_to_last(jxl_u32_chunks* self, uint32_t value) {
  JXL_DASSERT(jxl_u32_chunks_size(self) > 0);
  JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(&self->data, value));
  *jxl_array_at(&self->starts, jxl_array_len(&self->starts) - 1) =
      (uint32_t)(jxl_array_len(&self->data));
  return jxl_ok_status();
}

#endif  // LIB_JXL_BASE_CHUNKED_ARRAY_H_
