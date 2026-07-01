// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "grid/grid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) {
        jxl_allocator_init(&alloc, NULL);
        init = 1;
    }
    return &alloc;
}

static void test_shared_subgrid_slices(void) {
    jxl_grid_u32 grid;
    uint32_t dummy;
    jxl_allocator_state *alloc = test_alloc();
    if (!jxl_grid_u32_create(alloc, 128, 128, NULL, &grid, NULL)) {
        assert(0);
    }
    jxl_shared_subgrid_u32 shared = jxl_shared_subgrid_u32_from_grid(&grid);

    jxl_shared_subgrid_u32 top, bottom;
    jxl_shared_subgrid_u32_split_vertical(shared, 64, &top, &bottom);
    assert(jxl_shared_subgrid_u32_width(top) == 128);
    assert(jxl_shared_subgrid_u32_height(top) == 64);
    assert(jxl_shared_subgrid_u32_width(bottom) == 128);
    assert(jxl_shared_subgrid_u32_height(bottom) == 64);

    jxl_shared_subgrid_u32 tl, tr;
    jxl_shared_subgrid_u32_split_horizontal(top, 64, &tl, &tr);
    assert(jxl_shared_subgrid_u32_width(tl) == 64);
    assert(jxl_shared_subgrid_u32_height(tl) == 64);

    jxl_shared_subgrid_u32 tr2, empty_v;
    jxl_shared_subgrid_u32_split_vertical(tr, 64, &tr2, &empty_v);
    assert(jxl_shared_subgrid_u32_height(tr2) == 64);
    assert(jxl_shared_subgrid_u32_height(empty_v) == 0);
    dummy = 0;
    assert(!jxl_shared_subgrid_u32_try_get(empty_v, 0, 0, &dummy));

    jxl_shared_subgrid_u32 tr3, empty_h;
    jxl_shared_subgrid_u32_split_horizontal(tr2, 64, &tr3, &empty_h);
    assert(jxl_shared_subgrid_u32_width(tr3) == 64);
    assert(jxl_shared_subgrid_u32_width(empty_h) == 0);
    assert(!jxl_shared_subgrid_u32_try_get(empty_h, 0, 0, &dummy));

    jxl_grid_u32_destroy(alloc, &grid);
}

static void test_mutable_subgrid_slices(void) {
    jxl_grid_u32 grid;
    uint32_t dummy;
    jxl_allocator_state *alloc = test_alloc();
    if (!jxl_grid_u32_create(alloc, 128, 128, NULL, &grid, NULL)) {
        assert(0);
    }
    jxl_mutable_subgrid_u32 mutable = jxl_mutable_subgrid_u32_from_grid(&grid);

    jxl_mutable_subgrid_u32 top, bottom;
    jxl_mutable_subgrid_u32_split_vertical(mutable, 64, &top, &bottom);

    jxl_mutable_subgrid_u32 tl, tr;
    jxl_mutable_subgrid_u32_split_horizontal(top, 64, &tl, &tr);

    jxl_mutable_subgrid_u32 tr2, empty_v;
    jxl_mutable_subgrid_u32_split_vertical(tr, 64, &tr2, &empty_v);
    dummy = 0;
    assert(!jxl_mutable_subgrid_u32_try_get(empty_v, 0, 0, &dummy));

    jxl_mutable_subgrid_u32 tr3, empty_h;
    jxl_mutable_subgrid_u32_split_horizontal(tr2, 64, &tr3, &empty_h);
    assert(!jxl_mutable_subgrid_u32_try_get(empty_h, 0, 0, &dummy));

    jxl_mutable_subgrid_u32_set(tr3, 0, 0, 42);
    assert(jxl_grid_u32_get(&grid, 64, 0) == 42);

    jxl_grid_u32_destroy(alloc, &grid);
}

static void test_mutable_split_merge(void) {
    jxl_grid_u32 grid;
    jxl_mutable_subgrid_u32 top;
    jxl_mutable_subgrid_u32 tl;
    jxl_allocator_state *alloc = test_alloc();
    if (!jxl_grid_u32_create(alloc, 128, 128, NULL, &grid, NULL)) {
        assert(0);
    }
    jxl_mutable_subgrid_u32 mutable = jxl_mutable_subgrid_u32_from_grid(&grid);

    jxl_mutable_subgrid_u32 bottom = jxl_mutable_subgrid_u32_split_vertical_in_place(&mutable, 64);
    top = mutable;
    assert(jxl_mutable_subgrid_u32_height(top) == 64);
    assert(jxl_mutable_subgrid_u32_height(bottom) == 64);

    jxl_mutable_subgrid_u32 tr = jxl_mutable_subgrid_u32_split_horizontal_in_place(&top, 64);
    tl = top;

    jxl_mutable_subgrid_u32 empty0 = jxl_mutable_subgrid_u32_split_vertical_in_place(&tr, 64);
    jxl_mutable_subgrid_u32 empty1 = jxl_mutable_subgrid_u32_split_horizontal_in_place(&tr, 64);

    jxl_mutable_subgrid_u32_merge_horizontal_in_place(&tr, empty1);
    jxl_mutable_subgrid_u32_merge_vertical_in_place(&tr, empty0);
    jxl_mutable_subgrid_u32_merge_horizontal_in_place(&tl, tr);
    jxl_mutable_subgrid_u32_merge_vertical_in_place(&tl, bottom);

    assert(jxl_mutable_subgrid_u32_width(tl) == 128);
    assert(jxl_mutable_subgrid_u32_height(tl) == 128);

    jxl_grid_u32_destroy(alloc, &grid);
}

static void test_get_row_and_swap(void) {
    uint32_t buf_row[] = {1, 2, 3, 4, 5, 6};
    jxl_mutable_subgrid_u32 sub_row;
    uint32_t buf[] = {1, 2, 3, 4};
    jxl_mutable_subgrid_u32 sub;
    if (!jxl_mutable_subgrid_u32_from_buf(buf_row, 2, 2, 3, &sub_row)) {
        assert(0);
    }
    const uint32_t *row0 = jxl_mutable_subgrid_u32_row(sub_row, 0);
    assert(row0[0] == 1 && row0[1] == 2);
    uint32_t *row1 = jxl_mutable_subgrid_u32_row_mut(sub_row, 1);
    row1[0] = 7;
    row1[1] = 8;
    assert(buf_row[3] == 7 && buf_row[4] == 8);

    if (!jxl_mutable_subgrid_u32_from_buf(buf, 2, 2, 2, &sub)) {
        assert(0);
    }
    jxl_mutable_subgrid_u32_swap(sub, 0, 0, 1, 1);
    assert(buf[0] == 4 && buf[3] == 1);
}

static void test_into_groups(void) {
    size_t i;
    uint32_t buf[] = {1, 2, 3, 4};
    jxl_mutable_subgrid_u32 grid1;
    jxl_mutable_subgrid_u32 grid2;
    jxl_allocator_state *alloc = test_alloc();
    if (!jxl_mutable_subgrid_u32_from_buf(buf, 2, 2, 2, &grid1)) {
        assert(0);
    }
    jxl_mutable_subgrid_u32_list g1 = jxl_mutable_subgrid_u32_into_groups(alloc, grid1, 1, 1);
    assert(g1.count == 4);
    for (i = 0; i < g1.count; ++i) {
        assert(jxl_mutable_subgrid_u32_width(g1.items[i]) == 1);
        assert(jxl_mutable_subgrid_u32_height(g1.items[i]) == 1);
    }
    jxl_mutable_subgrid_u32_list_destroy(alloc, &g1);

    if (!jxl_mutable_subgrid_u32_from_buf(buf, 2, 2, 2, &grid2)) {
        assert(0);
    }
    jxl_mutable_subgrid_u32_list g2 = jxl_mutable_subgrid_u32_into_groups(alloc, grid2, 3, 3);
    assert(g2.count == 1);
    assert(jxl_mutable_subgrid_u32_width(g2.items[0]) == 2);
    assert(jxl_mutable_subgrid_u32_height(g2.items[0]) == 2);
    jxl_mutable_subgrid_u32_list_destroy(alloc, &g2);
}

static void test_into_groups_fixed(void) {
    uint32_t buf[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    jxl_mutable_subgrid_u32 grid;
    jxl_allocator_state *alloc = test_alloc();
    if (!jxl_mutable_subgrid_u32_from_buf(buf, 3, 3, 3, &grid)) {
        assert(0);
    }
    jxl_mutable_subgrid_u32_list groups =
        jxl_mutable_subgrid_u32_into_groups_fixed(alloc, grid, 2, 2, 2, 2);
    assert(groups.count == 4);
    assert(jxl_mutable_subgrid_u32_width(groups.items[0]) == 2);
    assert(jxl_mutable_subgrid_u32_height(groups.items[0]) == 2);
    assert(jxl_mutable_subgrid_u32_width(groups.items[1]) == 1);
    assert(jxl_mutable_subgrid_u32_height(groups.items[1]) == 2);
    jxl_mutable_subgrid_u32_list_destroy(alloc, &groups);
}

static void test_alloc_tracker(void) {
    jxl_allocator_state *alloc = test_alloc();
    jxl_grid_alloc_tracker *t = jxl_grid_alloc_tracker_create(alloc, 1000);
    if (t == NULL) {
        assert(0);
    }
    jxl_grid_alloc_handle *h = NULL;
    if (!jxl_grid_alloc_tracker_alloc(t, 100, &h) || h == NULL) {
        assert(0);
    }
    jxl_grid_alloc_handle_release(h);
    jxl_grid_alloc_tracker_destroy(t);
}

int main(void) {
    test_alloc_tracker();
    test_shared_subgrid_slices();
    test_mutable_subgrid_slices();
    test_mutable_split_merge();
    test_get_row_and_swap();
    test_into_groups();
    test_into_groups_fixed();
    printf("test_grid: ok\n");
    return 0;
}
