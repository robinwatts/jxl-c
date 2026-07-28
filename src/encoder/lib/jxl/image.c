// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/image.h"

#include "lib/jxl/memory_manager.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/status.h"
#include "lib/jxl/memory_manager_internal.h"

#if defined(MEMORY_SANITIZER)
#include <string.h>
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/sanitizers.h"
#include "lib/jxl/simd_util.h"
#endif


// Initializes the minimum bytes required to suppress MSAN warnings from
// legitimate vector loads/stores on the right border, where some lanes are
// uninitialized and assumed to be unused.
static void jxl_initialize_padding(jxl_plane_base* plane, const size_t sizeof_t) {
#if defined(MEMORY_SANITIZER)
  size_t xsize = jxl_plane_base_x_size(plane);
  size_t ysize = jxl_plane_base_y_size(plane);
  if (xsize == 0 || ysize == 0) return;

  const size_t vec_size = jxl_max_vector_size();
  if (vec_size == 0) return;  // Scalar mode: no padding needed

  const size_t valid_size = xsize * sizeof_t;
  const size_t initialize_size = jxl_round_up_to(valid_size, vec_size);
  if (valid_size == initialize_size) return;

  for (size_t y = 0; y < ysize; ++y) {
    uint8_t* JXL_RESTRICT row = jxl_plane_base_bytes(plane) + y * jxl_plane_base_bytes_per_row(plane);
    memset(row + valid_size, kMsanSentinelByte,
           initialize_size - valid_size);
  }
#endif  // MEMORY_SANITIZER
}

void jxl_plane_base_init(jxl_plane_base* self, const uint32_t xsize, const uint32_t ysize,
                   const size_t sizeof_t) {
  self->xsize_ = xsize;
  self->ysize_ = ysize;
  self->bytes_per_row_ = 0;
  self->sizeof_t_ = sizeof_t;
}

jxl_status jxl_plane_base_allocate(jxl_plane_base* self, jxl_memory_manager* memory_manager,
                         size_t pre_padding) {
  JXL_ENSURE(jxl_aligned_memory_address(&self->bytes_) == NULL);
  JXL_ENSURE(self->bytes_per_row_ == 0);

  JXL_RETURN_IF_ERROR(jxl_bytes_per_row(self->xsize_, self->sizeof_t_,
                                  &self->bytes_per_row_));

  // Dimensions can be zero, e.g. for lazily-allocated images. Only allocate
  // if nonzero, because "zero" bytes still have padding/bookkeeping overhead.
  if (self->xsize_ == 0 || self->ysize_ == 0) {
    return jxl_ok_status();
  }

  size_t total_bytes;
  if (!jxl_safe_mul(self->ysize_, self->bytes_per_row_, &total_bytes)) {
    return JXL_FAILURE("jxl_image dimensions are too large");
  }

  JXL_RETURN_IF_ERROR(jxl_aligned_memory_create(memory_manager, total_bytes,
                                              pre_padding * self->sizeof_t_,
                                              &self->bytes_));

  jxl_initialize_padding(self, self->sizeof_t_);

  return jxl_ok_status();
}

