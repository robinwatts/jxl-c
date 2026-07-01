// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/image.h"
#include "vardct/hf_coeff.h"

#include "allocator.h"
#include "test_helpers.h"

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

#include <assert.h>
#include <stdio.h>

static void test_lf_quant_subgrid_i16(void) {
    jxl_modular_grid_i32 g;
    jxl_lf_quant_subgrid_u32 view;
    JXL_TEST_REQUIRE(jxl_modular_grid_i16_create(test_alloc(), 4, 2, NULL, &g));
    jxl_modular_grid_store_i32(&g, 0, 0, 42);
    jxl_modular_grid_store_i32(&g, 3, 1, 40000);

    view.data = (const int16_t *)g.buf + g.offset;
    view.kind = JXL_MODULAR_SAMPLE_I16;
    view.width = g.width;
    view.height = g.height;
    view.stride = g.stride;

    assert(jxl_lf_quant_subgrid_sample(&view, 0, 0) == 42);
    assert(jxl_lf_quant_subgrid_sample(&view, 3, 1) == (int32_t)(int16_t)40000);
    jxl_modular_grid_i32_destroy(test_alloc(), &g);
}

static void test_lf_quant_subgrid_i32(void) {
    jxl_modular_grid_i32 g;
    jxl_lf_quant_subgrid_u32 view;
    JXL_TEST_REQUIRE(jxl_modular_grid_i32_create(test_alloc(), 2, 1, NULL, &g));
    jxl_modular_grid_store_i32(&g, 0, 0, -5);
    jxl_modular_grid_store_i32(&g, 1, 0, 60000);

    view.data = (const int32_t *)g.buf + g.offset;
    view.kind = JXL_MODULAR_SAMPLE_I32;
    view.width = g.width;
    view.height = g.height;
    view.stride = g.width;

    assert(jxl_lf_quant_subgrid_sample(&view, 0, 0) == -5);
    assert(jxl_lf_quant_subgrid_sample(&view, 1, 0) == 60000);
    jxl_modular_grid_i32_destroy(test_alloc(), &g);
}

int main(void) {
    test_lf_quant_subgrid_i16();
    test_lf_quant_subgrid_i32();
    printf("test_modular_lf_quant: ok\n");
    return 0;
}
