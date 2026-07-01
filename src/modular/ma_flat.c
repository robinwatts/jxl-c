// SPDX-License-Identifier: MIT OR Apache-2.0
#include "ma_flat.h"

#include "modular/ma_tree.h"
#include "modular/predictor_state.h"

#include "allocator.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

void jxl_ma_flat_tree_init(jxl_ma_flat_tree *t) {
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

void jxl_ma_flat_tree_free(jxl_allocator_state *alloc, jxl_ma_flat_tree *t) {
    size_t i;
    if (t == NULL) {
        return;
    }
    for (i = 0; i < t->len; ++i) {
        if (t->nodes[i].kind == JXL_MA_FLAT_TABLE) {
            jxl_free(alloc, t->nodes[i].u.table.indices);
        }
    }
    jxl_free(alloc, t->nodes);
    jxl_ma_flat_tree_init(t);
}

static const jxl_ma_tree_node *next_decision_node(const jxl_ma_tree_node *node, uint32_t channel,
                                                  uint32_t stream_idx, uint32_t prev_channels) {
    while (node != NULL && !node->is_leaf) {
        uint32_t property = node->u.decision.property;
        int32_t value = node->u.decision.value;
        if (property == 0 || property == 1) {
            uint32_t target = property == 0 ? channel : stream_idx;
            node = ((int32_t)target > value) ? node->u.decision.left : node->u.decision.right;
            continue;
        }
        if (property >= 16) {
            uint32_t prev_channel_idx = (property - 16) / 4;
            if (prev_channel_idx >= prev_channels) {
                node = (value < 0) ? node->u.decision.left : node->u.decision.right;
                continue;
            }
        }
        break;
    }
    return node;
}

static int flat_push(jxl_allocator_state *alloc, jxl_ma_flat_tree *out,
                     const jxl_ma_flat_node *node) {
    jxl_ma_flat_node *grown = jxl_realloc(alloc, out->nodes, (out->len + 1) * sizeof(*grown));
    if (grown == NULL) {
        return 0;
    }
    out->nodes = grown;
    out->nodes[out->len++] = *node;
    return 1;
}

typedef struct {
    const jxl_ma_tree_node *node;
    int32_t range_start;
    int32_t range_end;
} ma_compile_stack_entry;

typedef struct {
    const jxl_ma_tree_node *node;
    int32_t range_end;
} ma_range_node_entry;

typedef struct {
    int ok;
    jxl_ma_flat_node table_node;
    const jxl_ma_tree_node **child_nodes;
    size_t child_len;
} ma_table_compile_result;

static int32_t i32_abs_diff(int32_t a, int32_t b) {
    if (a < b) {
        return b - a;
    }
    return a - b;
}

static int range_end_cmp(const void *a, const void *b) {
    const ma_range_node_entry *ea = a;
    const ma_range_node_entry *eb = b;
    if (ea->range_end < eb->range_end) {
        return -1;
    }
    if (ea->range_end > eb->range_end) {
        return 1;
    }
    return 0;
}

static int ma_stack_push(jxl_allocator_state *alloc, ma_compile_stack_entry **stack, size_t *stack_len,
                         size_t *stack_cap, const jxl_ma_tree_node *node, int32_t range_start,
                         int32_t range_end) {
    size_t new_cap;
    ma_compile_stack_entry *grown;

    if (*stack_len + 1 > *stack_cap) {
        new_cap = *stack_cap ? *stack_cap * 2 : 8;
        grown = jxl_realloc(alloc, *stack, new_cap * sizeof(**stack));
        if (grown == NULL) {
            return 0;
        }
        *stack = grown;
        *stack_cap = new_cap;
    }
    (*stack)[*stack_len].node = node;
    (*stack)[*stack_len].range_start = range_start;
    (*stack)[*stack_len].range_end = range_end;
    *stack_len += 1;
    return 1;
}

static ma_table_compile_result try_compile_to_table(jxl_allocator_state *alloc,
                                                    const jxl_ma_tree_node *target,
                                                    uint32_t channel, uint32_t stream_idx,
                                                    uint32_t prev_channels,
                                                    uint32_t next_index_base) {
    size_t idx;
    size_t stack_len;
    size_t stack_cap;
    size_t range_len;
    size_t range_cap;
    int32_t lower_bound;
    int32_t upper_bound;
    int32_t range_start;
    size_t next_index;
    size_t out_node_count;
    uint32_t property;
    int32_t value;
    size_t index_count;
    ma_table_compile_result result = {0};
    const jxl_ma_tree_node *left;
    const jxl_ma_tree_node *right;
    ma_compile_stack_entry *stack;
    ma_range_node_entry *range_nodes;
    uint32_t *indices;
    const jxl_ma_tree_node **nodes;

    if (target == NULL || target->is_leaf) {
        return result;
    }

    property = target->u.decision.property;
    value = target->u.decision.value;
    left = target->u.decision.left;
    right = target->u.decision.right;

    stack = NULL;
    stack_len = 0;
    stack_cap = 0;
    range_nodes = NULL;
    range_len = 0;
    range_cap = 0;

    if (!ma_stack_push(alloc, &stack, &stack_len, &stack_cap, left, value + 1, INT32_MAX)) {
        goto compile_fail;
    }
    if (!ma_stack_push(alloc, &stack, &stack_len, &stack_cap, right, INT32_MIN, value)) {
        goto compile_fail;
    }

    lower_bound = value;
    upper_bound = value;

    while (stack_len > 0) {
        int32_t node_value;
        int32_t new_lower;
        int32_t new_upper;
        size_t grow_cap;
        ma_compile_stack_entry entry;
        const jxl_ma_tree_node *node;
        ma_range_node_entry *grown;

        entry = stack[--stack_len];
        node = next_decision_node(entry.node, channel, stream_idx, prev_channels);
        if (node == NULL) {
            continue;
        }

        if (!node->is_leaf && node->u.decision.property == property) {
            node_value = node->u.decision.value;
            new_lower = lower_bound < node_value ? lower_bound : node_value;
            new_upper = upper_bound > node_value ? upper_bound : node_value;
            if ((uint32_t)i32_abs_diff(new_upper, new_lower) > 1024u - 2u) {
                if (range_len + 1 > range_cap) {
                    grow_cap = range_cap ? range_cap * 2 : 8;
                    grown = jxl_realloc(alloc, range_nodes, grow_cap * sizeof(*range_nodes));
                    if (grown == NULL) {
                        goto compile_fail;
                    }
                    range_nodes = grown;
                    range_cap = grow_cap;
                }
                range_nodes[range_len].node = node;
                range_nodes[range_len].range_end = entry.range_end;
                range_len += 1;
                continue;
            }
            lower_bound = new_lower;
            upper_bound = new_upper;

            if (node_value < entry.range_end) {
                if (!ma_stack_push(alloc, &stack, &stack_len, &stack_cap, node->u.decision.left,
                                   node_value + 1, entry.range_end)) {
                    goto compile_fail;
                }
            }
            if (entry.range_start <= node_value) {
                if (!ma_stack_push(alloc, &stack, &stack_len, &stack_cap, node->u.decision.right,
                                   entry.range_start, node_value)) {
                    goto compile_fail;
                }
            }
            continue;
        }

        if (range_len + 1 > range_cap) {
            grow_cap = range_cap ? range_cap * 2 : 8;
            grown = jxl_realloc(alloc, range_nodes, grow_cap * sizeof(*range_nodes));
            if (grown == NULL) {
                goto compile_fail;
            }
            range_nodes = grown;
            range_cap = grow_cap;
        }
        range_nodes[range_len].node = node;
        range_nodes[range_len].range_end = entry.range_end;
        range_len += 1;
    }

    if (range_len < 4) {
        goto compile_fail;
    }

    qsort(range_nodes, range_len, sizeof(*range_nodes), range_end_cmp);

    index_count = (size_t)i32_abs_diff(upper_bound, lower_bound) + 2;
    indices = jxl_calloc(alloc, index_count, sizeof(*indices));
    nodes = jxl_alloc(alloc, range_len * sizeof(*nodes));
    if (indices == NULL || nodes == NULL) {
        jxl_free(alloc, indices);
        jxl_free_const(alloc, nodes);
        goto compile_fail;
    }

    range_start = lower_bound - 1;
    next_index = 0;
    out_node_count = 0;
    for (idx = 0; idx < range_len; ++idx) {
        size_t i;
        int32_t range_end;
        size_t len;
        size_t end_index;

        range_end = range_nodes[idx].range_end;
        if (range_end == INT32_MAX) {
            indices[index_count - 1] = next_index_base + (uint32_t)idx;
            nodes[out_node_count++] = range_nodes[idx].node;
            break;
        }
        len = (size_t)i32_abs_diff(range_end, range_start);
        end_index = next_index + len;
        if (end_index > index_count) {
            end_index = index_count;
        }
        for (i = next_index; i < end_index; ++i) {
            indices[i] = next_index_base + (uint32_t)idx;
        }
        nodes[out_node_count++] = range_nodes[idx].node;
        next_index = end_index;
        range_start = range_end;
    }

    jxl_free(alloc, stack);
    jxl_free(alloc, range_nodes);

    result.ok = 1;
    result.table_node.kind = JXL_MA_FLAT_TABLE;
    result.table_node.u.table.prop = property;
    result.table_node.u.table.value_base = lower_bound;
    result.table_node.u.table.indices = indices;
    result.table_node.u.table.indices_len = index_count;
    result.child_nodes = nodes;
    result.child_len = out_node_count;
    return result;

compile_fail:
    jxl_free(alloc, stack);
    jxl_free(alloc, range_nodes);
    return result;
}

static void update_meta(jxl_ma_flat_tree *t, const jxl_ma_flat_node *node) {
    if (node->kind == JXL_MA_FLAT_FUSED) {
        uint32_t p = node->u.fused.prop_level0;
        uint32_t pl = node->u.fused.props_level1[0];
        uint32_t pr = node->u.fused.props_level1[1];
        if (p == 15 || pl == 15 || pr == 15) {
            t->need_self_correcting = 1;
        }
        if (p >= 16) {
            size_t d = (size_t)((p - 16) / 4) + 1;
            if (d > t->max_prev_channel_depth) {
                t->max_prev_channel_depth = d;
            }
        }
        if (pl >= 16) {
            size_t d = (size_t)((pl - 16) / 4) + 1;
            if (d > t->max_prev_channel_depth) {
                t->max_prev_channel_depth = d;
            }
        }
        if (pr >= 16) {
            size_t d = (size_t)((pr - 16) / 4) + 1;
            if (d > t->max_prev_channel_depth) {
                t->max_prev_channel_depth = d;
            }
        }
    } else if (node->kind == JXL_MA_FLAT_TABLE) {
        if (node->u.table.prop == 15) {
            t->need_self_correcting = 1;
        }
        if (node->u.table.prop >= 16) {
            size_t d = (size_t)((node->u.table.prop - 16) / 4) + 1;
            if (d > t->max_prev_channel_depth) {
                t->max_prev_channel_depth = d;
            }
        }
    } else if (node->kind == JXL_MA_FLAT_LEAF &&
               node->u.leaf.predictor == JXL_PREDICTOR_SELF_CORRECTING) {
        t->need_self_correcting = 1;
    }
}

static int ma_queue_push(jxl_allocator_state *alloc, const jxl_ma_tree_node ***queue, size_t *queue_len,
                         size_t *queue_cap, const jxl_ma_tree_node *node) {
    size_t new_cap;
    const jxl_ma_tree_node **grown;

    if (*queue_len + 1 > *queue_cap) {
        new_cap = *queue_cap ? *queue_cap * 2 : 8;
        grown = jxl_realloc_const(alloc, *queue, new_cap * sizeof(**queue));
        if (grown == NULL) {
            return 0;
        }
        *queue = grown;
        *queue_cap = new_cap;
    }
    (*queue)[*queue_len] = node;
    *queue_len += 1;
    return 1;
}

jxl_modular_status_t jxl_ma_flat_tree_build(const jxl_ma_config *cfg, uint32_t channel,
                                            uint32_t stream_idx, uint32_t prev_channels,
                                            jxl_ma_flat_tree *out) {
    size_t queue_len;
    size_t queue_cap;
    uint32_t next_base;
    jxl_allocator_state *alloc;
    const jxl_ma_tree_node **queue;
    const jxl_ma_tree_node *start;

    if (cfg == NULL || out == NULL || cfg->tree == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    alloc = cfg->alloc;
    jxl_ma_flat_tree_free(alloc, out);

    queue = NULL;
    queue_len = 0;
    queue_cap = 0;

    start = next_decision_node(cfg->tree, channel, stream_idx, prev_channels);
    if (start == NULL) {
        return JXL_MODULAR_INVALID_MA_TREE;
    }

    queue = jxl_alloc(alloc, sizeof(*queue));
    if (queue == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    queue[0] = start;
    queue_len = 1;
    queue_cap = 1;

    next_base = 1;

    while (queue_len > 0) {
        size_t i;
        uint32_t lp;
        int32_t lv;
        uint32_t rp;
        int32_t rv;
        jxl_ma_flat_node n;
        uint32_t property;
        int32_t value;
        ma_table_compile_result compiled;
        const jxl_ma_tree_node *target;
        const jxl_ma_tree_node *left;
        const jxl_ma_tree_node *right;
        const jxl_ma_tree_node *ll;
        const jxl_ma_tree_node *lr;
        const jxl_ma_tree_node *rl;
        const jxl_ma_tree_node *rr;
        const jxl_ma_tree_node *push_nodes[4];

        target = queue[0];
        {
            union { const jxl_ma_tree_node **cp; jxl_ma_tree_node **p; } u;
            u.cp = queue;
            memmove(u.p, u.p + 1, (queue_len - 1) * sizeof(*queue));
        }
        --queue_len;

        target = next_decision_node(target, channel, stream_idx, prev_channels);
        if (target == NULL) {
            continue;
        }

        compiled = try_compile_to_table(alloc, target, channel, stream_idx, prev_channels, next_base);
        if (compiled.ok) {
            if (!flat_push(alloc, out, &compiled.table_node)) {
                jxl_free(alloc, compiled.table_node.u.table.indices);
                jxl_free_const(alloc, compiled.child_nodes);
                jxl_free_const(alloc, queue);
                jxl_ma_flat_tree_free(alloc, out);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
            update_meta(out, &compiled.table_node);
            for (i = 0; i < compiled.child_len; ++i) {
                if (!ma_queue_push(alloc, &queue, &queue_len, &queue_cap, compiled.child_nodes[i])) {
                    jxl_free_const(alloc, compiled.child_nodes);
                    jxl_free_const(alloc, queue);
                    jxl_ma_flat_tree_free(alloc, out);
                    return JXL_MODULAR_OUT_OF_MEMORY;
                }
            }
            next_base += (uint32_t)compiled.child_len;
            jxl_free_const(alloc, compiled.child_nodes);
            continue;
        }

        if (target->is_leaf) {
            n.kind = JXL_MA_FLAT_LEAF;
            n.u.leaf.cluster = target->u.leaf.cluster;
            n.u.leaf.predictor = target->u.leaf.predictor;
            n.u.leaf.offset = target->u.leaf.offset;
            n.u.leaf.multiplier = target->u.leaf.multiplier;
            if (!flat_push(alloc, out, &n)) {
                jxl_free_const(alloc, queue);
                jxl_ma_flat_tree_free(alloc, out);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
            update_meta(out, &n);
            continue;
        }

        property = target->u.decision.property;
        value = target->u.decision.value;
        left = target->u.decision.left;
        right = target->u.decision.right;

        left = next_decision_node(left, channel, stream_idx, prev_channels);
        lp = 0;
        lv = 0;
        ll = left;
        lr = left;
        if (left != NULL && !left->is_leaf) {
            lp = left->u.decision.property;
            lv = left->u.decision.value;
            ll = left->u.decision.left;
            lr = left->u.decision.right;
        }

        right = next_decision_node(right, channel, stream_idx, prev_channels);
        rp = 0;
        rv = 0;
        rl = right;
        rr = right;
        if (right != NULL && !right->is_leaf) {
            rp = right->u.decision.property;
            rv = right->u.decision.value;
            rl = right->u.decision.left;
            rr = right->u.decision.right;
        }

        n.kind = JXL_MA_FLAT_FUSED;
        n.u.fused.prop_level0 = property;
        n.u.fused.value_level0 = value;
        n.u.fused.props_level1[0] = lp;
        n.u.fused.props_level1[1] = rp;
        n.u.fused.values_level1[0] = lv;
        n.u.fused.values_level1[1] = rv;
        n.u.fused.index_base = next_base;
        if (!flat_push(alloc, out, &n)) {
            jxl_free_const(alloc, queue);
            jxl_ma_flat_tree_free(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        update_meta(out, &n);

        push_nodes[0] = ll;
        push_nodes[1] = lr;
        push_nodes[2] = rl;
        push_nodes[3] = rr;
        for (i = 0; i < 4; ++i) {
            if (!ma_queue_push(alloc, &queue, &queue_len, &queue_cap, push_nodes[i])) {
                jxl_free_const(alloc, queue);
                jxl_ma_flat_tree_free(alloc, out);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
        }
        next_base += 4;
    }

    jxl_free_const(alloc, queue);
    return JXL_MODULAR_OK;
}

#ifndef NDEBUG
static void debug_ma_walk(jxl_context *ctx, const jxl_ma_flat_tree *t,
                          jxl_modular_properties *props) {
    size_t idx;
    if (!JXL_DEBUG_FLAG(ctx, debug_ma_walk)) {
        return;
    }
    idx = 0;
    fprintf(stderr, "ma_walk:");
    for (;;) {
        int32_t p0v;
        int32_t plv;
        int32_t prv;
        int high_bit;
        uint32_t l;
        uint32_t r;
        int32_t v;
        int32_t delta;
        size_t max_idx;
        const jxl_ma_flat_node *node;

        if (idx >= t->len) {
            fprintf(stderr, " oob\n");
            return;
        }
        node = &t->nodes[idx];
        if (node->kind == JXL_MA_FLAT_LEAF) {
            fprintf(stderr, " ->%zuL(c=%u)\n", idx, (unsigned)node->u.leaf.cluster);
            return;
        }
        if (node->kind == JXL_MA_FLAT_FUSED) {
            p0v = jxl_ma_tree_prop(props, node->u.fused.prop_level0);
            plv = jxl_ma_tree_prop(props, node->u.fused.props_level1[0]);
            prv = jxl_ma_tree_prop(props, node->u.fused.props_level1[1]);
            high_bit = p0v <= node->u.fused.value_level0;
            l = plv <= node->u.fused.values_level1[0] ? 1u : 0u;
            r = 2u | (prv <= node->u.fused.values_level1[1] ? 1u : 0u);
            fprintf(stderr, " F%zu(%d->%u)", idx, high_bit, high_bit ? r : l);
            idx = (size_t)(node->u.fused.index_base + (high_bit ? r : l));
            continue;
        }
        if (node->kind == JXL_MA_FLAT_TABLE) {
            v = jxl_ma_tree_prop(props, node->u.table.prop);
            delta = v - node->u.table.value_base;
            if (delta < 0) {
                delta = 0;
            }
            max_idx = node->u.table.indices_len > 0 ? node->u.table.indices_len - 1 : 0;
            if ((size_t)delta > max_idx) {
                delta = (int32_t)max_idx;
            }
            fprintf(stderr, " T%zu(p=%u vb=%d d=%d)", idx,
                    (unsigned)node->u.table.prop, (int)node->u.table.value_base, delta);
            idx = (size_t)node->u.table.indices[(size_t)delta];
            continue;
        }
        fprintf(stderr, " ?\n");
        return;
    }
}
#endif

const jxl_ma_tree_leaf_clustered *jxl_ma_flat_tree_single_leaf(const jxl_ma_flat_tree *t) {
    if (t == NULL || t->len != 1 || t->nodes[0].kind != JXL_MA_FLAT_LEAF) {
        return NULL;
    }
    return &t->nodes[0].u.leaf;
}

int jxl_ma_flat_tree_is_fast_lossless_gradient(const jxl_ma_flat_tree *t) {
    /* Rust: ma_tree.as_ref().map(...).unwrap_or(true) — absent/empty tree is fast-lossless. */
    const jxl_ma_tree_leaf_clustered *leaf;
    if (t == NULL || t->len == 0) {
        return 1;
    }
    leaf = jxl_ma_flat_tree_single_leaf(t);
    return leaf != NULL && leaf->predictor == JXL_PREDICTOR_GRADIENT && leaf->offset == 0 &&
           leaf->multiplier == 1;
}

int jxl_ma_flat_tree_simple_table(const jxl_ma_flat_tree *t, uint32_t *decision_prop_out,
                                  int32_t *value_base_out, jxl_ma_tree_leaf_clustered *leaf_out,
                                  uint8_t *cluster_table_out, size_t cluster_table_cap,
                                  size_t *cluster_table_len_out) {
                                      size_t i;
    int32_t off;
    uint32_t mul;
    int seen;
    jxl_ma_tree_leaf_clustered compound_tmp;
    const jxl_ma_flat_node *root;
    jxl_predictor pred;
    if (t == NULL || t->len == 0 || t->nodes[0].kind != JXL_MA_FLAT_TABLE) {
        return 0;
    }
    root = &t->nodes[0];
    if (decision_prop_out != NULL) {
        *decision_prop_out = root->u.table.prop;
    }
    if (value_base_out != NULL) {
        *value_base_out = root->u.table.value_base;
    }
    if (cluster_table_len_out != NULL) {
        *cluster_table_len_out = root->u.table.indices_len;
    }
    if (leaf_out == NULL || cluster_table_out == NULL ||
        root->u.table.indices_len > cluster_table_cap) {
        return 0;
    }
    pred = JXL_PREDICTOR_ZERO;
    off = 0;
    mul = 1;
    seen = 0;
    for (i = 0; i < root->u.table.indices_len; ++i) {
        uint32_t idx = root->u.table.indices[i];
        const jxl_ma_tree_leaf_clustered *lf;
        if (idx >= t->len || t->nodes[idx].kind != JXL_MA_FLAT_LEAF) {
            return 0;
        }
        lf = &t->nodes[idx].u.leaf;
        if (!seen) {
            pred = lf->predictor;
            off = lf->offset;
            mul = lf->multiplier;
            seen = 1;
        } else if (lf->predictor != pred || lf->offset != off || lf->multiplier != mul) {
            return 0;
        }
        cluster_table_out[i] = lf->cluster;
    }
    if (!seen) {
        return 0;
    }
    compound_tmp.predictor = pred;
    compound_tmp.offset = off;
    compound_tmp.multiplier = mul;
    compound_tmp.cluster = cluster_table_out[0];
    *leaf_out = compound_tmp;

    return 1;
}
