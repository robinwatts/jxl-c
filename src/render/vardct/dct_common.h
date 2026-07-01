// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_DCT_COMMON_H_
#define JXL_RENDER_VARDCT_DCT_COMMON_H_

#include <stddef.h>

typedef enum {
    JXL_DCT_FORWARD = 0,
    JXL_DCT_INVERSE = 1,
} jxl_dct_direction;

const float *jxl_sec_half_small(size_t n);
void jxl_sec_half_fill(size_t n, float *out_half);
float jxl_scale_f(size_t c, size_t logb);

#endif /* JXL_RENDER_VARDCT_DCT_COMMON_H_ */
