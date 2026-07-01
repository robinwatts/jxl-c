// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_SUBGRID_F32_H_
#define JXL_RENDER_SUBGRID_F32_H_

#include "jxl_oxide/jxl_types.h"

#include <stddef.h>

typedef struct {
    float *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_subgrid_f32;

typedef struct {
    const float *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_const_subgrid_f32;

jxl_inline jxl_subgrid_f32 jxl_subgrid_f32_from_buf(float *data, size_t width, size_t height,
                                                     size_t stride) {
    jxl_subgrid_f32 sg;
    sg.data = data;
    sg.width = width;
    sg.height = height;
    sg.stride = stride;
    return sg;
}

jxl_inline jxl_const_subgrid_f32 jxl_const_subgrid_f32_from_buf(const float *data, size_t width,
                                                                size_t height, size_t stride) {
    jxl_const_subgrid_f32 sg;
    sg.data = data;
    sg.width = width;
    sg.height = height;
    sg.stride = stride;
    return sg;
}

jxl_inline jxl_subgrid_f32 jxl_subgrid_f32_sub(jxl_subgrid_f32 sg, size_t x, size_t y, size_t w,
                                               size_t h) {
    return jxl_subgrid_f32_from_buf(sg.data + y * sg.stride + x, w, h, sg.stride);
}

jxl_inline float jxl_const_subgrid_f32_get(jxl_const_subgrid_f32 sg, size_t x, size_t y) {
    return sg.data[y * sg.stride + x];
}

jxl_inline float jxl_subgrid_f32_get(jxl_subgrid_f32 sg, size_t x, size_t y) {
    return sg.data[y * sg.stride + x];
}

jxl_inline void jxl_subgrid_f32_set(jxl_subgrid_f32 sg, size_t x, size_t y, float v) {
    sg.data[y * sg.stride + x] = v;
}

jxl_inline float *jxl_subgrid_f32_row_mut(jxl_subgrid_f32 sg, size_t y) {
    return sg.data + y * sg.stride;
}

jxl_inline void jxl_subgrid_f32_swap(jxl_subgrid_f32 sg, size_t ax, size_t ay, size_t bx,
                                     size_t by) {
    float *a = &sg.data[ay * sg.stride + ax];
    float *b = &sg.data[by * sg.stride + bx];
    float t = *a;
    *a = *b;
    *b = t;
}

void jxl_subgrid_f32_split_vertical(jxl_subgrid_f32 sg, size_t y, jxl_subgrid_f32 *top,
                                    jxl_subgrid_f32 *bottom);

void jxl_subgrid_f32_copy_from_packed(jxl_subgrid_f32 dst, const float *src);

#endif /* JXL_RENDER_SUBGRID_F32_H_ */
