// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_FRAME_HEADER_H_
#define LIB_JXL_FRAME_HEADER_H_

// Frame header with backward and forward-compatible extension capability and
// compressed integer fields.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/coeff_order_fwd.h"
#include "lib/jxl/common.h"  // kMaxNumPasses
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/frame_dimensions.h"
#include "lib/jxl/image_metadata.h"
#include "lib/jxl/loop_filter.h"

typedef enum jxl_color_transform {
  kColorTransformXYB,    // Values are encoded with XYB. May only be used if
                         // jxl_image_bundle::xyb_encoded.
  kColorTransformNone,   // Values are encoded according to the attached color
                         // profile. May only be used if !xyb_encoded.
  kColorTransformYCbCr,  // Values are encoded according to the attached color
                         // profile, but transformed to YCbCr.
} jxl_color_transform;

// Component remapping for JPEG → JXL (YCbCr vs RGB / gray).
static inline void jxl_jpeg_order(jxl_color_transform ct, bool is_gray, int order[3]) {
  if (is_gray) {
    order[0] = order[1] = order[2] = 0;
    return;
  }
  if (ct == kColorTransformYCbCr) {
    order[0] = 1;
    order[1] = 0;
    order[2] = 2;
  } else if (ct == kColorTransformNone) {
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;
  } else {
    JXL_DEBUG_ABORT("Internal logic error");
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;
  }
}

// TODO(eustas): move to proper place?
// Also used by extra channel names.
static inline jxl_status jxl_visit_name_string(jxl_visitor* JXL_RESTRICT visitor,
                                     jxl_array_char* name) {
  uint32_t name_length = (uint32_t)(jxl_array_len(name));
  // Allows layer name lengths up to 1071 bytes
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits(4), jxl_bits_offset(5, 16), jxl_bits_offset(10, 48)), 0, &name_length));
  if (jxl_visitor_is_reading(visitor)) {
    if (!jxl_status_ok(jxl_array_resize_zero(name, name_length))) {
      return JXL_FAILURE("Failed to allocate name string");
    }
  }
  for (size_t i = 0; i < name_length; i++) {
    uint32_t c = (uint8_t)(*jxl_array_at(name, i));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 8, 0, &c));
    *jxl_array_at(name, i) = (char)(c);
  }
  return jxl_ok_status();
}

typedef enum jxl_frame_encoding {
  kVarDCT,
  kModular,
} jxl_frame_encoding;

extern const uint8_t kYCbCrChromaHShift[4];
extern const uint8_t kYCbCrChromaVShift[4];

typedef struct jxl_y_cb_cr_chroma_subsampling {
  jxl_fields fields;

  uint32_t channel_mode_[3];
  uint8_t maxhs_;
  uint8_t maxvs_;
} jxl_y_cb_cr_chroma_subsampling;

static inline void jxl_y_cb_cr_chroma_subsampling_recompute(jxl_y_cb_cr_chroma_subsampling* self) {
  size_t ch_i;
  self->maxhs_ = 0;
  self->maxvs_ = 0;
  for (ch_i = 0; ch_i < 3; ++ch_i) {
    uint32_t ch = self->channel_mode_[ch_i];
    self->maxhs_ = JXL_MAX(self->maxhs_, kYCbCrChromaHShift[ch]);
    self->maxvs_ = JXL_MAX(self->maxvs_, kYCbCrChromaVShift[ch]);
  }
}

static inline jxl_status jxl_y_cb_cr_chroma_subsampling_visit_fields(
    jxl_y_cb_cr_chroma_subsampling* self, jxl_visitor* JXL_RESTRICT visitor) {
  // TODO(veluca): consider allowing 4x downsamples
  size_t ch_i;
  for (ch_i = 0; ch_i < 3; ++ch_i) {
    uint32_t* ch = &self->channel_mode_[ch_i];
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, ch));
  }
  jxl_y_cb_cr_chroma_subsampling_recompute(self);
  return jxl_ok_status();
}
JXL_FIELDS_NAME(jxl_y_cb_cr_chroma_subsampling)

static inline void jxl_y_cb_cr_chroma_subsampling_init(jxl_y_cb_cr_chroma_subsampling* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_y_cb_cr_chroma_subsampling, &self->fields);
  jxl_bundle_init(&self->fields);
}

static inline size_t jxl_y_cb_cr_chroma_subsampling_h_shift(
    const jxl_y_cb_cr_chroma_subsampling* self, size_t c) {
  return self->maxhs_ - kYCbCrChromaHShift[self->channel_mode_[c]];
}
static inline size_t jxl_y_cb_cr_chroma_subsampling_v_shift(
    const jxl_y_cb_cr_chroma_subsampling* self, size_t c) {
  return self->maxvs_ - kYCbCrChromaVShift[self->channel_mode_[c]];
}
static inline uint8_t jxl_y_cb_cr_chroma_subsampling_max_h_shift(
    const jxl_y_cb_cr_chroma_subsampling* self) {
  return self->maxhs_;
}
static inline uint8_t jxl_y_cb_cr_chroma_subsampling_max_v_shift(
    const jxl_y_cb_cr_chroma_subsampling* self) {
  return self->maxvs_;
}

static inline jxl_status jxl_y_cb_cr_chroma_subsampling_set(jxl_y_cb_cr_chroma_subsampling* self,
                                               const uint8_t* hsample,
                                               const uint8_t* vsample) {
  size_t c;
  for (c = 0; c < 3; c++) {
    size_t cjpeg = c < 2 ? c ^ 1 : c;
    size_t i = 0;
    for (; i < 4; i++) {
      if (1 << kYCbCrChromaHShift[i] == hsample[cjpeg] &&
          1 << kYCbCrChromaVShift[i] == vsample[cjpeg]) {
        self->channel_mode_[c] = i;
        break;
      }
    }
    if (i == 4) {
      return JXL_FAILURE("Invalid subsample mode");
    }
  }
  jxl_y_cb_cr_chroma_subsampling_recompute(self);
  return jxl_ok_status();
}
static inline bool jxl_y_cb_cr_chroma_subsampling_is444(
    const jxl_y_cb_cr_chroma_subsampling* self) {
  return jxl_y_cb_cr_chroma_subsampling_h_shift(self, 0) == 0 &&
         jxl_y_cb_cr_chroma_subsampling_v_shift(self, 0) == 0 &&  // Cb
         jxl_y_cb_cr_chroma_subsampling_h_shift(self, 2) == 0 &&
         jxl_y_cb_cr_chroma_subsampling_v_shift(self, 2) == 0 &&  // Cr
         jxl_y_cb_cr_chroma_subsampling_h_shift(self, 1) == 0 &&
         jxl_y_cb_cr_chroma_subsampling_v_shift(self, 1) == 0;  // Y
}

// Indicates how to combine the current frame with a previously-saved one. Can
// be independently controlled for color and extra channels. Formulas are
// indicative and treat alpha as if it is in range 0.0-1.0. In descriptions
// below, alpha channel is the extra channel of type alpha used for blending
// according to the blend_channel, or fully opaque if there is no alpha channel.
// The blending specified here is used for performing blending *after* color
// transforms - in linear sRGB if blending a XYB-encoded frame on another
// XYB-encoded frame, in sRGB if blending a frame with kColorSpace == kSRGB, or
// in the original colorspace otherwise. Blending in XYB or YCbCr is done by
// using patches.
typedef enum jxl_blend_mode {
  // The new values (in the crop) replace the old ones: sample = new
  kReplace = 0,
  // The new values (in the crop) get added to the old ones: sample = old + new
  kAdd = 1,
  // The new values (in the crop) replace the old ones if alpha>0:
  // For the alpha channel that is used as source:
  // alpha = old + new * (1 - old)
  // For other channels if !alpha_associated:
  // sample = ((1 - new_alpha) * old * old_alpha + new_alpha * new) / alpha
  // For other channels if alpha_associated:
  // sample = (1 - new_alpha) * old + new
  // The alpha formula applies to the alpha used for the division in the other
  // channels formula, and applies to the alpha channel itself if its
  // blend_channel value matches itself.
  kBlend = 2,
  // The new values (in the crop) are added to the old ones if alpha>0:
  // For the alpha channel that is used as source:
  // sample = sample = old + new * (1 - old)
  // For other channels: sample = old + alpha * new
  kAlphaWeightedAdd = 3,
  // The new values (in the crop) get multiplied by the old ones:
  // sample = old * new
  // The range of the new value matters for multiplication purposes, and its
  // nominal range of 0..1 is computed the same way as this is done for the
  // alpha values in kBlend and kAlphaWeightedAdd.
  // If using kMul as a blend mode for color channels, no color transform is
  // performed on the current frame.
  kMul = 4,
} jxl_blend_mode;

typedef struct jxl_blending_info {
  jxl_fields fields;

  jxl_blend_mode mode;
  // Which extra channel to use as alpha channel for blending, only encoded
  // for blend modes that involve alpha and if there are more than 1 extra
  // channels.
  uint32_t alpha_channel;
  // Clamp alpha or channel values to 0-1 range.
  bool clamp;
  // Frame ID to copy from (0-3). Only encoded if blend_mode is not kReplace.
  uint32_t source;

  size_t nonserialized_num_extra_channels;
  bool nonserialized_is_partial_frame;
} jxl_blending_info;

jxl_status jxl_blending_info_visit_fields(jxl_blending_info* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_blending_info)

static inline void jxl_blending_info_construct_empty(jxl_blending_info* self) {
  jxl_fields_construct_empty(&self->fields);
  self->nonserialized_num_extra_channels = 0;
  self->nonserialized_is_partial_frame = false;
}
static inline void jxl_blending_info_destroy(jxl_blending_info* self) { (void)self; }
static inline void jxl_blending_info_init(jxl_blending_info* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_blending_info, &self->fields);
  jxl_bundle_init(&self->fields);
}
static inline void jxl_blending_info_swap(jxl_blending_info* self, jxl_blending_info* other) {
  jxl_fields tf = self->fields;
  self->fields = other->fields;
  other->fields = tf;
  jxl_blend_mode tm = self->mode;
  self->mode = other->mode;
  other->mode = tm;
  uint32_t ta = self->alpha_channel;
  self->alpha_channel = other->alpha_channel;
  other->alpha_channel = ta;
  bool tc = self->clamp;
  self->clamp = other->clamp;
  other->clamp = tc;
  uint32_t ts = self->source;
  self->source = other->source;
  other->source = ts;
  size_t tn = self->nonserialized_num_extra_channels;
  self->nonserialized_num_extra_channels =
      other->nonserialized_num_extra_channels;
  other->nonserialized_num_extra_channels = tn;
  bool tp = self->nonserialized_is_partial_frame;
  self->nonserialized_is_partial_frame = other->nonserialized_is_partial_frame;
  other->nonserialized_is_partial_frame = tp;
}

// Growable list of jxl_blending_info (was MoveArray<jxl_blending_info>).
typedef struct jxl_blending_infos {
  jxl_memory_manager* memory_manager;
  jxl_blending_info* ptr;
  size_t len;
  size_t capacity;
} jxl_blending_infos;

static inline size_t jxl_blending_infos_size(const jxl_blending_infos* self) {
  return self->len;
}
static inline bool jxl_blending_infos_empty(const jxl_blending_infos* self) {
  return self->len == 0;
}
static inline jxl_blending_info* jxl_blending_infos_data(jxl_blending_infos* self) {
  return self->ptr;
}
static inline const jxl_blending_info* jxl_blending_infos_data_const(
    const jxl_blending_infos* self) {
  return self->ptr;
}
static inline jxl_blending_info* jxl_blending_infos_at(jxl_blending_infos* self, size_t i) {
  return &self->ptr[i];
}
static inline const jxl_blending_info* jxl_blending_infos_at_const(
    const jxl_blending_infos* self, size_t i) {
  return &self->ptr[i];
}

static inline void jxl_blending_infos_construct_empty(jxl_blending_infos* self) {
  self->memory_manager = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_blending_infos_swap(jxl_blending_infos* self,
                                     jxl_blending_infos* other) {
  jxl_memory_manager* tmp_mm = self->memory_manager;
  self->memory_manager = other->memory_manager;
  other->memory_manager = tmp_mm;
  jxl_blending_info* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}

static inline void jxl_blending_infos_destroy(jxl_blending_infos* self) {
  size_t i;
  for (i = 0; i < self->len; ++i) {
    jxl_blending_info_destroy(self->ptr + i);
  }
  self->len = 0;
  if (self->ptr != NULL) {
    if (self->memory_manager != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}
static inline jxl_status jxl_blending_infos_reserve(jxl_blending_infos* self,
                                          size_t new_capacity) {
  size_t grown;
  size_t bytes;
  jxl_blending_info* neu;
  size_t i;
  if (new_capacity <= self->capacity) return jxl_ok_status();

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

  if (!jxl_safe_mul(grown, sizeof(jxl_blending_info), &bytes)) {
    return JXL_FAILURE("jxl_blending_infos::reserve: size overflow");
  }
  if (self->memory_manager == NULL) {
    return JXL_FAILURE("jxl_blending_infos::reserve: missing memory manager");
  }
  neu = (jxl_blending_info*)(
      self->memory_manager->alloc(self->memory_manager->opaque, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_blending_infos::reserve: allocation failed");
  }
  for (i = 0; i < self->len; ++i) {
    jxl_blending_info_construct_empty(neu + i);
    jxl_blending_info_init(neu + i);
    jxl_blending_info_swap(neu + i, &self->ptr[i]);
    jxl_blending_info_destroy(self->ptr + i);
  }
  if (self->ptr != NULL) {
    self->memory_manager->free(self->memory_manager->opaque, self->ptr);
  }
  self->ptr = neu;
  self->capacity = grown;
  return jxl_ok_status();
}

static inline jxl_status jxl_blending_infos_resize(jxl_blending_infos* self, size_t n) {
  size_t i;
  if (n < self->len) {
    for (i = n; i < self->len; ++i) {
      jxl_blending_info_destroy(self->ptr + i);
    }
    self->len = n;
    return jxl_ok_status();
  }
  JXL_RETURN_IF_ERROR(jxl_blending_infos_reserve(self, n));
  while (self->len < n) {
    jxl_blending_info_construct_empty(self->ptr + self->len);
    jxl_blending_info_init(self->ptr + self->len);
    ++self->len;
  }
  return jxl_ok_status();
}

// Origin of the current frame. Not present for frames of type
// kOnlyPatches.
typedef struct jxl_frame_origin {
  int32_t x0, y0;  // can be negative.
} jxl_frame_origin;

// Size of the current frame.
typedef struct jxl_frame_size {
  uint32_t xsize, ysize;
} jxl_frame_size;

// jxl_animation_frame defines duration of animation frames.
typedef struct jxl_animation_frame {
  jxl_fields fields;

  // How long to wait [in ticks, see Animation{}] after rendering.
  // May be 0 if the current frame serves as a foundation for another frame.
  uint32_t duration;

  uint32_t timecode;  // 0xHHMMSSFF

  // Must be set to the one jxl_image_metadata acting as the full codestream header,
  // with correct xyb_encoded, list of extra channels, etc...
  const jxl_codec_metadata* nonserialized_metadata;
} jxl_animation_frame;

jxl_status jxl_animation_frame_visit_fields(jxl_animation_frame* self,
                                 jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_animation_frame)

static inline void jxl_animation_frame_init(jxl_animation_frame* self,
                                      const jxl_codec_metadata* metadata) {
  self->nonserialized_metadata = metadata;
  JXL_FIELDS_REGISTER_PTR(jxl_animation_frame, &self->fields);
  jxl_bundle_init(&self->fields);
}

// For decoding to lower resolutions. Only used for kRegular frames.
typedef struct jxl_passes {
  jxl_fields fields;

  uint32_t num_passes;      // <= kMaxNumPasses
  uint32_t num_downsample;  // <= num_passes

  // Array of num_downsample pairs. downsample=1/last_pass=num_passes-1 and
  // downsample=8/last_pass=0 need not be specified; they are implicit.
  uint32_t downsample[kMaxNumPasses];
  uint32_t last_pass[kMaxNumPasses];
  // Array of shift values for each pass. It is implicitly assumed to be 0 for
  // the last pass.
  uint32_t shift[kMaxNumPasses];
} jxl_passes;

jxl_status jxl_passes_visit_fields(jxl_passes* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_passes)

static inline void jxl_passes_init(jxl_passes* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_passes, &self->fields);
  jxl_bundle_init(&self->fields);
}

typedef enum jxl_frame_type {
  // A "regular" frame: might be a crop, and will be blended on a previous
  // frame, if any, and displayed or blended in future frames.
  kRegularFrame = 0,
  // A DC frame: this frame is downsampled and will be *only* used as the DC of
  // a future frame and, possibly, for previews. Cannot be cropped, blended, or
  // referenced by patches or blending modes. Frames that *use* a DC frame
  // cannot have non-default sizes either.
  kDCFrame = 1,
  // A PatchesSource frame: this frame will be only used as a source frame for
  // taking patches. Can be cropped, but cannot have non-(0, 0) x0 and y0.
  kReferenceOnly = 2,
  // Same as kRegularFrame, but not used for progressive rendering. This also
  // implies no early display of DC.
  kSkipProgressive = 3,
} jxl_frame_type;

// jxl_image/frame := one of more of these, where the last has is_last = true.
// Starts at a byte-aligned address "a"; the next pass starts at "a + size".

typedef enum jxl_frame_header_flags {
  // Often but not always off => low bit value:

  // Inject noise into decoded output.
  kNoise = 1,

  // Overlay patches.
  kPatches = 2,

  // 4, 8 = reserved for future sometimes-off

  // Overlay splines.
  kSplines = 16,

  kUseDcFrame = 32,  // Implies kSkipAdaptiveDCSmoothing.

  // 64 = reserved for future often-off

  // Almost always on => negated:

  kSkipAdaptiveDCSmoothing = 128,
} jxl_frame_header_flags;

typedef struct jxl_frame_header {
  jxl_fields fields;
  // Optional postprocessing steps. These flags are the source of truth;
  // Override must set/clear them rather than change their meaning. Values
  // chosen such that typical flags == 0 (encoded in only two bits).

  bool all_default;

  // Always present
  // Some builds / emulators complain if those fields are not initialized.
  jxl_frame_encoding encoding;
  jxl_frame_type frame_type;

  uint64_t flags;

  jxl_color_transform color_transform;
  jxl_y_cb_cr_chroma_subsampling chroma_subsampling;

  uint32_t group_size_shift;  // only if encoding == kModular;

  uint32_t x_qm_scale;  // only if VarDCT and color_transform == kXYB
  uint32_t b_qm_scale;  // only if VarDCT and color_transform == kXYB

  jxl_array_char name;

  // Skipped for kReferenceOnly.
  jxl_passes passes;

  // Skipped for kDCFrame
  bool custom_size_or_origin;
  jxl_frame_size frame_size;

  // upsampling factors for color and extra channels.
  // Upsampling is always performed before applying any inverse color transform.
  // Skipped (1) if kUseDCFrame
  uint32_t upsampling;
  jxl_array_u32 extra_channel_upsampling;

  // Only for kRegular frames.
  jxl_frame_origin frame_origin;

  jxl_blending_info blending_info;
  jxl_blending_infos extra_channel_blending_info;

  // Animation info for this frame.
  jxl_animation_frame animation_frame;

  // This is the last frame.
  bool is_last;

  // ID to refer to this frame with. 0-3, not present if kDCFrame.
  // 0 has a special meaning for kRegular frames of nonzero duration: it defines
  // a frame that will not be referenced in the future.
  uint32_t save_as_reference;

  // Whether to save this frame before or after the color transform. A frame
  // that is saved before the color transform can only be used for blending
  // through patches. On the contrary, a frame that is saved after the color
  // transform can only be used for blending through blending modes.
  // Irrelevant for extra channel blending. Can only be true if
  // blending_info.mode == kReplace and this is not a partial kRegularFrame; if
  // this is a DC frame, it is always true.
  bool save_before_color_transform;

  uint32_t dc_level;  // 1-4 if kDCFrame (0 otherwise).

  // Must be set to the one jxl_image_metadata acting as the full codestream header,
  // with correct xyb_encoded, list of extra channels, etc...
  const jxl_codec_metadata* nonserialized_metadata;

  // NOTE: This is ignored by AllDefault.
  jxl_loop_filter loop_filter;

  bool nonserialized_is_preview;

  uint64_t extensions;
} jxl_frame_header;

jxl_status jxl_frame_header_visit_fields(jxl_frame_header* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_frame_header)

static inline size_t jxl_frame_header_default_x_size(const jxl_frame_header* self) {
  if (!self->nonserialized_metadata) return 0;
  if (self->nonserialized_is_preview) {
    return jxl_preview_header_x_size(
        &self->nonserialized_metadata->m.preview_size);
  }
  return jxl_codec_metadata_x_size(self->nonserialized_metadata);
}
static inline size_t jxl_frame_header_default_y_size(const jxl_frame_header* self) {
  if (!self->nonserialized_metadata) return 0;
  if (self->nonserialized_is_preview) {
    return jxl_preview_header_y_size(
        &self->nonserialized_metadata->m.preview_size);
  }
  return jxl_codec_metadata_y_size(self->nonserialized_metadata);
}

static inline jxl_frame_dimensions jxl_frame_header_to_frame_dimensions(
    const jxl_frame_header* self) {
  size_t xsize = jxl_frame_header_default_x_size(self);
  size_t ysize = jxl_frame_header_default_y_size(self);

  xsize = self->frame_size.xsize ? self->frame_size.xsize : xsize;
  ysize = self->frame_size.ysize ? self->frame_size.ysize : ysize;

  if (self->dc_level != 0) {
    xsize = jxl_div_ceil(xsize, 1 << (3 * self->dc_level));
    ysize = jxl_div_ceil(ysize, 1 << (3 * self->dc_level));
  }

  jxl_frame_dimensions frame_dim;
  jxl_frame_dimensions_set(
      &frame_dim, xsize, ysize, self->group_size_shift,
      jxl_y_cb_cr_chroma_subsampling_max_h_shift(&self->chroma_subsampling),
      jxl_y_cb_cr_chroma_subsampling_max_v_shift(&self->chroma_subsampling),
      self->encoding == kModular, self->upsampling);
  return frame_dim;
}

// Returns true if this frame is supposed to be saved for future usage by
// other frames.
static inline bool jxl_frame_header_can_be_referenced(const jxl_frame_header* self) {
  // DC frames cannot be referenced. The last frame cannot be referenced. A
  // duration 0 frame makes little sense if it is not referenced. A
  // non-duration 0 frame may or may not be referenced.
  return !self->is_last && self->frame_type != kDCFrame &&
         (self->animation_frame.duration == 0 || self->save_as_reference != 0);
}

static inline void jxl_frame_header_init(jxl_frame_header* self,
                                   const jxl_codec_metadata* metadata) {
  jxl_memory_manager* mm =
      metadata != NULL ? metadata->m.color_encoding.storage_.icc.memory_manager
                       : NULL;
  self->encoding = kModular;
  self->frame_type = kRegularFrame;
  self->color_transform = kColorTransformXYB;
  self->nonserialized_is_preview = false;
  jxl_array_construct_empty(&self->name, mm);
  jxl_array_construct_empty(&self->extra_channel_upsampling, mm);
  jxl_y_cb_cr_chroma_subsampling_init(&self->chroma_subsampling);
  jxl_passes_init(&self->passes);
  jxl_blending_info_init(&self->blending_info);
  jxl_blending_infos_construct_empty(&self->extra_channel_blending_info);
  self->extra_channel_blending_info.memory_manager = mm;
  jxl_animation_frame_init(&self->animation_frame, metadata);
  jxl_loop_filter_init(&self->loop_filter);
  self->nonserialized_metadata = metadata;
  JXL_FIELDS_REGISTER_PTR(jxl_frame_header, &self->fields);
  jxl_bundle_init(&self->fields);
}

static inline void jxl_frame_header_destroy(jxl_frame_header* self) {
  jxl_array_destroy(&self->name);
  jxl_array_destroy(&self->extra_channel_upsampling);
  jxl_blending_infos_destroy(&self->extra_channel_blending_info);
}

// Shared by enc/dec. 5F and 13 are by far the most common for d1/2/4/8, 0
// ensures low overhead for small images.
static inline jxl_u32_enc jxl_order_enc(void) {
  return jxl_u32_enc_make(jxl_val(0x5F), jxl_val(0x13), jxl_val(0), jxl_bits(kNumOrders));
}

#endif  // LIB_JXL_FRAME_HEADER_H_
