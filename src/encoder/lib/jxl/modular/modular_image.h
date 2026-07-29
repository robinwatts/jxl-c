// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MODULAR_MODULAR_IMAGE_H_
#define LIB_JXL_MODULAR_MODULAR_IMAGE_H_

#include <jxl/context.h>
#include "lib/jxl/enc_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/enc_image.h"


typedef int32_t pixel_type;  // can use int16_t if it's only for 8-bit images.
                             // Need some wiggle room for YCoCg / Squeeze etc

typedef int64_t pixel_type_w;

typedef struct jxl_channel {
  jxl_image_i plane;
  size_t w;
  size_t h;
  int hshift;  // w ~= image.w >> hshift;  h ~= image.h >> vshift
  int vshift;
  int component;
} jxl_channel;

jxl_enc_status jxl_channel_create(jxl_context* ctx, size_t iw, size_t ih,
                     int hsh, int vsh, jxl_channel* out);

static inline void jxl_channel_construct_empty(jxl_channel* self) {
  self->plane.plane.xsize_ = 0;
  self->plane.plane.ysize_ = 0;
  self->plane.plane.bytes_per_row_ = 0;
  self->plane.plane.sizeof_t_ = 0;
  jxl_aligned_memory_construct_empty(&self->plane.plane.bytes_);
  self->w = 0;
  self->h = 0;
  self->hshift = 0;
  self->vshift = 0;
  self->component = -1;
}
static inline void jxl_channel_destroy(jxl_channel* self) {
  jxl_image_i_destroy(&self->plane);
  self->w = 0;
  self->h = 0;
  self->hshift = 0;
  self->vshift = 0;
  self->component = -1;
}
static inline pixel_type* jxl_channel_row(jxl_channel* self, size_t y) {
  return jxl_image_i_row(&self->plane, y);
}
static inline const pixel_type* jxl_channel_row_const(const jxl_channel* self, size_t y) {
  return jxl_image_i_const_row(&self->plane, y);
}
static inline jxl_context* jxl_channel_ctx(const jxl_channel* self) {
  return jxl_image_i_ctx(&self->plane);
}
static inline jxl_enc_status jxl_channel_shrink(jxl_channel* self) {
  if (jxl_image_i_x_size(&self->plane) == self->w && jxl_image_i_y_size(&self->plane) == self->h) {
    return jxl_enc_ok_status();
  }
  JXL_RETURN_IF_ERROR(
      jxl_image_i_create(jxl_channel_ctx(self), self->w, self->h, 0,
                     &self->plane));
  return jxl_enc_ok_status();
}
static inline jxl_enc_status jxl_channel_shrink_to(jxl_channel* self, int nw, int nh) {
  self->w = nw;
  self->h = nh;
  return jxl_channel_shrink(self);
}
static inline void jxl_channel_swap(jxl_channel* self, jxl_channel* other) {
    size_t tw = self->w;
    self->w = other->w;
    other->w = tw;
    size_t th = self->h;
    self->h = other->h;
    other->h = th;
    int tsh = self->hshift;
    self->hshift = other->hshift;
    other->hshift = tsh;
    tsh = self->vshift;
    self->vshift = other->vshift;
    other->vshift = tsh;
    int tc = self->component;
    self->component = other->component;
    other->component = tc;
    jxl_image_i_swap(&self->plane, &other->plane);
  }


// Move-only list of jxl_channels (was MoveArray<jxl_channel>).
typedef struct jxl_channels {
  jxl_context* ctx;
  jxl_channel* ptr;
  size_t len;
  size_t capacity;

} jxl_channels;

static inline size_t jxl_channels_size(const jxl_channels* self) { return self->len; }
static inline bool jxl_channels_empty(const jxl_channels* self) { return self->len == 0; }
static inline jxl_channel* jxl_channels_data(jxl_channels* self) { return self->ptr; }
static inline const jxl_channel* jxl_channels_data_const(const jxl_channels* self) { return self->ptr; }
static inline jxl_channel* jxl_channels_at(jxl_channels* self, size_t i) { return &self->ptr[i]; }
static inline const jxl_channel* jxl_channels_at_const(const jxl_channels* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_channels_construct_empty(jxl_channels* self) {
  self->ctx = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_channels_clear(jxl_channels* self) {
  for (size_t i = 0; i < self->len; ++i) {
    jxl_channel_destroy(self->ptr + i);
  }
  self->len = 0;
}

static inline jxl_enc_status jxl_channels_reserve(jxl_channels* self, size_t new_capacity) {
  if (new_capacity <= self->capacity) return jxl_enc_ok_status();

  size_t grown = self->capacity;
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

  size_t bytes;
  if (!jxl_safe_mul(grown, sizeof(jxl_channel), &bytes)) {
    return JXL_FAILURE("jxl_channels::reserve: size overflow");
  }
  jxl_channel* neu;
  if (self->ctx == NULL) {
    return JXL_FAILURE("jxl_channels::reserve: missing memory manager");
  }
  neu = (jxl_channel*)(
      jxl_alloc(self->ctx, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_channels::reserve: allocation failed");
  }
  for (size_t i = 0; i < self->len; ++i) {
    jxl_channel_construct_empty(neu + i);
    jxl_channel_swap(neu + i, &self->ptr[i]);
    jxl_channel_destroy(self->ptr + i);
  }
  if (self->ptr != NULL) {
    jxl_free(self->ctx, self->ptr);
  }
  self->ptr = neu;
  self->capacity = grown;
  return jxl_enc_ok_status();
}

static inline void jxl_channels_destroy(jxl_channels* self) {
  jxl_channels_clear(self);
  if (self->ptr != NULL) {
    if (self->ctx != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}
static inline jxl_channel* jxl_channels_back(jxl_channels* self) {
  JXL_DASSERT(self != NULL && self->len > 0);
  return &self->ptr[self->len - 1];
}
static inline jxl_enc_status jxl_channels_emplace_back(jxl_channels* self, jxl_channel* value) {
  if (self->len == self->capacity) {
    size_t need;
    if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
      return JXL_FAILURE("jxl_channels::emplace_back: overflow");
    }
    JXL_RETURN_IF_ERROR(jxl_channels_reserve(self, need));
  }
  jxl_channel_construct_empty(self->ptr + self->len);
  jxl_channel_swap(self->ptr + self->len, value);
  ++self->len;
  return jxl_enc_ok_status();
}

static inline jxl_enc_status jxl_channels_push_back(jxl_channels* self, jxl_channel* value) {
  if (self->len == self->capacity) {
    size_t need;
    if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
      return JXL_FAILURE("jxl_channels::push_back: overflow");
    }
    JXL_RETURN_IF_ERROR(jxl_channels_reserve(self, need));
  }
  jxl_channel_construct_empty(self->ptr + self->len);
  jxl_channel_swap(self->ptr + self->len, value);
  ++self->len;
  return jxl_enc_ok_status();
}

static inline void jxl_channels_swap(jxl_channels* self, jxl_channels* other) {
  jxl_channel* tp = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tp;
  size_t tl = self->len;
  self->len = other->len;
  other->len = tl;
  size_t tc = self->capacity;
  self->capacity = other->capacity;
  other->capacity = tc;
  jxl_context* tm = self->ctx;
  self->ctx = other->ctx;
  other->ctx = tm;
}




typedef struct jxl_image {
  // image data
  jxl_channels channel;

  // image dimensions (channels may have different dimensions)
  size_t w;
  size_t h;
  int bitdepth;
  size_t nb_meta_channels;  // first few channels might contain palette(s)
  bool error;            // true if a fatal error occurred, false otherwise


  jxl_context* ctx_;
} jxl_image;

void jxl_image_init(jxl_image* self, jxl_context* ctx);
void jxl_image_init_dims(jxl_image* self, jxl_context* ctx, size_t iw,
                   size_t ih, int bitdepth);
jxl_enc_status jxl_image_create(jxl_context* ctx, size_t iw, size_t ih,
                   int bitdepth, int nb_chans, jxl_image* out);

static inline void jxl_image_construct_empty(jxl_image* self) {
  jxl_channels_construct_empty(&self->channel);
  self->w = 0;
  self->h = 0;
  self->bitdepth = 8;
  self->nb_meta_channels = 0;
  self->error = true;
  self->ctx_ = NULL;
}
static inline void jxl_image_destroy(jxl_image* self) {
  jxl_channels_destroy(&self->channel);
  self->w = 0;
  self->h = 0;
  self->bitdepth = 8;
  self->nb_meta_channels = 0;
  self->error = true;
  self->ctx_ = NULL;
}
static inline bool jxl_image_empty(const jxl_image* self) {
  for (size_t ch_i = 0; ch_i < jxl_channels_size(&self->channel); ++ch_i) {
    const jxl_channel* ch = jxl_channels_at_const(&self->channel, ch_i);
    if (ch->w && ch->h) return false;
  }
  return true;
}
static inline jxl_context* jxl_image_ctx(const jxl_image* self) {
  return self->ctx_;
}
static inline void jxl_image_swap(jxl_image* self, jxl_image* other) {
    size_t tw = self->w;
    self->w = other->w;
    other->w = tw;
    size_t th = self->h;
    self->h = other->h;
    other->h = th;
    int td = self->bitdepth;
    self->bitdepth = other->bitdepth;
    other->bitdepth = td;
    size_t tn = self->nb_meta_channels;
    self->nb_meta_channels = other->nb_meta_channels;
    other->nb_meta_channels = tn;
    bool te = self->error;
    self->error = other->error;
    other->error = te;
    jxl_channels_swap(&self->channel, &other->channel);
    jxl_context* tm = self->ctx_;
    self->ctx_ = other->ctx_;
    other->ctx_ = tm;
  }


// Move-only list of modular jxl_images (was MoveArray<jxl_image>).
typedef struct jxl_images {
  jxl_context* ctx;
  jxl_image* ptr;
  size_t len;
  size_t capacity;

} jxl_images;

static inline size_t jxl_images_size(const jxl_images* self) { return self->len; }
static inline bool jxl_images_empty(const jxl_images* self) { return self->len == 0; }
static inline jxl_image* jxl_images_data(jxl_images* self) { return self->ptr; }
static inline const jxl_image* jxl_images_data_const(const jxl_images* self) { return self->ptr; }
static inline jxl_image* jxl_images_at(jxl_images* self, size_t i) { return &self->ptr[i]; }
static inline const jxl_image* jxl_images_at_const(const jxl_images* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_images_construct_empty(jxl_images* self) {
  self->ctx = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline jxl_enc_status jxl_images_reserve(jxl_images* self, size_t new_capacity) {
  if (new_capacity <= self->capacity) return jxl_enc_ok_status();

  size_t grown = self->capacity;
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

  size_t bytes;
  if (!jxl_safe_mul(grown, sizeof(jxl_image), &bytes)) {
    return JXL_FAILURE("jxl_images::reserve: size overflow");
  }
  jxl_image* neu;
  if (self->ctx == NULL) {
    return JXL_FAILURE("jxl_images::reserve: missing memory manager");
  }
  neu = (jxl_image*)(
      jxl_alloc(self->ctx, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_images::reserve: allocation failed");
  }
  for (size_t i = 0; i < self->len; ++i) {
    jxl_image_construct_empty(neu + i);
    jxl_image_init(neu + i, NULL);
    jxl_image_swap(neu + i, &self->ptr[i]);
    jxl_image_destroy(self->ptr + i);
  }
  if (self->ptr != NULL) {
    jxl_free(self->ctx, self->ptr);
  }
  self->ptr = neu;
  self->capacity = grown;
  return jxl_enc_ok_status();
}

static inline void jxl_images_destroy(jxl_images* self) {
  for (size_t i = 0; i < self->len; ++i) {
    jxl_image_destroy(self->ptr + i);
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
static inline jxl_enc_status jxl_images_emplace_back(jxl_images* self, jxl_context* mm) {
  if (self->len == self->capacity) {
    size_t need;
    if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
      return JXL_FAILURE("jxl_images::emplace_back: overflow");
    }
    JXL_RETURN_IF_ERROR(jxl_images_reserve(self, need));
  }
  jxl_image_construct_empty(self->ptr + self->len);
  jxl_image_init(self->ptr + self->len, mm);
  ++self->len;
  return jxl_enc_ok_status();
}

static inline void jxl_images_swap(jxl_images* self, jxl_images* other) {
  jxl_image* tp = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tp;
  size_t tl = self->len;
  self->len = other->len;
  other->len = tl;
  size_t tc = self->capacity;
  self->capacity = other->capacity;
  other->capacity = tc;
  jxl_context* tm = self->ctx;
  self->ctx = other->ctx;
  other->ctx = tm;
}




#endif  // LIB_JXL_MODULAR_MODULAR_IMAGE_H_
