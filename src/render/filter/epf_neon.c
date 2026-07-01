// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/epf_neon.h"

#include <string.h>

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

#define JXL_EPF_V float32x4_t
#define JXL_EPF_LOAD(p) vld1q_f32((p))
#define JXL_EPF_STORE(p, v) vst1q_f32((p), (v))
#define JXL_EPF_SPLAT(x) vdupq_n_f32((x))
#define JXL_EPF_ADD vaddq_f32
#define JXL_EPF_SUB vsubq_f32
#define JXL_EPF_MUL vmulq_f32
#define JXL_EPF_DIV vdivq_f32
#define JXL_EPF_MAX vmaxq_f32
#define JXL_EPF_ABS(v) vabsq_f32((v))
#define JXL_EPF_ZERO() vdupq_n_f32(0.0f)

jxl_inline JXL_EPF_V jxl_epf_set_ps(float w, float z, float y, float x) {
    float lanes[4];
    lanes[0] = x;
    lanes[1] = y;
    lanes[2] = z;
    lanes[3] = w;

    return vld1q_f32(lanes);
}

jxl_inline JXL_EPF_V jxl_epf_insert_lane0_ps(JXL_EPF_V v, float scalar) {
    float lanes[4];
    vst1q_f32(lanes, v);
    lanes[0] = scalar;
    return vld1q_f32(lanes);
}

jxl_inline JXL_EPF_V jxl_epf_insert_lane3_ps(JXL_EPF_V v, float scalar) {
    float lanes[4];
    vst1q_f32(lanes, v);
    lanes[3] = scalar;
    return vld1q_f32(lanes);
}

jxl_inline JXL_EPF_V jxl_epf_weight_neon_v(JXL_EPF_V scaled_distance, float sigma,
                                              JXL_EPF_V step_multiplier) {
    const float neg_base = 6.6f * (0.70710677f - 1.0f) / sigma;
    JXL_EPF_V neg_inv_sigma = JXL_EPF_MUL(JXL_EPF_SPLAT(neg_base), step_multiplier);
    JXL_EPF_V result = JXL_EPF_ADD(JXL_EPF_MUL(scaled_distance, neg_inv_sigma), JXL_EPF_SPLAT(1.0f));
    return JXL_EPF_MAX(result, JXL_EPF_ZERO());
}

jxl_inline const float *jxl_epf_merged_row(const jxl_const_subgrid_f32 *sg, size_t y) {
    return sg->data + y * sg->stride;
}

jxl_inline float jxl_epf_merged_get(const jxl_const_subgrid_f32 *sg, size_t work_x, size_t y,
                                       size_t merged_x0) {
    return jxl_const_subgrid_f32_get(*sg, work_x + merged_x0, y);
}

static void jxl_epf_row_neon_inner(jxl_epf_row *row, unsigned step) {
    size_t dx;
    size_t kernel_len;
    size_t simd_start;
    size_t width;
    size_t y;
    const jxl_const_subgrid_f32 *merged_input_rows = row->merged_input;
    float *const *output_rows = row->output_rows;
    width = row->width;
    y = row->y;
    const float *sigma_pixels = row->sigma_pixels;
    const jxl_epf_filter *epf_params = row->epf_params;
    const size_t mx = row->merged_x0;

    const jxl_epf_kernel_offset *kernel_offsets;
    if (step == 0) {
        kernel_offsets = k_epf_kernel_2;
        kernel_len = 12;
    } else {
        kernel_offsets = k_epf_kernel_1;
        kernel_len = 4;
    }

    float step_multiplier = step == 0 ? epf_params->sigma.pass0_sigma_scale
                                      : (step == 2 ? epf_params->sigma.pass2_sigma_scale : 1.0f);
    float border_sad_mul = epf_params->sigma.border_sad_mul;
    const float *channel_scale = epf_params->channel_scale;

    unsigned padding = 3u - step;
    if (width < (size_t)padding * 2u) {
        return;
    }

    simd_start = 4;
    size_t simd_end = (width - padding) & ~(size_t)3u;
    if (simd_start > simd_end) {
        simd_end = simd_start;
    }

    int is_y_border = ((int)(y + 1) & 0b110) == 0;
    JXL_EPF_V sm[2];
    if (is_y_border) {
        sm[0] = JXL_EPF_SPLAT(step_multiplier * border_sad_mul);
        sm[1] = sm[0];
    } else {
        sm[0] = jxl_epf_set_ps(step_multiplier, step_multiplier, step_multiplier,
                               step_multiplier * border_sad_mul);
        sm[1] = jxl_epf_set_ps(step_multiplier, step_multiplier * border_sad_mul, step_multiplier,
                               step_multiplier);
    }

    for (dx = simd_start; dx < simd_end; dx += 4) {
        size_t c;
        JXL_EPF_V sm_val = sm[(dx / 4) & 1u];
        float sigma_val = sigma_pixels[(dx / 8u) * 8u];
        JXL_EPF_V originals[3];

        for (c = 0; c < 3; ++c) {
            originals[c] = JXL_EPF_LOAD(jxl_epf_merged_row(&merged_input_rows[c], 3) + dx + mx);
        }

        if (sigma_val < 0.3f) {
            for (c = 0; c < 3; ++c) {
                JXL_EPF_STORE(output_rows[c] + dx, originals[c]);
            }
            if (row->processed != NULL) {
                memset(row->processed + dx, 1, 4);
            }
            continue;
        }

        JXL_EPF_V sum_weights = JXL_EPF_SPLAT(1.0f);
        JXL_EPF_V sum_channels[3];
        sum_channels[0] = originals[0];
        sum_channels[1] = originals[1];
        sum_channels[2] = originals[2];


        if (step == 1) {
            {
                size_t c;
                JXL_EPF_V dist0 = JXL_EPF_ZERO();
                JXL_EPF_V dist1 = JXL_EPF_ZERO();
                for (c = 0; c < 3; ++c) {
                    JXL_EPF_V scale = JXL_EPF_SPLAT(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];

                    JXL_EPF_V v0 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 1) + dx + mx);
                    JXL_EPF_V v1 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + mx);
                    JXL_EPF_V v2 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + mx);
                    JXL_EPF_V v3 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx + mx);
                    JXL_EPF_V v4 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 5) + dx + mx);
                    JXL_EPF_V tmp = JXL_EPF_ADD(JXL_EPF_ABS(JXL_EPF_SUB(v1, v2)),
                                                JXL_EPF_ABS(JXL_EPF_SUB(v3, v2)));
                    JXL_EPF_V acc0 = JXL_EPF_ADD(tmp, JXL_EPF_ABS(JXL_EPF_SUB(v1, v0)));
                    JXL_EPF_V acc1 = JXL_EPF_ADD(tmp, JXL_EPF_ABS(JXL_EPF_SUB(v3, v4)));

                    JXL_EPF_V v1_left =
                        jxl_epf_insert_lane0_ps(v1, jxl_epf_merged_get(input_rows, dx - 1, 2, mx));
                    JXL_EPF_V v2_left =
                        jxl_epf_insert_lane0_ps(v2, jxl_epf_merged_get(input_rows, dx - 1, 3, mx));
                    JXL_EPF_V v3_left =
                        jxl_epf_insert_lane0_ps(v3, jxl_epf_merged_get(input_rows, dx - 1, 4, mx));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1_left, v2_left)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v3_left, v2_left)));

                    JXL_EPF_V v1_right =
                        jxl_epf_insert_lane3_ps(v1, jxl_epf_merged_get(input_rows, dx + 4, 2, mx));
                    JXL_EPF_V v2_right =
                        jxl_epf_insert_lane3_ps(v2, jxl_epf_merged_get(input_rows, dx + 4, 3, mx));
                    JXL_EPF_V v3_right =
                        jxl_epf_insert_lane3_ps(v3, jxl_epf_merged_get(input_rows, dx + 4, 4, mx));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1_right, v2_right)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v3_right, v2_right)));

                    dist0 = JXL_EPF_ADD(dist0, JXL_EPF_MUL(scale, acc0));
                    dist1 = JXL_EPF_ADD(dist1, JXL_EPF_MUL(scale, acc1));
                }

                JXL_EPF_V weight0 = jxl_epf_weight_neon_v(dist0, sigma_val, sm_val);
                JXL_EPF_V weight1 = jxl_epf_weight_neon_v(dist1, sigma_val, sm_val);
                sum_weights = JXL_EPF_ADD(sum_weights, JXL_EPF_ADD(weight0, weight1));

                for (c = 0; c < 3; ++c) {
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    JXL_EPF_V weighted0 = JXL_EPF_MUL(
                        weight0, JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + mx));
                    JXL_EPF_V weighted1 = JXL_EPF_MUL(
                        weight1, JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx + mx));
                    sum_channels[c] = JXL_EPF_ADD(sum_channels[c], JXL_EPF_ADD(weighted0, weighted1));
                }
            }

            {
                size_t c;
                JXL_EPF_V dist0 = JXL_EPF_ZERO();
                JXL_EPF_V dist1 = JXL_EPF_ZERO();
                for (c = 0; c < 3; ++c) {
                    JXL_EPF_V scale = JXL_EPF_SPLAT(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];

                    JXL_EPF_V v0r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + 1 + mx);
                    JXL_EPF_V v0 =
                        jxl_epf_insert_lane0_ps(v0r, jxl_epf_merged_get(input_rows, dx, 2, mx));
                    JXL_EPF_V v0l =
                        jxl_epf_insert_lane0_ps(v0, jxl_epf_merged_get(input_rows, dx - 1, 2, mx));
                    JXL_EPF_V acc0 = JXL_EPF_ABS(JXL_EPF_SUB(v0l, v0));
                    JXL_EPF_V acc1 = JXL_EPF_ABS(JXL_EPF_SUB(v0r, v0));

                    JXL_EPF_V v1rr = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + 2 + mx);
                    JXL_EPF_V v1r =
                        jxl_epf_insert_lane0_ps(v1rr, jxl_epf_merged_get(input_rows, dx + 1, 3, mx));
                    JXL_EPF_V v1 =
                        jxl_epf_insert_lane0_ps(v1r, jxl_epf_merged_get(input_rows, dx, 3, mx));
                    JXL_EPF_V v1l =
                        jxl_epf_insert_lane0_ps(v1, jxl_epf_merged_get(input_rows, dx - 1, 3, mx));
                    JXL_EPF_V v1ll =
                        jxl_epf_insert_lane0_ps(v1l, jxl_epf_merged_get(input_rows, dx - 2, 3, mx));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1ll, v1l)));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1l)));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1r)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1l)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1r)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v1rr, v1r)));

                    JXL_EPF_V v2r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx + 1 + mx);
                    JXL_EPF_V v2 =
                        jxl_epf_insert_lane0_ps(v2r, jxl_epf_merged_get(input_rows, dx, 4, mx));
                    JXL_EPF_V v2l =
                        jxl_epf_insert_lane0_ps(v2, jxl_epf_merged_get(input_rows, dx - 1, 4, mx));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v2l, v2)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v2r, v2)));

                    dist0 = JXL_EPF_ADD(dist0, JXL_EPF_MUL(scale, acc0));
                    dist1 = JXL_EPF_ADD(dist1, JXL_EPF_MUL(scale, acc1));
                }

                JXL_EPF_V weight0 = jxl_epf_weight_neon_v(dist0, sigma_val, sm_val);
                JXL_EPF_V weight1 = jxl_epf_weight_neon_v(dist1, sigma_val, sm_val);
                sum_weights = JXL_EPF_ADD(sum_weights, JXL_EPF_ADD(weight0, weight1));

                for (c = 0; c < 3; ++c) {
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    JXL_EPF_V weighted0 = JXL_EPF_MUL(
                        weight0, JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx - 1 + mx));
                    JXL_EPF_V weighted1 = JXL_EPF_MUL(
                        weight1, JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + 1 + mx));
                    sum_channels[c] = JXL_EPF_ADD(sum_channels[c], JXL_EPF_ADD(weighted0, weighted1));
                }
            }
        } else {
            size_t ki;
            for (ki = 0; ki < kernel_len; ++ki) {
                size_t c;
                int64_t kx_off = kernel_offsets[ki].kx;
                int64_t ky_off = kernel_offsets[ki].ky;
                size_t ky = (size_t)((int64_t)3 + ky_off);
                size_t kx = (size_t)((int64_t)dx + kx_off);

                JXL_EPF_V dist = JXL_EPF_ZERO();
                for (c = 0; c < 3; ++c) {
                    JXL_EPF_V scale = JXL_EPF_SPLAT(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    if (step == 0) {
                        JXL_EPF_V vk0 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky - 1) + kx + mx);
                        JXL_EPF_V vb0 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + mx);
                        JXL_EPF_V acc = JXL_EPF_ABS(JXL_EPF_SUB(vk0, vb0));

                        JXL_EPF_V vk1r =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky) + kx + 1 + mx);
                        JXL_EPF_V vb1r =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + 1 + mx);
                        acc = JXL_EPF_ADD(acc, JXL_EPF_ABS(JXL_EPF_SUB(vk1r, vb1r)));

                        JXL_EPF_V vk1 = jxl_epf_insert_lane0_ps(
                            vk1r, jxl_epf_merged_get(input_rows, kx, ky, mx));
                        JXL_EPF_V vb1 = jxl_epf_insert_lane0_ps(
                            vb1r, jxl_epf_merged_get(input_rows, dx, 3, mx));
                        acc = JXL_EPF_ADD(acc, JXL_EPF_ABS(JXL_EPF_SUB(vk1, vb1)));

                        JXL_EPF_V vk1l = jxl_epf_insert_lane0_ps(
                            vk1, jxl_epf_merged_get(input_rows, kx - 1, ky, mx));
                        JXL_EPF_V vb1l = jxl_epf_insert_lane0_ps(
                            vb1, jxl_epf_merged_get(input_rows, dx - 1, 3, mx));
                        acc = JXL_EPF_ADD(acc, JXL_EPF_ABS(JXL_EPF_SUB(vk1l, vb1l)));

                        JXL_EPF_V vk2 =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky + 1) + kx + mx);
                        JXL_EPF_V vb2 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx + mx);
                        acc = JXL_EPF_ADD(acc, JXL_EPF_ABS(JXL_EPF_SUB(vk2, vb2)));

                        dist = JXL_EPF_ADD(dist, JXL_EPF_MUL(scale, acc));
                    } else {
                        JXL_EPF_V v0 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky) + kx + mx);
                        JXL_EPF_V v1 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + mx);
                        dist = JXL_EPF_ADD(dist, JXL_EPF_MUL(scale, JXL_EPF_ABS(JXL_EPF_SUB(v0, v1))));
                    }
                }

                JXL_EPF_V weight = jxl_epf_weight_neon_v(dist, sigma_val, sm_val);
                sum_weights = JXL_EPF_ADD(sum_weights, weight);

                for (c = 0; c < 3; ++c) {
                    sum_channels[c] = JXL_EPF_ADD(
                        sum_channels[c],
                        JXL_EPF_MUL(weight, JXL_EPF_LOAD(jxl_epf_merged_row(&merged_input_rows[c], ky) +
                                                         kx + mx)));
                }
            }
        }

        for (c = 0; c < 3; ++c) {
            JXL_EPF_STORE(output_rows[c] + dx, JXL_EPF_DIV(sum_channels[c], sum_weights));
        }
        if (row->processed != NULL) {
            memset(row->processed + dx, 1, 4);
        }
    }
}

void jxl_epf_row_neon(jxl_epf_row *row) {
    if (row == NULL || !row->use_merged) {
        return;
    }
    jxl_epf_row_neon_inner(row, row->step);
}

#endif
