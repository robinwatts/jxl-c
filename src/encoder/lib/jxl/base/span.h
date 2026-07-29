// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_SPAN_H_
#define LIB_JXL_BASE_SPAN_H_

// Non-owning pointer+length views. Layout is C-friendly.
// jxl_bytes is the const-byte view used by the encoder/API surface.
// jxl_u32_span is the mutable uint32 view used by JPEG scan sidecars.

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/enc_status.h"

typedef struct jxl_bytes {
  const uint8_t* ptr_;
  size_t len_;
} jxl_bytes;

static inline size_t jxl_bytes_size(const jxl_bytes* self) { return self->len_; }
static inline bool jxl_bytes_is_empty(const jxl_bytes* self) { return self->len_ == 0; }
static inline const uint8_t* jxl_bytes_data(const jxl_bytes* self) { return self->ptr_; }
static inline uint8_t jxl_bytes_at(const jxl_bytes* self, size_t i) {
  return self->ptr_[i];
}

static inline jxl_bytes jxl_bytes_make(const uint8_t* array, size_t length) {
  jxl_bytes self;
  self.ptr_ = array;
  self.len_ = length;
  return self;
}

static inline jxl_bytes jxl_bytes_empty(void) { return jxl_bytes_make(NULL, 0); }

static inline jxl_enc_status jxl_bytes_remove_prefix(jxl_bytes* self, size_t n) {
  JXL_ENSURE(jxl_bytes_size(self) >= n);
  self->ptr_ += n;
  self->len_ -= n;
  return jxl_enc_ok_status();
}

typedef struct jxl_u32_span {
  uint32_t* ptr_;
  size_t len_;
} jxl_u32_span;

static inline size_t jxl_u32_span_size(const jxl_u32_span* self) { return self->len_; }
static inline bool jxl_u32_span_is_empty(const jxl_u32_span* self) {
  return self->len_ == 0;
}
static inline uint32_t* jxl_u32_span_data(const jxl_u32_span* self) { return self->ptr_; }
static inline uint32_t* jxl_u32_span_at(const jxl_u32_span* self, size_t i) {
  return self->ptr_ + i;
}

static inline jxl_u32_span jxl_u32_span_make(uint32_t* array, size_t length) {
  jxl_u32_span self;
  self.ptr_ = array;
  self.len_ = length;
  return self;
}

static inline jxl_u32_span jxl_u32_span_empty(void) { return jxl_u32_span_make(NULL, 0); }

JXL_DEFINE_POD_ARRAY(jxl_array_bytes, jxl_bytes)

#endif  // LIB_JXL_BASE_SPAN_H_
