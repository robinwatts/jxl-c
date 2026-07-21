// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_HEADERS_H_
#define LIB_JXL_HEADERS_H_

// Codestream headers.

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"

// Reserved by ISO/IEC 10918-1. LF causes files opened in text mode to be
// rejected because the marker changes to 0x0D instead. The 0xFF prefix also
// ensures there were no 7-bit transmission limitations.
enum { kCodestreamMarker = 0x0A };

// Compact representation of image dimensions (best case: 9 bits) so decoders
// can preallocate early.
typedef struct jxl_size_header {
  jxl_fields fields;

  bool small_;  // xsize and ysize <= 256 and divisible by 8.

  uint32_t ysize_div8_minus_1_;
  uint32_t ysize_;

  uint32_t ratio_;
  uint32_t xsize_div8_minus_1_;
  uint32_t xsize_;
} jxl_size_header;

jxl_status jxl_size_header_visit_fields(jxl_size_header* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_size_header)

jxl_status jxl_size_header_set(jxl_size_header* self, size_t xsize, size_t ysize);
size_t jxl_size_header_x_size(const jxl_size_header* self);

static inline void jxl_size_header_init(jxl_size_header* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_size_header, &self->fields);
  jxl_bundle_init(&self->fields);
}

static inline size_t jxl_size_header_y_size(const jxl_size_header* self) {
  return self->small_ ? ((self->ysize_div8_minus_1_ + 1) * 8) : self->ysize_;
}

// (Similar to jxl_size_header but different encoding because previews are smaller)
typedef struct jxl_preview_header {
  jxl_fields fields;

  bool div8_;  // xsize and ysize divisible by 8.

  uint32_t ysize_div8_;
  uint32_t ysize_;

  uint32_t ratio_;
  uint32_t xsize_div8_;
  uint32_t xsize_;
} jxl_preview_header;

jxl_status jxl_preview_header_visit_fields(jxl_preview_header* self,
                                jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_preview_header)

size_t jxl_preview_header_x_size(const jxl_preview_header* self);

static inline void jxl_preview_header_init(jxl_preview_header* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_preview_header, &self->fields);
  jxl_bundle_init(&self->fields);
}

static inline size_t jxl_preview_header_y_size(const jxl_preview_header* self) {
  return self->div8_ ? (self->ysize_div8_ * 8) : self->ysize_;
}

typedef struct jxl_animation_header {
  jxl_fields fields;

  // Ticks per second (expressed as rational number to support NTSC)
  uint32_t tps_numerator;
  uint32_t tps_denominator;

  uint32_t num_loops;  // 0 means to repeat infinitely.

  bool have_timecodes;
} jxl_animation_header;

jxl_status jxl_animation_header_visit_fields(jxl_animation_header* self,
                                  jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_animation_header)

static inline void jxl_animation_header_init(jxl_animation_header* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_animation_header, &self->fields);
  jxl_bundle_init(&self->fields);
}

#endif  // LIB_JXL_HEADERS_H_
