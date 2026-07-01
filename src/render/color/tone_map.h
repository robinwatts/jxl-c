// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_TONE_MAP_H_
#define JXL_RENDER_COLOR_TONE_MAP_H_

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_context jxl_context;

typedef struct {
    float luminances[3];
    float intensity_target;
    float min_nits;
} jxl_hdr_params;

void jxl_color_tone_map(jxl_context *ctx, float *r, float *g, float *b, size_t n,
                        const jxl_hdr_params *hdr, float target_display_luminance,
                        int detect_peak);

void jxl_color_tone_map_luma(jxl_context *ctx, float *luma, size_t n, const jxl_hdr_params *hdr,
                             float target_display_luminance, int detect_peak);

#endif /* JXL_RENDER_COLOR_TONE_MAP_H_ */
