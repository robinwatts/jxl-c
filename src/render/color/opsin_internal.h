// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_OPSIN_INTERNAL_H_
#define JXL_RENDER_COLOR_OPSIN_INTERNAL_H_

#include "image/image_internal.h"

#include <stddef.h>

typedef struct jxl_context jxl_context;

void jxl_color_opsin_xyb_to_linear_rgb_base(float *x, float *y, float *b, size_t num_pixels,
                                            const jxl_opsin_inverse_parsed *opsin,
                                            float intensity_target);

void jxl_color_opsin_xyb_to_linear_rgb(jxl_context *ctx, float *x, float *y, float *b,
                                       size_t num_pixels, const jxl_opsin_inverse_parsed *opsin,
                                       float intensity_target);

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_color_opsin_xyb_to_linear_rgb_x86_avx2(float *x, float *y, float *b, size_t num_pixels,
                                                const jxl_opsin_inverse_parsed *opsin,
                                                float intensity_target);
void jxl_color_opsin_xyb_to_linear_rgb_x86_fma(float *x, float *y, float *b, size_t num_pixels,
                                               const jxl_opsin_inverse_parsed *opsin,
                                               float intensity_target);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_color_opsin_xyb_to_linear_rgb_neon(float *x, float *y, float *b, size_t num_pixels,
                                            const jxl_opsin_inverse_parsed *opsin,
                                            float intensity_target);
#endif

#endif /* JXL_RENDER_COLOR_OPSIN_INTERNAL_H_ */
