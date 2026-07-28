// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/modular/modular_image.h"

#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <stddef.h>

#include "lib/jxl/image.h"

#include "lib/jxl/base/status.h"


jxl_status jxl_channel_create(jxl_context *ctx, size_t iw, size_t ih,
                     int hsh, int vsh, jxl_channel *out) {
  jxl_image_i plane;
  jxl_image_i_construct_empty(&plane);
  jxl_status status = jxl_image_i_create(ctx, iw, ih, 0, &plane);
  if (!jxl_status_ok(status)) {
    jxl_image_i_destroy(&plane);
    return status;
  }
  jxl_channel tmp;
  jxl_channel_construct_empty(&tmp);
  tmp.w = iw;
  tmp.h = ih;
  tmp.hshift = hsh;
  tmp.vshift = vsh;
  jxl_image_i_swap(&tmp.plane, &plane);
  jxl_channel_swap(out, &tmp);
  jxl_channel_destroy(&tmp);
  jxl_image_i_destroy(&plane);
  return jxl_ok_status();
}

void jxl_image_init(jxl_image *self, jxl_context *ctx) {
  self->w = 0;
  self->h = 0;
  self->bitdepth = 8;
  self->nb_meta_channels = 0;
  self->error = true;
  self->ctx_ = ctx;
  self->channel.ctx = ctx;
}

void jxl_image_init_dims(jxl_image *self, jxl_context *ctx, size_t iw,
                   size_t ih, int bitdepth) {
  self->w = iw;
  self->h = ih;
  self->bitdepth = bitdepth;
  self->nb_meta_channels = 0;
  self->error = false;
  self->ctx_ = ctx;
  self->channel.ctx = ctx;
}

jxl_status jxl_image_create(jxl_context *ctx, size_t iw, size_t ih,
                   int bitdepth, int nb_chans, jxl_image *out) {
  jxl_image result;
  jxl_image_construct_empty(&result);
  jxl_image_init_dims(&result, ctx, iw, ih, bitdepth);
  for (int i = 0; i < nb_chans; i++) {
    jxl_channel c;
    jxl_channel_construct_empty(&c);
    jxl_status status = jxl_channel_create(ctx, iw, ih, 0, 0, &c);
    if (!jxl_status_ok(status)) {
      jxl_channel_destroy(&c);
      jxl_image_destroy(&result);
      return status;
    }
    status = jxl_channels_emplace_back(&result.channel, &c);
    jxl_channel_destroy(&c);
    if (!jxl_status_ok(status)) {
      jxl_image_destroy(&result);
      return status;
    }
    jxl_channels_back(&result.channel)->component = i;
  }
  jxl_image_swap(out, &result);
  jxl_image_destroy(&result);
  return jxl_ok_status();
}

