// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "modular/encoding/encoding.h"

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/common.h"
#include "base/enc_status.h"
#include "fields.h"
#include "modular/options.h"


// Removes all nodes that use a static property (i.e. channel or group ID) from
// the tree and collapses each node on even levels with its two children to
// produce a flatter tree. Also computes whether the resulting tree requires
// using the weighted predictor.
void jxl_mark_tree_property(int32_t p, bool *has_wp, bool *gradient_only) {
  if (p == kWPProp) {
    *has_wp = true;
  }
  if (p >= kNumStaticProperties && p != kGradientProp) {
    *gradient_only = false;
  }
}

void jxl_filter_tree(const jxl_tree *global_tree,
                pixel_type static_props[kNumStaticProperties],
                size_t *num_props, bool *use_wp, bool *gradient_only,
                jxl_flat_tree *out) {
  *num_props = 0;
  bool has_wp = false;
  *gradient_only = true;
  jxl_context* mm = out->ctx;
  jxl_flat_tree output;
  jxl_array_construct_empty(&output, mm);
  jxl_array_size nodes;
  jxl_array_construct_empty(&nodes, mm);
  size_t nodes_head = 0;
  if (!jxl_enc_status_ok(jxl_array_size_push_back(&nodes, 0))) JXL_CRASH();
  // Produces a trimmed and flattened tree by doing a BFS visit of the original
  // tree, ignoring branches that are known to be false and proceeding two
  // levels at a time to collapse nodes in a flatter tree; if an inner parent
  // node has a leaf as a child, the leaf is duplicated and an implicit fake
  // node is added. This allows to reduce the number of branches when traversing
  // the resulting flat tree.
  while (nodes_head < jxl_array_len(&nodes)) {
    size_t cur = *jxl_array_at(&nodes, nodes_head++);
    // Skip nodes that we can decide now, by jumping directly to their children.
    while (jxl_array_at_const(global_tree, cur)->property < kNumStaticProperties &&
           jxl_array_at_const(global_tree, cur)->property != -1) {
      if (static_props[jxl_array_at_const(global_tree, cur)->property] > jxl_array_at_const(global_tree, cur)->splitval) {
        cur = jxl_array_at_const(global_tree, cur)->lchild;
      } else {
        cur = jxl_array_at_const(global_tree, cur)->rchild;
      }
    }
    jxl_flat_decision_node flat;
    if (jxl_array_at_const(global_tree, cur)->property == -1) {
      flat.property0 = -1;
      flat.childID = jxl_array_at_const(global_tree, cur)->lchild;
      flat.top.predictor = jxl_array_at_const(global_tree, cur)->predictor;
      flat.meta.predictor_offset = jxl_array_at_const(global_tree, cur)->predictor_offset;
      flat.children.multiplier = jxl_array_at_const(global_tree, cur)->multiplier;
      *gradient_only &= flat.top.predictor == kPredictorGradient;
      has_wp |= flat.top.predictor == kPredictorWeighted;
      if (!jxl_enc_status_ok(jxl_array_flat_decision_node_push_back(&output, flat))) JXL_CRASH();
      continue;
    }
    flat.childID = jxl_array_len(&output) + (jxl_array_len(&nodes) - nodes_head) + 1;

    flat.property0 = jxl_array_at_const(global_tree, cur)->property;
    *num_props = JXL_MAX((size_t)(flat.property0 + 1), *num_props);
    flat.top.splitval0 = jxl_array_at_const(global_tree, cur)->splitval;

    for (size_t i = 0; i < 2; i++) {
      size_t cur_child =
          i == 0 ? jxl_array_at_const(global_tree, cur)->lchild : jxl_array_at_const(global_tree, cur)->rchild;
      // Skip nodes that we can decide now.
      while (jxl_array_at_const(global_tree, cur_child)->property < kNumStaticProperties &&
             jxl_array_at_const(global_tree, cur_child)->property != -1) {
        if (static_props[jxl_array_at_const(global_tree, cur_child)->property] >
            jxl_array_at_const(global_tree, cur_child)->splitval) {
          cur_child = jxl_array_at_const(global_tree, cur_child)->lchild;
        } else {
          cur_child = jxl_array_at_const(global_tree, cur_child)->rchild;
        }
      }
      // We ended up in a leaf, add a placeholder decision and two copies of the
      // leaf.
      if (jxl_array_at_const(global_tree, cur_child)->property == -1) {
        flat.meta.properties[i] = 0;
        flat.children.splitvals[i] = 0;
        if (!jxl_enc_status_ok(jxl_array_size_push_back(&nodes, cur_child))) JXL_CRASH();
        if (!jxl_enc_status_ok(jxl_array_size_push_back(&nodes, cur_child))) JXL_CRASH();
      } else {
        flat.meta.properties[i] = jxl_array_at_const(global_tree, cur_child)->property;
        flat.children.splitvals[i] = jxl_array_at_const(global_tree, cur_child)->splitval;
        if (!jxl_enc_status_ok(jxl_array_size_push_back(&nodes, jxl_array_at_const(global_tree, cur_child)->lchild))) {
          JXL_CRASH();
        }
        if (!jxl_enc_status_ok(jxl_array_size_push_back(&nodes, jxl_array_at_const(global_tree, cur_child)->rchild))) {
          JXL_CRASH();
        }
        *num_props =
            JXL_MAX((size_t)(flat.meta.properties[i] + 1), *num_props);
      }
    }

    for (size_t property_i = 0; property_i < 2; ++property_i) {
      int16_t property = flat.meta.properties[property_i];
      jxl_mark_tree_property(property, &has_wp, gradient_only);
    }
    jxl_mark_tree_property(flat.property0, &has_wp, gradient_only);
    if (!jxl_enc_status_ok(jxl_array_flat_decision_node_push_back(&output, flat))) JXL_CRASH();
  }
  if (*num_props > kNumNonrefProperties) {
    *num_props =
        jxl_div_ceil(*num_props - kNumNonrefProperties, kExtraPropsPerChannel) *
            kExtraPropsPerChannel +
        kNumNonrefProperties;
  } else {
    *num_props = kNumNonrefProperties;
  }
  *use_wp = has_wp;

  jxl_array_destroy(&nodes);
  jxl_array_construct_empty(out, mm);
  jxl_array_swap(out, &output);
  jxl_array_destroy(&output);
}

