// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_MA_FLAT_H_
#define JXL_MODULAR_MA_FLAT_H_

#include "modular/ma.h"
#include "context.h"
#include "modular/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint8_t cluster;
    jxl_predictor predictor;
    int32_t offset;
    uint32_t multiplier;
} jxl_ma_tree_leaf_clustered;

typedef enum {
    JXL_MA_FLAT_FUSED = 0,
    JXL_MA_FLAT_TABLE = 1,
    JXL_MA_FLAT_LEAF = 2,
} jxl_ma_flat_kind;

typedef struct {
    jxl_ma_flat_kind kind;
    union {
        struct {
            uint32_t prop_level0;
            int32_t value_level0;
            uint32_t props_level1[2];
            int32_t values_level1[2];
            uint32_t index_base;
        } fused;
        struct {
            uint32_t prop;
            int32_t value_base;
            uint32_t *indices;
            size_t indices_len;
        } table;
        jxl_ma_tree_leaf_clustered leaf;
    } u;
} jxl_ma_flat_node;

typedef struct {
    jxl_ma_flat_node *nodes;
    size_t len;
    int need_self_correcting;
    size_t max_prev_channel_depth;
} jxl_ma_flat_tree;

void jxl_ma_flat_tree_init(jxl_ma_flat_tree *t);
void jxl_ma_flat_tree_free(jxl_allocator_state *alloc, jxl_ma_flat_tree *t);

jxl_modular_status_t jxl_ma_flat_tree_build(const jxl_ma_config *cfg, uint32_t channel,
                                            uint32_t stream_idx, uint32_t prev_channels,
                                            jxl_ma_flat_tree *out);

const jxl_ma_tree_leaf_clustered *jxl_ma_flat_tree_single_leaf(const jxl_ma_flat_tree *t);

int jxl_ma_flat_tree_is_fast_lossless_gradient(const jxl_ma_flat_tree *t);

typedef struct jxl_modular_properties jxl_modular_properties;

int jxl_ma_flat_tree_simple_table(const jxl_ma_flat_tree *t, uint32_t *decision_prop_out,
                                  int32_t *value_base_out, jxl_ma_tree_leaf_clustered *leaf_out,
                                  uint8_t *cluster_table_out, size_t cluster_table_cap,
                                  size_t *cluster_table_len_out);

#include "ma_flat_inline.h"

#endif /* JXL_MODULAR_MA_FLAT_H_ */
