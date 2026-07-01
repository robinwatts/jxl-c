// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/epf_sse41.h"

#include <emmintrin.h>
#include <smmintrin.h>
#include <string.h>

#define JXL_EPF_ABS_PS(v) _mm_andnot_ps(_mm_set1_ps(-0.0f), (v))

jxl_inline int32_t jxl_epf_float_bits(float f) {
    int32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

jxl_inline __m128 jxl_epf_insert_lane0_ps(__m128 v, float scalar) {
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_slli_si128(_mm_castps_si128(v), 4),
                                             jxl_epf_float_bits(scalar), 0));
}

jxl_inline __m128 jxl_epf_insert_lane3_ps(__m128 v, float scalar) {
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_srli_si128(_mm_castps_si128(v), 4),
                                             jxl_epf_float_bits(scalar), 3));
}

jxl_inline __m128 jxl_epf_weight_sse41_v(__m128 scaled_distance, float sigma,
                                            __m128 step_multiplier) {
    const float neg_base = 6.6f * (0.70710677f - 1.0f) / sigma;
    __m128 neg_inv_sigma = _mm_mul_ps(_mm_set1_ps(neg_base), step_multiplier);
    __m128 result = _mm_add_ps(_mm_mul_ps(scaled_distance, neg_inv_sigma), _mm_set1_ps(1.0f));
    return _mm_max_ps(result, _mm_setzero_ps());
}

jxl_inline const float *jxl_epf_merged_row(const jxl_const_subgrid_f32 *sg, size_t y) {
    return sg->data + y * sg->stride;
}

jxl_inline float jxl_epf_merged_get(const jxl_const_subgrid_f32 *sg, size_t work_x, size_t y,
                                       size_t merged_x0) {
    return jxl_const_subgrid_f32_get(*sg, work_x + merged_x0, y);
}

static void jxl_epf_row_sse41_inner(jxl_epf_row *row, unsigned step) {
    size_t dx;
    size_t kernel_len;
    size_t simd_start;
    __m128 sm[2];
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
    if (is_y_border) {
        sm[0] = _mm_set1_ps(step_multiplier * border_sad_mul);
        sm[1] = sm[0];
    } else {
        sm[0] = _mm_set_ps(step_multiplier, step_multiplier, step_multiplier,
                           step_multiplier * border_sad_mul);
        sm[1] = _mm_set_ps(step_multiplier * border_sad_mul, step_multiplier, step_multiplier,
                           step_multiplier);
    }

    for (dx = simd_start; dx < simd_end; dx += 4) {
        size_t c;
        __m128 sm_val = sm[(dx / 4) & 1u];
        float sigma_val = sigma_pixels[(dx / 8u) * 8u];
        __m128 originals[3];

        __m128 sum_channels[3];
        for (c = 0; c < 3; ++c) {
            originals[c] = _mm_loadu_ps(jxl_epf_merged_row(&merged_input_rows[c], 3) + dx + mx);
        }

        if (sigma_val < 0.3f) {
            for (c = 0; c < 3; ++c) {
                _mm_storeu_ps(output_rows[c] + dx, originals[c]);
            }
            if (row->processed != NULL) {
                memset(row->processed + dx, 1, 4);
            }
            continue;
        }

        __m128 sum_weights = _mm_set1_ps(1.0f);
        sum_channels[0] = originals[0];
        sum_channels[1] = originals[1];
        sum_channels[2] = originals[2];


        if (step == 1) {
            /* (0, -1), (0, 1) — match Rust: left/right shuffles use pre-mutation v* */
            {
                size_t c;
                __m128 dist0 = _mm_setzero_ps();
                __m128 dist1 = _mm_setzero_ps();
                for (c = 0; c < 3; ++c) {
                    __m128 scale = _mm_set1_ps(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];

                    __m128 v0 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 1) + dx + mx);
                    __m128 v1 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 2) + dx + mx);
                    __m128 v2 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 3) + dx + mx);
                    __m128 v3 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 4) + dx + mx);
                    __m128 v4 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 5) + dx + mx);
                    __m128 tmp = _mm_add_ps(JXL_EPF_ABS_PS(_mm_sub_ps(v1, v2)),
                                            JXL_EPF_ABS_PS(_mm_sub_ps(v3, v2)));
                    __m128 acc0 = _mm_add_ps(tmp, JXL_EPF_ABS_PS(_mm_sub_ps(v1, v0)));
                    __m128 acc1 = _mm_add_ps(tmp, JXL_EPF_ABS_PS(_mm_sub_ps(v3, v4)));

                    /* Left/right shuffles from original v* (Rust epf_sse41.rs); do not
                     * chain insert_lane0 then insert_lane3 on the same register. */
                    __m128 v1_left = jxl_epf_insert_lane0_ps(
                        v1, jxl_epf_merged_get(input_rows, dx - 1, 2, mx));
                    __m128 v2_left = jxl_epf_insert_lane0_ps(
                        v2, jxl_epf_merged_get(input_rows, dx - 1, 3, mx));
                    __m128 v3_left = jxl_epf_insert_lane0_ps(
                        v3, jxl_epf_merged_get(input_rows, dx - 1, 4, mx));
                    acc0 = _mm_add_ps(acc0, JXL_EPF_ABS_PS(_mm_sub_ps(v1_left, v2_left)));
                    acc1 = _mm_add_ps(acc1, JXL_EPF_ABS_PS(_mm_sub_ps(v3_left, v2_left)));

                    __m128 v1_right = jxl_epf_insert_lane3_ps(
                        v1, jxl_epf_merged_get(input_rows, dx + 4, 2, mx));
                    __m128 v2_right = jxl_epf_insert_lane3_ps(
                        v2, jxl_epf_merged_get(input_rows, dx + 4, 3, mx));
                    __m128 v3_right = jxl_epf_insert_lane3_ps(
                        v3, jxl_epf_merged_get(input_rows, dx + 4, 4, mx));
                    acc0 = _mm_add_ps(acc0, JXL_EPF_ABS_PS(_mm_sub_ps(v1_right, v2_right)));
                    acc1 = _mm_add_ps(acc1, JXL_EPF_ABS_PS(_mm_sub_ps(v3_right, v2_right)));

                    dist0 = _mm_add_ps(dist0, _mm_mul_ps(scale, acc0));
                    dist1 = _mm_add_ps(dist1, _mm_mul_ps(scale, acc1));
                }

                __m128 weight0 = jxl_epf_weight_sse41_v(dist0, sigma_val, sm_val);
                __m128 weight1 = jxl_epf_weight_sse41_v(dist1, sigma_val, sm_val);
                sum_weights = _mm_add_ps(sum_weights, _mm_add_ps(weight0, weight1));

                for (c = 0; c < 3; ++c) {
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    __m128 weighted0 =
                        _mm_mul_ps(weight0, _mm_loadu_ps(jxl_epf_merged_row(input_rows, 2) + dx + mx));
                    __m128 weighted1 =
                        _mm_mul_ps(weight1, _mm_loadu_ps(jxl_epf_merged_row(input_rows, 4) + dx + mx));
                    sum_channels[c] = _mm_add_ps(sum_channels[c], _mm_add_ps(weighted0, weighted1));
                }
            }

            /* (-1, 0), (1, 0) */
            {
                size_t c;
                __m128 dist0 = _mm_setzero_ps();
                __m128 dist1 = _mm_setzero_ps();
                for (c = 0; c < 3; ++c) {
                    __m128 scale = _mm_set1_ps(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];

                    __m128 v0r = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 2) + dx + 1 + mx);
                    __m128 v0 = jxl_epf_insert_lane0_ps(v0r, jxl_epf_merged_get(input_rows, dx, 2, mx));
                    __m128 v0l =
                        jxl_epf_insert_lane0_ps(v0, jxl_epf_merged_get(input_rows, dx - 1, 2, mx));
                    __m128 acc0 = JXL_EPF_ABS_PS(_mm_sub_ps(v0l, v0));
                    __m128 acc1 = JXL_EPF_ABS_PS(_mm_sub_ps(v0r, v0));

                    __m128 v1rr = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 3) + dx + 2 + mx);
                    __m128 v1r =
                        jxl_epf_insert_lane0_ps(v1rr, jxl_epf_merged_get(input_rows, dx + 1, 3, mx));
                    __m128 v1 = jxl_epf_insert_lane0_ps(v1r, jxl_epf_merged_get(input_rows, dx, 3, mx));
                    __m128 v1l =
                        jxl_epf_insert_lane0_ps(v1, jxl_epf_merged_get(input_rows, dx - 1, 3, mx));
                    __m128 v1ll =
                        jxl_epf_insert_lane0_ps(v1l, jxl_epf_merged_get(input_rows, dx - 2, 3, mx));
                    acc0 = _mm_add_ps(acc0, JXL_EPF_ABS_PS(_mm_sub_ps(v1ll, v1l)));
                    acc0 = _mm_add_ps(acc0, JXL_EPF_ABS_PS(_mm_sub_ps(v1, v1l)));
                    acc0 = _mm_add_ps(acc0, JXL_EPF_ABS_PS(_mm_sub_ps(v1, v1r)));
                    acc1 = _mm_add_ps(acc1, JXL_EPF_ABS_PS(_mm_sub_ps(v1, v1l)));
                    acc1 = _mm_add_ps(acc1, JXL_EPF_ABS_PS(_mm_sub_ps(v1, v1r)));
                    acc1 = _mm_add_ps(acc1, JXL_EPF_ABS_PS(_mm_sub_ps(v1rr, v1r)));

                    __m128 v2r = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 4) + dx + 1 + mx);
                    __m128 v2 = jxl_epf_insert_lane0_ps(v2r, jxl_epf_merged_get(input_rows, dx, 4, mx));
                    __m128 v2l =
                        jxl_epf_insert_lane0_ps(v2, jxl_epf_merged_get(input_rows, dx - 1, 4, mx));
                    acc0 = _mm_add_ps(acc0, JXL_EPF_ABS_PS(_mm_sub_ps(v2l, v2)));
                    acc1 = _mm_add_ps(acc1, JXL_EPF_ABS_PS(_mm_sub_ps(v2r, v2)));

                    dist0 = _mm_add_ps(dist0, _mm_mul_ps(scale, acc0));
                    dist1 = _mm_add_ps(dist1, _mm_mul_ps(scale, acc1));
                }

                __m128 weight0 = jxl_epf_weight_sse41_v(dist0, sigma_val, sm_val);
                __m128 weight1 = jxl_epf_weight_sse41_v(dist1, sigma_val, sm_val);
                sum_weights = _mm_add_ps(sum_weights, _mm_add_ps(weight0, weight1));

                for (c = 0; c < 3; ++c) {
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    __m128 weighted0 =
                        _mm_mul_ps(weight0, _mm_loadu_ps(jxl_epf_merged_row(input_rows, 3) + dx - 1 + mx));
                    __m128 weighted1 =
                        _mm_mul_ps(weight1, _mm_loadu_ps(jxl_epf_merged_row(input_rows, 3) + dx + 1 + mx));
                    sum_channels[c] = _mm_add_ps(sum_channels[c], _mm_add_ps(weighted0, weighted1));
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

                __m128 dist = _mm_setzero_ps();
                for (c = 0; c < 3; ++c) {
                    __m128 scale = _mm_set1_ps(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    if (step == 0) {
                        __m128 vk0 =
                            _mm_loadu_ps(jxl_epf_merged_row(input_rows, ky - 1) + kx + mx);
                        __m128 vb0 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 2) + dx + mx);
                        __m128 acc = JXL_EPF_ABS_PS(_mm_sub_ps(vk0, vb0));

                        __m128 vk1r =
                            _mm_loadu_ps(jxl_epf_merged_row(input_rows, ky) + kx + 1 + mx);
                        __m128 vb1r =
                            _mm_loadu_ps(jxl_epf_merged_row(input_rows, 3) + dx + 1 + mx);
                        acc = _mm_add_ps(acc, JXL_EPF_ABS_PS(_mm_sub_ps(vk1r, vb1r)));

                        __m128 vk1 =
                            jxl_epf_insert_lane0_ps(vk1r, jxl_epf_merged_get(input_rows, kx, ky, mx));
                        __m128 vb1 =
                            jxl_epf_insert_lane0_ps(vb1r, jxl_epf_merged_get(input_rows, dx, 3, mx));
                        acc = _mm_add_ps(acc, JXL_EPF_ABS_PS(_mm_sub_ps(vk1, vb1)));

                        __m128 vk1l = jxl_epf_insert_lane0_ps(
                            vk1, jxl_epf_merged_get(input_rows, kx - 1, ky, mx));
                        __m128 vb1l = jxl_epf_insert_lane0_ps(
                            vb1, jxl_epf_merged_get(input_rows, dx - 1, 3, mx));
                        acc = _mm_add_ps(acc, JXL_EPF_ABS_PS(_mm_sub_ps(vk1l, vb1l)));

                        __m128 vk2 =
                            _mm_loadu_ps(jxl_epf_merged_row(input_rows, ky + 1) + kx + mx);
                        __m128 vb2 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 4) + dx + mx);
                        acc = _mm_add_ps(acc, JXL_EPF_ABS_PS(_mm_sub_ps(vk2, vb2)));

                        dist = _mm_add_ps(dist, _mm_mul_ps(scale, acc));
                    } else {
                        __m128 v0 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, ky) + kx + mx);
                        __m128 v1 = _mm_loadu_ps(jxl_epf_merged_row(input_rows, 3) + dx + mx);
                        dist = _mm_add_ps(dist, _mm_mul_ps(scale, JXL_EPF_ABS_PS(_mm_sub_ps(v0, v1))));
                    }
                }

                __m128 weight = jxl_epf_weight_sse41_v(dist, sigma_val, sm_val);
                sum_weights = _mm_add_ps(sum_weights, weight);

                for (c = 0; c < 3; ++c) {
                    sum_channels[c] = _mm_add_ps(
                        sum_channels[c],
                        _mm_mul_ps(weight, _mm_loadu_ps(
                                          jxl_epf_merged_row(&merged_input_rows[c], ky) + kx + mx)));
                }
            }
        }

        for (c = 0; c < 3; ++c) {
            _mm_storeu_ps(output_rows[c] + dx, _mm_div_ps(sum_channels[c], sum_weights));
        }
        if (row->processed != NULL) {
            memset(row->processed + dx, 1, 4);
        }
    }
}

void jxl_epf_row_sse41(jxl_epf_row *row) {
    if (row == NULL || !row->use_merged) {
        return;
    }
    jxl_epf_row_sse41_inner(row, row->step);
}
