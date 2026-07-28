// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_IMAGE_H_
#define LIB_JXL_IMAGE_H_

// SIMD/multicore-friendly planar image representation with row accessors.

#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/memory_manager_internal.h"

// jxl_plane_base is image.{h|cc}-local; prefer typed Plane helpers elsewhere.
// Type-independent parts of jxl_image_sb/jxl_image_b/jxl_image_i/jxl_image_f - reduces code
// duplication and facilitates moving member function implementations to cc.
typedef struct jxl_plane_base {
  // (Members are non-const; jxl_plane_base_swap transfers ownership.)
  uint32_t xsize_;  // In valid pixels, not including any padding.
  uint32_t ysize_;
  size_t bytes_per_row_;  // Includes padding.
  jxl_aligned_memory bytes_;
  size_t sizeof_t_;
} jxl_plane_base;

jxl_status jxl_plane_base_allocate(jxl_plane_base* self, jxl_context* ctx,
                         size_t pre_padding);
void jxl_plane_base_init(jxl_plane_base* self, uint32_t xsize, uint32_t ysize,
                   size_t sizeof_t);

static inline void jxl_plane_base_construct_empty(jxl_plane_base* self) {
  self->xsize_ = 0;
  self->ysize_ = 0;
  self->bytes_per_row_ = 0;
  jxl_aligned_memory_construct_empty(&self->bytes_);
  self->sizeof_t_ = 0;
}

static inline void jxl_plane_base_destroy(jxl_plane_base* self) {
  jxl_aligned_memory_destroy(&self->bytes_);
  self->xsize_ = 0;
  self->ysize_ = 0;
  self->bytes_per_row_ = 0;
  self->sizeof_t_ = 0;
}

static inline size_t jxl_plane_base_x_size(const jxl_plane_base* self) {
  return self->xsize_;
}
static inline size_t jxl_plane_base_y_size(const jxl_plane_base* self) {
  return self->ysize_;
}
// NOTE: do not use this for copying rows - the valid xsize may be much less.
static inline size_t jxl_plane_base_bytes_per_row(const jxl_plane_base* self) {
  return self->bytes_per_row_;
}
static inline void* jxl_plane_base_void_row(const jxl_plane_base* self, size_t y) {
  JXL_DASSERT(y < self->ysize_);
  uint8_t* row = (uint8_t*)(jxl_aligned_memory_address(&self->bytes_)) +
                 y * self->bytes_per_row_;
  return JXL_ASSUME_ALIGNED(row, 64);
}

static inline uint8_t* jxl_plane_base_bytes(jxl_plane_base* self) {
  uint8_t* p = (uint8_t*)(jxl_aligned_memory_address(&self->bytes_));
  return (uint8_t * JXL_RESTRICT)(JXL_ASSUME_ALIGNED(p, 64));
}
static inline const uint8_t* jxl_plane_base_bytes_const(const jxl_plane_base* self) {
  const uint8_t* p = (const uint8_t*)(jxl_aligned_memory_address(&self->bytes_));
  return (const uint8_t * JXL_RESTRICT)(JXL_ASSUME_ALIGNED(p, 64));
}

static inline void jxl_plane_base_swap(jxl_plane_base* self, jxl_plane_base* other) {
  size_t tx = self->xsize_;
  self->xsize_ = other->xsize_;
  other->xsize_ = tx;
  size_t ty = self->ysize_;
  self->ysize_ = other->ysize_;
  other->ysize_ = ty;
  size_t tb = self->bytes_per_row_;
  self->bytes_per_row_ = other->bytes_per_row_;
  other->bytes_per_row_ = tb;
  size_t ts = self->sizeof_t_;
  self->sizeof_t_ = other->sizeof_t_;
  other->sizeof_t_ = ts;
  jxl_aligned_memory_swap(&self->bytes_, &other->bytes_);
}

static inline jxl_context* jxl_plane_base_ctx(const jxl_plane_base* self) {
  return jxl_aligned_memory_ctx(&self->bytes_);
}

// Single channel, aligned rows separated by padding.
//
// 'Single channel' (one 2D array per channel) simplifies vectorization
// (repeating the same operation on multiple adjacent components) without the
// complexity of a hybrid layout (8 R, 8 G, 8 B, ...).
//
// 'Aligned' means each row is aligned to the L1 cache line size. This prevents
// false sharing between two threads operating on adjacent rows.
//
// 'Padding' rounds up row sizes so reading/writing ALIGNED vectors whose first
// lane is a valid sample avoids a separate remainder loop.
#define JXL_DEFINE_PLANE(NAME, TYPE)                                          \
  typedef struct NAME {                                                       \
    jxl_plane_base plane;                                                     \
  } NAME;                                                                     \
  static inline void NAME##_construct_empty(NAME* self) {                     \
    jxl_plane_base_construct_empty(&self->plane);                             \
  }                                                                           \
  static inline void NAME##_destroy(NAME* self) {                             \
    jxl_plane_base_destroy(&self->plane);                                     \
  }                                                                           \
  static inline size_t NAME##_x_size(const NAME* self) {                      \
    return jxl_plane_base_x_size(&self->plane);                               \
  }                                                                           \
  static inline size_t NAME##_y_size(const NAME* self) {                      \
    return jxl_plane_base_y_size(&self->plane);                               \
  }                                                                           \
  static inline size_t NAME##_bytes_per_row(const NAME* self) {               \
    return jxl_plane_base_bytes_per_row(&self->plane);                        \
  }                                                                           \
  static inline uint8_t* NAME##_bytes(NAME* self) {                           \
    return jxl_plane_base_bytes(&self->plane);                                \
  }                                                                           \
  static inline const uint8_t* NAME##_bytes_const(const NAME* self) {         \
    return jxl_plane_base_bytes_const(&self->plane);                          \
  }                                                                           \
  static inline ptrdiff_t NAME##_pixels_per_row(const NAME* self) {           \
    return (ptrdiff_t)(self->plane.bytes_per_row_ / sizeof(TYPE));            \
  }                                                                           \
  static inline TYPE* NAME##_row(NAME* self, size_t y) {                      \
    return (TYPE*)(jxl_plane_base_void_row(&self->plane, y));                 \
  }                                                                           \
  static inline const TYPE* NAME##_const_row(const NAME* self, size_t y) {    \
    return (const TYPE*)(jxl_plane_base_void_row(&self->plane, y));           \
  }                                                                           \
  static inline void NAME##_swap(NAME* self, NAME* other) {                   \
    jxl_plane_base_swap(&self->plane, &other->plane);                         \
  }                                                                           \
  static inline jxl_context* NAME##_ctx(const NAME* self) { \
    return jxl_plane_base_ctx(&self->plane);                       \
  }                                                                           \
  static inline jxl_status NAME##_allocate(NAME* self,                        \
                                      jxl_context* ctx,     \
                                      size_t pre_padding) {                   \
    return jxl_plane_base_allocate(&self->plane, ctx,              \
                                   pre_padding);                              \
  }                                                                           \
  static inline jxl_status NAME##_create(jxl_context* ctx,  \
                                    const size_t xsize, const size_t ysize,   \
                                    const size_t pre_padding, NAME* out) {    \
    uint32_t xsize32 = (uint32_t)(xsize);                                     \
    uint32_t ysize32 = (uint32_t)(ysize);                                     \
    NAME image;                                                               \
    jxl_status status;                                                        \
    JXL_ENSURE(xsize32 == xsize);                                             \
    JXL_ENSURE(ysize32 == ysize);                                             \
    NAME##_construct_empty(&image);                                           \
    jxl_plane_base_init(&image.plane, xsize32, ysize32, sizeof(TYPE));        \
    status = NAME##_allocate(&image, ctx, pre_padding);            \
    if (!jxl_status_ok(status)) {                                             \
      NAME##_destroy(&image);                                                 \
      return status;                                                          \
    }                                                                         \
    NAME##_swap(out, &image);                                                 \
    NAME##_destroy(&image);                                                   \
    return jxl_ok_status();                                                   \
  }

JXL_DEFINE_PLANE(jxl_image_sb, int8_t)
JXL_DEFINE_PLANE(jxl_image_b, uint8_t)
JXL_DEFINE_PLANE(jxl_image_i, int32_t)
JXL_DEFINE_PLANE(jxl_image_f, float)
#undef JXL_DEFINE_PLANE

// Currently, we abuse jxl_image to either refer to an image that owns its storage
// or one that doesn't. In similar vein, we abuse jxl_image* function parameters to
// either mean "assign to me" or "fill the provided image with data".
// Hopefully, the "assign to me" meaning will go away and most images in the
// codebase will not be backed by own storage. When this happens we can redesign
// jxl_image to be a non-storage-holding view class and introduce BackedImage in
// those places that actually need it.

// NOTE: we can't use jxl_image as a view because invariants are violated
// (alignment and the presence of padding before/after each "row").

enum { kImage3NumPlanes = 3 };

// A bundle of 3 same-sized int32 planes (nzero counts / AC tokens).
typedef struct jxl_image3_i {
  jxl_image_i planes_[kImage3NumPlanes];
} jxl_image3_i;

static inline size_t jxl_image3_i_x_size(const jxl_image3_i* self) {
  return jxl_image_i_x_size(&self->planes_[0]);
}
static inline size_t jxl_image3_i_y_size(const jxl_image3_i* self) {
  return jxl_image_i_y_size(&self->planes_[0]);
}
static inline ptrdiff_t jxl_image3_i_pixels_per_row(const jxl_image3_i* self) {
  return jxl_image_i_pixels_per_row(&self->planes_[0]);
}

static inline void jxl_image3_i_plane_row_bounds_check(const jxl_image3_i* self, size_t c,
                                              size_t y) {
  JXL_DASSERT(c < kImage3NumPlanes && y < jxl_image3_i_y_size(self));
}

static inline int32_t* jxl_image3_i_plane_row(jxl_image3_i* self, size_t c, size_t y) {
  jxl_image3_i_plane_row_bounds_check(self, c, y);
  const size_t row_offset = y * jxl_image_i_bytes_per_row(&self->planes_[0]);
  void* row = jxl_image_i_bytes(&self->planes_[c]) + row_offset;
  return (int32_t * JXL_RESTRICT)(JXL_ASSUME_ALIGNED(row, 64));
}

static inline const int32_t* jxl_image3_i_plane_row_const(const jxl_image3_i* self, size_t c,
                                                  size_t y) {
  jxl_image3_i_plane_row_bounds_check(self, c, y);
  const size_t row_offset = y * jxl_image_i_bytes_per_row(&self->planes_[0]);
  const void* row = jxl_image_i_bytes_const(&self->planes_[c]) + row_offset;
  return (const int32_t * JXL_RESTRICT)(JXL_ASSUME_ALIGNED(row, 64));
}

static inline const int32_t* jxl_image3_i_const_plane_row(const jxl_image3_i* self, size_t c,
                                                  size_t y) {
  return jxl_image3_i_plane_row_const(self, c, y);
}

static inline jxl_image_i* jxl_image3_i_plane(jxl_image3_i* self, size_t idx) {
  return &self->planes_[idx];
}
static inline const jxl_image_i* jxl_image3_i_plane_const(const jxl_image3_i* self, size_t idx) {
  return &self->planes_[idx];
}

static inline jxl_context* jxl_image3_i_ctx(const jxl_image3_i* self) {
  return jxl_image_i_ctx(&self->planes_[0]);
}
static inline void jxl_image3_i_swap(jxl_image3_i* self, jxl_image3_i* other) {
  size_t i;
  for (i = 0; i < kImage3NumPlanes; i++) {
    jxl_image_i_swap(&self->planes_[i], &other->planes_[i]);
  }
}

static inline void jxl_image3_i_construct_empty(jxl_image3_i* self) {
  size_t i;
  for (i = 0; i < kImage3NumPlanes; i++) {
    jxl_image_i_construct_empty(&self->planes_[i]);
  }
}

static inline void jxl_image3_i_destroy(jxl_image3_i* self) {
  size_t i;
  for (i = 0; i < kImage3NumPlanes; i++) {
    jxl_image_i_destroy(&self->planes_[i]);
  }
}

static inline jxl_status jxl_image3_i_create(jxl_context* ctx,
                                   const size_t xsize, const size_t ysize,
                                   jxl_image3_i* out) {
  jxl_image_i plane0;
  jxl_image_i plane1;
  jxl_image_i plane2;
  jxl_image3_i tmp;
  jxl_status status;
  jxl_image_i_construct_empty(&plane0);
  jxl_image_i_construct_empty(&plane1);
  jxl_image_i_construct_empty(&plane2);
  status = jxl_image_i_create(ctx, xsize, ysize, 0, &plane0);
  if (!jxl_status_ok(status)) {
    jxl_image_i_destroy(&plane0);
    jxl_image_i_destroy(&plane1);
    jxl_image_i_destroy(&plane2);
    return status;
  }
  status = jxl_image_i_create(ctx, xsize, ysize, 0, &plane1);
  if (!jxl_status_ok(status)) {
    jxl_image_i_destroy(&plane0);
    jxl_image_i_destroy(&plane1);
    jxl_image_i_destroy(&plane2);
    return status;
  }
  status = jxl_image_i_create(ctx, xsize, ysize, 0, &plane2);
  if (!jxl_status_ok(status)) {
    jxl_image_i_destroy(&plane0);
    jxl_image_i_destroy(&plane1);
    jxl_image_i_destroy(&plane2);
    return status;
  }
  jxl_image3_i_construct_empty(&tmp);
  jxl_image_i_swap(&tmp.planes_[0], &plane0);
  jxl_image_i_swap(&tmp.planes_[1], &plane1);
  jxl_image_i_swap(&tmp.planes_[2], &plane2);
  jxl_image3_i_swap(out, &tmp);
  jxl_image3_i_destroy(&tmp);
  jxl_image_i_destroy(&plane0);
  jxl_image_i_destroy(&plane1);
  jxl_image_i_destroy(&plane2);
  return jxl_ok_status();
}

// A bundle of 3 same-sized float planes (e.g. JPEG DC).
typedef struct jxl_image3_f {
  jxl_image_f planes_[kImage3NumPlanes];
} jxl_image3_f;

static inline size_t jxl_image3_f_x_size(const jxl_image3_f* self) {
  return jxl_image_f_x_size(&self->planes_[0]);
}
static inline size_t jxl_image3_f_y_size(const jxl_image3_f* self) {
  return jxl_image_f_y_size(&self->planes_[0]);
}
static inline ptrdiff_t jxl_image3_f_pixels_per_row(const jxl_image3_f* self) {
  return jxl_image_f_pixels_per_row(&self->planes_[0]);
}

static inline void jxl_image3_f_plane_row_bounds_check(const jxl_image3_f* self, size_t c,
                                              size_t y) {
  JXL_DASSERT(c < kImage3NumPlanes && y < jxl_image3_f_y_size(self));
}

static inline float* jxl_image3_f_plane_row(jxl_image3_f* self, size_t c, size_t y) {
  jxl_image3_f_plane_row_bounds_check(self, c, y);
  const size_t row_offset = y * jxl_image_f_bytes_per_row(&self->planes_[0]);
  void* row = jxl_image_f_bytes(&self->planes_[c]) + row_offset;
  return (float * JXL_RESTRICT)(JXL_ASSUME_ALIGNED(row, 64));
}

static inline const float* jxl_image3_f_plane_row_const(const jxl_image3_f* self, size_t c,
                                                size_t y) {
  jxl_image3_f_plane_row_bounds_check(self, c, y);
  const size_t row_offset = y * jxl_image_f_bytes_per_row(&self->planes_[0]);
  const void* row = jxl_image_f_bytes_const(&self->planes_[c]) + row_offset;
  return (const float * JXL_RESTRICT)(JXL_ASSUME_ALIGNED(row, 64));
}

static inline const float* jxl_image3_f_const_plane_row(const jxl_image3_f* self, size_t c,
                                                size_t y) {
  return jxl_image3_f_plane_row_const(self, c, y);
}

static inline jxl_image_f* jxl_image3_f_plane(jxl_image3_f* self, size_t idx) {
  return &self->planes_[idx];
}
static inline const jxl_image_f* jxl_image3_f_plane_const(const jxl_image3_f* self, size_t idx) {
  return &self->planes_[idx];
}

static inline jxl_context* jxl_image3_f_ctx(const jxl_image3_f* self) {
  return jxl_image_f_ctx(&self->planes_[0]);
}
static inline void jxl_image3_f_swap(jxl_image3_f* self, jxl_image3_f* other) {
  size_t i;
  for (i = 0; i < kImage3NumPlanes; i++) {
    jxl_image_f_swap(&self->planes_[i], &other->planes_[i]);
  }
}

static inline void jxl_image3_f_construct_empty(jxl_image3_f* self) {
  size_t i;
  for (i = 0; i < kImage3NumPlanes; i++) {
    jxl_image_f_construct_empty(&self->planes_[i]);
  }
}

static inline void jxl_image3_f_destroy(jxl_image3_f* self) {
  size_t i;
  for (i = 0; i < kImage3NumPlanes; i++) {
    jxl_image_f_destroy(&self->planes_[i]);
  }
}

static inline jxl_status jxl_image3_f_create(jxl_context* ctx,
                                   const size_t xsize, const size_t ysize,
                                   jxl_image3_f* out) {
  jxl_image_f plane0;
  jxl_image_f plane1;
  jxl_image_f plane2;
  jxl_image3_f tmp;
  jxl_status status;
  jxl_image_f_construct_empty(&plane0);
  jxl_image_f_construct_empty(&plane1);
  jxl_image_f_construct_empty(&plane2);
  status = jxl_image_f_create(ctx, xsize, ysize, 0, &plane0);
  if (!jxl_status_ok(status)) {
    jxl_image_f_destroy(&plane0);
    jxl_image_f_destroy(&plane1);
    jxl_image_f_destroy(&plane2);
    return status;
  }
  status = jxl_image_f_create(ctx, xsize, ysize, 0, &plane1);
  if (!jxl_status_ok(status)) {
    jxl_image_f_destroy(&plane0);
    jxl_image_f_destroy(&plane1);
    jxl_image_f_destroy(&plane2);
    return status;
  }
  status = jxl_image_f_create(ctx, xsize, ysize, 0, &plane2);
  if (!jxl_status_ok(status)) {
    jxl_image_f_destroy(&plane0);
    jxl_image_f_destroy(&plane1);
    jxl_image_f_destroy(&plane2);
    return status;
  }
  jxl_image3_f_construct_empty(&tmp);
  jxl_image_f_swap(&tmp.planes_[0], &plane0);
  jxl_image_f_swap(&tmp.planes_[1], &plane1);
  jxl_image_f_swap(&tmp.planes_[2], &plane2);
  jxl_image3_f_swap(out, &tmp);
  jxl_image3_f_destroy(&tmp);
  jxl_image_f_destroy(&plane0);
  jxl_image_f_destroy(&plane1);
  jxl_image_f_destroy(&plane2);
  return jxl_ok_status();
}

static inline uint8_t* jxl_rect_row_b(const jxl_rect* self, jxl_image_b* image, size_t y) {
  return jxl_image_b_row(image, y + self->y0_) + self->x0_;
}
static inline const uint8_t* jxl_rect_const_row_b(const jxl_rect* self, const jxl_image_b* image,
                                           size_t y) {
  return jxl_image_b_const_row(image, y + self->y0_) + self->x0_;
}
static inline int32_t* jxl_rect_row_i(const jxl_rect* self, jxl_image_i* image, size_t y) {
  return jxl_image_i_row(image, y + self->y0_) + self->x0_;
}
static inline const int32_t* jxl_rect_const_row_i(const jxl_rect* self, const jxl_image_i* image,
                                           size_t y) {
  return jxl_image_i_const_row(image, y + self->y0_) + self->x0_;
}
static inline const int8_t* jxl_rect_const_row_sb(const jxl_rect* self,
                                           const jxl_image_sb* image, size_t y) {
  return jxl_image_sb_const_row(image, y + self->y0_) + self->x0_;
}
static inline const float* jxl_rect_const_plane_row(const jxl_rect* self,
                                             const jxl_image3_f* image, size_t c,
                                             size_t y) {
  return jxl_image3_f_const_plane_row(image, c, y + self->y0_) + self->x0_;
}

#endif  // LIB_JXL_IMAGE_H_
