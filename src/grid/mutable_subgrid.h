// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_GRID_MUTABLE_SUBGRID_H_
#define JXL_GRID_MUTABLE_SUBGRID_H_

#include "grid/aligned_grid.h"
#include "grid/shared_subgrid.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint32_t *ptr;
    void *split_base;
    size_t width;
    size_t height;
    size_t stride;
} jxl_mutable_subgrid_u32;

int jxl_mutable_subgrid_u32_from_buf(uint32_t *buf, size_t width, size_t height, size_t stride,
                                     jxl_mutable_subgrid_u32 *out);

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_empty(void);
jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_from_grid(jxl_grid_u32 *grid);

size_t jxl_mutable_subgrid_u32_width(jxl_mutable_subgrid_u32 sg);
size_t jxl_mutable_subgrid_u32_height(jxl_mutable_subgrid_u32 sg);

int jxl_mutable_subgrid_u32_try_get(jxl_mutable_subgrid_u32 sg, size_t x, size_t y,
                                    uint32_t *out);
void jxl_mutable_subgrid_u32_set(jxl_mutable_subgrid_u32 sg, size_t x, size_t y, uint32_t v);

const uint32_t *jxl_mutable_subgrid_u32_row(jxl_mutable_subgrid_u32 sg, size_t row);
uint32_t *jxl_mutable_subgrid_u32_row_mut(jxl_mutable_subgrid_u32 sg, size_t row);

void jxl_mutable_subgrid_u32_swap(jxl_mutable_subgrid_u32 sg, size_t ax, size_t ay, size_t bx,
                                  size_t by);

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_borrow(jxl_mutable_subgrid_u32 sg);
jxl_shared_subgrid_u32 jxl_mutable_subgrid_u32_as_shared(jxl_mutable_subgrid_u32 sg);

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_subgrid(jxl_mutable_subgrid_u32 sg, size_t x0,
                                                        size_t x1, size_t y0, size_t y1);

void jxl_mutable_subgrid_u32_split_horizontal(jxl_mutable_subgrid_u32 sg, size_t x,
                                              jxl_mutable_subgrid_u32 *left,
                                              jxl_mutable_subgrid_u32 *right);

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_split_horizontal_in_place(jxl_mutable_subgrid_u32 *sg,
                                                                          size_t x);

void jxl_mutable_subgrid_u32_split_vertical(jxl_mutable_subgrid_u32 sg, size_t y,
                                            jxl_mutable_subgrid_u32 *top,
                                            jxl_mutable_subgrid_u32 *bottom);

jxl_mutable_subgrid_u32 jxl_mutable_subgrid_u32_split_vertical_in_place(jxl_mutable_subgrid_u32 *sg,
                                                                        size_t y);

void jxl_mutable_subgrid_u32_merge_horizontal_in_place(jxl_mutable_subgrid_u32 *left,
                                                       jxl_mutable_subgrid_u32 right);

void jxl_mutable_subgrid_u32_merge_vertical_in_place(jxl_mutable_subgrid_u32 *top,
                                                     jxl_mutable_subgrid_u32 bottom);

typedef struct {
    jxl_mutable_subgrid_u32 *items;
    size_t count;
} jxl_mutable_subgrid_u32_list;

jxl_mutable_subgrid_u32_list jxl_mutable_subgrid_u32_into_groups(jxl_allocator_state *alloc,
                                                                 jxl_mutable_subgrid_u32 sg,
                                                                 size_t group_width,
                                                                 size_t group_height);

jxl_mutable_subgrid_u32_list jxl_mutable_subgrid_u32_into_groups_fixed(
    jxl_allocator_state *alloc, jxl_mutable_subgrid_u32 sg, size_t group_width,
    size_t group_height, size_t num_cols, size_t num_rows);

void jxl_mutable_subgrid_u32_list_destroy(jxl_allocator_state *alloc,
                                          jxl_mutable_subgrid_u32_list *list);

#endif /* JXL_GRID_MUTABLE_SUBGRID_H_ */
