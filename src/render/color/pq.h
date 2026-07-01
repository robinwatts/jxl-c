// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_PQ_H_
#define JXL_RENDER_COLOR_PQ_H_

#include <stddef.h>

typedef struct jxl_context jxl_context;

void jxl_color_linear_to_pq(jxl_context *ctx, float *samples, size_t n, float intensity_target);
void jxl_color_pq_to_linear(jxl_context *ctx, float *samples, size_t n, float intensity_target);

#endif /* JXL_RENDER_COLOR_PQ_H_ */
