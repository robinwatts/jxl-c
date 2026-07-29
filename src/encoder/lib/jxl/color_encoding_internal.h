// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef LIB_JXL_COLOR_ENCODING_INTERNAL_H_
#define LIB_JXL_COLOR_ENCODING_INTERNAL_H_

// Encoder metadata color encoding: fields/visitor wrapper over
// jxl_cms_color_encoding (storage_). Not the public jxl_color_encoding POD.
// See color_encoding_cms.h for the three-layer map.

#include <jxl/cms_interface.h>
#include <jxl/color_encoding.h>
#include <jxl/types.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/cms/color_encoding_cms.h"
#include "lib/jxl/cms/jxl_cms_internal.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"

static inline const char* jxl_enum_name_color_space(void) {
  return "jxl_color_space";
}
static inline uint64_t jxl_enum_bits_color_space(void) {
  return jxl_make_bit((uint32_t)(kRGB)) |
         jxl_make_bit((uint32_t)(kGray)) |
         jxl_make_bit((uint32_t)(kXYB)) |
         jxl_make_bit((uint32_t)(kColorSpaceUnknown));
}

static inline const char* jxl_enum_name_white_point(void) {
  return "jxl_white_point";
}
static inline uint64_t jxl_enum_bits_white_point(void) {
  return jxl_make_bit((uint32_t)(kWhitePointD65)) |
         jxl_make_bit((uint32_t)(kWhitePointCustom)) |
         jxl_make_bit((uint32_t)(kWhitePointE)) |
         jxl_make_bit((uint32_t)(kWhitePointDCI));
}

static inline const char* jxl_enum_name_primaries(void) { return "jxl_primaries"; }
static inline uint64_t jxl_enum_bits_primaries(void) {
  return jxl_make_bit((uint32_t)(kPrimariesSRGB)) |
         jxl_make_bit((uint32_t)(kPrimariesCustom)) |
         jxl_make_bit((uint32_t)(kPrimaries2100)) |
         jxl_make_bit((uint32_t)(kPrimariesP3));
}

static inline const char* jxl_enum_name_transfer_function(void) {
  return "jxl_transfer_function";
}

static inline uint64_t jxl_enum_bits_transfer_function(void) {
  return jxl_make_bit((uint32_t)(kTF709)) |
         jxl_make_bit((uint32_t)(kTFLinear)) |
         jxl_make_bit((uint32_t)(kTFSRGB)) |
         jxl_make_bit((uint32_t)(kTFPQ)) |
         jxl_make_bit((uint32_t)(kTFDCI)) |
         jxl_make_bit((uint32_t)(kTFHLG)) |
         jxl_make_bit((uint32_t)(kTFUnknown));
}

static inline const char* jxl_enum_name_rendering_intent(void) {
  return "jxl_rendering_intent";
}
static inline uint64_t jxl_enum_bits_rendering_intent(void) {
  return jxl_make_bit((uint32_t)(kPerceptual)) |
         jxl_make_bit((uint32_t)(kRelative)) |
         jxl_make_bit((uint32_t)(kSaturation)) |
         jxl_make_bit((uint32_t)(kAbsolute));
}

// Serializable form of jxl_ci_exy.
typedef struct jxl_customxy {
  jxl_fields fields;

  jxl_cms_customxy storage_;
} jxl_customxy;

jxl_enc_status jxl_customxy_visit_fields(jxl_customxy* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_customxy)

static inline void jxl_customxy_init(jxl_customxy* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_customxy, &self->fields);
  jxl_bundle_init(&self->fields);
}

typedef struct jxl_custom_transfer_function {
  jxl_fields fields;

  // Must be set before calling VisitFields!
  jxl_color_space nonserialized_color_space;

  jxl_cms_custom_transfer_function storage_;
} jxl_custom_transfer_function;

jxl_enc_status jxl_custom_transfer_function_visit_fields(jxl_custom_transfer_function* self,
                                         jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_custom_transfer_function)

static inline void jxl_custom_transfer_function_init(jxl_custom_transfer_function* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_custom_transfer_function, &self->fields);
  jxl_bundle_init(&self->fields);
}

static inline bool jxl_custom_transfer_function_set_implicit(
    jxl_custom_transfer_function* self) {
  if (self->nonserialized_color_space == kXYB) {
    if (!jxl_enc_status_ok(
            jxl_cms_custom_transfer_function_set_gamma(&self->storage_, 1.0 / 3))) {
      return false;
    }
    return true;
  }
  return false;
}

// Compact encoding of data required to interpret and translate pixels to a
// known color space. Stored in Metadata. Thread-compatible.
typedef struct jxl_enc_color_encoding {
  jxl_fields fields;

  bool all_default;

  // If true, the codestream contains an ICC profile and we do not serialize
  // fields. Otherwise, fields are serialized and we create an ICC profile.
  bool want_icc_;

  jxl_cms_color_encoding storage_;
  // Only used if white_point == kCustom.
  jxl_customxy white_;

  jxl_custom_transfer_function tf_;

  // Only used if primaries == kCustom.
  jxl_customxy red_;
  jxl_customxy green_;
  jxl_customxy blue_;
} jxl_enc_color_encoding;

jxl_enc_status jxl_enc_color_encoding_visit_fields(jxl_enc_color_encoding* self,
                                jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_enc_color_encoding)

void jxl_enc_color_encoding_create_c2(jxl_primaries pr, jxl_transfer_function tf,
                           jxl_context* mm, jxl_enc_color_encoding out[2]);

static inline void jxl_enc_color_encoding_construct_empty(
    jxl_enc_color_encoding* self, jxl_context* mm) {
  jxl_fields_construct_empty(&self->fields);
  jxl_fields_construct_empty(&self->white_.fields);
  jxl_fields_construct_empty(&self->tf_.fields);
  jxl_fields_construct_empty(&self->red_.fields);
  jxl_fields_construct_empty(&self->green_.fields);
  jxl_fields_construct_empty(&self->blue_.fields);
  self->tf_.nonserialized_color_space = kRGB;
  self->want_icc_ = false;
  jxl_cms_color_encoding_construct_empty(&self->storage_, mm);
}
static inline void jxl_enc_color_encoding_destroy(jxl_enc_color_encoding* self) {
  jxl_array_destroy(&self->storage_.icc);
}
static inline jxl_enc_status jxl_enc_color_encoding_copy_from(jxl_enc_color_encoding* self,
                                           const jxl_enc_color_encoding* other) {
  if (self == other) return jxl_enc_ok_status();
  jxl_array_destroy(&self->storage_.icc);
  memcpy(self, other, sizeof(*self));
  jxl_array_construct_empty(&self->storage_.icc, other->storage_.icc.ctx);
  return jxl_array_copy_from(&self->storage_.icc, &other->storage_.icc);
}
static inline void jxl_enc_color_encoding_init(jxl_enc_color_encoding* self) {
  jxl_customxy_init(&self->white_);
  jxl_custom_transfer_function_init(&self->tf_);
  jxl_customxy_init(&self->red_);
  jxl_customxy_init(&self->green_);
  jxl_customxy_init(&self->blue_);
  JXL_FIELDS_REGISTER_PTR(jxl_enc_color_encoding, &self->fields);
  jxl_bundle_init(&self->fields);
}

static inline jxl_color_encoding jxl_enc_color_encoding_to_external(
    const jxl_enc_color_encoding* self) {
  return jxl_cms_color_encoding_to_external(&self->storage_);
}

static inline jxl_enc_status jxl_enc_color_encoding_create_icc(jxl_enc_color_encoding* self) {
  jxl_array_clear(&self->storage_.icc);
  const jxl_color_encoding external = jxl_enc_color_encoding_to_external(self);
  if (!jxl_enc_status_ok(jxl_maybe_create_profile(&external, &self->storage_.icc))) {
    jxl_array_clear(&self->storage_.icc);
    return JXL_FAILURE("Failed to create ICC profile");
  }
  return jxl_enc_ok_status();
}

static inline const jxl_icc_bytes* jxl_enc_color_encoding_icc(const jxl_enc_color_encoding* self) {
  return &self->storage_.icc;
}

static inline jxl_enc_status jxl_enc_color_encoding_set_icc(jxl_enc_color_encoding* self, jxl_icc_bytes* icc,
                                         const jxl_cms_interface* cms) {
  JXL_ENSURE(cms != NULL);
  JXL_ENSURE(icc != NULL);
  JXL_ENSURE(!jxl_array_empty(icc));
  self->storage_.have_fields = true;
  self->want_icc_ =
      jxl_enc_status_ok(jxl_cms_color_encoding_set_fields_from_icc(&self->storage_, icc, cms));
  return jxl_enc_status_from_bool(self->want_icc_);
}

static inline bool jxl_enc_color_encoding_want_icc(const jxl_enc_color_encoding* self) {
  return self->want_icc_;
}

static inline size_t jxl_enc_color_encoding_channels(const jxl_enc_color_encoding* self) {
  return jxl_cms_color_encoding_channels(&self->storage_);
}

static inline bool jxl_enc_color_encoding_has_primaries(const jxl_enc_color_encoding* self) {
  return jxl_cms_color_encoding_has_primaries(&self->storage_);
}

static inline bool jxl_enc_color_encoding_implicit_white_point(jxl_enc_color_encoding* self) {
  if (self->storage_.color_space == kXYB) {
    self->storage_.white_point = kWhitePointD65;
    return true;
  }
  return false;
}

static inline void jxl_enc_color_encoding_set_color_space(jxl_enc_color_encoding* self,
                                              jxl_color_space cs) {
  self->storage_.color_space = cs;
}

static inline const jxl_cms_custom_transfer_function* jxl_enc_color_encoding_tf(
    const jxl_enc_color_encoding* self) {
  return &self->storage_.tf;
}

#endif  // LIB_JXL_COLOR_ENCODING_INTERNAL_H_
