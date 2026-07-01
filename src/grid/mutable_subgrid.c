// SPDX-License-Identifier: MIT OR Apache-2.0
#include "mutable_subgrid.h"

#include <assert.h>
#include <string.h>

static size_t subgrid_required_len(size_t width, size_t height, size_t stride) {
    size_t last_row;
    size_t offset;
    if (height == 0 || width == 0) {
        return 0;
    }
    if (height == 1) {
        return width;
    }
    last_row = height - 1;
    assert(last_row <= SIZE_MAX / stride);
    offset = last_row * stride;
    assert(offset <= SIZE_MAX - width);
    return offset + width;
}

static uint32_t *ptr_wrapping(uint32_t *base, size_t x, size_t y, size_t stride) {
    size_t offset = y * stride + x;
    return base + offset;
}

static void *split_base_of(jxl_mutable_subgrid_u32 sg) {
    return sg.split_base != NULL ? sg.split_base : (void *)sg.ptr;
}

int jxl_mutable_subgrid_u32_from_buf(uint32_t *buf, size_t width, size_t height, size_t stride,
                                     jxl_mutable_subgrid_u32 *out) {
    size_t need;
    if (out == NULL) {
        return 0;
    }
    if (width > stride) {
        return 0;
    }
    if (width == 0 || height == 0) {
        out->ptr = buf;
        out->split_base = NULL;
        out->width = width;
        out->height = height;
        out->stride = stride;
        return width <= stride;
    }
    need = subgrid_required_len(width, height, stride);
    (void)need;
    if (buf == NULL) {
        return 0;
    }
    out->ptr = buf;
    out->split_base = NULL;
    out->width = width;
    out->height = height;
    out->stride = stride;
    return 1;
}

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_empty(void) {
    jxl_mutable_subgrid_u32 sg = {0};
    sg.ptr = NULL;
    return sg;
}

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_from_grid(jxl_grid_u32 *grid) {
    jxl_mutable_subgrid_u32 sg = {0};
    if (!jxl_mutable_subgrid_u32_from_buf(jxl_grid_u32_buf(grid), grid->width, grid->height,
                                          grid->width, &sg)) {
        assert(0);
    }
    return sg;
}

size_t jxl_mutable_subgrid_u32_width(jxl_mutable_subgrid_u32 sg) { return sg.width; }
size_t jxl_mutable_subgrid_u32_height(jxl_mutable_subgrid_u32 sg) { return sg.height; }

int jxl_mutable_subgrid_u32_try_get(jxl_mutable_subgrid_u32 sg, size_t x, size_t y,
                                    uint32_t *out) {
    if (out == NULL || x >= sg.width || y >= sg.height) {
        return 0;
    }
    *out = *ptr_wrapping(sg.ptr, x, y, sg.stride);
    return 1;
}

void jxl_mutable_subgrid_u32_set(jxl_mutable_subgrid_u32 sg, size_t x, size_t y, uint32_t v) {
    assert(x < sg.width && y < sg.height);
    *ptr_wrapping(sg.ptr, x, y, sg.stride) = v;
}

const uint32_t *jxl_mutable_subgrid_u32_row(jxl_mutable_subgrid_u32 sg, size_t row) {
    assert(row < sg.height);
    if (sg.width == 0) {
        return sg.ptr;
    }
    return ptr_wrapping(sg.ptr, 0, row, sg.stride);
}

uint32_t *jxl_mutable_subgrid_u32_row_mut(jxl_mutable_subgrid_u32 sg, size_t row) {
    assert(row < sg.height);
    if (sg.width == 0) {
        return sg.ptr;
    }
    return ptr_wrapping(sg.ptr, 0, row, sg.stride);
}

void jxl_mutable_subgrid_u32_swap(jxl_mutable_subgrid_u32 sg, size_t ax, size_t ay, size_t bx,
                                  size_t by) {
    uint32_t *a = ptr_wrapping(sg.ptr, ax, ay, sg.stride);
    uint32_t *b = ptr_wrapping(sg.ptr, bx, by, sg.stride);
    uint32_t tmp;
    if (a == b) {
        return;
    }
    tmp = *a;
    *a = *b;
    *b = tmp;
}

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_borrow(jxl_mutable_subgrid_u32 sg) {
    jxl_mutable_subgrid_u32 out = sg;
    return out;
}

jxl_shared_subgrid_u32 jxl_mutable_subgrid_u32_as_shared(jxl_mutable_subgrid_u32 sg) {
    jxl_shared_subgrid_u32 out;
    if (!jxl_shared_subgrid_u32_from_buf(sg.ptr, sg.width, sg.height, sg.stride, &out)) {
        if (sg.width == 0 || sg.height == 0) {
            out.ptr = sg.ptr;
            out.width = sg.width;
            out.height = sg.height;
            out.stride = sg.stride;
        } else {
            assert(0);
        }
    }
    return out;
}

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_subgrid(jxl_mutable_subgrid_u32 sg, size_t x0,
                                                        size_t x1, size_t y0, size_t y1) {
    jxl_mutable_subgrid_u32 out;
    if (x1 == SIZE_MAX) {
        x1 = sg.width;
    }
    if (y1 == SIZE_MAX) {
        y1 = sg.height;
    }
    assert(x0 <= x1 && y0 <= y1 && x1 <= sg.width && y1 <= sg.height);
    out.ptr = ptr_wrapping(sg.ptr, x0, y0, sg.stride);
    out.split_base = sg.split_base;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.stride = sg.stride;
    return out;
}

void jxl_mutable_subgrid_u32_split_horizontal(jxl_mutable_subgrid_u32 sg, size_t x,
                                              jxl_mutable_subgrid_u32 *left,
                                              jxl_mutable_subgrid_u32 *right) {
    void *base;
    assert(x <= sg.width);
    base = split_base_of(sg);
    left->ptr = sg.ptr;
    left->split_base = base;
    left->width = x;
    left->height = sg.height;
    left->stride = sg.stride;

    right->ptr = ptr_wrapping(sg.ptr, x, 0, sg.stride);
    right->split_base = base;
    right->width = sg.width - x;
    right->height = sg.height;
    right->stride = sg.stride;
}

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_split_horizontal_in_place(jxl_mutable_subgrid_u32 *sg,
                                                                          size_t x) {
    jxl_mutable_subgrid_u32 right;
    void *base;
    size_t right_width;
    uint32_t *right_ptr;
    assert(sg != NULL && x <= sg->width);
    base = split_base_of(*sg);
    right_width = sg->width - x;
    right_ptr = ptr_wrapping(sg->ptr, x, 0, sg->stride);
    sg->width = x;
    sg->split_base = base;

    right.ptr = right_ptr;
    right.split_base = base;
    right.width = right_width;
    right.height = sg->height;
    right.stride = sg->stride;
    return right;
}

void jxl_mutable_subgrid_u32_split_vertical(jxl_mutable_subgrid_u32 sg, size_t y,
                                            jxl_mutable_subgrid_u32 *top,
                                            jxl_mutable_subgrid_u32 *bottom) {
    void *base;
    assert(y <= sg.height);
    base = split_base_of(sg);
    top->ptr = sg.ptr;
    top->split_base = base;
    top->width = sg.width;
    top->height = y;
    top->stride = sg.stride;

    bottom->ptr = ptr_wrapping(sg.ptr, 0, y, sg.stride);
    bottom->split_base = base;
    bottom->width = sg.width;
    bottom->height = sg.height - y;
    bottom->stride = sg.stride;
}

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_split_vertical_in_place(jxl_mutable_subgrid_u32 *sg,
                                                                        size_t y) {
    jxl_mutable_subgrid_u32 bottom;
    void *base;
    size_t bottom_height;
    uint32_t *bottom_ptr;
    assert(sg != NULL && y <= sg->height);
    base = split_base_of(*sg);
    bottom_height = sg->height - y;
    bottom_ptr = ptr_wrapping(sg->ptr, 0, y, sg->stride);
    sg->height = y;
    sg->split_base = base;

    bottom.ptr = bottom_ptr;
    bottom.split_base = base;
    bottom.width = sg->width;
    bottom.height = bottom_height;
    bottom.stride = sg->stride;
    return bottom;
}

void jxl_mutable_subgrid_u32_merge_horizontal_in_place(jxl_mutable_subgrid_u32 *left,
                                                       jxl_mutable_subgrid_u32 right) {
    assert(left->split_base != NULL && left->split_base == right.split_base &&
           left->stride == right.stride && left->height == right.height &&
           left->stride >= left->width + right.width);
    assert(ptr_wrapping(left->ptr, left->width, 0, left->stride) == right.ptr);
    left->width += right.width;
}

void jxl_mutable_subgrid_u32_merge_vertical_in_place(jxl_mutable_subgrid_u32 *top,
                                                     jxl_mutable_subgrid_u32 bottom) {
    assert(top->split_base != NULL && top->split_base == bottom.split_base &&
           top->stride == bottom.stride && top->width == bottom.width);
    assert(ptr_wrapping(top->ptr, 0, top->height, top->stride) == bottom.ptr);
    top->height += bottom.height;
}

static jxl_mutable_subgrid_u32_list make_groups(jxl_allocator_state *alloc,
                                                jxl_mutable_subgrid_u32 sg, size_t group_width,
                                                size_t group_height, size_t num_cols,
                                                size_t num_rows) {
    size_t gy;
    jxl_mutable_subgrid_u32_list list;
    size_t group_count;
    void *split_base;
    assert(alloc != NULL);
    assert(group_width != 0 && group_height != 0);
    assert(num_cols <= SIZE_MAX / num_rows);
    group_count = num_cols * num_rows;

    list.count = group_count;
    list.items = jxl_calloc(alloc, group_count, sizeof(*list.items));
    assert(list.items != NULL);

    split_base = split_base_of(sg);
    for (gy = 0; gy < num_rows; ++gy) {
        size_t gx;
        size_t y = gy * group_height;
        size_t gh;
        if (y > sg.height) {
            y = sg.height;
        }
        gh = sg.height - y;
        if (gh > group_height) {
            gh = group_height;
        }
        for (gx = 0; gx < num_cols; ++gx) {
            size_t x = gx * group_width;
            size_t offset;
            size_t gw;
            jxl_mutable_subgrid_u32 *g;
            if (x > sg.width) {
                x = sg.width;
            }
            gw = sg.width - x;
            if (gw > group_width) {
                gw = group_width;
            }
            offset = 0;
            if (gh > 0 && sg.width > 0) {
                assert(y <= SIZE_MAX / sg.stride);
                offset = y * sg.stride + x;
            }
            g = &list.items[gy * num_cols + gx];
            g->ptr = sg.ptr + offset;
            g->split_base = split_base;
            g->width = gw;
            g->height = gh;
            g->stride = sg.stride;
        }
    }
    return list;
}

jxl_mutable_subgrid_u32_list jxl_mutable_subgrid_u32_into_groups(jxl_allocator_state *alloc,
                                                                 jxl_mutable_subgrid_u32 sg,
                                                                 size_t group_width,
                                                                 size_t group_height) {
    size_t num_cols = (sg.width + group_width - 1) / group_width;
    size_t num_rows = (sg.height + group_height - 1) / group_height;
    return make_groups(alloc, sg, group_width, group_height, num_cols, num_rows);
}

jxl_mutable_subgrid_u32_list jxl_mutable_subgrid_u32_into_groups_fixed(
    jxl_allocator_state *alloc, jxl_mutable_subgrid_u32 sg, size_t group_width,
    size_t group_height, size_t num_cols, size_t num_rows) {
    return make_groups(alloc, sg, group_width, group_height, num_cols, num_rows);
}

void jxl_mutable_subgrid_u32_list_destroy(jxl_allocator_state *alloc,
                                          jxl_mutable_subgrid_u32_list *list) {
    if (list == NULL || alloc == NULL) {
        return;
    }
    jxl_free(alloc, list->items);
    list->items = NULL;
    list->count = 0;
}
