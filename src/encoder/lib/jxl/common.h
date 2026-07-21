// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_COMMON_H_
#define LIB_JXL_COMMON_H_

// Shared constants.

#include <stddef.h>
#include <stdint.h>

// Some enums and typedefs used by more than one header file.

// The 12-byte JXL container signature box (all valid containers start with it).
static const uint8_t kJxlSignatureBox[12] = {
    0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A};
enum { kJxlSignatureBoxSize = 12 };

// Maximum number of passes in an image.
enum { kMaxNumPasses = 11 };

typedef enum jxl_speed_tier {
  // Learn a global tree in Modular mode.
  kGlacier = 0,
  // Turns on FindBestQuantizationHQ loop. Equivalent to "guetzli" mode.
  kTortoise = 1,
  // Turns on FindBestQuantization butteraugli loop.
  kKitten = 2,
  // Turns on dots, patches, and spline detection by default, as well as full
  // context clustering. Default.
  kSquirrel = 3,
  // Turns on error diffusion and full AC strategy heuristics. Equivalent to
  // "fast" mode.
  kWombat = 4,
  // Turns on simple heuristics for AC strategy, quant field, gaborish by default, non-default cmap, initial quant field, non-default CFL.
  kHare = 5,
  // Turns on clustering and enables coefficient reordering.
  kCheetah = 6,
  // Turns off most encoder features. Does context clustering.
  // Modular: uses fixed tree with Weighted predictor.
  kFalcon = 7,
  // Currently fastest possible setting for VarDCT.
  // Modular: uses fixed tree with Gradient predictor.
  kThunder = 8,
  // VarDCT: same as kThunder.
  // Modular: no tree, Gradient predictor, fast histograms
  kLightning = 9
} jxl_speed_tier;

#endif  // LIB_JXL_COMMON_H_
