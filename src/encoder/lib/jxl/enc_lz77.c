// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_lz77.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/fast_math_scalar.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/base/common.h"


typedef struct jxl_lz77_dist_code {
  int dist;
  int code;
} jxl_lz77_dist_code;
JXL_DEFINE_POD_ARRAY(jxl_array_lz77_dist_code, jxl_lz77_dist_code)

static bool jxl_dist_code_less(jxl_lz77_dist_code a, jxl_lz77_dist_code b) {
  if (a.dist != b.dist) return a.dist < b.dist;
  return a.code < b.code;
}
static void jxl_dist_code_swap_at(jxl_lz77_dist_code* a, size_t i, size_t j) {
  jxl_lz77_dist_code tmp = a[i];
  a[i] = a[j];
  a[j] = tmp;
}
static void jxl_dist_code_sift_down(jxl_lz77_dist_code* a, size_t len, size_t hole) {
  for (;;) {
    size_t child = 2 * hole + 1;
    if (child >= len) break;
    size_t right = child + 1;
    if (right < len && jxl_dist_code_less(a[child], a[right])) child = right;
    if (!jxl_dist_code_less(a[hole], a[child])) break;
    jxl_dist_code_swap_at(a, hole, child);
    hole = child;
  }
}
static void jxl_dist_code_sort(jxl_array_lz77_dist_code* table) {
  jxl_lz77_dist_code* a = jxl_array_data(table);
  size_t n = jxl_array_len(table);
  if (n < 2) return;
  for (size_t i = n / 2; i > 0; --i) {
    jxl_dist_code_sift_down(a, n, i - 1);
  }
  while (n > 1) {
    jxl_dist_code_swap_at(a, 0, n - 1);
    --n;
    jxl_dist_code_sift_down(a, n, 0);
  }
}

typedef struct jxl_lz77_match_info {
  uint32_t len;
  uint32_t dist_symbol;
  uint32_t ctx;
  float total_cost;
} jxl_lz77_match_info;
JXL_DEFINE_POD_ARRAY(jxl_array_lz77_match_info, jxl_lz77_match_info)

typedef struct jxl_symbol_cost_estimator {
  size_t max_alphabet_size_;
  jxl_array_float bits_;
  jxl_array_float add_symbol_cost_;

} jxl_symbol_cost_estimator;

static void jxl_symbol_cost_estimator_init(jxl_symbol_cost_estimator* self, size_t num_contexts,
                             const jxl_token_streams* tokens, const jxl_lz77_params* lz77,
                             bool with_add_symbol_cost) {
  jxl_context* mm = tokens->ctx;
  jxl_array_construct_empty(&self->bits_, mm);
  jxl_array_construct_empty(&self->add_symbol_cost_, mm);
  self->max_alphabet_size_ = 0;
  jxl_array_histogram builder;
  jxl_array_construct_empty(&builder, mm);
  jxl_histogram hist_empty = jxl_histogram_empty();
  if (!jxl_status_ok(jxl_array_histogram_resize_fill(&builder, num_contexts, hist_empty))) JXL_CRASH();
  jxl_hist_count_streams builder_counts;
  jxl_hist_count_streams_create(&builder_counts, num_contexts, mm);
  // Build histograms for estimating lz77 savings.
  jxl_hybrid_uint_config uint_config = jxl_hybrid_uint_config_default();
  for (size_t stream_i = 0; stream_i < jxl_token_streams_size(tokens); ++stream_i) {
    const jxl_token_stream* stream = jxl_token_streams_at_const(tokens, stream_i);
    for (size_t token_i = 0; token_i < jxl_array_len(stream); ++token_i) {
      const jxl_token* token = jxl_array_at_const(stream, token_i);
      uint32_t tok, nbits, bits;
      jxl_hybrid_uint_config_encode(
          (token->is_lz77_length ? lz77->length_uint_config : uint_config),
          token->value, &tok, &nbits, &bits);
      tok += token->is_lz77_length ? lz77->min_symbol : 0;
      JXL_DASSERT(token->context < num_contexts);
      jxl_histogram_add(jxl_array_at(&builder, token->context),
                   jxl_hist_count_streams_at(&builder_counts, token->context), tok);
    }
  }
  for (size_t i = 0; i < num_contexts; i++) {
    self->max_alphabet_size_ = JXL_MAX(
        self->max_alphabet_size_, jxl_array_len(jxl_hist_count_streams_at(&builder_counts, i)));
  }
  if (!jxl_status_ok(jxl_array_resize_zero(&self->bits_,
                                num_contexts * self->max_alphabet_size_))) {
    JXL_CRASH();
  }
  // TODO(veluca): SIMD?
  if (with_add_symbol_cost) {
    if (!jxl_status_ok(jxl_array_resize_zero(&self->add_symbol_cost_, num_contexts))) {
      JXL_CRASH();
    }
  }
  for (size_t i = 0; i < num_contexts; i++) {
    float inv_total = 1.0f / (jxl_array_at(&builder, i)->total_count + 1e-8f);
    float total_cost = 0;
    for (size_t j = 0; j < jxl_array_len(jxl_hist_count_streams_at(&builder_counts, i));
         j++) {
      size_t cnt = *jxl_array_at(jxl_hist_count_streams_at(&builder_counts, i), j);
      float cost = 0;
      if (cnt != 0 && cnt != jxl_array_at(&builder, i)->total_count) {
        cost = -jxl_fast_log2f(cnt * inv_total);
      } else if (cnt == 0) {
        cost = ANS_LOG_TAB_SIZE;  // Highest possible cost.
      }
      *jxl_array_at(&self->bits_, i * self->max_alphabet_size_ + j) = cost;
      if (with_add_symbol_cost) {
        total_cost +=
            cost * *jxl_array_at(jxl_hist_count_streams_at(&builder_counts, i), j);
      }
    }
    if (with_add_symbol_cost) {
      // Penalty for adding a lz77 symbol to this context (only used for the
      // greedy LZ77 cost model).
      *jxl_array_at(&self->add_symbol_cost_, i) =
          JXL_MAX(0.0f, 6.0f - total_cost * inv_total);
    }
  }
  jxl_hist_count_streams_destroy(&builder_counts);
  jxl_array_destroy(&builder);
}

static void jxl_symbol_cost_estimator_destroy(jxl_symbol_cost_estimator* self) {
  if (self == NULL) return;
  jxl_array_destroy(&self->bits_);
  jxl_array_destroy(&self->add_symbol_cost_);
}

static float jxl_symbol_cost_estimator_bits(const jxl_symbol_cost_estimator* self, size_t ctx,
                              size_t sym) {
  return *jxl_array_at_const(&self->bits_, ctx * self->max_alphabet_size_ + sym);
}
static float jxl_symbol_cost_estimator_len_cost(const jxl_symbol_cost_estimator* self, size_t ctx,
                                 size_t len, const jxl_lz77_params* lz77) {
  uint32_t nbits, bits, tok;
  jxl_hybrid_uint_config_encode(lz77->length_uint_config, len, &tok, &nbits, &bits);
  tok += lz77->min_symbol;
  return nbits + jxl_symbol_cost_estimator_bits(self, ctx, tok);
}
static float jxl_symbol_cost_estimator_dist_cost(const jxl_symbol_cost_estimator* self, size_t len,
                                  const jxl_lz77_params* lz77) {
  uint32_t nbits, bits, tok;
  jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config_make(4, 2, 0), len, &tok, &nbits, &bits);
  return nbits + jxl_symbol_cost_estimator_bits(
                     self, lz77->nonserialized_distance_context, tok);
}
static float jxl_symbol_cost_estimator_add_symbol_cost(const jxl_symbol_cost_estimator* self,
                                       size_t idx) {
  return *jxl_array_at_const(&self->add_symbol_cost_, idx);
}

static void ApplyLZ77_RLE(size_t num_contexts, const jxl_token_streams* tokens,
                   const jxl_lz77_params* lz77, jxl_token_streams* out) {
  jxl_token_streams_create(out, jxl_token_streams_size(tokens), tokens->ctx);
  // TODO(veluca): tune heuristics here.
  jxl_symbol_cost_estimator sce;
  jxl_symbol_cost_estimator_init(&sce, num_contexts, tokens, lz77, false);
  float bit_decrease = 0;
  size_t total_symbols = 0;
  jxl_array_float sym_cost;
  jxl_array_construct_empty(&sym_cost, tokens->ctx);
  jxl_hybrid_uint_config uint_config = jxl_hybrid_uint_config_default();
  for (size_t stream = 0; stream < jxl_token_streams_size(tokens); stream++) {
    const jxl_token_stream* in = jxl_token_streams_at_const(tokens, stream);
    jxl_token_stream* out_stream = jxl_token_streams_at(out, stream);
    total_symbols += jxl_array_len(in);
    // Cumulative sum of bit costs.
    if (!jxl_status_ok(jxl_array_resize_zero(&sym_cost, jxl_array_len(in) + 1))) JXL_CRASH();
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      uint32_t tok, nbits, unused_bits;
      jxl_hybrid_uint_config_encode(uint_config, jxl_array_at_const(in, i)->value, &tok, &nbits, &unused_bits);
      *jxl_array_at(&sym_cost, i + 1) = jxl_symbol_cost_estimator_bits(&sce, jxl_array_at_const(in, i)->context, tok) + nbits + *jxl_array_at(&sym_cost, i);
    }
    if (!jxl_status_ok(jxl_array_reserve(out_stream, jxl_array_len(in)))) JXL_CRASH();
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      size_t num_to_copy = 0;
      if (i > 0) {
        for (; i + num_to_copy < jxl_array_len(in); num_to_copy++) {
          if (jxl_array_at_const(in, i + num_to_copy)->value != jxl_array_at_const(in, i - 1)->value) {
            break;
          }
        }
      }
      if (num_to_copy == 0) {
if (!jxl_status_ok(jxl_array_token_push_back(out_stream, *jxl_array_at_const(in, i)))) JXL_CRASH();
continue;
      }
      float cost = *jxl_array_at(&sym_cost, i + num_to_copy) - *jxl_array_at(&sym_cost, i);
      // This subtraction might overflow, but that's OK.
      size_t lz77_len = num_to_copy - lz77->min_length;
      float lz77_cost = num_to_copy >= lz77->min_length
                            ? jxl_ceil_log2_nonzero32((uint32_t)(lz77_len + 1)) + 1
                            : 0;
      if (num_to_copy < lz77->min_length || cost <= lz77_cost) {
        for (size_t j = 0; j < num_to_copy; j++) {
if (!jxl_status_ok(jxl_array_token_push_back(out_stream, *jxl_array_at_const(in, i + j)))) JXL_CRASH();
}
        i += num_to_copy - 1;
        continue;
      }
      // Output the LZ77 length
    if (!jxl_status_ok(jxl_array_token_push_back(out_stream, jxl_token_make(jxl_array_at_const(in, i)->context, (uint32_t)(lz77_len))))) {
      JXL_CRASH();
    }
    jxl_array_back_ptr(out_stream)->is_lz77_length = true;
      i += num_to_copy - 1;
      bit_decrease += cost - lz77_cost;
      // Output the LZ77 copy distance (zero for RLE).
      if (!jxl_status_ok(jxl_array_token_push_back(out_stream, jxl_token_make((uint32_t)(lz77->nonserialized_distance_context), 0)))) {
        JXL_CRASH();
      }
    }
  }

  if (bit_decrease > total_symbols * 0.2 + 16) {
    jxl_array_destroy(&sym_cost);
    jxl_symbol_cost_estimator_destroy(&sce);
    return;
  }
  jxl_array_destroy(&sym_cost);
  jxl_symbol_cost_estimator_destroy(&sce);
  jxl_token_streams_destroy(out);
  jxl_token_streams_construct_empty(out);
}

typedef struct jxl_find_match_best_ctx {
  size_t* result_dist_symbol;
  size_t* result_len;
} jxl_find_match_best_ctx;

static void jxl_update_best_match(void* opaque, size_t len, size_t dist_symbol) {
  jxl_find_match_best_ctx* c = (jxl_find_match_best_ctx*)(opaque);
  if (len > *c->result_len ||
      (len == *c->result_len && *c->result_dist_symbol > dist_symbol)) {
    *c->result_len = len;
    *c->result_dist_symbol = dist_symbol;
  }
}

static void jxl_collect_dist_symbols(void* opaque, size_t len, size_t dist_symbol) {
  jxl_array_u32* dist_symbols = (jxl_array_u32*)(opaque);
  if (jxl_array_len(dist_symbols) <= len) {
    if (!jxl_status_ok(jxl_array_u32_resize_fill(dist_symbols, len + 1,
                         (uint32_t)(dist_symbol)))) {
      JXL_CRASH();
    }
  }
  if (dist_symbol < *jxl_array_at(dist_symbols, len)) {
    *jxl_array_at(dist_symbols, len) = dist_symbol;
  }
}

typedef struct jxl_hash_chain jxl_hash_chain;
static int jxl_hash_chain_dist_symbol(const jxl_hash_chain* self, int dist);
static uint32_t jxl_hash_chain_get_hash(const jxl_hash_chain* self, size_t pos);
static uint32_t jxl_hash_chain_count_zeros(const jxl_hash_chain* self, size_t pos,
                             uint32_t prevzeros);
static void jxl_hash_chain_update(jxl_hash_chain* self, size_t pos);
static void jxl_hash_chain_update_range(jxl_hash_chain* self, size_t pos, size_t len);
typedef void (*jxl_hash_chain_find_match_fn)(void* opaque, size_t len,
                                     size_t dist_symbol);
static void jxl_hash_chain_find_matches(const jxl_hash_chain* self, size_t pos, int max_dist,
                          jxl_hash_chain_find_match_fn found_match, void* opaque);
static void jxl_hash_chain_find_match(const jxl_hash_chain* self, size_t pos, int max_dist,
                        size_t* result_dist_symbol, size_t* result_len);

// Hash chain for LZ77 matching
struct jxl_hash_chain {
  size_t size_;
  jxl_array_u32 data_;

  unsigned hash_num_values_;
  unsigned hash_mask_;
  unsigned hash_shift_;

  jxl_array_int head;
  jxl_array_u32 chain;
  jxl_array_int val;

  // Speed up repetitions of zero
  jxl_array_int headz;
  jxl_array_u32 chainz;
  jxl_array_u32 zeros;
  uint32_t numzeros;

  size_t window_size_;
  size_t window_mask_;
  size_t min_length_;
  size_t max_length_;

  // Sorted (distance -> code) table for special distance codes.
  jxl_array_lz77_dist_code special_dist_table_;
  size_t num_special_distances_;

  uint32_t maxchainlength;  // window_size_ to allow all

};

static void jxl_hash_chain_destroy(jxl_hash_chain* self) {
  if (self == NULL) return;
  jxl_array_destroy(&self->data_);
  jxl_array_destroy(&self->head);
  jxl_array_destroy(&self->chain);
  jxl_array_destroy(&self->val);
  jxl_array_destroy(&self->headz);
  jxl_array_destroy(&self->chainz);
  jxl_array_destroy(&self->zeros);
  jxl_array_destroy(&self->special_dist_table_);
}

static void jxl_hash_chain_init(jxl_hash_chain* self, const jxl_token* data, size_t size,
                   size_t window_size, size_t min_length, size_t max_length,
                   size_t distance_multiplier, jxl_context* mm) {
  jxl_array_construct_empty(&self->data_, mm);
  jxl_array_construct_empty(&self->head, mm);
  jxl_array_construct_empty(&self->chain, mm);
  jxl_array_construct_empty(&self->val, mm);
  jxl_array_construct_empty(&self->headz, mm);
  jxl_array_construct_empty(&self->chainz, mm);
  jxl_array_construct_empty(&self->zeros, mm);
  jxl_array_construct_empty(&self->special_dist_table_, mm);
  self->hash_num_values_ = 32768;
  self->hash_mask_ = self->hash_num_values_ - 1;
  self->hash_shift_ = 5;
  self->numzeros = 0;
  self->num_special_distances_ = 0;
  self->maxchainlength = 256;
  self->size_ = size;
  self->window_size_ = window_size;
  self->window_mask_ = window_size - 1;
  self->min_length_ = min_length;
  self->max_length_ = max_length;
  if (!jxl_status_ok(jxl_array_resize_zero(&self->data_, size))) JXL_CRASH();
  for (size_t i = 0; i < size; i++) {
    *jxl_array_at(&self->data_, i) = data[i].value;
  }

  if (!jxl_status_ok(jxl_array_int_resize_fill(&self->head, self->hash_num_values_, -1)))
    JXL_CRASH();
  if (!jxl_status_ok(jxl_array_int_resize_fill(&self->val, window_size, -1))) JXL_CRASH();
  if (!jxl_status_ok(jxl_array_resize_zero(&self->chain, window_size))) JXL_CRASH();
  for (uint32_t i = 0; i < window_size; ++i) {
    *jxl_array_at(&self->chain, i) = i;  // same value as index indicates uninitialized
  }

  if (!jxl_status_ok(jxl_array_resize_zero(&self->zeros, window_size))) JXL_CRASH();
  if (!jxl_status_ok(jxl_array_int_resize_fill(&self->headz, window_size + 1, -1)))
    JXL_CRASH();
  if (!jxl_status_ok(jxl_array_resize_zero(&self->chainz, window_size))) JXL_CRASH();
  for (uint32_t i = 0; i < window_size; ++i) {
    *jxl_array_at(&self->chainz, i) = i;
  }
  // Translate distance to special distance code.
  if (distance_multiplier) {
    if (!jxl_status_ok(
            jxl_array_resize_zero(&self->special_dist_table_, kNumSpecialDistances)))
      JXL_CRASH();
    for (int i = 0; i < (int)(kNumSpecialDistances); ++i) {
      *jxl_array_at(&self->special_dist_table_, i) = (jxl_lz77_dist_code){
          jxl_special_distance(i, distance_multiplier), i};
    }
    // Ascending by distance, then by code so unique keeps the smallest code
    // when multiple special distances collide.
    jxl_dist_code_sort(&self->special_dist_table_);
    size_t n = 0;
    for (size_t i = 0; i < jxl_array_len(&self->special_dist_table_); ++i) {
      if (n == 0 || jxl_array_at(&self->special_dist_table_, i)->dist !=
                        jxl_array_at(&self->special_dist_table_, n - 1)->dist) {
        *jxl_array_at(&self->special_dist_table_, n++) = *jxl_array_at(&self->special_dist_table_, i);
      }
    }
    if (!jxl_status_ok(jxl_array_resize_zero(&self->special_dist_table_, n))) JXL_CRASH();
    self->num_special_distances_ = kNumSpecialDistances;
  }
}

static int jxl_hash_chain_dist_symbol(const jxl_hash_chain* self, int dist) {
    size_t lo = 0;
    size_t hi = jxl_array_len(&self->special_dist_table_);
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (jxl_array_at_const(&self->special_dist_table_, mid)->dist < dist) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo < jxl_array_len(&self->special_dist_table_) &&
        jxl_array_at_const(&self->special_dist_table_, lo)->dist == dist) {
      return jxl_array_at_const(&self->special_dist_table_, lo)->code;
    }
    return (int)(self->num_special_distances_ + dist - 1);
  }
static uint32_t jxl_hash_chain_get_hash(const jxl_hash_chain* self, size_t pos) {
    uint32_t result = 0;
    if (pos + 2 < self->size_) {
      // TODO(lode): take the MSB's of the uint32_t values into account as well,
      // given that the hash code itself is less than 32 bits.
      result ^= (uint32_t)(*jxl_array_at_const(&self->data_, pos + 0)) << 0u;
      result ^= (uint32_t)(*jxl_array_at_const(&self->data_, pos + 1)) << self->hash_shift_;
      result ^= (uint32_t)(*jxl_array_at_const(&self->data_, pos + 2)) << (self->hash_shift_ * 2);
    } else {
      // No need to compute hash of last 2 bytes, the length 2 is too short.
      return 0;
    }
    return result & self->hash_mask_;
  }
static uint32_t jxl_hash_chain_count_zeros(const jxl_hash_chain* self, size_t pos, uint32_t prevzeros) {
    size_t end = pos + self->window_size_;
    if (end > self->size_) end = self->size_;
    if (prevzeros > 0) {
      if (prevzeros >= self->window_mask_ && *jxl_array_at_const(&self->data_, end - 1) == 0 &&
          end == pos + self->window_size_) {
        return prevzeros;
      } else {
        return prevzeros - 1;
      }
    }
    uint32_t num = 0;
    while (pos + num < end && *jxl_array_at_const(&self->data_, pos + num) == 0) num++;
    return num;
  }
static void jxl_hash_chain_update(jxl_hash_chain* self, size_t pos) {
    uint32_t hashval = jxl_hash_chain_get_hash(self, pos);
    uint32_t wpos = pos & self->window_mask_;

    *jxl_array_at(&self->val, wpos) = (int)(hashval);
    if (*jxl_array_at(&self->head, hashval) != -1) *jxl_array_at(&self->chain, wpos) = *jxl_array_at(&self->head, hashval);
    *jxl_array_at(&self->head, hashval) = wpos;

    if (pos > 0 && *jxl_array_at(&self->data_, pos) != *jxl_array_at(&self->data_, pos - 1)) self->numzeros = 0;
    self->numzeros = jxl_hash_chain_count_zeros(self, pos, self->numzeros);

    *jxl_array_at(&self->zeros, wpos) = self->numzeros;
    if (*jxl_array_at(&self->headz, self->numzeros) != -1) *jxl_array_at(&self->chainz, wpos) = *jxl_array_at(&self->headz, self->numzeros);
    *jxl_array_at(&self->headz, self->numzeros) = wpos;
  }
static void jxl_hash_chain_update_range(jxl_hash_chain* self, size_t pos, size_t len) {
    for (size_t i = 0; i < len; i++) {
      jxl_hash_chain_update(self, pos + i);
    }
  }
static void jxl_hash_chain_find_matches(const jxl_hash_chain* self, size_t pos, int max_dist, jxl_hash_chain_find_match_fn found_match, void* opaque) {
    uint32_t wpos = pos & self->window_mask_;
    uint32_t hashval = jxl_hash_chain_get_hash(self, pos);
    uint32_t hashpos = *jxl_array_at_const(&self->chain, wpos);

    int prev_dist = 0;
    int end = JXL_MIN((int)(pos + self->max_length_),
                      (int)(self->size_));
    uint32_t chainlength = 0;
    uint32_t best_len = 0;
    for (;;) {
      int dist = (hashpos <= wpos) ? (wpos - hashpos)
                                   : (wpos - hashpos + self->window_mask_ + 1);
      if (dist < prev_dist) break;
      prev_dist = dist;
      uint32_t len = 0;
      if (dist > 0) {
        int i = pos;
        int j = pos - dist;
        if (self->numzeros > 3) {
          int r = JXL_MIN((int)self->numzeros - 1, (int)(*jxl_array_at_const(&self->zeros, hashpos)));
          if (i + r >= end) r = end - i - 1;
          i += r;
          j += r;
        }
        while (i < end && *jxl_array_at_const(&self->data_, i) == *jxl_array_at_const(&self->data_, j)) {
          i++;
          j++;
        }
        len = i - pos;
        // This can trigger even if the new length is slightly smaller than the
        // best length, because it is possible for a slightly cheaper distance
        // symbol to occur.
        if (len >= self->min_length_ && len + 2 >= best_len) {
          found_match(opaque, len, jxl_hash_chain_dist_symbol(self, dist));
          if (len > best_len) best_len = len;
        }
      }

      chainlength++;
      if (chainlength >= self->maxchainlength) break;

      if (self->numzeros >= 3 && len > self->numzeros) {
        if (hashpos == *jxl_array_at_const(&self->chainz, hashpos)) break;
        hashpos = *jxl_array_at_const(&self->chainz, hashpos);
        if (*jxl_array_at_const(&self->zeros, hashpos) != self->numzeros) break;
      } else {
        if (hashpos == *jxl_array_at_const(&self->chain, hashpos)) break;
        hashpos = *jxl_array_at_const(&self->chain, hashpos);
        if (*jxl_array_at_const(&self->val, hashpos) != (int)(hashval)) {
          // outdated hash value
          break;
        }
      }
    }
  }
static void jxl_hash_chain_find_match(const jxl_hash_chain* self, size_t pos, int max_dist, size_t* result_dist_symbol, size_t* result_len) {
    *result_dist_symbol = 0;
    *result_len = 1;
    jxl_find_match_best_ctx ctx = {result_dist_symbol, result_len};
    jxl_hash_chain_find_matches(self, pos, max_dist, jxl_update_best_match, &ctx);
  }

static float jxl_len_cost(size_t len) {
  uint32_t nbits, bits, tok;
  jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config_make(1, 0, 0), len, &tok, &nbits, &bits);
  const float kCostTable[] = {
      2.797667318563126,  3.213177690381199,  2.5706009246743737,
      2.408392498667534,  2.829649191872326,  3.3923087753324577,
      4.029267451554331,  4.415576699706408,  4.509357574741465,
      9.21481543803004,   10.020590190114898, 11.858671627804766,
      12.45853300490526,  11.713105831990857, 12.561996324849314,
      13.775477692278367, 13.174027068768641,
  };
  size_t table_size = sizeof kCostTable / sizeof *kCostTable;
  if (tok >= table_size) tok = table_size - 1;
  return kCostTable[tok] + nbits;
}

// TODO(veluca): this does not take into account usage or non-usage of distance
// multipliers.
static float jxl_dist_cost(size_t dist) {
  uint32_t nbits, bits, tok;
  jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config_make(7, 0, 0), dist, &tok, &nbits, &bits);
  const float kCostTable[] = {
      6.368282626312716,  5.680793277090298,  8.347404197105247,
      7.641619201599141,  6.914328374119438,  7.959808291537444,
      8.70023120759855,   8.71378518934703,   9.379132523982769,
      9.110472749092708,  9.159029569270908,  9.430936766731973,
      7.278284055315169,  7.8278514904267755, 10.026641158289236,
      9.976049229827066,  9.64351607048908,   9.563403863480442,
      10.171474111762747, 10.45950155077234,  9.994813912104219,
      10.322524683741156, 8.465808729388186,  8.756254166066853,
      10.160930174662234, 10.247329273413435, 10.04090403724809,
      10.129398517544082, 9.342311691539546,  9.07608009102374,
      10.104799540677513, 10.378079384990906, 10.165828974075072,
      10.337595322341553, 7.940557464567944,  10.575665823319431,
      11.023344321751955, 10.736144698831827, 11.118277044595054,
      7.468468230648442,  10.738305230932939, 10.906980780216568,
      10.163468216353817, 10.17805759656433,  11.167283670483565,
      11.147050200274544, 10.517921919244333, 10.651764778156886,
      10.17074446448919,  11.217636876224745, 11.261630721139484,
      11.403140815247259, 10.892472096873417, 11.1859607804481,
      8.017346947551262,  7.895143720278828,  11.036577113822025,
      11.170562110315794, 10.326988722591086, 10.40872184751056,
      11.213498225466386, 11.30580635516863,  10.672272515665442,
      10.768069466228063, 11.145257364153565, 11.64668307145549,
      10.593156194627339, 11.207499484844943, 10.767517766396908,
      10.826629811407042, 10.737764794499988, 10.6200448518045,
      10.191315385198092, 8.468384171390085,  11.731295299170432,
      11.824619886654398, 10.41518844301179,  10.16310536548649,
      10.539423685097576, 10.495136599328031, 10.469112847728267,
      11.72057686174922,  10.910326337834674, 11.378921834673758,
      11.847759036098536, 11.92071647623854,  10.810628276345282,
      11.008601085273893, 11.910326337834674, 11.949212023423133,
      11.298614839104337, 11.611603659010392, 10.472930394619985,
      11.835564720850282, 11.523267392285337, 12.01055816679611,
      8.413029688994023,  11.895784139536406, 11.984679534970505,
      11.220654278717394, 11.716311684833672, 10.61036646226114,
      10.89849965960364,  10.203762898863669, 10.997560826267238,
      11.484217379438984, 11.792836176993665, 12.24310468755171,
      11.464858097919262, 12.212747017409377, 11.425595666074955,
      11.572048533398757, 12.742093965163013, 11.381874288645637,
      12.191870445817015, 11.683156920035426, 11.152442115262197,
      11.90303691580457,  11.653292787169159, 11.938615382266098,
      16.970641701570223, 16.853602280380002, 17.26240782594733,
      16.644655390108507, 17.14310889757499,  16.910935455445955,
      17.505678976959697, 17.213498225466388, 2.4162310293553024,
      3.494587244462329,  3.5258600986408344, 3.4959806589517095,
      3.098390886949687,  3.343454654302911,  3.588847442290287,
      4.14614790111827,   5.152948641990529,  7.433696808092598,
      9.716311684833672,
  };
  size_t table_size = sizeof kCostTable / sizeof *kCostTable;
  if (tok >= table_size) tok = table_size - 1;
  return kCostTable[tok] + nbits;
}

static void ApplyLZ77_LZ77(
    const jxl_histogram_params* params, size_t num_contexts,
    const jxl_token_streams* tokens, const jxl_lz77_params* lz77,
    const jxl_array_size* image_widths, jxl_token_streams* out) {
  jxl_token_streams_create(out, jxl_token_streams_size(tokens), tokens->ctx);
  // TODO(veluca): tune heuristics here.
  jxl_symbol_cost_estimator sce;
  jxl_symbol_cost_estimator_init(&sce, num_contexts, tokens, lz77, true);
  float bit_decrease = 0;
  size_t total_symbols = 0;
  jxl_hybrid_uint_config uint_config = jxl_hybrid_uint_config_default();
  jxl_array_float sym_cost;
  jxl_array_construct_empty(&sym_cost, tokens->ctx);
  for (size_t stream = 0; stream < jxl_token_streams_size(tokens); stream++) {
    size_t distance_multiplier =
        jxl_array_len(image_widths) > stream ? *jxl_array_at_const(image_widths, stream) : 0;
    const jxl_token_stream* in = jxl_token_streams_at_const(tokens, stream);
    jxl_token_stream* out_stream = jxl_token_streams_at(out, stream);
    total_symbols += jxl_array_len(in);
    // Cumulative sum of bit costs.
    if (!jxl_status_ok(jxl_array_resize_zero(&sym_cost, jxl_array_len(in) + 1))) JXL_CRASH();
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      uint32_t tok, nbits, unused_bits;
      jxl_hybrid_uint_config_encode(uint_config, jxl_array_at_const(in, i)->value, &tok, &nbits, &unused_bits);
      *jxl_array_at(&sym_cost, i + 1) = jxl_symbol_cost_estimator_bits(&sce, jxl_array_at_const(in, i)->context, tok) + nbits + *jxl_array_at(&sym_cost, i);
    }

    if (!jxl_status_ok(jxl_array_reserve(out_stream, jxl_array_len(in)))) JXL_CRASH();
    size_t max_distance = jxl_array_len(in);
    size_t min_length = lz77->min_length;
    JXL_DASSERT(min_length >= 3);
    size_t max_length = jxl_array_len(in);

    // Use next power of two as window size.
    size_t window_size = 1;
    while (window_size < max_distance && window_size < kWindowSize) {
      window_size <<= 1;
    }

    jxl_hash_chain chain;
    jxl_hash_chain_init(&chain, jxl_array_data_const(in), jxl_array_len(in), window_size, min_length,
                  max_length, distance_multiplier, tokens->ctx);
    size_t len;
    size_t dist_symbol;

    const size_t max_lazy_match_len = 256;  // 0 to disable lazy matching

    // Whether the next symbol was already updated (to test lazy matching)
    bool already_updated = false;
    for (size_t i = 0; i < jxl_array_len(in); i++) {
if (!jxl_status_ok(jxl_array_token_push_back(out_stream, *jxl_array_at_const(in, i)))) JXL_CRASH();
if (!already_updated) jxl_hash_chain_update(&chain, i);
      already_updated = false;
      jxl_hash_chain_find_match(&chain, i, max_distance, &dist_symbol, &len);
      if (len >= min_length) {
        if (len < max_lazy_match_len && i + 1 < jxl_array_len(in)) {
          // Try length at next symbol lazy matching
          jxl_hash_chain_update(&chain, i + 1);
          already_updated = true;
          size_t len2, dist_symbol2;
          jxl_hash_chain_find_match(&chain, i + 1, max_distance, &dist_symbol2, &len2);
          if (len2 > len) {
            // Use the lazy match. Add literal, and use the next length starting
            // from the next byte.
            ++i;
            already_updated = false;
            len = len2;
            dist_symbol = dist_symbol2;
if (!jxl_status_ok(jxl_array_token_push_back(out_stream, *jxl_array_at_const(in, i)))) JXL_CRASH();
}
        }

        float cost = *jxl_array_at(&sym_cost, i + len) - *jxl_array_at(&sym_cost, i);
        size_t lz77_len = len - lz77->min_length;
        float lz77_cost = jxl_len_cost(lz77_len) + jxl_dist_cost(dist_symbol) +
                          jxl_symbol_cost_estimator_add_symbol_cost(&sce, jxl_array_back_ptr(out_stream)->context);

        if (lz77_cost <= cost) {
          jxl_array_back_ptr(out_stream)->value = len - min_length;
          jxl_array_back_ptr(out_stream)->is_lz77_length = true;
          if (!jxl_status_ok(jxl_array_token_push_back(
                  out_stream, jxl_token_make((uint32_t)(lz77->nonserialized_distance_context),
                             (uint32_t)(dist_symbol))))) {
            JXL_CRASH();
          }
          bit_decrease += cost - lz77_cost;
        } else {
          // LZ77 match ignored, and symbol already pushed. Push all other
          // symbols and skip.
          for (size_t j = 1; j < len; j++) {
if (!jxl_status_ok(jxl_array_token_push_back(out_stream, *jxl_array_at_const(in, i + j)))) JXL_CRASH();
}
        }

        if (already_updated) {
          jxl_hash_chain_update_range(&chain, i + 2, len - 2);
          already_updated = false;
        } else {
          jxl_hash_chain_update_range(&chain, i + 1, len - 1);
        }
        i += len - 1;
      } else {
        // Literal, already pushed
      }
    }
    jxl_hash_chain_destroy(&chain);
  }

  if (bit_decrease > total_symbols * 0.2 + 16) {
    jxl_array_destroy(&sym_cost);
    jxl_symbol_cost_estimator_destroy(&sce);
    return;
  }
  jxl_array_destroy(&sym_cost);
  jxl_symbol_cost_estimator_destroy(&sce);
  jxl_token_streams_destroy(out);
  jxl_token_streams_construct_empty(out);
}

static void ApplyLZ77_Optimal(
    const jxl_histogram_params* params, size_t num_contexts,
    const jxl_token_streams* tokens, const jxl_lz77_params* lz77,
    const jxl_array_size* image_widths, jxl_token_streams* out) {
  jxl_token_streams tokens_for_cost_estimate;
  jxl_token_streams_construct_empty(&tokens_for_cost_estimate);
  ApplyLZ77_LZ77(params, num_contexts, tokens, lz77, image_widths,
                 &tokens_for_cost_estimate);
  // If greedy-LZ77 does not give better compression than no-lz77, no reason to
  // run the optimal matching.
  if (jxl_token_streams_empty(&tokens_for_cost_estimate)) {
    jxl_token_streams_destroy(&tokens_for_cost_estimate);
    jxl_token_streams_construct_empty(out);
    return;
  }
  jxl_symbol_cost_estimator sce;
  jxl_symbol_cost_estimator_init(&sce, num_contexts + 1, &tokens_for_cost_estimate, lz77,
                          false);
  jxl_token_streams_create(out, jxl_token_streams_size(tokens), tokens->ctx);
  jxl_hybrid_uint_config uint_config = jxl_hybrid_uint_config_default();
  jxl_array_float sym_cost;
  jxl_array_construct_empty(&sym_cost, tokens->ctx);
  jxl_array_u32 dist_symbols;
  jxl_array_construct_empty(&dist_symbols, tokens->ctx);
  for (size_t stream = 0; stream < jxl_token_streams_size(tokens); stream++) {
    size_t distance_multiplier =
        jxl_array_len(image_widths) > stream ? *jxl_array_at_const(image_widths, stream) : 0;
    const jxl_token_stream* in = jxl_token_streams_at_const(tokens, stream);
    jxl_token_stream* out_stream = jxl_token_streams_at(out, stream);
    // Cumulative sum of bit costs.
    if (!jxl_status_ok(jxl_array_resize_zero(&sym_cost, jxl_array_len(in) + 1))) JXL_CRASH();
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      uint32_t tok, nbits, unused_bits;
      jxl_hybrid_uint_config_encode(uint_config, jxl_array_at_const(in, i)->value, &tok, &nbits, &unused_bits);
      *jxl_array_at(&sym_cost, i + 1) = jxl_symbol_cost_estimator_bits(&sce, jxl_array_at_const(in, i)->context, tok) + nbits + *jxl_array_at(&sym_cost, i);
    }

    if (!jxl_status_ok(jxl_array_reserve(out_stream, jxl_array_len(in)))) JXL_CRASH();
    size_t max_distance = jxl_array_len(in);
    size_t min_length = lz77->min_length;
    JXL_DASSERT(min_length >= 3);
    size_t max_length = jxl_array_len(in);

    // Use next power of two as window size.
    size_t window_size = 1;
    while (window_size < max_distance && window_size < kWindowSize) {
      window_size <<= 1;
    }

    jxl_hash_chain chain;
    jxl_hash_chain_init(&chain, jxl_array_data_const(in), jxl_array_len(in), window_size, min_length,
                  max_length, distance_multiplier, tokens->ctx);

    // Total cost to encode the first N symbols.
    jxl_array_lz77_match_info prefix_costs;
    jxl_array_construct_empty(&prefix_costs, tokens->ctx);
    jxl_lz77_match_info match_init;
    match_init.len = 0;
    match_init.dist_symbol = 0;
    match_init.ctx = 0;
    match_init.total_cost = FLT_MAX;
    if (!jxl_status_ok(jxl_array_lz77_match_info_resize_fill(&prefix_costs, jxl_array_len(in) + 1, match_init))) JXL_CRASH();
    jxl_array_at(&prefix_costs, 0)->total_cost = 0;

    size_t rle_length = 0;
    size_t skip_lz77 = 0;
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      jxl_hash_chain_update(&chain, i);
      float lit_cost =
          jxl_array_at(&prefix_costs, i)->total_cost + *jxl_array_at(&sym_cost, i + 1) - *jxl_array_at(&sym_cost, i);
      if (jxl_array_at(&prefix_costs, i + 1)->total_cost > lit_cost) {
        jxl_array_at(&prefix_costs, i + 1)->dist_symbol = 0;
        jxl_array_at(&prefix_costs, i + 1)->len = 1;
        jxl_array_at(&prefix_costs, i + 1)->ctx = jxl_array_at_const(in, i)->context;
        jxl_array_at(&prefix_costs, i + 1)->total_cost = lit_cost;
      }
      if (skip_lz77 > 0) {
        skip_lz77--;
        continue;
      }
jxl_array_clear(&dist_symbols);
      jxl_hash_chain_find_matches(&chain, i, max_distance, jxl_collect_dist_symbols, &dist_symbols);
      if (jxl_array_len(&dist_symbols) <= min_length) continue;
      {
        size_t best_cost = jxl_array_back(&dist_symbols);
        for (size_t j = jxl_array_len(&dist_symbols) - 1; j >= min_length; j--) {
          if (*jxl_array_at(&dist_symbols, j) < best_cost) {
            best_cost = *jxl_array_at(&dist_symbols, j);
          }
          *jxl_array_at(&dist_symbols, j) = best_cost;
        }
      }
      for (size_t j = min_length; j < jxl_array_len(&dist_symbols); j++) {
        // Cost model that uses results from lazy LZ77.
        float lz77_cost = jxl_symbol_cost_estimator_len_cost(&sce, jxl_array_at_const(in, i)->context, j - min_length, lz77) +
                          jxl_symbol_cost_estimator_dist_cost(&sce, *jxl_array_at(&dist_symbols, j), lz77);
        float cost = jxl_array_at(&prefix_costs, i)->total_cost + lz77_cost;
        if (jxl_array_at(&prefix_costs, i + j)->total_cost > cost) {
          jxl_array_at(&prefix_costs, i + j)->len = j;
          jxl_array_at(&prefix_costs, i + j)->dist_symbol = *jxl_array_at(&dist_symbols, j) + 1;
          jxl_array_at(&prefix_costs, i + j)->ctx = jxl_array_at_const(in, i)->context;
          jxl_array_at(&prefix_costs, i + j)->total_cost = cost;
        }
      }
      // We are in a RLE sequence: skip all the symbols except the first 8 and
      // the last 8. This avoid quadratic costs for sequences with long runs of
      // the same symbol.
      if ((jxl_array_back(&dist_symbols) == 0 && distance_multiplier == 0) ||
          (jxl_array_back(&dist_symbols) == 1 && distance_multiplier != 0)) {
        rle_length++;
      } else {
        rle_length = 0;
      }
      if (rle_length >= 8 && jxl_array_len(&dist_symbols) > 9) {
        skip_lz77 = jxl_array_len(&dist_symbols) - 10;
        rle_length = 0;
      }
    }
    size_t pos = jxl_array_len(in);
    while (pos > 0) {
      bool is_lz77_length = jxl_array_at(&prefix_costs, pos)->dist_symbol != 0;
      if (is_lz77_length) {
        size_t dist_symbol = jxl_array_at(&prefix_costs, pos)->dist_symbol - 1;
        if (!jxl_status_ok(jxl_array_token_push_back(out_stream, jxl_token_make((uint32_t)(lz77->nonserialized_distance_context),
                           (uint32_t)(dist_symbol))))) {
          JXL_CRASH();
        }
      }
      uint32_t val =
          is_lz77_length
              ? (jxl_array_at(&prefix_costs, pos)->len - (uint32_t)(min_length))
              : jxl_array_at_const(in, pos - 1)->value;
      if (!jxl_status_ok(jxl_array_token_push_back(out_stream, jxl_token_make(jxl_array_at(&prefix_costs, pos)->ctx, val)))) {
        JXL_CRASH();
      }
      jxl_array_back_ptr(out_stream)->is_lz77_length = is_lz77_length;
      pos -= jxl_array_at(&prefix_costs, pos)->len;
    }
    if (!jxl_array_empty(out_stream)) {
      jxl_token* first = jxl_array_data(out_stream);
      jxl_token* last = jxl_array_data(out_stream) + jxl_array_len(out_stream);
      while (first < last) {
        --last;
        if (!(first < last)) break;
        jxl_token tmp = *first;
        *first = *last;
        *last = tmp;
        ++first;
      }
    }
    jxl_array_destroy(&prefix_costs);
    jxl_hash_chain_destroy(&chain);
  }
  jxl_array_destroy(&dist_symbols);
  jxl_array_destroy(&sym_cost);
  jxl_symbol_cost_estimator_destroy(&sce);
  jxl_token_streams_destroy(&tokens_for_cost_estimate);
  return;
}

void jxl_apply_lz77(const jxl_histogram_params* params, size_t num_contexts,
               const jxl_token_streams* tokens, const jxl_lz77_params* lz77,
               const jxl_array_size* image_widths, jxl_token_streams* out) {
  switch (params->lz77_method) {
    case kLZ77RLE:
      ApplyLZ77_RLE(num_contexts, tokens, lz77, out);
      break;
    case kLZ77:
      ApplyLZ77_LZ77(params, num_contexts, tokens, lz77, image_widths, out);
      break;
    case kLZ77Optimal:
      ApplyLZ77_Optimal(params, num_contexts, tokens, lz77, image_widths, out);
      break;
    default:
      jxl_token_streams_construct_empty(out);
      break;
  }
}
