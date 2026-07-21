// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/color_encoding_internal.h"

#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/cms/color_encoding_cms.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/pack_signed.h"

void jxl_enc_color_encoding_create_c2(jxl_primaries pr, jxl_transfer_function tf,
                           jxl_memory_manager* mm, jxl_enc_color_encoding out[2]) {
  jxl_enc_color_encoding* c_rgb = &out[0];
  jxl_enc_color_encoding_construct_empty(c_rgb, mm);
  jxl_enc_color_encoding_init(c_rgb);
  jxl_enc_color_encoding_set_color_space(c_rgb, kRGB);
  c_rgb->storage_.white_point = kWhitePointD65;
  c_rgb->storage_.primaries = pr;
  jxl_cms_custom_transfer_function_set_transfer_function(&c_rgb->storage_.tf, tf);
  jxl_status status = jxl_enc_color_encoding_create_icc(c_rgb);
  (void)status;
  JXL_DASSERT(jxl_status_ok(status));

  jxl_enc_color_encoding* c_gray = &out[1];
  jxl_enc_color_encoding_construct_empty(c_gray, mm);
  jxl_enc_color_encoding_init(c_gray);
  jxl_enc_color_encoding_set_color_space(c_gray, kGray);
  c_gray->storage_.white_point = kWhitePointD65;
  c_gray->storage_.primaries = pr;
  jxl_cms_custom_transfer_function_set_transfer_function(&c_gray->storage_.tf, tf);
  status = jxl_enc_color_encoding_create_icc(c_gray);
  (void)status;
  JXL_DASSERT(jxl_status_ok(status));
}

jxl_status jxl_customxy_visit_fields(jxl_customxy* self, jxl_visitor* JXL_RESTRICT visitor) {
  uint32_t ux = jxl_pack_signed(self->storage_.x);
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits(19), jxl_bits_offset(19, 524288), jxl_bits_offset(20, 1048576), jxl_bits_offset(21, 2097152)), 0, &ux));
  self->storage_.x = jxl_unpack_signed(ux);
  uint32_t uy = jxl_pack_signed(self->storage_.y);
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits(19), jxl_bits_offset(19, 524288), jxl_bits_offset(20, 1048576), jxl_bits_offset(21, 2097152)), 0, &uy));
  self->storage_.y = jxl_unpack_signed(uy);
  return jxl_ok_status();
}

jxl_status jxl_custom_transfer_function_visit_fields(jxl_custom_transfer_function* self, jxl_visitor* JXL_RESTRICT visitor) {
  if (jxl_status_ok(jxl_visitor_conditional(visitor, !jxl_custom_transfer_function_set_implicit(self)))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->storage_.have_gamma));

    if (jxl_status_ok(jxl_visitor_conditional(visitor, self->storage_.have_gamma))) {
      // Gamma is represented as a 24-bit int, the exponent used is
      // gamma_ / 1e7. Valid values are (0, 1]. On the low end side, we also
      // limit it to kMaxGamma/1e7.
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 
          24, kCmsCustomTransferFunctionGammaMul, &self->storage_.gamma));
      if (self->storage_.gamma > kCmsCustomTransferFunctionGammaMul ||
          (uint64_t)(self->storage_.gamma) *
                  kCmsCustomTransferFunctionMaxGamma <
              kCmsCustomTransferFunctionGammaMul) {
        return JXL_FAILURE("Invalid gamma %u", self->storage_.gamma);
      }
    }

    if (jxl_status_ok(jxl_visitor_conditional(visitor, !self->storage_.have_gamma))) {
      uint32_t transfer_function =
          (uint32_t)(self->storage_.transfer_function);
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_enum(visitor, 
          (uint32_t)(kTFSRGB), &transfer_function,
          jxl_enum_bits_transfer_function(), jxl_enum_name_transfer_function()));
      self->storage_.transfer_function =
          (jxl_transfer_function)(transfer_function);
    }
  }

  return jxl_ok_status();
}

jxl_status jxl_enc_color_encoding_visit_fields(jxl_enc_color_encoding* self, jxl_visitor* JXL_RESTRICT visitor) {
  if (jxl_status_ok(jxl_visitor_all_default(visitor, &self->fields, &self->all_default))) {
    // Overwrite all serialized fields, but not any nonserialized_*.
    jxl_visitor_set_default(visitor, &self->fields);
    return jxl_ok_status();
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->want_icc_));

  // Always send even if self->want_icc_ because self affects decoding.
  // We can skip the white point/primaries because they do not.
  {
    uint32_t color_space = (uint32_t)(self->storage_.color_space);
    JXL_QUIET_RETURN_IF_ERROR(
        jxl_visitor_enum(visitor, (uint32_t)(kRGB), &color_space,
                      jxl_enum_bits_color_space(), jxl_enum_name_color_space()));
    self->storage_.color_space = (jxl_color_space)(color_space);
  }

  if (jxl_status_ok(jxl_visitor_conditional(visitor, !jxl_enc_color_encoding_want_icc(self)))) {
    // Serialize enums. NOTE: we set the defaults to the most common values so
    // jxl_image_metadata.all_default is true in the common case.

    if (jxl_status_ok(jxl_visitor_conditional(visitor, !jxl_enc_color_encoding_implicit_white_point(self)))) {
      uint32_t white_point = (uint32_t)(self->storage_.white_point);
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_enum(visitor, (uint32_t)(kWhitePointD65), &white_point,
                        jxl_enum_bits_white_point(), jxl_enum_name_white_point()));
      self->storage_.white_point = (jxl_white_point)(white_point);
      if (jxl_status_ok(jxl_visitor_conditional(visitor, self->storage_.white_point == kWhitePointCustom))) {
        self->white_.storage_ = self->storage_.white;
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->white_.fields));
        self->storage_.white = self->white_.storage_;
      }
    }

    if (jxl_status_ok(jxl_visitor_conditional(visitor, jxl_enc_color_encoding_has_primaries(self)))) {
      uint32_t primaries = (uint32_t)(self->storage_.primaries);
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_enum(visitor, (uint32_t)(kPrimariesSRGB), &primaries,
                        jxl_enum_bits_primaries(), jxl_enum_name_primaries()));
      self->storage_.primaries = (jxl_primaries)(primaries);
      if (jxl_status_ok(jxl_visitor_conditional(visitor, self->storage_.primaries == kPrimariesCustom))) {
        self->red_.storage_ = self->storage_.red;
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->red_.fields));
        self->storage_.red = self->red_.storage_;
        self->green_.storage_ = self->storage_.green;
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->green_.fields));
        self->storage_.green = self->green_.storage_;
        self->blue_.storage_ = self->storage_.blue;
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->blue_.fields));
        self->storage_.blue = self->blue_.storage_;
      }
    }

    self->tf_.nonserialized_color_space = self->storage_.color_space;
    self->tf_.storage_ = self->storage_.tf;
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->tf_.fields));
    self->storage_.tf = self->tf_.storage_;

    {
      uint32_t rendering_intent =
          (uint32_t)(self->storage_.rendering_intent);
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_enum(visitor, 
          (uint32_t)(kRelative), &rendering_intent,
          jxl_enum_bits_rendering_intent(), jxl_enum_name_rendering_intent()));
      self->storage_.rendering_intent =
          (jxl_rendering_intent)(rendering_intent);
    }

    // We didn't have ICC, so all self->fields should be known.
    if (self->storage_.color_space == kColorSpaceUnknown ||
        jxl_cms_custom_transfer_function_is_unknown(&self->storage_.tf)) {
      return JXL_FAILURE(
          "No ICC but cs %u and tf %u%s",
          (unsigned int)(self->storage_.color_space),
          self->storage_.tf.have_gamma
              ? 0
              : (unsigned int)(self->storage_.tf.transfer_function),
          self->storage_.tf.have_gamma ? "(gamma)" : "");
    }

    JXL_RETURN_IF_ERROR(jxl_enc_color_encoding_create_icc(self));
  }

  if (jxl_enc_color_encoding_want_icc(self) && jxl_visitor_is_reading(visitor)) {
    // Haven't called SetICC() yet, do nothing.
  } else {
    if (jxl_array_empty(jxl_enc_color_encoding_icc(self))) return JXL_FAILURE("Empty ICC");
  }

  return jxl_ok_status();
}

