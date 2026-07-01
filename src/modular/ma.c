// SPDX-License-Identifier: MIT OR Apache-2.0
#include "ma.h"

#include "bitstream/unpack.h"
#include "modular/ma_tree.h"
#include "modular/util.h"

#include <string.h>

typedef enum {
    JXL_FOLD_DECISION = 0,
    JXL_FOLD_LEAF = 1,
} jxl_fold_kind;

typedef struct {
    jxl_fold_kind kind;
    union {
        struct {
            uint32_t property;
            int32_t value;
        } decision;
        struct {
            uint32_t ctx;
            jxl_predictor predictor;
            int32_t offset;
            uint32_t multiplier;
        } leaf;
    } u;
} jxl_folding_node;

void jxl_ma_config_init(jxl_ma_config *cfg) {
    if (cfg != NULL) {
        memset(cfg, 0, sizeof(*cfg));
    }
}

static void ma_tree_free(jxl_allocator_state *alloc, jxl_ma_tree_node *node) {
    if (node == NULL) {
        return;
    }
    if (!node->is_leaf) {
        ma_tree_free(alloc, node->u.decision.left);
        ma_tree_free(alloc, node->u.decision.right);
    }
    jxl_free(alloc, node);
}

void jxl_ma_config_destroy(jxl_allocator_state *alloc, jxl_ma_config *cfg) {
    (void)alloc;
    if (cfg == NULL) {
        return;
    }
    ma_tree_free(alloc, cfg->tree);
    cfg->tree = NULL;
    if (cfg->decoder != NULL && cfg->alloc != NULL) {
        jxl_coding_decoder_destroy(cfg->alloc, cfg->decoder);
    }
    cfg->decoder = NULL;
    cfg->alloc = NULL;
    jxl_ma_config_init(cfg);
}

static int ma_infinite_tree_dist(const jxl_coding_decoder *dec) {
    size_t map_len = 0;
    uint32_t token;
    const uint8_t *map = jxl_coding_decoder_cluster_map(dec, &map_len);
    if (map == NULL || map_len <= 1) {
        return 0;
    }
    token = 0;
    if (!jxl_coding_decoder_single_token(dec, map[1], &token)) {
        return 0;
    }
    return token != 0;
}

static jxl_modular_status_t from_coding(jxl_coding_status_t st) {
    return st == JXL_CODING_OK ? JXL_MODULAR_OK : JXL_MODULAR_DECODER_ERROR;
}

const jxl_coding_decoder *jxl_ma_config_decoder(const jxl_ma_config *cfg) {
    return cfg != NULL ? cfg->decoder : NULL;
}

const jxl_ma_tree_node *jxl_ma_config_tree_root(const jxl_ma_config *cfg) {
    return cfg != NULL ? cfg->tree : NULL;
}

jxl_modular_status_t jxl_ma_config_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                         const jxl_ma_config_params *params, jxl_ma_config *out) {
                                             size_t ni;
    size_t nodes_left;
    size_t max_depth;
    size_t ctx;
    size_t nodes_len;
    size_t nodes_cap;
    size_t num_tree_nodes;
    size_t map_len;
    size_t stack_len;
    size_t tree_depth;
    size_t node_limit;
    size_t depth_limit;
    jxl_coding_decoder *tree_decoder = NULL;
    jxl_coding_status_t cst;
    jxl_folding_node *nodes;
    jxl_coding_decoder *sample_decoder;
    const uint8_t *cluster_map;
    typedef struct {
        jxl_ma_tree_node *node;
        size_t depth;
    } jxl_build_entry;
    jxl_build_entry *stack;

    if (alloc == NULL || bs == NULL || params == NULL || out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_ma_config_destroy(alloc, out);

    cst = jxl_coding_decoder_parse(alloc, bs, 6, &tree_decoder);
    if (from_coding(cst) != JXL_MODULAR_OK) {
        return from_coding(cst);
    }
    if (ma_infinite_tree_dist(tree_decoder)) {
        jxl_coding_decoder_destroy(alloc, tree_decoder);
        return JXL_MODULAR_INVALID_MA_TREE;
    }

    node_limit = params->node_limit;
    depth_limit = params->depth_limit;
    nodes_left = 1;
    max_depth = 1;
    ctx = 0;
    nodes = NULL;
    nodes_len = 0;
    nodes_cap = 16;

    nodes = jxl_alloc(alloc, nodes_cap * sizeof(*nodes));
    if (nodes == NULL) {
        jxl_coding_decoder_destroy(alloc, tree_decoder);
        return JXL_MODULAR_OUT_OF_MEMORY;
    }

    cst = jxl_coding_decoder_begin(tree_decoder, bs);
    if (from_coding(cst) != JXL_MODULAR_OK) {
        jxl_free(alloc, nodes);
        jxl_coding_decoder_destroy(alloc, tree_decoder);
        return from_coding(cst);
    }

    while (nodes_left > 0) {
        uint32_t property;
        jxl_folding_node node;
        if (nodes_len >= (1u << 26)) {
            jxl_free(alloc, nodes);
            jxl_coding_decoder_destroy(alloc, tree_decoder);
            return JXL_MODULAR_INVALID_MA_TREE;
        }
        if (nodes_len > node_limit) {
            jxl_free(alloc, nodes);
            jxl_coding_decoder_destroy(alloc, tree_decoder);
            return JXL_MODULAR_BITSTREAM_ERROR;
        }
        if (nodes_len + 1 > nodes_cap) {
            size_t nc = nodes_cap < 256 ? 256 : nodes_cap * 2;
            jxl_folding_node *grown = jxl_realloc(alloc, nodes, nc * sizeof(*nodes));
            if (grown == NULL) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
            nodes = grown;
            nodes_cap = nc;
        }

        nodes_left -= 1;
        property = 0;
        cst = jxl_coding_decoder_read_varint(tree_decoder, bs, 1, &property);
        if (from_coding(cst) != JXL_MODULAR_OK) {
            jxl_free(alloc, nodes);
            jxl_coding_decoder_destroy(alloc, tree_decoder);
            return from_coding(cst);
        }

        if (property != 0) {
            uint32_t raw = 0;
            property -= 1;
            cst = jxl_coding_decoder_read_varint(tree_decoder, bs, 0, &raw);
            if (from_coding(cst) != JXL_MODULAR_OK) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return from_coding(cst);
            }
            node.kind = JXL_FOLD_DECISION;
            node.u.decision.property = property;
            node.u.decision.value = jxl_unpack_signed(raw);
            nodes_left += 2;
        } else {
            uint32_t pred = 0;
            uint32_t raw_off = 0;
            uint32_t mul_log = 0;
            uint32_t mul_bits = 0;
            jxl_predictor predictor;
            cst = jxl_coding_decoder_read_varint(tree_decoder, bs, 2, &pred);
            if (from_coding(cst) != JXL_MODULAR_OK) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return from_coding(cst);
            }
            if (jxl_predictor_from_u32(pred, &predictor) != JXL_MODULAR_OK) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return JXL_MODULAR_INVALID_MA_TREE;
            }
            cst = jxl_coding_decoder_read_varint(tree_decoder, bs, 3, &raw_off);
            if (from_coding(cst) != JXL_MODULAR_OK) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return from_coding(cst);
            }
            cst = jxl_coding_decoder_read_varint(tree_decoder, bs, 4, &mul_log);
            if (from_coding(cst) != JXL_MODULAR_OK) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return from_coding(cst);
            }
            if (mul_log > 30) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return JXL_MODULAR_INVALID_MA_TREE;
            }
            cst = jxl_coding_decoder_read_varint(tree_decoder, bs, 5, &mul_bits);
            if (from_coding(cst) != JXL_MODULAR_OK) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return from_coding(cst);
            }
            if (mul_bits > (1u << (31 - mul_log)) - 2u) {
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, tree_decoder);
                return JXL_MODULAR_INVALID_MA_TREE;
            }
            node.kind = JXL_FOLD_LEAF;
            node.u.leaf.ctx = (uint32_t)ctx;
            node.u.leaf.predictor = predictor;
            node.u.leaf.offset = jxl_unpack_signed(raw_off);
            node.u.leaf.multiplier = (mul_bits + 1u) << mul_log;
            ctx += 1;
        }
        nodes[nodes_len++] = node;
        if (nodes_left > max_depth) {
            max_depth = nodes_left;
        }
    }

    cst = jxl_coding_decoder_finalize(tree_decoder);
    jxl_coding_decoder_destroy(alloc, tree_decoder);
    if (from_coding(cst) != JXL_MODULAR_OK) {
        jxl_free(alloc, nodes);
        return from_coding(cst);
    }

    num_tree_nodes = nodes_len;
    sample_decoder = NULL;
    cst = jxl_coding_decoder_parse(alloc, bs, (uint32_t)ctx, &sample_decoder);
    if (from_coding(cst) != JXL_MODULAR_OK) {
        jxl_free(alloc, nodes);
        return from_coding(cst);
    }

    map_len = 0;
    cluster_map = jxl_coding_decoder_cluster_map(sample_decoder, &map_len);

    stack = jxl_alloc(alloc, (max_depth + 2) * sizeof(*stack));
    if (stack == NULL) {
        jxl_free(alloc, nodes);
        jxl_coding_decoder_destroy(alloc, sample_decoder);
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    stack_len = 0;
    tree_depth = 0;

    for (ni = nodes_len; ni-- > 0;) {
        jxl_folding_node fn = nodes[ni];
        jxl_ma_tree_node *built = jxl_alloc(alloc, sizeof(*built));
        if (built == NULL) {
            size_t si;
            for (si = 0; si < stack_len; ++si) {
                ma_tree_free(alloc, stack[si].node);
            }
            jxl_free(alloc, stack);
            jxl_free(alloc, nodes);
            jxl_coding_decoder_destroy(alloc, sample_decoder);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        if (fn.kind == JXL_FOLD_DECISION) {
            size_t depth;
            jxl_build_entry right;
            jxl_build_entry left;
            if (stack_len < 2) {
                size_t si;
                jxl_free(alloc, built);
                for (si = 0; si < stack_len; ++si) {
                    ma_tree_free(alloc, stack[si].node);
                }
                jxl_free(alloc, stack);
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, sample_decoder);
                return JXL_MODULAR_INVALID_MA_TREE;
            }
            right = stack[0];
            left = stack[1];
            if (stack_len > 2) {
                memmove(stack, stack + 2, (stack_len - 2) * sizeof(*stack));
            }
            stack_len -= 2;
            depth = left.depth > right.depth ? left.depth : right.depth;
            depth += 1;
            if (depth > depth_limit) {
                size_t si;
                ma_tree_free(alloc, left.node);
                ma_tree_free(alloc, right.node);
                jxl_free(alloc, built);
                for (si = 0; si < stack_len; ++si) {
                    ma_tree_free(alloc, stack[si].node);
                }
                jxl_free(alloc, stack);
                jxl_free(alloc, nodes);
                jxl_coding_decoder_destroy(alloc, sample_decoder);
                return JXL_MODULAR_BITSTREAM_ERROR;
            }
            built->is_leaf = 0;
            built->u.decision.property = fn.u.decision.property;
            built->u.decision.value = fn.u.decision.value;
            built->u.decision.left = left.node;
            built->u.decision.right = right.node;
            stack[stack_len].node = built;
            stack[stack_len].depth = depth;
            stack_len += 1;
            if (depth > tree_depth) {
                tree_depth = depth;
            }
        } else {
            uint8_t cluster = 0;
            if (fn.u.leaf.ctx < map_len) {
                cluster = cluster_map[fn.u.leaf.ctx];
            }
            built->is_leaf = 1;
            built->u.leaf.cluster = cluster;
            built->u.leaf.predictor = fn.u.leaf.predictor;
            built->u.leaf.offset = fn.u.leaf.offset;
            built->u.leaf.multiplier = fn.u.leaf.multiplier;
            stack[stack_len].node = built;
            stack[stack_len].depth = 0;
            stack_len += 1;
        }
    }

    jxl_free(alloc, nodes);
    if (stack_len != 1) {
        size_t si;
        for (si = 0; si < stack_len; ++si) {
            ma_tree_free(alloc, stack[si].node);
        }
        jxl_free(alloc, stack);
        jxl_coding_decoder_destroy(alloc, sample_decoder);
        return JXL_MODULAR_INVALID_MA_TREE;
    }

    out->num_tree_nodes = num_tree_nodes;
    out->tree_depth = tree_depth;
    out->tree = stack[0].node;
    out->decoder = sample_decoder;
    out->alloc = alloc;
    jxl_free(alloc, stack);
    return JXL_MODULAR_OK;
}
