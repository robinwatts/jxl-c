// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_REC2408_H_
#define JXL_RENDER_COLOR_REC2408_H_

#include <stddef.h>

typedef struct jxl_context jxl_context;

typedef struct {
    float lo;
    float hi;
} jxl_luminance_nits_range;

float jxl_rec2408_eetf_base(float from_pq_sample, float intensity_target,
                            jxl_luminance_nits_range from_luminance_range,
                            jxl_luminance_nits_range to_luminance_range);

void jxl_rec2408_eetf_pq(jxl_context *ctx, float *samples, size_t n, float intensity_target,
                         jxl_luminance_nits_range from_luminance_range,
                         jxl_luminance_nits_range to_luminance_range);

#endif /* JXL_RENDER_COLOR_REC2408_H_ */
