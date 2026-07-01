// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_GRID_ALIGNED_GRID_H_
#define JXL_GRID_ALIGNED_GRID_H_

#include "grid/alloc_tracker.h"
#include "grid/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#define JXL_GRID_ALIGN 32

typedef struct jxl_grid_u32 {
    size_t width;
    size_t height;
    size_t offset; /* in elements */
    uint32_t *buf;
    size_t buf_len; /* capacity in elements */
    jxl_grid_alloc_handle *handle;
} jxl_grid_u32;

typedef struct jxl_grid_f32 {
    size_t width;
    size_t height;
    size_t offset;
    float *buf;
    size_t buf_len;
    jxl_grid_alloc_handle *handle;
} jxl_grid_f32;

void jxl_grid_u32_init_empty(jxl_grid_u32 *g);
int jxl_grid_u32_create(jxl_allocator_state *alloc, size_t width, size_t height,
                        jxl_grid_alloc_tracker *tracker, jxl_grid_u32 *out, jxl_grid_oom *oom);
void jxl_grid_u32_destroy(jxl_allocator_state *alloc, jxl_grid_u32 *g);

size_t jxl_grid_u32_width(const jxl_grid_u32 *g);
size_t jxl_grid_u32_height(const jxl_grid_u32 *g);
uint32_t *jxl_grid_u32_buf(jxl_grid_u32 *g);
const uint32_t *jxl_grid_u32_buf_const(const jxl_grid_u32 *g);
uint32_t jxl_grid_u32_get(const jxl_grid_u32 *g, size_t x, size_t y);
int jxl_grid_u32_try_get(const jxl_grid_u32 *g, size_t x, size_t y, uint32_t *out);
void jxl_grid_u32_set(jxl_grid_u32 *g, size_t x, size_t y, uint32_t v);
const uint32_t *jxl_grid_u32_row(const jxl_grid_u32 *g, size_t row);

void jxl_grid_f32_init_empty(jxl_grid_f32 *g);
int jxl_grid_f32_create(jxl_allocator_state *alloc, size_t width, size_t height,
                        jxl_grid_alloc_tracker *tracker, jxl_grid_f32 *out, jxl_grid_oom *oom);
void jxl_grid_f32_destroy(jxl_allocator_state *alloc, jxl_grid_f32 *g);

size_t jxl_grid_f32_width(const jxl_grid_f32 *g);
size_t jxl_grid_f32_height(const jxl_grid_f32 *g);
float *jxl_grid_f32_buf(jxl_grid_f32 *g);
const float *jxl_grid_f32_buf_const(const jxl_grid_f32 *g);

#endif /* JXL_GRID_ALIGNED_GRID_H_ */
