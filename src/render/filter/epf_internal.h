// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_EPF_INTERNAL_H_
#define JXL_RENDER_FILTER_EPF_INTERNAL_H_

#include "frame/filter.h"
#include "render/subgrid_f32.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int64_t kx;
    int64_t ky;
} jxl_epf_kernel_offset;

static const jxl_epf_kernel_offset k_epf_kernel_1[4] = {
    {0, -1}, {0, 1}, {-1, 0}, {1, 0},
};

static const jxl_epf_kernel_offset k_epf_kernel_2[12] = {
    {0, -2}, {-1, -1}, {0, -1}, {1, -1}, {-2, 0}, {-1, 0},
    {1, 0},  {2, 0},   {-1, 1},  {0, 1},  {1, 1},  {0, 2},
};

static const jxl_epf_kernel_offset k_dist_step0[5] = {
    {0, -1}, {1, 0}, {0, 0}, {-1, 0}, {0, 1},
};

static const jxl_epf_kernel_offset k_dist_step1[5] = {
    {0, -1}, {0, 0}, {0, 1}, {-1, 0}, {1, 0},
};

static const jxl_epf_kernel_offset k_dist_step2[1] = {
    {0, 0},
};

typedef struct jxl_epf_row {
    const float *input_rows[3][7];
    jxl_const_subgrid_f32 merged_input[3];
    int use_merged;
    size_t merged_x0;
    float *output_rows[3];
    size_t width;
    size_t y;
    const float *sigma_pixels;
    uint8_t *processed;
    const jxl_epf_filter *epf_params;
    int skip_inner;
    unsigned step;
} jxl_epf_row;

jxl_inline float jxl_epf_weight(float scaled_distance, float sigma, float step_multiplier) {
    float neg_inv_sigma =
        6.6f * (0.70710677f - 1.0f) / sigma * step_multiplier; /* FRAC_1_SQRT_2 */
    float w = 1.0f + scaled_distance * neg_inv_sigma;
    return w > 0.0f ? w : 0.0f;
}

#endif /* JXL_RENDER_FILTER_EPF_INTERNAL_H_ */
