// SPDX-License-Identifier: MIT OR Apache-2.0
#include "shared_subgrid.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"
#include <assert.h>

static uint32_t *ptr_wrapping(const uint32_t *base, size_t x, size_t y, size_t stride) {
    size_t offset = y * stride + x;
    return (uint32_t *)(base + offset);
}

int jxl_shared_subgrid_u32_from_buf(const uint32_t *buf, size_t width, size_t height, size_t stride,
                                    jxl_shared_subgrid_u32 *out) {
    if (out == NULL) {
        return 0;
    }
    if (width == 0 || height == 0) {
        return 0;
    }
    if (width > stride || buf == NULL) {
        return 0;
    }
    out->ptr = buf;
    out->width = width;
    out->height = height;
    out->stride = stride;
    return 1;
}

jxl_shared_subgrid_u32 jxl_shared_subgrid_u32_from_grid(const jxl_grid_u32 *grid) {
    jxl_shared_subgrid_u32 sg = {0};
    const uint32_t *buf;
    assert(grid != NULL);
    buf = jxl_grid_u32_buf_const(grid);
    assert(buf != NULL);
    if (!jxl_shared_subgrid_u32_from_buf(buf, grid->width, grid->height, grid->width, &sg)) {
        assert(0);
    }
    return sg;
}

size_t jxl_shared_subgrid_u32_width(jxl_shared_subgrid_u32 sg) { return sg.width; }
size_t jxl_shared_subgrid_u32_height(jxl_shared_subgrid_u32 sg) { return sg.height; }

int jxl_shared_subgrid_u32_try_get(jxl_shared_subgrid_u32 sg, size_t x, size_t y, uint32_t *out) {
    if (out == NULL || x >= sg.width || y >= sg.height) {
        return 0;
    }
    *out = *ptr_wrapping(sg.ptr, x, y, sg.stride);
    return 1;
}

const uint32_t *jxl_shared_subgrid_u32_row(jxl_shared_subgrid_u32 sg, size_t row) {
    assert(row < sg.height);
    if (sg.width == 0) {
        return sg.ptr;
    }
    return ptr_wrapping(sg.ptr, 0, row, sg.stride);
}

void jxl_shared_subgrid_u32_split_horizontal(jxl_shared_subgrid_u32 sg, size_t x,
                                           jxl_shared_subgrid_u32 *left,
                                           jxl_shared_subgrid_u32 *right) {
    assert(x <= sg.width);
    left->ptr = sg.ptr;
    left->width = x;
    left->height = sg.height;
    left->stride = sg.stride;

    right->ptr = ptr_wrapping(sg.ptr, x, 0, sg.stride);
    right->width = sg.width - x;
    right->height = sg.height;
    right->stride = sg.stride;
}

void jxl_shared_subgrid_u32_split_vertical(jxl_shared_subgrid_u32 sg, size_t y,
                                         jxl_shared_subgrid_u32 *top,
                                         jxl_shared_subgrid_u32 *bottom) {
    assert(y <= sg.height);
    top->ptr = sg.ptr;
    top->width = sg.width;
    top->height = y;
    top->stride = sg.stride;

    bottom->ptr = ptr_wrapping(sg.ptr, 0, y, sg.stride);
    bottom->width = sg.width;
    bottom->height = sg.height - y;
    bottom->stride = sg.stride;
}

jxl_shared_subgrid_u32 jxl_shared_subgrid_u32_subgrid(jxl_shared_subgrid_u32 sg, size_t x0,
                                                      size_t x1, size_t y0, size_t y1) {
    jxl_shared_subgrid_u32 out;
    if (x1 == SIZE_MAX) {
        x1 = sg.width;
    }
    if (y1 == SIZE_MAX) {
        y1 = sg.height;
    }
    assert(x0 <= x1 && y0 <= y1 && x1 <= sg.width && y1 <= sg.height);
    out.ptr = ptr_wrapping(sg.ptr, x0, y0, sg.stride);
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.stride = sg.stride;
    return out;
}
