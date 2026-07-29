// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include <jxl/cms.h>

#include <jxl/cms_interface.h>
#include <jxl/color_encoding.h>
#include <jxl/types.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "base/span.h"
#include "cms/color_encoding_cms.h"
#include "cms/jxl_cms_internal.h"
#include "color_encoding_internal.h"
#include "context_internal.h"
#include <jxl/context.h>
#include "enc_allocator.h"
#include "lcms2.h"
#include "lcms2_plugin.h"


JXL_MUST_USE_RESULT static jxl_ci_exy jxl_ci_exy_fromxy_y(const cmsCIExyY* xyY) {
  jxl_ci_exy xy;
  xy.x = xyY->x;
  xy.y = xyY->y;
  return xy;
}

JXL_MUST_USE_RESULT static jxl_ci_exy jxl_ci_exy_from_xyz(const cmsCIEXYZ* XYZ) {
  cmsCIExyY xyY;
  cmsXYZ2xyY(/*Dest=*/&xyY, /*Source=*/XYZ);
  return jxl_ci_exy_fromxy_y(&xyY);
}

JXL_MUST_USE_RESULT static cmsCIEXYZ D50_XYZ(void) {
  // Quantized D50 as stored in ICC profiles.
  cmsCIEXYZ xyz;
  xyz.X = 0.96420288;
  xyz.Y = 1.0;
  xyz.Z = 0.82490540;
  return xyz;
}

// Owning LittleCMS handles (Init/Reset/Destroy; no C++ RAII).
typedef struct jxl_cms_owned {
  void* p;
  void (*destroy)(void*);
} jxl_cms_owned;

static inline void jxl_cms_owned_init(jxl_cms_owned* self, void* ptr, void (*destroy_fn)(void*)) {
  self->p = ptr;
  self->destroy = destroy_fn;
}

static inline void jxl_cms_owned_reset(jxl_cms_owned* self, void* ptr) {
  if (self == NULL) return;
  if (self->p != NULL && self->destroy != NULL) self->destroy(self->p);
  self->p = ptr;
}

static inline void jxl_cms_owned_destroy(jxl_cms_owned* self) { jxl_cms_owned_reset(self, NULL); }

static inline void* jxl_cms_owned_get(const jxl_cms_owned* self) { return self->p; }
static inline bool jxl_cms_owned_ok(const jxl_cms_owned* self) { return self->p != NULL; }

static inline void jxl_cms_owned_swap(jxl_cms_owned* self, jxl_cms_owned* other) {
  void* tp = self->p;
  self->p = other->p;
  other->p = tp;
  void (*td)(void*) = self->destroy;
  self->destroy = other->destroy;
  other->destroy = td;
}

static inline void jxl_destroy_cms_context(void* p) {
  cmsDeleteContext((cmsContext)(p));
}
static inline void jxl_destroy_cms_profile(void* p) { cmsCloseProfile(p); }
static inline void jxl_destroy_cms_transform(void* p) { cmsDeleteTransform(p); }

typedef jxl_cms_owned jxl_profile;

static inline void jxl_profile_init(jxl_profile* self) {
  jxl_cms_owned_init(self, NULL, jxl_destroy_cms_profile);
}

static inline void jxl_profile_reset(jxl_profile* self, void* ptr) {
  jxl_cms_owned_reset(self, ptr);
  if (self != NULL) self->destroy = jxl_destroy_cms_profile;
}

typedef jxl_cms_owned jxl_transform;

static inline void jxl_transform_init(jxl_transform* self) {
  jxl_cms_owned_init(self, NULL, jxl_destroy_cms_transform);
}

static inline void jxl_transform_init_with(jxl_transform* self, void* ptr) {
  jxl_cms_owned_init(self, ptr, jxl_destroy_cms_transform);
}

static jxl_enc_status jxl_create_profile_xyz(const cmsContext context,
                        jxl_profile* JXL_RESTRICT profile) {
  jxl_profile_reset(profile, cmsCreateXYZProfileTHR(context));
  if (jxl_cms_owned_get(profile) == NULL) return JXL_FAILURE("Failed to create XYZ");
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_decode_profile(const cmsContext context, const jxl_bytes* icc, jxl_profile* profile) {
  jxl_profile_reset(profile, cmsOpenProfileFromMemTHR(context, jxl_bytes_data(icc), jxl_bytes_size(icc)));
  if (jxl_cms_owned_get(profile) == NULL) {
    return JXL_FAILURE("Failed to decode profile");
  }

  // WARNING: due to the LCMS MD5 issue mentioned above, many existing
  // profiles have incorrect MD5, so do not even bother checking them nor
  // generating warning clutter.

  return jxl_enc_ok_status();
}

static uint32_t jxl_type64(const jxl_cms_color_encoding* c) {
  if (c->color_space == kGray) return TYPE_GRAY_DBL;
  return TYPE_RGB_DBL;
}

static jxl_color_space jxl_color_space_from_profile(const jxl_profile* profile) {
  switch (cmsGetColorSpace(jxl_cms_owned_get(profile))) {
    case cmsSigRgbData:
    case cmsSigCmykData:
      return kRGB;
    case cmsSigGrayData:
      return kGray;
    default:
      return kColorSpaceUnknown;
  }
}

// "profile1" is pre-decoded to save time in jxl_detect_transfer_function.
static jxl_enc_status jxl_profile_equivalent_to_icc(const cmsContext context, const jxl_profile* profile1,
                              const jxl_icc_bytes* icc, const jxl_cms_color_encoding* c) {
  const uint32_t type_src = jxl_type64(c);

  jxl_profile profile2;
  jxl_profile_init(&profile2);
  {
    jxl_bytes icc_bytes = jxl_bytes_make(jxl_array_data_const(icc), jxl_array_len(icc));
    jxl_enc_status status = jxl_decode_profile(context, &icc_bytes, &profile2);
    if (!jxl_enc_status_ok(status)) {
      jxl_cms_owned_destroy(&profile2);
      return status;
    }
  }

  jxl_profile profile_xyz;
  jxl_profile_init(&profile_xyz);
  jxl_enc_status status = jxl_create_profile_xyz(context, &profile_xyz);
  if (!jxl_enc_status_ok(status)) {
    jxl_cms_owned_destroy(&profile2);
    jxl_cms_owned_destroy(&profile_xyz);
    return status;
  }

  const uint32_t intent = INTENT_RELATIVE_COLORIMETRIC;
  const uint32_t flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_BLACKPOINTCOMPENSATION |
                         cmsFLAGS_HIGHRESPRECALC;
  jxl_transform xform1;
  jxl_transform_init_with(&xform1,
                     cmsCreateTransformTHR(context, jxl_cms_owned_get(profile1), type_src,
                                           jxl_cms_owned_get(&profile_xyz), TYPE_XYZ_DBL,
                                           intent, flags));
  jxl_transform xform2;
  jxl_transform_init_with(&xform2,
                     cmsCreateTransformTHR(context, jxl_cms_owned_get(&profile2), type_src,
                                           jxl_cms_owned_get(&profile_xyz), TYPE_XYZ_DBL,
                                           intent, flags));
  if (!jxl_cms_owned_ok(&xform1) || !jxl_cms_owned_ok(&xform2)) {
    status = JXL_FAILURE("Failed to create transform");
    jxl_cms_owned_destroy(&xform2);
    jxl_cms_owned_destroy(&xform1);
    jxl_cms_owned_destroy(&profile_xyz);
    jxl_cms_owned_destroy(&profile2);
    return status;
  }

  double in[3];
  double out1[3];
  double out2[3];

  // Uniformly spaced samples from very dark to almost fully bright.
  const double init = 1E-3;
  const double step = 0.2;

  if (c->color_space == kGray) {
    // Finer sampling and replicate each component.
    for (in[0] = init; in[0] < 1.0; in[0] += step / 8) {
      cmsDoTransform(jxl_cms_owned_get(&xform1), in, out1, 1);
      cmsDoTransform(jxl_cms_owned_get(&xform2), in, out2, 1);
      if (!jxl_cms_approx_eq(out1[0], out2[0], 2E-4)) {
        status = jxl_enc_error_status();
        jxl_cms_owned_destroy(&xform2);
        jxl_cms_owned_destroy(&xform1);
        jxl_cms_owned_destroy(&profile_xyz);
        jxl_cms_owned_destroy(&profile2);
        return status;
      }
    }
  } else {
    for (in[0] = init; in[0] < 1.0; in[0] += step) {
      for (in[1] = init; in[1] < 1.0; in[1] += step) {
        for (in[2] = init; in[2] < 1.0; in[2] += step) {
          cmsDoTransform(jxl_cms_owned_get(&xform1), in, out1, 1);
          cmsDoTransform(jxl_cms_owned_get(&xform2), in, out2, 1);
          for (size_t i = 0; i < 3; ++i) {
            if (!jxl_cms_approx_eq(out1[i], out2[i], 2E-4)) {
              status = jxl_enc_error_status();
              jxl_cms_owned_destroy(&xform2);
              jxl_cms_owned_destroy(&xform1);
              jxl_cms_owned_destroy(&profile_xyz);
              jxl_cms_owned_destroy(&profile2);
              return status;
            }
          }
        }
      }
    }
  }

  jxl_cms_owned_destroy(&xform2);
  jxl_cms_owned_destroy(&xform1);
  jxl_cms_owned_destroy(&profile_xyz);
  jxl_cms_owned_destroy(&profile2);
  return jxl_enc_ok_status();
}

// Returns white point that was specified when creating the profile.
// NOTE: we can't just use cmsSigMediaWhitePointTag because its interpretation
// differs between ICC versions.
JXL_MUST_USE_RESULT static cmsCIEXYZ jxl_unadapted_white_point(const cmsContext context,
                                                  const jxl_profile* profile,
                                                  const jxl_cms_color_encoding* c) {
  const cmsCIEXYZ* white_point = (const cmsCIEXYZ*)(
      cmsReadTag(jxl_cms_owned_get(profile), cmsSigMediaWhitePointTag));
  if (white_point != NULL &&
      cmsReadTag(jxl_cms_owned_get(profile), cmsSigChromaticAdaptationTag) == NULL) {
    // No chromatic adaptation matrix: the white point is already unadapted.
    return *white_point;
  }

  cmsCIEXYZ XYZ = {1.0, 1.0, 1.0};
  jxl_profile profile_xyz;
  jxl_profile_init(&profile_xyz);
  if (!jxl_enc_status_ok(jxl_create_profile_xyz(context, &profile_xyz))) {
    jxl_cms_owned_destroy(&profile_xyz);
    return XYZ;
  }
  // Array arguments are one per profile->
  cmsHPROFILE profiles[2] = {jxl_cms_owned_get(profile), jxl_cms_owned_get(&profile_xyz)};
  // Leave white point unchanged - that is what we're trying to extract.
  cmsUInt32Number intents[2] = {INTENT_ABSOLUTE_COLORIMETRIC,
                                INTENT_ABSOLUTE_COLORIMETRIC};
  cmsBool black_compensation[2] = {0, 0};
  cmsFloat64Number adaption[2] = {0.0, 0.0};
  // Only transforming a single pixel, so skip expensive optimizations.
  cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC;
  jxl_transform xform;
  jxl_transform_init_with(&xform,
                    cmsCreateExtendedTransform(
                        context, 2, profiles, black_compensation, intents,
                        adaption, NULL, 0, jxl_type64(c), TYPE_XYZ_DBL, flags));
  if (!jxl_cms_owned_ok(&xform)) {
    jxl_cms_owned_destroy(&xform);
    jxl_cms_owned_destroy(&profile_xyz);
    return XYZ;  // TODO(lode): return error
  }

  // xy are relative, so magnitude does not matter if we ignore output Y.
  const cmsFloat64Number in[3] = {1.0, 1.0, 1.0};
  cmsDoTransform(jxl_cms_owned_get(&xform), in, &XYZ.X, 1);
  jxl_cms_owned_destroy(&xform);
  jxl_cms_owned_destroy(&profile_xyz);
  return XYZ;
}

static jxl_enc_status jxl_identify_primaries(const cmsContext context, const jxl_profile* profile,
                         const cmsCIEXYZ* wp_unadapted, jxl_cms_color_encoding* c) {
  if (!jxl_cms_color_encoding_has_primaries(c)) return jxl_enc_ok_status();
  if (jxl_color_space_from_profile(profile) == kColorSpaceUnknown) return jxl_enc_ok_status();

  // These were adapted to the profile illuminant before storing in the profile->
  const cmsCIEXYZ* adapted_r = (const cmsCIEXYZ*)(
      cmsReadTag(jxl_cms_owned_get(profile), cmsSigRedColorantTag));
  const cmsCIEXYZ* adapted_g = (const cmsCIEXYZ*)(
      cmsReadTag(jxl_cms_owned_get(profile), cmsSigGreenColorantTag));
  const cmsCIEXYZ* adapted_b = (const cmsCIEXYZ*)(
      cmsReadTag(jxl_cms_owned_get(profile), cmsSigBlueColorantTag));

  cmsCIEXYZ converted_rgb[3];
  if (adapted_r == NULL || adapted_g == NULL || adapted_b == NULL) {
    // No colorant tag, determine the XYZ coordinates of the primaries by
    // converting from the colorspace.
    jxl_profile profile_xyz;
    jxl_profile_init(&profile_xyz);
    if (!jxl_enc_status_ok(jxl_create_profile_xyz(context, &profile_xyz))) {
      jxl_cms_owned_destroy(&profile_xyz);
      return JXL_FAILURE("Failed to retrieve colorants");
    }
    // Array arguments are one per profile->
    cmsHPROFILE profiles[2] = {jxl_cms_owned_get(profile), jxl_cms_owned_get(&profile_xyz)};
    cmsUInt32Number intents[2] = {INTENT_RELATIVE_COLORIMETRIC,
                                  INTENT_RELATIVE_COLORIMETRIC};
    cmsBool black_compensation[2] = {0, 0};
    cmsFloat64Number adaption[2] = {0.0, 0.0};
    // Only transforming three pixels, so skip expensive optimizations.
    cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC;
    jxl_transform xform;
    jxl_transform_init_with(&xform,
                      cmsCreateExtendedTransform(
                          context, 2, profiles, black_compensation, intents,
                          adaption, NULL, 0, jxl_type64(c), TYPE_XYZ_DBL, flags));
    if (!jxl_cms_owned_ok(&xform)) {
      jxl_cms_owned_destroy(&xform);
      jxl_cms_owned_destroy(&profile_xyz);
      return JXL_FAILURE("Failed to retrieve colorants");
    }

    const cmsFloat64Number in[9] = {1.0, 0.0, 0.0, 0.0, 1.0,
                                    0.0, 0.0, 0.0, 1.0};
    cmsDoTransform(jxl_cms_owned_get(&xform), in, &converted_rgb->X, 3);
    adapted_r = &converted_rgb[0];
    adapted_g = &converted_rgb[1];
    adapted_b = &converted_rgb[2];
    jxl_cms_owned_destroy(&xform);
    jxl_cms_owned_destroy(&profile_xyz);
  }

  // TODO(janwas): no longer assume Bradford and D50.
  // Undo the chromatic adaptation.
  const cmsCIEXYZ d50 = D50_XYZ();

  cmsCIEXYZ r, g, b;
  cmsAdaptToIlluminant(&r, &d50, wp_unadapted, adapted_r);
  cmsAdaptToIlluminant(&g, &d50, wp_unadapted, adapted_g);
  cmsAdaptToIlluminant(&b, &d50, wp_unadapted, adapted_b);

  const jxl_primaries_ci_exy rgb = {jxl_ci_exy_from_xyz(&r), jxl_ci_exy_from_xyz(&g),
                              jxl_ci_exy_from_xyz(&b)};
  return jxl_cms_color_encoding_set_primaries(c, &rgb);
}

static jxl_enc_status jxl_detect_transfer_function(const cmsContext context, const jxl_profile* profile,
                              jxl_cms_color_encoding* JXL_RESTRICT c) {
  JXL_ENSURE(c->color_space != kXYB);

  float gamma = 0;
  {
    const cmsToneCurve* gray_trc = (const cmsToneCurve*)(
        cmsReadTag(jxl_cms_owned_get(profile), cmsSigGrayTRCTag));
    if (gray_trc) {
      const double estimated_gamma =
          cmsEstimateGamma(gray_trc, /*precision=*/1e-4);
      if (estimated_gamma > 0) {
        gamma = 1. / estimated_gamma;
      }
    } else {
    float rgb_gamma[3];
    memset(rgb_gamma, 0, sizeof(rgb_gamma));
    int i = 0;
    static const cmsTagSignature kTrcTags[] = {
        cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag};
    for (size_t tag_i = 0; tag_i < 3; ++tag_i) {
      cmsTagSignature tag = kTrcTags[tag_i];
      const cmsToneCurve* trc = (const cmsToneCurve*)(
          cmsReadTag(jxl_cms_owned_get(profile), tag));
      if (trc) {
        const double estimated_gamma =
            cmsEstimateGamma(trc, /*precision=*/1e-4);
        if (estimated_gamma > 0) {
          rgb_gamma[i] = 1. / estimated_gamma;
        }
      }
      ++i;
    }
    if (rgb_gamma[0] != 0 && fabs(rgb_gamma[0] - rgb_gamma[1]) < 1e-4f &&
        fabs(rgb_gamma[1] - rgb_gamma[2]) < 1e-4f) {
      gamma = rgb_gamma[0];
    }
    }
  }

  if (gamma != 0 && jxl_enc_status_ok(jxl_cms_custom_transfer_function_set_gamma(&c->tf, gamma))) {
    jxl_icc_bytes icc_test;
    jxl_array_construct_empty(&icc_test, (jxl_context*)cmsGetContextUserData(context));
    jxl_color_encoding external = jxl_cms_color_encoding_to_external(c);
    if (jxl_enc_status_ok(jxl_maybe_create_profile(&external, &icc_test)) &&
        jxl_enc_status_ok(jxl_profile_equivalent_to_icc(context, profile, &icc_test, c))) {
      return jxl_enc_ok_status();
    }
  }

  static const jxl_transfer_function kKnownTf[] = {
      kTF709, kTFLinear,
      kTFSRGB, kTFPQ, kTFDCI,
      kTFHLG};
  for (size_t tf_i = 0; tf_i < 6; ++tf_i) {
    jxl_transfer_function tf = kKnownTf[tf_i];
    jxl_cms_custom_transfer_function_set_transfer_function(&c->tf, tf);

    jxl_icc_bytes icc_test;
    jxl_array_construct_empty(&icc_test, (jxl_context*)cmsGetContextUserData(context));
    jxl_color_encoding external = jxl_cms_color_encoding_to_external(c);
    if (jxl_enc_status_ok(jxl_maybe_create_profile(&external, &icc_test)) &&
        jxl_enc_status_ok(jxl_profile_equivalent_to_icc(context, profile, &icc_test, c))) {
      return jxl_enc_ok_status();
    }
  }

  jxl_cms_custom_transfer_function_set_transfer_function(&c->tf, kTFUnknown);
  return jxl_enc_ok_status();
}

static void jxl_error_handler(cmsContext context, cmsUInt32Number code, const char* text) {
  JXL_WARNING("LCMS error %u: %s", code, text);
}

static bool jxl_is_known_transfer_function(jxl_transfer_function tf) {
  // All but kTFUnknown
  return tf == kTF709 || tf == kTFLinear || tf == kTFSRGB ||
         tf == kTFPQ || tf == kTFDCI || tf == kTFHLG;
}

static const uint8_t kColorPrimariesP3_D65 = 12;

static bool jxl_is_known_color_primaries(uint8_t color_primaries) {
  // All but kPrimariesCustom
  if (color_primaries == kColorPrimariesP3_D65) return true;
  const jxl_primaries p = (jxl_primaries)(color_primaries);
  return p == kPrimariesSRGB || p == kPrimaries2100 || p == kPrimariesP3;
}

static bool jxl_apply_cicp(const uint8_t color_primaries,
               const uint8_t transfer_characteristics,
               const uint8_t matrix_coefficients, const uint8_t full_range,
               jxl_cms_color_encoding* JXL_RESTRICT c) {
  if (matrix_coefficients != 0) return false;
  if (full_range != 1) return false;

  const jxl_primaries primaries = (jxl_primaries)(color_primaries);
  const jxl_transfer_function tf = (jxl_transfer_function)(transfer_characteristics);
  if (!jxl_is_known_transfer_function(tf)) return false;
  if (!jxl_is_known_color_primaries(color_primaries)) return false;
  c->color_space = kRGB;
  jxl_cms_custom_transfer_function_set_transfer_function(&c->tf, tf);
  if (primaries == kPrimariesP3) {
    c->white_point = kWhitePointDCI;
    c->primaries = kPrimariesP3;
  } else if (color_primaries == kColorPrimariesP3_D65) {
    c->white_point = kWhitePointD65;
    c->primaries = kPrimariesP3;
  } else {
    c->white_point = kWhitePointD65;
    c->primaries = primaries;
  }
  return true;
}

static JXL_BOOL jxl_cms_set_fields_from_icc(void* user_data, const uint8_t* icc_data,
                                size_t icc_size, jxl_color_encoding* c,
                                JXL_BOOL* cmyk) {
  if (c == NULL) return JXL_FALSE;
  if (cmyk == NULL) return JXL_FALSE;

  *cmyk = JXL_FALSE;

  // In case parsing fails, mark the jxl_cms_color_encoding as invalid.
  c->color_space = JXL_COLOR_SPACE_UNKNOWN;
  c->transfer_function = JXL_TRANSFER_FUNCTION_UNKNOWN;

  if (icc_size == 0) {
    (void)JXL_FAILURE("Empty ICC profile");
    return JXL_FALSE;
  }

  /* user_data must be the cmsContext owned by jxl_context (always required). */
  if (user_data == NULL) {
    (void)JXL_FAILURE("CMS requires a library context LCMS handle");
    return JXL_FALSE;
  }
  const cmsContext context = (cmsContext)user_data;
  jxl_context* mm = (jxl_context*)cmsGetContextUserData(context);
  jxl_cms_color_encoding c_enc;
  jxl_cms_color_encoding_construct_empty(&c_enc, mm);

  jxl_profile profile;
  jxl_profile_init(&profile);
  {
    jxl_bytes icc_bytes = jxl_bytes_make(icc_data, icc_size);
    if (!jxl_enc_status_ok(jxl_decode_profile(context, &icc_bytes, &profile))) {
      jxl_cms_owned_destroy(&profile);
      return JXL_FALSE;
    }
  }

  const cmsUInt32Number rendering_intent32 =
      cmsGetHeaderRenderingIntent(jxl_cms_owned_get(&profile));
  if (rendering_intent32 > 3) {
    (void)JXL_FAILURE("Invalid rendering intent %u\n", rendering_intent32);
    jxl_cms_owned_destroy(&profile);
    return JXL_FALSE;
  }
  // ICC and jxl_rendering_intent have the same values (0..3).
  c_enc.rendering_intent = (jxl_rendering_intent)(rendering_intent32);

  static const size_t kCICPSize = 12;
  static const cmsTagSignature kCICPSignature =
      (cmsTagSignature)(0x63696370);
  uint8_t cicp_buffer[kCICPSize];
  if (cmsReadRawTag(jxl_cms_owned_get(&profile), kCICPSignature, cicp_buffer, kCICPSize) ==
          kCICPSize &&
      jxl_apply_cicp(cicp_buffer[8], cicp_buffer[9], cicp_buffer[10],
                cicp_buffer[11], &c_enc)) {
    *c = jxl_cms_color_encoding_to_external(&c_enc);
    jxl_cms_owned_destroy(&profile);
    return JXL_TRUE;
  }

  c_enc.color_space = jxl_color_space_from_profile(&profile);
  if (cmsGetColorSpace(jxl_cms_owned_get(&profile)) == cmsSigCmykData) {
    *cmyk = JXL_TRUE;
    *c = jxl_cms_color_encoding_to_external(&c_enc);
    jxl_cms_owned_destroy(&profile);
    return JXL_TRUE;
  }

  const cmsCIEXYZ wp_unadapted = jxl_unadapted_white_point(context, &profile, &c_enc);
  {
    jxl_ci_exy wp = jxl_ci_exy_from_xyz(&wp_unadapted);
    if (!jxl_enc_status_ok(jxl_cms_color_encoding_set_white_point(&c_enc, &wp))) {
      jxl_cms_owned_destroy(&profile);
      return JXL_FALSE;
    }
  }

  // Relies on color_space.
  if (!jxl_enc_status_ok(jxl_identify_primaries(context, &profile, &wp_unadapted, &c_enc))) {
    jxl_cms_owned_destroy(&profile);
    return JXL_FALSE;
  }

  // Relies on color_space/white point/primaries being set already.
  if (!jxl_enc_status_ok(jxl_detect_transfer_function(context, &profile, &c_enc))) {
    jxl_cms_owned_destroy(&profile);
    return JXL_FALSE;
  }

  *c = jxl_cms_color_encoding_to_external(&c_enc);
  jxl_cms_owned_destroy(&profile);
  return JXL_TRUE;
}

/* LCMS allocations go through the jxl_context stored as context user data. */
static jxl_context* jxl_cms_mm(cmsContext ctx) {
  return (jxl_context*)cmsGetContextUserData(ctx);
}

static void* jxl_cms_lcms_malloc(cmsContext ctx, cmsUInt32Number size) {
  jxl_context* mm = jxl_cms_mm(ctx);
  size_t* block;
  if (mm == NULL || size == 0) return NULL;
  block = (size_t*)jxl_alloc(mm, (size_t)size + sizeof(size_t));
  if (block == NULL) return NULL;
  *block = (size_t)size;
  return block + 1;
}

static void jxl_cms_lcms_free(cmsContext ctx, void* ptr) {
  jxl_context* mm = jxl_cms_mm(ctx);
  if (mm == NULL || ptr == NULL) return;
  jxl_free(mm, (size_t*)ptr - 1);
}

static void* jxl_cms_lcms_realloc(cmsContext ctx, void* ptr,
                                  cmsUInt32Number new_size) {
  jxl_context* mm = jxl_cms_mm(ctx);
  size_t old_size;
  size_t copy;
  void* grown;
  if (mm == NULL) return NULL;
  if (ptr == NULL) return jxl_cms_lcms_malloc(ctx, new_size);
  if (new_size == 0) {
    jxl_cms_lcms_free(ctx, ptr);
    return NULL;
  }
  old_size = *((size_t*)ptr - 1);
  grown = jxl_cms_lcms_malloc(ctx, new_size);
  if (grown == NULL) return NULL;
  copy = old_size < (size_t)new_size ? old_size : (size_t)new_size;
  memcpy(grown, ptr, copy);
  jxl_cms_lcms_free(ctx, ptr);
  return grown;
}

static void* jxl_cms_lcms_malloc_zero(cmsContext ctx, cmsUInt32Number size) {
  void* p = jxl_cms_lcms_malloc(ctx, size);
  if (p != NULL && size > 0) memset(p, 0, (size_t)size);
  return p;
}

static void* jxl_cms_lcms_calloc(cmsContext ctx, cmsUInt32Number num,
                                 cmsUInt32Number size) {
  cmsUInt32Number bytes;
  if (num == 0 || size == 0) return NULL;
  if (num > (cmsUInt32Number)(~(cmsUInt32Number)0 / size)) return NULL;
  bytes = num * size;
  return jxl_cms_lcms_malloc_zero(ctx, bytes);
}

static void* jxl_cms_lcms_dup(cmsContext ctx, const void* org,
                              cmsUInt32Number size) {
  void* copy = jxl_cms_lcms_malloc(ctx, size);
  if (copy != NULL && org != NULL && size > 0) {
    memcpy(copy, org, (size_t)size);
  }
  return copy;
}

static cmsPluginMemHandler k_jxl_cms_mem_plugin = {
    {cmsPluginMagicNumber, LCMS_VERSION, cmsPluginMemHandlerSig, NULL},
    jxl_cms_lcms_malloc,
    jxl_cms_lcms_free,
    jxl_cms_lcms_realloc,
    jxl_cms_lcms_malloc_zero,
    jxl_cms_lcms_calloc,
    jxl_cms_lcms_dup,
};

#ifdef __cplusplus
extern "C" {
#endif

JXL_CMS_EXPORT const jxl_cms_interface* jxl_get_default_cms() {
  // JPEG encoder only uses set_fields_from_icc; pixel transforms are unused.
  // Callers must set set_fields_data to a cmsContext from jxl_cms_create_lcms_context
  // (jxl_encoder_create does this from the jxl_context).
  static const jxl_cms_interface kInterface = {
      /*set_fields_data=*/NULL,
      /*set_fields_from_icc=*/&jxl_cms_set_fields_from_icc,
      /*init_data=*/NULL,
      /*init=*/NULL,
      /*get_src_buf=*/NULL,
      /*get_dst_buf=*/NULL,
      /*run=*/NULL,
      /*destroy=*/NULL};
  return &kInterface;
}

JXL_CMS_EXPORT void* jxl_cms_create_lcms_context(jxl_context* ctx) {
  cmsContext lcms;
  if (ctx == NULL) {
    return NULL;
  }
  lcms = cmsCreateContext(&k_jxl_cms_mem_plugin, ctx);
  if (lcms != NULL) {
    cmsSetLogErrorHandlerTHR(lcms, &jxl_error_handler);
  }
  return lcms;
}

JXL_CMS_EXPORT void jxl_cms_destroy_lcms_context(void* lcms_context) {
  if (lcms_context != NULL) {
    jxl_destroy_cms_context(lcms_context);
  }
}

#ifdef __cplusplus
}  // extern "C"
#endif

