// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Main codestream header bundles, the metadata that applies to all frames.
// Enums must align with the C API definitions in codestream_header.h.

#ifndef LIB_JXL_ENC_IMAGE_METADATA_H_
#define LIB_JXL_ENC_IMAGE_METADATA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/matrix_ops.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/color_encoding_internal.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/headers.h"

#include "lib/jxl/layer_type.h"
// EXIF orientation is stored as uint32_t orientation (1..8, EXIF / jxl_orientation).

typedef enum jxl_extra_channel {
  // Wire-format values; must match jxl_extra_channel_type in full libjxl.
  kAlpha = 0,
  kDepth = 1,
  kSpotColor = 2,
  kSelectionMask = 3,
  kBlack = 4,
  kCFA = 5,
  kThermal = 6,
  kReserved0 = 7,
  kReserved1 = 8,
  kReserved2 = 9,
  kReserved3 = 10,
  kReserved4 = 11,
  kReserved5 = 12,
  kReserved6 = 13,
  kReserved7 = 14,
  kUnknown = 15,
  kOptional = 16,
} jxl_extra_channel;

static inline const char* jxl_enum_name_extra_channel(void) {
  return "jxl_extra_channel";
}
static inline uint64_t jxl_enum_bits_extra_channel(void) {
  return jxl_make_bit((uint32_t)(kAlpha)) |
         jxl_make_bit((uint32_t)(kDepth)) |
         jxl_make_bit((uint32_t)(kSpotColor)) |
         jxl_make_bit((uint32_t)(kSelectionMask)) |
         jxl_make_bit((uint32_t)(kBlack)) |
         jxl_make_bit((uint32_t)(kCFA)) |
         jxl_make_bit((uint32_t)(kThermal)) |
         jxl_make_bit((uint32_t)(kUnknown)) |
         jxl_make_bit((uint32_t)(kOptional));
}

// Used in jxl_image_metadata and jxl_extra_channel_info.
typedef struct jxl_enc_bit_depth {
  jxl_fields fields;

  // Whether the original (uncompressed) samples are floating point or
  // unsigned integer.
  bool floating_point_sample;

  // Bit depth of the original (uncompressed) image samples. Must be in the
  // range [1, 32].
  uint32_t bits_per_sample;

  // Floating point exponent bits of the original (uncompressed) image samples,
  // only used if floating_point_sample is true.
  // If used, the samples are floating point with:
  // - 1 sign bit
  // - exponent_bits_per_sample exponent bits
  // - (bits_per_sample - exponent_bits_per_sample - 1) mantissa bits
  // If used, exponent_bits_per_sample must be in the range
  // [2, 8] and amount of mantissa bits must be in the range [2, 23].
  // NOTE: exponent_bits_per_sample is 8 for single precision binary32
  // point, 5 for half precision binary16, 7 for fp24.
  uint32_t exponent_bits_per_sample;
} jxl_enc_bit_depth;

jxl_enc_status jxl_enc_bit_depth_visit_fields(jxl_enc_bit_depth* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_enc_bit_depth)

static inline void jxl_enc_bit_depth_init(jxl_enc_bit_depth* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_enc_bit_depth, &self->fields);
  jxl_bundle_init(&self->fields);
}

// Describes one extra channel.
typedef struct jxl_extra_channel_info {
  jxl_fields fields;

  bool all_default;

  jxl_extra_channel type;
  jxl_enc_bit_depth bit_depth;
  uint32_t dim_shift;  // downsampled by 2^dim_shift on each axis

  jxl_array_char name;  // UTF-8

  // Conditional:
  bool alpha_associated;  // i.e. premultiplied
  float spot_color[4];    // spot color in linear RGBA
  uint32_t cfa_channel;
} jxl_extra_channel_info;

jxl_enc_status jxl_extra_channel_info_visit_fields(jxl_extra_channel_info* self,
                                   jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_extra_channel_info)

static inline void jxl_extra_channel_info_construct_empty(
    jxl_extra_channel_info* self, jxl_context* mm) {
  jxl_fields_construct_empty(&self->fields);
  jxl_fields_construct_empty(&self->bit_depth.fields);
  jxl_array_construct_empty(&self->name, mm);
}
static inline void jxl_extra_channel_info_destroy(jxl_extra_channel_info* self) {
  jxl_array_destroy(&self->name);
}
static inline void jxl_extra_channel_info_init(jxl_extra_channel_info* self) {
  jxl_enc_bit_depth_init(&self->bit_depth);
  JXL_FIELDS_REGISTER_PTR(jxl_extra_channel_info, &self->fields);
  jxl_bundle_init(&self->fields);
}
static inline void jxl_extra_channel_info_swap(jxl_extra_channel_info* self,
                                        jxl_extra_channel_info* other) {
  size_t i;
  jxl_fields tf = self->fields;
  self->fields = other->fields;
  other->fields = tf;
  bool tad = self->all_default;
  self->all_default = other->all_default;
  other->all_default = tad;
  jxl_extra_channel tt = self->type;
  self->type = other->type;
  other->type = tt;
  jxl_enc_bit_depth tb = self->bit_depth;
  self->bit_depth = other->bit_depth;
  other->bit_depth = tb;
  uint32_t td = self->dim_shift;
  self->dim_shift = other->dim_shift;
  other->dim_shift = td;
  jxl_array_swap(&self->name, &other->name);
  bool ta = self->alpha_associated;
  self->alpha_associated = other->alpha_associated;
  other->alpha_associated = ta;
  for (i = 0; i < 4; ++i) {
    float ts = self->spot_color[i];
    self->spot_color[i] = other->spot_color[i];
    other->spot_color[i] = ts;
  }
  uint32_t tc = self->cfa_channel;
  self->cfa_channel = other->cfa_channel;
  other->cfa_channel = tc;
}

// Growable list of jxl_extra_channel_info (was MoveArray<jxl_extra_channel_info>).
typedef struct jxl_extra_channel_infos {
  jxl_context* ctx;
  jxl_extra_channel_info* ptr;
  size_t len;
  size_t capacity;
} jxl_extra_channel_infos;

static inline void jxl_extra_channel_infos_construct_empty(jxl_extra_channel_infos* self) {
  self->ctx = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}
static inline size_t jxl_extra_channel_infos_size(const jxl_extra_channel_infos* self) {
  return self->len;
}
static inline bool jxl_extra_channel_infos_empty(const jxl_extra_channel_infos* self) {
  return self->len == 0;
}
static inline jxl_extra_channel_info* jxl_extra_channel_infos_data(jxl_extra_channel_infos* self) {
  return self->ptr;
}
static inline const jxl_extra_channel_info* jxl_extra_channel_infos_data_const(
    const jxl_extra_channel_infos* self) {
  return self->ptr;
}
static inline jxl_extra_channel_info* jxl_extra_channel_infos_at(jxl_extra_channel_infos* self,
                                                    size_t i) {
  return &self->ptr[i];
}
static inline const jxl_extra_channel_info* jxl_extra_channel_infos_at_const(
    const jxl_extra_channel_infos* self, size_t i) {
  return &self->ptr[i];
}

static inline void jxl_extra_channel_infos_swap(jxl_extra_channel_infos* self,
                                         jxl_extra_channel_infos* other) {
  jxl_context* tmp_mm = self->ctx;
  self->ctx = other->ctx;
  other->ctx = tmp_mm;
  jxl_extra_channel_info* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}

static inline void jxl_extra_channel_infos_clear(jxl_extra_channel_infos* self) {
  size_t i;
  for (i = 0; i < self->len; ++i) {
    jxl_extra_channel_info_destroy(self->ptr + i);
  }
  self->len = 0;
}

static inline void jxl_extra_channel_infos_destroy(jxl_extra_channel_infos* self) {
  jxl_extra_channel_infos_clear(self);
  if (self->ptr != NULL) {
    if (self->ctx != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline jxl_enc_status jxl_extra_channel_infos_reserve(jxl_extra_channel_infos* self,
                                              size_t new_capacity) {
  size_t grown;
  size_t bytes;
  jxl_extra_channel_info* neu;
  size_t i;
  if (new_capacity <= self->capacity) return jxl_enc_ok_status();

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

  if (!jxl_safe_mul(grown, sizeof(jxl_extra_channel_info), &bytes)) {
    return JXL_FAILURE("jxl_extra_channel_infos::reserve: size overflow");
  }
  if (self->ctx == NULL) {
    return JXL_FAILURE("jxl_extra_channel_infos::reserve: missing memory manager");
  }
  neu = (jxl_extra_channel_info*)(
      jxl_alloc(self->ctx, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_extra_channel_infos::reserve: allocation failed");
  }
  for (i = 0; i < self->len; ++i) {
    jxl_extra_channel_info_construct_empty(neu + i, self->ctx);
    jxl_extra_channel_info_init(neu + i);
    jxl_extra_channel_info_swap(neu + i, &self->ptr[i]);
    jxl_extra_channel_info_destroy(self->ptr + i);
  }
  if (self->ptr != NULL) {
    jxl_free(self->ctx, self->ptr);
  }
  self->ptr = neu;
  self->capacity = grown;
  return jxl_enc_ok_status();
}

static inline jxl_enc_status jxl_extra_channel_infos_resize(jxl_extra_channel_infos* self, size_t n) {
  size_t i;
  if (n < self->len) {
    for (i = n; i < self->len; ++i) {
      jxl_extra_channel_info_destroy(self->ptr + i);
    }
    self->len = n;
    return jxl_enc_ok_status();
  }
  JXL_RETURN_IF_ERROR(jxl_extra_channel_infos_reserve(self, n));
  while (self->len < n) {
    jxl_extra_channel_info_construct_empty(self->ptr + self->len,
                                           self->ctx);
    jxl_extra_channel_info_init(self->ptr + self->len);
    ++self->len;
  }
  return jxl_enc_ok_status();
}

typedef struct jxl_enc_opsin_inverse_matrix {
  jxl_fields fields;

  bool all_default;

  jxl_matrix3x3 inverse_matrix;
  float opsin_biases[3];
  float quant_biases[4];
} jxl_enc_opsin_inverse_matrix;

jxl_enc_status jxl_enc_opsin_inverse_matrix_visit_fields(jxl_enc_opsin_inverse_matrix* self,
                                     jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_enc_opsin_inverse_matrix)

static inline void jxl_enc_opsin_inverse_matrix_init(jxl_enc_opsin_inverse_matrix* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_enc_opsin_inverse_matrix, &self->fields);
  jxl_bundle_init(&self->fields);
}

// Information useful for mapping HDR images to lower dynamic range displays.
typedef struct jxl_tone_mapping {
  jxl_fields fields;

  bool all_default;

  // Upper bound on the intensity level present in the image. For unsigned
  // integer pixel encodings, this is the brightness of the largest
  // representable value. The image does not necessarily contain a pixel
  // actually this bright. An encoder is allowed to set 255 for SDR images
  // without computing a histogram.
  float intensity_target;  // [nits]

  // Lower bound on the intensity level present in the image. This may be
  // loose, i.e. lower than the actual darkest pixel. When tone mapping, a
  // decoder will map [min_nits, intensity_target] to the display range.
  float min_nits;

  bool relative_to_max_display;  // see below
  // The tone mapping will leave unchanged (linear mapping) any pixels whose
  // brightness is strictly below this. The interpretation depends on
  // relative_to_max_display. If true, this is a ratio [0, 1] of the maximum
  // display brightness [nits], otherwise an absolute brightness [nits].
  float linear_below;
} jxl_tone_mapping;

jxl_enc_status jxl_tone_mapping_visit_fields(jxl_tone_mapping* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_tone_mapping)

static inline void jxl_tone_mapping_init(jxl_tone_mapping* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_tone_mapping, &self->fields);
  jxl_bundle_init(&self->fields);
}

// Contains weights to customize some transforms - in particular, XYB and
// upsampling.
typedef struct jxl_custom_transform_data {
  jxl_fields fields;

  // Must be set before calling VisitFields. Must equal xyb_encoded of
  // jxl_image_metadata, should be set by jxl_image_metadata during VisitFields.
  bool nonserialized_xyb_encoded;

  bool all_default;

  jxl_enc_opsin_inverse_matrix opsin_inverse_matrix;

  uint32_t custom_weights_mask;
  float upsampling2_weights[15];
  float upsampling4_weights[55];
  float upsampling8_weights[210];
} jxl_custom_transform_data;

jxl_enc_status jxl_custom_transform_data_visit_fields(jxl_custom_transform_data* self,
                                      jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_custom_transform_data)

static inline void jxl_custom_transform_data_init(jxl_custom_transform_data* self) {
  jxl_enc_opsin_inverse_matrix_init(&self->opsin_inverse_matrix);
  JXL_FIELDS_REGISTER_PTR(jxl_custom_transform_data, &self->fields);
  jxl_bundle_init(&self->fields);
}

// jxl_properties of the original image bundle. This enables Encode(Decode()) to
// re-create an equivalent image without user input.
typedef struct jxl_image_metadata {
  jxl_fields fields;

  bool all_default;

  jxl_enc_bit_depth bit_depth;
  bool modular_16_bit_buffer_sufficient;  // otherwise 32 is.

  // Whether the colors values of the pixels of frames are encoded in the
  // codestream using the absolute XYB color space, or the using values that
  // follow the color space defined by the jxl_enc_color_encoding or ICC profile. This
  // determines when or whether a CMS (jxl_color Management System) is needed to get
  // the pixels in a desired color space. In one case, the pixels have one known
  // color space and a CMS is needed to convert them to the original image's
  // color space, in the other case the pixels have the color space of the
  // original image and a CMS is required if a different display space, or a
  // single known consistent color space for multiple decoded images, is
  // desired. In all cases, the color space of all frames from a single image is
  // the same, both VarDCT and modular frames.
  //
  // If true: then frames can be decoded to XYB (which can also be converted to
  // linear and non-linear sRGB with the built in conversion without CMS). The
  // attached jxl_enc_color_encoding or ICC profile has no effect on the meaning of the
  // pixel's color values, but instead indicates what the color profile of the
  // original image was, and what color profile one should convert to when
  // decoding to integers to prevent clipping and precision loss. To do that
  // conversion requires a CMS.
  //
  // If false: then the color values of decoded frames are in the space defined
  // by the attached jxl_enc_color_encoding or ICC profile. To instead get the pixels in
  // a chosen known color space, such as sRGB, requires a CMS, since the
  // attached jxl_enc_color_encoding or ICC profile could be any arbitrary color space.
  // This mode is typically used for lossless images encoded as integers.
  // Frames can also use YCbCr encoding, some frames may and some may not, but
  // this is not a different color space but a certain encoding of the RGB
  // values.
  //
  // Note: if !xyb_encoded, but the attached color profile indicates XYB (which
  // can happen either if it's a jxl_enc_color_encoding with color_space_ ==
  // kXYB, or if it's an ICC jxl_profile that has been crafted to
  // represent XYB), then the frames still may not use jxl_enc_color_encoding kXYB, they
  // must still use kNone (or kYCbCr, which would mean applying the YCbCr
  // transform to the 3-channel XYB data), since with !xyb_encoded, the 3
  // channels are stored as-is, no matter what meaning the color profile assigns
  // to them. To use kXYB, xyb_encoded must be true.
  //
  // This value is defined in image metadata because this is the global
  // codestream header. This value does not affect the image itself, so is not
  // image metadata per se, it only affects the encoding, and what color space
  // the decoder can receive the pixels in without needing a CMS.
  bool xyb_encoded;

  jxl_enc_color_encoding color_encoding;

  // These values are initialized to defaults such that the 'extra_fields'
  // condition in VisitFields uses correctly initialized values.
  uint32_t orientation;
  bool have_preview;
  bool have_animation;
  bool have_intrinsic_size;

  // If present, the stored image has the dimensions of the first jxl_enc_size_header,
  // but decoders are advised to resample or display per `intrinsic_size`.
  jxl_enc_size_header intrinsic_size;  // only if have_intrinsic_size

  jxl_tone_mapping tone_mapping;

  // When reading: deserialized. When writing: automatically set from size.
  uint32_t num_extra_channels;
  jxl_extra_channel_infos extra_channel_info;

  // Only present if m.have_preview.
  jxl_preview_header preview_size;
  // Only present if m.have_animation.
  jxl_enc_animation_header animation;

  uint64_t extensions;
} jxl_image_metadata;

jxl_enc_status jxl_image_metadata_visit_fields(jxl_image_metadata* self,
                                jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_image_metadata)

jxl_enc_status jxl_write_image_metadata(const jxl_image_metadata* metadata,
                          jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer);

static inline void jxl_image_metadata_set_intensity_target(jxl_image_metadata* self,
                                                   float intensity_target) {
  self->tone_mapping.intensity_target = intensity_target;
}

static inline void jxl_image_metadata_construct_empty(jxl_image_metadata* self,
                                                      jxl_context* mm) {
  jxl_fields_construct_empty(&self->fields);
  jxl_fields_construct_empty(&self->bit_depth.fields);
  jxl_fields_construct_empty(&self->tone_mapping.fields);
  jxl_fields_construct_empty(&self->intrinsic_size.fields);
  jxl_fields_construct_empty(&self->preview_size.fields);
  jxl_fields_construct_empty(&self->animation.fields);
  jxl_enc_color_encoding_construct_empty(&self->color_encoding, mm);
  self->orientation = 1;
  self->have_preview = false;
  self->have_animation = false;
  self->have_intrinsic_size = false;
  jxl_extra_channel_infos_construct_empty(&self->extra_channel_info);
  self->extra_channel_info.ctx = mm;
}
static inline void jxl_image_metadata_destroy(jxl_image_metadata* self) {
  jxl_enc_color_encoding_destroy(&self->color_encoding);
  jxl_extra_channel_infos_destroy(&self->extra_channel_info);
}

static inline void jxl_image_metadata_init(jxl_image_metadata* self) {
  jxl_enc_bit_depth_init(&self->bit_depth);
  jxl_enc_color_encoding_init(&self->color_encoding);
  jxl_tone_mapping_init(&self->tone_mapping);
  jxl_enc_size_header_init(&self->intrinsic_size);
  jxl_preview_header_init(&self->preview_size);
  jxl_enc_animation_header_init(&self->animation);
  JXL_FIELDS_REGISTER_PTR(jxl_image_metadata, &self->fields);
  jxl_bundle_init(&self->fields);
}

// All metadata applicable to the entire codestream (dimensions, extra channels,
// ...)
typedef struct jxl_codec_metadata {
  // TODO(lode): use the preview and animation fields too, in place of the
  // nonserialized_ ones in jxl_image_metadata.
  jxl_image_metadata m;
  // The size of the codestream: this is the nominal size applicable to all
  // frames, although some frames can have a different effective size through
  // crop, dc_level or representing a the preview.
  jxl_enc_size_header size;
  // Often default.
  jxl_custom_transform_data transform_data;
} jxl_codec_metadata;

static inline void jxl_codec_metadata_construct_empty(jxl_codec_metadata* self,
                                                      jxl_context* mm) {
  jxl_image_metadata_construct_empty(&self->m, mm);
  jxl_fields_construct_empty(&self->size.fields);
  jxl_fields_construct_empty(&self->transform_data.fields);
  jxl_fields_construct_empty(&self->transform_data.opsin_inverse_matrix.fields);
  self->transform_data.nonserialized_xyb_encoded = false;
}
static inline void jxl_codec_metadata_destroy(jxl_codec_metadata* self) {
  jxl_image_metadata_destroy(&self->m);
}
static inline void jxl_codec_metadata_init(jxl_codec_metadata* self) {
  jxl_image_metadata_init(&self->m);
  jxl_enc_size_header_init(&self->size);
  jxl_custom_transform_data_init(&self->transform_data);
}

static inline size_t jxl_codec_metadata_x_size(const jxl_codec_metadata* self) {
  return jxl_enc_size_header_x_size(&self->size);
}
static inline size_t jxl_codec_metadata_y_size(const jxl_codec_metadata* self) {
  return jxl_enc_size_header_y_size(&self->size);
}

#endif  // LIB_JXL_ENC_IMAGE_METADATA_H_
