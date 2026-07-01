// SPDX-License-Identifier: MIT OR Apache-2.0
#include "subgrid_f32.h"

void jxl_subgrid_f32_split_vertical(jxl_subgrid_f32 sg, size_t y, jxl_subgrid_f32 *top,
                                    jxl_subgrid_f32 *bottom) {
    jxl_subgrid_f32 compound_tmp;
    jxl_subgrid_f32 compound_tmp_2;
    compound_tmp.data = sg.data;
    compound_tmp.width = sg.width;
    compound_tmp.height = y;
    compound_tmp.stride = sg.stride;

    *top = compound_tmp;

    compound_tmp_2.data = sg.data + y * sg.stride;
    compound_tmp_2.width = sg.width;
    compound_tmp_2.height = sg.height - y;
    compound_tmp_2.stride = sg.stride;

    *bottom = compound_tmp_2;
}

void jxl_subgrid_f32_copy_from_packed(jxl_subgrid_f32 dst, const float *src) {
    size_t y;
    if (dst.data == NULL || src == NULL || dst.width == 0 || dst.height == 0) {
        return;
    }
    for (y = 0; y < dst.height; ++y) {
        size_t x;
        float *row = dst.data + y * dst.stride;
        const float *src_row = src + y * dst.width;
        for (x = 0; x < dst.width; ++x) {
            row[x] = src_row[x];
        }
    }
}
