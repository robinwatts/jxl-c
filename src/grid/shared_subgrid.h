// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_GRID_SHARED_SUBGRID_H_
#define JXL_GRID_SHARED_SUBGRID_H_

#include "grid/aligned_grid.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    const uint32_t *ptr;
    size_t width;
    size_t height;
    size_t stride;
} jxl_shared_subgrid_u32;

/* Returns 0 on invalid parameters (Rust panics). */
int jxl_shared_subgrid_u32_from_buf(const uint32_t *buf, size_t width, size_t height, size_t stride,
                                    jxl_shared_subgrid_u32 *out);

jxl_shared_subgrid_u32 jxl_shared_subgrid_u32_from_grid(const jxl_grid_u32 *grid);

size_t jxl_shared_subgrid_u32_width(jxl_shared_subgrid_u32 sg);
size_t jxl_shared_subgrid_u32_height(jxl_shared_subgrid_u32 sg);

int jxl_shared_subgrid_u32_try_get(jxl_shared_subgrid_u32 sg, size_t x, size_t y, uint32_t *out);
const uint32_t *jxl_shared_subgrid_u32_row(jxl_shared_subgrid_u32 sg, size_t row);

void jxl_shared_subgrid_u32_split_horizontal(jxl_shared_subgrid_u32 sg, size_t x,
                                             jxl_shared_subgrid_u32 *left,
                                             jxl_shared_subgrid_u32 *right);
void jxl_shared_subgrid_u32_split_vertical(jxl_shared_subgrid_u32 sg, size_t y,
                                           jxl_shared_subgrid_u32 *top,
                                           jxl_shared_subgrid_u32 *bottom);

/* Use SIZE_MAX for unbounded end. */
jxl_shared_subgrid_u32 jxl_shared_subgrid_u32_subgrid(jxl_shared_subgrid_u32 sg, size_t x0,
                                                      size_t x1, size_t y0, size_t y1);

#endif /* JXL_GRID_SHARED_SUBGRID_H_ */
