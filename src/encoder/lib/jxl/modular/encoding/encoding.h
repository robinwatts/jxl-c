// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MODULAR_ENCODING_ENCODING_H_
#define LIB_JXL_MODULAR_ENCODING_ENCODING_H_

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/modular/encoding/context_predict.h"
#include "lib/jxl/modular/encoding/dec_ma.h"
#include "lib/jxl/modular/modular_image.h"
#include "lib/jxl/modular/options.h"


// Valid range of properties for using lookup tables instead of trees.
enum { kPropRangeFast = 512 << 4 };

typedef struct jxl_group_header {
  jxl_fields fields;




  bool use_global_tree;
  jxl_weighted_header wp_header;
} jxl_group_header;

static inline jxl_status jxl_group_header_visit_fields(jxl_group_header* self, jxl_visitor *JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_group_header)

static inline jxl_status jxl_group_header_visit_fields(jxl_group_header* self, jxl_visitor *JXL_RESTRICT visitor) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->use_global_tree));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_nested(visitor, &self->wp_header.fields));
    // JPEG encoder-only: modular groups never use transforms. Keep the
    // empty-list encoding so bitstream parity is preserved.
    uint32_t num_transforms = 0;
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_bits_offset(4, 2), jxl_bits_offset(8, 18)), 0, &num_transforms));
    if (num_transforms != 0) {
      return JXL_FAILURE("Modular transforms are not supported");
    }
    return jxl_ok_status();
  }


static inline void jxl_group_header_construct_empty(jxl_group_header* self) {
  self->fields.visit_fields_fn = NULL;
#if (JXL_IS_DEBUG_BUILD)
  self->fields.name_fn = NULL;
#endif
  self->wp_header.fields.visit_fields_fn = NULL;
#if (JXL_IS_DEBUG_BUILD)
  self->wp_header.fields.name_fn = NULL;
#endif
  self->wp_header.p1C = 0;
  self->wp_header.p2C = 0;
  self->wp_header.p3Ca = 0;
  self->wp_header.p3Cb = 0;
  self->wp_header.p3Cc = 0;
  self->wp_header.p3Cd = 0;
  self->wp_header.p3Ce = 0;
  for (size_t i = 0; i < kWeightedNumPredictors; ++i) {
    self->wp_header.w[i] = 0;
  }
}
static inline void jxl_group_header_destroy(jxl_group_header* self) {
  (void)self;
}
static inline void jxl_group_header_init(jxl_group_header* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_group_header, &self->fields);
  jxl_weighted_header_init(&self->wp_header);
  jxl_bundle_init(&self->fields);
}
static inline void jxl_group_header_swap(jxl_group_header* self, jxl_group_header* other) {
    jxl_fields tf = self->fields;
    self->fields = other->fields;
    other->fields = tf;
    bool tg = self->use_global_tree;
    self->use_global_tree = other->use_global_tree;
    other->use_global_tree = tg;
    jxl_weighted_header th = self->wp_header;
    self->wp_header = other->wp_header;
    other->wp_header = th;
  }


// Move-only list of jxl_group_headers (was MoveArray<jxl_group_header>).
typedef struct jxl_group_headers {
  jxl_context* ctx;
  jxl_group_header* ptr;
  size_t len;
  size_t capacity;

} jxl_group_headers;

static inline size_t jxl_group_headers_size(const jxl_group_headers* self) { return self->len; }

static inline bool jxl_group_headers_empty(const jxl_group_headers* self) { return self->len == 0; }

static inline jxl_group_header* jxl_group_headers_data(jxl_group_headers* self) { return self->ptr; }

static inline const jxl_group_header* jxl_group_headers_data_const(const jxl_group_headers* self) { return self->ptr; }

static inline jxl_group_header* jxl_group_headers_at(jxl_group_headers* self, size_t i) { return &self->ptr[i]; }

static inline const jxl_group_header* jxl_group_headers_at_const(const jxl_group_headers* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_group_headers_construct_empty(jxl_group_headers* self) {
  self->ctx = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline jxl_status jxl_group_headers_reserve(jxl_group_headers* self, size_t new_capacity) {
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
    if (!jxl_safe_mul(grown, sizeof(jxl_group_header), &bytes)) {
      return JXL_FAILURE("jxl_group_headers::reserve: size overflow");
    }
    jxl_group_header* neu;
    if (self->ctx == NULL) {
      return JXL_FAILURE("jxl_group_headers::reserve: missing memory manager");
    }
    neu = (jxl_group_header*)(
        jxl_alloc(self->ctx, bytes));
    if (neu == NULL) {
      return JXL_FAILURE("jxl_group_headers::reserve: allocation failed");
    }
    for (size_t i = 0; i < self->len; ++i) {
      jxl_group_header_construct_empty(neu + i);
      jxl_group_header_init(neu + i);
      jxl_group_header_swap(neu + i, &self->ptr[i]);
      jxl_group_header_destroy(self->ptr + i);
    }
    if (self->ptr != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
    self->ptr = neu;
    self->capacity = grown;
    return jxl_ok_status();
  }

static inline jxl_status jxl_group_headers_resize(jxl_group_headers* self, size_t n) {
    if (n < self->len) {
      for (size_t i = n; i < self->len; ++i) {
        jxl_group_header_destroy(self->ptr + i);
      }
      self->len = n;
      return jxl_ok_status();
    }
    JXL_RETURN_IF_ERROR(jxl_group_headers_reserve(self, n));
    while (self->len < n) {
      jxl_group_header_construct_empty(self->ptr + self->len);
      jxl_group_header_init(self->ptr + self->len);
      ++self->len;
    }
    return jxl_ok_status();
  }

static inline void jxl_group_headers_destroy(jxl_group_headers* self) {
  for (size_t i = 0; i < self->len; ++i) {
    jxl_group_header_destroy(self->ptr + i);
  }
  self->len = 0;
  if (self->ptr != NULL) {
    if (self->ctx != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline void jxl_group_headers_swap(jxl_group_headers* self, jxl_group_headers* other) {
    jxl_group_header* tp = self->ptr;
    self->ptr = other->ptr;
    other->ptr = tp;
    size_t tl = self->len;
    self->len = other->len;
    other->len = tl;
    size_t tc = self->capacity;
    self->capacity = other->capacity;
    other->capacity = tc;
    jxl_context* tm = self->ctx;
    self->ctx = other->ctx;
    other->ctx = tm;
  }


void jxl_filter_tree(const jxl_tree *global_tree,
                pixel_type static_props[kNumStaticProperties],
                size_t *num_props, bool *use_wp, bool *gradient_only,
                jxl_flat_tree *out);

typedef struct jxl_tree_lut {
  jxl_array_u16 context_lookup;

} jxl_tree_lut;

static inline jxl_status jxl_tree_lut_init(jxl_tree_lut* self,
                                           jxl_context* mm) {
  jxl_array_construct_empty(&self->context_lookup, mm);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&self->context_lookup, 2 * kPropRangeFast));
  return jxl_ok_status();
}
static inline void jxl_tree_lut_destroy(jxl_tree_lut* self) {
  if (self == NULL) return;
  jxl_array_destroy(&self->context_lookup);
}

typedef struct jxl_tree_range {
  // Begin *excluded*, end *included*. This works best with > vs <= decision
  // nodes.
  int begin, end;
  size_t pos;
} jxl_tree_range;

static inline jxl_tree_range jxl_tree_range_make(int begin, int end, size_t pos) {
  jxl_tree_range range;
  range.begin = begin;
  range.end = end;
  range.pos = pos;
  return range;
}

JXL_DEFINE_POD_ARRAY(jxl_array_tree_range, jxl_tree_range)

static inline bool jxl_tree_to_lookup_table(const jxl_flat_tree *tree, jxl_tree_lut *lut) {
  jxl_array_tree_range ranges;
  bool ok = true;
  jxl_array_construct_empty(&ranges, lut->context_lookup.ctx);
  if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges, jxl_tree_range_make(-kPropRangeFast - 1, kPropRangeFast - 1, 0)))) {
    JXL_CRASH();
  }
  while (!jxl_array_empty(&ranges)) {
    jxl_tree_range cur = jxl_array_back(&ranges);
    jxl_array_pop_back(&ranges);
    if (cur.begin < -kPropRangeFast - 1 || cur.begin >= kPropRangeFast - 1 ||
        cur.end > kPropRangeFast - 1) {
      // jxl_tree is outside the allowed range, exit.
      ok = false;
      break;
    }
    const jxl_flat_decision_node* node = jxl_array_at_const(tree, cur.pos);
    // Leaf.
    if (node->property0 == -1) {
      if (node->meta.predictor_offset < INT8_MIN ||
          node->meta.predictor_offset > INT8_MAX) {
        ok = false;
        break;
      }
      if (node->children.multiplier < INT8_MIN ||
          node->children.multiplier > INT8_MAX) {
        ok = false;
        break;
      }
      // Encoder LUT path only supports multiplier 1 and zero offset.
      if (node->children.multiplier != 1) {
        ok = false;
        break;
      }
      if (node->meta.predictor_offset != 0) {
        ok = false;
        break;
      }
      for (int i = cur.begin + 1; i < cur.end + 1; i++) {
        *jxl_array_at(&lut->context_lookup, i + kPropRangeFast) =
            (uint16_t)(node->childID);
      }
      continue;
    }
    // > side of top node->
    if (node->meta.properties[0] >= kNumStaticProperties) {
      if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges,
                         jxl_tree_range_make(node->children.splitvals[0], cur.end, node->childID)))) {
        JXL_CRASH();
      }
      if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges, jxl_tree_range_make(node->top.splitval0, node->children.splitvals[0],
                                           node->childID + 1)))) {
        JXL_CRASH();
      }
    } else {
      if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges,
                         jxl_tree_range_make(node->top.splitval0, cur.end, node->childID)))) {
        JXL_CRASH();
      }
    }
    // <= side
    if (node->meta.properties[1] >= kNumStaticProperties) {
      if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges, jxl_tree_range_make(node->children.splitvals[1], node->top.splitval0,
                                           node->childID + 2)))) {
        JXL_CRASH();
      }
      if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges,
                         jxl_tree_range_make(cur.begin, node->children.splitvals[1], node->childID + 3)))) {
        JXL_CRASH();
      }
    } else {
      if (!jxl_status_ok(jxl_array_tree_range_push_back(&ranges,
                         jxl_tree_range_make(cur.begin, node->top.splitval0, node->childID + 2)))) {
        JXL_CRASH();
      }
    }
  }
  jxl_array_destroy(&ranges);
  return ok;
}


#endif  // LIB_JXL_MODULAR_ENCODING_ENCODING_H_
