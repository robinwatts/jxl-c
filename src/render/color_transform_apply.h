// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_TRANSFORM_APPLY_H_
#define JXL_RENDER_COLOR_TRANSFORM_APPLY_H_

#include "image/image_internal.h"

#include "jxl_oxide/jxl_status.h"

typedef struct jxl_context jxl_context;

/* XYB planes → target enum colour encoding (D65/sRGB-primaries fast path). */
jxl_status_t jxl_color_transform_xyb_to_encoding(jxl_context *ctx, float *x, float *y, float *b,
                                                 size_t num_pixels,
                                                 const jxl_opsin_inverse_parsed *opsin,
                                                 const jxl_colour_encoding_parsed *target,
                                                 float intensity_target);

#endif /* JXL_RENDER_COLOR_TRANSFORM_APPLY_H_ */
