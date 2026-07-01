// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/epf_wasm128.h"

#include <string.h>

#if defined(JXL_HAVE_SIMD_WASM128)

#include <wasm_simd128.h>

#define JXL_EPF_V v128_t
#define JXL_EPF_LOAD(p) wasm_v128_load((const v128_t *)(const void *)(p))
#define JXL_EPF_STORE(p, v) wasm_v128_store((v128_t *)(void *)(p), (v))
#define JXL_EPF_SPLAT(x) wasm_f32x4_splat((x))
#define JXL_EPF_ADD wasm_f32x4_add
#define JXL_EPF_SUB wasm_f32x4_sub
#define JXL_EPF_MUL wasm_f32x4_mul
#define JXL_EPF_DIV wasm_f32x4_div
#define JXL_EPF_MAX wasm_f32x4_max
#define JXL_EPF_ABS(v) wasm_f32x4_abs((v))
#define JXL_EPF_ZERO() wasm_f32x4_splat(0.0f)
#define JXL_EPF_SHUF(a, b, i0, i1, i2, i3) wasm_i32x4_shuffle((a), (b), (i0), (i1), (i2), (i3))

jxl_inline JXL_EPF_V jxl_epf_weight_wasm128(JXL_EPF_V scaled_distance, float sigma,
                                              JXL_EPF_V step_multiplier) {
    const float neg_base = 6.6f * (0.70710677f - 1.0f) / sigma;
    JXL_EPF_V neg_inv_sigma = JXL_EPF_MUL(JXL_EPF_SPLAT(neg_base), step_multiplier);
    JXL_EPF_V result = JXL_EPF_ADD(JXL_EPF_MUL(scaled_distance, neg_inv_sigma), JXL_EPF_SPLAT(1.0f));
    return JXL_EPF_MAX(result, JXL_EPF_ZERO());
}

jxl_inline const float *jxl_epf_merged_row(const jxl_const_subgrid_f32 *sg, size_t y) {
    return sg->data + y * sg->stride;
}

static void jxl_epf_row_wasm128_inner(jxl_epf_row *row, unsigned step) {
    size_t dx;
    size_t kernel_len;
    size_t simd_start;
    size_t width;
    size_t y;
    JXL_EPF_V sm[2];
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
        sm[0] = JXL_EPF_SPLAT(step_multiplier * border_sad_mul);
        sm[1] = sm[0];
    } else {
        JXL_EPF_V base_sm = JXL_EPF_SPLAT(step_multiplier);
        sm[0] = wasm_f32x4_replace_lane(base_sm, 0, step_multiplier * border_sad_mul);
        sm[1] = wasm_f32x4_replace_lane(base_sm, 3, step_multiplier * border_sad_mul);
    }

    for (dx = simd_start; dx < simd_end; dx += 4) {
        size_t c;
        JXL_EPF_V sm_val = sm[(dx / 4) & 1u];
        float sigma_val = sigma_pixels[dx / 8];

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
                JXL_EPF_V dist0 = JXL_EPF_ZERO();
                JXL_EPF_V dist1 = JXL_EPF_ZERO();
                for (c = 0; c < 3; ++c) {
                    JXL_EPF_V scale = JXL_EPF_SPLAT(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];

                    JXL_EPF_V v0 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 1) + dx + mx);

                    JXL_EPF_V v1r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + 1 + mx);
                    JXL_EPF_V v1l = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx - 1 + mx);
                    JXL_EPF_V v1 = JXL_EPF_SHUF(v1l, v1r, 1, 2, 3, 6);

                    JXL_EPF_V v2r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + 1 + mx);
                    JXL_EPF_V v2l = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx - 1 + mx);
                    JXL_EPF_V v2 = JXL_EPF_SHUF(v2l, v2r, 1, 2, 3, 6);

                    JXL_EPF_V v3r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx + 1 + mx);
                    JXL_EPF_V v3l = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx - 1 + mx);
                    JXL_EPF_V v3 = JXL_EPF_SHUF(v3l, v3r, 1, 2, 3, 6);

                    JXL_EPF_V v4 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 5) + dx + mx);

                    JXL_EPF_V tmp =
                        JXL_EPF_ADD(JXL_EPF_ABS(JXL_EPF_SUB(v1, v2)), JXL_EPF_ABS(JXL_EPF_SUB(v3, v2)));
                    JXL_EPF_V acc0 = JXL_EPF_ADD(tmp, JXL_EPF_ABS(JXL_EPF_SUB(v1, v0)));
                    JXL_EPF_V acc1 = JXL_EPF_ADD(tmp, JXL_EPF_ABS(JXL_EPF_SUB(v3, v4)));

                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1l, v2l)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v3l, v2l)));

                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1r, v2r)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v3r, v2r)));

                    dist0 = JXL_EPF_ADD(dist0, JXL_EPF_MUL(scale, acc0));
                    dist1 = JXL_EPF_ADD(dist1, JXL_EPF_MUL(scale, acc1));
                }

                JXL_EPF_V weight0 = jxl_epf_weight_wasm128(dist0, sigma_val, sm_val);
                JXL_EPF_V weight1 = jxl_epf_weight_wasm128(dist1, sigma_val, sm_val);
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
                JXL_EPF_V dist0 = JXL_EPF_ZERO();
                JXL_EPF_V dist1 = JXL_EPF_ZERO();
                for (c = 0; c < 3; ++c) {
                    JXL_EPF_V scale = JXL_EPF_SPLAT(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];

                    JXL_EPF_V v0r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + 1 + mx);
                    JXL_EPF_V v0l = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx - 1 + mx);
                    JXL_EPF_V v0 = JXL_EPF_SHUF(v0l, v0r, 1, 2, 3, 6);
                    JXL_EPF_V acc0 = JXL_EPF_ABS(JXL_EPF_SUB(v0l, v0));
                    JXL_EPF_V acc1 = JXL_EPF_ABS(JXL_EPF_SUB(v0r, v0));

                    JXL_EPF_V v1rr = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + 2 + mx);
                    JXL_EPF_V v1ll = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx - 2 + mx);
                    JXL_EPF_V v1l = JXL_EPF_SHUF(v1ll, v1rr, 1, 2, 3, 4);
                    JXL_EPF_V v1 = JXL_EPF_SHUF(v1ll, v1rr, 2, 3, 4, 5);
                    JXL_EPF_V v1r = JXL_EPF_SHUF(v1ll, v1rr, 3, 4, 5, 6);
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1ll, v1l)));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1l)));
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1r)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1l)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v1, v1r)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v1rr, v1r)));

                    JXL_EPF_V v2r = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx + 1 + mx);
                    JXL_EPF_V v2l = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 4) + dx - 1 + mx);
                    JXL_EPF_V v2 = JXL_EPF_SHUF(v2l, v2r, 1, 2, 3, 6);
                    acc0 = JXL_EPF_ADD(acc0, JXL_EPF_ABS(JXL_EPF_SUB(v2l, v2)));
                    acc1 = JXL_EPF_ADD(acc1, JXL_EPF_ABS(JXL_EPF_SUB(v2r, v2)));

                    dist0 = JXL_EPF_ADD(dist0, JXL_EPF_MUL(scale, acc0));
                    dist1 = JXL_EPF_ADD(dist1, JXL_EPF_MUL(scale, acc1));
                }

                JXL_EPF_V weight0 = jxl_epf_weight_wasm128(dist0, sigma_val, sm_val);
                JXL_EPF_V weight1 = jxl_epf_weight_wasm128(dist1, sigma_val, sm_val);
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
                int64_t kx_off = kernel_offsets[ki].kx;
                int64_t ky_off = kernel_offsets[ki].ky;
                size_t ky = (size_t)((int64_t)3 + ky_off);
                size_t kx = (size_t)((int64_t)dx + kx_off);

                JXL_EPF_V dist = JXL_EPF_ZERO();
                for (c = 0; c < 3; ++c) {
                    JXL_EPF_V scale = JXL_EPF_SPLAT(channel_scale[c]);
                    const jxl_const_subgrid_f32 *input_rows = &merged_input_rows[c];
                    if (step == 0) {
                        JXL_EPF_V vk0 =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky - 1) + kx + mx);
                        JXL_EPF_V vb0 = JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 2) + dx + mx);
                        JXL_EPF_V acc = JXL_EPF_ABS(JXL_EPF_SUB(vk0, vb0));

                        JXL_EPF_V vk1r =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky) + kx + 1 + mx);
                        JXL_EPF_V vb1r =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx + 1 + mx);
                        acc = JXL_EPF_ADD(acc, JXL_EPF_ABS(JXL_EPF_SUB(vk1r, vb1r)));

                        JXL_EPF_V vk1l =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, ky) + kx - 1 + mx);
                        JXL_EPF_V vb1l =
                            JXL_EPF_LOAD(jxl_epf_merged_row(input_rows, 3) + dx - 1 + mx);
                        JXL_EPF_V vk1 = JXL_EPF_SHUF(vk1l, vk1r, 1, 2, 3, 6);
                        JXL_EPF_V vb1 = JXL_EPF_SHUF(vb1l, vb1r, 1, 2, 3, 6);
                        acc = JXL_EPF_ADD(acc, JXL_EPF_ABS(JXL_EPF_SUB(vk1, vb1)));
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

                JXL_EPF_V weight = jxl_epf_weight_wasm128(dist, sigma_val, sm_val);
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

void jxl_epf_row_wasm128(jxl_epf_row *row) {
    if (row == NULL || !row->use_merged) {
        return;
    }
    jxl_epf_row_wasm128_inner(row, row->step);
}

#endif
