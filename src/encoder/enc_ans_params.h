// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_ANS_PARAMS_H_
#define JXL_ENC_ENC_ANS_PARAMS_H_

// Encoder-only parameter needed for ANS entropy encoding methods.

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "base/array.h"
#include "base/common.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "dec_ans.h"

// RebalanceHistogram requires a signed type.
typedef int32_t jxl_ans_hist_bin;

// Unscoped enums with unique enumerator names (C-shaped; no enum class).
typedef enum jxl_clustering_type {
  kClusteringFastest,  // Only 4 clusters.
  kClusteringFast,
  kClusteringBest,
} jxl_clustering_type;

typedef enum jxl_hybrid_uint_method {
  kHybridUintNone,        // just use kHybridUint420Config.
  kHybridUintFast,        // just try a couple of options.
  kHybridUintContextMap,  // fast choice for ctx map.
  kHybridUintBest,
} jxl_hybrid_uint_method;

typedef enum jxl_lz77_method {
  kLZ77None,     // do not try lz77.
  kLZ77RLE,      // only try doing RLE.
  kLZ77,         // try lz77 with backward references.
  kLZ77Optimal,  // optimal-matching LZ77 parsing.
} jxl_lz77_method;

typedef enum jxl_ans_histogram_strategy {
  kANSHistFast,         // Only try some methods, early exit.
  kANSHistApproximate,  // Only try some methods.
  kANSHistPrecise,      // Try all methods.
} jxl_ans_histogram_strategy;

typedef struct jxl_histogram_params {
  jxl_clustering_type clustering;
  jxl_hybrid_uint_method uint_method;
  jxl_lz77_method lz77_method;
  jxl_ans_histogram_strategy ans_histogram_strategy;
  size_t max_histograms;
  bool force_huffman;
} jxl_histogram_params;

static inline void jxl_histogram_params_construct_empty(jxl_histogram_params* self) {
  self->clustering = kClusteringBest;
  self->uint_method = kHybridUintBest;
  self->lz77_method = kLZ77RLE;
  self->ans_histogram_strategy = kANSHistPrecise;
  self->max_histograms = (size_t)(~0);
  self->force_huffman = false;
}

static inline jxl_hybrid_uint_config jxl_histogram_params_uint_config(const jxl_histogram_params* self) {
  if (self->uint_method ==
      kHybridUintContextMap) {
    return jxl_hybrid_uint_config_make(2, 0, 1);
  }
  // Default config for clustering.
  return jxl_hybrid_uint_config_make(4, 2, 0);
}

// Trivially copyable histogram metadata. Count buffers live in parallel
// Arrays (see jxl_histogram_add / jxl_cluster_histograms).
enum { kHistogramRounding = 8 };

typedef struct jxl_histogram {
  size_t total_count;
  float entropy;  // WARNING: not kept up-to-date.
} jxl_histogram;

static inline void jxl_histogram_construct_empty(jxl_histogram* self) {
  self->total_count = 0;
  self->entropy = 0;
}

static inline jxl_histogram jxl_histogram_empty(void) {
  jxl_histogram self;
  jxl_histogram_construct_empty(&self);
  return self;
}

JXL_DEFINE_POD_ARRAY(jxl_array_histogram, jxl_histogram)

static inline void jxl_histogram_ensure_capacity(jxl_array_i32* counts, size_t length) {
  size_t need = jxl_div_ceil(length, kHistogramRounding) * kHistogramRounding;
  if (jxl_array_len(counts) >= need) return;
  if (!jxl_enc_status_ok(jxl_array_resize_zero(counts, need))) JXL_CRASH();
}

static inline void jxl_histogram_add(jxl_histogram* h, jxl_array_i32* counts,
                         size_t symbol) {
  if (jxl_array_len(counts) <= symbol) {
    jxl_histogram_ensure_capacity(counts, symbol + 1);
  }
  ++*jxl_array_at(counts, symbol);
  ++h->total_count;
}

static inline void jxl_histogram_fast_add(jxl_array_i32* counts, size_t symbol) {
  (*(jxl_array_data(counts) + symbol))++;
}

static inline void jxl_histogram_add_histogram(jxl_histogram* h, jxl_array_i32* counts,
                                  const jxl_histogram* other,
                                  const jxl_array_i32* other_counts) {
  if (jxl_array_len(other_counts) > jxl_array_len(counts)) {
    if (!jxl_enc_status_ok(jxl_array_resize_zero(counts, jxl_array_len(other_counts)))) JXL_CRASH();
  }
  for (size_t i = 0; i < jxl_array_len(other_counts); ++i) {
    *jxl_array_at(counts, i) += *jxl_array_at_const(other_counts, i);
  }
  h->total_count += other->total_count;
}

static inline size_t jxl_histogram_alphabet_size(const jxl_array_i32* counts) {
  for (int i = (int)(jxl_array_len(counts)) - 1; i >= 0; --i) {
    if (*jxl_array_at_const(counts, i) > 0) return (size_t)(i) + 1;
  }
  return 0;
}

static inline size_t jxl_histogram_max_symbol(const jxl_histogram* h,
                                 const jxl_array_i32* counts) {
  if (h->total_count == 0) return 0;
  for (int i = (int)(jxl_array_len(counts)) - 1; i > 0; --i) {
    if (*jxl_array_at_const(counts, i)) return (size_t)(i);
  }
  return 0;
}

static inline void jxl_histogram_swap(jxl_histogram* a, jxl_array_i32* a_counts,
                          jxl_histogram* b, jxl_array_i32* b_counts) {
  jxl_array_swap(a_counts, b_counts);
  jxl_swap(&a->total_count, &b->total_count);
  jxl_swap(&a->entropy, &b->entropy);
}

void jxl_histogram_condition(jxl_histogram* h, jxl_array_i32* counts);

// Returns an estimate of the number of bits required to encode the given
// histogram (header bits plus data bits).
jxl_enc_status jxl_histogram_ans_population_cost(const jxl_histogram* h, const jxl_array_i32* counts,
                                  float* out);

float jxl_histogram_shannon_entropy(jxl_histogram* h, const jxl_array_i32* counts);

// Move-only list of per-context histogram count arrays (was
// MoveArray<jxl_array_i32>).
typedef struct jxl_hist_count_streams {
  jxl_context* ctx;
  jxl_array_i32* ptr;
  size_t len;
  size_t capacity;
} jxl_hist_count_streams;
static inline size_t jxl_hist_count_streams_size(const jxl_hist_count_streams* self) { return self->len; }

static inline bool jxl_hist_count_streams_empty(const jxl_hist_count_streams* self) { return self->len == 0; }

static inline jxl_array_i32* jxl_hist_count_streams_data(jxl_hist_count_streams* self) { return self->ptr; }

static inline const jxl_array_i32* jxl_hist_count_streams_data_const(const jxl_hist_count_streams* self) {
  return self->ptr;
}

static inline jxl_array_i32* jxl_hist_count_streams_at(jxl_hist_count_streams* self, size_t i) { return &self->ptr[i]; }

static inline const jxl_array_i32* jxl_hist_count_streams_at_const(const jxl_hist_count_streams* self,
                                               size_t i) {
  return &self->ptr[i];
}

static inline void jxl_hist_count_streams_construct_empty(jxl_hist_count_streams* self) {
  self->ctx = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_hist_count_streams_clear(jxl_hist_count_streams* self) {
    for (size_t i = 0; i < self->len; ++i) {
      jxl_array_destroy(self->ptr + i);
    }
    self->len = 0;
  }

static inline jxl_enc_status jxl_hist_count_streams_reserve(jxl_hist_count_streams* self, size_t new_capacity) {
    if (new_capacity <= self->capacity) return jxl_enc_ok_status();

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
    if (!jxl_safe_mul(grown, sizeof(jxl_array_i32), &bytes)) {
      return JXL_FAILURE("jxl_hist_count_streams::reserve: size overflow");
    }
    jxl_array_i32* neu;
    if (self->ctx == NULL) {
      return JXL_FAILURE("jxl_hist_count_streams::reserve: missing memory manager");
    }
    neu = (jxl_array_i32*)(
        jxl_alloc(self->ctx, bytes));
    if (neu == NULL) {
      return JXL_FAILURE("jxl_hist_count_streams::reserve: allocation failed");
    }
    for (size_t i = 0; i < self->len; ++i) {
      jxl_array_construct_empty(neu + i, self->ctx);
      jxl_array_swap(neu + i, &self->ptr[i]);
      jxl_array_destroy(self->ptr + i);
    }
    if (self->ptr != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
    self->ptr = neu;
    self->capacity = grown;
    return jxl_enc_ok_status();
  }

static inline jxl_enc_status jxl_hist_count_streams_resize(jxl_hist_count_streams* self, size_t n) {
    if (n < self->len) {
      for (size_t i = n; i < self->len; ++i) {
        jxl_array_destroy(self->ptr + i);
      }
      self->len = n;
      return jxl_enc_ok_status();
    }
    JXL_RETURN_IF_ERROR(jxl_hist_count_streams_reserve(self, n));
    while (self->len < n) {
      jxl_array_construct_empty(self->ptr + self->len, self->ctx);
      ++self->len;
    }
    return jxl_enc_ok_status();
  }

static inline void jxl_hist_count_streams_destroy(jxl_hist_count_streams* self) {
  jxl_hist_count_streams_clear(self);
  if (self->ptr != NULL) {
    if (self->ctx != NULL) {
      jxl_free(self->ctx, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline void jxl_hist_count_streams_create(jxl_hist_count_streams* self,
                                                 size_t n,
                                                 jxl_context* mm) {
  jxl_hist_count_streams_construct_empty(self);
  self->ctx = mm;
  if (!jxl_enc_status_ok(jxl_hist_count_streams_resize(self, n))) JXL_CRASH();
}

static inline jxl_array_i32* jxl_hist_count_streams_back(jxl_hist_count_streams* self) {
  JXL_DASSERT(self != NULL && self->len > 0);
  return &self->ptr[self->len - 1];
}

static inline jxl_enc_status jxl_hist_count_streams_emplace_back(jxl_hist_count_streams* self, jxl_array_i32* value) {
    if (self->len == self->capacity) {
      size_t need;
      if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
        return JXL_FAILURE("jxl_hist_count_streams::emplace_back: overflow");
      }
      JXL_RETURN_IF_ERROR(jxl_hist_count_streams_reserve(self, need));
    }
    jxl_array_construct_empty(self->ptr + self->len, self->ctx);
    jxl_array_swap(self->ptr + self->len, value);
    ++self->len;
    return jxl_enc_ok_status();
  }

static inline jxl_enc_status jxl_hist_count_streams_push_back(jxl_hist_count_streams* self,
                                         const jxl_array_i32* value) {
    if (self->len == self->capacity) {
      size_t need;
      if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
        return JXL_FAILURE("jxl_hist_count_streams::push_back: overflow");
      }
      JXL_RETURN_IF_ERROR(jxl_hist_count_streams_reserve(self, need));
    }
    jxl_array_construct_empty(self->ptr + self->len, self->ctx);
    JXL_RETURN_IF_ERROR(jxl_array_copy_from(self->ptr + self->len, value));
    ++self->len;
    return jxl_enc_ok_status();
  }

static inline void jxl_hist_count_streams_swap(jxl_hist_count_streams* self, jxl_hist_count_streams* other) {
    jxl_context* tmp_mm = self->ctx;
    self->ctx = other->ctx;
    other->ctx = tmp_mm;
    jxl_array_i32* tmp_ptr = self->ptr;
    self->ptr = other->ptr;
    other->ptr = tmp_ptr;
    jxl_swap(&self->len, &other->len);
    jxl_swap(&self->capacity, &other->capacity);
  }



#endif  // JXL_ENC_ENC_ANS_PARAMS_H_
