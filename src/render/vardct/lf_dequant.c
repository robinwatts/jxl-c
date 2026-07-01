// SPDX-License-Identifier: MIT OR Apache-2.0
#include "lf_dequant.h"

#include "vardct/hf_coeff.h"

void jxl_copy_lf_dequant(jxl_subgrid_f32 grid, const jxl_quantizer *quantizer, float m_lf,
                         const jxl_lf_quant_subgrid_u32 *channel_data, uint8_t extra_precision) {
                             size_t y;
    int32_t precision_scale;
    size_t width;
    size_t height;
    uint64_t scale_inv;
    float scale;
    if (quantizer == NULL || extra_precision >= 4) {
        return;
    }

    precision_scale = 1 << (9 - extra_precision);
    scale_inv = (uint64_t)quantizer->global_scale * (uint64_t)quantizer->quant_lf;
    if (scale_inv == 0) {
        return;
    }
    scale = (float)((double)m_lf * (double)precision_scale / (double)scale_inv);

    if (channel_data == NULL) {
        return;
    }

    width = channel_data->width;
    height = channel_data->height;
    if (width > grid.width) {
        width = grid.width;
    }
    if (height > grid.height) {
        height = grid.height;
    }

    for (y = 0; y < height; ++y) {
        size_t x;
        for (x = 0; x < width; ++x) {
            int32_t q = jxl_lf_quant_subgrid_sample(channel_data, x, y);
            jxl_subgrid_f32_set(grid, x, y, (float)q * scale);
        }
    }
}
