// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Transfer functions for color encodings.

#ifndef LIB_JXL_CMS_TRANSFER_FUNCTIONS_H_
#define LIB_JXL_CMS_TRANSFER_FUNCTIONS_H_

#include <math.h>

#include "base/common.h"
#include "base/enc_status.h"

// Definitions for BT.2100-2 transfer functions (used inside/outside SIMD):
// "display" is linear light (nits) normalized to [0, 1].
// "encoded" is a nonlinear encoding (e.g. PQ) in [0, 1].
// "scene" is a linear function of photon counts, normalized to [0, 1].

// Despite the stated ranges, we need unbounded transfer functions: see
// http://www.littlecms.com/CIC18_UnboundedCMM.pdf. Inputs can be negative or
// above 1 due to chromatic adaptation. To avoid severe round-trip errors caused
// by clamping, we mirror negative inputs via copysign (f(-x) = -f(x), see
// https://developer.apple.com/documentation/coregraphics/cgcolorspace/1644735-extendedsrgb)
// and extend the function domains above 1.

// Hybrid Log-Gamma.
#define kTFHlgA 0.17883277
#define kTFHlgRA (1.0 / kTFHlgA)
#define kTFHlgB (1 - 4 * kTFHlgA)
#define kTFHlgC 0.5599107295
#define kTFHlgInv12 (1.0 / 12.0)

// e = encoded, returns scene (= display here).
static inline double TF_HLG_BaseInvOETF(double e) {
  if (e == 0.0) return 0.0;
  const double original_sign = e;
  e = fabs(e);

  if (e <= 0.5) return copysignf(e * e * (1.0 / 3), original_sign);

  const double s =
      (exp((e - kTFHlgC) * kTFHlgRA) + kTFHlgB) * kTFHlgInv12;
  JXL_DASSERT(s >= 0);
  return copysignf(s, original_sign);
}

// EOTF. e = encoded. (OOTF is identity at the HLG system gamma we use.)
static inline double TF_HLG_BaseDisplayFromEncoded(const double e) {
  return TF_HLG_BaseInvOETF(e);
}

// Perceptual Quantization
#define kTFPQM1 (2610.0 / 16384)
#define kTFPQM2 ((2523.0 / 4096) * 128)
#define kTFPQC1 (3424.0 / 4096)
#define kTFPQC2 ((2413.0 / 4096) * 32)
#define kTFPQC3 ((2392.0 / 4096) * 32)

// EOTF (defines the PQ approach). e = encoded, returns display.
static inline double TF_PQ_BaseDisplayFromEncoded(float display_intensity_target,
                                                  double e) {
  if (e == 0.0) return 0.0;
  const double original_sign = e;
  e = fabs(e);

  const double xp = pow(e, 1.0 / kTFPQM2);
  const double num = JXL_MAX(xp - kTFPQC1, 0.0);
  const double den = kTFPQC2 - kTFPQC3 * xp;
  JXL_DASSERT(den != 0.0);
  const double d = pow(num / den, 1.0 / kTFPQM1);
  JXL_DASSERT(d >= 0.0);  // Equal for e ~= 1E-9
  return copysignf(d * (10000.0f / display_intensity_target), original_sign);
}

#endif  // LIB_JXL_CMS_TRANSFER_FUNCTIONS_H_
