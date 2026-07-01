// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_HF_METADATA_H_
#define JXL_VARDCT_HF_METADATA_H_

#include "vardct/dct_select.h"

#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_BLOCK_INFO_UNINIT = 0,
    JXL_BLOCK_INFO_OCCUPIED,
    JXL_BLOCK_INFO_DATA,
} jxl_block_info_kind;

typedef struct {
    jxl_block_info_kind kind;
    jxl_transform_type dct_select;
    int32_t hf_mul;
} jxl_block_info;

jxl_inline int jxl_block_info_is_occupied(const jxl_block_info *info) {
    return info != NULL &&
           (info->kind == JXL_BLOCK_INFO_OCCUPIED || info->kind == JXL_BLOCK_INFO_DATA);
}

typedef struct {
    const jxl_block_info *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_block_info_subgrid;

#endif /* JXL_VARDCT_HF_METADATA_H_ */
