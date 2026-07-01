// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_MA_TREE_H_
#define JXL_MODULAR_MA_TREE_H_

#include "modular/predictor.h"

#include "jxl_oxide/jxl_types.h"

typedef struct jxl_ma_tree_node jxl_ma_tree_node;

struct jxl_ma_tree_node {
    int is_leaf;
    union {
        struct {
            uint32_t property;
            int32_t value;
            jxl_ma_tree_node *left;
            jxl_ma_tree_node *right;
        } decision;
        struct {
            uint8_t cluster;
            jxl_predictor predictor;
            int32_t offset;
            uint32_t multiplier;
        } leaf;
    } u;
};

#endif /* JXL_MODULAR_MA_TREE_H_ */
