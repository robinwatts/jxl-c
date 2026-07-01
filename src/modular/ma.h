// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_MA_H_
#define JXL_MODULAR_MA_H_

#include "allocator.h"
#include "coding/decoder.h"
#include "grid/alloc_tracker.h"
#include "modular/error.h"
#include "modular/ma_tree.h"
#include "modular/predictor.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_ma_tree_node jxl_ma_tree_node;

typedef struct {
    uint8_t cluster;
    jxl_predictor predictor;
    int32_t offset;
    uint32_t multiplier;
} jxl_ma_tree_leaf;

typedef struct jxl_ma_config {
    size_t num_tree_nodes;
    size_t tree_depth;
    jxl_ma_tree_node *tree;
    jxl_coding_decoder *decoder;
    jxl_allocator_state *alloc;
} jxl_ma_config;

typedef struct {
    jxl_grid_alloc_tracker *tracker;
    size_t node_limit;
    size_t depth_limit;
} jxl_ma_config_params;

void jxl_ma_config_init(jxl_ma_config *cfg);
void jxl_ma_config_destroy(jxl_allocator_state *alloc, jxl_ma_config *cfg);

jxl_modular_status_t jxl_ma_config_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                         const jxl_ma_config_params *params, jxl_ma_config *out);

const jxl_coding_decoder *jxl_ma_config_decoder(const jxl_ma_config *cfg);

const jxl_ma_tree_node *jxl_ma_config_tree_root(const jxl_ma_config *cfg);

#endif /* JXL_MODULAR_MA_H_ */
