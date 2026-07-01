// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/gabor_neon.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

void jxl_gabor_row_neon(jxl_gabor_row *row) {
    size_t width = row->width;
    float global_weight;
    size_t dx2;
    size_t x;
    float w0;
    float w1;
    const float *input_ptr_t = row->row_t;
    const float *input_ptr_c = row->row_c;
    const float *input_ptr_b = row->row_b;
    float *output_ptr = row->out;
    w0 = row->w0;
    w1 = row->w1;
    global_weight = 1.0f / (1.0f + w0 * 4.0f + w1 * 4.0f);

    if (width == 0) {
        return;
    }

    float32x2_t tl = vld1_dup_f32(input_ptr_t);
    float32x2_t cl = vld1_dup_f32(input_ptr_c);
    float32x2_t bl = vld1_dup_f32(input_ptr_b);

    for (dx2 = 0; dx2 < (width - 1) / 2; ++dx2) {
        x = dx2 * 2;

        float32x2_t tr = vld1_f32(input_ptr_t + 1 + x);
        float32x2_t cr = vld1_f32(input_ptr_c + 1 + x);
        float32x2_t br = vld1_f32(input_ptr_b + 1 + x);

        float32x2_t t = vext_f32(tl, tr, 1);
        float32x2_t c = vext_f32(cl, cr, 1);
        float32x2_t b = vext_f32(bl, br, 1);

        float32x2_t sum_side = vadd_f32(vadd_f32(vadd_f32(t, cl), cr), b);
        float32x2_t sum_diag = vadd_f32(vadd_f32(vadd_f32(tl, tr), bl), br);
        float32x2_t unweighted_sum = vfma_n_f32(vfma_n_f32(c, sum_side, w0), sum_diag, w1);
        float32x2_t sum = vmul_n_f32(unweighted_sum, global_weight);

        vst1_f32(output_ptr + x, sum);
        tl = tr;
        cl = cr;
        bl = br;
    }

    if ((width % 2) == 0) {
        x = width - 2;

        float32x2_t tr = vld1_dup_f32(input_ptr_t + 1 + x);
        float32x2_t cr = vld1_dup_f32(input_ptr_c + 1 + x);
        float32x2_t br = vld1_dup_f32(input_ptr_b + 1 + x);

        float32x2_t t = vext_f32(tl, tr, 1);
        float32x2_t c = vext_f32(cl, cr, 1);
        float32x2_t b = vext_f32(bl, br, 1);

        float32x2_t sum_side = vadd_f32(vadd_f32(vadd_f32(t, cl), cr), b);
        float32x2_t sum_diag = vadd_f32(vadd_f32(vadd_f32(tl, tr), bl), br);
        float32x2_t unweighted_sum = vfma_n_f32(vfma_n_f32(c, sum_side, w0), sum_diag, w1);
        float32x2_t sum = vmul_n_f32(unweighted_sum, global_weight);

        vst1_f32(output_ptr + x, sum);
    } else {
        float sum_side;
        float sum_diag;
        x = width - 1;
        float t0 = vget_lane_f32(tl, 0);
        float t1 = vget_lane_f32(tl, 1);
        float c0 = vget_lane_f32(cl, 0);
        float c1 = vget_lane_f32(cl, 1);
        float b0 = vget_lane_f32(bl, 0);
        float b1 = vget_lane_f32(bl, 1);
        sum_side = t1 + c0 + c1 + b1;
        sum_diag = t0 + t1 + b0 + b1;
        float unweighted_sum = c1 + sum_side * w0 + sum_diag * w1;
        output_ptr[x] = unweighted_sum * global_weight;
    }
}

#endif /* JXL_HAVE_SIMD_NEON */
