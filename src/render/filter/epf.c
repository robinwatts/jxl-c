// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/epf.h"

#include "context.h"
#include "render/filter/epf_internal.h"
#include "render/filter/filter_util.h"
#include "render/simd/features.h"
#include "render/subgrid_f32.h"

#if defined(JXL_HAVE_SIMD_SSE41)
#include "render/filter/epf_sse41.h"
#endif
#if defined(JXL_HAVE_SIMD_WASM128)
#include "render/filter/epf_wasm128.h"
#endif
#if defined(JXL_HAVE_SIMD_NEON)
#include "render/filter/epf_neon.h"
#endif

#include <math.h>
#include <stddef.h>
#include "jxl_oxide/jxl_types.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int epf_use_scalar(const jxl_context *ctx) {
    return ctx != NULL && ctx->debug.epf_test_force_scalar;
}

static int epf_assert_row_parity(const jxl_context *ctx) {
    if (ctx == NULL) {
        return 0;
    }
    return JXL_DEBUG_FLAG(ctx, epf_assert_row);
}

static uint32_t trailing_zeros_u32(uint32_t v) {
    uint32_t n;
    if (v == 0) {
        return 32;
    }
    n = 0;
    while ((v & 1u) == 0) {
        v >>= 1;
        n++;
    }
    return n;
}

static float lookup_sigma(const jxl_epf_filter *epf, const jxl_frame_header *fh,
                          const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                          const jxl_filter_frame_region *region, int32_t frame_x,
                          int32_t frame_y) {
    uint32_t mask;
    uint32_t sigma_group_x;
    uint32_t sigma_group_y;
    uint32_t sigma_inner_x;
    uint32_t sigma_inner_y;
    uint32_t lf_idx;
    int32_t local_x;
    int32_t local_y;
    uint32_t group_dim;
    uint32_t shift;
    uint32_t groups_per_row;
    uint32_t sigma_x;
    uint32_t sigma_y;
    const jxl_lf_group *lg;
    if (epf == NULL) {
        return 1.0f;
    }
    if (region == NULL || frame_x < 0 || frame_y < 0) {
        return epf->sigma_for_modular;
    }
    local_x = frame_x - (int32_t)region->frame_left;
    local_y = frame_y - (int32_t)region->frame_top;
    if (local_x < 0 || local_y < 0 || (uint32_t)local_x >= region->frame_width ||
        (uint32_t)local_y >= region->frame_height) {
        return epf->sigma_for_modular;
    }

    group_dim = jxl_frame_header_group_dim(fh);
    shift = trailing_zeros_u32(group_dim);
    mask = group_dim - 1u;
    groups_per_row = jxl_frame_header_lf_groups_per_row(fh);

    sigma_x = (uint32_t)frame_x / 8u;
    sigma_y = (uint32_t)frame_y / 8u;
    sigma_group_x = sigma_x >> shift;
    sigma_group_y = sigma_y >> shift;
    sigma_inner_x = sigma_x & mask;
    sigma_inner_y = sigma_y & mask;
    lf_idx = sigma_group_y * groups_per_row + sigma_group_x;
    if (lf_idx >= num_lf_groups || lf_groups == NULL || lf_groups[lf_idx].epf_sigma == NULL) {
        return epf->sigma_for_modular;
    }
    lg = &lf_groups[lf_idx];
    if (sigma_inner_x >= lg->epf_sigma_width || sigma_inner_y >= lg->epf_sigma_height) {
        return epf->sigma_for_modular;
    }
    return lg->epf_sigma[sigma_inner_y * lg->epf_sigma_stride + sigma_inner_x];
}

static void epf_fill_sigma_pixels(float *sigma_pixels, size_t width, size_t y,
                                  const jxl_epf_filter *epf, const jxl_frame_header *fh,
                                  const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                                  const jxl_filter_frame_region *region) {
    int32_t frame_top = region != NULL ? region->frame_top : 0;
    int32_t frame_left = region != NULL ? region->frame_left : 0;
    int32_t frame_y = (int32_t)y + frame_top;
    size_t bx;
    float fallback;

    if (epf == NULL) {
        return;
    }
    fallback = epf->sigma_for_modular;
    if (region == NULL || frame_y < 0) {
        for (bx = 0; bx < width; bx += 8) {
            sigma_pixels[bx] = fallback;
        }
        return;
    }

    {
        int32_t local_y = frame_y - (int32_t)region->frame_top;
        if (local_y < 0 || (uint32_t)local_y >= region->frame_height) {
            for (bx = 0; bx < width; bx += 8) {
                sigma_pixels[bx] = fallback;
            }
            return;
        }
    }

    {
        uint32_t group_dim = jxl_frame_header_group_dim(fh);
        uint32_t shift = trailing_zeros_u32(group_dim);
        uint32_t mask = group_dim - 1u;
        uint32_t groups_per_row = jxl_frame_header_lf_groups_per_row(fh);
        uint32_t sigma_y = (uint32_t)frame_y / 8u;
        uint32_t sigma_group_y = sigma_y >> shift;
        uint32_t sigma_inner_y = sigma_y & mask;

        for (bx = 0; bx < width; bx += 8) {
            int32_t frame_x = frame_left + (int32_t)bx;
            int32_t local_x = frame_x - (int32_t)region->frame_left;
            uint32_t sigma_x;
            uint32_t sigma_group_x;
            uint32_t sigma_inner_x;
            uint32_t lf_idx;
            if (local_x < 0 || (uint32_t)local_x >= region->frame_width) {
                sigma_pixels[bx] = fallback;
                continue;
            }

            sigma_x = (uint32_t)frame_x / 8u;
            sigma_group_x = sigma_x >> shift;
            sigma_inner_x = sigma_x & mask;
            lf_idx = sigma_group_y * groups_per_row + sigma_group_x;
            if (lf_idx >= num_lf_groups || lf_groups == NULL ||
                lf_groups[lf_idx].epf_sigma == NULL) {
                sigma_pixels[bx] = fallback;
                continue;
            }
            {
                const jxl_lf_group *lg = &lf_groups[lf_idx];
                if (sigma_inner_x >= lg->epf_sigma_width ||
                    sigma_inner_y >= lg->epf_sigma_height) {
                    sigma_pixels[bx] = fallback;
                    continue;
                }
                sigma_pixels[bx] =
                    lg->epf_sigma[sigma_inner_y * lg->epf_sigma_stride + sigma_inner_x];
            }
        }
    }
}

jxl_inline float epf_sigma_at(const float *sigma_pixels, size_t x) {
    return sigma_pixels[(x / 8u) * 8u];
}

static void epf_pixel(const jxl_filter_extent input[3], jxl_subgrid_f32 output[3], size_t width,
                      size_t height, size_t x, size_t y, int32_t frame_y, float sigma_val,
                      const jxl_epf_filter *epf, unsigned step) {
    size_t c;
    size_t ki;
    size_t dist_len;
    float sm[8];
    float sum_weights;
    float sum_channels[3];
    float step_multiplier;
    float sm_idx;
    int is_y_border;
    size_t kernel_len = step == 0 ? 12 : 4;
    const jxl_epf_kernel_offset *kernel = step == 0 ? k_epf_kernel_2 : k_epf_kernel_1;
    const jxl_epf_kernel_offset *dist =
        step == 0 ? k_dist_step0 : (step == 1 ? k_dist_step1 : k_dist_step2);

    dist_len = step == 0 ? 5 : (step == 1 ? 5 : 1);
    step_multiplier = step == 0 ? epf->sigma.pass0_sigma_scale
                                : (step == 2 ? epf->sigma.pass2_sigma_scale : 1.0f);
    is_y_border = (((int)frame_y + 1) & 0b110) == 0;
    if (is_y_border) {
        size_t i;
        for (i = 0; i < 8; ++i) {
            sm[i] = step_multiplier * epf->sigma.border_sad_mul;
        }
    } else {
        size_t i;
        for (i = 0; i < 8; ++i) {
            sm[i] = step_multiplier;
        }
        sm[0] *= epf->sigma.border_sad_mul;
        sm[7] *= epf->sigma.border_sad_mul;
    }
    sm_idx = sm[x & 7u];

    if (sigma_val < 0.3f) {
        for (c = 0; c < 3; ++c) {
            jxl_subgrid_f32_set(output[c], x, y,
                                jxl_filter_extent_get(&input[c], (int64_t)x, (int64_t)y));
        }
        return;
    }

    sum_weights = 1.0f;
    for (c = 0; c < 3; ++c) {
        sum_channels[c] = jxl_filter_extent_get(&input[c], (int64_t)x, (int64_t)y);
    }

    for (ki = 0; ki < kernel_len; ++ki) {
        size_t c;
        int64_t kx = kernel[ki].kx;
        int64_t ky = kernel[ki].ky;
        float dist_acc = 0.0f;
        float weight;
        int64_t sample_x;
        int64_t sample_y;
        for (c = 0; c < 3; ++c) {
            size_t di;
            float acc = 0.0f;
            for (di = 0; di < dist_len; ++di) {
                int64_t ix = dist[di].kx;
                int64_t iy = dist[di].ky;
                int64_t kernel_x = (int64_t)x + kx + ix;
                int64_t kernel_y = (int64_t)y + ky + iy;
                int64_t base_x = (int64_t)x + ix;
                int64_t base_y = (int64_t)y + iy;
                float kernel_v = jxl_filter_extent_get(&input[c], kernel_x, kernel_y);
                float base_v = jxl_filter_extent_get(&input[c], base_x, base_y);
                acc += fabsf(kernel_v - base_v);
            }
            dist_acc += epf->channel_scale[c] * acc;
        }

        weight = jxl_epf_weight(dist_acc, sigma_val, sm_idx);
        sum_weights += weight;
        sample_x = (int64_t)x + kx;
        sample_y = (int64_t)y + ky;
        for (c = 0; c < 3; ++c) {
            float sample = jxl_filter_extent_get(&input[c], sample_x, sample_y);
            sum_channels[c] += weight * sample;
        }
    }

    for (c = 0; c < 3; ++c) {
        jxl_subgrid_f32_set(output[c], x, y, sum_channels[c] / sum_weights);
    }
}

static void epf_row(const jxl_context *ctx, const jxl_cpu_features *feat,
                    const jxl_filter_extent input[3], jxl_subgrid_f32 output[3], size_t width,
                    size_t height, size_t y, const jxl_epf_filter *epf, const jxl_frame_header *fh,
                    const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                    const jxl_filter_frame_region *region, unsigned step, float *sigma_pixels,
                    uint8_t *simd_processed) {
    size_t x;
    int32_t frame_top = region != NULL ? region->frame_top : 0;
    int simd_ran;
    int have_row_bufs;
    unsigned padding = 3u - step;
    (void)padding;

    have_row_bufs = sigma_pixels != NULL && simd_processed != NULL;
    if (have_row_bufs) {
        memset(simd_processed, 0, width);
        epf_fill_sigma_pixels(sigma_pixels, width, y, epf, fh, lf_groups, num_lf_groups, region);
    }

    simd_ran = 0;
#if 1 /* EPF SIMD enabled; JXL_EPF_ASSERT_ROW checks row parity */
#if defined(JXL_HAVE_SIMD_WASM128)
    if (have_row_bufs && !epf_use_scalar(ctx) && y >= 3 && y + 4 <= height &&
        width >= (size_t)padding * 2u) {
        size_t c;
        jxl_epf_row row;
        memset(&row, 0, sizeof(row));
        row.width = width;
        row.y = y + (size_t)frame_top;
        row.sigma_pixels = sigma_pixels;
        row.processed = simd_processed;
        row.epf_params = epf;
        row.step = step;
        row.use_merged = 1;

        for (c = 0; c < 3; ++c) {
            jxl_subgrid_f32 view = jxl_filter_extent_view(&input[c]);
            row.output_rows[c] = jxl_subgrid_f32_row_mut(output[c], y);
            row.merged_x0 = 0;
            row.merged_input[c] = jxl_const_subgrid_f32_from_buf(
                view.data + (y - 3) * view.stride, view.width, 7, view.stride);
        }

        jxl_epf_row_wasm128(&row);
        simd_ran = 1;

        if (epf_assert_row_parity(ctx)) {
            size_t x;
            for (x = 0; x < width; ++x) {
                size_t c;
                float saved[3];
                if (!simd_processed[x]) {
                    continue;
                }
                for (c = 0; c < 3; ++c) {
                    saved[c] = jxl_subgrid_f32_get(output[c], x, y);
                }
                int32_t frame_x = (int32_t)x + (region != NULL ? region->frame_left : 0);
                int32_t frame_y = (int32_t)y + frame_top;
                float sigma_val =
                    lookup_sigma(epf, fh, lf_groups, num_lf_groups, region, frame_x, frame_y);
                epf_pixel(input, output, width, height, x, y, frame_y, sigma_val, epf, step);
                for (c = 0; c < 3; ++c) {
                    int a = (int)(saved[c] * 65536.0f);
                    int b = (int)(jxl_subgrid_f32_get(output[c], x, y) * 65536.0f);
                    int d = a - b;
                    if (d < 0) {
                        d = -d;
                    }
                    if (d > 1) {
                        fprintf(stderr,
                                "epf row parity: step=%u y=%zu x=%zu c=%zu simd=%f scalar=%f "
                                "sigma=%f\n",
                                step, y, x, c, saved[c], jxl_subgrid_f32_get(output[c], x, y),
                                sigma_val);
                        assert(d <= 1);
                    }
                    jxl_subgrid_f32_set(output[c], x, y, saved[c]);
                }
            }
        }
    }
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    if (have_row_bufs && !epf_use_scalar(ctx) && y >= 3 && y + 4 <= height &&
        width >= (size_t)padding * 2u && feat->neon) {
            size_t c;
        jxl_epf_row row;
        memset(&row, 0, sizeof(row));
        row.width = width;
        row.y = y + (size_t)frame_top;
        row.sigma_pixels = sigma_pixels;
        row.processed = simd_processed;
        row.epf_params = epf;
        row.step = step;
        row.use_merged = 1;

        for (c = 0; c < 3; ++c) {
            jxl_subgrid_f32 view = jxl_filter_extent_view(&input[c]);
            row.output_rows[c] = jxl_subgrid_f32_row_mut(output[c], y);
            row.merged_x0 = 0;
            row.merged_input[c] = jxl_const_subgrid_f32_from_buf(
                view.data + (y - 3) * view.stride, view.width, 7, view.stride);
        }

        jxl_epf_row_neon(&row);
        simd_ran = 1;

        if (epf_assert_row_parity(ctx)) {
            size_t x;
            for (x = 0; x < width; ++x) {
                size_t c;
                float saved[3];
                if (!simd_processed[x]) {
                    continue;
                }
                for (c = 0; c < 3; ++c) {
                    saved[c] = jxl_subgrid_f32_get(output[c], x, y);
                }
                int32_t frame_x = (int32_t)x + (region != NULL ? region->frame_left : 0);
                int32_t frame_y = (int32_t)y + frame_top;
                float sigma_val =
                    lookup_sigma(epf, fh, lf_groups, num_lf_groups, region, frame_x, frame_y);
                epf_pixel(input, output, width, height, x, y, frame_y, sigma_val, epf, step);
                for (c = 0; c < 3; ++c) {
                    int a = (int)(saved[c] * 65536.0f);
                    int b = (int)(jxl_subgrid_f32_get(output[c], x, y) * 65536.0f);
                    int d = a - b;
                    if (d < 0) {
                        d = -d;
                    }
                    if (d > 1) {
                        fprintf(stderr,
                                "epf row parity: step=%u y=%zu x=%zu c=%zu simd=%f scalar=%f "
                                "sigma=%f\n",
                                step, y, x, c, saved[c], jxl_subgrid_f32_get(output[c], x, y),
                                sigma_val);
                        assert(d <= 1);
                    }
                    jxl_subgrid_f32_set(output[c], x, y, saved[c]);
                }
            }
        }
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    if (have_row_bufs && !simd_ran && !epf_use_scalar(ctx) && y >= 3 && y + 4 <= height &&
        width >= (size_t)padding * 2u && feat->sse41) {
            size_t c;
        jxl_epf_row row;
        memset(&row, 0, sizeof(row));
        row.width = width;
        row.y = y + (size_t)frame_top;
        row.sigma_pixels = sigma_pixels;
        row.processed = simd_processed;
        row.epf_params = epf;
        row.step = step;
        row.use_merged = 1;

        for (c = 0; c < 3; ++c) {
            jxl_subgrid_f32 view = jxl_filter_extent_view(&input[c]);
            row.output_rows[c] = jxl_subgrid_f32_row_mut(output[c], y);
            row.merged_x0 = 0;
            row.merged_input[c] = jxl_const_subgrid_f32_from_buf(
                view.data + (y - 3) * view.stride, view.width, 7, view.stride);
        }

        jxl_epf_row_sse41(&row);
        simd_ran = 1;

        if (epf_assert_row_parity(ctx)) {
            size_t x;
            for (x = 0; x < width; ++x) {
                size_t c;
                float saved[3];
                if (!simd_processed[x]) {
                    continue;
                }
                for (c = 0; c < 3; ++c) {
                    saved[c] = jxl_subgrid_f32_get(output[c], x, y);
                }
                int32_t frame_x = (int32_t)x + (region != NULL ? region->frame_left : 0);
                int32_t frame_y = (int32_t)y + frame_top;
                float sigma_val =
                    lookup_sigma(epf, fh, lf_groups, num_lf_groups, region, frame_x, frame_y);
                epf_pixel(input, output, width, height, x, y, frame_y, sigma_val, epf, step);
                for (c = 0; c < 3; ++c) {
                    int a = (int)(saved[c] * 65536.0f);
                    int b = (int)(jxl_subgrid_f32_get(output[c], x, y) * 65536.0f);
                    int d = a - b;
                    if (d < 0) {
                        d = -d;
                    }
                    if (d > 1) {
                        fprintf(stderr,
                                "epf row parity: step=%u y=%zu x=%zu c=%zu simd=%f scalar=%f "
                                "sigma=%f\n",
                                step, y, x, c, saved[c], jxl_subgrid_f32_get(output[c], x, y),
                                sigma_val);
                        assert(d <= 1);
                    }
                    jxl_subgrid_f32_set(output[c], x, y, saved[c]);
                }
            }
        }
    }
#endif
#endif

    for (x = 0; x < width; ++x) {
        int32_t frame_y;
        float sigma_val;
        if (simd_ran && have_row_bufs && simd_processed[x]) {
            continue;
        }
        frame_y = (int32_t)y + frame_top;
        sigma_val = have_row_bufs ? epf_sigma_at(sigma_pixels, x)
                                  : lookup_sigma(epf, fh, lf_groups, num_lf_groups, region,
                                                 (int32_t)x + (region != NULL ? region->frame_left : 0),
                                                 frame_y);
        epf_pixel(input, output, width, height, x, y, frame_y, sigma_val, epf, step);
    }
}

typedef struct {
    float *sigma_pixels;
    uint8_t *simd_processed;
    size_t capacity;
} jxl_epf_row_bufs;

static int epf_row_bufs_alloc(jxl_epf_row_bufs *bufs, size_t width) {
    if (width == 0) {
        return 1;
    }
    if (bufs->capacity >= width && bufs->sigma_pixels != NULL && bufs->simd_processed != NULL) {
        return 1;
    }
    free(bufs->sigma_pixels);
    free(bufs->simd_processed);
    bufs->sigma_pixels = (float *)malloc(width * sizeof(float));
    bufs->simd_processed = (uint8_t *)malloc(width);
    if (bufs->sigma_pixels == NULL || bufs->simd_processed == NULL) {
        free(bufs->sigma_pixels);
        free(bufs->simd_processed);
        bufs->sigma_pixels = NULL;
        bufs->simd_processed = NULL;
        bufs->capacity = 0;
        return 0;
    }
    bufs->capacity = width;
    return 1;
}

static void epf_row_bufs_free(jxl_epf_row_bufs *bufs) {
    free(bufs->sigma_pixels);
    free(bufs->simd_processed);
    bufs->sigma_pixels = NULL;
    bufs->simd_processed = NULL;
    bufs->capacity = 0;
}

typedef struct {
    jxl_filter_extent input[3];
    jxl_subgrid_f32 output[3];
} jxl_epf_step_bufs;

static void epf_step_bufs_set(jxl_filter_extent channels[3], float *scratch[3],
                              jxl_epf_step_bufs *bufs, int output_to_scratch) {
    size_t c;
    size_t width = channels[0].width;
    size_t height = channels[0].height;
    if (output_to_scratch) {
        for (c = 0; c < 3; ++c) {
            bufs->input[c] = channels[c];
            bufs->output[c] = jxl_subgrid_f32_from_buf(scratch[c], width, height, width);
        }
    } else {
        for (c = 0; c < 3; ++c) {
            bufs->input[c].full =
                jxl_subgrid_f32_from_buf(scratch[c], width, height, width);
            bufs->input[c].origin_x = 0;
            bufs->input[c].origin_y = 0;
            bufs->input[c].width = width;
            bufs->input[c].height = height;
            bufs->output[c] = jxl_filter_extent_view(&channels[c]);
        }
    }
}

static void run_epf_step(const jxl_context *ctx, const jxl_cpu_features *feat,
                         const jxl_epf_step_bufs *bufs, const jxl_epf_filter *epf,
                         const jxl_frame_header *fh, const jxl_lf_group *lf_groups,
                         uint32_t num_lf_groups, const jxl_filter_frame_region *region,
                         unsigned step, jxl_epf_row_bufs *row_bufs) {
    size_t y;
    size_t width;
    size_t height;
    jxl_epf_row_bufs local_bufs;
    jxl_epf_row_bufs *bufs_row;
    float *sigma_pixels;
    uint8_t *simd_processed;

    width = bufs->input[0].width;
    height = bufs->input[0].height;
    memset(&local_bufs, 0, sizeof(local_bufs));
    bufs_row = row_bufs;
    if (bufs_row == NULL) {
        bufs_row = &local_bufs;
    }
    sigma_pixels = NULL;
    simd_processed = NULL;
    if (width > 0) {
        if (!epf_row_bufs_alloc(bufs_row, width)) {
            return;
        }
        sigma_pixels = bufs_row->sigma_pixels;
        simd_processed = bufs_row->simd_processed;
    }
    for (y = 0; y < height; ++y) {
        epf_row(ctx, feat, bufs->input, bufs->output, width, height, y, epf, fh, lf_groups,
                num_lf_groups, region, step, sigma_pixels, simd_processed);
    }
    if (row_bufs == NULL) {
        epf_row_bufs_free(&local_bufs);
    }
}

int jxl_apply_epf_extent(jxl_context *ctx, jxl_filter_extent channels[3],
                         const jxl_epf_filter *epf, const jxl_frame_header *frame_header,
                         const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                         const jxl_filter_frame_region *region, float *scratch[3]) {
    size_t c;
    size_t width;
    jxl_epf_row_bufs row_bufs;
    jxl_epf_step_bufs step_bufs;
    int output_to_scratch;
    const jxl_cpu_features *feat;

    if (channels == NULL || epf == NULL || !epf->enabled || frame_header == NULL ||
        scratch == NULL) {
        return 0;
    }
    feat = jxl_context_cpu_features(ctx);
    for (c = 0; c < 3; ++c) {
        if (channels[c].full.data == NULL || scratch[c] == NULL) {
            return 0;
        }
    }

    memset(&row_bufs, 0, sizeof(row_bufs));
    output_to_scratch = 1;
    width = channels[0].width;
    if (width > 0 && !epf_row_bufs_alloc(&row_bufs, width)) {
        return 0;
    }

    if (epf->iters == 3) {
        epf_step_bufs_set(channels, scratch, &step_bufs, output_to_scratch);
        run_epf_step(ctx, feat, &step_bufs, epf, frame_header, lf_groups, num_lf_groups, region, 0,
                     &row_bufs);
        output_to_scratch ^= 1;
    }
    epf_step_bufs_set(channels, scratch, &step_bufs, output_to_scratch);
    run_epf_step(ctx, feat, &step_bufs, epf, frame_header, lf_groups, num_lf_groups, region, 1,
                 &row_bufs);
    output_to_scratch ^= 1;
    if (epf->iters >= 2) {
        epf_step_bufs_set(channels, scratch, &step_bufs, output_to_scratch);
        run_epf_step(ctx, feat, &step_bufs, epf, frame_header, lf_groups, num_lf_groups, region, 2,
                     &row_bufs);
        output_to_scratch ^= 1;
    }
    if (output_to_scratch == 0) {
        for (c = 0; c < 3; ++c) {
            jxl_subgrid_f32 view = jxl_filter_extent_view(&channels[c]);
            jxl_subgrid_f32_copy_from_packed(view, scratch[c]);
        }
    }
    epf_row_bufs_free(&row_bufs);
    return 1;
}

int jxl_apply_epf_extent_step(jxl_context *ctx, jxl_filter_extent channels[3],
                              const jxl_epf_filter *epf, const jxl_frame_header *frame_header,
                              const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                              const jxl_filter_frame_region *region, float *scratch[3],
                              unsigned step) {
    size_t c;
    jxl_epf_step_bufs step_bufs;
    const jxl_cpu_features *feat;
    if (channels == NULL || epf == NULL || !epf->enabled || frame_header == NULL ||
        scratch == NULL || step > 2) {
        return 0;
    }
    feat = jxl_context_cpu_features(ctx);
    for (c = 0; c < 3; ++c) {
        if (channels[c].full.data == NULL || scratch[c] == NULL) {
            return 0;
        }
    }
    epf_step_bufs_set(channels, scratch, &step_bufs, 1);
    run_epf_step(ctx, feat, &step_bufs, epf, frame_header, lf_groups, num_lf_groups, region, step,
                 NULL);
    return 1;
}
