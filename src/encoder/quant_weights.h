// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_QUANT_WEIGHTS_H_
#define LIB_JXL_QUANT_WEIGHTS_H_

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stdint.h>

#include "base/array.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"

enum {
  kNumPredefinedTables = 1,
  kCeilLog2NumPredefinedTables = 0,
  kLog2NumQuantModes = 3
};

// Mode values are part of the quantization-table wire format; keep unused
// modes so Library/RAW bit patterns stay unchanged.
// Trivially copyable: RAW qtables live on jxl_dequant_matrices::raw_qtables_.
typedef enum jxl_quant_encoding_mode {
  kQuantModeLibrary = 0,
  kQuantModeID = 1,
  kQuantModeDCT2 = 2,
  kQuantModeDCT4 = 3,
  kQuantModeDCT4X8 = 4,
  kQuantModeAFV = 5,
  kQuantModeDCT = 6,
  kQuantModeRAW = 7,
} jxl_quant_encoding_mode;

typedef struct jxl_quant_encoding {
  jxl_quant_encoding_mode mode;
  // Which predefined table to use. Only used if mode is kQuantModeLibrary.
  uint8_t predefined;
  // Only used in kQuantModeRAW mode.
  float qtable_den;
} jxl_quant_encoding;

static inline jxl_quant_encoding jxl_quant_encoding_library(size_t index) {
  jxl_quant_encoding encoding;
  JXL_DASSERT(index < kNumPredefinedTables);
  encoding.mode = kQuantModeLibrary;
  encoding.predefined = (uint8_t)(index);
  encoding.qtable_den = 1.f / (8 * 255);
  return encoding;
}

// RAW mode metadata only; qtable storage is passed separately to
// jxl_dequant_matrices / AddQuantTable.
static inline jxl_quant_encoding jxl_quant_encoding_raw(int shift) {
  jxl_quant_encoding encoding;
  encoding.mode = kQuantModeRAW;
  encoding.predefined = 0;
  encoding.qtable_den = (1 << shift) * (1.f / (8 * 255));
  return encoding;
}

JXL_DEFINE_POD_ARRAY(jxl_array_quant_encoding, jxl_quant_encoding)

// Let's try to keep these 2**N for possible future simplicity.
static const float kInvDCQuant[3] = {
    4096.0f,
    512.0f,
    256.0f,
};

static const float kDCQuant[3] = {
    1.0f / 4096.0f,
    1.0f / 512.0f,
    1.0f / 256.0f,
};

typedef struct jxl_modular_frame_encoder jxl_modular_frame_encoder;

typedef enum jxl_quant_table {
  kQT_DCT = 0,
  kQT_IDENTITY,
  kQT_DCT2X2,
  kQT_DCT4X4,
  kQT_DCT16X16,
  kQT_DCT32X32,
  // DCT16X8
  kQT_DCT8X16,
  // DCT32X8
  kQT_DCT8X32,
  // DCT32X16
  kQT_DCT16X32,
  kQT_DCT4X8,
  // DCT8X4
  kQT_AFV0,
  // AFV1
  // AFV2
  // AFV3
  kQT_DCT64X64,
  // DCT64X32,
  kQT_DCT32X64,
  kQT_DCT128X128,
  // DCT128X64,
  kQT_DCT64X128,
  kQT_DCT256X256,
  // DCT256X128,
  kQT_DCT128X256
} jxl_quant_table;

enum { kNumQuantTables = (int)kQT_DCT128X256 + 1 };

typedef struct jxl_dequant_matrices {
  float dc_quant_[3];
  float inv_dc_quant_[3];
  jxl_array_quant_encoding encodings_;
  jxl_array_int raw_qtables_[kNumQuantTables];
} jxl_dequant_matrices;

void jxl_dequant_matrices_init(jxl_dequant_matrices* self,
                               jxl_context* ctx);

static const int kDequantMatricesRequiredSizeX[kNumQuantTables] = {
    1, 1, 1, 1, 2, 4, 1, 1, 2, 1, 1, 8, 4, 16, 8, 32, 16};
JXL_STATIC_ASSERT(sizeof(kDequantMatricesRequiredSizeX) /
                      sizeof(kDequantMatricesRequiredSizeX[0]) ==
                      kNumQuantTables,
                  "Update this array when adding or removing quant tables.");

static const int kDequantMatricesRequiredSizeY[kNumQuantTables] = {
    1, 1, 1, 1, 2, 4, 2, 4, 4, 1, 1, 8, 8, 16, 16, 32, 32};
JXL_STATIC_ASSERT(sizeof(kDequantMatricesRequiredSizeY) /
                      sizeof(kDequantMatricesRequiredSizeY[0]) ==
                      kNumQuantTables,
                  "Update this array when adding or removing quant tables.");

static inline float jxl_dequant_matrices_dc_quant(const jxl_dequant_matrices* self,
                                           size_t c) {
  return self->dc_quant_[c];
}
static inline const float* jxl_dequant_matrices_dc_quants(const jxl_dequant_matrices* self) {
  return self->dc_quant_;
}
static inline float jxl_dequant_matrices_inv_dc_quant(const jxl_dequant_matrices* self,
                                              size_t c) {
  return self->inv_dc_quant_[c];
}

static inline void jxl_dequant_matrices_set_encodings(
    jxl_dequant_matrices* self, const jxl_array_quant_encoding* encodings,
    jxl_array_int* raw_qtables) {
  size_t i;
  if (!jxl_enc_status_ok(jxl_array_copy_from(&self->encodings_, encodings))) JXL_CRASH();
  for (i = 0; i < kNumQuantTables; ++i) {
    jxl_array_swap(&self->raw_qtables_[i], &raw_qtables[i]);
  }
}

static inline void jxl_dequant_matrices_set_dc_quant(jxl_dequant_matrices* self,
                                             const float dc[3]) {
  size_t c;
  for (c = 0; c < 3; c++) {
    self->dc_quant_[c] = 1.0f / dc[c];
    self->inv_dc_quant_[c] = dc[c];
  }
}

static inline void jxl_dequant_matrices_set_dc_quant_decoded(
    jxl_dequant_matrices* self, const float dc_quant[3]) {
  size_t c;
  for (c = 0; c < 3; c++) {
    self->dc_quant_[c] = dc_quant[c];
    self->inv_dc_quant_[c] = 1.0f / dc_quant[c];
  }
}

static inline const jxl_array_quant_encoding* jxl_dequant_matrices_encodings(
    const jxl_dequant_matrices* self) {
  return &self->encodings_;
}
static inline const jxl_array_int* jxl_dequant_matrices_raw_q_table(
    const jxl_dequant_matrices* self, size_t i) {
  return &self->raw_qtables_[i];
}

static inline void jxl_dequant_matrices_destroy(jxl_dequant_matrices* self) {
  size_t i;
  jxl_array_destroy(&self->encodings_);
  for (i = 0; i < kNumQuantTables; ++i) {
    jxl_array_destroy(&self->raw_qtables_[i]);
  }
}

#endif  // LIB_JXL_QUANT_WEIGHTS_H_
