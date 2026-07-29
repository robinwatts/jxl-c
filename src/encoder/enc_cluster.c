// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "enc_cluster.h"

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/fast_math_scalar.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "base/common.h"
#include "enc_ans.h"


static float jxl_entropy(float count, float inv_total, float total) {
  if (count == 0.0f) return 0.0f;
  if (count == total) return 0.0f;
  return -count * jxl_fast_log2f(inv_total * count);
}

static void jxl_histogram_condition_in_place(jxl_histogram* a, jxl_array_i32* counts) {
  int32_t total = 0;
  int nz_pos = -(int)(kHistogramRounding);
  for (size_t i = 0; i < jxl_array_len(counts); ++i) {
    if (*jxl_array_at(counts, i) != 0) {
      total += *jxl_array_at(counts, i);
      nz_pos = (int)(i);
    }
  }
  if (nz_pos < 0) {
jxl_array_clear(counts);
  } else {
    if (!jxl_enc_status_ok(jxl_array_resize_zero(counts, (size_t)(nz_pos) + kHistogramRounding))) {
      // Clustering path has no jxl_enc_status return; match prior OOM behavior.
      JXL_CRASH();
    }
  }
  a->total_count = total;
}

static void jxl_histogram_entropy(jxl_histogram* a, const jxl_array_i32* counts){
  a->entropy = 0.0f;
  if (a->total_count == 0) return;

  const float inv_tot = 1.0f / a->total_count;
  const float total = (float)(a->total_count);
  float entropy = 0.0f;

  for (size_t i = 0; i < jxl_array_len(counts); ++i) {
    entropy += jxl_entropy((float)(*jxl_array_at_const(counts, i)), inv_tot, total);
  }
  a->entropy += entropy;
}

static float jxl_histogram_distance(const jxl_histogram* a, const jxl_array_i32* a_counts,
                        const jxl_histogram* b,
                        const jxl_array_i32* b_counts) {
  if (a->total_count == 0 || b->total_count == 0) return 0;

  const float inv_tot = 1.0f / (a->total_count + b->total_count);
  const float total = (float)(a->total_count + b->total_count);
  float distance = 0.0f;

  for (size_t i = 0; i < JXL_MAX(jxl_array_len(a_counts), jxl_array_len(b_counts)); ++i) {
    const int32_t a_count = i < jxl_array_len(a_counts) ? *jxl_array_at_const(a_counts, i) : 0;
    const int32_t b_count = i < jxl_array_len(b_counts) ? *jxl_array_at_const(b_counts, i) : 0;
    distance += jxl_entropy((float)(a_count + b_count), inv_tot, total);
  }
  return distance - a->entropy - b->entropy;
}

static const float kInfinity = __builtin_inff();

static float jxl_histogram_kl_divergence(const jxl_histogram* actual,
                            const jxl_array_i32* actual_counts,
                            const jxl_histogram* coding,
                            const jxl_array_i32* coding_counts) {
  if (actual->total_count == 0) return 0;
  if (coding->total_count == 0) return kInfinity;

  const float coding_inv = 1.0f / coding->total_count;
  float cost = 0.0f;

  for (size_t i = 0; i < jxl_array_len(actual_counts); ++i) {
    const int32_t count = *jxl_array_at_const(actual_counts, i);
    const int32_t coding_count =
        i < jxl_array_len(coding_counts) ? *jxl_array_at_const(coding_counts, i) : 0;
    if (count == 0) continue;
    if (coding_count == 0) {
      cost += kInfinity * count;
      continue;
    }
    const float coding_prob = coding_count * coding_inv;
    cost -= count * jxl_fast_log2f(coding_prob);
  }
  return cost - actual->entropy;
}

// First step of a k-means clustering with a fancy distance metric.
static jxl_enc_status jxl_fast_cluster_histograms(
    const jxl_array_histogram* in,
    const jxl_hist_count_streams* in_counts, size_t max_histograms,
    jxl_array_histogram* out, jxl_hist_count_streams* out_counts,
    jxl_array_u32* histogram_symbols) {
  const size_t prev_histograms = jxl_array_len(out);
if (!jxl_enc_status_ok(jxl_array_reserve(out, max_histograms))) JXL_CRASH();
  JXL_RETURN_IF_ERROR(jxl_hist_count_streams_reserve(out_counts, max_histograms));
jxl_array_clear(histogram_symbols);
  JXL_RETURN_IF_ERROR(jxl_array_u32_resize_fill(
      histogram_symbols, jxl_array_len(in), (uint32_t)(max_histograms)));

  jxl_array_float dists;
  jxl_context* mm = in->ctx;
  jxl_array_construct_empty(&dists, mm);
  jxl_enc_status status = jxl_array_float_resize_fill(&dists, jxl_array_len(in), FLT_MAX);
  if (!jxl_enc_status_ok(status)) {
    jxl_array_destroy(&dists);
    return status;
  }
  size_t largest_idx = 0;
  for (size_t i = 0; i < jxl_array_len(in); i++) {
    if (jxl_array_at_const(in, i)->total_count == 0) {
      *jxl_array_at(histogram_symbols, i) = 0;
      *jxl_array_at(&dists, i) = 0.0f;
      continue;
    }
    // in is const for clustering; entropy is a cached side-field.
    jxl_histogram_entropy((jxl_histogram*)(jxl_array_at_const(in, i)), jxl_hist_count_streams_at_const(in_counts, i));
    if (jxl_array_at_const(in, i)->total_count > jxl_array_at_const(in, largest_idx)->total_count) {
      largest_idx = i;
    }
  }

  if (prev_histograms > 0) {
    for (size_t j = 0; j < prev_histograms; ++j) {
      jxl_histogram_entropy(jxl_array_at(out, j), jxl_hist_count_streams_at(out_counts, j));
    }
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      if (*jxl_array_at(&dists, i) == 0.0f) continue;
      for (size_t j = 0; j < prev_histograms; ++j) {
        *jxl_array_at(&dists, i) = JXL_MIN(
            jxl_histogram_kl_divergence(jxl_array_at_const(in, i), jxl_hist_count_streams_at_const(in_counts, i), jxl_array_at_const(out, j),
                                  jxl_hist_count_streams_at(out_counts, j)),
            *jxl_array_at(&dists, i));
      }
    }
    if (!jxl_array_empty(&dists)) {
      size_t max_i = 0;
      for (size_t i = 1; i < jxl_array_len(&dists); ++i) {
        if (*jxl_array_at(&dists, i) > *jxl_array_at(&dists, max_i)) max_i = i;
      }
      if (*jxl_array_at(&dists, max_i) > 0.0f) {
        largest_idx = max_i;
      }
    }
  }

  const float kMinDistanceForDistinct = 48.0f;
  while (jxl_array_len(out) < max_histograms) {
    *jxl_array_at(histogram_symbols, largest_idx) = jxl_array_len(out);
    if (!jxl_enc_status_ok(jxl_array_histogram_push_back(out, *jxl_array_at_const(in, largest_idx)))) JXL_CRASH();
    status = jxl_hist_count_streams_push_back(out_counts,
                                       jxl_hist_count_streams_at_const(in_counts, largest_idx));
    if (!jxl_enc_status_ok(status)) {
      jxl_array_destroy(&dists);
      return status;
    }
    *jxl_array_at(&dists, largest_idx) = 0.0f;
    largest_idx = 0;
    for (size_t i = 0; i < jxl_array_len(in); i++) {
      if (*jxl_array_at(&dists, i) == 0.0f) continue;
      *jxl_array_at(&dists, i) = JXL_MIN(jxl_histogram_distance(jxl_array_at_const(in, i), jxl_hist_count_streams_at_const(in_counts, i), jxl_array_back_ptr_const(out),
                                           jxl_hist_count_streams_back(out_counts)),
                         *jxl_array_at(&dists, i));
      if (*jxl_array_at(&dists, i) > *jxl_array_at(&dists, largest_idx)) largest_idx = i;
    }
    if (*jxl_array_at(&dists, largest_idx) < kMinDistanceForDistinct) break;
  }

  for (size_t i = 0; i < jxl_array_len(in); i++) {
    if (*jxl_array_at(histogram_symbols, i) != max_histograms) continue;
    size_t best = 0;
    float best_dist = FLT_MAX;
    for (size_t j = 0; j < jxl_array_len(out); j++) {
      float dist =
          j < prev_histograms
              ? jxl_histogram_kl_divergence(jxl_array_at_const(in, i), jxl_hist_count_streams_at_const(in_counts, i), jxl_array_at_const(out, j),
                                      jxl_hist_count_streams_at(out_counts, j))
              : jxl_histogram_distance(jxl_array_at_const(in, i), jxl_hist_count_streams_at_const(in_counts, i), jxl_array_at_const(out, j),
                                  jxl_hist_count_streams_at(out_counts, j));
      if (dist < best_dist) {
        best = j;
        best_dist = dist;
      }
    }
    JXL_ENSURE(best_dist < FLT_MAX);
    if (best >= prev_histograms) {
      jxl_histogram_add_histogram(jxl_array_at(out, best), jxl_hist_count_streams_at(out_counts, best), jxl_array_at_const(in, i), jxl_hist_count_streams_at_const(in_counts, i));
      jxl_histogram_entropy(jxl_array_at(out, best), jxl_hist_count_streams_at(out_counts, best));
    }
    *jxl_array_at(histogram_symbols, i) = best;
  }
  jxl_array_destroy(&dists);
  return jxl_enc_ok_status();
}

// Reorder histograms in *out so that the new symbols in *symbols come in
// increasing order.
static void jxl_histogram_reindex(jxl_array_histogram* out, jxl_hist_count_streams* out_counts,
                      size_t prev_histograms, jxl_array_u32* symbols) {
  jxl_array_histogram tmp;
  jxl_context* mm = out->ctx;
  jxl_array_construct_empty(&tmp, mm);
  if (!jxl_enc_status_ok(jxl_array_copy_from(&tmp, out))) JXL_CRASH();
  jxl_hist_count_streams tmp_counts;
  jxl_hist_count_streams_construct_empty(&tmp_counts);
  tmp_counts.ctx = out_counts->ctx;
  jxl_hist_count_streams_swap(&tmp_counts, out_counts);
  if (!jxl_enc_status_ok(jxl_hist_count_streams_resize(out_counts, jxl_hist_count_streams_size(&tmp_counts)))) JXL_CRASH();
  jxl_array_i32 new_index;
  jxl_array_construct_empty(&new_index, mm);
  if (!jxl_enc_status_ok(jxl_array_i32_resize_fill(&new_index, jxl_array_len(&tmp), (int32_t)(-1)))) {
    jxl_array_destroy(&new_index);
    jxl_hist_count_streams_destroy(&tmp_counts);
    jxl_array_destroy(&tmp);
    JXL_CRASH();
  }
  for (size_t i = 0; i < prev_histograms; ++i) {
    *jxl_array_at(&new_index, i) = (int32_t)(i);
  }
  int next_index = (int)(prev_histograms);
  for (size_t symbol_i = 0; symbol_i < jxl_array_len(symbols); ++symbol_i) {
    uint32_t symbol = *jxl_array_at(symbols, symbol_i);
    if (*jxl_array_at(&new_index, symbol) < 0) {
      *jxl_array_at(&new_index, symbol) = next_index;
      *jxl_array_at(out, next_index) = *jxl_array_at(&tmp, symbol);
      jxl_array_swap(jxl_hist_count_streams_at(out_counts, next_index), jxl_hist_count_streams_at(&tmp_counts, symbol));
      ++next_index;
    }
  }
  if (!jxl_enc_status_ok(jxl_array_resize_zero(out, (size_t)(next_index)))) JXL_CRASH();
  if (!jxl_enc_status_ok(jxl_hist_count_streams_resize(out_counts, (size_t)(next_index)))) JXL_CRASH();
  for (size_t symbol_i = 0; symbol_i < jxl_array_len(symbols); ++symbol_i) {
    *jxl_array_at(symbols, symbol_i) = (uint32_t)(*jxl_array_at(&new_index, *jxl_array_at(symbols, symbol_i)));
  }
  jxl_array_destroy(&new_index);
  jxl_hist_count_streams_destroy(&tmp_counts);
  jxl_array_destroy(&tmp);
}

void jxl_histogram_condition(jxl_histogram* h, jxl_array_i32* counts) {
  jxl_histogram_condition_in_place(h, counts);
}

float jxl_histogram_shannon_entropy(jxl_histogram* h, const jxl_array_i32* counts){
  jxl_histogram_entropy(h, counts);
  return h->entropy;
}

typedef struct jxl_histogram_pair {
  // validity of a pair: p.version == max(version[i], version[j])
  float cost;
  uint32_t first;
  uint32_t second;
  uint32_t version;
} jxl_histogram_pair;

// Heap ordering: returns true if `self` should appear before `other` (lower
// cost first). Uses inverted comparisons because this is a max-heap.
bool jxl_histogram_pair_heap_less_than(const jxl_histogram_pair* self, jxl_histogram_pair other) {
  if (self->cost != other.cost) return self->cost > other.cost;
  if (self->first != other.first) return self->first > other.first;
  if (self->second != other.second) return self->second > other.second;
  return self->version > other.version;
}

void jxl_histogram_pair_swap_at(jxl_histogram_pair* a, size_t i, size_t j) {
  jxl_histogram_pair tmp = a[i];
  a[i] = a[j];
  a[j] = tmp;
}
void jxl_histogram_pair_sift_up(jxl_histogram_pair* a, size_t hole) {
  while (hole > 0) {
    size_t parent = (hole - 1) / 2;
    if (!jxl_histogram_pair_heap_less_than(&a[parent], a[hole])) break;
    jxl_histogram_pair_swap_at(a, parent, hole);
    hole = parent;
  }
}
void jxl_histogram_pair_sift_down(jxl_histogram_pair* a, size_t len, size_t hole) {
  for (;;) {
    size_t child = 2 * hole + 1;
    if (child >= len) break;
    size_t right = child + 1;
    if (right < len && jxl_histogram_pair_heap_less_than(&a[child], a[right])) {
      child = right;
    }
    if (!jxl_histogram_pair_heap_less_than(&a[hole], a[child])) break;
    jxl_histogram_pair_swap_at(a, hole, child);
    hole = child;
  }
}

JXL_DEFINE_POD_ARRAY(jxl_array_histogram_pair, jxl_histogram_pair)

void jxl_histogram_pair_push_heap(jxl_array_histogram_pair* pairs) {
  size_t n = jxl_array_len(pairs);
  if (n < 2) return;
  jxl_histogram_pair_sift_up(jxl_array_data(pairs), n - 1);
}
void jxl_histogram_pair_pop_heap(jxl_array_histogram_pair* pairs) {
  size_t n = jxl_array_len(pairs);
  if (n < 2) return;
  jxl_histogram_pair_swap_at(jxl_array_data(pairs), 0, n - 1);
  jxl_histogram_pair_sift_down(jxl_array_data(pairs), n - 1, 0);
}

// Clusters similar histograms in 'in' together, the selected histograms are
// placed in 'out', and for each index in 'in', *histogram_symbols will
// indicate which of the 'out' histograms is the best approximation.
jxl_enc_status jxl_cluster_histograms(const jxl_histogram_params* params,
                         const jxl_array_histogram* in,
                         const jxl_hist_count_streams* in_counts,
                         size_t max_histograms, jxl_array_histogram* out,
                         jxl_hist_count_streams* out_counts,
                         jxl_array_u32* histogram_symbols){
  size_t prev_histograms = jxl_array_len(out);
  max_histograms = JXL_MIN(max_histograms, params->max_histograms);
  max_histograms = JXL_MIN(max_histograms, jxl_array_len(in));
  if (params->clustering == kClusteringFastest) {
    max_histograms = JXL_MIN(max_histograms, (size_t)(4));
  }

  JXL_RETURN_IF_ERROR(jxl_fast_cluster_histograms(
      in, in_counts, prev_histograms + max_histograms, out, out_counts,
      histogram_symbols));

  if (prev_histograms == 0 &&
      params->clustering == kClusteringBest) {
    for (size_t i = 0; i < jxl_array_len(out); ++i) {
      JXL_RETURN_IF_ERROR(jxl_histogram_ans_population_cost(
          jxl_array_at(out, i), jxl_hist_count_streams_at(out_counts, i), &jxl_array_at(out, i)->entropy));
    }
    uint32_t next_version = 2;
    jxl_context* mm = out->ctx;
    jxl_array_u32 version;
    jxl_array_construct_empty(&version, mm);
    jxl_array_u32 renumbering;
    jxl_array_construct_empty(&renumbering, mm);
    jxl_array_histogram_pair pairs_to_merge;
    jxl_array_construct_empty(&pairs_to_merge, mm);
    jxl_array_u32 reverse_renumbering;
    jxl_array_construct_empty(&reverse_renumbering, mm);
    jxl_enc_status status = jxl_array_u32_resize_fill(&version, jxl_array_len(out), (uint32_t)(1));
    if (!jxl_enc_status_ok(status)) {
      jxl_array_destroy(&version);
      jxl_array_destroy(&renumbering);
      jxl_array_destroy(&pairs_to_merge);
      jxl_array_destroy(&reverse_renumbering);
      return status;
    }
    status = jxl_array_resize_zero(&renumbering, jxl_array_len(out));
    if (!jxl_enc_status_ok(status)) {
      jxl_array_destroy(&version);
      jxl_array_destroy(&renumbering);
      jxl_array_destroy(&pairs_to_merge);
      jxl_array_destroy(&reverse_renumbering);
      return status;
    }
    for (size_t i = 0; i < jxl_array_len(out); ++i) *jxl_array_at(&renumbering, i) = i;

    // Try to pair up clusters if doing so reduces the total cost.

    // Create list of all pairs by increasing merging cost.
    for (uint32_t i = 0; i < jxl_array_len(out); i++) {
      for (uint32_t j = i + 1; j < jxl_array_len(out); j++) {
        jxl_histogram histo;
        jxl_array_i32 histo_counts;
        jxl_histogram_construct_empty(&histo);
        jxl_array_construct_empty(&histo_counts, mm);
        jxl_histogram_add_histogram(&histo, &histo_counts, jxl_array_at(out, i), jxl_hist_count_streams_at(out_counts, i));
        jxl_histogram_add_histogram(&histo, &histo_counts, jxl_array_at(out, j), jxl_hist_count_streams_at(out_counts, j));
        float cost;
        status = jxl_histogram_ans_population_cost(&histo, &histo_counts, &cost);
        if (!jxl_enc_status_ok(status)) {
          jxl_array_destroy(&histo_counts);
          jxl_array_destroy(&version);
          jxl_array_destroy(&renumbering);
          jxl_array_destroy(&pairs_to_merge);
          jxl_array_destroy(&reverse_renumbering);
          return status;
        }
        cost -= jxl_array_at(out, i)->entropy + jxl_array_at(out, j)->entropy;
        // Avoid enqueueing pairs that are not advantageous to merge.
        if (cost >= 0) {
          jxl_array_destroy(&histo_counts);
          continue;
        }
        if (!jxl_enc_status_ok(jxl_array_histogram_pair_push_back(&pairs_to_merge,
                           (jxl_histogram_pair){cost, i, j,
                                         JXL_MAX(*jxl_array_at(&version, i), *jxl_array_at(&version, j))}))) {
          JXL_CRASH();
        }
        jxl_histogram_pair_push_heap(&pairs_to_merge);
        jxl_array_destroy(&histo_counts);
      }
    }

    // Merge the best pair to merge, add new pairs that get formed as a
    // consequence.
    while (!jxl_array_empty(&pairs_to_merge)) {
      uint32_t first = jxl_array_at(&pairs_to_merge, 0)->first;
      uint32_t second = jxl_array_at(&pairs_to_merge, 0)->second;
      uint32_t ver = jxl_array_at(&pairs_to_merge, 0)->version;
      jxl_histogram_pair_pop_heap(&pairs_to_merge);
      jxl_array_pop_back(&pairs_to_merge);
      if (ver != JXL_MAX(*jxl_array_at(&version, first), *jxl_array_at(&version, second)) ||
          *jxl_array_at(&version, first) == 0 || *jxl_array_at(&version, second) == 0) {
        continue;
      }
      jxl_histogram_add_histogram(jxl_array_at(out, first), jxl_hist_count_streams_at(out_counts, first), jxl_array_at(out, second), jxl_hist_count_streams_at(out_counts, second));
      status = jxl_histogram_ans_population_cost(
          jxl_array_at(out, first), jxl_hist_count_streams_at(out_counts, first),
          &jxl_array_at(out, first)->entropy);
      if (!jxl_enc_status_ok(status)) {
        jxl_array_destroy(&version);
        jxl_array_destroy(&renumbering);
        jxl_array_destroy(&pairs_to_merge);
        jxl_array_destroy(&reverse_renumbering);
        return status;
      }
      for (size_t item_i = 0; item_i < jxl_array_len(&renumbering); ++item_i) {
        uint32_t* item = jxl_array_at(&renumbering, item_i);
        if (*item == second) {
          *item = first;
        }
      }
      *jxl_array_at(&version, second) = 0;
      *jxl_array_at(&version, first) = next_version++;
      for (uint32_t j = 0; j < jxl_array_len(out); j++) {
        if (j == first) continue;
        if (*jxl_array_at(&version, j) == 0) continue;
        jxl_histogram histo;
        jxl_array_i32 histo_counts;
        jxl_histogram_construct_empty(&histo);
        jxl_array_construct_empty(&histo_counts, mm);
        jxl_histogram_add_histogram(&histo, &histo_counts, jxl_array_at(out, first), jxl_hist_count_streams_at(out_counts, first));
        jxl_histogram_add_histogram(&histo, &histo_counts, jxl_array_at(out, j), jxl_hist_count_streams_at(out_counts, j));
        float merge_cost;
        status = jxl_histogram_ans_population_cost(&histo, &histo_counts, &merge_cost);
        if (!jxl_enc_status_ok(status)) {
          jxl_array_destroy(&histo_counts);
          jxl_array_destroy(&version);
          jxl_array_destroy(&renumbering);
          jxl_array_destroy(&pairs_to_merge);
          jxl_array_destroy(&reverse_renumbering);
          return status;
        }
        merge_cost -= jxl_array_at(out, first)->entropy + jxl_array_at(out, j)->entropy;
        // Avoid enqueueing pairs that are not advantageous to merge.
        if (merge_cost >= 0) {
          jxl_array_destroy(&histo_counts);
          continue;
        }
        if (!jxl_enc_status_ok(jxl_array_histogram_pair_push_back(&pairs_to_merge,
                           (jxl_histogram_pair){merge_cost, JXL_MIN(first, j),
                                         JXL_MAX(first, j),
                                         JXL_MAX(*jxl_array_at(&version, first),
                                                 *jxl_array_at(&version, j))}))) {
          JXL_CRASH();
        }
        jxl_histogram_pair_push_heap(&pairs_to_merge);
        jxl_array_destroy(&histo_counts);
      }
    }
    status = jxl_array_u32_resize_fill(&reverse_renumbering, jxl_array_len(out),
                             (uint32_t)(-1));
    if (!jxl_enc_status_ok(status)) {
      jxl_array_destroy(&version);
      jxl_array_destroy(&renumbering);
      jxl_array_destroy(&pairs_to_merge);
      jxl_array_destroy(&reverse_renumbering);
      return status;
    }
    size_t num_alive = 0;
    for (size_t i = 0; i < jxl_array_len(out); i++) {
      if (*jxl_array_at(&version, i) == 0) continue;
      *jxl_array_at(out, num_alive) = *jxl_array_at(out, i);
      if (!jxl_enc_status_ok(jxl_array_copy_from(jxl_hist_count_streams_at(out_counts, num_alive),
                                  jxl_hist_count_streams_at(out_counts, i)))) {
        JXL_CRASH();
      }
      ++num_alive;
      *jxl_array_at(&reverse_renumbering, i) = num_alive - 1;
    }
    if (!jxl_enc_status_ok(jxl_array_resize_zero(out, num_alive))) JXL_CRASH();
    if (!jxl_enc_status_ok(jxl_hist_count_streams_resize(out_counts, num_alive))) JXL_CRASH();
    for (size_t item_i = 0; item_i < jxl_array_len(histogram_symbols); ++item_i) {
      *jxl_array_at(histogram_symbols, item_i) =
          *jxl_array_at(&reverse_renumbering, *jxl_array_at(&renumbering, *jxl_array_at(histogram_symbols, item_i)));
    }
    jxl_array_destroy(&version);
    jxl_array_destroy(&renumbering);
    jxl_array_destroy(&pairs_to_merge);
    jxl_array_destroy(&reverse_renumbering);
  }

  // Convert the context map to a canonical form.
  jxl_histogram_reindex(out, out_counts, prev_histograms, histogram_symbols);
  return jxl_enc_ok_status();
}
