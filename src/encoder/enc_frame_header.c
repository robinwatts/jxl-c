// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "enc_frame_header.h"

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "field_encodings.h"
#include "enc_image_metadata.h"

#include "base/printf_macros.h"
#include "base/enc_status.h"
#include "common.h"  // kMaxNumPasses
#include "fields.h"
#include "pack_signed.h"

const uint8_t kYCbCrChromaHShift[] = {0, 1, 1, 0};
const uint8_t kYCbCrChromaVShift[] = {0, 1, 0, 1};

static jxl_enc_status jxl_visit_blend_mode(jxl_visitor* JXL_RESTRICT visitor,
                             jxl_enc_blend_mode default_value, jxl_enc_blend_mode* blend_mode) {
  uint32_t encoded = (uint32_t)(*blend_mode);

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val((uint32_t)(kReplace)), jxl_val((uint32_t)(kAdd)), jxl_val((uint32_t)(kBlend)), jxl_bits_offset(2, 3)), (uint32_t)(default_value), &encoded));
  if (encoded > (uint32_t)(kMul)) {
    return JXL_FAILURE("Invalid blend_mode");
  }
  *blend_mode = (jxl_enc_blend_mode)(encoded);
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_visit_frame_type(jxl_visitor* JXL_RESTRICT visitor,
                             jxl_enc_frame_type default_value, jxl_enc_frame_type* frame_type) {
  uint32_t encoded = (uint32_t)(*frame_type);

  JXL_QUIET_RETURN_IF_ERROR(
      jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val((uint32_t)(kRegularFrame)), jxl_val((uint32_t)(kDCFrame)), jxl_val((uint32_t)(kReferenceOnly)), jxl_val((uint32_t)(kSkipProgressive))), (uint32_t)(default_value), &encoded));
  *frame_type = (jxl_enc_frame_type)(encoded);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_enc_blending_info_visit_fields(jxl_enc_blending_info* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(
      jxl_visit_blend_mode(visitor, kReplace, &self->mode));
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->nonserialized_num_extra_channels > 0 &&
                           (self->mode == kBlend ||
                            self->mode == kAlphaWeightedAdd)))) {
    // Up to 11 alpha channels for blending.
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_val(2), jxl_bits_offset(3, 3)), 0, &self->alpha_channel));
    if (jxl_visitor_is_reading(visitor) &&
        self->alpha_channel >= self->nonserialized_num_extra_channels) {
      return JXL_FAILURE("Invalid alpha channel for blending");
    }
  }
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, (self->nonserialized_num_extra_channels > 0 &&
                            (self->mode == kBlend ||
                             self->mode == kAlphaWeightedAdd)) ||
                           self->mode == kMul))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->clamp));
  }
  // 'old' frame for blending. Only necessary if self is not a full frame, or
  // blending is not kReplace.
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->mode != kReplace ||
                           self->nonserialized_is_partial_frame))) {
    JXL_QUIET_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_val(2), jxl_val(3)), 0, &self->source));
  }
  return jxl_enc_ok_status();
}


jxl_enc_status jxl_animation_frame_visit_fields(jxl_animation_frame* self, jxl_visitor* JXL_RESTRICT visitor) {
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->nonserialized_metadata != NULL &&
                           self->nonserialized_metadata->m.have_animation))) {
    JXL_QUIET_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_bits(8), jxl_bits(32)), 0, &self->duration));
  }

  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, 
          self->nonserialized_metadata != NULL &&
          self->nonserialized_metadata->m.animation.have_timecodes))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 32, 0, &self->timecode));
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_passes_visit_fields(jxl_passes* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(
      jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(3), jxl_bits_offset(3, 4)), 1, &self->num_passes));
  JXL_ENSURE(self->num_passes <= kMaxNumPasses);  // Cannot happen when reading

  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->num_passes != 1))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_val(2), jxl_bits_offset(1, 3)), 0, &self->num_downsample));
    JXL_ENSURE(self->num_downsample <= 4);  // 1,2,4,8
    if (self->num_downsample > self->num_passes) {
      return JXL_FAILURE("num_downsample %u > num_passes %u", self->num_downsample,
                         self->num_passes);
    }

    for (uint32_t i = 0; i < self->num_passes - 1; i++) {
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, &self->shift[i]));
    }
    self->shift[self->num_passes - 1] = 0;

    for (uint32_t i = 0; i < self->num_downsample; ++i) {
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(4), jxl_val(8)), 1, &self->downsample[i]));
      if (i > 0 && self->downsample[i] >= self->downsample[i - 1]) {
        return JXL_FAILURE("downsample sequence should be decreasing");
      }
    }
    for (uint32_t i = 0; i < self->num_downsample; ++i) {
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_val(2), jxl_bits(3)), 0, &self->last_pass[i]));
      if (i > 0 && self->last_pass[i] <= self->last_pass[i - 1]) {
        return JXL_FAILURE("last_pass sequence should be increasing");
      }
      if (self->last_pass[i] >= self->num_passes) {
        return JXL_FAILURE("last_pass %u >= num_passes %u", self->last_pass[i],
                           self->num_passes);
      }
    }
  }

  return jxl_enc_ok_status();
}





jxl_enc_status jxl_enc_frame_header_visit_fields(jxl_enc_frame_header* self, jxl_visitor* JXL_RESTRICT visitor) {
  if (jxl_enc_status_ok(jxl_visitor_all_default(visitor, &self->fields, &self->all_default))) {
    // Overwrite all serialized fields, but not any nonserialized_*.
    jxl_visitor_set_default(visitor, &self->fields);
    return jxl_enc_ok_status();
  }

  JXL_QUIET_RETURN_IF_ERROR(
      jxl_visit_frame_type(visitor, kRegularFrame, &self->frame_type));
  if (jxl_visitor_is_reading(visitor) && self->nonserialized_is_preview &&
      self->frame_type != kRegularFrame) {
    return JXL_FAILURE("Only regular frame could be a preview");
  }

  // jxl_enc_frame_encoding.
  bool is_modular = (self->encoding == kModular);
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &is_modular));
  self->encoding = (is_modular ? kModular : kVarDCT);

  // Flags
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u64(visitor, 0, &self->flags));

  // jxl_color transform
  bool xyb_encoded = self->nonserialized_metadata == NULL ||
                     self->nonserialized_metadata->m.xyb_encoded;

  if (xyb_encoded) {
    self->color_transform = kColorTransformXYB;
  } else {
    // Alternate if kYCbCr.
    bool alternate = self->color_transform == kColorTransformYCbCr;
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &alternate));
    self->color_transform =
        (alternate ? kColorTransformYCbCr : kColorTransformNone);
  }

  // Chroma subsampling for YCbCr, if no DC frame is used.
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->color_transform == kColorTransformYCbCr &&
                           ((self->flags & kUseDcFrame) == 0)))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->chroma_subsampling.fields));
  }

  size_t num_extra_channels =
      self->nonserialized_metadata != NULL
          ? jxl_extra_channel_infos_size(&self->nonserialized_metadata->m.extra_channel_info)
          : 0;

  // Upsampling
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, (self->flags & kUseDcFrame) == 0))) {
    JXL_QUIET_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(4), jxl_val(8)), 1, &self->upsampling));
    if (self->nonserialized_metadata != NULL &&
        jxl_enc_status_ok(jxl_visitor_conditional(visitor, num_extra_channels != 0))) {
      const jxl_extra_channel_infos* extra_channels =
          &self->nonserialized_metadata->m.extra_channel_info;
      JXL_RETURN_IF_ERROR(
          jxl_array_u32_resize_fill(&self->extra_channel_upsampling, jxl_extra_channel_infos_size(extra_channels),
                          (uint32_t)(1)));
      for (size_t i = 0; i < jxl_extra_channel_infos_size(extra_channels); ++i) {
        uint32_t dim_shift =
            jxl_extra_channel_infos_at_const(&self->nonserialized_metadata->m.extra_channel_info, i)->dim_shift;
        uint32_t* ec_upsampling = jxl_array_at(&self->extra_channel_upsampling, i);
        *ec_upsampling >>= dim_shift;
        JXL_QUIET_RETURN_IF_ERROR(
            jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(4), jxl_val(8)), 1, ec_upsampling));
        *ec_upsampling <<= dim_shift;
        if (*ec_upsampling < self->upsampling) {
          return JXL_FAILURE(
              "EC upsampling (%u) < color upsampling (%u), which is invalid.",
              *ec_upsampling, self->upsampling);
        }
        if (*ec_upsampling > 8) {
          return JXL_FAILURE("EC upsampling too large (%u)", *ec_upsampling);
        }
      }
    } else {
      jxl_array_clear(&self->extra_channel_upsampling);
    }
  }

  // Modular- or VarDCT-specific data.
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->encoding == kModular))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 1, &self->group_size_shift));
  }
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->encoding == kVarDCT &&
                           self->color_transform == kColorTransformXYB))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 3, 3, &self->x_qm_scale));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 3, 2, &self->b_qm_scale));
  } else {
    self->x_qm_scale = self->b_qm_scale = 2;  // noop
  }

  // Not useful for kPatchSource
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type != kReferenceOnly))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->passes.fields));
  }

  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type == kDCFrame))) {
    // Up to 4 pyramid levels - for up to 16384x downsampling.
    JXL_QUIET_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(3), jxl_val(4)), 1, &self->dc_level));
  }
  if (self->frame_type != kDCFrame) {
    self->dc_level = 0;
  }

  bool is_partial_frame = false;
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type != kDCFrame))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->custom_size_or_origin));
    if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->custom_size_or_origin))) {
      const jxl_u32_enc enc = jxl_u32_enc_make(jxl_bits(8), jxl_bits_offset(11, 256), jxl_bits_offset(14, 2304), jxl_bits_offset(30, 18688));
      // Frame offset, only if kRegularFrame or kSkipProgressive.
      if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type == kRegularFrame ||
                               self->frame_type == kSkipProgressive))) {
        uint32_t ux0 = jxl_pack_signed(self->frame_origin.x0);
        uint32_t uy0 = jxl_pack_signed(self->frame_origin.y0);
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, enc, 0, &ux0));
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, enc, 0, &uy0));
        self->frame_origin.x0 = jxl_unpack_signed(ux0);
        self->frame_origin.y0 = jxl_unpack_signed(uy0);
      }
      // Frame size
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, enc, 0, &self->frame_size.xsize));
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, enc, 0, &self->frame_size.ysize));
      if (self->custom_size_or_origin &&
          (self->frame_size.xsize == 0 || self->frame_size.ysize == 0)) {
        return JXL_FAILURE(
            "Invalid crop dimensions for frame: zero width or height");
      }
      int32_t image_xsize = jxl_enc_frame_header_default_x_size(self);
      int32_t image_ysize = jxl_enc_frame_header_default_y_size(self);
      if (self->frame_type == kRegularFrame ||
          self->frame_type == kSkipProgressive) {
        is_partial_frame |= self->frame_origin.x0 > 0;
        is_partial_frame |= self->frame_origin.y0 > 0;
        is_partial_frame |= ((int32_t)(self->frame_size.xsize) +
                             self->frame_origin.x0) < image_xsize;
        is_partial_frame |= ((int32_t)(self->frame_size.ysize) +
                             self->frame_origin.y0) < image_ysize;
      }
    }
  }

  // Blending info, animation info and whether self is the last frame or not.
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type == kRegularFrame ||
                           self->frame_type == kSkipProgressive))) {
    self->blending_info.nonserialized_num_extra_channels = num_extra_channels;
    self->blending_info.nonserialized_is_partial_frame = is_partial_frame;
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->blending_info.fields));
    bool replace_all = (self->blending_info.mode == kReplace);
    if (!jxl_enc_status_ok(jxl_blending_infos_resize(&self->extra_channel_blending_info, num_extra_channels))) {
      return JXL_FAILURE("Failed to allocate extra_channel_blending_info");
    }
    for (size_t i = 0; i < num_extra_channels; i++) {
      jxl_enc_blending_info* ec_blending_info =
          jxl_blending_infos_at(&self->extra_channel_blending_info, i);
      ec_blending_info->nonserialized_is_partial_frame = is_partial_frame;
      ec_blending_info->nonserialized_num_extra_channels = num_extra_channels;
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &ec_blending_info->fields));
      replace_all &= (ec_blending_info->mode == kReplace);
    }
    if (jxl_visitor_is_reading(visitor) && self->nonserialized_is_preview) {
      if (!replace_all || self->custom_size_or_origin) {
        return JXL_FAILURE("Preview is not compatible with blending");
      }
    }
    if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->nonserialized_metadata != NULL &&
                             self->nonserialized_metadata->m.have_animation))) {
      self->animation_frame.nonserialized_metadata = self->nonserialized_metadata;
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->animation_frame.fields));
    }
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, true, &self->is_last));
  } else {
    self->is_last = false;
  }

  // ID of that can be used to refer to self frame. 0 for a non-zero-duration
  // frame means that it will not be referenced. Not necessary for the last
  // frame.
  if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type != kDCFrame && !self->is_last))) {
    JXL_QUIET_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_val(2), jxl_val(3)), 0, &self->save_as_reference));
  }

  // If self frame is not blended on another frame post-color-transform, it may
  // be stored for being referenced either before or after the color transform.
  // If it is blended post-color-transform, it must be blended after. It must
  // also be blended after if self is a kRegular frame that does not cover the
  // full frame, as samples outside the partial region are from a
  // post-color-transform frame.
  if (self->frame_type != kDCFrame) {
    if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, jxl_enc_frame_header_can_be_referenced(self) &&
                             self->blending_info.mode == kReplace &&
                             !is_partial_frame &&
                             (self->frame_type == kRegularFrame ||
                              self->frame_type == kSkipProgressive)))) {
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_bool(visitor, false, &self->save_before_color_transform));
    } else if (jxl_enc_status_ok(jxl_visitor_conditional(visitor, self->frame_type == kReferenceOnly))) {
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_bool(visitor, true, &self->save_before_color_transform));
      size_t xsize = self->custom_size_or_origin
                         ? self->frame_size.xsize
                         : jxl_codec_metadata_x_size(self->nonserialized_metadata);
      size_t ysize = self->custom_size_or_origin
                         ? self->frame_size.ysize
                         : jxl_codec_metadata_y_size(self->nonserialized_metadata);
      if (!self->save_before_color_transform &&
          (xsize < jxl_codec_metadata_x_size(self->nonserialized_metadata) ||
           ysize < jxl_codec_metadata_y_size(self->nonserialized_metadata) ||
           self->frame_origin.x0 != 0 || self->frame_origin.y0 != 0)) {
        return JXL_FAILURE(
            "non-patch reference frame with invalid crop: %" jxl_pr_iu_s "x%" jxl_pr_iu_s
            "%+d%+d",
            xsize, ysize, (int)(self->frame_origin.x0),
            (int)(self->frame_origin.y0));
      }
    }
  } else {
    self->save_before_color_transform = true;
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visit_name_string(visitor, &self->name));

  self->loop_filter.nonserialized_is_modular = is_modular;
  JXL_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->loop_filter.fields));

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_begin_extensions(visitor, &self->extensions));
  // Extensions: in chronological order of being added to the format.
  return jxl_visitor_end_extensions(visitor);
}


