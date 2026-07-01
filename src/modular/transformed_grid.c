// SPDX-License-Identifier: MIT OR Apache-2.0
#include "transformed_grid.h"

#include "allocator.h"
#include <string.h>

void jxl_transformed_grid_init_empty(jxl_transformed_grid *tg) {
    if (tg != NULL) {
        memset(tg, 0, sizeof(*tg));
        jxl_modular_grid_i32_init_empty(&tg->grid);
        tg->kind = JXL_TRANSFORMED_GRID_SINGLE;
    }
}

void jxl_transformed_grid_set_single(jxl_allocator_state *alloc, jxl_transformed_grid *tg, jxl_modular_grid grid) {
    if (tg == NULL) {
        return;
    }
    jxl_transformed_grid_teardown(alloc, tg);
    jxl_modular_grid_normalize_stride(&grid);
    tg->kind = JXL_TRANSFORMED_GRID_SINGLE;
    tg->grid = grid;
}

void jxl_transformed_grid_teardown(jxl_allocator_state *alloc, jxl_transformed_grid *tg) {
    if (tg == NULL) {
        return;
    }
    jxl_free(alloc, tg->members);
    tg->members = NULL;
    tg->members_len = 0;
    tg->kind = JXL_TRANSFORMED_GRID_SINGLE;
}

jxl_modular_grid *jxl_transformed_grid_leader(jxl_transformed_grid *tg) {
    return tg != NULL ? &tg->grid : NULL;
}

const jxl_modular_grid *jxl_transformed_grid_leader_const(const jxl_transformed_grid *tg) {
    return tg != NULL ? &tg->grid : NULL;
}

jxl_modular_status_t jxl_transformed_grid_merge(jxl_allocator_state *alloc,
                                              jxl_transformed_grid *tg,
                                              jxl_transformed_grid *members, size_t member_count) {
    size_t new_len;
    jxl_transformed_grid *grown;
    if (tg == NULL || (member_count > 0 && members == NULL)) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    if (member_count == 0) {
        return JXL_MODULAR_OK;
    }
    if (tg->kind == JXL_TRANSFORMED_GRID_SINGLE) {
        tg->kind = JXL_TRANSFORMED_GRID_MERGED;
        tg->members = members;
        tg->members_len = member_count;
        return JXL_MODULAR_OK;
    }
    new_len = tg->members_len + member_count;
    grown = jxl_realloc(alloc, tg->members, new_len * sizeof(*tg->members));
    if (grown == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    memcpy(grown + tg->members_len, members, member_count * sizeof(*members));
    jxl_free(alloc, members);
    tg->members = grown;
    tg->members_len = new_len;
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_transformed_grid_unmerge(jxl_allocator_state *alloc,
                                                jxl_transformed_grid *tg, size_t count,
                                                  jxl_transformed_grid **out_members) {
    size_t start;
    if (tg == NULL || out_members == NULL || count == 0) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    if (tg->kind != JXL_TRANSFORMED_GRID_MERGED || count > tg->members_len) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    *out_members = jxl_calloc(alloc, count, sizeof(**out_members));
    if (*out_members == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    start = tg->members_len - count;
    memcpy(*out_members, tg->members + start, count * sizeof(**out_members));
    tg->members_len = start;
    if (tg->members_len == 0) {
        jxl_free(alloc, tg->members);
        tg->members = NULL;
        tg->kind = JXL_TRANSFORMED_GRID_SINGLE;
    }
    return JXL_MODULAR_OK;
}

void jxl_transformed_grids_teardown(jxl_allocator_state *alloc, jxl_transformed_grid *grids,
                                    size_t len) {
    size_t i;
    if (grids == NULL) {
        return;
    }
    for (i = 0; i < len; ++i) {
        jxl_transformed_grid_teardown(alloc, &grids[i]);
    }
}

jxl_modular_status_t jxl_transformed_grids_resize(jxl_allocator_state *alloc,
                                                jxl_transformed_grid **grids, size_t *len,
                                                size_t new_len) {
    jxl_transformed_grid *grown;
    if (grids == NULL || len == NULL || new_len == *len) {
        return JXL_MODULAR_OK;
    }
    grown = jxl_realloc(alloc, *grids, new_len * sizeof(**grids));
    if (grown == NULL && new_len > 0) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    if (new_len > *len) {
        size_t i;
        for (i = *len; i < new_len; ++i) {
            jxl_transformed_grid_init_empty(&grown[i]);
        }
    }
    *grids = grown;
    *len = new_len;
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_transformed_grids_insert_at(jxl_allocator_state *alloc,
                                                     jxl_transformed_grid **grids, size_t *len,
                                                     size_t at, const jxl_transformed_grid *insert,
                                                     size_t count) {
                                                         size_t i;
    size_t old_len;
    jxl_modular_status_t st;
    if (count == 0) {
        return JXL_MODULAR_OK;
    }
    if (grids == NULL || len == NULL || insert == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    old_len = *len;
    st = jxl_transformed_grids_resize(alloc, grids, len, old_len + count);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    if (at < old_len) {
        memmove(&(*grids)[at + count], &(*grids)[at], (old_len - at) * sizeof(**grids));
    }
    for (i = 0; i < count; ++i) {
        (*grids)[at + i] = insert[i];
    }
    return JXL_MODULAR_OK;
}

void jxl_transformed_grids_remove_range(jxl_allocator_state *alloc, jxl_transformed_grid *grids,
                                      size_t *len, size_t from,
                                      size_t count) {
                                          size_t i;
    size_t tail;
    if (grids == NULL || len == NULL || count == 0 || from >= *len) {
        return;
    }
    if (from + count > *len) {
        count = *len - from;
    }
    for (i = from; i < from + count; ++i) {
        jxl_transformed_grid_teardown(alloc, &grids[i]);
        jxl_transformed_grid_init_empty(&grids[i]);
    }
    tail = *len - from - count;
    if (tail > 0) {
        memmove(&grids[from], &grids[from + count], tail * sizeof(*grids));
    }
    *len -= count;
}
