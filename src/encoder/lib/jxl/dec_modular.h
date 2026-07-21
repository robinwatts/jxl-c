// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_DEC_MODULAR_H_
#define LIB_JXL_DEC_MODULAR_H_

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/status.h"
#include "lib/jxl/frame_dimensions.h"
#include "lib/jxl/quant_weights.h"


typedef enum jxl_modular_stream_kind {
  kModularStreamGlobalData,
  kModularStreamVarDCTDC,
  kModularStreamACMetadata,
  kModularStreamQuantTable,
} jxl_modular_stream_kind;

typedef struct jxl_modular_stream_id {
  jxl_modular_stream_kind kind;
  size_t quant_table_id;
  size_t group_id;
} jxl_modular_stream_id;

static inline jxl_modular_stream_id jxl_modular_stream_id_make(jxl_modular_stream_kind kind,
                                           size_t quant_table_id,
                                           size_t group_id) {
  jxl_modular_stream_id id;
  id.kind = kind;
  id.quant_table_id = quant_table_id;
  id.group_id = group_id;
  return id;
}

static inline size_t jxl_modular_stream_id_id(jxl_modular_stream_id self,
                                const jxl_frame_dimensions* frame_dim) {
  size_t id = 0;
  switch (self.kind) {
    case kModularStreamGlobalData:
      id = 0;
      break;
    case kModularStreamVarDCTDC:
      id = 1 + self.group_id;
      break;
    case kModularStreamACMetadata:
      // After Global + VarDCTDC groups + empty ModularDC group slots.
      id = 1 + 2 * frame_dim->num_dc_groups + self.group_id;
      break;
    case kModularStreamQuantTable:
      id = 1 + 3 * frame_dim->num_dc_groups + self.quant_table_id;
      break;
  };
  return id;
}
static inline jxl_modular_stream_id jxl_modular_stream_id_global(void) {
  return jxl_modular_stream_id_make(kModularStreamGlobalData, 0, 0);
}
static inline jxl_modular_stream_id jxl_modular_stream_id_var_dctdc(size_t group_id) {
  return jxl_modular_stream_id_make(kModularStreamVarDCTDC, 0, group_id);
}
static inline jxl_modular_stream_id jxl_modular_stream_id_ac_metadata(size_t group_id) {
  return jxl_modular_stream_id_make(kModularStreamACMetadata, 0, group_id);
}
static inline jxl_modular_stream_id jxl_modular_stream_id_quant_table(size_t quant_table_id) {
  JXL_DASSERT(quant_table_id < kNumQuantTables);
  return jxl_modular_stream_id_make(kModularStreamQuantTable, quant_table_id, 0);
}
// Stream count keeps empty ModularDC slots and ModularAC pass slots so
// tree_splits / stream layout stay bitstream-compatible.
static inline size_t jxl_modular_stream_id_num(const jxl_frame_dimensions* frame_dim,
                                 size_t passes) {
  return 1 + 3 * frame_dim->num_dc_groups + kNumQuantTables +
         frame_dim->num_groups * passes;
}


#endif  // LIB_JXL_DEC_MODULAR_H_
