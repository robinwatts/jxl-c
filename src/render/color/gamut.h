// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_GAMUT_H_
#define JXL_RENDER_COLOR_GAMUT_H_

#include <stddef.h>

typedef struct jxl_context jxl_context;

void jxl_color_gamut_map(jxl_context *ctx, float *r, float *g, float *b, size_t n,
                         const float luminances[3], float saturation_factor);

#endif /* JXL_RENDER_COLOR_GAMUT_H_ */
