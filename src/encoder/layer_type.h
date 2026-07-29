// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_LAYER_TYPE_H_
#define JXL_ENC_LAYER_TYPE_H_

// Bitstream layer tags used by encoder writers (WithMaxBits / allotments).

#include <stdint.h>

typedef enum jxl_layer_type {
  kLayerHeader = 0,
  kLayerToc,
  kLayerQuant,
  kLayerModularTree,
  kLayerModularGlobal,
  kLayerDc,
  kLayerControlFields,
  kLayerOrder,
  kLayerAc,
  kLayerAcTokens,
} jxl_layer_type;

#endif  // JXL_ENC_LAYER_TYPE_H_
