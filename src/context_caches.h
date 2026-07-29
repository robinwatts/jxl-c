// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTEXT_CACHES_H_
#define JXL_CONTEXT_CACHES_H_

/*
 * Decoder production caches owned by jxl_context. Kept out of context.h so
 * encoder TUs can include the thin context layout without modular/vardct.
 */

#include "vardct/coeff_order.h"

#include <stddef.h>

#ifndef JXL_DEQUANT_MATRIX_COUNT
#define JXL_DEQUANT_MATRIX_COUNT 17
#endif

typedef struct {
    float *data;
    size_t len;
} jxl_context_dequant_buf;

typedef struct jxl_context_dequant {
    jxl_context_dequant_buf weights[JXL_DEQUANT_MATRIX_COUNT][3];
    jxl_context_dequant_buf weights_tr[JXL_DEQUANT_MATRIX_COUNT][3];
} jxl_context_dequant;

typedef struct jxl_context_hf_orders {
    int initialized;
    jxl_coeff_order natural_8x8[64];
    jxl_coeff_order natural_16x16[256];
    jxl_coeff_order natural_32x32[1024];
    jxl_coeff_order natural_16x8[128];
    jxl_coeff_order natural_32x8[256];
    jxl_coeff_order natural_32x16[512];
    jxl_coeff_order natural_64x64[4096];
    jxl_coeff_order natural_64x32[2048];
    jxl_coeff_order natural_128x128[16384];
} jxl_context_hf_orders;

#endif /* JXL_CONTEXT_CACHES_H_ */
