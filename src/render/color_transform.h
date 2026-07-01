// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COLOR_TRANSFORM_H_
#define JXL_COLOR_TRANSFORM_H_

#include "image/image_internal.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_context jxl_context;

/* XYB (opsin) -> linear sRGB D65, then sRGB transfer. Matches Rust ColorTransform for ICC+xyb. */
void jxl_color_transform_xyb_to_srgb(jxl_context *ctx, float *x, float *y, float *b,
                                     size_t num_pixels, const jxl_opsin_inverse_parsed *opsin,
                                     float intensity_target);

void jxl_color_transform_xyb_to_linear_rgb(jxl_context *ctx, float *x, float *y, float *b,
                                         size_t num_pixels, const jxl_opsin_inverse_parsed *opsin,
                                         float intensity_target);

void jxl_color_transform_apply_forward_transfer(jxl_context *ctx, float *samples, size_t num_pixels,
                                                jxl_transfer_function_i tf,
                                                float intensity_target);

void jxl_color_transform_apply_inverse_transfer(jxl_context *ctx, float *samples,
                                                size_t num_pixels, jxl_transfer_function_i tf,
                                                float intensity_target);

void jxl_color_apply_gamma(jxl_context *ctx, float *samples, size_t n, float gamma);

#endif /* JXL_COLOR_TRANSFORM_H_ */
