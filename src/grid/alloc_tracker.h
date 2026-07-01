// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_GRID_ALLOC_TRACKER_H_
#define JXL_GRID_ALLOC_TRACKER_H_

#include "allocator.h"
#include "grid/error.h"

#include <stddef.h>

typedef struct jxl_grid_alloc_tracker jxl_grid_alloc_tracker;
typedef struct jxl_grid_alloc_handle jxl_grid_alloc_handle;

jxl_grid_alloc_tracker *jxl_grid_alloc_tracker_create(jxl_allocator_state *alloc, size_t bytes_limit);
void jxl_grid_alloc_tracker_destroy(jxl_grid_alloc_tracker *tracker);

int jxl_grid_alloc_tracker_alloc(jxl_grid_alloc_tracker *tracker, size_t bytes,
                                 jxl_grid_alloc_handle **out);
void jxl_grid_alloc_handle_release(jxl_grid_alloc_handle *handle);

void jxl_grid_alloc_tracker_expand(jxl_grid_alloc_tracker *tracker, size_t by_bytes);
int jxl_grid_alloc_tracker_shrink(jxl_grid_alloc_tracker *tracker, size_t by_bytes);

jxl_grid_alloc_tracker *jxl_grid_alloc_handle_tracker(const jxl_grid_alloc_handle *handle);

#endif /* JXL_GRID_ALLOC_TRACKER_H_ */
