// SPDX-License-Identifier: MIT OR Apache-2.0
#include "lf_smooth.h"

#include <math.h>

void jxl_chroma_from_luma_lf(jxl_subgrid_f32 x, jxl_const_subgrid_f32 y, jxl_subgrid_f32 b,
                             const jxl_lf_channel_correlation *lf_chan_corr) {
                                 size_t row;
    size_t width;
    size_t height;
    int32_t x_factor;
    int32_t b_factor;
    float colour_factor;
    float kx;
    float kb;
    if (lf_chan_corr == NULL || x.data == NULL || y.data == NULL || b.data == NULL) {
        return;
    }

    x_factor = (int32_t)lf_chan_corr->x_factor_lf - 128;
    b_factor = (int32_t)lf_chan_corr->b_factor_lf - 128;
    colour_factor = (float)lf_chan_corr->colour_factor;
    kx = lf_chan_corr->base_correlation_x + (float)x_factor / colour_factor;
    kb = lf_chan_corr->base_correlation_b + (float)b_factor / colour_factor;

    width = x.width;
    height = x.height;
    if (y.width < width) {
        width = y.width;
    }
    if (b.width < width) {
        width = b.width;
    }
    if (y.height < height) {
        height = y.height;
    }
    if (b.height < height) {
        height = b.height;
    }

    for (row = 0; row < height; ++row) {
        size_t col;
        float *row_x = x.data + row * x.stride;
        const float *row_y = y.data + row * y.stride;
        float *row_b = b.data + row * b.stride;
        for (col = 0; col < width; ++col) {
            float yy = row_y[col];
            row_x[col] += kx * yy;
            row_b[col] += kb * yy;
        }
    }
}

int jxl_adaptive_lf_smoothing(jxl_allocator_state *alloc, jxl_subgrid_f32 x, jxl_subgrid_f32 y,
                              jxl_subgrid_f32 b, const jxl_lf_channel_dequant *lf_dequant,
                              const jxl_quantizer *quantizer) {
                                  size_t ch;
                                  size_t row;
    size_t udsum_count;
    size_t width;
    size_t height;
    uint64_t scale_inv;
    float lf_x;
    float lf_y;
    float lf_b;

    const float SCALE_SELF = 0.052262735f;
    const float SCALE_SIDE = 0.2034514f;
    const float SCALE_DIAG = 0.03348292f;

    float *udsum_x;
    float *udsum_y;
    float *udsum_b;
    if (alloc == NULL || lf_dequant == NULL || quantizer == NULL || x.data == NULL ||
        y.data == NULL || b.data == NULL) {
        return 0;
    }

    width = x.width;
    height = x.height;
    if (width != y.width || width != b.width || height != y.height || height != b.height ||
        width <= 2 || height <= 2) {
        return 1;
    }

    scale_inv = (uint64_t)quantizer->global_scale * (uint64_t)quantizer->quant_lf;
    if (scale_inv == 0) {
        return 1;
    }
    lf_x = (float)(512.0 * (double)lf_dequant->m_x_lf / (double)scale_inv);
    lf_y = (float)(512.0 * (double)lf_dequant->m_y_lf / (double)scale_inv);
    lf_b = (float)(512.0 * (double)lf_dequant->m_b_lf / (double)scale_inv);

    udsum_count = width * (height - 2);
    udsum_x = jxl_alloc(alloc, udsum_count * sizeof(float));
    udsum_y = jxl_alloc(alloc, udsum_count * sizeof(float));
    udsum_b = jxl_alloc(alloc, udsum_count * sizeof(float));
    if (udsum_x == NULL || udsum_y == NULL || udsum_b == NULL) {
        jxl_free(alloc, udsum_x);
        jxl_free(alloc, udsum_y);
        jxl_free(alloc, udsum_b);
        return 0;
    }

    for (ch = 0; ch < 3; ++ch) {
        size_t row;
        jxl_subgrid_f32 grid = ch == 0 ? x : (ch == 1 ? y : b);
        float *udsum = ch == 0 ? udsum_x : (ch == 1 ? udsum_y : udsum_b);
        for (row = 0; row < height - 2; ++row) {
            size_t col;
            const float *grid_row = grid.data + row * grid.stride;
            const float *grid_row2 = grid.data + (row + 2) * grid.stride;
            float *out_row = udsum + row * width;
            for (col = 0; col < width; ++col) {
                out_row[col] = grid_row[col] + grid_row2[col];
            }
        }
    }

    for (row = 1; row < height - 1; ++row) {
        size_t col;
        const float *x_row = x.data + row * x.stride;
        const float *y_row = y.data + row * y.stride;
        const float *b_row = b.data + row * b.stride;
        float *x_mut = x.data + row * x.stride;
        float *y_mut = y.data + row * y.stride;
        float *b_mut = b.data + row * b.stride;
        float in_x_prev = x_row[0];
        float in_y_prev = y_row[0];
        float in_b_prev = b_row[0];
        const float *udsum_x_row = udsum_x + (row - 1) * width;
        const float *udsum_y_row = udsum_y + (row - 1) * width;
        const float *udsum_b_row = udsum_b + (row - 1) * width;

        for (col = 1; col < width - 1; ++col) {
            float x_self = x_row[col];
            float x_side = in_x_prev + x_row[col + 1] + udsum_x_row[col];
            float x_diag = udsum_x_row[col - 1] + udsum_x_row[col + 1];
            float x_wa = x_self * SCALE_SELF + x_side * SCALE_SIDE + x_diag * SCALE_DIAG;
            float x_gap_t = fabsf(x_wa - x_self) / lf_x;

            float y_self = y_row[col];
            float y_side = in_y_prev + y_row[col + 1] + udsum_y_row[col];
            float y_diag = udsum_y_row[col - 1] + udsum_y_row[col + 1];
            float y_wa = y_self * SCALE_SELF + y_side * SCALE_SIDE + y_diag * SCALE_DIAG;
            float y_gap_t = fabsf(y_wa - y_self) / lf_y;

            float b_self = b_row[col];
            float b_side = in_b_prev + b_row[col + 1] + udsum_b_row[col];
            float b_diag = udsum_b_row[col - 1] + udsum_b_row[col + 1];
            float b_wa = b_self * SCALE_SELF + b_side * SCALE_SIDE + b_diag * SCALE_DIAG;
            float b_gap_t = fabsf(b_wa - b_self) / lf_b;

            float gap = 0.5f;
            float gap_scale;
            if (x_gap_t > gap) {
                gap = x_gap_t;
            }
            if (y_gap_t > gap) {
                gap = y_gap_t;
            }
            if (b_gap_t > gap) {
                gap = b_gap_t;
            }
            gap_scale = 3.0f - 4.0f * gap;
            if (gap_scale < 0.0f) {
                gap_scale = 0.0f;
            }

            x_mut[col] = (x_wa - x_self) * gap_scale + x_self;
            y_mut[col] = (y_wa - y_self) * gap_scale + y_self;
            b_mut[col] = (b_wa - b_self) * gap_scale + b_self;

            in_x_prev = x_self;
            in_y_prev = y_self;
            in_b_prev = b_self;
        }
    }

    jxl_free(alloc, udsum_x);
    jxl_free(alloc, udsum_y);
    jxl_free(alloc, udsum_b);
    return 1;
}
