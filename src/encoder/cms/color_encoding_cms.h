// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_CMS_COLOR_ENCODING_CMS_H_
#define JXL_ENC_CMS_COLOR_ENCODING_CMS_H_

// Internal CMS color-encoding storage (ICC + fields).
// Layering (do not collapse):
//   jxl_color_encoding           — public POD (<jxl/color_encoding.h>)
//   jxl_cms_color_encoding       — this file: owned ICC + field storage
//   jxl_enc_color_encoding       — fields/visitor wrapper around storage_
//   jxl_color_encoding_parsed    — decode-only parsed bitstream form
// Convert at API edges with jxl_cms_color_encoding_to/from_external (encode)
// and jxl_color_encoding_parsed_to_public (decode).

#include <jxl/cms_interface.h>
#include <jxl/color_encoding.h>
#include <jxl/types.h>

#include <math.h>
#include <stdint.h>

#include "base/array.h"
#include "base/enc_status.h"

typedef jxl_array_u8 jxl_icc_bytes;

// Returns whether the two inputs are approximately equal.
static inline bool jxl_cms_approx_eq(const double a, const double b,
                            double max_l1) {
  // Threshold should be sufficient for ICC's 15-bit fixed-point numbers.
  // We have seen differences of 7.1E-5 with lcms2 and 1E-3 with skcms.
  return fabs(a - b) <= max_l1;
}

// (All CIE units are for the standard 1931 2 degree observer)
//
// Color enums come from <jxl/color_encoding.h>. Keep short k* aliases for
// internal encoder/CMS code (same numeric values as the public JXL_* constants).
#define kRGB JXL_COLOR_SPACE_RGB
#define kGray JXL_COLOR_SPACE_GRAY
#define kXYB JXL_COLOR_SPACE_XYB
#define kColorSpaceUnknown JXL_COLOR_SPACE_UNKNOWN

#define kWhitePointD65 JXL_WHITE_POINT_D65
#define kWhitePointCustom JXL_WHITE_POINT_CUSTOM
#define kWhitePointE JXL_WHITE_POINT_E
#define kWhitePointDCI JXL_WHITE_POINT_DCI

#define kPrimariesSRGB JXL_PRIMARIES_SRGB
#define kPrimariesCustom JXL_PRIMARIES_CUSTOM
#define kPrimaries2100 JXL_PRIMARIES_2100
#define kPrimariesP3 JXL_PRIMARIES_P3

#define kTF709 JXL_TRANSFER_FUNCTION_709
#define kTFUnknown JXL_TRANSFER_FUNCTION_UNKNOWN
#define kTFLinear JXL_TRANSFER_FUNCTION_LINEAR
#define kTFSRGB JXL_TRANSFER_FUNCTION_SRGB
#define kTFPQ JXL_TRANSFER_FUNCTION_PQ
#define kTFDCI JXL_TRANSFER_FUNCTION_DCI
#define kTFHLG JXL_TRANSFER_FUNCTION_HLG

#define kPerceptual JXL_RENDERING_INTENT_PERCEPTUAL
#define kRelative JXL_RENDERING_INTENT_RELATIVE
#define kSaturation JXL_RENDERING_INTENT_SATURATION
#define kAbsolute JXL_RENDERING_INTENT_ABSOLUTE


// Chromaticity (Y is omitted because it is 1 for white points and implicit for
// primaries)
typedef struct jxl_ci_exy {
  double x;
  double y;
} jxl_ci_exy;

static inline jxl_ci_exy jxl_ci_exy_make(double x, double y) {
  jxl_ci_exy self;
  self.x = x;
  self.y = y;
  return self;
}

typedef struct jxl_primaries_ci_exy {
  jxl_ci_exy r;
  jxl_ci_exy g;
  jxl_ci_exy b;
} jxl_primaries_ci_exy;

static inline void jxl_primaries_ci_exy_construct_empty(jxl_primaries_ci_exy* xy) {
  xy->r.x = 0;
  xy->r.y = 0;
  xy->g.x = 0;
  xy->g.y = 0;
  xy->b.x = 0;
  xy->b.y = 0;
}

// Serializable form of jxl_ci_exy.
static const uint32_t kCmsCustomxyMul = 1000000;
static const double kCmsCustomxyRoughLimit = 4.0;
static const int32_t kCmsCustomxyMin = -0x200000;
static const int32_t kCmsCustomxyMax = 0x1FFFFF;

typedef struct jxl_cms_customxy {
  int32_t x;
  int32_t y;
} jxl_cms_customxy;
static inline void jxl_cms_customxy_construct_empty(jxl_cms_customxy* self) {
  self->x = 0;
  self->y = 0;
}
static inline jxl_ci_exy jxl_cms_customxy_get_value(const jxl_cms_customxy* self) {
  jxl_ci_exy xy;
  xy.x = self->x * (1.0 / kCmsCustomxyMul);
  xy.y = self->y * (1.0 / kCmsCustomxyMul);
  return xy;
}

static inline jxl_enc_status jxl_cms_customxy_set_value(jxl_cms_customxy* self, const jxl_ci_exy* xy) {
  bool ok = (fabs(xy->x) < kCmsCustomxyRoughLimit) &&
            (fabs(xy->y) < kCmsCustomxyRoughLimit);
  if (!ok) return JXL_FAILURE("X or Y is out of bounds");
  self->x = (int32_t)(roundf((float)(xy->x * kCmsCustomxyMul)));
  if (self->x < kCmsCustomxyMin || self->x > kCmsCustomxyMax) {
    return JXL_FAILURE("X is out of bounds");
  }
  self->y = (int32_t)(roundf((float)(xy->y * kCmsCustomxyMul)));
  if (self->y < kCmsCustomxyMin || self->y > kCmsCustomxyMax) {
    return JXL_FAILURE("Y is out of bounds");
  }
  return jxl_enc_ok_status();
}

static inline jxl_enc_status jxl_white_point_from_external(const jxl_white_point external,
                                            jxl_white_point* out) {
  switch (external) {
    case JXL_WHITE_POINT_D65:
      *out = kWhitePointD65;
      return jxl_enc_ok_status();
    case JXL_WHITE_POINT_CUSTOM:
      *out = kWhitePointCustom;
      return jxl_enc_ok_status();
    case JXL_WHITE_POINT_E:
      *out = kWhitePointE;
      return jxl_enc_ok_status();
    case JXL_WHITE_POINT_DCI:
      *out = kWhitePointDCI;
      return jxl_enc_ok_status();
  }
  return JXL_FAILURE("Invalid jxl_white_point enum value %d",
                     (int)(external));
}

static inline jxl_enc_status jxl_primaries_from_external(const jxl_primaries external,
                                           jxl_primaries* out) {
  switch (external) {
    case JXL_PRIMARIES_SRGB:
      *out = kPrimariesSRGB;
      return jxl_enc_ok_status();
    case JXL_PRIMARIES_CUSTOM:
      *out = kPrimariesCustom;
      return jxl_enc_ok_status();
    case JXL_PRIMARIES_2100:
      *out = kPrimaries2100;
      return jxl_enc_ok_status();
    case JXL_PRIMARIES_P3:
      *out = kPrimariesP3;
      return jxl_enc_ok_status();
  }
  return JXL_FAILURE("Invalid jxl_primaries enum value");
}

static inline jxl_enc_status jxl_rendering_intent_from_external(
    const jxl_rendering_intent external, jxl_rendering_intent* out) {
  switch (external) {
    case JXL_RENDERING_INTENT_PERCEPTUAL:
      *out = kPerceptual;
      return jxl_enc_ok_status();
    case JXL_RENDERING_INTENT_RELATIVE:
      *out = kRelative;
      return jxl_enc_ok_status();
    case JXL_RENDERING_INTENT_SATURATION:
      *out = kSaturation;
      return jxl_enc_ok_status();
    case JXL_RENDERING_INTENT_ABSOLUTE:
      *out = kAbsolute;
      return jxl_enc_ok_status();
  }
  return JXL_FAILURE("Invalid jxl_rendering_intent enum value");
}

// Highest reasonable value for the gamma of a transfer curve.
static const uint32_t kCmsCustomTransferFunctionMaxGamma = 8192;
static const uint32_t kCmsCustomTransferFunctionGammaMul = 10000000;

typedef struct jxl_cms_custom_transfer_function {
  bool have_gamma;

  // OETF exponent to go from linear to gamma-compressed.
  uint32_t gamma;  // Only used if have_gamma_.

  // Can be kUnknown.
  jxl_transfer_function transfer_function;  // Only used if !have_gamma_.

} jxl_cms_custom_transfer_function;
static inline void jxl_cms_custom_transfer_function_construct_empty(
    jxl_cms_custom_transfer_function* self) {
  self->have_gamma = false;
  self->gamma = 0;
  self->transfer_function = kTFSRGB;
}
static inline jxl_transfer_function jxl_cms_custom_transfer_function_get_transfer_function(
    const jxl_cms_custom_transfer_function* self) {
  JXL_DASSERT(!self->have_gamma);
  return self->have_gamma ? kTFUnknown
                          : self->transfer_function;
}
static inline void jxl_cms_custom_transfer_function_set_transfer_function(
    jxl_cms_custom_transfer_function* self, const jxl_transfer_function tf) {
  self->have_gamma = false;
  self->transfer_function = tf;
}

static inline bool jxl_cms_custom_transfer_function_is_unknown(const jxl_cms_custom_transfer_function* self) {
  return !self->have_gamma &&
         (self->transfer_function == kTFUnknown);
}
static inline bool jxl_cms_custom_transfer_function_is_pq(const jxl_cms_custom_transfer_function* self) {
  return !self->have_gamma &&
         (self->transfer_function == kTFPQ);
}
static inline bool jxl_cms_custom_transfer_function_is_hlg(const jxl_cms_custom_transfer_function* self) {
  return !self->have_gamma &&
         (self->transfer_function == kTFHLG);
}

static inline double jxl_cms_custom_transfer_function_get_gamma(const jxl_cms_custom_transfer_function* self) {
  JXL_DASSERT(self->have_gamma);
  if (!self->have_gamma) return 0.0;
  return self->gamma * (1.0 / kCmsCustomTransferFunctionGammaMul);  // (0, 1)
}
static inline jxl_enc_status jxl_cms_custom_transfer_function_set_gamma(jxl_cms_custom_transfer_function* self,
                                             double new_gamma) {
  if (new_gamma < (1.0 / kCmsCustomTransferFunctionMaxGamma) ||
      new_gamma > 1.0) {
    return JXL_FAILURE("Invalid gamma %f", new_gamma);
  }

  self->have_gamma = false;
  if (jxl_cms_approx_eq(new_gamma, 1.0, 1E-3)) {
    self->transfer_function = kTFLinear;
    return jxl_enc_ok_status();
  }
  if (jxl_cms_approx_eq(new_gamma, 1.0 / 2.6, 1E-3)) {
    self->transfer_function = kTFDCI;
    return jxl_enc_ok_status();
  }
  // Don't translate 0.45.. to kSRGB nor k709 - that might change pixel
  // values because those curves also have a linear part.

  self->have_gamma = true;
  self->gamma = (uint32_t)roundf((float)(new_gamma * kCmsCustomTransferFunctionGammaMul));
  self->transfer_function = kTFUnknown;
  return jxl_enc_ok_status();
}

static inline jxl_enc_status jxl_convert_external_to_internal_transfer_function(
    const jxl_transfer_function external, jxl_transfer_function* internal) {
  switch (external) {
    case JXL_TRANSFER_FUNCTION_709:
      *internal = kTF709;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_UNKNOWN:
      *internal = kTFUnknown;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_LINEAR:
      *internal = kTFLinear;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_SRGB:
      *internal = kTFSRGB;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_PQ:
      *internal = kTFPQ;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_DCI:
      *internal = kTFDCI;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_HLG:
      *internal = kTFHLG;
      return jxl_enc_ok_status();
    case JXL_TRANSFER_FUNCTION_GAMMA:
      return JXL_FAILURE("Gamma should be handled separately");
  }
  return JXL_FAILURE("Invalid jxl_transfer_function enum value");
}

static inline void jxl_set_unknown_external_color_encoding(jxl_color_encoding* external) {
  external->color_space = JXL_COLOR_SPACE_UNKNOWN;
  external->primaries = JXL_PRIMARIES_CUSTOM;
  external->rendering_intent = JXL_RENDERING_INTENT_PERCEPTUAL;  //?
  external->transfer_function = JXL_TRANSFER_FUNCTION_UNKNOWN;
  external->white_point = JXL_WHITE_POINT_CUSTOM;
}

static inline void jxl_color_encoding_set_empty(jxl_color_encoding* external) {
  external->color_space = (jxl_color_space)0;
  external->white_point = (jxl_white_point)0;
  external->white_point_xy[0] = 0;
  external->white_point_xy[1] = 0;
  external->primaries = (jxl_primaries)0;
  external->primaries_red_xy[0] = 0;
  external->primaries_red_xy[1] = 0;
  external->primaries_green_xy[0] = 0;
  external->primaries_green_xy[1] = 0;
  external->primaries_blue_xy[0] = 0;
  external->primaries_blue_xy[1] = 0;
  external->transfer_function = (jxl_transfer_function)0;
  external->gamma = 0;
  external->rendering_intent = (jxl_rendering_intent)0;
}

typedef struct jxl_cms_color_encoding jxl_cms_color_encoding;

// Compact encoding of data required to interpret and translate pixels to a
// known color space. Stored in Metadata. Thread-compatible.
typedef struct jxl_cms_color_encoding {
  // Only valid if HaveFields()
  jxl_white_point white_point;
  jxl_primaries primaries;  // Only valid if HasPrimaries()
  jxl_rendering_intent rendering_intent;

  // When false, fields such as white_point and tf are invalid and must not be
  // used. This occurs after setting a raw bytes-only ICC profile, only the
  // ICC bytes may be used. The color_space_ field is still valid.
  bool have_fields;

  jxl_icc_bytes icc;  // Valid ICC profile

  jxl_color_space color_space;  // Can be kUnknown
  bool cmyk;

  // "late sync" fields
  jxl_cms_custom_transfer_function tf;
  jxl_cms_customxy white;  // Only used if white_point == kCustom
  jxl_cms_customxy red;    // Only used if primaries == kCustom
  jxl_cms_customxy green;  // Only used if primaries == kCustom
  jxl_cms_customxy blue;   // Only used if primaries == kCustom

} jxl_cms_color_encoding;

static inline void jxl_cms_color_encoding_construct_empty(
    jxl_cms_color_encoding* self, jxl_context* mm) {
  self->white_point = kWhitePointD65;
  self->primaries = kPrimariesSRGB;
  self->rendering_intent = kRelative;
  self->have_fields = true;
  jxl_array_construct_empty(&self->icc, mm);
  self->color_space = kRGB;
  self->cmyk = false;
  jxl_cms_custom_transfer_function_construct_empty(&self->tf);
  jxl_cms_customxy_construct_empty(&self->white);
  jxl_cms_customxy_construct_empty(&self->red);
  jxl_cms_customxy_construct_empty(&self->green);
  jxl_cms_customxy_construct_empty(&self->blue);
}


static inline bool jxl_cms_color_encoding_has_primaries(const jxl_cms_color_encoding* self) {
  return (self->color_space != kGray) &&
         (self->color_space != kXYB);
}
static inline size_t jxl_cms_color_encoding_channels(const jxl_cms_color_encoding* self) {
  return (self->color_space == kGray) ? 1 : 3;
}

static inline jxl_enc_status jxl_cms_color_encoding_get_primaries(const jxl_cms_color_encoding* self,
                                           jxl_primaries_ci_exy* xy) {
  JXL_ENSURE(self->have_fields);
  JXL_ENSURE(jxl_cms_color_encoding_has_primaries(self));
  jxl_primaries_ci_exy_construct_empty(xy);
  switch (self->primaries) {
    case kPrimariesCustom:
      xy->r = jxl_cms_customxy_get_value(&self->red);
      xy->g = jxl_cms_customxy_get_value(&self->green);
      xy->b = jxl_cms_customxy_get_value(&self->blue);
      break;

    case kPrimariesSRGB:
      xy->r.x = 0.639998686;
      xy->r.y = 0.330010138;
      xy->g.x = 0.300003784;
      xy->g.y = 0.600003357;
      xy->b.x = 0.150002046;
      xy->b.y = 0.059997204;
      break;

    case kPrimaries2100:
      xy->r.x = 0.708;
      xy->r.y = 0.292;
      xy->g.x = 0.170;
      xy->g.y = 0.797;
      xy->b.x = 0.131;
      xy->b.y = 0.046;
      break;

    case kPrimariesP3:
      xy->r.x = 0.680;
      xy->r.y = 0.320;
      xy->g.x = 0.265;
      xy->g.y = 0.690;
      xy->b.x = 0.150;
      xy->b.y = 0.060;
      break;

    default:
      JXL_DEBUG_ABORT("internal: unexpected jxl_primaries: %d",
                      (int)(self->primaries));
  }
  return jxl_enc_ok_status();
}

static inline jxl_enc_status jxl_cms_color_encoding_set_primaries(jxl_cms_color_encoding* self,
                                           const jxl_primaries_ci_exy* xy) {
  JXL_ENSURE(self->have_fields);
  JXL_ENSURE(jxl_cms_color_encoding_has_primaries(self));
  if (xy->r.x == 0.0 || xy->r.y == 0.0 || xy->g.x == 0.0 || xy->g.y == 0.0 ||
      xy->b.x == 0.0 || xy->b.y == 0.0) {
    return JXL_FAILURE("Invalid primaries %f %f %f %f %f %f", xy->r.x, xy->r.y,
                       xy->g.x, xy->g.y, xy->b.x, xy->b.y);
  }

  if (jxl_cms_approx_eq(xy->r.x, 0.64, 1E-3) && jxl_cms_approx_eq(xy->r.y, 0.33, 1E-3) &&
      jxl_cms_approx_eq(xy->g.x, 0.30, 1E-3) && jxl_cms_approx_eq(xy->g.y, 0.60, 1E-3) &&
      jxl_cms_approx_eq(xy->b.x, 0.15, 1E-3) && jxl_cms_approx_eq(xy->b.y, 0.06, 1E-3)) {
    self->primaries = kPrimariesSRGB;
    return jxl_enc_ok_status();
  }

  if (jxl_cms_approx_eq(xy->r.x, 0.708, 1E-3) && jxl_cms_approx_eq(xy->r.y, 0.292, 1E-3) &&
      jxl_cms_approx_eq(xy->g.x, 0.170, 1E-3) && jxl_cms_approx_eq(xy->g.y, 0.797, 1E-3) &&
      jxl_cms_approx_eq(xy->b.x, 0.131, 1E-3) && jxl_cms_approx_eq(xy->b.y, 0.046, 1E-3)) {
    self->primaries = kPrimaries2100;
    return jxl_enc_ok_status();
  }
  if (jxl_cms_approx_eq(xy->r.x, 0.680, 1E-3) && jxl_cms_approx_eq(xy->r.y, 0.320, 1E-3) &&
      jxl_cms_approx_eq(xy->g.x, 0.265, 1E-3) && jxl_cms_approx_eq(xy->g.y, 0.690, 1E-3) &&
      jxl_cms_approx_eq(xy->b.x, 0.150, 1E-3) && jxl_cms_approx_eq(xy->b.y, 0.060, 1E-3)) {
    self->primaries = kPrimariesP3;
    return jxl_enc_ok_status();
  }

  self->primaries = kPrimariesCustom;
  JXL_RETURN_IF_ERROR(jxl_cms_customxy_set_value(&self->red, &xy->r));
  JXL_RETURN_IF_ERROR(jxl_cms_customxy_set_value(&self->green, &xy->g));
  JXL_RETURN_IF_ERROR(jxl_cms_customxy_set_value(&self->blue, &xy->b));
  return jxl_enc_ok_status();
}

static inline jxl_ci_exy jxl_cms_color_encoding_get_white_point(const jxl_cms_color_encoding* self) {
  jxl_ci_exy xy = jxl_ci_exy_make(0.0, 0.0);
  JXL_DASSERT(self->have_fields);
  if (!self->have_fields) return xy;
  switch (self->white_point) {
    case kWhitePointCustom:
      xy = jxl_cms_customxy_get_value(&self->white);
      break;

    case kWhitePointD65:
      xy.x = 0.3127;
      xy.y = 0.3290;
      break;

    case kWhitePointDCI:
      // From https://ieeexplore.ieee.org/document/7290729 C.2 page 11
      xy.x = 0.314;
      xy.y = 0.351;
      break;

    case kWhitePointE:
      xy.x = xy.y = 1.0 / 3;
      break;

    default:
      JXL_DEBUG_ABORT("internal: unexpected jxl_white_point: %d",
                      (int)(self->white_point));
  }
  return xy;
}

static inline jxl_enc_status jxl_cms_color_encoding_set_white_point(jxl_cms_color_encoding* self,
                                            const jxl_ci_exy* xy) {
  JXL_ENSURE(self->have_fields);
  if (xy->x == 0.0 || xy->y == 0.0) {
    return JXL_FAILURE("Invalid white point %f %f", xy->x, xy->y);
  }
  if (jxl_cms_approx_eq(xy->x, 0.3127, 1E-3) && jxl_cms_approx_eq(xy->y, 0.3290, 1E-3)) {
    self->white_point = kWhitePointD65;
    return jxl_enc_ok_status();
  }
  if (jxl_cms_approx_eq(xy->x, 1.0 / 3, 1E-3) && jxl_cms_approx_eq(xy->y, 1.0 / 3, 1E-3)) {
    self->white_point = kWhitePointE;
    return jxl_enc_ok_status();
  }
  if (jxl_cms_approx_eq(xy->x, 0.314, 1E-3) && jxl_cms_approx_eq(xy->y, 0.351, 1E-3)) {
    self->white_point = kWhitePointDCI;
    return jxl_enc_ok_status();
  }
  self->white_point = kWhitePointCustom;
  return jxl_cms_customxy_set_value(&self->white, xy);
}

static inline jxl_enc_status jxl_cms_color_encoding_from_external(jxl_cms_color_encoding* self,
                                           const jxl_color_encoding* external);
static inline jxl_color_encoding jxl_cms_color_encoding_to_external(const jxl_cms_color_encoding* self);

// Returns true if all fields have been initialized (possibly to kUnknown).
// Returns false if the ICC profile is invalid or decoding it fails.
static inline jxl_enc_status jxl_cms_color_encoding_set_fields_from_icc(jxl_cms_color_encoding* self,
                                               jxl_icc_bytes* new_icc,
                                               const jxl_cms_interface* cms) {
  // In case parsing fails, mark the jxl_cms_color_encoding as invalid.
  JXL_ENSURE(new_icc != NULL);
  JXL_ENSURE(!jxl_array_empty(new_icc));
  self->color_space = kColorSpaceUnknown;
  self->tf.transfer_function = kTFUnknown;
  jxl_array_clear(&self->icc);

  jxl_color_encoding external;
  jxl_color_encoding_set_empty(&external);
  JXL_BOOL new_cmyk;
  JXL_RETURN_IF_ERROR(jxl_enc_status_from_bool(cms->set_fields_from_icc(
      cms->set_fields_data, jxl_array_data(new_icc), jxl_array_len(new_icc), &external,
      &new_cmyk)));
  self->cmyk = (bool)(new_cmyk);
  JXL_RETURN_IF_ERROR(jxl_cms_color_encoding_from_external(self, &external));
  jxl_array_swap(&self->icc, new_icc);
  return jxl_enc_ok_status();
}

static inline jxl_color_encoding jxl_cms_color_encoding_to_external(const jxl_cms_color_encoding* self) {
  jxl_color_encoding external;
  jxl_color_encoding_set_empty(&external);
  if (!self->have_fields) {
    jxl_set_unknown_external_color_encoding(&external);
    return external;
  }
  external.color_space = (jxl_color_space)(self->color_space);

  external.white_point = (jxl_white_point)(self->white_point);

  jxl_ci_exy wp = jxl_cms_color_encoding_get_white_point(self);
  external.white_point_xy[0] = wp.x;
  external.white_point_xy[1] = wp.y;

  if (external.color_space == JXL_COLOR_SPACE_RGB ||
      external.color_space == JXL_COLOR_SPACE_UNKNOWN) {
    external.primaries = (jxl_primaries)(self->primaries);
    jxl_primaries_ci_exy p;
    if (!jxl_enc_status_ok(jxl_cms_color_encoding_get_primaries(self, &p))) {
      jxl_set_unknown_external_color_encoding(&external);
      return external;
    }
    external.primaries_red_xy[0] = p.r.x;
    external.primaries_red_xy[1] = p.r.y;
    external.primaries_green_xy[0] = p.g.x;
    external.primaries_green_xy[1] = p.g.y;
    external.primaries_blue_xy[0] = p.b.x;
    external.primaries_blue_xy[1] = p.b.y;
  }

  if (self->tf.have_gamma) {
    external.transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
    external.gamma = jxl_cms_custom_transfer_function_get_gamma(&self->tf);
  } else {
    external.transfer_function =
        (jxl_transfer_function)(jxl_cms_custom_transfer_function_get_transfer_function(
            &self->tf));
    external.gamma = 0;
  }

  external.rendering_intent =
      (jxl_rendering_intent)(self->rendering_intent);
  return external;
}

// NB: does not create ICC.
static inline jxl_enc_status jxl_cms_color_encoding_from_external(jxl_cms_color_encoding* self,
                                           const jxl_color_encoding* external) {
  // TODO(eustas): update non-serializable on call-site
  self->color_space = (jxl_color_space)(external->color_space);

  JXL_RETURN_IF_ERROR(
      jxl_white_point_from_external(external->white_point, &self->white_point));
  if (external->white_point == JXL_WHITE_POINT_CUSTOM) {
    jxl_ci_exy wp;
    wp.x = external->white_point_xy[0];
    wp.y = external->white_point_xy[1];
    JXL_RETURN_IF_ERROR(jxl_cms_color_encoding_set_white_point(self, &wp));
  }

  if (external->color_space == JXL_COLOR_SPACE_RGB ||
      external->color_space == JXL_COLOR_SPACE_UNKNOWN) {
    JXL_RETURN_IF_ERROR(
        jxl_primaries_from_external(external->primaries, &self->primaries));
    if (external->primaries == JXL_PRIMARIES_CUSTOM) {
      jxl_primaries_ci_exy new_primaries;
      new_primaries.r.x = external->primaries_red_xy[0];
      new_primaries.r.y = external->primaries_red_xy[1];
      new_primaries.g.x = external->primaries_green_xy[0];
      new_primaries.g.y = external->primaries_green_xy[1];
      new_primaries.b.x = external->primaries_blue_xy[0];
      new_primaries.b.y = external->primaries_blue_xy[1];
      JXL_RETURN_IF_ERROR(jxl_cms_color_encoding_set_primaries(self, &new_primaries));
    }
  }
  jxl_cms_custom_transfer_function new_tf;
  if (external->transfer_function == JXL_TRANSFER_FUNCTION_GAMMA) {
    JXL_RETURN_IF_ERROR(
        jxl_cms_custom_transfer_function_set_gamma(&new_tf, external->gamma));
  } else {
    jxl_transfer_function tf_enum;
    // JXL_TRANSFER_FUNCTION_GAMMA is not handled by this function since
    // there's no internal enum value for it.
    JXL_RETURN_IF_ERROR(jxl_convert_external_to_internal_transfer_function(
        external->transfer_function, &tf_enum));
    jxl_cms_custom_transfer_function_set_transfer_function(&new_tf, tf_enum);
  }
  self->tf = new_tf;

  JXL_RETURN_IF_ERROR(jxl_rendering_intent_from_external(external->rendering_intent,
                                                  &self->rendering_intent));

  jxl_array_clear(&self->icc);

  return jxl_enc_ok_status();
}


#endif  // JXL_ENC_CMS_COLOR_ENCODING_CMS_H_
