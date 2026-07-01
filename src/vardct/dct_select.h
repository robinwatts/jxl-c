// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_DCT_SELECT_H_
#define JXL_VARDCT_DCT_SELECT_H_

#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_TRANSFORM_DCT8 = 0,
    JXL_TRANSFORM_HORNUSS,
    JXL_TRANSFORM_DCT2,
    JXL_TRANSFORM_DCT4,
    JXL_TRANSFORM_DCT16,
    JXL_TRANSFORM_DCT32,
    JXL_TRANSFORM_DCT16X8,
    JXL_TRANSFORM_DCT8X16,
    JXL_TRANSFORM_DCT32X8,
    JXL_TRANSFORM_DCT8X32,
    JXL_TRANSFORM_DCT32X16,
    JXL_TRANSFORM_DCT16X32,
    JXL_TRANSFORM_DCT4X8,
    JXL_TRANSFORM_DCT8X4,
    JXL_TRANSFORM_AFV0,
    JXL_TRANSFORM_AFV1,
    JXL_TRANSFORM_AFV2,
    JXL_TRANSFORM_AFV3,
    JXL_TRANSFORM_DCT64,
    JXL_TRANSFORM_DCT64X32,
    JXL_TRANSFORM_DCT32X64,
    JXL_TRANSFORM_DCT128,
    JXL_TRANSFORM_DCT128X64,
    JXL_TRANSFORM_DCT64X128,
    JXL_TRANSFORM_DCT256,
    JXL_TRANSFORM_DCT256X128,
    JXL_TRANSFORM_DCT128X256,
} jxl_transform_type;

/* Returns JXL_VARDCT_OK or JXL_VARDCT_BITSTREAM_ERROR. */
int jxl_transform_type_from_u8(uint8_t value, jxl_transform_type *out);

void jxl_transform_dct_select_size(jxl_transform_type t, uint32_t *w_blocks, uint32_t *h_blocks);
void jxl_transform_dequant_matrix_size(jxl_transform_type t, uint32_t *w, uint32_t *h);
uint32_t jxl_transform_dequant_matrix_param_index(jxl_transform_type t);
uint32_t jxl_transform_order_id(jxl_transform_type t);
int jxl_transform_need_transpose(jxl_transform_type t);

#endif /* JXL_VARDCT_DCT_SELECT_H_ */
