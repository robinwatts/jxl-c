// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_TRANSFORMED_GRID_H_
#define JXL_MODULAR_TRANSFORMED_GRID_H_

#include "allocator.h"
#include "modular/error.h"
#include "modular/image.h"

#include <stddef.h>

typedef enum {
    JXL_TRANSFORMED_GRID_SINGLE = 0,
    JXL_TRANSFORMED_GRID_MERGED = 1,
} jxl_transformed_grid_kind;

/* Rust TransformedGrid — Single(grid) or Merged { leader, members }.
 * Leader grid carries jxl_modular_sample_kind; views inherit kind via struct copy. */
typedef struct jxl_transformed_grid {
    jxl_transformed_grid_kind kind;
    jxl_modular_grid grid;
    jxl_transformed_grid *members;
    size_t members_len;
} jxl_transformed_grid;

void jxl_transformed_grid_init_empty(jxl_transformed_grid *tg);
void jxl_transformed_grid_set_single(jxl_allocator_state *alloc, jxl_transformed_grid *tg,
                                   jxl_modular_grid grid);
void jxl_transformed_grid_teardown(jxl_allocator_state *alloc, jxl_transformed_grid *tg);

jxl_modular_grid *jxl_transformed_grid_leader(jxl_transformed_grid *tg);
const jxl_modular_grid *jxl_transformed_grid_leader_const(const jxl_transformed_grid *tg);

jxl_modular_status_t jxl_transformed_grid_merge(jxl_allocator_state *alloc,
                                              jxl_transformed_grid *tg,
                                              jxl_transformed_grid *members, size_t member_count);
jxl_modular_status_t jxl_transformed_grid_unmerge(jxl_allocator_state *alloc,
                                                jxl_transformed_grid *tg, size_t count,
                                                  jxl_transformed_grid **out_members);

void jxl_transformed_grids_teardown(jxl_allocator_state *alloc, jxl_transformed_grid *grids,
                                  size_t len);
jxl_modular_status_t jxl_transformed_grids_resize(jxl_allocator_state *alloc,
                                                jxl_transformed_grid **grids, size_t *len,
                                                size_t new_len);
jxl_modular_status_t jxl_transformed_grids_insert_at(jxl_allocator_state *alloc,
                                                     jxl_transformed_grid **grids, size_t *len,
                                                     size_t at, const jxl_transformed_grid *insert,
                                                     size_t count);
void jxl_transformed_grids_remove_range(jxl_allocator_state *alloc, jxl_transformed_grid *grids,
                                      size_t *len, size_t from,
                                      size_t count);

#endif /* JXL_MODULAR_TRANSFORMED_GRID_H_ */
