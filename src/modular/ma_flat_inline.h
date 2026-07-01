// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_MA_FLAT_INLINE_H_
#define JXL_MODULAR_MA_FLAT_INLINE_H_

#include "ma_flat.h"
#include "modular/predictor_state.h"

JXL_ALWAYS_INLINE int32_t jxl_ma_tree_prop(jxl_modular_properties *props, uint32_t property) {
    return jxl_modular_property_from_state(props->state, props->is_edge, property, props);
}

#define JXL_MA_FLAT_GET_LEAF(t, props, leaf_out)                                              \
    do {                                                                                       \
        size_t _idx = 0;                                                                       \
        const jxl_ma_flat_tree *_t = (t);                                                       \
        jxl_modular_properties *_p = (props);                                                    \
        (leaf_out) = NULL;                                                                     \
        if (_t != NULL && _t->len > 0 && _p != NULL) {                                         \
            for (;;) {                                                                         \
                int32_t _p0v;                                                                  \
                int32_t _plv;                                                                  \
                int32_t _prv;                                                                  \
                int _high_bit;                                                                 \
                uint32_t _l;                                                                   \
                uint32_t _r;                                                                   \
                int32_t _v;                                                                    \
                int32_t _delta;                                                                \
                size_t _max_idx;                                                               \
                const jxl_ma_flat_node *_node;                                                   \
                if (_idx >= _t->len) {                                                         \
                    break;                                                                     \
                }                                                                              \
                _node = &_t->nodes[_idx];                                                      \
                if (_node->kind == JXL_MA_FLAT_LEAF) {                                         \
                    (leaf_out) = &_node->u.leaf;                                               \
                    break;                                                                     \
                }                                                                              \
                if (_node->kind == JXL_MA_FLAT_FUSED) {                                        \
                    _p0v = jxl_ma_tree_prop(_p, _node->u.fused.prop_level0);                   \
                    _plv = jxl_ma_tree_prop(_p, _node->u.fused.props_level1[0]);               \
                    _prv = jxl_ma_tree_prop(_p, _node->u.fused.props_level1[1]);             \
                    _high_bit = _p0v <= _node->u.fused.value_level0;                           \
                    _l = _plv <= _node->u.fused.values_level1[0] ? 1u : 0u;                    \
                    _r = 2u | (_prv <= _node->u.fused.values_level1[1] ? 1u : 0u);            \
                    _idx = (size_t)(_node->u.fused.index_base + (_high_bit ? _r : _l));       \
                    continue;                                                                  \
                }                                                                              \
                if (_node->kind == JXL_MA_FLAT_TABLE) {                                        \
                    _v = jxl_ma_tree_prop(_p, _node->u.table.prop);                            \
                    _delta = _v - _node->u.table.value_base;                                   \
                    if (_delta < 0) {                                                          \
                        _delta = 0;                                                            \
                    }                                                                          \
                    _max_idx =                                                                 \
                        _node->u.table.indices_len > 0 ? _node->u.table.indices_len - 1 : 0;   \
                    if ((size_t)_delta > _max_idx) {                                           \
                        _delta = (int32_t)_max_idx;                                            \
                    }                                                                          \
                    _idx = (size_t)_node->u.table.indices[(size_t)_delta];                     \
                    continue;                                                                  \
                }                                                                              \
                break;                                                                         \
            }                                                                                  \
        }                                                                                      \
    } while (0)

JXL_ALWAYS_INLINE const jxl_ma_tree_leaf_clustered *
jxl_ma_flat_tree_get_leaf(const jxl_ma_flat_tree *t, jxl_context *ctx, jxl_modular_properties *props) {
    const jxl_ma_tree_leaf_clustered *leaf = NULL;
    (void)ctx;
    JXL_MA_FLAT_GET_LEAF(t, props, leaf);
    return leaf;
}

#endif /* JXL_MODULAR_MA_FLAT_INLINE_H_ */
