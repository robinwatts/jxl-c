// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_FILTER_H_
#define JXL_FRAME_FILTER_H_

#include "jxl_oxide/jxl_types.h"

typedef struct {
    float w0;
    float w1;
} jxl_gabor_channel_weights;

typedef struct {
    int enabled;
    jxl_gabor_channel_weights weights[3];
} jxl_gabor_filter;

typedef struct {
    float quant_mul;
    float pass0_sigma_scale;
    float pass2_sigma_scale;
    float border_sad_mul;
} jxl_epf_sigma;

typedef struct {
    int enabled;
    uint32_t iters;
    float sharp_lut[8];
    float channel_scale[3];
    jxl_epf_sigma sigma;
    float sigma_for_modular;
} jxl_epf_filter;

typedef struct {
    jxl_gabor_filter gab;
    jxl_epf_filter epf;
} jxl_restoration_filter;

void jxl_restoration_filter_init(jxl_restoration_filter *rf);
void jxl_restoration_filter_set_defaults(jxl_restoration_filter *rf);

int jxl_gabor_enabled(const jxl_restoration_filter *rf);
int jxl_epf_enabled(const jxl_restoration_filter *rf);

#endif /* JXL_FRAME_FILTER_H_ */
