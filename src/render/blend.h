// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_BLEND_H_
#define JXL_RENDER_BLEND_H_

#include "frame/frame_header.h"

#include "jxl_oxide/jxl_types.h"

float jxl_blend_clamp01(float v, int clamp);

typedef struct {
    jxl_blend_mode mode;
    int clamp;
    uint32_t alpha_channel;
    int premultiplied;
} jxl_blend_params;

float jxl_blend_samples(float base_sample, float new_sample, float base_alpha, float new_alpha,
                        const jxl_blend_params *params);

#endif /* JXL_RENDER_BLEND_H_ */
