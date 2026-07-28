// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MODULAR_ENCODING_DEC_MA_H_
#define LIB_JXL_MODULAR_ENCODING_DEC_MA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/memory_manager.h"

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/modular/options.h"


// inner nodes
typedef struct jxl_property_decision_node {
  jxl_property_val splitval;
  int16_t property;  // -1: leaf node, lchild points to leaf node
  uint32_t lchild;
  uint32_t rchild;
  jxl_predictor predictor;
  int64_t predictor_offset;
  uint32_t multiplier;
} jxl_property_decision_node;

static inline void jxl_property_decision_node_construct_empty(jxl_property_decision_node* n) {
  n->splitval = 0;
  n->property = -1;
  n->lchild = 0;
  n->rchild = 0;
  n->predictor = kPredictorZero;
  n->predictor_offset = 0;
  n->multiplier = 1;
}

static inline jxl_property_decision_node jxl_property_decision_node_leaf(jxl_predictor predictor,
                                                     int64_t offset,
                                                     uint32_t multiplier) {
  jxl_property_decision_node n;
  jxl_property_decision_node_construct_empty(&n);
  n.predictor = predictor;
  n.predictor_offset = offset;
  n.multiplier = multiplier;
  return n;
}

static inline jxl_property_decision_node jxl_property_decision_node_split(int p, int split_val,
                                                      int lchild, int rchild) {
  jxl_property_decision_node n;
  jxl_property_decision_node_construct_empty(&n);
  n.property = (int16_t)(p);
  n.splitval = split_val;
  n.lchild = lchild;
  n.rchild =
      (rchild == -1) ? (uint32_t)(lchild + 1) : (uint32_t)(rchild);
  return n;
}

JXL_DEFINE_POD_ARRAY(jxl_array_property_decision_node, jxl_property_decision_node)
typedef jxl_array_property_decision_node jxl_tree;

// Empty jxl_tree: prefer jxl_array_construct_empty(&tree, mm) at the call site.
static inline void jxl_tree_construct_empty(jxl_tree* tree,
                                            jxl_memory_manager* mm) {
  jxl_array_construct_empty(tree, mm);
}

// Move-free list of jxl_trees (was MoveArray<jxl_tree>); avoids nested Array<Array<…>>.
typedef struct jxl_trees {
  jxl_memory_manager* memory_manager;
  jxl_tree* ptr;
  size_t len;
  size_t capacity;

} jxl_trees;

static inline size_t jxl_trees_size(const jxl_trees* self) { return self->len; }

static inline bool jxl_trees_empty(const jxl_trees* self) { return self->len == 0; }

static inline jxl_tree* jxl_trees_data(jxl_trees* self) { return self->ptr; }

static inline const jxl_tree* jxl_trees_data_const(const jxl_trees* self) { return self->ptr; }

static inline jxl_tree* jxl_trees_at(jxl_trees* self, size_t i) { return &self->ptr[i]; }

static inline const jxl_tree* jxl_trees_at_const(const jxl_trees* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_trees_construct_empty(jxl_trees* self) {
  self->memory_manager = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline jxl_status jxl_trees_reserve(jxl_trees* self, size_t new_capacity) {
    if (new_capacity <= self->capacity) return jxl_ok_status();

    size_t grown = self->capacity;
    if (grown == 0) grown = 16;
    while (grown < new_capacity) {
      size_t next;
      if (!jxl_safe_add(grown, grown / 2, &next) || next <= grown) {
        grown = new_capacity;
        break;
      }
      grown = next;
    }
    if (grown < new_capacity) grown = new_capacity;

    size_t bytes;
    if (!jxl_safe_mul(grown, sizeof(jxl_tree), &bytes)) {
      return JXL_FAILURE("jxl_trees::reserve: size overflow");
    }
    jxl_tree* neu;
    if (self->memory_manager == NULL) {
      return JXL_FAILURE("jxl_trees::reserve: missing memory manager");
    }
    neu = (jxl_tree*)(
        self->memory_manager->alloc(self->memory_manager->opaque, bytes));
    if (neu == NULL) {
      return JXL_FAILURE("jxl_trees::reserve: allocation failed");
    }
    for (size_t i = 0; i < self->len; ++i) {
      jxl_array_construct_empty(neu + i, self->memory_manager);
      jxl_array_swap(neu + i, &self->ptr[i]);
      jxl_array_destroy(self->ptr + i);
    }
    if (self->ptr != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
    self->ptr = neu;
    self->capacity = grown;
    return jxl_ok_status();
  }

static inline jxl_status jxl_trees_resize(jxl_trees* self, size_t n) {
    if (n < self->len) {
      for (size_t i = n; i < self->len; ++i) {
        jxl_array_destroy(self->ptr + i);
      }
      self->len = n;
      return jxl_ok_status();
    }
    JXL_RETURN_IF_ERROR(jxl_trees_reserve(self, n));
    while (self->len < n) {
      jxl_array_construct_empty(self->ptr + self->len, self->memory_manager);
      ++self->len;
    }
    return jxl_ok_status();
  }

static inline void jxl_trees_create(jxl_trees* self, size_t n,
                                    jxl_memory_manager* mm) {
  jxl_trees_construct_empty(self);
  self->memory_manager = mm;
  if (!jxl_status_ok(jxl_trees_resize(self, n))) JXL_CRASH();
}

static inline void jxl_trees_destroy(jxl_trees* self) {
  for (size_t i = 0; i < self->len; ++i) {
    jxl_array_destroy(self->ptr + i);
  }
  self->len = 0;
  if (self->ptr != NULL) {
    if (self->memory_manager != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline void jxl_trees_swap(jxl_trees* self, jxl_trees* other) {
  jxl_memory_manager* tmp_mm = self->memory_manager;
  self->memory_manager = other->memory_manager;
  other->memory_manager = tmp_mm;
  jxl_tree* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}


#endif  // LIB_JXL_MODULAR_ENCODING_DEC_MA_H_
