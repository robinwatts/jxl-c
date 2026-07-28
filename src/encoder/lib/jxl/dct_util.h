// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_DCT_UTIL_H_
#define LIB_JXL_DCT_UTIL_H_

#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/status.h"
#include "lib/jxl/image.h"
#include "lib/jxl/image_ops.h"

typedef struct jxl_ac_ptr {
  int32_t* ptr32;
} jxl_ac_ptr;

static inline jxl_ac_ptr jxl_ac_ptr_make(int32_t* p) {
  jxl_ac_ptr self;
  self.ptr32 = p;
  return self;
}

typedef struct jxl_const_ac_ptr {
  const int32_t* ptr32;
} jxl_const_ac_ptr;

static inline jxl_const_ac_ptr jxl_const_ac_ptr_make(const int32_t* p) {
  jxl_const_ac_ptr self;
  self.ptr32 = p;
  return self;
}

// Per-group AC coefficient image (int32). Value type — no heap polymorphism.
typedef struct jxl_ac_image {
  jxl_image3_i img_;
} jxl_ac_image;

static inline void jxl_ac_image_construct_empty(jxl_ac_image* self) {
  jxl_image3_i_construct_empty(&self->img_);
}
static inline void jxl_ac_image_destroy(jxl_ac_image* self) {
  jxl_image3_i_destroy(&self->img_);
}
static inline void jxl_ac_image_swap(jxl_ac_image* self, jxl_ac_image* other) {
  jxl_image3_i_swap(&self->img_, &other->img_);
}

static inline jxl_ac_ptr jxl_ac_image_plane_row(jxl_ac_image* self, size_t c, size_t y,
                                    size_t xbase) {
  return jxl_ac_ptr_make(jxl_image3_i_plane_row(&self->img_, c, y) + xbase);
}
static inline jxl_const_ac_ptr jxl_ac_image_plane_row_const(const jxl_ac_image* self, size_t c,
                                              size_t y, size_t xbase) {
  return jxl_const_ac_ptr_make(jxl_image3_i_plane_row_const(&self->img_, c, y) + xbase);
}

static inline void jxl_ac_image_zero_fill(jxl_ac_image* self) {
  jxl_zero_fill_image3_i(&self->img_);
}

static inline void jxl_ac_image_zero_fill_plane(jxl_ac_image* self, size_t c) {
  jxl_zero_fill_image_i(jxl_image3_i_plane(&self->img_, c));
}

static inline jxl_status jxl_ac_image_make(jxl_context* ctx, size_t xsize,
                                 size_t ysize, jxl_ac_image* out) {
  jxl_ac_image result;
  jxl_status status;
  jxl_ac_image_construct_empty(&result);
  status = jxl_image3_i_create(ctx, xsize, ysize, &result.img_);
  if (!jxl_status_ok(status)) {
    jxl_ac_image_destroy(&result);
    return status;
  }
  jxl_ac_image_swap(out, &result);
  jxl_ac_image_destroy(&result);
  return jxl_ok_status();
}

#endif  // LIB_JXL_DCT_UTIL_H_
