// SPDX-License-Identifier: MIT OR Apache-2.0
#include "cfl_hf.h"

static int32_t cfl_get(jxl_const_subgrid_i32 sg, size_t x, size_t y) {
    size_t st;
    if (sg.data == NULL || x >= sg.width || y >= sg.height) {
        return 0;
    }
    st = sg.stride != 0 ? sg.stride : sg.width;
    return sg.data[y * st + x];
}

void jxl_chroma_from_luma_hf_grouped(jxl_subgrid_f32 coeff[3], jxl_const_subgrid_i32 x_from_y,
                                     jxl_const_subgrid_i32 b_from_y,
                                     const jxl_lf_channel_correlation *lf_chan_corr) {
                                         size_t y;
    jxl_subgrid_f32 coeff_x;
    jxl_subgrid_f32 coeff_y;
    jxl_subgrid_f32 coeff_b;
    size_t gw;
    size_t gh;
    float colour_factor;
    if (coeff == NULL || lf_chan_corr == NULL || coeff[0].data == NULL || coeff[1].data == NULL ||
        coeff[2].data == NULL) {
        return;
    }

    coeff_x = coeff[0];
    coeff_y = coeff[1];
    coeff_b = coeff[2];
    gw = coeff_x.width;
    gh = coeff_x.height;
    colour_factor = (float)lf_chan_corr->colour_factor;

    for (y = 0; y < gh; ++y) {
        size_t x64;
        size_t cfl_y = y / 64;
        const float *row_y = coeff_y.data + y * coeff_y.stride;
        float *row_x = coeff_x.data + y * coeff_x.stride;
        float *row_b = coeff_b.data + y * coeff_b.stride;
        for (x64 = 0; x64 < (gw + 63) / 64; ++x64) {
            size_t dx;
            int32_t kx_i = cfl_get(x_from_y, x64, cfl_y);
            int32_t kb_i = cfl_get(b_from_y, x64, cfl_y);
            float kx = lf_chan_corr->base_correlation_x + (float)kx_i / colour_factor;
            float kb = lf_chan_corr->base_correlation_b + (float)kb_i / colour_factor;

            size_t dx_max = gw - x64 * 64;
            size_t x0;
            if (dx_max > 64) {
                dx_max = 64;
            }
            x0 = x64 * 64;
            for (dx = 0; dx < dx_max; ++dx) {
                size_t x = x0 + dx;
                float cy = row_y[x];
                row_x[x] += kx * cy;
                row_b[x] += kb * cy;
            }
        }
    }
}
