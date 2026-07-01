// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/ma_flat.h"
#include "modular/predictor_state.h"

#include "allocator.h"

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void test_flat_get_leaf_gradient(void) {
    jxl_ma_flat_tree flat;
    jxl_modular_predictor_state st;
    jxl_modular_properties props;
    jxl_ma_flat_node compound_tmp;
    jxl_ma_flat_tree_init(&flat);
    flat.len = 2;
    flat.nodes = calloc(2, sizeof(*flat.nodes));
    assert(flat.nodes != NULL);
    compound_tmp.kind = JXL_MA_FLAT_LEAF;
    compound_tmp.u.leaf.cluster = 0;
    compound_tmp.u.leaf.predictor = JXL_PREDICTOR_GRADIENT;
    compound_tmp.u.leaf.offset = 0;
    compound_tmp.u.leaf.multiplier = 1;
    flat.nodes[0] = compound_tmp;


    jxl_modular_predictor_state_init(&st);
    jxl_modular_predictor_state_reset(test_alloc(), &st, 4, NULL, 0, NULL);

    jxl_modular_properties_edge(&st, &props);
    const jxl_ma_tree_leaf_clustered *leaf = jxl_ma_flat_tree_get_leaf(&flat, NULL, &props);
    assert(leaf != NULL);
    assert(leaf->predictor == JXL_PREDICTOR_GRADIENT);

    jxl_ma_flat_tree_free(test_alloc(), &flat);
    jxl_modular_predictor_state_free(test_alloc(), &st);
}

int main(void) {
    test_flat_get_leaf_gradient();
    printf("test_modular_decode: ok\n");
    return 0;
}
